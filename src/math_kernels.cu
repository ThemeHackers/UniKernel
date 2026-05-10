#include "math_kernels.cuh"
#include <algorithm>

extern "C" {

__global__ __launch_bounds__(256, 1)
void prime_search_kernel(int *found_primes, int *count, int start, int range, int shared_limit) {
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

__global__ __launch_bounds__(256, 1)
void mandelbrot_kernel(float *dest, int width, int height, float zoom, float offsetX, float offsetY, int maxIter) {
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;

    if (x < width && y < height) {
        float zx = 0, zy = 0;
        float cx = (x - width / 2.0f) * zoom + offsetX;
        float cy = (y - height / 2.0f) * zoom + offsetY;

        int iter = 0;
        while (zx * zx + zy * zy < 4.0f && iter < maxIter) {
            float tmp = zx * zx - zy * zy + cx;
            zy = 2.0f * zx * zy + cy;
            zx = tmp;
            iter++;
        }
        dest[y * width + x] = (float)iter / maxIter;
    }
}

} 
