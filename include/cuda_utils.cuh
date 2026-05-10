#ifndef CUDA_UTILS_CUH
#define CUDA_UTILS_CUH

#include <cstdio>
#include <cuda_runtime.h>
#include <math.h>


#define CUDA_CHECK(call) \
    do { \
        cudaError_t err = call; \
        if (err != cudaSuccess) { \
            fprintf(stderr, "CUDA Error at %s:%d: %s\n", __FILE__, __LINE__, cudaGetErrorString(err)); \
            exit(EXIT_FAILURE); \
        } \
    } while (0)


#define FULL_MASK 0xFFFFFFFF


#if __CUDA_ARCH__ >= 700
    #define HAS_INDEPENDENT_SCHEDULING
#endif

#if __CUDA_ARCH__ >= 800
    #define HAS_AMPERE_FEATURES
#endif


static __device__ __inline__ float3 add_float3(float3 a, float3 b) {
    return make_float3(a.x + b.x, a.y + b.y, a.z + b.z);
}

static __device__ __inline__ float3 mul_float3(float3 a, float s) {
    return make_float3(a.x * s, a.y * s, a.z * s);
}

static __device__ __inline__ float length(float3 v) {
    return sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
}

static __device__ __inline__ float3 normalize(float3 v) {
    float invLen = 1.0f / length(v);
    return mul_float3(v, invLen);
}

#endif
