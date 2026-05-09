# UniKernel User Manual for ESP8266 , ESP32

UniKernel is a microcontroller-level kernel emulator designed for ESP8266 resource management. It features a Virtual File System (VFS), multitasking task management, and integrated security protocols.

![UniKernel Dashboard](image.png)


---

## 1. Command Reference

### 1.1 FileSystem Management
| Command | Usage | Description |
| :--- | :--- | :--- |
| `ls` | `ls [-l]` | List files and directories (-l for detailed view). |
| `cd` | `cd [directory]` | Change the current directory. |
| `pwd` | `pwd` | Print the path of the current working directory. |
| `mkdir` | `mkdir [name]` | Create a new directory. |
| `touch` | `touch [name]` | Create a new empty file. |
| `cat` | `cat [filename]` | Display the contents of a file. |
| `echo` | `echo [text] > [file]` | Write text to a file (Overwrites existing content). |
| `append` | `append [file] [text]` | Append text to the end of a file. |
| `cp` | `cp [source] [destination]` | Copy a file to a new location. |
| `mv` | `mv [source] [destination]` | Move or rename a file/directory. |
| `rm` | `rm [name]` | Delete a file or directory. |
| `info` | `info [name]` | Display file properties (Type and Size). |
| `save` | `save` | Persist the VFS state to EEPROM. |
| `load` | `load` | Restore the VFS state from EEPROM. |
| `lfs` | `lfs [ls/format/write]` | Manage the LittleFS persistent flash storage. |
| `alias` | `alias name=cmd` | Create custom command shortcuts (e.g., `alias d=neofetch`). |

### 1.2 System & Security
| Command | Usage | Description |
| :--- | :--- | :--- |
| `login` | `login [password]` | Authenticate to access protected commands. |
| `passwd` | `passwd [new_pass]` | Set/Change the system password. |
| `trigger`| `trigger [cond] [op] [val] [act]` | Intelligent automation (e.g., `trigger vcc < 3000 deepsleep`). |
| `deepsleep`| `deepsleep [sec]` | Enter low-power mode for X seconds. |
| `mqtt` | `mqtt [host] [msg]` | Simulate/Send data to an external IoT Broker. |
| `reboot` | `reboot` | Restart the system. |
| `chown` | `chown [root/guest] [file]` | Change the owner of a file. |
| `chmod` | `chmod [mode] [file]` | Change file permissions (Octal mode). |


### 1.2 Hardware Interface Control
| Command | Usage | Description |
| :--- | :--- | :--- |
| `on` / `off` | `on/off [pin]` | Set digital logic to HIGH or LOW. |
| `write` | `write [pin] [high/low]` | Write a digital output value. |
| `read` | `read [pin]` | Read the digital input value from a pin. |
| `pwm` | `pwm [pin] [0-255]` | Set a Pulse Width Modulation (PWM) signal. |
| `gpio` | `gpio [pin] [action]` | Control GPIO status (on, off, toggle). |
| `pinmode` | `pinmode [pin] [in/out]` | Set the operational mode of a pin (Input/Output). |
| `i2c` | `i2c scan` | Scan the I2C bus for connected device addresses. |

### 1.3 Networking and Communication
| Command | Usage | Description |
| :--- | :--- | :--- |
| `wifi` | `wifi connect [ssid] [pass]` | Configure and establish a WiFi connection. |
| `wifi` | `wifi [scan/status/off]` | Manage WiFi operational status. |
| `ifconfig` | `ifconfig` | Display network configuration (IP, Gateway, MAC). |
| `ping` | `ping [host]` | Test network connectivity to a remote host. |
| `wget` | `wget [url] [filename]` | Retrieve data from the internet via HTTP protocol. |
| `ntp` | `ntp` | Synchronize system time via Network Time Protocol. |
| `telnet` | `telnet [on/off]` | Enable/Disable remote access (Unsafe, Disabled by default). |
| `web` | `web [on/off]` | Enable/Disable the Web Dashboard (Hardened with Firewall). |
| `ssh` | `ssh [on/off]` | Enable/Disable Encrypted Shell (**EC P-256 Elliptic Curve**). |
| `bt` | `bt [on/off]` | Manage Bluetooth status (ESP32 only). |
| `netstat` | `netstat` | Display active network services and ports. |


