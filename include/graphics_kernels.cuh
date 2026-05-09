#ifndef GRAPHICS_KERNELS_CUH
#define GRAPHICS_KERNELS_CUH

#include <cuda_runtime.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Renders a 3D scene
 */
__global__ void render_3d_kernel(float *dest, int width, int height, float time);

/**
 * @brief Matrix multiplication benchmark
 */
__global__ void matrix_mul_kernel(float *A, float *B, float *C, int N);

#ifdef __cplusplus
}
#endif

#endif
