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
import subprocess
import logging
from zeroconf import IPVersion, ServiceInfo, Zeroconf
from zeroconf.asyncio import AsyncZeroconf
import torch
from transformers import pipeline


warnings.filterwarnings("ignore", category=UserWarning)
warnings.filterwarnings("ignore", category=DeprecationWarning)
warnings.filterwarnings("ignore", category=FutureWarning)

from pynvml import *
from rich.console import Console
from rich.panel import Panel

console = Console()


def setup_environment():
    if os.name == 'nt':
        vswhere_path = os.path.expandvars("%ProgramFiles(x86)%\\Microsoft Visual Studio\\Installer\\vswhere.exe")
        vcvars_paths = []
        if os.path.exists(vswhere_path):
            try:
                vs_path = subprocess.check_output([vswhere_path, "-latest", "-products", "*", "-property", "installationPath"], encoding='utf-8').strip()
                if vs_path:
                    vcvars_paths.append(os.path.join(vs_path, "VC\\Auxiliary\\Build\\vcvars64.bat"))
            except: pass
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
                except: pass

setup_environment()

try:
    import pycuda.driver as drv
    from pycuda.compiler import SourceModule
except ImportError:
    console.print("[bold red]Error:[/bold red] PyCUDA not installed correctly.")
    sys.exit(1)


current_mod = None
cuda_ctx = None
nv_handle = None
current_hf_model = None


def compile_master_code(code_content=None):
    global current_mod, cuda_ctx
    if code_content is None:
        if os.path.exists("main.cu"):
            with open("main.cu", "r") as f: code_content = f.read()
        else: return False
        
    try:
        if cuda_ctx: cuda_ctx.push()
        major, minor = drv.Device(0).compute_capability()
        arch_flag = f"-arch=sm_{major}{minor}"
        compile_options = [arch_flag, "-O3", "--use_fast_math", "-allow-unsupported-compiler", "-Xcompiler", "/wd4819"]
        inc_dirs = [os.getcwd(), os.path.join(os.getcwd(), "include"), os.path.join(os.getcwd(), "src")]
        
        new_mod = SourceModule(code_content, options=compile_options, no_extern_c=True, keep=False, include_dirs=inc_dirs)
        current_mod = new_mod
        if cuda_ctx: cuda_ctx.pop()
        return True
    except Exception as e:
        console.print(f"[bold red]Compile Failed:[/bold red] {e}")
        if cuda_ctx: cuda_ctx.pop()
        return False


try:
    nvmlInit()
    nv_handle = nvmlDeviceGetHandleByIndex(0)
    drv.init()
    dev = drv.Device(0)
    cuda_ctx = dev.make_context()
    compile_master_code()
    cuda_ctx.pop()
    console.print(f"[bold green]GPU System Ready:[/bold green] {dev.name()}")
except Exception as e:
    console.print(f"[bold red]GPU Init Error:[/bold red] {e}")

def get_telemetry():
    if not nv_handle: return {}
    try:
        return {
            "temp": nvmlDeviceGetTemperature(nv_handle, 0),
            "util": nvmlDeviceGetUtilizationRates(nv_handle).gpu,
            "mem": nvmlDeviceGetMemoryInfo(nv_handle).used // 1048576,
            "pwr": nvmlDeviceGetPowerUsage(nv_handle) / 1000.0,
            "clk": nvmlDeviceGetClockInfo(nv_handle, 0)
        }
    except: return {}

