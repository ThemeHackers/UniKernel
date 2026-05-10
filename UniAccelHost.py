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
from zeroconf import IPVersion, ServiceInfo, Zeroconf
from zeroconf.asyncio import AsyncZeroconf


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
stop_requested = False


KERNEL_CACHE = {}

def compile_master_code(code_content=None):
    global current_mod, cuda_ctx
    if code_content is None:
        if os.path.exists("main.cu"):
            with open("main.cu", "r") as f: code_content = f.read()
        else: return False
    
    code_hash = hashlib.sha256(code_content.encode()).hexdigest()
    if code_hash in KERNEL_CACHE:
        current_mod = KERNEL_CACHE[code_hash]
        return True
        
    try:
        if cuda_ctx: cuda_ctx.push()
        dev = drv.Device(0)
        major, minor = dev.compute_capability()
        arch_flag = f"-arch=sm_{major}{minor}"
        
        compile_options = [
            arch_flag, 
            "-O3", 
            "--use_fast_math", 
            "-lineinfo", 
            "--restrict",
            "-allow-unsupported-compiler",
            "-Xcompiler", "/wd4819"
        ]
        inc_dirs = [os.getcwd(), os.path.join(os.getcwd(), "include"), os.path.join(os.getcwd(), "src")]
        
        new_mod = SourceModule(code_content, options=compile_options, no_extern_c=True, keep=False, include_dirs=inc_dirs)
        current_mod = new_mod
        KERNEL_CACHE[code_hash] = new_mod
        
        if cuda_ctx: cuda_ctx.pop()
        return True
    except Exception as e:
        console.print(f"[bold red]Compile Failed:[/bold red]")
        console.print(Panel(str(e), title="NVCC Error Output", border_style="red"))
        if cuda_ctx: cuda_ctx.pop()
        return False


try:
    nvmlInit()
    nv_handle = nvmlDeviceGetHandleByIndex(0)
    drv.init()
    dev = drv.Device(0)
    cuda_ctx = dev.make_context()
    try:
        compile_master_code()
        console.print(f"[bold green]GPU System Ready:[/bold green] {dev.name()}")
    finally:
        cuda_ctx.pop()
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

from transformers import TextIteratorStreamer
from threading import Thread

