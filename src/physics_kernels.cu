#include <cuda_runtime.h>
#include <device_launch_parameters.h>

extern "C" {

struct Particle {
    float2 pos;
    float2 vel;
};

#define BLOCK_SIZE 256


__global__ void nbody_step_kernel(Particle* in, Particle* out, int n, float dt, float gravity) {
    __shared__ float2 shared_pos[BLOCK_SIZE];

    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= n) return;

    float2 my_pos = in[idx].pos;
    float2 my_vel = in[idx].vel;
    float2 acc = make_float2(0.0f, 0.0f);


    for (int i = 0; i < gridDim.x; i++) {

        int tile_idx = i * BLOCK_SIZE + threadIdx.x;
        if (tile_idx < n) {
            shared_pos[threadIdx.x] = in[tile_idx].pos;
        } else {
            shared_pos[threadIdx.x] = make_float2(0.0f, 0.0f);
        }
        __syncthreads();


        #pragma unroll
        for (int j = 0; j < BLOCK_SIZE; j++) {
            int other_idx = i * BLOCK_SIZE + j;
            if (other_idx < n && other_idx != idx) {
                float2 other_p = shared_pos[j];
                float2 diff = make_float2(other_p.x - my_pos.x, other_p.y - my_pos.y);
                float distSq = diff.x * diff.x + diff.y * diff.y + 0.001f; 
                float invDist = rsqrtf(distSq);
                float force = gravity * invDist * invDist;
                acc.x += force * diff.x * invDist;
                acc.y += force * diff.y * invDist;
            }
        }
        __syncthreads();
    }


    my_vel.x += acc.x * dt;
    my_vel.y += acc.y * dt;
    

    my_vel.x *= 0.999f;
    my_vel.y *= 0.999f;

    my_pos.x += my_vel.x * dt;
    my_pos.y += my_vel.y * dt;


    if (my_pos.x < -1.0f) { my_pos.x = -1.0f; my_vel.x *= -0.5f; }
    if (my_pos.x > 1.0f) { my_pos.x = 1.0f; my_vel.x *= -0.5f; }
    if (my_pos.y < -1.0f) { my_pos.y = -1.0f; my_vel.y *= -0.5f; }
    if (my_pos.y > 1.0f) { my_pos.y = 1.0f; my_vel.y *= -0.5f; }
    

    out[idx].pos = my_pos;
    out[idx].vel = my_vel;
}


__global__ void render_physics_kernel(float* dest, Particle* particles, int n, int width, int height) {
  
    int tid = blockIdx.x * blockDim.x + threadIdx.x;
    if (tid < width * height) {
        dest[tid] = 0.0f;
    }
    __syncthreads();


    if (tid < n) {
        float2 p = particles[tid].pos;
        
     
        int gx = (int)((p.x + 1.0f) * 0.5f * (width - 1));
        int gy = (int)((p.y + 1.0f) * 0.5f * (height - 1));

        if (gx >= 0 && gx < width && gy >= 0 && gy < height) {
        
            atomicAdd(&dest[gy * width + gx], 0.5f);
            
         
            if (gx > 0) atomicAdd(&dest[gy * width + (gx - 1)], 0.1f);
            if (gx < width - 1) atomicAdd(&dest[gy * width + (gx + 1)], 0.1f);
            if (gy > 0) atomicAdd(&dest[(gy - 1) * width + gx], 0.1f);
            if (gy < height - 1) atomicAdd(&dest[(gy + 1) * width + gx], 0.1f);
        }
    }
}

}
