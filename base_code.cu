#include <math_constants.h>
#define FULL_MASK 0xffffffff

__device__ float3 add_f3(float3 a, float3 b) { return make_float3(a.x+b.x, a.y+b.y, a.z+b.z); }
__device__ float3 sub_f3(float3 a, float3 b) { return make_float3(a.x-b.x, a.y-b.y, a.z-b.z); }
__device__ float3 mul_f3(float3 a, float b) { return make_float3(a.x*b, a.y*b, a.z*b); }
__device__ float dot_f3(float3 a, float3 b) { return a.x*b.x + a.y*b.y + a.z*b.z; }
__device__ float vlen_f3(float3 v) { return sqrtf(dot_f3(v, v)); }
__device__ float3 norm_f3(float3 v) { float invLen = rsqrtf(dot_f3(v, v)); return mul_f3(v, invLen); }

__device__ float sdSphere(float3 p, float s) { return vlen_f3(p) - s; }
__device__ float map(float3 p, float time) {
    float d = 1000.0f;
    for(int i=0; i<3; i++) {
        float3 pos = make_float3(sinf(time + i*2.0f)*0.6f, cosf(time*0.7f + i)*0.4f, sinf(time*0.5f + i)*0.3f);
        float d2 = sdSphere(sub_f3(p, pos), 0.3f);
        float h = fmaxf(0.1f - fabsf(d - d2), 0.0f) / 0.1f;
        d = fminf(d, d2) - h*h*0.1f * 0.25f;
    }
    return d;
}

__device__ float3 getNormal(float3 p, float time) {
    float e = 0.001f;
    float3 n = make_float3(
        map(make_float3(p.x+e, p.y, p.z), time) - map(make_float3(p.x-e, p.y, p.z), time),
        map(make_float3(p.x, p.y+e, p.z), time) - map(make_float3(p.x, p.y-e, p.z), time),
        map(make_float3(p.x, p.y, p.z+e), time) - map(make_float3(p.x, p.y, p.z-e), time)
    );
    return norm_f3(n);
}

__global__ void render_3d_kernel(float *dest, int width, int height, float time) {
    const int x = blockIdx.x * blockDim.x + threadIdx.x;
    const int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= width || y >= height) return;
    float ux = ((float)x/width * 2.0f - 1.0f) * ((float)width/height);
    float uy = (float)y/height * 2.0f - 1.0f;
    float3 ro = make_float3(0.0f, 0.0f, -2.5f);
    float3 rd = norm_f3(make_float3(ux, uy, 1.5f));
    float t = 0.0f; float d = 0.0f;
    for(int i=0; i<64; i++) {
        float3 p = add_f3(ro, mul_f3(rd, t));
        d = map(p, time);
        if(d < 0.001f || t > 10.0f) break;
        t += d;
    }
    float col = 0.0f;
    if(d < 0.001f) {
        float3 p = add_f3(ro, mul_f3(rd, t));
        float3 n = getNormal(p, time);
        float3 light = norm_f3(make_float3(1.0f, 1.0f, -2.0f));
        col = fmaxf(dot_f3(n, light), 0.0f) * 0.8f + 0.2f;
        col *= 1.0f / (1.0f + t*t*0.1f);
    }
    dest[y * width + x] = col;
}

__global__ void encrypt_kernel(unsigned char *data, int len, unsigned char key) {
    const int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx < len) {
        unsigned char v = data[idx];
        v ^= key; v = (v << 3) | (v >> 5); v += 0x55; v ^= (key >> 1);
        data[idx] = v;
    }
}

__global__ void hash_crack_kernel(int *result, unsigned int target, int start, int range) {
    const int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= range || *result != -1) return;
    unsigned int val = (unsigned int)(start + idx);
    unsigned int h = val;
    h ^= h >> 16; h *= 0x85ebca6b; h ^= h >> 13; h *= 0xc2b2ae35; h ^= h >> 16;
    if (h == target) atomicExch(result, (int)val);
}

