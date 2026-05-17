#include "math_kernels.cuh"
#include <cuda_runtime.h>
#include <math_constants.h>
#include "cuda_utils.cuh"

extern "C" {

__global__ __launch_bounds__(256, 2)
void prime_search_kernel(int* __restrict__ found_primes,
                         int* __restrict__ count,
                         int start,
                         int range,
                         int shared_limit)
{
    __shared__ int s_small[54];
    __shared__ int s_small_count;

    if (threadIdx.x == 0) {
        int cnt = 0;
        for (int p = 2; p < 256 && cnt < 54; p++) {
            bool ok = true;
            for (int d = 2; d * d <= p && ok; d++)
                ok = (p % d != 0);
            if (ok) s_small[cnt++] = p;
        }
        s_small_count = cnt;
    }
    __syncthreads();

    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= range) return;

    int num = start + idx;
    bool is_prime = (num >= 2);

    if (is_prime && num >= 4) {
        int sq = (int)sqrtf((float)num) + 1;
        for (int si = 0; si < s_small_count && is_prime; si++) {
            int p = s_small[si];
            if (p > sq) break;
            is_prime = (num % p != 0);
        }

        if (is_prime && num > 65536) {
            for (int d = 257; d <= sq && is_prime; d += 2)
                is_prime = (num % d != 0);
        }
    }

    unsigned ballot = __ballot_sync(FULL_MASK, is_prime);
    int warp_count  = __popc(ballot);
    int warp_lane   = threadIdx.x & (WARP_SIZE - 1);

    int base_slot = -1;
    if (warp_lane == 0 && warp_count > 0) {
        base_slot = atomicAdd(count, warp_count);
    }

    base_slot = __shfl_sync(FULL_MASK, base_slot, 0);

    if (is_prime && base_slot >= 0) {
        unsigned lower_mask = (1u << warp_lane) - 1u;
        int slot = base_slot + __popc(ballot & lower_mask);
        if (slot < shared_limit)
            found_primes[slot] = num;
    }
}

__global__ __launch_bounds__(256, 2)
void mandelbrot_kernel(float* __restrict__ dest,
                       int width, int height,
                       float zoom, float offsetX, float offsetY,
                       int maxIter)
{
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= width || y >= height) return;

    float cx = (float)(x - width  / 2) * zoom + offsetX;
    float cy = (float)(y - height / 2) * zoom + offsetY;

    float zx = 0.0f, zy = 0.0f;
    int iter = 0;

    while (iter < maxIter - 3) {
        #pragma unroll 4
        for (int u = 0; u < 4; u++) {
            float zx2  = __fmaf_rn(zx, zx, -zy*zy) + cx;
            float zy2  = 2.0f * zx * zy + cy;
            zx = zx2; zy = zy2;
            iter++;
            if (zx*zx + zy*zy >= 4.0f) goto escape;
        }
    }

    while (iter < maxIter) {
        float zx2 = zx*zx - zy*zy + cx;
        zy = 2.0f*zx*zy + cy;
        zx = zx2;
        if (zx*zx + zy*zy >= 4.0f) goto escape;
        iter++;
    }
    dest[y * width + x] = 0.0f;
    return;

escape:
    float log_zn  = logf(zx*zx + zy*zy) * 0.5f;
    float nu      = logf(log_zn * (1.0f / logf(2.0f))) * (1.0f / logf(2.0f));
    float smooth  = (float)iter - nu + 1.0f;
    dest[y * width + x] = smooth / (float)maxIter;
}

}
