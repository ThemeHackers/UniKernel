#include "graphics_kernels.cuh"
#include "cuda_utils.cuh"
#include <math_constants.h>
extern "C" {
__device__ __forceinline__
float sdTorus(float3 p, float2 t)
{
    float2 q = { length(make_float3(p.x, 0.f, p.z)) - t.x, p.y };
    return sqrtf(q.x*q.x + q.y*q.y) - t.y;
}
__device__ __forceinline__
float3 rotateRod(float3 p, float angle, float3 ax)
{
    float s, c;
    sincosf(angle, &s, &c);
    float oc   = 1.0f - c;
    float dot  = ax.x*p.x + ax.y*p.y + ax.z*p.z;
    float3 cr  = { ax.y*p.z - ax.z*p.y,
                   ax.z*p.x - ax.x*p.z,
                   ax.x*p.y - ax.y*p.x };
    return {
        p.x*c + cr.x*s + ax.x*dot*oc,
        p.y*c + cr.y*s + ax.y*dot*oc,
        p.z*c + cr.z*s + ax.z*dot*oc
    };
}
__device__ __forceinline__
float sceneSDF(float3 p, float time)
{
    float3 rp = rotateRod(p, time * 1.2f, c_axis);
    return sdTorus(rp, {0.8f, 0.35f});
}
__device__
float3 getNormal(float3 p, float time)
{
    const float eps = 0.001f;
    float3 rp = rotateRod(p, time * 1.2f, c_axis); 
    float3 n = {
        sdTorus(rotateRod({p.x+eps, p.y, p.z}, time*1.2f, c_axis), {0.8f,0.35f})
      - sdTorus(rotateRod({p.x-eps, p.y, p.z}, time*1.2f, c_axis), {0.8f,0.35f}),
        sdTorus(rotateRod({p.x, p.y+eps, p.z}, time*1.2f, c_axis), {0.8f,0.35f})
      - sdTorus(rotateRod({p.x, p.y-eps, p.z}, time*1.2f, c_axis), {0.8f,0.35f}),
        sdTorus(rotateRod({p.x, p.y, p.z+eps}, time*1.2f, c_axis), {0.8f,0.35f})
      - sdTorus(rotateRod({p.x, p.y, p.z-eps}, time*1.2f, c_axis), {0.8f,0.35f})
    };
    return normalize(n);
}
__global__ __launch_bounds__(256, 1)
void render_3d_kernel(float* __restrict__ dest,
                      int width, int height, float time)
{
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= width || y >= height) return;
    float2 uv = {
        ((float)x / (float)width)  * 2.0f - 1.0f,
        ((float)y / (float)height) * 2.0f - 1.0f
    };
    float3 ro = {0.f, 0.f, -3.0f};
    float3 rd = normalize(make_float3(uv.x, uv.y, 1.5f));
    float t    = 0.0f;
    float prev = 0.0f;                  
    float omega = 1.2f;                 
    bool  hit  = false;
    float3 p;
    for (int i = 0; i < 80; i++) {     
        p = add_float3(ro, mul_float3(rd, t));
        float d = sceneSDF(p, time);
        float step = d * omega;
        if (step < 0.001f) { hit = true; break; }
        if (t > 10.0f) break;
        if (omega > 1.0f && (prev + d) < step) {
            step = d;
            omega = 1.0f;
        }
        prev = d;
        t   += step;
    }
    float val = 0.0f;
    if (hit) {
        float3 n   = getNormal(p, time);
        float3 lp  = {2.0f, 2.0f, -2.0f};
        float3 l   = normalize(add_float3(lp, mul_float3(p, -1.0f)));
        float  diff = fmaxf(0.0f, n.x*l.x + n.y*l.y + n.z*l.z);
        float3 ref  = add_float3(mul_float3(n, 2.0f*(n.x*l.x+n.y*l.y+n.z*l.z)),
                                  mul_float3(l, -1.0f));
        float3 v    = mul_float3(rd, -1.0f);
        float spec  = powf(fmaxf(0.0f, ref.x*v.x+ref.y*v.y+ref.z*v.z), 32.0f);
        val = 0.05f + diff * 0.75f + spec * 0.2f;
    }
    dest[y * width + x] = val;
}
}
