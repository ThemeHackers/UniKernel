#include "../include/cuda_utils.cuh"
#include "../include/graphics_kernels.cuh"
#include "../include/crypto_kernels.cuh"
#include "../include/math_kernels.cuh"
#include "../include/physics_kernels.cuh"
#include "../include/signal_kernels.cuh"
#include "../include/bench_kernels.cuh"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#define BLOCK_SIZE 256
#define GRID_SIZE 64
#define TEST_WIDTH 640
#define TEST_HEIGHT 480
#define TEST_PARTICLES 1024
#define TEST_SIGNAL_SIZE 256
void initGraphicsConstants() {
    float3 axis = { 1.0f, 0.8f, 0.3f };
    float len = sqrtf(axis.x*axis.x + axis.y*axis.y + axis.z*axis.z);
    axis.x /= len; axis.y /= len; axis.z /= len;
    CUDA_CHECK(cudaMemcpyToSymbol(c_axis, &axis, sizeof(float3)));
}
void test_graphics() {
    printf("\n[TEST] Graphics Kernels...\n");
    float *d_frame;
    int frame_bytes = TEST_WIDTH * TEST_HEIGHT * sizeof(float);
    CUDA_CHECK(cudaMalloc(&d_frame, frame_bytes));
    dim3 blocks(GRID_SIZE, GRID_SIZE);
    dim3 threads(BLOCK_SIZE);
    render_3d_kernel<<<blocks, threads>>>(d_frame, TEST_WIDTH, TEST_HEIGHT, 0.5f);
    CUDA_CHECK(cudaGetLastError());
    CUDA_CHECK(cudaDeviceSynchronize());
    printf("  ✓ render_3d_kernel: PASSED\n");
    CUDA_CHECK(cudaFree(d_frame));
}
void test_crypto() {
    printf("\n[TEST] Crypto Kernels...\n");
    int data_size = 1024;
    unsigned char *d_data;
    CUDA_CHECK(cudaMalloc(&d_data, data_size));
    dim3 blocks((data_size + BLOCK_SIZE - 1) / BLOCK_SIZE);
    dim3 threads(BLOCK_SIZE);
    encrypt_kernel<<<blocks, threads>>>(d_data, data_size, 42);
    CUDA_CHECK(cudaGetLastError());
    CUDA_CHECK(cudaDeviceSynchronize());
    printf("  ✓ encrypt_kernel: PASSED\n");
    int *d_result;
    CUDA_CHECK(cudaMalloc(&d_result, sizeof(int)));
    hash_crack_kernel<<<blocks, threads>>>(d_result, 0x12345678, 0, 100000);
    CUDA_CHECK(cudaGetLastError());
    CUDA_CHECK(cudaDeviceSynchronize());
    printf("  ✓ hash_crack_kernel: PASSED\n");
    sbox_encrypt_kernel<<<blocks, threads>>>(d_data, data_size, 42);
    CUDA_CHECK(cudaGetLastError());
    CUDA_CHECK(cudaDeviceSynchronize());
    printf("  ✓ sbox_encrypt_kernel: PASSED\n");
    CUDA_CHECK(cudaFree(d_data));
    CUDA_CHECK(cudaFree(d_result));
}
void test_math() {
    printf("\n[TEST] Math Kernels...\n");
    int *d_primes, *d_count;
    CUDA_CHECK(cudaMalloc(&d_primes, 10 * sizeof(int)));
    CUDA_CHECK(cudaMalloc(&d_count, sizeof(int)));
    dim3 blocks(GRID_SIZE);
    dim3 threads(BLOCK_SIZE);
    prime_search_kernel<<<blocks, threads>>>(d_primes, d_count, 2, 10000, 1024);
    CUDA_CHECK(cudaGetLastError());
    CUDA_CHECK(cudaDeviceSynchronize());
    printf("  ✓ prime_search_kernel: PASSED\n");
    float *d_mandel;
    CUDA_CHECK(cudaMalloc(&d_mandel, TEST_WIDTH * TEST_HEIGHT * sizeof(float)));
    dim3 mandel_blocks((TEST_WIDTH + 15) / 16, (TEST_HEIGHT + 15) / 16);
    dim3 mandel_threads(16, 16);
    mandelbrot_kernel<<<mandel_blocks, mandel_threads>>>(
        d_mandel, TEST_WIDTH, TEST_HEIGHT, 1.0f, -0.5f, 0.0f, 100
    );
    CUDA_CHECK(cudaGetLastError());
    CUDA_CHECK(cudaDeviceSynchronize());
    printf("  ✓ mandelbrot_kernel: PASSED\n");
    CUDA_CHECK(cudaFree(d_primes));
    CUDA_CHECK(cudaFree(d_count));
    CUDA_CHECK(cudaFree(d_mandel));
}
void test_physics() {
    printf("\n[TEST] Physics Kernels...\n");
    Particle *d_particles_in, *d_particles_out;
    CUDA_CHECK(cudaMalloc(&d_particles_in, TEST_PARTICLES * sizeof(Particle)));
    CUDA_CHECK(cudaMalloc(&d_particles_out, TEST_PARTICLES * sizeof(Particle)));
    dim3 blocks((TEST_PARTICLES + BLOCK_SIZE - 1) / BLOCK_SIZE);
    dim3 threads(BLOCK_SIZE);
    nbody_step_kernel<<<blocks, threads>>>(
        d_particles_in, d_particles_out, TEST_PARTICLES, 0.016f, 9.81f
    );
    CUDA_CHECK(cudaGetLastError());
    CUDA_CHECK(cudaDeviceSynchronize());
    printf("  ✓ nbody_step_kernel: PASSED\n");
    float *d_frame;
    CUDA_CHECK(cudaMalloc(&d_frame, TEST_WIDTH * TEST_HEIGHT * sizeof(float)));
    int frame_size = TEST_WIDTH * TEST_HEIGHT;
    dim3 frame_blocks((frame_size + BLOCK_SIZE - 1) / BLOCK_SIZE);
    clear_frame_kernel<<<frame_blocks, threads>>>(d_frame, frame_size);
    CUDA_CHECK(cudaGetLastError());
    CUDA_CHECK(cudaDeviceSynchronize());
    printf("  ✓ clear_frame_kernel: PASSED\n");
    dim3 render_blocks((TEST_WIDTH + 15) / 16, (TEST_HEIGHT + 15) / 16);
    dim3 render_threads(16, 16);
    render_physics_kernel<<<render_blocks, render_threads>>>(
        d_frame, d_particles_in, TEST_PARTICLES, TEST_WIDTH, TEST_HEIGHT
    );
    CUDA_CHECK(cudaGetLastError());
    CUDA_CHECK(cudaDeviceSynchronize());
    printf("  ✓ render_physics_kernel: PASSED\n");
    CUDA_CHECK(cudaFree(d_particles_in));
    CUDA_CHECK(cudaFree(d_particles_out));
    CUDA_CHECK(cudaFree(d_frame));
}
void test_signal() {
    printf("\n[TEST] Signal Kernels...\n");
    float *d_real_in, *d_imag_in, *d_magnitude_out;
    CUDA_CHECK(cudaMalloc(&d_real_in, TEST_SIGNAL_SIZE * sizeof(float)));
    CUDA_CHECK(cudaMalloc(&d_imag_in, TEST_SIGNAL_SIZE * sizeof(float)));
    CUDA_CHECK(cudaMalloc(&d_magnitude_out, TEST_SIGNAL_SIZE * sizeof(float)));
    dim3 blocks((TEST_SIGNAL_SIZE + BLOCK_SIZE - 1) / BLOCK_SIZE);
    dim3 threads(BLOCK_SIZE);
    dft_kernel<<<blocks, threads>>>(d_real_in, d_imag_in, d_magnitude_out, TEST_SIGNAL_SIZE);
    CUDA_CHECK(cudaGetLastError());
    CUDA_CHECK(cudaDeviceSynchronize());
    printf("  ✓ dft_kernel: PASSED\n");
    fft_bitrev_kernel<<<blocks, threads>>>(d_real_in, d_imag_in, TEST_SIGNAL_SIZE, 8);
    CUDA_CHECK(cudaGetLastError());
    CUDA_CHECK(cudaDeviceSynchronize());
    printf("  ✓ fft_bitrev_kernel: PASSED\n");
    fft_butterfly_kernel<<<blocks, threads>>>(d_real_in, d_imag_in, TEST_SIGNAL_SIZE, 1);
    CUDA_CHECK(cudaGetLastError());
    CUDA_CHECK(cudaDeviceSynchronize());
    printf("  ✓ fft_butterfly_kernel: PASSED\n");
    fft_shared_kernel<<<blocks, threads>>>(d_real_in, d_imag_in, TEST_SIGNAL_SIZE, 8);
    CUDA_CHECK(cudaGetLastError());
    CUDA_CHECK(cudaDeviceSynchronize());
    printf("  ✓ fft_shared_kernel: PASSED\n");
    uchar4 *d_pixels;
    CUDA_CHECK(cudaMalloc(&d_pixels, TEST_WIDTH * TEST_HEIGHT * sizeof(uchar4)));
    dim3 vision_blocks((TEST_WIDTH + 15) / 16, (TEST_HEIGHT + 15) / 16);
    dim3 vision_threads(16, 16);
    vision_filter_kernel<<<vision_blocks, vision_threads>>>(d_pixels, TEST_WIDTH, TEST_HEIGHT);
    CUDA_CHECK(cudaGetLastError());
    CUDA_CHECK(cudaDeviceSynchronize());
    printf("  ✓ vision_filter_kernel: PASSED\n");
    unsigned char *d_data;
    int *d_found_idx;
    CUDA_CHECK(cudaMalloc(&d_data, 1024));
    CUDA_CHECK(cudaMalloc(&d_found_idx, sizeof(int)));
    pattern_match_kernel<<<blocks, threads>>>(d_data, 1024, 16, d_found_idx);
    CUDA_CHECK(cudaGetLastError());
    CUDA_CHECK(cudaDeviceSynchronize());
    printf("  ✓ pattern_match_kernel: PASSED\n");
    CUDA_CHECK(cudaFree(d_real_in));
    CUDA_CHECK(cudaFree(d_imag_in));
    CUDA_CHECK(cudaFree(d_magnitude_out));
    CUDA_CHECK(cudaFree(d_pixels));
    CUDA_CHECK(cudaFree(d_data));
    CUDA_CHECK(cudaFree(d_found_idx));
}
void test_bench() {
    printf("\n[TEST] Benchmark Kernels...\n");
    int N = 512;
    float *d_A, *d_B, *d_C;
    CUDA_CHECK(cudaMalloc(&d_A, N * N * sizeof(float)));
    CUDA_CHECK(cudaMalloc(&d_B, N * N * sizeof(float)));
    CUDA_CHECK(cudaMalloc(&d_C, N * N * sizeof(float)));
    dim3 blocks((N + BLOCK_SIZE - 1) / BLOCK_SIZE);
    dim3 threads(BLOCK_SIZE);
    bench_matmul_kernel<<<blocks, threads>>>(d_A, d_B, d_C, N);
    CUDA_CHECK(cudaGetLastError());
    CUDA_CHECK(cudaDeviceSynchronize());
    printf("  ✓ bench_matmul_kernel: PASSED\n");
    float *d_out;
    CUDA_CHECK(cudaMalloc(&d_out, GRID_SIZE * sizeof(float)));
    shared_mem_bench_kernel<<<GRID_SIZE, threads>>>(d_out);
    CUDA_CHECK(cudaGetLastError());
    CUDA_CHECK(cudaDeviceSynchronize());
    printf("  ✓ shared_mem_bench_kernel: PASSED\n");
    int *d_counters;
    CUDA_CHECK(cudaMalloc(&d_counters, 4 * sizeof(int)));
    atomic_bench_kernel<<<GRID_SIZE, threads>>>(d_counters, 1000, 0);
    CUDA_CHECK(cudaGetLastError());
    CUDA_CHECK(cudaDeviceSynchronize());
    printf("  ✓ atomic_bench_kernel: PASSED\n");
    float4 *d_src, *d_dst;
    int mem_size = 1024 * 1024;
    CUDA_CHECK(cudaMalloc(&d_src, mem_size * sizeof(float4)));
    CUDA_CHECK(cudaMalloc(&d_dst, mem_size * sizeof(float4)));
    memory_bandwidth_kernel<<<GRID_SIZE, threads>>>(d_src, d_dst, mem_size);
    CUDA_CHECK(cudaGetLastError());
    CUDA_CHECK(cudaDeviceSynchronize());
    printf("  ✓ memory_bandwidth_kernel: PASSED\n");
    null_kernel<<<GRID_SIZE, threads>>>();
    CUDA_CHECK(cudaGetLastError());
    CUDA_CHECK(cudaDeviceSynchronize());
    printf("  ✓ null_kernel: PASSED\n");
    CUDA_CHECK(cudaFree(d_A));
    CUDA_CHECK(cudaFree(d_B));
    CUDA_CHECK(cudaFree(d_C));
    CUDA_CHECK(cudaFree(d_out));
    CUDA_CHECK(cudaFree(d_counters));
    CUDA_CHECK(cudaFree(d_src));
    CUDA_CHECK(cudaFree(d_dst));
}
int main() {
    printf("\n╔════════════════════════════════════════════════════════╗\n");
    printf("║  UNIKERNEL CUDA KERNEL COMPREHENSIVE TEST SUITE       ║\n");
    printf("╚════════════════════════════════════════════════════════╝\n");
    int device_count;
    cudaGetDeviceCount(&device_count);
    if (device_count == 0) {
        printf("ERROR: No CUDA devices found!\n");
        return 1;
    }
    cudaDeviceProp prop;
    CUDA_CHECK(cudaGetDeviceProperties(&prop, 0));
    printf("\nGPU: %s\n", prop.name);
    printf("Compute Capability: %d.%d\n", prop.major, prop.minor);
    initGraphicsConstants();
    try {
        test_graphics();
        test_crypto();
        test_math();
        test_physics();
        test_signal();
        test_bench();
        printf("\n╔════════════════════════════════════════════════════════╗\n");
        printf("║  ✓ ALL TESTS PASSED SUCCESSFULLY                    ║\n");
        printf("╚════════════════════════════════════════════════════════╝\n");
    } catch (const char* error) {
        printf("\n❌ TEST FAILED: %s\n", error);
        return 1;
    }
    return 0;
}
