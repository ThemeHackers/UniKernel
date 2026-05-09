#include "crypto_kernels.cuh"

extern "C" {

__global__ void encrypt_kernel(unsigned char *data, int len, unsigned char key) {
    const int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx < len) {
        data[idx] ^= key;
    }
}

__global__ void hash_crack_kernel(int *result, unsigned int target_hash, int start_val, int range) {
    const int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx < range) {
        unsigned int current = start_val + idx;

        unsigned int h = (current * 1103515245 + 12345);
        if (h == target_hash) {
            atomicExch(result, (int)current);
        }
    }
}

} 
