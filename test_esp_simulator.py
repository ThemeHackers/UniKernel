#!/usr/bin/env python3
import asyncio
import websockets
import msgpack
import json
import sys
import time
import numpy as np
from colorama import init, Fore, Back, Style
init(autoreset=True)
SUCCESS = Fore.GREEN + Style.BRIGHT
ERROR = Fore.RED + Style.BRIGHT
WARNING = Fore.YELLOW + Style.BRIGHT
INFO = Fore.CYAN + Style.BRIGHT
HEADER = Fore.MAGENTA + Style.BRIGHT
CLR_RST = "\033[0m"
CLR_RED = "\033[1;31m"
CLR_GRN = "\033[1;32m"
CLR_YLW = "\033[1;33m"
CLR_BLU = "\033[1;34m"
CLR_MAG = "\033[1;35m"
CLR_CYN = "\033[1;36m"
CLR_WHT = "\033[1;37m"
SESSION_KEY = bytes([0x5A, 0xA5, 0x12, 0x34, 0x56, 0x78, 0x90, 0xAB, 
                     0xCD, 0xEF, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66])
def secure_crypt(data):
    arr = np.frombuffer(data, dtype=np.uint8).copy()
    for i in range(len(arr)):
        arr[i] ^= SESSION_KEY[i % 16]
    return arr.tobytes()
COMMAND_HELP = """
Available ACCEL Commands:
  accel build              - Compile CUDA kernels
  accel status             - Get GPU telemetry
  accel bench              - Run GPU benchmark suite
  accel exec <kernel>      - Execute kernel (render_3d, hash_crack, rsa_2048)
  accel encrypt <text>     - Encrypt data using GPU
  accel physics            - Run N-body physics simulation
  accel signal             - Signal processing (DFT/FFT)
  accel cluster            - List cluster nodes
  accel help               - Show this help message
  accel exit               - Exit simulator
"""
class ESPShellEmulator:
    def __init__(self):
        self.device_name = "ESP8266_SIM"
        self.shell_depth = 0
        self.history = []
        self.connected = False
        self.websocket = None
    def print_banner(self):
        print(f"\n{CLR_CYN}╔════════════════════════════════════════╗{CLR_RST}")
        print(f"{CLR_CYN}║     UniKernel ESP8266/ESP32 Shell     ║{CLR_RST}")
        print(f"{CLR_CYN}║         Simulation Console             ║{CLR_RST}")
        print(f"{CLR_CYN}╚════════════════════════════════════════╝{CLR_RST}")
        print(f"{CLR_GRN}[BOOT] Device initialized: {self.device_name}{CLR_RST}")
        print(f"{CLR_GRN}[BOOT] Heap available: 81920 bytes{CLR_RST}")
        print(f"{CLR_GRN}[BOOT] WiFi Status: CONNECTED{CLR_RST}\n")
    def print_prompt(self):
        path = "/" if self.shell_depth == 0 else f"/shell[{self.shell_depth}]"
        print(f"{CLR_BLU}{self.device_name}:{path}> {CLR_RST}", end="", flush=True)
    def execute_command(self, cmd):
        print(f"{CLR_WHT}{cmd}{CLR_RST}")  
        self.history.append(cmd)
    def print_output(self, text, color=CLR_RST):
        print(f"{color}{text}{CLR_RST}")
    def print_error(self, text):
        print(f"{CLR_RED}Error: {text}{CLR_RST}")
    def print_success(self, text):
        print(f"{CLR_GRN}✓ {text}{CLR_RST}")
    def print_box(self, title, data_dict):
        print(f"\n{CLR_CYN}╔═══════════════════════════════════════════════╗{CLR_RST}")
        print(f"{CLR_CYN}║{CLR_RST} {title:<43} {CLR_CYN}║{CLR_RST}")
        print(f"{CLR_CYN}╠═══════════════════════════════════════════════╣{CLR_RST}")
        for key, val in data_dict.items():
            val_str = str(val)
            print(f"{CLR_CYN}║{CLR_RST} {key:<40} {val_str:>8} {CLR_CYN}│{CLR_RST}")
        print(f"{CLR_CYN}╚═══════════════════════════════════════════════╝{CLR_RST}\n")
