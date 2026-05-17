#include "include/cuda_utils.cuh"

#include "include/graphics_kernels.cuh"
#include "include/crypto_kernels.cuh"
#include "include/math_kernels.cuh"
#include "include/signal_kernels.cuh"
#include "include/bench_kernels.cuh"

extern "C" {
    __global__ void nbody_step_kernel(void*, void*, int, float, float);
    __global__ void clear_frame_kernel(float*, int);
    __global__ void render_physics_kernel(float*, void*, int, int, int);
    __global__ void rsa_2048_kernel(uint32_t*, uint32_t*, uint32_t*,
                                    uint32_t*, uint32_t, int);
}

#include <cstring>
void initGraphicsConstants() {

    float3 axis = { 1.0f, 0.8f, 0.3f };
    float len = sqrtf(axis.x*axis.x + axis.y*axis.y + axis.z*axis.z);
    axis.x /= len; axis.y /= len; axis.z /= len;

  
    CUDA_CHECK(cudaMemcpyToSymbol(c_axis, &axis, sizeof(float3)));
}

int main() {
    initGraphicsConstants();

    return 0;
}
