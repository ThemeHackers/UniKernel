#ifndef MATH_KERNELS_CUH
#define MATH_KERNELS_CUH

#include <cuda_runtime.h>

#ifdef __cplusplus
extern "C" {
#endif

__global__ void prime_search_kernel(int *found_primes, int *count, int start, int range, int shared_limit);
__global__ void mandelbrot_kernel(float *dest, int width, int height, float zoom, float offsetX, float offsetY, int maxIter);

#ifdef __cplusplus
}
#endif

#endif
