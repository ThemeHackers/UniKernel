# KernelUNO Pro OS Edition 🚀

**KernelUNO** is a lightweight, ultra-efficient embedded operating system designed for Arduino UNO and ESP8266/ESP32 platforms. It provides a Unix-like shell environment, a virtual filesystem, and advanced networking capabilities within a 2KB RAM constraint.

## 🌟 Key Features
- **NetShell (Telnet)**: Remote access over WiFi (Port 23) - The embedded "SSH" alternative.
- **Pro Security**: Integrated Login system with encrypted password storage in EEPROM.
- **Advanced Networking**:
  - `wifi connect/scan/status/auto`: Manage connectivity.
  - `ping`: Check internet reachability.
  - `wget`: Download files directly from the web into the RAM filesystem.
  - `ifconfig`: Full network interface information.
- **Unix-like VFS**: Virtual Filesystem with `/home`, `/dev`, `/sys`, and `/bin`.
- **System Monitoring**: `ps` (Task manager), `free` (RAM monitor), `df` (Disk usage).
- **Automation**: `sh` command to execute batch scripts stored in memory.

## 🛠️ Getting Started
1. Upload the `KernelUNO.ino` to your ESP8266 or Arduino.
2. Open Serial Monitor (115200 baud).
3. Type `login admin` to unlock the system.
4. Use `wifi connect SSID PASS` to go online.
5. Use `ifconfig` to get your IP, then connect via PuTTY (Telnet port 23) for remote control.

## 📂 Command Reference
- **Files**: `ls`, `cd`, `pwd`, `mkdir`, `touch`, `cat`, `echo`, `rm`, `save`, `load`
- **Network**: `wifi`, `ping`, `wget`, `ifconfig`
- **Hardware**: `gpio`, `pwm`, `i2c scan`, `read`, `write`
- **System**: `uname`, `uptime`, `date`, `ps`, `dmesg`, `clear`, `reboot`

---
*Optimized for extreme memory efficiency and performance.*
