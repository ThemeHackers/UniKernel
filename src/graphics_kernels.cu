#include "graphics_kernels.cuh"
#include <math_constants.h>

extern "C" {

__global__ void render_3d_kernel(float *dest, int width, int height, float time) {
    const int x = blockIdx.x * blockDim.x + threadIdx.x;
    const int y = blockIdx.y * blockDim.y + threadIdx.y;

    if (x >= width || y >= height) return;

    float2 uv = make_float2((float)x / width, (float)y / height);
    float3 ro = make_float3(0, 0, -3.0f + sinf(time) * 0.5f);
    float3 rd = normalize(make_float3(uv.x * 2.0f - 1.0f, uv.y * 2.0f - 1.0f, 1.0f));

   
    float d = 100.0f;
    float3 p = ro;
    for (int i = 0; i < 32; i++) {
        float dist = length(p) - 1.0f;
        if (dist < 0.01f) {
            d = (float)i / 32.0f;
            break;
        }
        p = add_float3(p, mul_float3(rd, dist));
    }

    dest[y * width + x] = 1.0f - d;
}

} 
