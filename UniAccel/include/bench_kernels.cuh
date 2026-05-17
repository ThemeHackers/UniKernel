#ifndef BENCH_KERNELS_CUH
#define BENCH_KERNELS_CUH

#include <cuda_runtime.h>

#ifdef __cplusplus
extern "C" {
#endif

__global__ void bench_matmul_kernel(const float *A, const float *B,
                                          float *C, int N);

__global__ void shared_mem_bench_kernel(float *out);

__global__ void atomic_bench_kernel(int *counters, int iterations, int mode);

__global__ void memory_bandwidth_kernel(const float4 *src, float4 *dst, int n);

__global__ void null_kernel();

#ifdef __cplusplus
}
#endif

#endif 
