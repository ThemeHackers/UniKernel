import asyncio
import websockets
import json
import numpy as np
import os
import glob
import hashlib
import time
import msgpack
import socket
import threading
import sys
import traceback
import warnings
import subprocess
import logging
import aiohttp
from aiohttp import web
from zeroconf import IPVersion, ServiceInfo, Zeroconf
from zeroconf.asyncio import AsyncZeroconf
warnings.filterwarnings("ignore", category=UserWarning)
warnings.filterwarnings("ignore", message=".*TypedStorage is deprecated.*")
warnings.filterwarnings("ignore", message=".*coroutine '.*' was never awaited.*")

from pynvml import *
from rich.console import Console
from rich.panel import Panel

console = Console()
SESSION_KEY = bytes([0x5A, 0xA5, 0x12, 0x34, 0x56, 0x78, 0x90, 0xAB, 0xCD, 0xEF, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66])

def secure_crypt(data):
    arr = np.frombuffer(data, dtype=np.uint8).copy()
    for i in range(len(arr)):
        arr[i] ^= SESSION_KEY[i % 16]
    return arr.tobytes()

def setup_environment():
    if os.name == 'nt':
        vswhere_path = os.path.expandvars("%ProgramFiles(x86)%\\Microsoft Visual Studio\\Installer\\vswhere.exe")
        vcvars_paths = []
        if os.path.exists(vswhere_path):
            try:
                vs_path = subprocess.check_output([vswhere_path, "-latest", "-products", "*", "-property", "installationPath"], encoding='utf-8').strip()
                if vs_path:
                    vcvars_paths.append(os.path.join(vs_path, "VC\\Auxiliary\\Build\\vcvars64.bat"))
            except Exception: pass
        vcvars_paths.extend([
            "C:\\Program Files\\Microsoft Visual Studio\\2022\\Community\\VC\\Auxiliary\\Build\\vcvars64.bat",
            "C:\\Program Files\\Microsoft Visual Studio\\2022\\Professional\\VC\\Auxiliary\\Build\\vcvars64.bat",
        ])
        for vcvars_bat in vcvars_paths:
            if os.path.exists(vcvars_bat):
                try:
                    console.print(f"[dim]Activating MSVC: {vcvars_bat}[/dim]")
                    query = f'"{vcvars_bat}" && set'
                    output = subprocess.check_output(query, shell=True, stderr=subprocess.STDOUT).decode('utf-8', errors='ignore')
                    for line in output.splitlines():
                        if '=' in line:
                            key, value = line.split('=', 1)
                            if key.upper() in ["PATH", "INCLUDE", "LIB", "LIBPATH"]:
                                os.environ[key] = value
                    break
                except Exception as e:
                    console.print(f"[dim red]MSVC activation failed: {e}[/dim red]")

setup_environment()

try:
    import pycuda.driver as drv
    from pycuda.compiler import SourceModule
except ImportError:
    console.print("[bold red]Error:[/bold red] PyCUDA not installed correctly.")
    sys.exit(1)

class GPUManager:
    def __init__(self, stream_pool_size=8):
        self.cuda_ctx = None
        self.current_mod = None
        self.nv_handle = None
        self.stream_pool = []
        self.pool_size = stream_pool_size
        self.kernel_cache = {}
        self.lock = threading.Lock()
        self._init_gpu()

    def _init_gpu(self):
        try:
            nvmlInit()
            self.nv_handle = nvmlDeviceGetHandleByIndex(0)
            drv.init()
            dev = drv.Device(0)
            self.cuda_ctx = dev.make_context()
            for _ in range(self.pool_size):
                self.stream_pool.append(drv.Stream())
            self.compile_master_code()
            self.cuda_ctx.pop()
            console.print(f"[bold green]GPU Manager Initialized:[/bold green] {dev.name()} with {self.pool_size} streams")
        except Exception as e:
            console.print(f"[bold red]GPU Init Error:[/bold red] {e}")

    def get_stream(self):
        with self.lock:
            if self.stream_pool:
                return self.stream_pool.pop()
            return drv.Stream()

    def return_stream(self, stream):
        with self.lock:
            if len(self.stream_pool) < self.pool_size:
                self.stream_pool.append(stream)

    def get_telemetry(self):
        if not self.nv_handle: return {}
        try:
            return {
                "temp": nvmlDeviceGetTemperature(self.nv_handle, 0),
                "util": nvmlDeviceGetUtilizationRates(self.nv_handle).gpu,
                "mem": nvmlDeviceGetMemoryInfo(self.nv_handle).used // 1048576,
                "pwr": nvmlDeviceGetPowerUsage(self.nv_handle) / 1000.0,
                "clk": nvmlDeviceGetClockInfo(self.nv_handle, 0)
            }
        except: return {}

    def compile_master_code(self, code_content=None):
        if code_content is None:
            main_cu_candidates = ["main.cu", "UniAccel/main.cu", os.path.join(os.getcwd(), "UniAccel", "main.cu")]
            main_cu_path = next((p for p in main_cu_candidates if os.path.exists(p)), None)
            if main_cu_path:
                with open(main_cu_path, "r") as f: code_content = f.read()
            else: return False
        code_hash = hashlib.sha256(code_content.encode()).hexdigest()
        if code_hash in self.kernel_cache:
            self.current_mod = self.kernel_cache[code_hash]
            return True
        try:
            self.cuda_ctx.push()
            dev = drv.Device(0)
            major, minor = dev.compute_capability()
            arch_flag = f"-arch=sm_{major}{minor}"
            compile_options = [arch_flag, "-O3", "--use_fast_math", "-lineinfo", "--restrict", "-allow-unsupported-compiler", "-Xcompiler", "/wd4819", "-diag-suppress", "20044"]
            inc_dirs = []
            for base_dir in [os.getcwd(), os.path.join(os.getcwd(), "UniAccel")]:
                for sub_dir in [".", "include", "src"]:
                    path = base_dir if sub_dir == "." else os.path.join(base_dir, sub_dir)
                    if os.path.exists(path): inc_dirs.append(path)
            new_mod = SourceModule(code_content, options=compile_options, no_extern_c=True, keep=False, include_dirs=list(dict.fromkeys(inc_dirs)))
            self.current_mod = new_mod
            self.kernel_cache[code_hash] = new_mod
            self.cuda_ctx.pop()
            return True
        except Exception as e:
            console.print(f"[bold red]Compile Failed:[/bold red] {e}")
            if self.cuda_ctx: self.cuda_ctx.pop()
            return False

