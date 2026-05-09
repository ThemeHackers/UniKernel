#include "signal_kernels.cuh"

extern "C" {

__global__ void vision_filter_kernel(uchar4 *pixels, int width, int height) {
    const int x = blockIdx.x * blockDim.x + threadIdx.x;
    const int y = blockIdx.y * blockDim.y + threadIdx.y;

    if (x < width && y < height) {
        int idx = y * width + x;
        uchar4 p = pixels[idx];
     
        unsigned char gray = (unsigned char)(0.299f * p.x + 0.587f * p.y + 0.114f * p.z);
        pixels[idx] = make_uchar4(gray, gray, gray, p.w);
    }
}

__constant__ unsigned char CONST_PATTERN[256];

__global__ void pattern_match_kernel(unsigned char *data, int data_len, int pat_len, int *found_idx) {
    const int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx <= data_len - pat_len) {
        bool match = true;
        for (int i = 0; i < pat_len; i++) {
            if (data[idx + i] != CONST_PATTERN[i]) {
                match = false;
                break;
            }
        }
        if (match) atomicExch(found_idx, idx);
    }
}

} 
