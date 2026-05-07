# UniKernel Security Vulnerability Report

## Scope
Static analysis of `UniKernel.ino` with focus on defensive patching opportunities across:
1. Network Security
2. Memory Security
3. Kernel Security
4. Cryptography

---

## 1) Network Security Findings

### N-01: Plaintext remote administration over Telnet
- **Location:** Telnet daemon and authentication flow (`telnet on`, port 23).
- **Evidence:** `WiFiServer telnetServer(23)` and plaintext login/auth over telnet stream.
- **Impact:** Credentials and command traffic are trivially sniffable on the same network; active MITM command injection is feasible.
- **Attack concept:** Attacker with LAN access captures `login [password]` and reuses credentials to gain root shell.
- **Patch guidance:** Remove Telnet entirely or gate it behind a compile-time debug flag disabled in production. Prefer authenticated encrypted channel only.

### N-02: Weak/incorrect CSRF origin check using Host header prefixes
- **Location:** `/api/bt` host validation.
- **Evidence:** Request accepted if `Host` starts with broad private ranges (`192.168.`, `10.`, `172.` etc.).
- **Impact:** `Host` is attacker-controlled HTTP metadata; this can be bypassed and does not provide origin/authenticity protection.
- **Attack concept:** Cross-site request from malicious page or forged request with crafted `Host` can trigger privileged actions once password is known.
- **Patch guidance:** Remove Host-based trust logic. Require a random CSRF token or HMAC-bound nonce per session, plus strict authentication and rate-limits.

---

## 2) Memory Security Findings

### M-01: Stack-based buffer overflow in trigger command parser
- **Location:** `trigger` command creation path.
- **Evidence:** `strcpy(triggerTable[found].cond, cond);` into 16-byte buffer and `strcpy(triggerTable[found].action, act);` into 32-byte buffer with no length check.
- **Impact:** Overwrite adjacent state in RAM, leading to denial of service and potentially controlled behavior corruption.
- **Attack concept:** Authenticated attacker submits oversized `trigger` arguments, corrupting memory and influencing control flow/state.
- **Patch guidance:** Replace `strcpy` with bounded copies and explicit rejection of oversized input; validate token lengths before storing.

### M-02: Potential fixed-buffer copy risks from unchecked string operations
- **Location:** Multiple `strcpy`/`strcat`/`strncpy` patterns across command paths.
- **Impact:** In constrained embedded RAM layouts, one missed null-termination or oversized input can become latent memory corruption.
- **Patch guidance:** Introduce a central safe string helper (`copy_trunc(dst, dst_size, src)`), enforce return-code checks, and add fuzz tests for command parser.

---

## 3) Kernel Security Findings

### K-01: Authentication bypass via serial `passwd` special case
- **Location:** `executeCommandInternal()` early branch.
- **Evidence:** If input comes from serial and starts with `passwd`, code writes new password hash directly to EEPROM and returns, bypassing standard auth/setup checks.
- **Impact:** Anyone with serial/UART access can reset root password regardless of lockout/authentication state.
- **Attack concept:** Physical attacker or exposed serial bridge issues `passwd <new>` and obtains administrator control.
- **Patch guidance:** Remove bypass branch. If recovery is needed, require physical-presence challenge (button sequence + timed window) and explicit recovery mode.

### K-02: Trigger execution runs as privileged serial context
- **Location:** `processTriggers()` executes `executeCommand(buf, true);`.
- **Impact:** Trigger action commands execute as `fromSerial=true`, effectively privileged context escalation for stored automation actions.
- **Attack concept:** User plants trigger with privileged command payload; once condition fires, command runs with elevated trust semantics.
- **Patch guidance:** Execute trigger actions under least-privilege identity, not forced serial/admin context; re-check authorization at execution time.

---

## 4) Cryptography Findings

### C-01: Password hashing is unsalted in practice and truncated to 9 bytes
- **Location:** `hashPass()` and login verification path.
- **Evidence:** SHA-256 output truncated to first 9 bytes; stored `salt` is generated but never fed into hash function.
- **Impact:** Reduced brute-force cost and no per-device salt hardening; offline recovery risk significantly increased.
- **Patch guidance:** Use full modern password KDF (Argon2id/scrypt/PBKDF2-HMAC-SHA256 with per-device random salt + iteration cost). Store structured hash format.

### C-02: Reversible XOR obfuscation for OTA secret storage
- **Location:** OTA password storage (`KERNEL_KEY` XOR).
- **Evidence:** Password is XOR-obfuscated with static key and stored in EEPROM.
- **Impact:** Anyone with firmware/EEPROM read access can recover OTA password instantly.
- **Patch guidance:** Do not store reversible secrets. Store one-way verifier or derive per-device key material from hardware root/secure element where available.

---

## Risk Prioritization
- **Critical:** K-01, M-01
- **High:** C-01, N-01
- **Medium:** K-02, C-02, N-02
- **Low/Hardening:** M-02

## Immediate Patch Plan (short)
1. Remove serial `passwd` bypass and trigger privileged execution behavior.
2. Fix `trigger` parser overflows with strict bounds checks.
3. Disable Telnet in production firmware.
4. Replace credential scheme with salted KDF and migrate stored credentials.
5. Replace Host-prefix checks with real CSRF/session protections.
