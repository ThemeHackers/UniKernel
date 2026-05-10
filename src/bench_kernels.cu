#include "bench_kernels.cuh"

extern "C" {

#define TILE_SIZE 16

__global__ void matrix_mul_kernel(float *A, float *B, float *C, int N) {
    __shared__ float sA[TILE_SIZE][TILE_SIZE];
    __shared__ float sB[TILE_SIZE][TILE_SIZE];

    int bx = blockIdx.x; int by = blockIdx.y;
    int tx = threadIdx.x; int ty = threadIdx.y;

    int row = by * TILE_SIZE + ty;
    int col = bx * TILE_SIZE + tx;

    float sum = 0;
    for (int m = 0; m < (N + TILE_SIZE - 1) / TILE_SIZE; ++m) {
        if (row < N && m * TILE_SIZE + tx < N)
            sA[ty][tx] = A[row * N + m * TILE_SIZE + tx];
        else
            sA[ty][tx] = 0;

        if (col < N && m * TILE_SIZE + ty < N)
            sB[ty][tx] = B[(m * TILE_SIZE + ty) * N + col];
        else
            sB[ty][tx] = 0;

        __syncthreads();

        #pragma unroll
        for (int k = 0; k < TILE_SIZE; ++k) {
            sum += sA[ty][k] * sB[k][tx];
        }
        __syncthreads();
    }

    if (row < N && col < N) {
        C[row * N + col] = sum;
    }
}

__global__ void shared_mem_bench_kernel(float *out) {
    __shared__ float sData[1024];
    int tid = threadIdx.x;
    sData[tid] = (float)tid;
    __syncthreads();
    if (tid == 0) *out = sData[512];
}

__global__ void atomic_bench_kernel(int *counter, int iterations) {
    for (int i = 0; i < iterations; i++) {
        atomicAdd(counter, 1);
    }
}

__global__ void null_kernel() { }

} 
