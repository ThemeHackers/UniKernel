#include <cuda_runtime.h>
#include <device_launch_parameters.h>
#include <math_constants.h>

extern "C" {


__global__ void dft_kernel(float* real_in, float* imag_in, float* magnitude_out, int n) {
    int k = blockIdx.x * blockDim.x + threadIdx.x;
    if (k >= n) return;

    float sum_real = 0.0f;
    float sum_imag = 0.0f;

    for (int t = 0; t < n; t++) {
        float angle = -2.0f * CUDART_PI_F * k * t / n;
        float cos_val = cosf(angle);
        float sin_val = sinf(angle);
        
        sum_real += real_in[t] * cos_val - imag_in[t] * sin_val;
        sum_imag += real_in[t] * sin_val + imag_in[t] * cos_val;
    }

    magnitude_out[k] = sqrtf(sum_real * sum_real + sum_imag * sum_imag) / n;
}


__device__ int reverse_bits(int x, int bits) {
    int res = 0;
    for (int i = 0; i < bits; i++) {
        res = (res << 1) | (x & 1);
        x >>= 1;
    }
    return res;
}


__global__ void fft_kernel(float* real, float* imag, int n, int bits) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= n) return;


    int rev = reverse_bits(idx, bits);
    if (idx < rev) {
        float temp_r = real[idx];
        float temp_i = imag[idx];
        real[idx] = real[rev];
        imag[idx] = imag[rev];
        real[rev] = temp_r;
        imag[rev] = temp_i;
    }
    __syncthreads();

    for (int step = 1; step < n; step <<= 1) {
        float angle = -CUDART_PI_F / step;
        float w_real = cosf(angle);
        float w_imag = sinf(angle);
        
        if ((idx & step) == 0) {
            float cur_w_r = 1.0f;
            float cur_w_i = 0.0f;
            
        }
        __syncthreads();
    }
}

}