async def execute_accel_command(shell, websocket, cmd_parts):
    if not cmd_parts:
        return
    cmd_type = cmd_parts[0]
    if cmd_type == "build":
        print(f"{CLR_CYN}[ACCEL] Sending build request...{CLR_RST}\n")
        build_cmd = {"cmd": "build_cu", "verbose": True}
        payload = msgpack.packb(build_cmd)
        payload = secure_crypt(payload)
        await websocket.send(payload)
        timeout_counter = 0
        first_response = True
        while timeout_counter < 30:
            try:
                response = await asyncio.wait_for(websocket.recv(), timeout=1.0)
                timeout_counter = 0
                response = secure_crypt(response)
                try:
                    data = msgpack.unpackb(response)
                except:
                    data = json.loads(response)
                if isinstance(data, dict) and "build_status" in data:
                    bs = data["build_status"]
                    progress = bs.get("progress", 0)
                    phase = bs.get("phase", "unknown")
                    message = bs.get("message", "")
                    if first_response:
                        print(f"{CLR_YLW}╔═══════════════════════════════════════════════╗{CLR_RST}")
                        print(f"{CLR_YLW}║         CUDA Build Progress                   ║{CLR_RST}")
                        print(f"{CLR_YLW}╚═══════════════════════════════════════════════╝{CLR_RST}\n")
                        first_response = False
                    bar_len = 45
                    filled = int(bar_len * progress / 100)
                    bar = "█" * filled + "░" * (bar_len - filled)
                    print(f"[{phase:<8}] [{bar}] {progress:3.0f}%")
                    if message:
                        print(f"  → {message}")
                    if progress >= 100:
                        print()
                        shell.print_success("Build completed!")
                        print()
                        break
            except asyncio.TimeoutError:
                timeout_counter += 1
    elif cmd_type == "status":
        print(f"{CLR_CYN}[ACCEL] Requesting GPU status...{CLR_RST}\n")
        gpu_cmd = {"cmd": "gpu_list"}
        payload = msgpack.packb(gpu_cmd)
        payload = secure_crypt(payload)
        await websocket.send(payload)
        for _ in range(10):
            try:
                response = await asyncio.wait_for(websocket.recv(), timeout=1.0)
                response = secure_crypt(response)
                try:
                    data = msgpack.unpackb(response)
                except:
                    data = json.loads(response)
                if isinstance(data, dict) and "telemetry" in data:
                    tel = data["telemetry"]
                    temp = tel.get('temp', 0)
                    util = tel.get('util', 0)
                    mem = tel.get('mem', 0)
                    pwr = tel.get('pwr', 0)
                    clk = tel.get('clk', 0)
                    print(f"{CLR_CYN}╔═══════════════════════════════════════════════╗{CLR_RST}")
                    print(f"{CLR_CYN}║       GPU TELEMETRY STATUS                    ║{CLR_RST}")
                    print(f"{CLR_CYN}╠═══════════════════════════════════════════════╣{CLR_RST}")
                    temp_color = CLR_RED if temp > 70 else CLR_YLW if temp > 50 else CLR_GRN
                    util_color = CLR_RED if util > 90 else CLR_YLW if util > 50 else CLR_GRN
                    mem_color = CLR_RED if mem > 1500 else CLR_YLW if mem > 800 else CLR_GRN
                    print(f"{CLR_CYN}║{CLR_RST} {temp_color}Temperature    :{CLR_RST} {temp:>6}°C        {CLR_CYN}│{CLR_RST}")
                    print(f"{CLR_CYN}║{CLR_RST} {util_color}Utilization    :{CLR_RST} {util:>6}%        {CLR_CYN}│{CLR_RST}")
                    print(f"{CLR_CYN}║{CLR_RST} {mem_color}VRAM Usage     :{CLR_RST} {mem:>6} MB      {CLR_CYN}│{CLR_RST}")
                    print(f"{CLR_CYN}║{CLR_RST} {CLR_YLW}Power Draw     :{CLR_RST} {pwr:>6.1f}W       {CLR_CYN}│{CLR_RST}")
                    print(f"{CLR_CYN}║{CLR_RST} {CLR_MAG}Clock Speed    :{CLR_RST} {clk:>6} MHz     {CLR_CYN}│{CLR_RST}")
                    print(f"{CLR_CYN}╚═══════════════════════════════════════════════╝{CLR_RST}\n")
                    break
            except asyncio.TimeoutError:
                continue
    elif cmd_type == "bench":
        print(f"{CLR_CYN}[ACCEL] Running GPU benchmark...{CLR_RST}\n")
        bench_cmd = {"cmd": "gpu_bench"}
        payload = msgpack.packb(bench_cmd)
        payload = secure_crypt(payload)
        await websocket.send(payload)
        for _ in range(10):
            try:
                response = await asyncio.wait_for(websocket.recv(), timeout=3.0)
                response = secure_crypt(response)
                try:
                    data = msgpack.unpackb(response)
                except:
                    data = json.loads(response)
                if isinstance(data, dict) and data.get("cmd") == "gpu_bench":
                    bench_data = data.get("data", {})
                    print(f"{CLR_GRN}╔═══════════════════════════════════════════════╗{CLR_RST}")
                    print(f"{CLR_GRN}║       GPU BENCHMARK RESULTS                   ║{CLR_RST}")
                    print(f"{CLR_GRN}╠═══════════════════════════════════════════════╣{CLR_RST}")
                    for key, val in bench_data.items():
                        key_display = key.replace('_', ' ').title()
                        print(f"{CLR_GRN}║{CLR_RST} {key_display:<40} {str(val):>8} {CLR_GRN}│{CLR_RST}")
                    print(f"{CLR_GRN}╚═══════════════════════════════════════════════╝{CLR_RST}\n")
                    break
            except asyncio.TimeoutError:
                continue
    elif cmd_type == "exec":
        kernel_name = cmd_parts[1] if len(cmd_parts) > 1 else "render_3d"
        print(f"{CLR_CYN}[ACCEL] Executing kernel: {kernel_name}...{CLR_RST}\n")
        if kernel_name == "render_3d":
            exec_cmd = {"cmd": "gpu_exec", "kernel": "render_3d"}
        elif kernel_name == "hash_crack":
            exec_cmd = {"cmd": "gpu_exec", "kernel": "hash_crack", "data": [123456, 0, 1000000]}
        elif kernel_name == "rsa_2048":
            exec_cmd = {"cmd": "gpu_exec", "kernel": "rsa_2048", "data": [[1,2,3], [4,5,6], [7,8,9], 10]}
        else:
            exec_cmd = {"cmd": "gpu_exec", "kernel": kernel_name}
        payload = msgpack.packb(exec_cmd)
        payload = secure_crypt(payload)
        await websocket.send(payload)
        for _ in range(10):
            try:
                response = await asyncio.wait_for(websocket.recv(), timeout=3.0)
                response = secure_crypt(response)
                try:
                    data = msgpack.unpackb(response)
                except:
                    data = json.loads(response)
                if isinstance(data, dict) and data.get("kernel"):
                    kernel = data.get("kernel", "unknown")
                    result_len = len(str(data.get("data", "")))
                    compute_ms = data.get("compute_ms", 0)
                    print(f"{CLR_GRN}╔═══════════════════════════════════════════════╗{CLR_RST}")
                    print(f"{CLR_GRN}║       KERNEL EXECUTION RESULT                 ║{CLR_RST}")
                    print(f"{CLR_GRN}╠═══════════════════════════════════════════════╣{CLR_RST}")
                    print(f"{CLR_GRN}║{CLR_RST} Kernel Name{CLR_RST}{' ':<26} {kernel:<12} {CLR_GRN}│{CLR_RST}")
                    print(f"{CLR_GRN}║{CLR_RST} Result Size{CLR_RST}{' ':<26} {result_len} bytes   {CLR_GRN}│{CLR_RST}")
                    print(f"{CLR_GRN}║{CLR_RST} Compute Time{CLR_RST}{' ':<25} {compute_ms:.2f} ms   {CLR_GRN}│{CLR_RST}")
                    print(f"{CLR_GRN}╚═══════════════════════════════════════════════╝{CLR_RST}\n")
                    shell.print_success(f"Kernel {kernel} completed!")
                    print()
                    break
            except asyncio.TimeoutError:
                continue
    elif cmd_type == "physics":
        print(f"{CLR_CYN}[ACCEL] Running N-body physics simulation...{CLR_RST}\n")
        phys_cmd = {"cmd": "gpu_physics"}
        payload = msgpack.packb(phys_cmd)
        payload = secure_crypt(payload)
        await websocket.send(payload)
        for _ in range(10):
            try:
                response = await asyncio.wait_for(websocket.recv(), timeout=3.0)
                response = secure_crypt(response)
                try:
                    data = msgpack.unpackb(response)
                except:
                    data = json.loads(response)
                if isinstance(data, dict) and data.get("kernel") == "render_3d":
                    w = data.get("width", 24)
                    h = data.get("height", 24)
                    compute_ms = data.get("compute_ms", 0)
                    print(f"{CLR_MAG}╔═══════════════════════════════════════════════╗{CLR_RST}")
                    print(f"{CLR_MAG}║       N-BODY PHYSICS SIMULATION               ║{CLR_RST}")
                    print(f"{CLR_MAG}╠═══════════════════════════════════════════════╣{CLR_RST}")
                    print(f"{CLR_MAG}║{CLR_RST} Resolution{CLR_RST}{' ':<28} {w}x{h}           {CLR_MAG}│{CLR_RST}")
                    print(f"{CLR_MAG}║{CLR_RST} Frame Render Time{CLR_RST}{' ':<21} {compute_ms:.2f} ms   {CLR_MAG}│{CLR_RST}")
                    print(f"{CLR_MAG}║{CLR_RST} Particles{CLR_RST}{' ':<32} 256          {CLR_MAG}│{CLR_RST}")
                    print(f"{CLR_MAG}╚═══════════════════════════════════════════════╝{CLR_RST}\n")
                    shell.print_success("Physics simulation step completed!")
                    print()
                    break
            except asyncio.TimeoutError:
                continue
    elif cmd_type == "signal":
        print(f"{CLR_CYN}[ACCEL] Running signal processing (DFT)...{CLR_RST}\n")
        signal_data = np.random.randn(256).tolist()
        signal_cmd = {"cmd": "gpu_signal", "data": signal_data}
        payload = msgpack.packb(signal_cmd)
        payload = secure_crypt(payload)
        await websocket.send(payload)
        for _ in range(10):
            try:
                response = await asyncio.wait_for(websocket.recv(), timeout=3.0)
                response = secure_crypt(response)
                try:
                    data = msgpack.unpackb(response)
                except:
                    data = json.loads(response)
                if isinstance(data, dict) and data.get("kernel") == "signal_fft":
                    result_data = data.get("data", [])
                    compute_ms = data.get("compute_ms", 0)
                    print(f"{CLR_YLW}╔═══════════════════════════════════════════════╗{CLR_RST}")
                    print(f"{CLR_YLW}║       SIGNAL PROCESSING (DFT) RESULT          ║{CLR_RST}")
                    print(f"{CLR_YLW}╠═══════════════════════════════════════════════╣{CLR_RST}")
                    print(f"{CLR_YLW}║{CLR_RST} Input Samples{CLR_RST}{' ':<26} 256          {CLR_YLW}│{CLR_RST}")
                    print(f"{CLR_YLW}║{CLR_RST} Output Points{CLR_RST}{' ':<26} {len(result_data):<10} {CLR_YLW}│{CLR_RST}")
                    print(f"{CLR_YLW}║{CLR_RST} Compute Time{CLR_RST}{' ':<25} {compute_ms:.2f} ms   {CLR_YLW}│{CLR_RST}")
                    print(f"{CLR_YLW}╚═══════════════════════════════════════════════╝{CLR_RST}\n")
                    shell.print_success("Signal processing completed!")
                    print()
                    break
            except asyncio.TimeoutError:
                continue
    elif cmd_type == "encrypt":
        text = " ".join(cmd_parts[1:]) if len(cmd_parts) > 1 else "Hello CUDA World"
        print(f"{CLR_CYN}[ACCEL] Encrypting: '{text}'...{CLR_RST}\n")
        enc_cmd = {"cmd": "gpu_encrypt", "text": text, "key": 0x5A}
        payload = msgpack.packb(enc_cmd)
        payload = secure_crypt(payload)
        await websocket.send(payload)
        for _ in range(10):
            try:
                response = await asyncio.wait_for(websocket.recv(), timeout=2.0)
                response = secure_crypt(response)
                try:
                    data = msgpack.unpackb(response)
                except:
                    data = json.loads(response)
                if isinstance(data, dict) and data.get("cmd") == "gpu_encrypt":
                    encrypted = data.get("data", [])
                    compute_ms = data.get("compute_ms", 0)
                    print(f"{CLR_GRN}╔═══════════════════════════════════════════════╗{CLR_RST}")
                    print(f"{CLR_GRN}║       GPU ENCRYPTION RESULT                   ║{CLR_RST}")
                    print(f"{CLR_GRN}╠═══════════════════════════════════════════════╣{CLR_RST}")
                    print(f"{CLR_GRN}║{CLR_RST} Original Text{CLR_RST}{' ':<25} {text:<12}  {CLR_GRN}│{CLR_RST}")
                    print(f"{CLR_GRN}║{CLR_RST} Encrypted Bytes{CLR_RST}{' ':<23} {len(encrypted):<9} {CLR_GRN}│{CLR_RST}")
                    print(f"{CLR_GRN}║{CLR_RST} Compute Time{CLR_RST}{' ':<25} {compute_ms:.2f} ms   {CLR_GRN}│{CLR_RST}")
                    print(f"{CLR_GRN}╚═══════════════════════════════════════════════╝{CLR_RST}\n")
                    shell.print_success("Encryption completed!")
                    print()
                    break
            except asyncio.TimeoutError:
                continue
    elif cmd_type == "cluster":
        print(f"{CLR_CYN}[ACCEL] Listing cluster nodes...{CLR_RST}\n")
        cluster_cmd = {"cmd": "gpu_cluster_list"}
        payload = msgpack.packb(cluster_cmd)
        payload = secure_crypt(payload)
        await websocket.send(payload)
        for _ in range(10):
            try:
                response = await asyncio.wait_for(websocket.recv(), timeout=2.0)
                response = secure_crypt(response)
                try:
                    data = msgpack.unpackb(response)
                except:
                    data = json.loads(response)
                if isinstance(data, dict) and "nodes" in data:
                    nodes = data.get("nodes", [])
                    print(f"{CLR_MAG}╔═══════════════════════════════════════════════╗{CLR_RST}")
                    print(f"{CLR_MAG}║       CLUSTER NODE STATUS                     ║{CLR_RST}")
                    print(f"{CLR_MAG}╠═══════════════════════════════════════════════╣{CLR_RST}")
                    print(f"{CLR_MAG}║ Total Nodes: {len(nodes):<37}{CLR_MAG}│{CLR_RST}")
                    if nodes:
                        print(f"{CLR_MAG}╠═══════════════════════════════════════════════╣{CLR_RST}")
                        for node in nodes:
                            node_ip = node.get("ip", "unknown")
                            node_req = node.get("req", 0)
                            print(f"{CLR_MAG}║{CLR_RST} {node_ip:<30} Requests: {node_req:<6} {CLR_MAG}│{CLR_RST}")
                    print(f"{CLR_MAG}╚═══════════════════════════════════════════════╝{CLR_RST}\n")
                    shell.print_success("Cluster info retrieved!")
                    print()
                    break
            except asyncio.TimeoutError:
                continue
    elif cmd_type == "help":
        print(COMMAND_HELP)
    else:
        shell.print_error(f"Unknown command: {cmd_type}")
        print(f"Type '{CLR_CYN}help{CLR_RST}' for available commands\n")
