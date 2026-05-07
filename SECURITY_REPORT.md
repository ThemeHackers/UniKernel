# UniKernel Security Vulnerability Report (Extended)

## Scope
Static analysis of `UniKernel.ino` with focus on defensive patching opportunities across:
1. Network Security
2. Memory Security
3. Kernel Security
4. Cryptography

This revision adds **more complex findings** and exploit-chain analysis.

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
- **Patch guidance:** Remove Host-based trust logic. Require random CSRF token + SameSite cookies/session binding (or HMAC challenge per request).

### N-03: No network-side brute-force throttling on Web API authentication
- **Location:** `/api/gpio`, `/api/stats`, `/api/bt` password checks.
- **Evidence:** Endpoints validate password hash directly but do not increment `loginFailCount`, apply cooldown, or lock source on repeated failures.
- **Impact:** Online brute-force is feasible from LAN/WLAN, especially because credential verifier is weak/truncated.
- **Attack concept:** Automated HTTP requests enumerate password guesses with no meaningful delay/ban logic.
- **Patch guidance:** Add per-IP and global rate limits, exponential backoff, temporary bans, and audit logs for API auth failures.

### N-04: Potential command-channel confusion due mixed admin surfaces
- **Location:** Serial, Telnet, SSH, Web API share same root credential semantics.
- **Impact:** A weaker channel (e.g., Telnet or physical UART) can undermine stronger channels and become an entry point to full system compromise.
- **Attack concept:** Adversary compromises lowest-assurance interface, then pivots to OTA/web controls.
- **Patch guidance:** Separate credentials/scopes per interface; enforce capability-based authorization (not single global root secret).

---

## 2) Memory Security Findings

### M-01: Stack-based buffer overflow in trigger command parser
- **Location:** `trigger` command creation path.
- **Evidence:** `strcpy(triggerTable[found].cond, cond);` into 16-byte buffer and `strcpy(triggerTable[found].action, act);` into 32-byte buffer with no length check.
- **Impact:** Overwrite adjacent state in RAM, leading to denial of service and potentially controlled behavior corruption.
- **Attack concept:** Authenticated attacker submits oversized `trigger` arguments, corrupting memory and influencing control flow/state.
- **Patch guidance:** Replace with bounded copies and reject oversized input before writing.

### M-02: Recursive alias expansion can exhaust stack (logic-to-memory DoS)
- **Location:** Alias resolver in `executeCommandInternal()` recursively calls itself.
- **Evidence:** Alias substitution performs `executeCommandInternal(resolved, fromSerial);` without recursion depth guard or cycle detection.
- **Impact:** Self-referential aliases (`alias a=a`) or cyclic pairs (`a->b`, `b->a`) can crash/reboot device via stack exhaustion.
- **Attack concept:** Malicious authenticated user stores recursive alias then triggers shell execution repeatedly.
- **Patch guidance:** Track recursion depth + visited aliases; hard-fail on cycles; cap alias expansion depth.

### M-03: Command parser tokenization enables oversized intermediate command paths
- **Location:** Multi-command splitting and script execution flows.
- **Impact:** Complex chained input (`;`, `&&`, variable expansion) increases chance of edge-case truncation/termination bugs and parser state corruption.
- **Attack concept:** Crafted command lines maximize expansion and redirection edge cases to induce unstable behavior.
- **Patch guidance:** Introduce parser state-machine tests and fuzzing corpus; enforce strict max lengths at each parse stage, not only at input ingress.

---

## 3) Kernel Security Findings

### K-01: Authentication bypass via serial `passwd` special case
- **Location:** `executeCommandInternal()` early branch.
- **Evidence:** If input comes from serial and starts with `passwd`, code writes new password hash directly to EEPROM and returns.
- **Impact:** Anyone with serial/UART access can reset root password regardless of lockout/authentication state.
- **Patch guidance:** Remove bypass; implement explicit physical recovery mode with auditable event.

### K-02: Trigger execution runs as privileged serial context
- **Location:** `processTriggers()` executes `executeCommand(buf, true);`.
- **Impact:** Trigger actions run in trusted serial context, creating privilege-escalation semantics for stored automation.
- **Patch guidance:** Bind trigger owner identity and execute with least privilege.

### K-03: Security model bypass via trust collapse between transport and identity
- **Location:** `fromSerial` boolean controls privilege behavior in multiple paths.
- **Impact:** Security decisions are coupled to transport origin, not cryptographic identity; bugs in origin assignment can become full auth bypasses.
- **Attack concept:** Any code path that unintentionally sets trusted origin (`fromSerial=true`) yields elevated authority.
- **Patch guidance:** Replace transport-derived trust with explicit authenticated principal object and centralized authorization checks.

---

## 4) Cryptography Findings

### C-01: Password hashing is unsalted in practice and truncated to 9 bytes
- **Location:** `hashPass()` and login verification path.
- **Evidence:** SHA-256 output truncated to first 9 bytes; salt is generated/stored but never mixed into hashing.
- **Impact:** Reduced brute-force cost and missing per-device salt hardening.
- **Patch guidance:** Migrate to PBKDF2-HMAC-SHA256 (or Argon2id where feasible) with per-device random salt and iteration/work factor.

### C-02: Reversible XOR obfuscation for OTA secret storage
- **Location:** OTA password storage (`KERNEL_KEY` XOR).
- **Impact:** EEPROM/firmware readers recover secret directly.
- **Patch guidance:** Store non-reversible verifier only; if secret persistence is required, use hardware-backed secure storage.

### C-03: Cross-platform crypto downgrade risk on non-ESP path
- **Location:** `hashPass()` fallback branch (`strncpy(output, input, 9)`).
- **Evidence:** For non-ESP builds, password "hash" is plain-text prefix copy.
- **Impact:** If compiled for alternate targets or test harnesses, authentication degrades catastrophically and may be shipped accidentally.
- **Patch guidance:** Remove insecure fallback entirely; fail build if strong hash primitive is unavailable.

---

## Advanced Exploit Chains (Complex)

### Chain A: Web brute-force -> privileged automation persistence
1. Brute-force Web API auth (N-03 + C-01).
2. Plant malicious trigger/alias payload.
3. Execute trigger under privileged serial context (K-02).
4. Persist behavior via boot script/EEPROM commands.

### Chain B: Memory corruption -> auth/control-plane destabilization
1. Submit oversized `trigger` parameters (M-01).
2. Corrupt adjacent runtime structures.
3. Cause watchdog resets, inconsistent auth state, or forced recovery workflows exploitable by attacker.

### Chain C: Low-assurance interface pivot
1. Capture credentials over Telnet (N-01) or reset via UART bypass (K-01).
2. Enable OTA/web controls and update runtime behavior.
3. Maintain long-term foothold by changing boot profile and operational settings.

---

## Updated Risk Prioritization
- **Critical:** K-01, M-01, C-01
- **High:** N-01, N-03, K-02, C-03
- **Medium:** N-02, K-03, C-02, M-02
- **Low/Hardening:** M-03, N-04

## Immediate Patch Plan (Re-prioritized)
1. Remove serial `passwd` bypass and forbid transport-based trust shortcuts.
2. Fix trigger parser overflow and add strict parser bounds validation.
3. Add API rate-limits/lockouts + auth failure telemetry.
4. Disable Telnet in production and split credentials by interface scope.
5. Replace credential storage with strong salted KDF; remove insecure fallback paths.
6. Add alias recursion/cycle protection and parser fuzz regression suite.
