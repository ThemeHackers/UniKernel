

#include "bench_kernels.cuh"
#include <cuda_runtime.h>

extern "C" {

#define TILE_SIZE 16

__global__ __launch_bounds__(TILE_SIZE * TILE_SIZE, 2)
void bench_matmul_kernel(const float* __restrict__ A,
                         const float* __restrict__ B,
                               float* __restrict__ C,
                         int N)
{
    __shared__ float sA[TILE_SIZE][TILE_SIZE];
    __shared__ float sB[TILE_SIZE][TILE_SIZE];

    int row = blockIdx.y * TILE_SIZE + threadIdx.y;
    int col = blockIdx.x * TILE_SIZE + threadIdx.x;
    float sum = 0.0f;

    int nTiles = (N + TILE_SIZE - 1) / TILE_SIZE;
    for (int m = 0; m < nTiles; m++) {
        int aCol = m * TILE_SIZE + threadIdx.x;
        int bRow = m * TILE_SIZE + threadIdx.y;

        sA[threadIdx.y][threadIdx.x] = (row < N && aCol < N)
            ? __ldg(&A[row * N + aCol]) : 0.0f;
        sB[threadIdx.y][threadIdx.x] = (col < N && bRow < N)
            ? __ldg(&B[bRow * N + col]) : 0.0f;
        __syncthreads();

        #pragma unroll
        for (int k = 0; k < TILE_SIZE; k++)
            sum += sA[threadIdx.y][k] * sB[k][threadIdx.x];
        __syncthreads();
    }

    if (row < N && col < N)
        C[row * N + col] = sum;
}

__global__ __launch_bounds__(1024, 1)
void shared_mem_bench_kernel(float* __restrict__ out)
{
    __shared__ float sData[1024];
    int tid = threadIdx.x;

    
    sData[tid] = (float)tid + 0.5f;
    __syncthreads();

    
    float val = sData[tid];
    #pragma unroll
    for (int stride = 512; stride > 0; stride >>= 1) {
        if (tid < stride)
            sData[tid] = val = val + sData[tid + stride];
        __syncthreads();
    }

    
    if (tid == 0) *out = sData[0];
}

__global__ __launch_bounds__(256, 2)
void atomic_bench_kernel(int* __restrict__ counters,
                         int iterations,
                         int mode)
{
    int tid  = blockIdx.x * blockDim.x + threadIdx.x;
    
    int slot = (mode == 0) ? 0 : (tid / 32) % 32;

    #pragma unroll 4
    for (int i = 0; i < iterations; i++)
        atomicAdd(&counters[slot], 1);
}

__global__ __launch_bounds__(256, 4)
void memory_bandwidth_kernel(const float4* __restrict__ src,
                                   float4* __restrict__ dst,
                             int n)
{
    int tid = blockIdx.x * blockDim.x + threadIdx.x;
    if (tid < n)
        dst[tid] = __ldg(&src[tid]);    
}

__global__ void null_kernel() { }

} 