__global__ void prime_search_kernel(int *found_primes, int *count, int start, int range) {
    __shared__ int local_count;
    __shared__ int local_primes[256];
    if (threadIdx.x == 0) local_count = 0;
    __syncthreads();

    const int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx < range) {
        int val = start + idx;
        if (val >= 2) {
            bool is_prime = true;
            for (int i = 2; i * i <= val; i++) {
                if (val % i == 0) { is_prime = false; break; }
            }
            if (is_prime) {
                int pos = atomicAdd(&local_count, 1);
                if (pos < 256) local_primes[pos] = val;
            }
        }
    }
    __syncthreads();

    if (threadIdx.x == 0 && local_count > 0) {
        int global_pos = atomicAdd(count, local_count);
        for(int i=0; i < min(local_count, 256); i++) {
            if (global_pos + i < 1000) found_primes[global_pos + i] = local_primes[i];
        }
    }
}

__global__ void vision_filter_kernel(uchar4 *pixels, int width, int height) {
    const int x = blockIdx.x * blockDim.x + threadIdx.x;
    const int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= width || y >= height) return;
    
    int idx = y * width + x;
    uchar4 p = pixels[idx];
    unsigned char gray = (unsigned char)(0.299f*p.x + 0.587f*p.y + 0.114f*p.z);
    pixels[idx] = make_uchar4(gray, gray, gray, p.w);
}

__global__ void signal_proc_kernel(float *real, float *imag, float *magnitude, int len) {
    const int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx < len) magnitude[idx] = sqrtf(real[idx]*real[idx] + imag[idx]*imag[idx]);
}

__constant__ unsigned char CONST_PATTERN[256];

__global__ void pattern_match_kernel(unsigned char *data, int data_len, int pat_len, int *found_idx) {
    const int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx <= data_len - pat_len) {
        bool match = true;
        for (int i = 0; i < pat_len; i++) {
            if (data[idx + i] != CONST_PATTERN[i]) { match = false; break; }
        }
        if (match) atomicExch(found_idx, idx);
    }
}

__global__ void matrix_mul_kernel(float *A, float *B, float *C, int N) {
    const int TILE_SIZE = 16;
    __shared__ float sA[TILE_SIZE][TILE_SIZE];
    __shared__ float sB[TILE_SIZE][TILE_SIZE];
    int row = blockIdx.y * TILE_SIZE + threadIdx.y;
    int col = blockIdx.x * TILE_SIZE + threadIdx.x;
    float sum = 0.0f;
    for (int i = 0; i < (N + TILE_SIZE - 1) / TILE_SIZE; i++) {
        if (row < N && (i * TILE_SIZE + threadIdx.x) < N)
            sA[threadIdx.y][threadIdx.x] = A[row * N + i * TILE_SIZE + threadIdx.x];
        else sA[threadIdx.y][threadIdx.x] = 0.0f;
        if (col < N && (i * TILE_SIZE + threadIdx.y) < N)
            sB[threadIdx.y][threadIdx.x] = B[(i * TILE_SIZE + threadIdx.y) * N + col];
        else sB[threadIdx.y][threadIdx.x] = 0.0f;
        __syncthreads();
        for (int k = 0; k < TILE_SIZE; k++) sum += sA[threadIdx.y][k] * sB[k][threadIdx.x];
        __syncthreads();
    }
    if (row < N && col < N) C[row * N + col] = sum;
}

__global__ void shared_mem_bench_kernel(float *out) {
    __shared__ float sData[1024];
    int tid = threadIdx.x;
    sData[tid] = (float)tid;
    __syncthreads();
    for(int i=0; i<100; i++) {
        sData[tid] = sData[tid] * 1.0001f + sData[(tid + 1) % 1024];
    }
    if(tid == 0) out[0] = sData[0];
}

__global__ void atomic_bench_kernel(int *counter, int iterations) {
    for(int i=0; i<iterations; i++) {
        atomicAdd(counter, 1);
    }
}

__global__ void null_kernel() { }