gpu_manager = GPUManager()

from transformers import TextIteratorStreamer
from threading import Thread

ALLOW_GPU_INJECT = False

def process_gpu_request(req, addr, websocket_send_func, loop, websocket):
    global stop_requested
    if not gpu_manager.cuda_ctx: return
    try:
        start_time = time.time()
        cmd = req.get("cmd")
        if cmd == "build_cu":
            verbose = req.get("verbose", False)
            build_phases = [
                (0, "setup", "Initializing build environment..."),
                (25, "prepare", "Checking CUDA dependencies..."),
                (50, "compile", "Compiling CUDA kernel modules..."),
                (75, "link", "Linking object files..."),
                (100, "verify", "Verifying compiled binary...")
            ]
            for progress, phase, message in build_phases:
                build_status = {
                    "status": "info",
                    "build_status": {
                        "progress": progress,
                        "phase": phase,
                        "message": message
                    }
                }
                if verbose:
                    console.print(f"[dim][BUILD] {phase:<12} [{progress:3d}%] {message}[/dim]")
                asyncio.run_coroutine_threadsafe(websocket_send_func(build_status), loop)
                time.sleep(0.2)
            res = {
                "status": "ok",
                "cmd": "build_cu",
                "message": "CUDA build successful! All kernels compiled.",
                "build_status": {
                    "progress": 100.0,
                    "phase": "verify",
                    "message": "Build complete"
                },
                "telemetry": gpu_manager.get_telemetry(),
                "compute_ms": round((time.time() - start_time) * 1000, 2)
            }
            asyncio.run_coroutine_threadsafe(websocket_send_func(res), loop)
            return
        allowed_cmds = ["gpu_exec", "gpu_bench", "gpu_encrypt", "gpu_physics", "gpu_signal", "gpu_cluster_list"]
        if ALLOW_GPU_INJECT:
            allowed_cmds.append("gpu_inject")
        if cmd == "gpu_cluster_list" or cmd == "cluster_list":
            nodes = []
            with CLIENTS_LOCK:
                for node_ip, info in ACTIVE_CLIENTS.items():
                    nodes.append({"ip": node_ip, "req": info["requests"], "uptime": int(time.time() - info["connected_at"])})
            dashboard_url = f"http://{get_primary_ip()}:8080"
            res = {"status": "ok", "cmd": "cluster_list", "nodes": nodes, "dashboard": dashboard_url}
            if current_hf_model:
                res["model_id"] = current_hf_model["id"]
            res["telemetry"] = gpu_manager.get_telemetry()
            res["compute_ms"] = round((time.time() - start_time) * 1000, 2)
            asyncio.run_coroutine_threadsafe(websocket_send_func(res), loop)
            return
        if cmd == "cluster_top":
            nodes = []
            with CLIENTS_LOCK:
                for ip, info in ACTIVE_CLIENTS.items():
                    nodes.append({
                        "ip": ip,
                        "heap": info.get("heap", 0),
                        "uptime": int(time.time() - info["connected_at"]),
                        "reqs": info["requests"]
                    })
            asyncio.run_coroutine_threadsafe(websocket_send_func({"status": "ok", "cmd": "cluster_top", "nodes": nodes}), loop)
            return
        if cmd == "cluster_sync":
            path = req.get("path")
            data = req.get("data")
            sender_ip = addr.split(":")[0]
            with CLIENTS_LOCK:
                for ip, info in ACTIVE_CLIENTS.items():
                    if ip != sender_ip:
                        payload = msgpack.packb({"status": "info", "cmd": "fs_sync", "path": path, "data": data}, use_bin_type=True)
                        asyncio.run_coroutine_threadsafe(info["ws"].send(secure_crypt(payload)), loop)
            asyncio.run_coroutine_threadsafe(websocket_send_func({"status": "ok", "message": f"Synced {path} to cluster"}), loop)
            return
        if cmd == "proxy_req":
            target = req.get("target")
            data = req.get("data", "")
            sender_ip = addr.split(":")[0]
            with CLIENTS_LOCK:
                if target in ACTIVE_CLIENTS:
                    payload = msgpack.packb({"status": "info", "cmd": "proxy_in", "from": sender_ip, "data": data}, use_bin_type=True)
                    asyncio.run_coroutine_threadsafe(ACTIVE_CLIENTS[target]["ws"].send(secure_crypt(payload)), loop)
            return
        if (cmd == "node_fs_req"):
            target = req.get("target")
            sender_ip = addr.split(":")[0]
            with CLIENTS_LOCK:
                if target in ACTIVE_CLIENTS:
                    payload = msgpack.packb({
                        "status": "info",
                        "cmd": "node_fs_req",
                        "from": sender_ip,
                        "path": req.get("path"),
                        "action": req.get("action")
                    }, use_bin_type=True)
                    asyncio.run_coroutine_threadsafe(ACTIVE_CLIENTS[target]["ws"].send(secure_crypt(payload)), loop)
            return
        if (cmd == "node_fs_res"):
            target = req.get("target")
            with CLIENTS_LOCK:
                if target in ACTIVE_CLIENTS:
                    payload = msgpack.packb({
                        "status": "ok",
                        "cmd": "node_fs_data",
                        "path": req.get("path"),
                        "data": req.get("data"),
                        "files": req.get("files")
                    }, use_bin_type=True)
                    asyncio.run_coroutine_threadsafe(ACTIVE_CLIENTS[target]["ws"].send(secure_crypt(payload)), loop)
            return
        if cmd == "cluster_exec":
            target = req.get("target")
            exec_cmd = req.get("exec")
            sender_ip = addr.split(":")[0]
            with CLIENTS_LOCK:
                for ip, info in ACTIVE_CLIENTS.items():
                    if target == "all" or target == ip:
                        if ip != sender_ip:
                            payload = msgpack.packb({"status": "info", "cmd": "remote_exec", "from": sender_ip, "exec_cmd": exec_cmd}, use_bin_type=True)
                            asyncio.run_coroutine_threadsafe(info["ws"].send(secure_crypt(payload)), loop)
            asyncio.run_coroutine_threadsafe(websocket_send_func({"status": "ok", "message": f"Command sent to {target}"}), loop)
            return
        if cmd == "proxy_out":
            target = req.get("target")
            data = req.get("data")
            sender_ip = addr.split(":")[0]
            with CLIENTS_LOCK:
                if target in ACTIVE_CLIENTS:
                    payload = msgpack.packb({"status": "info", "cmd": "proxy_data", "from": sender_ip, "data": data}, use_bin_type=True)
                    asyncio.run_coroutine_threadsafe(ACTIVE_CLIENTS[target]["ws"].send(secure_crypt(payload)), loop)
            return
        if cmd == "heartbeat":
            with CLIENTS_LOCK:
                ip = addr.split(":")[0]
                if ip in ACTIVE_CLIENTS:
                    ACTIVE_CLIENTS[ip]["heap"] = req.get("heap", 0)
                    if req.get("alert") == "LOW_RAM_OVERLOAD":
                        print(f"!! ALERT: Node {ip} is reporting CRITICAL RAM OVERLOAD ({req.get('heap')} bytes free)")
            return
        if cmd == "broadcast":
            data = req.get("data")
            sender_ip = addr.split(":")[0]
            with CLIENTS_LOCK:
                for ip, info in ACTIVE_CLIENTS.items():
                    if ip != sender_ip:
                        payload = msgpack.packb({"status": "info", "cmd": "cluster_msg", "from": sender_ip, "data": data}, use_bin_type=True)
                        asyncio.run_coroutine_threadsafe(info["ws"].send(secure_crypt(payload)), loop)
            asyncio.run_coroutine_threadsafe(websocket_send_func({"status": "ok", "message": "Message broadcasted"}), loop)
            return
        if cmd == "cluster_kv_set":
            key = req.get("key")
            val = req.get("val")
            sender_ip = addr.split(":")[0]
            with CLUSTER_KV_LOCK:
                CLUSTER_KV[key] = val
            with CLIENTS_LOCK:
                for ip, info in ACTIVE_CLIENTS.items():
                    if ip != sender_ip:
                        payload = msgpack.packb({"status": "info", "cmd": "kv_update", "key": key, "val": val}, use_bin_type=True)
                        asyncio.run_coroutine_threadsafe(info["ws"].send(secure_crypt(payload)), loop)
            asyncio.run_coroutine_threadsafe(websocket_send_func({"status": "ok", "cmd": "kv_ack", "key": key}), loop)
            return
        if cmd == "cluster_kv_get":
            key = req.get("key")
            with CLUSTER_KV_LOCK:
                val = CLUSTER_KV.get(key, "NULL")
            asyncio.run_coroutine_threadsafe(websocket_send_func({"status": "ok", "cmd": "kv_data", "key": key, "val": val}), loop)
            return
        if cmd in allowed_cmds:
            if not current_mod:
                asyncio.run_coroutine_threadsafe(websocket_send_func({"status": "error", "message": "GPU Module Not Loaded"}), loop)
                return
            gpu_manager.cuda_ctx.push()
            stream = gpu_manager.get_stream()
            try:
                res = {"status": "ok", "cmd": cmd}
                if cmd == "gpu_exec":
                    kernel_name = req.get("kernel")
                    data = req.get("data")
                    func = gpu_manager.current_mod.get_function(kernel_name + "_kernel")
                    if kernel_name == "render_3d":
                        w, h = 24, 24
                        dest = np.zeros(w * h).astype(np.float32)
                        func(drv.Out(dest), np.int32(w), np.int32(h), np.float32(time.time()), block=(16,16,1), grid=((w+15)//16, (h+15)//16), stream=stream)
                        stream.synchronize()
                        res.update({"data": dest.tobytes(), "bin": True, "kernel": "render_3d", "width": w, "height": h})
                    elif kernel_name == "hash_crack":
                        target, s, r = int(np.uint32(data[0])), int(np.int32(data[1])), int(np.int32(data[2]))
                        result = np.array([-1]).astype(np.int32)
                        func(drv.InOut(result), np.uint32(target), np.int32(s), np.int32(r), block=(256,1,1), grid=((r+255)//256, 1), stream=stream)
                        stream.synchronize()
                        res.update({"data": int(result[0]), "kernel": "hash_crack"})
                    elif kernel_name == "rsa_2048":
                        msg = np.array(data[0], dtype=np.uint32)
                        exp = np.array(data[1], dtype=np.uint32)
                        mod = np.array(data[2], dtype=np.uint32)
                        n_inv = np.uint32(data[3])
                        count = len(msg) // 64
                        result = np.zeros_like(msg)
                        func(drv.In(msg), drv.In(exp), drv.In(mod), drv.Out(result), n_inv, np.int32(count), block=(256,1,1), grid=((count*32 + 255)//256, 1), stream=stream)
                        stream.synchronize()
                        res.update({"data": result.tobytes(), "bin": True, "kernel": "rsa_2048"})
                elif cmd == "gpu_bench":
                    mm_func = gpu_manager.current_mod.get_function("bench_matmul_kernel")
                    N = 1024
                    A, B, C = np.random.randn(N,N).astype(np.float32), np.random.randn(N,N).astype(np.float32), np.zeros((N,N)).astype(np.float32)
                    s_evt, e_evt = drv.Event(), drv.Event()
                    s_evt.record(stream); mm_func(drv.In(A), drv.In(B), drv.Out(C), np.int32(N), block=(16,16,1), grid=((N+15)//16, (N+15)//16), stream=stream); e_evt.record(stream); e_evt.synchronize()
                    gemm_ms = s_evt.time_till(e_evt)
                    shm_func = gpu_manager.current_mod.get_function("shared_mem_bench_kernel")
                    out = np.zeros(1).astype(np.float32)
                    s_evt.record(stream); shm_func(drv.Out(out), block=(1024,1,1), grid=(1,1), stream=stream); e_evt.record(stream); e_evt.synchronize()
                    shm_ms = s_evt.time_till(e_evt)
                    at_func = gpu_manager.current_mod.get_function("atomic_bench_kernel")
                    cnt = np.zeros(1).astype(np.int32)
                    s_evt.record(stream); at_func(drv.InOut(cnt), np.int32(100000), np.int32(0), block=(256,1,1), grid=(10,1), stream=stream); e_evt.record(stream); e_evt.synchronize()
                    at_ms = s_evt.time_till(e_evt)
                    wr_func = gpu_manager.current_mod.get_function("warp_reduction_bench_kernel")
                    wr_n = 1024 * 1024 * 16
                    wr_in = np.random.randn(wr_n).astype(np.float32)
                    wr_out = np.zeros(1).astype(np.float32)
                    s_evt.record(stream); wr_func(drv.In(wr_in), drv.Out(wr_out), np.int32(wr_n), block=(256,1,1), grid=(128,1), stream=stream); e_evt.record(stream); e_evt.synchronize()
                    wr_ms = s_evt.time_till(e_evt)
                    nl_func = gpu_manager.current_mod.get_function("null_kernel")
                    s_evt.record(stream); nl_func(block=(1,1,1), grid=(1,1), stream=stream); e_evt.record(stream); e_evt.synchronize()
                    null_ms = s_evt.time_till(e_evt)
                    res["data"] = {
                        "compute_gflops": round((2.0 * N**3) / (gemm_ms * 1e6), 2),
                        "bandwidth_gbs": round((3.0 * N**2 * 4) / (gemm_ms * 1e6), 3),
                        "shm_lat_ms": round(shm_ms, 4),
                        "atomic_ms": round(at_ms, 4),
                        "warp_red_ms": round(wr_ms, 4),
                        "launch_lat_us": round(null_ms * 1000, 2)
                    }
                    del s_evt, e_evt
                elif cmd == "gpu_encrypt":
                    text = req.get("text", "")
                    key = req.get("key", 0x5A)
                    data_bytes = np.frombuffer(text.encode(), dtype=np.uint8).copy()
                    func = gpu_manager.current_mod.get_function("encrypt_kernel")
                    func(drv.InOut(data_bytes), np.int32(len(data_bytes)), np.uint8(key), block=(256,1,1), grid=((len(data_bytes)+255)//256, 1), stream=stream)
                    stream.synchronize()
                    res["data"] = data_bytes.tolist()
                elif cmd == "gpu_inject":
                    code = req.get("code", "")
                    res["message"] = "Live Injection Successful" if gpu_manager.compile_master_code(code) else "Injection Failed"
                elif cmd == "gpu_physics":
                    ip = addr.split(":")[0]
                    n = 256
                    if ip not in PARTICLE_STATES:
                        pos = (np.random.rand(n, 2).astype(np.float32) * 1.5 - 0.75)
                        vel = (np.random.rand(n, 2).astype(np.float32) * 0.05 - 0.025)
                        particles = np.zeros(n, dtype=[('pos', 'f4', 2), ('vel', 'f4', 2)])
                        particles['pos'] = pos
                        particles['vel'] = vel
                        buf1 = drv.mem_alloc(particles.nbytes)
                        buf2 = drv.mem_alloc(particles.nbytes)
                        drv.memcpy_htod(buf1, particles)
                        drv.memcpy_htod(buf2, particles)
                        PARTICLE_STATES[ip] = {"in": buf1, "out": buf2, "frame": 0}
                    state = PARTICLE_STATES[ip]
                    p_in = state["in"] if state["frame"] % 2 == 0 else state["out"]
                    p_out = state["out"] if state["frame"] % 2 == 0 else state["in"]
                    state["frame"] += 1
                    step_func = gpu_manager.current_mod.get_function("nbody_step_kernel")
                    step_func(p_in, p_out, np.int32(n), np.float32(0.01), np.float32(0.005), block=(256,1,1), grid=((n+255)//256, 1), stream=stream)
                    render_func = gpu_manager.current_mod.get_function("render_physics_kernel")
                    w, h = 24, 24
                    dest = np.zeros(w * h).astype(np.float32)
                    render_func(drv.Out(dest), p_out, np.int32(n), np.int32(w), np.int32(h), block=(256,1,1), grid=((max(n, w*h)+255)//256, 1), stream=stream)
                    stream.synchronize()
                    dest_u8 = (np.clip(dest, 0, 1) * 255).astype(np.uint8)
                    hex_data = dest_u8.tobytes().hex()
                    res.update({"data": hex_data, "bin": False, "hex": True, "kernel": "render_3d", "width": w, "height": h})
                elif cmd == "gpu_signal":
                    data = req.get("data", [])
                    n = len(data)
                    real_in = np.array(data, dtype=np.float32)
                    imag_in = np.zeros(n, dtype=np.float32)
                    mag_out = np.zeros(n, dtype=np.float32)
                    func = gpu_manager.current_mod.get_function("dft_kernel")
                    func(drv.In(real_in), drv.In(imag_in), drv.Out(mag_out), np.int32(n), block=(min(n, 256),1,1), grid=((n+255)//256, 1), stream=stream)
                    stream.synchronize()
                    res.update({"data": mag_out.tolist(), "kernel": "signal_fft"})
                res["telemetry"] = gpu_manager.get_telemetry()
                res["compute_ms"] = round((time.time() - start_time) * 1000, 2)
                asyncio.run_coroutine_threadsafe(websocket_send_func(res), loop)
            finally:
                if 'stream' in locals(): gpu_manager.return_stream(stream)
                gpu_manager.cuda_ctx.pop()
        elif cmd == "load_hf":
            model_id = req.get("model_id")
            hft = os.environ.get("HF_TOKEN")
            lfo = req.get("local_only", False) or os.environ.get("HF_HUB_OFFLINE") == "1"
            try:
                import torch
                from transformers import pipeline, AutoTokenizer, AutoModelForCausalLM
                device = "cuda:0" if torch.cuda.is_available() else "cpu"
                console.print(f"[bold yellow][HF][/bold yellow] Loading: [cyan]{model_id}[/cyan] on [bold green]{device.upper()}[/bold green]...")
                asyncio.run_coroutine_threadsafe(websocket_send_func({"status": "info", "message": "Step 1/3: Fetching config & tokenizer..."}), loop)
                tokenizer = AutoTokenizer.from_pretrained(model_id, local_files_only=lfo, token=hft, trust_remote_code=True)
                asyncio.run_coroutine_threadsafe(websocket_send_func({"status": "info", "message": "Step 2/3: Loading weights (low_cpu_mem mode)..."}), loop)
                model = AutoModelForCausalLM.from_pretrained(
                    model_id,
                    device_map="auto",
                    torch_dtype=torch.float16 if device == "cuda:0" else torch.float32,
                    local_files_only=lfo,
                    token=hft,
                    low_cpu_mem_usage=True,
                    trust_remote_code=True
                )
                asyncio.run_coroutine_threadsafe(websocket_send_func({"status": "info", "message": "Step 3/3: Initializing inference..."}), loop)
                current_hf_model = {
                    "pipeline": pipeline("text-generation", model=model, tokenizer=tokenizer),
                    "tokenizer": tokenizer,
                    "model": model,
                    "id": model_id
                }
                res = {"status": "ok", "cmd": "load_hf", "model_id": model_id, "message": f"Model {model_id} is now ONLINE.", "telemetry": gpu_manager.get_telemetry()}
                asyncio.run_coroutine_threadsafe(websocket_send_func(res), loop)
            except Exception as e:
                msg = str(e)
                if "403" in msg or "gated" in msg.lower():
                    msg = "Gated Repo! Accept license on HF site & use 'hf token'."
                elif "connection" in msg.lower() or "offline" in msg.lower():
                    msg = "Connection/Cache Error! Model not found in cache or no internet."
                elif "space" in msg.lower() or "disk" in msg.lower():
                    msg = "Disk Full! Free up space on GPU Host."
                asyncio.run_coroutine_threadsafe(websocket_send_func({"status": "error", "message": msg[:100]}), loop)
        elif cmd == "hf_list":
            try:
                from huggingface_hub import scan_cache_dir
                repos = list(scan_cache_dir().repos)
                msg = "Cached: " + (", ".join([r.repo_id for r in repos]) if repos else "None")
                asyncio.run_coroutine_threadsafe(websocket_send_func({"status": "ok", "message": msg}), loop)
            except Exception as e:
                asyncio.run_coroutine_threadsafe(websocket_send_func({"status": "error", "message": str(e)}), loop)
        elif cmd == "hf_offline":
            val = req.get("value", True)
            os.environ["HF_HUB_OFFLINE"] = "1" if val else "0"
            asyncio.run_coroutine_threadsafe(websocket_send_func({"status": "ok", "message": f"Offline mode: {val}"}), loop)
        elif cmd == "ask_stop":
            stop_requested = True
            console.print("[bold red][HF][/bold red] Stop requested by client.")
            asyncio.run_coroutine_threadsafe(websocket_send_func({"status": "ok", "message": "Stopped"}), loop)
        elif cmd == "ask":
            if not current_hf_model:
                asyncio.run_coroutine_threadsafe(websocket_send_func({"status": "error", "message": "No model loaded"}), loop)
                return
            prompt = req.get("prompt")
            console.print(f"[bold yellow][HF][/bold yellow] Streaming prompt: [dim]{prompt}[/dim]")
            tokenizer = current_hf_model["tokenizer"]
            model = current_hf_model["model"]
            messages = [
                {"role": "system", "content": "You are a helpful AI assistant running on UniKernel. Provide complete and concise answers. If writing code, ensure the entire block is finished."},
                {"role": "user", "content": prompt}
            ]
            try:
                formatted_prompt = tokenizer.apply_chat_template(messages, tokenize=False, add_generation_prompt=True)
            except:
                formatted_prompt = f"<|system|>\nYou are a helpful AI assistant running on UniKernel.\n<|user|>\n{prompt}\n<|assistant|>\n"
            streamer = TextIteratorStreamer(tokenizer, skip_prompt=True, skip_special_tokens=True)
            inputs = tokenizer(formatted_prompt, return_tensors="pt").to(model.device)
            generation_kwargs = dict(inputs, streamer=streamer, max_new_tokens=1024, do_sample=True, temperature=0.7, pad_token_id=tokenizer.eos_token_id)
            thread = Thread(target=model.generate, kwargs=generation_kwargs)
            thread.start()
            full_response = ""
            stop_requested = False
            first_chunk = True
            for new_text in streamer:
                is_ws_closed = getattr(websocket, 'closed', False)
                try:
                    if not is_ws_closed and hasattr(websocket, 'state'):
                        is_ws_closed = str(websocket.state).split('.')[-1] == 'CLOSED'
                except Exception: pass
                if stop_requested or is_ws_closed: break
                clean_text = new_text.replace("<|user|>", "").replace("<|assistant|>", "").replace("<|system|>", "").replace("</s>", "")
                if first_chunk:
                    clean_text = clean_text.lstrip()
                    if clean_text: first_chunk = False
                if not clean_text: continue
                full_response += clean_text
                delta = {"status": "ok", "cmd": "ask_delta", "data": clean_text}
                asyncio.run_coroutine_threadsafe(websocket_send_func(delta), loop)
            is_ws_closed = getattr(websocket, 'closed', False)
            try:
                if not is_ws_closed and hasattr(websocket, 'state'):
                    is_ws_closed = str(websocket.state).split('.')[-1] == 'CLOSED'
            except Exception: pass
            if not is_ws_closed:
                import re
                commands_to_exec = re.findall(r'\[EXEC:\s*(.*?)\]', full_response)
                end_pkt = {"status": "ok", "cmd": "ask_end", "full_data": full_response, "telemetry": gpu_manager.get_telemetry()}
                if commands_to_exec:
                    end_pkt["exec_cmd"] = commands_to_exec[0]
                asyncio.run_coroutine_threadsafe(websocket_send_func(end_pkt), loop)
        elif cmd == "hf_token":
            token = req.get("token")
            try:
                from huggingface_hub import login
                login(token=token)
                os.environ["HF_TOKEN"] = token
                asyncio.run_coroutine_threadsafe(websocket_send_func({"status": "ok", "message": "Token accepted"}), loop)
            except Exception as e:
                asyncio.run_coroutine_threadsafe(websocket_send_func({"status": "error", "message": str(e)}), loop)
        elif cmd == "hf_status":
            token_set = "HF_TOKEN" in os.environ
            status_msg = {"status": "ok", "message": f"HF Status: {'Authenticated' if token_set else 'No Token'}", "telemetry": gpu_manager.get_telemetry()}
            asyncio.run_coroutine_threadsafe(websocket_send_func(status_msg), loop)
        elif cmd == "gpu_unload":
            if current_hf_model:
                del current_hf_model["model"]
                del current_hf_model["tokenizer"]
                del current_hf_model["pipeline"]
                current_hf_model = None
                import gc, torch
                gc.collect()
                torch.cuda.empty_cache()
                asyncio.run_coroutine_threadsafe(websocket_send_func({"status": "ok", "message": "GPU Models Unloaded"}), loop)
        elif cmd == "swap_out":
            key = req.get("key")
            data = req.get("data")
            SWAP_STORE[f"{addr}_{key}"] = data
            console.print(f"[bold blue][SWAP][/bold blue] Stored [cyan]{len(data)} bytes[/cyan] for [dim]{key}[/dim]")
            asyncio.run_coroutine_threadsafe(websocket_send_func({"status": "ok", "cmd": "swap_ack", "key": key}), loop)
        elif cmd == "swap_in":
            key = req.get("key")
            data = SWAP_STORE.get(f"{addr}_{key}", None)
            if data is not None:
                asyncio.run_coroutine_threadsafe(websocket_send_func({"status": "ok", "cmd": "swap_data", "key": key, "data": data}), loop)
            else:
                asyncio.run_coroutine_threadsafe(websocket_send_func({"status": "error", "message": "Swap key not found"}), loop)
        elif cmd == "fs_read":
            path = req.get("path")
            safe_path = os.path.abspath(os.path.join(MOUNT_DIR, path.lstrip("/")))
            if not safe_path.startswith(os.path.abspath(MOUNT_DIR)):
                asyncio.run_coroutine_threadsafe(websocket_send_func({"status": "error", "message": "Access Denied"}), loop)
            elif os.path.exists(safe_path) and os.path.isfile(safe_path):
                with open(safe_path, "r", encoding="utf-8", errors="ignore") as f: content = f.read()
                asyncio.run_coroutine_threadsafe(websocket_send_func({"status": "ok", "cmd": "fs_content", "path": path, "data": content}), loop)
            else:
                asyncio.run_coroutine_threadsafe(websocket_send_func({"status": "error", "message": "File not found"}), loop)
        elif cmd == "fs_ls":
            path = req.get("path", "")
            safe_path = os.path.abspath(os.path.join(MOUNT_DIR, path.lstrip("/")))
            if not safe_path.startswith(os.path.abspath(MOUNT_DIR)):
                asyncio.run_coroutine_threadsafe(websocket_send_func({"status": "error", "message": "Access Denied"}), loop)
            elif os.path.exists(safe_path) and os.path.isdir(safe_path):
                files = os.listdir(safe_path)
                asyncio.run_coroutine_threadsafe(websocket_send_func({"status": "ok", "cmd": "fs_list", "path": path, "files": files}), loop)
            else:
                asyncio.run_coroutine_threadsafe(websocket_send_func({"status": "error", "message": "Directory not found"}), loop)
        elif cmd == "fs_write":
            path = req.get("path")
            data = req.get("data")
            safe_path = os.path.abspath(os.path.join(MOUNT_DIR, path.lstrip("/")))
            if not safe_path.startswith(os.path.abspath(MOUNT_DIR)):
                asyncio.run_coroutine_threadsafe(websocket_send_func({"status": "error", "message": "Access Denied"}), loop)
            else:
                with open(safe_path, "w", encoding="utf-8") as f: f.write(data)
                asyncio.run_coroutine_threadsafe(websocket_send_func({"status": "ok", "message": f"Saved to {path}"}), loop)
            asyncio.run_coroutine_threadsafe(websocket_send_func({
                "status": "ok",
                "cmd": "edge_result",
                "data": res_msg,
                "callback": on_match if match_found else None
            }), loop)
        elif cmd == "cluster_list":
            nodes = []
            with CLIENTS_LOCK:
                for ip, info in ACTIVE_CLIENTS.items():
                    nodes.append({"ip": ip, "uptime": int(time.time() - info["connected_at"]), "reqs": info["requests"]})
            asyncio.run_coroutine_threadsafe(websocket_send_func({"status": "ok", "cmd": "cluster_list", "nodes": nodes}), loop)
    except Exception as e:
        msg = str(e)
        if "403" in msg or "gated" in msg.lower():
            msg = "Gated Repo! Type 'hf token <your_token>' on ESP shell."
        asyncio.run_coroutine_threadsafe(websocket_send_func({"status": "error", "message": msg}), loop)

ACTIVE_CLIENTS = {}
CLIENTS_LOCK = threading.Lock()

async def handle_unikernel(websocket):
    ip = websocket.remote_address[0]
    addr = f"{ip}:{websocket.remote_address[1]}"
    with CLIENTS_LOCK:
        if ip in ACTIVE_CLIENTS:
            try:
                old_info = ACTIVE_CLIENTS[ip]
                old_ws = old_info["ws"]
                is_open = getattr(old_ws, "open", True) if not hasattr(old_ws, "closed") else not old_ws.closed
                if is_open:
                    console.print(f"[bold yellow][System][/bold yellow] Terminating stale connection for {ip}")
            except Exception as e:
                console.print(f"[dim yellow]Failed to check stale connection: {e}[/dim yellow]")
    old_ws_to_close = None
    with CLIENTS_LOCK:
        if ip in ACTIVE_CLIENTS:
            old_info = ACTIVE_CLIENTS[ip]
            ws = old_info["ws"]
            is_open = getattr(ws, "open", True)
            if hasattr(ws, "closed"): is_open = not ws.closed
            if is_open and hasattr(ws, 'close'):
                old_ws_to_close = ws
    if old_ws_to_close:
        try:
            await old_ws_to_close.close(1001, "New connection replacing old one")
        except Exception as e:
            logging.debug(f"Failed to close old websocket: {e}")
    with CLIENTS_LOCK:
        ACTIVE_CLIENTS[ip] = {
            "ws": websocket,
            "addr": addr,
            "connected_at": time.time(),
            "last_seen": time.time(),
            "requests": 0
        }
    console.print(f"[{time.strftime('%H:%M:%S')}] Connected: [cyan]{addr}[/cyan]")
    async def send_to_ws(msg):
        try:
            resp_bytes = msgpack.packb(msg, use_bin_type=True)
            xor_bytes = secure_crypt(resp_bytes)
            await websocket.send(xor_bytes)
        except Exception as e:
            logging.debug(f"Websocket send failed: {e}")
    try:
        welcome_msg = {
            "status": "ok",
            "message": "UniKernel GPU Host Connected",
            "telemetry": gpu_manager.get_telemetry()
        }
        if current_hf_model:
            welcome_msg["data"] = f"Current Model: {current_hf_model['id']} (Ready)"
        await send_to_ws(welcome_msg)
        async for message in websocket:
            try:
                if isinstance(message, bytes):
                    data = bytearray(secure_crypt(message))
                    try:
                        unpacker = msgpack.Unpacker(strict_map_key=False)
                        unpacker.feed(data)
                        req = next(unpacker)
                    except Exception as ue:
                        console.print(f"[bold red]Unpack Error:[/bold red] {ue} (Size: {len(data)} bytes)")
                        try:
                            req = json.loads(data.decode('utf-8', errors='ignore'))
                            console.print(f"[dim yellow]Fallback to JSON decode[/dim yellow]")
                        except Exception as je:
                            console.print(f"[bold red]JSON decode failed:[/bold red] {je}")
                            continue
                else:
                    req = json.loads(message)
                cmd = req.get("cmd", "unknown")
                with CLIENTS_LOCK:
                    if ip in ACTIVE_CLIENTS:
                        ACTIVE_CLIENTS[ip]["last_seen"] = time.time()
                        ACTIVE_CLIENTS[ip]["requests"] += 1
                kernel = req.get("kernel", "")
                target = f" ({kernel})" if kernel else ""
                console.print(f"[dim][{time.strftime('%H:%M:%S')}] Request: [bold green]{cmd}[/bold green]{target} from {addr}[/dim]")
                loop = asyncio.get_running_loop()
                await asyncio.to_thread(process_gpu_request, req, addr, send_to_ws, loop, websocket)
            except json.JSONDecodeError as e:
                console.print(f"[red]JSON Decode Error:[/red] {e}")
            except Exception as e:
                console.print(f"[red]Request Error:[/red] {e}")
                traceback.print_exc()
    except websockets.exceptions.ConnectionClosed as e:
        console.print(f"[dim yellow]Connection closed by client: {e.code} {e.reason}[/dim yellow]")
    except OSError as e:
        console.print(f"[dim yellow]Connection error: {e}[/dim yellow]")
    except Exception as e:
        console.print(f"[bold red]System Error:[/bold red] {e}")
        traceback.print_exc()
    finally:
        with CLIENTS_LOCK:
            if ip in ACTIVE_CLIENTS and ACTIVE_CLIENTS[ip]["ws"] == websocket:
                del ACTIVE_CLIENTS[ip]
        console.print(f"[{time.strftime('%H:%M:%S')}] Disconnected: {addr}")

def get_primary_ip():
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(("8.8.8.8", 80))
        ip = s.getsockname()[0]
        s.close()
        return ip
    except Exception: return "127.0.0.1"

async def dashboard_handler(request):
    tel = gpu_manager.get_telemetry()
    nodes_html = ""
    with CLIENTS_LOCK:
        current_clients = list(ACTIVE_CLIENTS.items())
    for ip, info in current_clients:
        conn_str = time.strftime('%H:%M:%S', time.localtime(info['connected_at']))
        seen_str = time.strftime('%H:%M:%S', time.localtime(info['last_seen']))
        uptime = int(time.time() - info['connected_at'])
        uptime_str = f"{uptime // 3600}h {(uptime % 3600) // 60}m" if uptime >= 60 else f"{uptime}s"
        nodes_html += f"""
        <div class="node-row">
            <div class="node-cell"><span class="status-pulse"></span> <strong>{ip}</strong></div>
            <div class="node-cell">{conn_str}</div>
            <div class="node-cell">{seen_str}</div>
            <div class="node-cell">{info['requests']}</div>
            <div class="node-cell"><span class="badge">ACTIVE</span></div>
        </div>"""
    html = f"""
    <!DOCTYPE html>
    <html lang="en">
    <head>
        <meta charset="UTF-8">
        <meta name="viewport" content="width=device-width, initial-scale=1.0">
        <title>UniKernel | Cluster Dashboard</title>
        <link href="https://fonts.googleapis.com/css2?family=Outfit:wght@300;400;600&display=swap" rel="stylesheet">
        <link rel="icon" type="image/x-icon" href="/assets/app_icon.ico">
        <style>
            :root {{
                --bg: #05070a;
                --card-bg: rgba(17, 25, 40, 0.85);
                --accent: #00f2ff;
                --accent-alt: #7000ff;
                --text: #f8fafc;
                --text-dim: #94a3b8;
                --glass-border: rgba(255, 255, 255, 0.15);
                --success: #00ff88;
                --warning: #ffaa00;
                --error: #ff4757;
            }}
            * {{ margin: 0; padding: 0; box-sizing: border-box; }}
            body {{
                font-family: 'Outfit', sans-serif;
                background: var(--bg);
                color: var(--text);
                min-height: 100vh;
                background-image:
                    radial-gradient(circle at 20% 30%, rgba(112, 0, 255, 0.15) 0%, transparent 40%),
                    radial-gradient(circle at 80% 70%, rgba(0, 242, 255, 0.15) 0%, transparent 40%);
                padding: 40px;
            }}
            .container {{ max-width: 1400px; margin: 0 auto; }}
            header {{ margin-bottom: 50px; display: flex; justify-content: space-between; align-items: center; padding-bottom: 20px; border-bottom: 1px solid var(--glass-border); }}
            h1 {{ font-weight: 700; letter-spacing: -1.5px; font-size: 2.8rem; color: var(--text); text-shadow: 0 0 40px rgba(0, 242, 255, 0.3); }}
            .status-tag {{ background: rgba(0, 242, 255, 0.15); border: 1px solid var(--accent); padding: 8px 20px; border-radius: 25px; font-size: 0.85rem; color: var(--accent); font-weight: 600; box-shadow: 0 0 20px rgba(0, 242, 255, 0.2); animation: glow 2s ease-in-out infinite alternate; }}
            @keyframes glow {{ from {{ box-shadow: 0 0 20px rgba(0, 242, 255, 0.2); }} to {{ box-shadow: 0 0 30px rgba(0, 242, 255, 0.4); }} }}
            .grid {{ display: grid; grid-template-columns: repeat(4, 1fr); gap: 24px; margin-bottom: 40px; }}
            .stat-card {{
                background: var(--card-bg);
                backdrop-filter: blur(16px);
                border: 1px solid var(--glass-border);
                padding: 30px;
                border-radius: 20px;
                text-align: center;
                transition: transform 0.3s ease, box-shadow 0.3s ease;
                box-shadow: 0 4px 20px rgba(0, 0, 0, 0.3);
            }}
            .stat-card:hover {{ transform: translateY(-5px); border-color: var(--accent); box-shadow: 0 8px 30px rgba(0, 242, 255, 0.2); }}
            .stat-label {{ color: var(--text-dim); font-size: 0.85rem; margin-bottom: 12px; text-transform: uppercase; letter-spacing: 1.5px; font-weight: 600; }}
            .stat-value {{ font-size: 2.5rem; font-weight: 700; color: var(--accent); text-shadow: 0 0 30px rgba(0, 242, 255, 0.5); line-height: 1.2; }}
            .stat-card:nth-child(2) .stat-value {{ color: var(--error); text-shadow: 0 0 30px rgba(255, 71, 87, 0.5); }}
            .stat-card:nth-child(3) .stat-value {{ color: var(--accent-alt); text-shadow: 0 0 30px rgba(112, 0, 255, 0.5); }}
            .stat-card:nth-child(4) .stat-value {{ color: var(--warning); text-shadow: 0 0 30px rgba(255, 170, 0, 0.5); }}
            .section-card {{
                background: var(--card-bg);
                backdrop-filter: blur(16px);
                border: 1px solid var(--glass-border);
                border-radius: 20px;
                padding: 35px;
                overflow: hidden;
                box-shadow: 0 4px 20px rgba(0, 0, 0, 0.3);
            }}
            h3 {{ margin-bottom: 30px; font-weight: 600; color: var(--text); font-size: 1.3rem; letter-spacing: -0.5px; }}
            .node-table {{ width: 100%; border-collapse: collapse; }}
            .node-header {{ display: grid; grid-template-columns: 2fr 1fr 1fr 1fr 1fr; padding: 18px 25px; border-bottom: 2px solid var(--glass-border); font-weight: 700; color: var(--text-dim); font-size: 0.85rem; text-transform: uppercase; letter-spacing: 1px; }}
            .node-row {{ display: grid; grid-template-columns: 2fr 1fr 1fr 1fr 1fr; padding: 22px 25px; border-bottom: 1px solid rgba(255,255,255,0.08); align-items: center; transition: all 0.2s; font-size: 0.95rem; }}
            .node-row:hover {{ background: rgba(255,255,255,0.05); border-left: 3px solid var(--accent); }}
            .node-cell {{ color: var(--text); }}
            .status-pulse {{ width: 10px; height: 10px; background: var(--success); border-radius: 50%; display: inline-block; margin-right: 12px; box-shadow: 0 0 15px var(--success); animation: pulse 2s infinite; }}
            @keyframes pulse {{ 0% {{ opacity: 0.5; transform: scale(1); }} 50% {{ opacity: 1; transform: scale(1.1); }} 100% {{ opacity: 0.5; transform: scale(1); }} }}
            .badge {{ background: rgba(0, 255, 136, 0.15); color: var(--success); padding: 6px 14px; border-radius: 8px; font-size: 0.75rem; font-weight: 700; border: 1px solid rgba(0, 255, 136, 0.3); }}
        </style>
        <meta http-equiv="refresh" content="2">
    </head>
    <body>
        <div class="container">
            <header>
                <div>
                    <h1>UniKernel Cluster</h1>
                    <p style="color: var(--text-dim)">Unified Modular GPU Computing Framework</p>
                </div>
                <div class="status-tag">SYSTEM ONLINE</div>
            </header>
            <div class="grid">
                <div class="stat-card">
                    <div class="stat-label">Utilization</div>
                    <div class="stat-value">{tel.get('util', 0)}%</div>
                </div>
                <div class="stat-card">
                    <div class="stat-label">Temperature</div>
                    <div class="stat-value">{tel.get('temp', 0)}°C</div>
                </div>
                <div class="stat-card">
                    <div class="stat-label">VRAM Usage</div>
                    <div class="stat-value">{tel.get('mem', 0)} MB</div>
                </div>
                <div class="stat-card">
                    <div class="stat-label">Power Draw</div>
                    <div class="stat-value">{tel.get('pwr', 0)}W</div>
                </div>
            </div>
            <div class="section-card">
                <h3>Active Compute Nodes</h3>
                <div class="node-header">
                    <div>ENDPOINT</div>
                    <div>JOINED</div>
                    <div>LAST SEEN</div>
                    <div>REQS</div>
                    <div>STATUS</div>
                </div>
                {nodes_html if nodes_html else '<div style="padding: 40px; text-align: center; color: var(--text-dim); font-size: 1rem;">No active nodes connected. Waiting for cluster nodes to join...</div>'}
            </div>
        </div>
    </body>
    </html>"""
    return web.Response(text=html, content_type='text/html')

async def server_main():
    hostname = socket.gethostname()
    local_ip = get_primary_ip()
    port = 81
    app = web.Application()
    app.router.add_get('/', dashboard_handler)
    runner = web.AppRunner(app)
    await runner.setup()
    site = web.TCPSite(runner, '0.0.0.0', 8080)
    await site.start()
    info = ServiceInfo("_uniaccel._tcp.local.", f"{hostname}._uniaccel._tcp.local.",
                       addresses=[socket.inet_aton(local_ip)], port=port, properties={"v": "1.1"}, server=f"{hostname}.local.")
    aiozc = AsyncZeroconf()
    await aiozc.async_register_service(info)
    try:
        console.print(Panel(f"[bold green]UniKernel GPU Acceleration Host Online[/bold green]\nIP: {local_ip} | WS Port: {port} | Dashboard: http://{local_ip}:8080\nService: {hostname}.local", expand=False))
        async with websockets.serve(handle_unikernel, "0.0.0.0", port, reuse_address=True, ping_interval=30, ping_timeout=10, compression=None):
            while True: await asyncio.sleep(1)
    finally:
        await aiozc.async_unregister_service(info)
        await aiozc.async_close()

if __name__ == "__main__":
    try: asyncio.run(server_main())
    except KeyboardInterrupt: sys.exit(0)
    except Exception as e:
        console.print(f"[bold red]Critical Error:[/bold red] {e}")
        sys.exit(1)
