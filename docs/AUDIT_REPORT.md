# UniKernel: Development Ideas + Security/Bug Audit (2026-05-11)

## Scope
- Reviewed: `Unikernel.ino`, `UniAccelHost.py`, `upload.py`, `README.md`.
- Focus: architecture, core code quality, security vulnerabilities, and likely bugs.

## 1) Development Ideas

### 1.1 CUDA programming layer (GPU kernels/runtime/offload pipeline)
1. **Unify command parsing into a table-driven dispatcher**
   - Current command handling is long and branching-heavy in `Unikernel.ino`.
   - Introduce metadata table: command name, auth level, arg schema, handler pointer.
   - Benefits: easier command lifecycle management (add/edit/remove), less regression when adding GPU features.

2. **Split kernel entry wrappers by workload class**
   - Separate wrappers for `render`, `crypto`, `bench`, `physics`, `signal`, and `rsa` paths.
   - Enforce typed payload validation before launching kernels (shape, dtype, max size).
   - Benefits: lower runtime mismatch errors and safer host-to-device execution.

3. **Introduce structured response format**
   - Today, outputs are mostly plain text and ad-hoc JSON fragments.
   - Add consistent response object for shell/websocket/web API: `{ok, code, message, data}`.
   - Benefits: easier host integration, lower parser fragility.

4. **Versioned config & migration**
   - EEPROM/LittleFS fields should include `schema_version` and migration logic.
   - Avoid silent breakage when adding new settings (password format, trigger schema, boot script metadata).

5. **Input validation centralization**
   - Use shared helpers for pin ranges, filename constraints, URL validation, safe numeric parsing.
   - Currently validation is scattered and can drift.

6. **Command history + audit log persistence policy**
   - Add log rotation and retention caps (size/time based).
   - Store security-sensitive event counters separately from user logs.

### 1.2 Core-code direction (kernel/runtime/system internals)
1. **Split monolithic `Unikernel.ino` into modules**
   - Suggested units: `auth`, `vfs`, `net`, `web`, `scheduler`, `io`, `security`.
   - Add a small platform abstraction for ESP8266/ESP32 differences.

2. **Replace dynamic `String` hot paths with bounded buffers**
   - Especially in high-frequency request handlers and prompt/log code.
   - Reduces heap fragmentation and long-run instability.

3. **Formal task scheduler contract**
   - Define max execution time per task and jitter reporting.
   - Add watchdog-aware task budget (soft/hard thresholds).

4. **Typed protocol between MCU and GPU host**
   - Existing host accepts many dynamic request shapes.
   - Introduce a strict schema (MsgPack or CBOR with required keys and types).

5. **Feature flags + build profiles**
   - Profiles: `safe`, `dev`, `performance`.
   - Disable risky features by default in production profile (telnet, code injection, debug endpoints).

## 2) Security Findings (High to Low)

### High Risk
1. **Remote code injection capability on GPU host (`gpu_inject`)**
   - `UniAccelHost.py` accepts request command `gpu_inject` and compiles supplied CUDA code.
   - If exposed without robust auth + ACL, this is effectively remote code execution on the host GPU process.
   - Recommendation: disable by default, require signed payloads or local-only admin channel.

2. **Default OTA password fallback (`admin`)**
   - If OTA hash is absent, firmware sets OTA password to `admin`.
   - This is a dangerous default for network-accessible devices.
   - Recommendation: force first-boot password setup; refuse OTA start until strong password exists.

3. **Web auth token passed via query string (`/api/stats?pass=`)**
   - Password in URL can leak through logs, proxies, browser history.
   - Recommendation: use POST body or Authorization header; rotate nonce/session token.

4. **Telnet support remains available**
   - README and code confirm telnet mode and runtime enablement.
   - Even if off by default, accidental enablement provides cleartext credential exposure.
   - Recommendation: remove telnet in release builds or gate behind compile-time flag.

### Medium Risk
5. **Weak password salt length (`PASS_SALT_LEN = 4`)**
   - 4-byte salt is too short by modern standards.
   - Recommendation: at least 16 bytes random salt and stronger KDF (PBKDF2/Argon2 if feasible, or iterative SHA-256 with high rounds on MCU constraints).

6. **Global mutable buffer in obfuscation macro (`global_obf_buf`)**
   - Shared buffer can cause unexpected overwrites/reentrancy issues and accidental data leakage between calls.
   - Recommendation: replace with compile-time constants or per-call local buffer.

7. **Broad exception swallowing in Python host**
   - Multiple `except: pass` blocks hide failures and can create insecure fallback behavior.
   - Recommendation: log explicit exception classes and fail closed on security-sensitive setup.

### Low Risk / Hardening
8. **Suppressed warnings globally in host**
   - Warning suppression may hide deprecation/security signals.
   - Recommendation: suppress selectively by module/message only.

## 3) Bug & Reliability Findings

1. **Potential path inconsistency (`/sys/` vs `/sys`) in VFS lookups**
   - `logError` checks parent path `/sys/` while initialization stores `/sys`.
   - This can break error log lookup silently.

2. **`String` heavy operations in constrained RAM environment**
   - Repeated `String` concatenation in handlers can fragment heap over time.

3. **Shift-based cooldown growth (`1000UL << loginFailCount`)**
   - Bounded later, but pattern is fragile and can overflow if constants change.
   - Prefer saturating arithmetic with explicit upper bound.

4. **Cross-platform upload script assumptions**
   - `upload.py` hardcodes Windows-only paths (`.\\tools\\arduino-cli.exe`, `COMx`, `wmic`).
   - Fails on Linux/macOS CI and reduces maintainability.

5. **Single-file architecture hampers testing**
   - Hard to isolate and unit test command handlers, auth, filesystem logic.

## 4) Prioritized Remediation Plan

### Phase 0 (Immediate)
- Disable `gpu_inject` by default.
- Remove OTA `admin` fallback and enforce setup flow.
- Remove credential-in-query usage from web dashboard API.
- Keep telnet permanently off in production builds.

### Phase 1 (1-2 sprints)
- Refactor auth/session management into a dedicated module.
- Introduce command dispatcher table and shared arg validators.
- Expand salt length and strengthen password derivation.

### Phase 2 (2-4 sprints)
- Split `Unikernel.ino` into modules and add host/device protocol schema.
- Add integration tests (command parser, auth lockout, VFS persistence).
- Add static analysis and CI matrix (ESP8266/ESP32 + Python host).

## 5) Suggested Quality Gates
- Firmware compile gate for both ESP8266 and ESP32 targets.
- Python host: `ruff`, `mypy` (at least gradual typing for request payloads), and minimal security linting.
- Security regression tests:
  - auth lockout timing
  - firewall allowlist enforcement
  - OTA refusal without configured secret
  - forbidden command matrix for guest mode

## 6) Notes
This report is static-review based (source inspection) and should be followed by runtime penetration testing on isolated hardware/network before production use.
