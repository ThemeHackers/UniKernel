import subprocess
import argparse
import os
import time
from concurrent.futures import ThreadPoolExecutor

def run_command(cmd, desc):
    print(f"[Exec] {desc}...")
    try:
        result = subprocess.run(cmd, capture_output=True, text=True)
        if result.returncode == 0:
            return True, result.stdout
        else:
            return False, result.stderr
    except Exception as e:
        return False, str(e)

def run_upload(port_num):
    arduino_cli = ".\\arduino-cli.exe"
    fqbn = "esp8266:esp8266:nodemcuv2"
    sketch = "Unikernel.ino"
    com_port = f"COM{port_num}"
    
    cmd = [
        arduino_cli, "upload",
        "-p", com_port,
        "--fqbn", fqbn,
        sketch
    ]
    
    success, output = run_command(cmd, f"Uploading to {com_port}")
    if success:
        return f"[Success] {com_port}: Uploaded latest build."
    else:
        return f"[Failed] {com_port}: {output}"

def main():
    parser = argparse.ArgumentParser(description="UniKernel Smart Parallel Uploader")
    parser.add_argument("--cport", nargs="+", type=int, help="List of COM port numbers")
    parser.add_argument("--skip-compile", action="store_true", help="Skip compilation step")
    args = parser.parse_args()

    if not args.cport:
        print("Usage: python upload.py --cport 5 6")
        return

    arduino_cli = ".\\arduino-cli.exe"
    fqbn = "esp8266:esp8266:nodemcuv2"
    sketch = "Unikernel.ino"

    if not os.path.exists(arduino_cli):
        print("Error: arduino-cli.exe not found.")
        return


    if not args.skip_compile:
        start_time = time.time()
        print(">>> Stage 1: Compiling latest code...")
        compile_cmd = [arduino_cli, "compile", "--fqbn", fqbn, sketch]
        success, output = run_command(compile_cmd, "Building binary")
        
        if not success:
            print(f"\n[Compile Error]\n{output}")
            return
        print(f"Compilation finished in {round(time.time() - start_time, 2)}s\n")
    else:
        print(">>> Stage 1: Skipping compilation (using previous build)\n")


    print(f">>> Stage 2: Starting parallel upload to ports: {args.cport}")
    with ThreadPoolExecutor(max_workers=len(args.cport)) as executor:
        results = list(executor.map(run_upload, args.cport))

    print("\n--- Final Status Report ---")
    for res in results:
        print(res)

if __name__ == "__main__":
    main()
