#include "cuda_utils.cuh"
#include <stdint.h>

#define WORDS 64
#define TPW 32

extern "C" {


__device__ void mont_mul_2048_warp(uint32_t *r, const uint32_t *a, const uint32_t *b, const uint32_t *n, uint32_t n_inv) {
    uint32_t lane = threadIdx.x % TPW;
    

    uint32_t res[2] = {0, 0};

    #pragma unroll
    for (int i = 0; i < WORDS; i++) {
     
        uint32_t bi = __shfl_sync(FULL_MASK, (i < 32) ? b[0] : b[1], i % 32); 
        if (i >= 32 && i < 64 && i % 32 == lane) bi = b[1]; 
        
        uint32_t a0 = __shfl_sync(FULL_MASK, a[0], 0);
        uint32_t r0 = __shfl_sync(FULL_MASK, res[0], 0);
        

        uint32_t m = (r0 + a0 * bi) * n_inv;

        uint64_t carry = 0;
        #pragma unroll
        for (int j = 0; j < 2; j++) {
            uint64_t prod_a = (uint64_t)a[j] * bi;
            uint64_t prod_n = (uint64_t)n[j] * m;
            uint64_t sum = (uint64_t)res[j] + (uint32_t)prod_a + (uint32_t)prod_n + carry;
            res[j] = (uint32_t)sum;
            carry = (sum >> 32) + (prod_a >> 32) + (prod_n >> 32);
        }

       
        uint32_t next_carry = __shfl_up_sync(FULL_MASK, (uint32_t)carry, 1);
        if (lane > 0) res[0] += next_carry;

     
        uint32_t low_word = __shfl_down_sync(FULL_MASK, res[0], 1);
        res[0] = (lane < 31) ? low_word : (uint32_t)(carry >> 32);
        
    }
    
    r[0] = res[0];
    r[1] = res[1];
}

 
__global__ __launch_bounds__(256, 1)
void rsa_2048_kernel(uint32_t *messages, uint32_t *exponents, uint32_t *modulus, uint32_t *results, uint32_t n_inv, int count) {
    int tid = blockIdx.x * blockDim.x + threadIdx.x;
    int warp_id = tid / TPW;
    int lane = tid % TPW;

    if (warp_id >= count) return;

    uint32_t a[2], e[2], n[2], r[2];
    int base = warp_id * WORDS;

   
    a[0] = messages[base + lane];
    a[1] = messages[base + lane + 32];
    e[0] = exponents[base + lane];
    e[1] = exponents[base + lane + 32];
    n[0] = modulus[base + lane];
    n[1] = modulus[base + lane + 32];

   
    r[0] = (lane == 0) ? 1 : 0;
    r[1] = 0;

  
    #pragma unroll 1
    for (int i = 2047; i >= 0; i--) {
       
        uint32_t bit = (__shfl_sync(FULL_MASK, (i < 1024) ? e[0] : e[1], (i/32)%32) >> (i%32)) & 1;
        if (bit) {
        
        }
    }


    results[base + lane] = r[0];
    results[base + lane + 32] = r[1];
}

}
