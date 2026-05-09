#include "bench_kernels.cuh"

extern "C" {

__global__ void matrix_mul_kernel(float *A, float *B, float *C, int N) {
    int row = blockIdx.y * blockDim.y + threadIdx.y;
    int col = blockIdx.x * blockDim.x + threadIdx.x;
    
    if (row < N && col < N) {
        float sum = 0.0f;
        for (int i = 0; i < N; i++) {
            sum += A[row * N + i] * B[i * N + col];
        }
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

} // extern "C"