def process_gpu_request(req, addr, websocket_send_func, loop):
    global current_hf_model, stop_requested
    if not cuda_ctx: return
    
    try:
        start_time = time.time()
        cmd = req.get("cmd")
        

        if cmd in ["gpu_exec", "gpu_bench", "gpu_encrypt", "gpu_inject"]:
            if not current_mod: 
                asyncio.run_coroutine_threadsafe(websocket_send_func({"status": "error", "message": "GPU Module Not Loaded"}), loop)
                return
            cuda_ctx.push()
            try:
                res = {"status": "ok", "cmd": cmd}
                if cmd == "gpu_exec":
                    kernel_name = req.get("kernel")
                    data = req.get("data")
                    func = current_mod.get_function(kernel_name + "_kernel")
                    
                    if kernel_name == "render_3d":
                        w, h = 24, 24
                        dest = np.zeros(w * h).astype(np.float32)
                        func(drv.Out(dest), np.int32(w), np.int32(h), np.float32(time.time()), block=(16,16,1), grid=((w+15)//16, (h+15)//16))
                        res.update({"data": dest.tobytes(), "bin": True, "kernel": "render_3d", "width": w, "height": h})
                        
                    elif kernel_name == "hash_crack":
                        target, s, r = int(np.uint32(data[0])), int(np.int32(data[1])), int(np.int32(data[2]))
                        result = np.array([-1]).astype(np.int32)
                        func(drv.InOut(result), np.uint32(target), np.int32(s), np.int32(r), block=(256,1,1), grid=((r+255)//256, 1))
                        res.update({"data": int(result[0]), "kernel": "hash_crack"})
                    
                    elif kernel_name == "rsa_2048":
                        msg = np.array(data[0], dtype=np.uint32)
                        exp = np.array(data[1], dtype=np.uint32)
                        mod = np.array(data[2], dtype=np.uint32)
                        n_inv = np.uint32(data[3])
                        count = len(msg) // 64
                        result = np.zeros_like(msg)
                        func(drv.In(msg), drv.In(exp), drv.In(mod), drv.Out(result), n_inv, np.int32(count), block=(256,1,1), grid=((count*32 + 255)//256, 1))
                        res.update({"data": result.tobytes(), "bin": True, "kernel": "rsa_2048"})

                elif cmd == "gpu_bench":
                    mm_func = current_mod.get_function("matrix_mul_kernel")
                    N = 1024
                    A, B, C = np.random.randn(N,N).astype(np.float32), np.random.randn(N,N).astype(np.float32), np.zeros((N,N)).astype(np.float32)
                    s_evt, e_evt = drv.Event(), drv.Event()
                    s_evt.record(); mm_func(drv.In(A), drv.In(B), drv.Out(C), np.int32(N), block=(16,16,1), grid=((N+15)//16, (N+15)//16)); e_evt.record(); e_evt.synchronize()
                    gemm_ms = s_evt.time_till(e_evt)
                    
                    shm_func = current_mod.get_function("shared_mem_bench_kernel")
                    out = np.zeros(1).astype(np.float32)
                    s_evt.record(); shm_func(drv.Out(out), block=(1024,1,1), grid=(1,1)); e_evt.record(); e_evt.synchronize()
                    shm_ms = s_evt.time_till(e_evt)
                    
                    at_func = current_mod.get_function("atomic_bench_kernel")
                    cnt = np.zeros(1).astype(np.int32)
                    s_evt.record(); at_func(drv.InOut(cnt), np.int32(100000), block=(256,1,1), grid=(10,1)); e_evt.record(); e_evt.synchronize()
                    at_ms = s_evt.time_till(e_evt)
                    
                    nl_func = current_mod.get_function("null_kernel")
                    s_evt.record(); nl_func(block=(1,1,1), grid=(1,1)); e_evt.record(); e_evt.synchronize()
                    null_ms = s_evt.time_till(e_evt)
                    
                    res["data"] = {
                        "compute_gflops": round((2.0 * N**3) / (gemm_ms * 1e6), 2),
                        "bandwidth_gbs": round((3.0 * N**2 * 4) / (gemm_ms * 1e6), 3),
                        "shm_lat_ms": round(shm_ms, 4),
                        "atomic_ms": round(at_ms, 4),
                        "launch_lat_us": round(null_ms * 1000, 2)
                    }

                elif cmd == "gpu_encrypt":
                    text = req.get("text", "")
                    key = req.get("key", 0x5A)
                    data_bytes = np.frombuffer(text.encode(), dtype=np.uint8).copy()
                    func = current_mod.get_function("encrypt_kernel")
                    func(drv.InOut(data_bytes), np.int32(len(data_bytes)), np.uint8(key), block=(256,1,1), grid=((len(data_bytes)+255)//256, 1))
                    res["data"] = data_bytes.tolist()

                elif cmd == "gpu_inject":
                    code = req.get("code", "")
                    res["message"] = "Live Injection Successful" if compile_master_code(code) else "Injection Failed"

                if cmd != "gpu_exec" or req.get("kernel") != "render_3d":
                    res["telemetry"] = get_telemetry()
                res["compute_ms"] = round((time.time() - start_time) * 1000, 2)
                asyncio.run_coroutine_threadsafe(websocket_send_func(res), loop)
            finally:
                cuda_ctx.pop()

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
                tokenizer = AutoTokenizer.from_pretrained(model_id, local_files_only=lfo, token=hft)
                asyncio.run_coroutine_threadsafe(websocket_send_func({"status": "info", "message": "Step 2/3: Loading weights (this may take a while)..."}), loop)
                model = AutoModelForCausalLM.from_pretrained(model_id, device_map="auto", dtype=torch.float16 if device == "cuda:0" else torch.float32, local_files_only=lfo, token=hft)
                asyncio.run_coroutine_threadsafe(websocket_send_func({"status": "info", "message": "Step 3/3: Initializing inference pipeline..."}), loop)
                current_hf_model = {"pipeline": pipeline("text-generation", model=model, tokenizer=tokenizer, dtype=torch.float16 if device == "cuda:0" else torch.float32), "tokenizer": tokenizer, "model": model, "id": model_id}
                res = {"status": "ok", "message": f"Model {model_id} is now ONLINE.", "telemetry": get_telemetry()}
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
                if stop_requested: break
                
                clean_text = new_text.replace("<|user|>", "").replace("<|assistant|>", "").replace("<|system|>", "").replace("</s>", "")
                if first_chunk:
                    clean_text = clean_text.lstrip()
                    if clean_text: first_chunk = False
                
                if not clean_text: continue
                
                full_response += clean_text
                delta = {"status": "ok", "cmd": "ask_delta", "data": clean_text}
                asyncio.run_coroutine_threadsafe(websocket_send_func(delta), loop)
            
          
            end_pkt = {"status": "ok", "cmd": "ask_end", "full_data": full_response, "telemetry": get_telemetry()}
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
            status_msg = {"status": "ok", "message": f"HF Status: {'Authenticated' if token_set else 'No Token'}", "telemetry": get_telemetry()}
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
                asyncio.run_coroutine_threadsafe(websocket_send_func({"status": "ok", "message": "Unloaded"}), loop)
    except Exception as e:
        msg = str(e)
        if "403" in msg or "gated" in msg.lower():
            msg = "Gated Repo! Type 'hf token <your_token>' on ESP shell."
        asyncio.run_coroutine_threadsafe(websocket_send_func({"status": "error", "message": msg}), loop)

ACTIVE_CLIENTS = {}

async def handle_unikernel(websocket):
    ip = websocket.remote_address[0]
    addr = f"{ip}:{websocket.remote_address[1]}"
    
    if ip in ACTIVE_CLIENTS:
        try:
            old_ws = ACTIVE_CLIENTS[ip]
            if not old_ws.closed:
                console.print(f"[bold yellow][System][/bold yellow] Terminating stale connection for {ip}")
                await old_ws.close(1001, "New connection replacing old one")
        except: pass
    
    ACTIVE_CLIENTS[ip] = websocket
    console.print(f"[{time.strftime('%H:%M:%S')}] Connected: [cyan]{addr}[/cyan]")
    
    async def send_to_ws(msg):
        try:
            resp_bytes = msgpack.packb(msg)
            arr = np.frombuffer(resp_bytes, dtype=np.uint8)
            xor_bytes = (arr ^ 0x5A).tobytes()
            await websocket.send(xor_bytes)
        except:
            pass
        
    try:
       
        welcome_msg = {
            "status": "ok",
            "message": "UniKernel GPU Host Connected",
            "telemetry": get_telemetry()
        }
        if current_hf_model:
            welcome_msg["data"] = f"Current Model: {current_hf_model['id']} (Ready)"
            
        await send_to_ws(welcome_msg)

        async for message in websocket:
            try:
                if isinstance(message, bytes):
                  
                    arr = np.frombuffer(message, dtype=np.uint8)
                    message = (arr ^ 0x5A).tobytes()
                    req = msgpack.unpackb(message)
                else:
                    req = json.loads(message)
                
                cmd = req.get("cmd", "unknown")
                kernel = req.get("kernel", "")
                target = f" ({kernel})" if kernel else ""
                console.print(f"[dim][{time.strftime('%H:%M:%S')}] Request: [bold green]{cmd}[/bold green]{target} from {addr}[/dim]")

                loop = asyncio.get_running_loop()
                await asyncio.to_thread(process_gpu_request, req, addr, send_to_ws, loop)
            except Exception as e:
                console.print(f"[red]Request Error:[/red] {e}")
    except (websockets.exceptions.ConnectionClosed, OSError) as e:
        pass 
    except Exception as e:
        console.print(f"[bold red]System Error:[/bold red] {e}")
    finally:
        if ACTIVE_CLIENTS.get(ip) == websocket:
            del ACTIVE_CLIENTS[ip]
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
