#ifndef PHYSICS_KERNELS_CUH
#define PHYSICS_KERNELS_CUH

#include <cuda_runtime.h>

#ifdef __cplusplus
extern "C" {
#endif

struct Particle {
    float2 pos;
    float2 vel;
};

__global__ void nbody_step_kernel(const Particle *in, Particle *out, int n, float dt, float gravity);
__global__ void clear_frame_kernel(float *dest, int total);
__global__ void render_physics_kernel(float *dest, const Particle *particles, int n, int width, int height);

#ifdef __cplusplus
}
#endif

#endif