async def test_esp_simulator(host="localhost", port=81):
    shell = ESPShellEmulator()
    shell.print_banner()
    uri = f"ws://{host}:{port}/"
    shell.print_prompt()
    shell.execute_command(f"accel connect {host}:{port}")
    try:
        async with websockets.connect(uri) as websocket:
            shell.print_success(f"Connected to {uri}")
            print()
            print(f"{CLR_YLW}Type 'help' for available commands or 'exit' to quit{CLR_RST}\n")
            test_commands = [
                "build",
                "status",
                "bench",
                "exec render_3d",
                "physics",
                "signal",
                "encrypt Hello CUDA",
                "cluster"
            ]
            for test_cmd in test_commands:
                shell.print_prompt()
                shell.execute_command(f"accel {test_cmd}")
                await execute_accel_command(shell, websocket, test_cmd.split())
                await asyncio.sleep(0.5)
            shell.print_success("All tests completed!")
            print()
    except ConnectionRefusedError:
        shell.print_error(f"Connection refused - Is UniAccelHost.py running on {host}:{port}?")
        print(f"{INFO}Start it with: python UniAccelHost.py{CLR_RST}")
        sys.exit(1)
    except Exception as e:
        shell.print_error(str(e))
        import traceback
        traceback.print_exc()
        sys.exit(1)
if __name__ == "__main__":
    host = sys.argv[1] if len(sys.argv) > 1 else "localhost"
    port = int(sys.argv[2]) if len(sys.argv) > 2 else 81
    asyncio.run(test_esp_simulator(host, port))
