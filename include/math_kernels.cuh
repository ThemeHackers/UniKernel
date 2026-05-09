#ifndef MATH_KERNELS_CUH
#define MATH_KERNELS_CUH

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Searches for primes in a range
 */
__global__ void prime_search_kernel(int *found_primes, int *count, int start, int range, int shared_limit);

#ifdef __cplusplus
}
#endif

#endif
