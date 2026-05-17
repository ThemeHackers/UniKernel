#ifndef SIGNAL_KERNELS_CUH
#define SIGNAL_KERNELS_CUH

#include <cuda_runtime.h>

#ifdef __cplusplus
extern "C" {
#endif

__global__ void dft_kernel(const float *real_in, const float *imag_in,
                            float *magnitude_out, int n);

__global__ void fft_bitrev_kernel(float *real, float *imag, int n, int bits);
__global__ void fft_butterfly_kernel(float *real, float *imag, int n, int step);

__global__ void fft_shared_kernel(float *real, float *imag, int n, int bits);

__global__ void vision_filter_kernel(uchar4 *pixels, int width, int height);

__global__ void pattern_match_kernel(const unsigned char *data, int data_len,
                                     int pat_len, int *found_idx);

#ifdef __cplusplus
}
#endif

#endif 
