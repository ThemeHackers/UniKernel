#include "math_kernels.cuh"
#include <algorithm>

extern "C" {

__global__ void prime_search_kernel(int *found_primes, int *count, int start, int range, int shared_limit) {
    extern __shared__ int s_primes[];
    const int idx = blockIdx.x * blockDim.x + threadIdx.x;
    
    if (idx < range) {
        int num = start + idx;
        bool is_prime = (num > 1);
        for (int i = 2; i * i <= num; i++) {
            if (num % i == 0) {
                is_prime = false;
                break;
            }
        }
        
        if (is_prime) {
            int old_count = atomicAdd(count, 1);
            if (old_count < 1000) {
                found_primes[old_count] = num;
            }
        }
    }
}

} 
