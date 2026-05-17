# UniKernel Project Context

## Project Overview
UniKernel is a microcontroller-level kernel emulator designed for ESP8266 and ESP32 devices. It provides a Virtual File System (VFS), multitasking task management, security protocols (firewall, PBKDF2-style authentication), and a command-line shell interface. 

A major component of the project is **UniAccel**, a distributed computing engine that offloads heavy mathematical, cryptographic, physics, and AI (Hugging Face) workloads from the resource-constrained microcontroller to a powerful Python-based GPU Host (`UniAccelHost.py`) using WebSockets and PyCUDA.

## Main Technologies
- **Microcontroller Firmware:** C++ (Arduino framework for ESP8266/ESP32), LittleFS for persistent storage, ArduinoOTA.
- **GPU Acceleration Host:** Python 3, `asyncio`, `websockets`, `msgpack`.
- **GPU Compute:** CUDA (via PyCUDA), JIT compilation of `.cu` kernels.
- **AI/LLM:** PyTorch, Hugging Face `transformers` (Text generation pipelines).

## Architecture
1. **Node (ESP8266/ESP32):** Runs the core `Unikernel.ino` firmware. Handles the local shell, basic hardware control, VFS, and initiates connections to the GPU Host.
2. **Host (PC):** Runs `UniAccelHost.py`. It acts as a WebSocket server, compiles CUDA kernels dynamically, manages a cluster of connected ESP nodes, and executes AI inference using local GPU resources.
3. **Communication:** Nodes and Host communicate using XOR-obfuscated MessagePack over WebSockets. Service discovery is facilitated by mDNS (Zeroconf).

## Building and Running
### Microcontroller Firmware
- Use the Arduino IDE, PlatformIO, or `arduino-cli` (included in `tools/`) to compile and upload `Unikernel.ino`.
- **Dependencies:** `ArduinoJson`, `ArduinoOTA`, `WebSocketsClient`, etc.

### GPU Host
- Ensure an NVIDIA GPU and CUDA toolkit/MSVC are available on the host machine.
- Install Python dependencies:
  ```bash
  pip install -r requirements.txt
  ```
- Run the server:
  ```bash
  python UniAccelHost.py
  ```
- Access the Web Dashboard on the host at `http://localhost:8080`.

## Development Conventions
- **Memory Optimization:** The C++ code extensively uses `ICACHE_FLASH_ATTR`, `PROGMEM`, and `F()` macros to conserve RAM on the ESP8266. Keep memory limits in mind when adding features.
- **File Structure:**
  - `src/` and `include/`: Core C++ implementation files for the ESP firmware (shell, commands, vfs, auth).
  - `UniAccel/`: Contains the CUDA kernels (`.cu` and `.cuh`) used by the host.
- **Python Style:** The host uses `asyncio` for non-blocking network operations while executing heavy CUDA/AI tasks in separate threads (`asyncio.to_thread` or standard threading) to prevent blocking the event loop. UI output in the console uses the `rich` library.

## Testing
- Run the build test script to verify the project structure and CUDA compilation:
  ```bash
  python test_build.py
  ```

### CUDA Prerequisites
- Ensure NVIDIA GPU and CUDA toolkit/MSVC are available on the host machine.
- Verify CUDA installation:
  ```bash
  nvcc --version
  ```
- Check GPU compatibility:
  ```bash
  nvidia-smi
  ```
- If you start developing CUDA Code, please read this: "./agent-skills/SKILL.md"

### Clean Code
- It's not always necessary to include comments when writing code.
- If you find any comments in the code, please delete them.
- And most importantly, when writing code in any programming language, never use comments.
- If the user requests code cleanup or anything related to code clearing, clean it only in the current project or as specified, and do not clean it in packages or other add-ons.

### When you create files to update any code
- When you create files to update any code, this doesn't necessarily require modifying the original files you're editing. If you accidentally do, just proceed with the changes and delete the files after the update.