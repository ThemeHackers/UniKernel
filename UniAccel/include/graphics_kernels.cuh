#ifndef GRAPHICS_KERNELS_CUH
#define GRAPHICS_KERNELS_CUH

#include <cuda_runtime.h>

#ifdef __cplusplus
extern "C" {
#endif

__global__ void render_3d_kernel(float *dest, int width, int height, float time);

#ifdef __cplusplus
}
#endif

#endif
