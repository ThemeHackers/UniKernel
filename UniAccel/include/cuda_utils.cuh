#ifndef CUDA_UTILS_CUH
#define CUDA_UTILS_CUH

#include <cuda_runtime.h>
#include <stdio.h>

#define CUDA_CHECK(call) \
    do { \
        cudaError_t err = call; \
        if (err != cudaSuccess) { \
            fprintf(stderr, "CUDA Error at %s:%d - %s\n", __FILE__, __LINE__, cudaGetErrorString(err)); \
        } \
    } while (0)

__device__ __forceinline__ float3 add_float3(float3 a, float3 b) {
    return make_float3(a.x + b.x, a.y + b.y, a.z + b.z);
}

__device__ __forceinline__ float3 mul_float3(float3 a, float s) {
    return make_float3(a.x * s, a.y * s, a.z * s);
}

__device__ __forceinline__ float length(float3 v) {
    return sqrtf(v.x*v.x + v.y*v.y + v.z*v.z);
}

__device__ __forceinline__ float3 normalize(float3 v) {
    float len = length(v);
    return (len > 0.0f) ? make_float3(v.x/len, v.y/len, v.z/len) : make_float3(0.0f, 0.0f, 0.0f);
}

extern __constant__ float3 c_axis;

#endif
