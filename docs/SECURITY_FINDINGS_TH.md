# รายงานช่องโหว่ความปลอดภัย (Defensive) — UniKernel

วันที่ตรวจสอบ: 2026-05-12  
ขอบเขต: `Unikernel.ino`, `src/auth.cpp`, `src/commands.cpp`, `UniAccelHost.py`, `include/common.h`

> เอกสารนี้เป็นการประเมินเชิงป้องกัน (defensive) และปรับปรุงให้เป็น **evidence-based** จากโค้ดปัจจุบัน

## Executive Summary
- พบประเด็นเสี่ยงสูงด้าน **authorization logic**, **cross-platform auth implementation**, และ **host dangerous feature exposure**
- พบประเด็นเสี่ยงกลางด้าน **salt handling**, **memory pressure safety**, และ **session/rate-limit design**
- แก้ไขรายงานเดิม: ไม่พบหลักฐานชัดว่ามี `?pass=` ใน `/api/stats` และ OTA fallback `admin` ในโค้ดปัจจุบัน

---

## 1) Critical / High Findings

### H-01: Cross-platform auth gap ใน `hashPass` (ESP32 branch ว่าง)
**Evidence**
- `src/auth.cpp` มี implementation hash เฉพาะฝั่ง ESP8266; branch `#else` ว่าง

**Impact**
- การตรวจรหัสผ่านบนบางแพลตฟอร์มอาจไม่ deterministic หรือผิดพลาดเชิงตรรกะด้าน auth

**Defensive PoC Idea**
1. build และรัน test บน ESP8266/ESP32 ด้วย credential เดียวกัน
2. assert ว่า hash output และผล auth ต้อง deterministic และตรง policy เดียวกัน

**Remediation**
- implement hash path สำหรับ ESP32 ให้ behavior เท่ากันทุกแพลตฟอร์ม
- เพิ่ม regression test cross-platform

### H-02: Authorization gate ผูกกับ `shellDepth == 0`
**Evidence**
- ใน `dispatchCommand` เงื่อนไขบังคับ auth คือ `if (c.authRequired && !currentAuth && shellDepth == 0)`

**Impact**
- ถ้า context ไหนทำให้ `shellDepth > 0` ได้ อาจเกิด policy bypass สำหรับ protected commands

**Defensive PoC Idea**
1. สร้าง integration test จำลอง nested shell/context
2. เรียกคำสั่งที่ต้อง auth (`passwd`, `telnet`, `ota`, `firewall`)
3. คาดหวัง 401 ทุกกรณีเมื่อยังไม่ authenticated

**Remediation**
- บังคับ auth invariant โดยไม่ผูกกับ shell depth

### H-03: Dangerous host capability (`gpu_inject`) เพิ่ม attack surface
**Evidence**
- `UniAccelHost.py` มีการเพิ่มคำสั่ง `gpu_inject` ใน allowed commands และ command handler

**Impact**
- หากมีการเปิดเผย endpoint/สิทธิ์ไม่รัดกุม อาจกลายเป็นทางรันงานอันตรายบนโฮสต์

**Defensive PoC Idea**
1. production profile ต้องปิดคำสั่งนี้ default
2. test ว่าคำสั่งนี้ถูก deny หากไม่ใช่ admin channel + signed request

**Remediation**
- feature flag ปิด default + ACL + signed admin action + audit log

---

## 2) Medium Findings

### M-01: Salt handling ไม่ binary-safe (`strlen` บนข้อมูล EEPROM)
**Evidence**
- `hashPass` อ่าน salt จาก EEPROM แล้วใช้ `strlen(salt)`

**Impact**
- ถ้ามี null byte ใน salt จะลด entropy ที่ใช้จริง

**Remediation**
- ใช้ fixed-length (`PASS_SALT_LEN`) กับ binary buffer โดยตรง

### M-02: `strdup` ใน command path ไม่มี null check
**Evidence**
- `dispatchCommand` ใช้ `char *cmdLine = strdup(line);` และใช้งานต่อทันที

**Impact**
- ภายใต้ memory pressure อาจ dereference null / crash / reset

**Remediation**
- ตรวจ null-return และ fail-safe response

### M-03: Rate-limit/auth fail counters เป็น global state เดียว
**Evidence**
- `loginFailCount`, `loginCooldown`, `lastLoginAttempt` ใช้รวม

**Impact**
- มีโอกาสถูก abuse เพื่อ throttle/lockout ผู้ใช้อื่น (availability)

**Remediation**
- แยกตาม source/channel และใช้ sliding window

---

## 3) Clarification / Corrections จากรายงานก่อนหน้า
1. **`/api/stats?pass=`**: โค้ดปัจจุบันอ่าน `Authorization` header; ไม่พบหลักฐานว่าต้องส่งผ่าน query string
2. **OTA fallback `admin`**: โค้ดปัจจุบันแสดงพฤติกรรม “ไม่มีรหัสผ่าน => OTA disabled”

> หมายเหตุ: หากมี branch/commit อื่นที่ยังมีพฤติกรรมเดิม ให้แยกรายงานตาม commit hash เพื่อไม่สับสน

---

## 4) Explain Chain (Defensive)

### Chain-A: Nested Context -> Auth Gate Weakness -> Protected Command Execution
1. เข้าสู่ context ที่ทำให้ `shellDepth` เปลี่ยน
2. auth gate พึ่ง `shellDepth == 0`
3. เรียก protected command ใน path ที่ไม่ควรผ่าน
4. กระทบ configuration/security posture
5. ปิด chain ด้วย auth invariant + tests ทุก depth

### Chain-B: Config/Memory Access -> Weak Salt Handling -> Credential Abuse
1. ได้ข้อมูล config/hash จากอุปกรณ์
2. salt ใช้งานไม่ binary-safe + KDF cost ต่ำ
3. ลดต้นทุนการเดารหัส offline
4. ใช้ credential ที่ได้เพื่อเข้าถึง endpoint สำคัญ
5. ปิด chain ด้วย binary-safe salt + stronger KDF + rotate secrets

### Chain-C: Exposed Host API -> Dangerous Command Surface -> Host Compromise Risk
1. เข้าถึง host API ได้
2. ใช้ surface คำสั่งอันตราย (`gpu_inject`) เมื่อ policy ไม่แน่น
3. เพิ่มโอกาส abuse ทรัพยากร/โค้ดพาธฝั่ง host
4. กระทบทั้ง host และ control plane
5. ปิด chain ด้วย disable-by-default + strict ACL + signed actions

---

## 5) Prioritized Backlog
- **P0**: แก้ `hashPass` ให้ครบทุกแพลตฟอร์ม + cross-platform tests
- **P0**: refactor auth gate ไม่ให้ขึ้นกับ `shellDepth`
- **P1**: binary-safe salt + ปรับ KDF policy
- **P1**: harden `gpu_inject` (หรือปิดใน production)
- **P2**: session model/rate-limit แยก per-channel/per-source