### 1.4 System Monitor and Management
| Command | Usage | Description |
| :--- | :--- | :--- |
| `hwinfo` | `hwinfo` | Display low-level hardware parameters (Flash Mode, VCC, CPU). |
| `top` | `top` | Real-time monitor for memory and active tasks. |
| `ps` | `ps` | List currently running tasks and processes. |
| `uptime` | `uptime` | Show the total system elapsed time since boot. |
| `date` | `date` | Display the current system date and time. |
| `free` | `free` | Show available heap memory. |
| `cpu` | `cpu [80/160]` | Adjust the CPU clock frequency (MHz) at runtime. |
| `sleep` | `sleep [seconds]` | Enter Light Sleep mode for a specified duration. |
| `reboot` | `reboot` | Perform a system hardware restart. |
| `boot` | `boot [file/list/reset]` | Manage startup boot scripts and profiles. |
| `neofetch` | `neofetch` | Display system information banner. |
| `clear` | `clear` | Clear the terminal screen. |
| `dmesg` | `dmesg` | Display kernel log messages. |
| `df` | `df` | Show filesystem disk space usage. |
| `whoami` | `whoami` | Display current logged-in user. |
| `uname` | `uname` | Show system and kernel information. |


### 1.5 Security and Utilities
| Command | Usage | Description |
| :--- | :--- | :--- |
| `login` | `login [password]` | Authenticate as root (Uses **Salted SHA-256**). |
| `logout` | `logout` | Terminate the current session. |
| `passwd` | `passwd [new_pass]` | Change root password (128-bit entropy salted hash). |
| `firewall` | `firewall allow [IP]` | Whitelist an IP for Shell, Web, and API access. |
| `ota` | `ota on` | Enable wireless firmware updates. |
| `ota` | `ota setpass [pass]` | Set OTA password (**MD5 Hash Storage**). |
| `color` | `color [on/off]` | Enable or disable ANSI terminal colors. |
| `export` | `export key=val` | Set an environment variable. |
| `env` | `env` | List all active environment variables. |
| `sh` | `sh [script_file]` | Execute a batch of shell commands from a file. |
| `cron` | `cron [list/rm ID]` | Manage scheduled tasks. |
| `delay` | `delay [ms]` | Pause execution for specified milliseconds. |
| `kill` | `kill [pid]` | Terminate a background task by PID. |
| `bg` | `bg [blink]` | Start a task in the background. |


---

## 2. Security and Access Protocols

*   **Authentication:** Sensitive commands require root privileges. All passwords are secured with **Salted SHA-256 (16-byte salt)**.
*   **Brute-Force Protection:** Exponential backoff (Cooldown) triggered after failed logins. 5 consecutive fails trigger a **300s system lockout**.
*   **Encrypted Shell (SSH):** Advanced encryption using **Elliptic Curve P-256 (secp256r1)** for fast and secure remote management.
*   **Hardened Firewall:** Whitelist your IP using `firewall allow [IP]`. This protects Serial, Telnet, SSH, and the **Web API/Dashboard**.
*   **OTA Security:** Firmware updates are protected by **MD5 Hashed** credentials. Default: `admin`.
*   **Session Security:** Automatic logout occurs after 5 minutes of inactivity.

## 3. Technical Specifications

*   **Platform:** ESP8266 (NodeMCU / Wemos D1 Mini)
*   **CPU Speed:** 160 MHz (Optimized Default)
*   **Communication:** Serial Baud 115200 / Telnet Port 23 / HTTP Port 80 / SSH Port 22
*   **File System:** Hybrid VFS (RAM-based) + LittleFS (Persistent Flash-based)

---

## 4. Intelligent Automation

### 4.1 Smart Variables
You can use dynamic hardware data in any command by prefixing with `$`:
- `$VCC`: Current battery/input voltage in mV.
- `$TEMP`: Internal system temperature simulation.
- `$RAM`: Remaining free heap memory in bytes.

**Example:** `echo "Power Level: $VCC" > /dev/null`

### 4.2 Smart Triggers
UniKernel can monitor itself and react to environment changes:
- `trigger vcc < 3100 deepsleep` : Sleep if battery is low.
- `trigger ram < 2500 clear` : Free memory buffers if RAM is tight.

