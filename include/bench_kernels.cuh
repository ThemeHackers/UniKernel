#ifndef BENCH_KERNELS_CUH
#define BENCH_KERNELS_CUH

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Shared memory latency benchmark
 */
__global__ void shared_mem_bench_kernel(float *out);

/**
 * @brief Atomic operations benchmark
 */
__global__ void atomic_bench_kernel(int *counter, int iterations);

/**
 * @brief Empty kernel for launch latency test
 */
__global__ void null_kernel();

#ifdef __cplusplus
}
#endif

#endif
