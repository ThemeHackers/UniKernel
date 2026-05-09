#ifndef CRYPTO_KERNELS_CUH
#define CRYPTO_KERNELS_CUH

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief XOR Encryption kernel
 */
__global__ void encrypt_kernel(unsigned char *data, int len, unsigned char key);

/**
 * @brief Simple hash cracking benchmark
 */
__global__ void hash_crack_kernel(int *result, unsigned int target_hash, int start_val, int range);

#ifdef __cplusplus
}
#endif

#endif
