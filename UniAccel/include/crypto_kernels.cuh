#ifndef CRYPTO_KERNELS_CUH
#define CRYPTO_KERNELS_CUH

#include <cuda_runtime.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

__global__ void encrypt_kernel(unsigned char *data, int len, unsigned char key);
__global__ void hash_crack_kernel(int *result, unsigned int target_hash, int start_val, int range);
__global__ void sbox_encrypt_kernel(unsigned char *data, int len, unsigned char key);

#ifdef __cplusplus
}
#endif

#endif
