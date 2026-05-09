import asyncio
import websockets
import json
import numpy as np
import os
import glob
import time
import msgpack
import socket
import threading
import sys
import traceback
import warnings
from zeroconf import IPVersion, ServiceInfo, Zeroconf
from zeroconf.asyncio import AsyncZeroconf


warnings.filterwarnings("ignore", category=UserWarning)
warnings.filterwarnings("ignore", category=DeprecationWarning)
warnings.filterwarnings("ignore", category=FutureWarning)

from pynvml import *

from rich.console import Console
from rich.live import Live
from rich.table import Table
from rich.panel import Panel
from rich.layout import Layout
from rich.text import Text
from rich.columns import Columns
from rich import box

console = Console()

def setup_environment():
    if os.name == 'nt':
        import shutil
        import subprocess

        found_msvc = False
       
        if shutil.which("cl.exe"):
            console.print("[dim]cl.exe already in PATH.[/dim]")
            found_msvc = True
        
      
        if not found_msvc:
            vswhere_path = os.path.expandvars("%ProgramFiles(x86)%\\Microsoft Visual Studio\\Installer\\vswhere.exe")
            if os.path.exists(vswhere_path):
                try:
                    out = subprocess.check_output([vswhere_path, "-latest", "-products", "*", "-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "-property", "installationPath"], encoding='utf-8').strip()
                    if out:
                        base = os.path.join(out, "VC", "Tools", "MSVC")
                        if os.path.exists(base):
                            versions = os.listdir(base)
                            if versions:
                                latest_ver = sorted(versions)[-1]
                                msvc_bin = os.path.join(base, latest_ver, "bin", "Hostx64", "x64")
                                if os.path.exists(msvc_bin):
                                    os.environ['PATH'] = msvc_bin + os.pathsep + os.environ['PATH']
                                    console.print(f"[dim]vswhere found MSVC at: {msvc_bin}[/dim]")
                                    found_msvc = True
                except:
                    pass


        if not found_msvc:
            base_paths = [
                'C:\\Program Files\\Microsoft Visual Studio',
                'C:\\Program Files (x86)\\Microsoft Visual Studio',
                'D:\\Program Files\\Microsoft Visual Studio',
                'E:\\Program Files\\Microsoft Visual Studio'
            ]
            vs_versions = ['2022', '2019', '2017']
            vs_editions = ['Community', 'Professional', 'Enterprise', 'BuildTools']
            
            for bp in base_paths:
                for v in vs_versions:
                    for e in vs_editions:
                        base = os.path.join(bp, v, e, "VC", "Tools", "MSVC")
                        if os.path.exists(base):
                            versions = os.listdir(base)
                            if versions:
                                latest_ver = sorted(versions)[-1]
                                msvc_bin = os.path.join(base, latest_ver, "bin", "Hostx64", "x64")
                                if os.path.exists(msvc_bin):
                                    os.environ['PATH'] = msvc_bin + os.pathsep + os.environ['PATH']
                                    console.print(f"[dim]Found MSVC at: {msvc_bin}[/dim]")
                                    found_msvc = True
                                    break
                    if found_msvc: break
                if found_msvc: break
        
        if not found_msvc:
            console.print("[yellow]Warning:[/yellow] MSVC (cl.exe) not found in standard paths. Compilation might fail.")

        cuda_path = os.environ.get('CUDA_PATH')
        if cuda_path:
            bin_path = os.path.join(cuda_path, 'bin')
            if bin_path not in os.environ['PATH']:
                os.environ['PATH'] = bin_path + os.pathsep + os.environ['PATH']

setup_environment()

try:
    import pycuda.driver as drv
    from pycuda.compiler import SourceModule
except ImportError:
    console.print("[bold red]Error:[/bold red] PyCUDA not installed correctly.")
    sys.exit(1)

BASE_CODE = """
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
    const int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= range) return;
    int val = start + idx;
    if (val < 2) return;
    bool is_prime = true;
    for (int i = 2; i * i <= val; i++) {
        if (val % i == 0) { is_prime = false; break; }
    }
    if (is_prime) {
        int pos = atomicAdd(count, 1);
        if (pos < 1000) found_primes[pos] = val;
    }
}

__global__ void vision_filter_kernel(unsigned char *pixels, int width, int height) {
    const int x = blockIdx.x * blockDim.x + threadIdx.x;
    const int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= width || y >= height) return;
    int idx = (y * width + x) * 3; 
    unsigned char r = pixels[idx], g = pixels[idx+1], b = pixels[idx+2];
    unsigned char gray = (unsigned char)(0.299f*r + 0.587f*g + 0.114f*b);
    pixels[idx] = pixels[idx+1] = pixels[idx+2] = gray;
}

__global__ void signal_proc_kernel(float *real, float *imag, float *magnitude, int len) {
    const int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx < len) magnitude[idx] = sqrtf(real[idx]*real[idx] + imag[idx]*imag[idx]);
}

__global__ void pattern_match_kernel(unsigned char *data, int data_len, unsigned char *pattern, int pat_len, int *found_idx) {
    const int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx <= data_len - pat_len) {
        bool match = true;
        for (int i = 0; i < pat_len; i++) {
            if (data[idx + i] != pattern[i]) { match = false; break; }
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
"""


major, minor = 0, 0
dev_name = "Unknown"

try:
    nvmlInit()
    nv_handle = nvmlDeviceGetHandleByIndex(0)
    drv.init()
    dev = drv.Device(0)
    dev_name = dev.name()
    major, minor = dev.compute_capability()
    arch_flag = f"-arch=sm_{major}{minor}"
    console.print(f"[bold green]Detected GPU:[/bold green] {dev_name} (Compute {major}.{minor})")
    cuda_ctx = dev.make_context()
   
    compile_options = [arch_flag, "-O3", "--use_fast_math", "--allow-unsupported-compiler", "-Xcompiler", "/wd4819"]

    try:
        current_mod = SourceModule(BASE_CODE, options=compile_options, no_extern_c=True, keep=False)
    except Exception as e:
        console.print(f"[bold red]CUDA Compile Error:[/bold red] {str(e)}")
        try:
            current_mod = SourceModule("__global__ void dummy() {}", options=[arch_flag])
        except:
            current_mod = None
    finally:
        cuda_ctx.pop()
except Exception as e:
    console.print(f"[bold red]GPU Init Error:[/bold red] {e}")
    current_mod = None
    cuda_ctx = None
    nv_handle = None

total_tasks_count = 0
connected_clients = {}
logs = []

def add_log(msg, style="white"):
    timestamp = time.strftime('%H:%M:%S')
    console.print(f"[{timestamp}] {msg}")
    logs.append(f"[{timestamp}] {msg}")
    if len(logs) > 50: logs.pop(0)

def process_gpu_request(req, addr):
    global total_tasks_count
    if not current_mod or not cuda_ctx: return {"status": "error", "message": "GPU Context not initialized"}
    cuda_ctx.push()
    try:
        start = time.time()
        res = {"status": "ok"}
        cmd = req.get("cmd")
        if cmd == "gpu_exec":
            kernel_name, data = req.get("kernel"), req.get("data")
            func = current_mod.get_function(kernel_name + "_kernel")
            if kernel_name == "render_3d":
                w, h = int(data[0]), int(data[1])
                dest = np.zeros(w*h).astype(np.float32)
                func(drv.Out(dest), np.int32(w), np.int32(h), np.float32(time.time()), block=(16,16,1), grid=((w+15)//16,(h+15)//16))
                res["data"] = dest.tolist() if w <= 32 else "omitted"
                res["kernel"] = "render_3d"
            elif kernel_name == "hash_crack":
                target, s, r = int(np.uint32(data[0])), int(np.int32(data[1])), int(np.int32(data[2]))
                result = np.array([-1]).astype(np.int32)
                g, b = func.get_max_potential_block_size(0)
                g = min(g, (r + b - 1) // b)
                func(drv.InOut(result), np.uint32(target), np.int32(s), np.int32(r), block=(b,1,1), grid=(g, 1))
                res["data"] = int(result[0])
                res["kernel"] = "hash_crack"
         
            res["compute_ms"] = round((time.time()-start)*1000, 2)
        return res
    except Exception as e: return {"status": "error", "message": str(e)}
    finally: cuda_ctx.pop()

async def handle_unikernel(websocket):
    addr = f"{websocket.remote_address[0]}:{websocket.remote_address[1]}"
    add_log(f"New client: [cyan]{addr}[/cyan]")
    connected_clients[addr] = {"time": time.strftime("%H:%M:%S"), "tasks": 0}
    try:
        async for message in websocket:
            if isinstance(message, bytes): message = bytes([b ^ 0x5A for b in message])
            try:
                req = msgpack.unpackb(message)
                res = await asyncio.to_thread(process_gpu_request, req, addr)
                resp_bytes = msgpack.packb(res)
                await websocket.send(bytes([b ^ 0x5A for b in resp_bytes]))
                connected_clients[addr]["tasks"] += 1
            except: pass
    finally: connected_clients.pop(addr, None)

def generate_dashboard():
    layout = Layout()
    layout.split_column(Layout(name="header", size=3), Layout(name="main"), Layout(name="footer", size=5))
    layout["main"].split_row(Layout(name="stats", ratio=1), Layout(name="clients", ratio=2))
    layout["header"].update(Panel(Text("UniAccelHost - High Performance Engine", justify="center", style="bold cyan"), box=box.DOUBLE))
    
    gpu_stats = Text()
    if nv_handle:
        try:
            temp = nvmlDeviceGetTemperature(nv_handle, 0)
            util = nvmlDeviceGetUtilizationRates(nv_handle).gpu
            mem = nvmlDeviceGetMemoryInfo(nv_handle)
            gpu_stats.append(f"\n THERMAL: {temp}\u00b0C\n LOAD:    {util}%\n VRAM:    {mem.used//1048576}MB\n ARCH:    sm_{major}{minor}\n", style="cyan")
        except: pass
    layout["stats"].update(Panel(gpu_stats, title="GPU TELEMETRY", border_style="cyan"))
    
    table = Table(expand=True)
    table.add_column("NODE", style="cyan")
    table.add_column("TASKS", justify="right")
    for addr, info in connected_clients.items(): table.add_row(addr, str(info["tasks"]))
    layout["clients"].update(Panel(table, title="CONNECTED NODES", border_style="blue"))
    return layout

async def server_main():
    hostname = socket.gethostname()
    local_ip = socket.gethostbyname(hostname)
    info = ServiceInfo("_uniaccel._tcp.local.", f"{hostname}._uniaccel._tcp.local.", addresses=[socket.inet_aton(local_ip)], port=81, properties={"v": "1.1"}, server=f"{hostname}.local.")
    aiozc = AsyncZeroconf()
    await aiozc.async_register_service(info)
    try:
        async with websockets.serve(handle_unikernel, "0.0.0.0", 81):
            while True: await asyncio.sleep(1)
    finally:
        await aiozc.async_unregister_service(info)
        await aiozc.async_close()

if __name__ == "__main__":
    try: asyncio.run(server_main())
    except KeyboardInterrupt: sys.exit(0)
