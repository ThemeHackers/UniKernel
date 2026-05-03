# UniKernel User Manual for ESP8266

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
| `telnet` | `telnet [on/off]` | Enable or disable remote access via Telnet. |
| `web` | `web [on/off]` | Enable or disable the Web Dashboard interface. |
| `ssh` | `ssh [on/off]` | Enable or disable the Encrypted Shell (Port 22). |
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
| `neofetch` | `neofetch` | Display system information banner. |
| `clear` | `clear` | Clear the terminal screen. |
| `dmesg` | `dmesg` | Display kernel log messages. |
| `df` | `df` | Show filesystem disk space usage. |
| `whoami` | `whoami` | Display current logged-in user. |
| `uname` | `uname` | Show system and kernel information. |


### 1.5 Security and Utilities
| Command | Usage | Description |
| :--- | :--- | :--- |
| `login` | `login [password]` | Authenticate as root user. |
| `logout` | `logout` | Terminate the current session. |
| `passwd` | `passwd [new_pass]` | Change the root password. |
| `firewall` | `firewall allow [IP]` | Restrict remote access to a specific IP address. |
| `ota` | `ota on` | Enable wireless firmware updates. |
| `ota` | `ota setpass [pass]` | Set a custom password for OTA updates. |
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

*   **Default OTA Password:** `admin123` (Change immediately via `ota setpass`).
*   **Authentication:** Sensitive commands require root privileges via `login`.
*   **Firewall:** Use `firewall allow [Your_PC_IP]` to block unauthorized users.
*   **Session Security:** Automatic logout occurs after 5 minutes of inactivity.
*   **Encrypted Shell:** Use `ssh on` for encrypted communication on Port 22.

## 3. Technical Specifications

*   **Platform:** ESP8266 (NodeMCU / Wemos D1 Mini)
*   **CPU Speed:** 160 MHz (Optimized Default)
*   **Communication:** Serial Baud 115200 / Telnet Port 23 / HTTP Port 80 / SSH Port 22
*   **File System:** Hybrid VFS (RAM-based) + LittleFS (Persistent Flash-based)

---
*UniKernel - Professional Kernel Environment for Microcontrollers*
