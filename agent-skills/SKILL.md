
---

# CUDA for High-Performance Computing (HPC)

## Overview

CUDA is NVIDIA's parallel computing platform and API model. For HPC workloads, maximizing throughput requires deep hardware-aware optimization, including memory coalescing, latency hiding via warp scheduling, asynchronous execution overlap, and multi-GPU scaling using NVLink and RDMA.

**cuda-samples version:** v13.1 (CUDA Toolkit 13.1)
**Architecture Target:** Ampere (sm_80), Hopper (sm_90)
**Primary Focus:** Latency Hiding, Memory Bandwidth Optimization, Multi-Node Scaling (NCCL)

## Expert Pattern: Warp-Level Primitives & Async Execution

Instead of naive element-wise operations, HPC focuses on low-level hardware utilization (e.g., Warp shuffle instructions) and execution overlap.

```cpp
// Highly-optimized Warp Reduce using Register Shuffles (No Shared Memory bottlenecks)
__inline__ __device__ float warpReduceSum(float val) {
    for (int offset = warpSize / 2; offset > 0; offset /= 2) {
        val += __shfl_down_sync(0xffffffff, val, offset);
    }
    return val;
}

// Host code pattern: Asynchronous memory copy and execution overlap
void launchAsyncPipeline(float *h_data, float *d_data, int size, cudaStream_t stream) {
    // Requires h_data to be Pinned Memory (cudaMallocHost) for DMA transfer
    cudaMemcpyAsync(d_data, h_data, size * sizeof(float), cudaMemcpyHostToDevice, stream);
    
    int threads = 256;
    int blocks = (size + threads - 1) / threads;
    computeKernel<<<blocks, threads, 0, stream>>>(d_data, size);
    
    cudaMemcpyAsync(h_data, d_data, size * sizeof(float), cudaMemcpyDeviceToHost, stream);
}

```

## Advanced Core Concepts

* **Memory Coalescing & Alignment:** Ensuring global memory accesses by a warp fall into minimum cache lines (typically 128 bytes) to achieve peak bandwidth.
* **Shared Memory & Bank Conflicts:** Utilizing `__shared__` memory as a user-managed L1 cache while padding strides to avoid bank conflicts (32-way memory banks).
* **Warp-Level Primitives:** Using `__shfl_sync`, `__ballot_sync`, and `__any_sync` to share data between threads in the same warp directly through registers, bypassing shared memory latency.
* **Pinned (Page-Locked) Memory:** Using `cudaMallocHost` to enable direct memory access (DMA) for fast, asynchronous PCI-e transfers via `cudaMemcpyAsync`.
* **CUDA Graphs:** Capturing a sequence of kernel launches and memory operations into a graph to eliminate CPU launch overhead, crucial for iterative HPC workloads (e.g., PDE solvers, iterative linear algebra).

## HPC & Deep Learning API Ecosystem

| Domain | Library | HPC Application |
| --- | --- | --- |
| **Multi-GPU Sync** | **NCCL** | AllReduce, Broadcast, AllGather over NVLink/PCIe/InfiniBand (Essential for Distributed Training). |
| **Dense Math / AI** | **cuBLAS / cuTENSOR** | Tensor Core accelerated GEMM (`mma.sync`), Mixed-precision computation (FP16/BF16/FP8). |
| **Sparse Math** | **cuSPARSE** | Sparse Matrix-Vector (SpMV) optimizations for Finite Element Analysis (FEA). |
| **Graph / Launch** | **CUDA Graphs** | Minimizing CPU bound launch latency in iterative loops. |
| **Direct I/O** | **GPUDirect** | RDMA for Multi-node clustering, Storage Direct for fast dataset loading. |

## Profiling & Optimization Toolkit

Optimization in HPC is data-driven. The "Roofline Model" determines whether a kernel is **Memory Bound** or **Compute Bound**.

* **Nsight Systems (`nsys`):** System-wide timeline profiling. Used to identify CPU bottlenecks, PCIe transfer gaps, and ensure computation overlaps with data movement (Stream concurrency).
* **Nsight Compute (`ncu`):** Detailed kernel-level analysis. Used to analyze SM occupancy, cache hit rates, warp divergence, and memory throughput (Speed-of-Light metrics).
* **NVTX (NVIDIA Tools Extension):** Instrumenting C++ code to inject custom markers (`nvtxRangePush`) into the Nsight timeline for better readability.

## Key Considerations for Extreme Performance

* **Occupancy vs. ILP:** High occupancy (active warps per SM) hides memory latency, but sometimes lower occupancy with higher Instruction-Level Parallelism (ILP) and more registers per thread yields better performance. Use the *CUDA Occupancy Calculator*.
* **Unified Memory Prefetching:** If using `cudaMallocManaged`, always use `cudaMemPrefetchAsync` and `cudaMemAdvise` to avoid crippling page fault penalties during kernel execution.
* **Stream Priorities:** Utilize `cudaStreamCreateWithPriority` to ensure critical path compute (e.g., boundaries in domain decomposition) preempts lower-priority background tasks.
* **NUMA Awareness:** In Multi-GPU nodes, ensure CPU threads, pinned memory allocation, and the assigned GPU belong to the same NUMA node to prevent QPI/UPI bottleneck over system bus.

---

