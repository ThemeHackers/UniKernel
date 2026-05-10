#include "graphics_kernels.cuh"
#include <math_constants.h>

extern "C" {

static __device__ float sdTorus(float3 p, float2 t) {
    float2 q = make_float2(length(make_float3(p.x, 0, p.z)) - t.x, p.y);
    return sqrtf(q.x * q.x + q.y * q.y) - t.y;
}

static __device__ float3 rotate(float3 p, float angle, float3 axis) {
    float s = sinf(angle);
    float c = cosf(angle);
    float oc = 1.0f - c;
    

    float3 r;
    r.x = p.x * (c + axis.x*axis.x*oc) + p.y * (axis.x*axis.y*oc - axis.z*s) + p.z * (axis.x*axis.z*oc + axis.y*s);
    r.y = p.x * (axis.y*axis.x*oc + axis.z*s) + p.y * (c + axis.y*axis.y*oc) + p.z * (axis.y*axis.z*oc - axis.x*s);
    r.z = p.x * (axis.z*axis.x*oc - axis.y*s) + p.y * (axis.z*axis.y*oc + axis.x*s) + p.z * (c + axis.z*axis.z*oc);
    return r;
}

static __device__ float3 getNormal(float3 p, float time) {
    float2 e = make_float2(0.001f, 0.0f);
    float3 n = make_float3(
        sdTorus(rotate(add_float3(p, make_float3(e.x, e.y, e.y)), time * 1.2f, normalize(make_float3(1.0f, 0.8f, 0.3f))), make_float2(0.8f, 0.35f)) -
        sdTorus(rotate(add_float3(p, make_float3(-e.x, e.y, e.y)), time * 1.2f, normalize(make_float3(1.0f, 0.8f, 0.3f))), make_float2(0.8f, 0.35f)),
        sdTorus(rotate(add_float3(p, make_float3(e.y, e.x, e.y)), time * 1.2f, normalize(make_float3(1.0f, 0.8f, 0.3f))), make_float2(0.8f, 0.35f)) -
        sdTorus(rotate(add_float3(p, make_float3(e.y, -e.x, e.y)), time * 1.2f, normalize(make_float3(1.0f, 0.8f, 0.3f))), make_float2(0.8f, 0.35f)),
        sdTorus(rotate(add_float3(p, make_float3(e.y, e.y, e.x)), time * 1.2f, normalize(make_float3(1.0f, 0.8f, 0.3f))), make_float2(0.8f, 0.35f)) -
        sdTorus(rotate(add_float3(p, make_float3(e.y, e.y, -e.x)), time * 1.2f, normalize(make_float3(1.0f, 0.8f, 0.3f))), make_float2(0.8f, 0.35f))
    );
    return normalize(n);
}

__global__ __launch_bounds__(256, 1)
void render_3d_kernel(float *dest, int width, int height, float time) {
    const int x = blockIdx.x * blockDim.x + threadIdx.x;
    const int y = blockIdx.y * blockDim.y + threadIdx.y;

    if (x >= width || y >= height) return;

    float2 uv = make_float2(((float)x / width) * 2.0f - 1.0f, ((float)y / height) * 2.0f - 1.0f);
    float3 ro = make_float3(0, 0, -3.0f);
    float3 rd = normalize(make_float3(uv.x, uv.y, 1.5f));

    float t = 0.0f;
    bool hit = false;
    float3 p;
    for (int i = 0; i < 64; i++) {
        p = add_float3(ro, mul_float3(rd, t));
        float3 rp = rotate(p, time * 1.2f, normalize(make_float3(1.0f, 0.8f, 0.3f)));
        float dist = sdTorus(rp, make_float2(0.8f, 0.35f));
        if (dist < 0.001f) { hit = true; break; }
        if (t > 10.0f) break;
        t += dist;
    }

    if (hit) {
        float3 n = getNormal(p, time);
        float3 lp = make_float3(2.0f, 2.0f, -2.0f);
        float3 l = normalize(add_float3(lp, mul_float3(p, -1.0f)));
        float diff = fmaxf(0.0f, n.x*l.x + n.y*l.y + n.z*l.z);
        float amb = 0.1f;
        dest[y * width + x] = diff + amb;
    } else {
        dest[y * width + x] = 0.0f;
    }
}

} 
