#ifndef SIGNAL_KERNELS_CUH
#define SIGNAL_KERNELS_CUH

#include <cuda_runtime.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Image grayscale filter
 */
__global__ void vision_filter_kernel(uchar4 *pixels, int width, int height);

/**
 * @brief Pattern matching in byte array
 */
__global__ void pattern_match_kernel(unsigned char *data, int data_len, int pat_len, int *found_idx);

#ifdef __cplusplus
}
#endif

#endif
