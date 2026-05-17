#ifndef CUDA_UTILS_CUH
#define CUDA_UTILS_CUH

#include <cuda_runtime.h>
#include <device_launch_parameters.h>
#include <stdio.h>

#define CUDA_CHECK(call)                                                              \
    do {                                                                              \
        cudaError_t err = call;                                                       \
        if (err != cudaSuccess) {                                                     \
            fprintf(stderr, "CUDA Error at %s:%d - %s (%s)\n",                        \
                    __FILE__, __LINE__, cudaGetErrorString(err), cudaGetErrorName(err)); \
        }                                                                             \
    } while (0)

#define WARP_SIZE 32
#define FULL_MASK 0xFFFFFFFF

__device__ __forceinline__ float3 add_float3(float3 a, float3 b) {
    return make_float3(a.x + b.x, a.y + b.y, a.z + b.z);
}

__device__ __forceinline__ float3 sub_float3(float3 a, float3 b) {
    return make_float3(a.x - b.x, a.y - b.y, a.z - b.z);
}

__device__ __forceinline__ float3 mul_float3(float3 a, float s) {
    return make_float3(a.x * s, a.y * s, a.z * s);
}

__device__ __forceinline__ float length_sq(float3 v) {
    return v.x*v.x + v.y*v.y + v.z*v.z;
}

__device__ __forceinline__ float length(float3 v) {
    return sqrtf(length_sq(v));
}

__device__ __forceinline__ float3 normalize(float3 v) {
    float invLen = rsqrtf(length_sq(v) + 1e-10f);
    return mul_float3(v, invLen);
}

__device__ __forceinline__ float warpReduceSum(float val) {
    for (int offset = WARP_SIZE / 2; offset > 0; offset /= 2)
        val += __shfl_down_sync(FULL_MASK, val, offset);
    return val;
}

extern __constant__ float3 c_axis;

#endif