---

## 5. Hardware Interface & Recovery

### 5.1 Virtual Devices (`/dev/`)
Access hardware directly via the filesystem:
- `cat /dev/vcc` : Read current voltage.
- `cat /dev/temp` : Read current temperature.
- `cat /dev/led` : Get LED status (0/1).

### 5.2 Physical Reset Button (FLASH/GPIO 0)
Use the physical button on your board to recover access:
1. **Double Click (2x):** Manual Unlock (Resets `loginFailCount`).
2. **Triple Click (3x):** Factory Reset (Clears Password and Fail Count).

---

## 6. External Connectivity (IoT)

### 6.1 MQTT Bridge
To send data to an external server or Home Assistant:
`mqtt 192.168.1.50 "Temperature is $TEMP"`

### 6.2 Remote Control (Web Dashboard)
Access your dashboard at `http://[ESP_IP]`. The Pro Dashboard includes real-time RAM gauges and remote power management tools.

---

## 7. Lightweight UniAccel GPU Acceleration

UniAccel is a distributed computing engine that offloads heavy mathematical and security tasks from the microcontroller to a powerful GPU Host (via `UniAccelHost.py`).

### 7.1 UniAccel Command Reference
| Command | Usage | Description |
| :--- | :--- | :--- |
| `accel connect` | `accel connect [IP] [Port]` | Connect to a GPU Host (Default port: 81). |
| `accel discover` | `accel discover` | Auto-discover Host via **mDNS (Zeroconf)**. |
| `accel research` | `accel research crack [hash] [s] [r]` | Parallel Brute-force Hash Cracking (MurmurMix). |
| `accel research` | `accel research prime [s] [r]` | Massively parallel Prime Number search. |
| `accel research` | `accel research match [text] [pat]` | High-speed Pattern Scanning in constant memory. |
| `accel encrypt` | `accel encrypt [text] [key]` | Offload complex XOR/Rotate/Shift encryption to GPU. |
| `accel bench` | `accel bench` | Run a **5-stage GPU Performance Analysis**. |
| `accel status` | `accel status` | View connection state and real-time **GPU Telemetry**. |
| `accel disconnect`| `accel disconnect` | Close the link to the accelerator host. |

### 7.2 Advanced GPU Kernels
The UniAccel engine leverages CUDA kernels for extreme throughput:
- **`render_3d`**: Real-time Raymarching/SDF rendering (Visualized in ASCII).
- **`vision_filter`**: Grayscale and contrast enhancement for vision data.
- **`signal_proc`**: Complex magnitude calculation for DSP tasks.
- **`matrix_mul`**: Shared-memory optimized Tiled Matrix Multiplication.
- **`hash_crack`**: Brute-force search for target hash values.
- **`prime_search`**: Sieve of Eratosthenes variant for parallel prime detection.
- **`pattern_match`**: Constant-memory optimized byte-sequence searching.

### 7.3 Performance Benchmarking (`accel bench`)
The built-in benchmark measures:
1.  **Memory Bandwidth**: Host-to-Device and Device-to-Host transfer speeds (GB/s).
2.  **Compute Throughput**: Raw floating-point performance (GFLOPS).
3.  **Shared Memory Latency**: Access speed of on-chip low-latency memory.
4.  **Atomic Operations**: Throughput of synchronized global memory writes.
5.  **Launch Latency**: Driver overhead for triggering kernel execution.

### 7.4 Security & Transport
- **mDNS Discovery**: Automatically finds `_uniaccel._tcp.local` services.
- **MessagePack Serialization**: Compact binary data exchange.
- **XOR Obfuscation**: All packets are obfuscated with a dynamic XOR key to prevent sniffing.
- **GPU Telemetry**: Real-time monitoring of Temperature, Load, VRAM, and Power.

### 7.5 Setup & Requirements
1.  **Host**: Windows PC with an **NVIDIA GPU** (Compute Capability 3.0+).
2.  **Dependencies**: Install `PyCUDA`, `msgpack`, `zeroconf`, and `pynvml`.
3.  **Run**: Launch `UniAccelHost.py`. It will auto-detect MSVC and CUDA environments.
4.  **Connect**: On UniKernel, type `accel discover` to sync with the host.

---