def process_gpu_request(req, addr):
    if not current_mod or not cuda_ctx: return {"status": "error", "message": "GPU Not Ready"}
    cuda_ctx.push()
    try:
        start_time = time.time()
        res = {"status": "ok"}
        cmd = req.get("cmd")
        
        if cmd == "gpu_exec":
            kernel_name = req.get("kernel")
            data = req.get("data")
            func = current_mod.get_function(kernel_name + "_kernel")
            
            if kernel_name == "render_3d":
                w, h = 32, 32
                dest = np.zeros(w * h).astype(np.float32)
                func(drv.Out(dest), np.int32(w), np.int32(h), np.float32(time.time()), block=(16,16,1), grid=((w+15)//16, (h+15)//16))
                res["data"] = dest.tolist()
                res["kernel"] = "render_3d"
                res["width"], res["height"] = w, h
                
            elif kernel_name == "hash_crack":
                target, s, r = int(np.uint32(data[0])), int(np.int32(data[1])), int(np.int32(data[2]))
                result = np.array([-1]).astype(np.int32)
                func(drv.InOut(result), np.uint32(target), np.int32(s), np.int32(r), block=(256,1,1), grid=((r+255)//256, 1))
                res["data"] = int(result[0])
                res["kernel"] = "hash_crack"
                
            elif kernel_name == "prime_search":
                s, r = int(data[0]), int(data[1])
                found, count = np.zeros(1000).astype(np.int32), np.array([0]).astype(np.int32)
                func(drv.Out(found), drv.InOut(count), np.int32(s), np.int32(r), np.int32(512), block=(256,1,1), grid=((r+255)//256, 1), shared=2048)
                res["data"] = found[:count[0]].tolist()
                res["kernel"] = "prime_search"

        elif cmd == "gpu_bench":
            mm_func = current_mod.get_function("matrix_mul_kernel")
            N = 1024
            A, B, C = np.random.randn(N,N).astype(np.float32), np.random.randn(N,N).astype(np.float32), np.zeros((N,N)).astype(np.float32)
            start_evt, end_evt = drv.Event(), drv.Event()
            start_evt.record()
            mm_func(drv.In(A), drv.In(B), drv.Out(C), np.int32(N), block=(16,16,1), grid=((N+15)//16, (N+15)//16))
            end_evt.record(); end_evt.synchronize()
            avg_ms = start_evt.time_till(end_evt)
            res["data"] = {
                "compute_gflops": round((2.0 * N**3) / (avg_ms * 1e-3 * 1e9), 2),
                "bandwidth_gbs": round((3.0 * N**2 * 4) / (avg_ms * 1e-3 * 1e9), 2),
                "launch_lat_us": round(avg_ms * 1000, 2)
            }
            res["cmd"] = "gpu_bench"

        elif cmd == "gpu_encrypt":
            text = req.get("text", "")
            key = req.get("key", 0x5A)
            data_bytes = np.frombuffer(text.encode(), dtype=np.uint8)
            dest = np.zeros_like(data_bytes)
            func = current_mod.get_function("encrypt_kernel")
            func(drv.InOut(data_bytes), np.int32(len(data_bytes)), np.uint8(key), block=(256,1,1), grid=((len(data_bytes)+255)//256, 1))
            res["data"] = data_bytes.tolist()
            res["cmd"] = "gpu_encrypt"

        elif cmd == "gpu_inject":
            code = req.get("code", "")
            if compile_master_code(code):
                res["message"] = "Live Injection Successful"
            else:
                res["status"] = "error"
                res["message"] = "Injection Compile Failed"

        elif cmd == "load_hf":
            model_id = req.get("model_id")
            # Short-name resolution
            presets = {
                "tiny": "TinyLlama/TinyLlama-1.1B-Chat-v1.0",
                "phi": "microsoft/phi-2",
                "gpt2": "gpt2",
                "stable": "stabilityai/stable-code-3b"
            }
            if model_id in presets:
                model_id = presets[model_id]
                
            console.print(f"[bold yellow][HF][/bold yellow] Loading model: [cyan]{model_id}[/cyan]...")

            try:
                device = "cuda:0" if torch.cuda.is_available() else "cpu"
                global current_hf_model
                current_hf_model = pipeline(
                    "text-generation",
                    model=model_id,
                    device=device,
                    torch_dtype=torch.float16 if device == "cuda:0" else torch.float32
                )
                res["message"] = f"Model {model_id} loaded on {device}."
                res["status"] = "ok"
            except Exception as e:
                res["status"] = "error"
                res["message"] = f"HF Load Error: {str(e)}"

        elif cmd == "ask":
            prompt = req.get("prompt")
            if current_hf_model is None:
                res["status"] = "error"
                res["message"] = "No HF model loaded. Use 'accel load [model]'"
            else:
                console.print(f"[bold yellow][HF][/bold yellow] Processing prompt: [dim]{prompt}[/dim]")
                result = current_hf_model(prompt, max_new_tokens=100, do_sample=True, temperature=0.7)
                answer = result[0]['generated_text']
                if answer.startswith(prompt):
                    answer = answer[len(prompt):].strip()
                res["data"] = answer
                res["status"] = "ok"

        elif cmd == "gpu_unload":
            global current_hf_model
            if current_hf_model:
                del current_hf_model
                current_hf_model = None
                import gc
                gc.collect()
                if torch.cuda.is_available():
                    torch.cuda.empty_cache()
                res["message"] = "Model unloaded and VRAM cleared."
            else:
                res["message"] = "No model to unload."
            res["status"] = "ok"

        elif cmd == "gpu_list":
            res["data"] = {
                "tiny": "TinyLlama/TinyLlama-1.1B-Chat-v1.0",
                "phi": "microsoft/phi-2",
                "gpt2": "gpt2",
                "stable": "stabilityai/stable-code-3b"
            }
            res["status"] = "ok"



        res["telemetry"] = get_telemetry()
        res["compute_ms"] = round((time.time() - start_time) * 1000, 2)
        return res
    except Exception as e:
        return {"status": "error", "message": str(e)}
    finally:
        cuda_ctx.pop()

async def handle_unikernel(websocket):
    addr = f"{websocket.remote_address[0]}:{websocket.remote_address[1]}"
    console.print(f"[{time.strftime('%H:%M:%S')}] Connected: [cyan]{addr}[/cyan]")
    try:
        async for message in websocket:
            try:
                if isinstance(message, bytes):
                    message = bytes([b ^ 0x5A for b in message])
                    req = msgpack.unpackb(message)
                else:
                    req = json.loads(message)
                
                res = await asyncio.to_thread(process_gpu_request, req, addr)

                resp_bytes = msgpack.packb(res)
                await websocket.send(bytes([b ^ 0x5A for b in resp_bytes]))
            except Exception as e:
                console.print(f"[red]Error:[/red] {e}")
                break
    finally:
        console.print(f"[{time.strftime('%H:%M:%S')}] Disconnected: {addr}")

def get_primary_ip():
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(("8.8.8.8", 80))
        ip = s.getsockname()[0]
        s.close()
        return ip
    except: return "127.0.0.1"

async def server_main():
    hostname = socket.gethostname()
    local_ip = get_primary_ip()
    port = 81
    info = ServiceInfo("_uniaccel._tcp.local.", f"{hostname}._uniaccel._tcp.local.", 
                       addresses=[socket.inet_aton(local_ip)], port=port, properties={"v": "1.1"}, server=f"{hostname}.local.")
    aiozc = AsyncZeroconf()
    await aiozc.async_register_service(info)
    try:
        console.print(Panel(f"[bold green]UniKernel GPU Acceleration Host Online[/bold green]\nIP: {local_ip} | Port: {port}\nService: {hostname}.local", expand=False))
        async with websockets.serve(handle_unikernel, "0.0.0.0", port, reuse_address=True, ping_interval=None, compression=None):
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
