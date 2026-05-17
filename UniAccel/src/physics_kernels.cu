

#include <cuda_runtime.h>
#include <device_launch_parameters.h>

extern "C" {

struct Particle {
    float2 pos;
    float2 vel;
};

#define BLOCK_SIZE 256

__global__ __launch_bounds__(BLOCK_SIZE, 1)
void nbody_step_kernel(const Particle* __restrict__ in,
                             Particle* __restrict__ out,
                       int n, float dt, float gravity)
{
    __shared__ float2 shared_pos[BLOCK_SIZE];

    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= n) return;

    float2 my_pos = __ldg(&in[idx].pos);
    float2 my_vel = __ldg(&in[idx].vel);
    float2 acc    = {0.0f, 0.0f};

    for (int tile = 0; tile < gridDim.x; tile++) {
        int tile_idx = tile * BLOCK_SIZE + threadIdx.x;
        shared_pos[threadIdx.x] = (tile_idx < n)
            ? __ldg(&in[tile_idx].pos)
            : make_float2(1e10f, 1e10f);   
        __syncthreads();

        #pragma unroll 8
        for (int j = 0; j < BLOCK_SIZE; j++) {
            int other = tile * BLOCK_SIZE + j;
            if (other < n && other != idx) {
                float2 d = { shared_pos[j].x - my_pos.x,
                             shared_pos[j].y - my_pos.y };
                float distSq  = d.x*d.x + d.y*d.y + 1e-4f; 
                float invDist  = rsqrtf(distSq);
                float force    = gravity * invDist * invDist * invDist;
                acc.x += force * d.x;
                acc.y += force * d.y;
            }
        }
        __syncthreads();
    }

    
    my_vel.x = (my_vel.x + acc.x * dt) * 0.999f;
    my_vel.y = (my_vel.y + acc.y * dt) * 0.999f;
    my_pos.x += my_vel.x * dt;
    my_pos.y += my_vel.y * dt;

    
    if (my_pos.x < -1.0f) { my_pos.x = -1.0f; my_vel.x = fabsf(my_vel.x) * 0.5f; }
    if (my_pos.x >  1.0f) { my_pos.x =  1.0f; my_vel.x = -fabsf(my_vel.x) * 0.5f; }
    if (my_pos.y < -1.0f) { my_pos.y = -1.0f; my_vel.y = fabsf(my_vel.y) * 0.5f; }
    if (my_pos.y >  1.0f) { my_pos.y =  1.0f; my_vel.y = -fabsf(my_vel.y) * 0.5f; }

    out[idx].pos = my_pos;
    out[idx].vel = my_vel;
}

__global__ __launch_bounds__(256, 4)
void clear_frame_kernel(float* __restrict__ dest, int total)
{
    int tid = blockIdx.x * blockDim.x + threadIdx.x;
    if (tid < total)
        dest[tid] = 0.0f;
}

__global__ __launch_bounds__(BLOCK_SIZE, 2)
void render_physics_kernel(float* __restrict__ dest,
                           const Particle* __restrict__ particles,
                           int n, int width, int height)
{
    int tid = blockIdx.x * blockDim.x + threadIdx.x;
    if (tid >= n) return;

    float2 p = __ldg(&particles[tid].pos);

    
    int gx = __float2int_rn((p.x + 1.0f) * 0.5f * (float)(width  - 1));
    int gy = __float2int_rn((p.y + 1.0f) * 0.5f * (float)(height - 1));

    
    gx = max(0, min(gx, width  - 1));
    gy = max(0, min(gy, height - 1));

    
    atomicAdd(&dest[gy * width + gx], 0.5f);

    
    if (gx > 0)          atomicAdd(&dest[gy * width + (gx - 1)], 0.1f);
    if (gx < width  - 1) atomicAdd(&dest[gy * width + (gx + 1)], 0.1f);
    if (gy > 0)          atomicAdd(&dest[(gy - 1) * width + gx], 0.1f);
    if (gy < height - 1) atomicAdd(&dest[(gy + 1) * width + gx], 0.1f);
}

} 
