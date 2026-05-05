# Static Security Audit (UniKernel)

เอกสารนี้สรุปช่องโหว่ที่ตรวจพบจาก `UniKernel.ino` พร้อม PoC เพื่อพิสูจน์การโจมตีเชิงปฏิบัติ (สำหรับใช้ในแล็บที่ได้รับอนุญาตเท่านั้น)

## Scope
- Target: ESP8266/ESP32 (Arduino framework)
- File: `UniKernel.ino`
- Method: Static analysis + reproducible input-driven PoC

---

## 1) Stack Buffer Overflow ในคำสั่ง `trigger`
- **Vulnerability:** Unbounded `%s` in `sscanf` -> stack overwrite
- **Severity:** High
- **Location:** `UniKernel.ino` บริเวณ parse `trigger`

### Why vulnerable
โค้ดใช้ `sscanf(args, "%s %1s %d %s", cond, opStr, &val, act)` โดยไม่จำกัดความยาว `%s` สำหรับ `cond[16]` และ `act[32]`.

### PoC (Serial/Telnet shell)
ส่ง payload ยาวเกินขนาดบัฟเฟอร์:

```sh
trigger AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA = 1 BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB
```

**ผลที่คาดหวัง:**
- อุปกรณ์ค้าง รีบูตเอง หรือพฤติกรรมผิดปกติ (memory corruption)
- บนบาง build อาจเห็น reset reason เกี่ยวกับ exception/watchdog

### Fix
กำหนด field width ให้ตรงกับ buffer:
```cpp
if (sscanf(args, "%15s %1s %d %31s", cond, opStr, &val, act) == 4) {
  ...
}
```

---

## 2) Credential Leak ผ่าน `boot save` (Wi‑Fi SSID/PSK ถูกเขียนลงไฟล์)
- **Vulnerability:** Plaintext credential persistence
- **Severity:** High
- **Location:** คำสั่ง `boot save` ประกอบสตริง `wifi connect <ssid> <psk>` แล้วบันทึกลง VFS

### Why vulnerable
คำสั่งสร้างไฟล์ profile ที่มี `WiFi.psk()` แบบ plaintext ทำให้คนที่อ่านไฟล์ได้เห็นรหัส Wi‑Fi ทันที.

### PoC
1) ตั้งค่า Wi‑Fi ให้เชื่อมต่อก่อน:
```sh
wifi connect MyLabSSID SuperSecretPass123
```
2) บันทึกโปรไฟล์บูต:
```sh
boot save home
```
3) อ่านไฟล์ที่สร้าง (ชื่อจะเป็น `homerc.sh`):
```sh
cat homerc.sh
```

**ผลที่คาดหวัง:**
- เจอบรรทัดลักษณะ `wifi connect MyLabSSID SuperSecretPass123; ...`

### Fix
- ห้ามบันทึก PSK ลงไฟล์ shell
- ใช้ secure storage (เช่น encrypted NVS) และให้สคริปต์อ้างอิง alias/ID แทน secret จริง

---

## 3) Default Secret สำหรับ OTA/BT API (`unikernel`)
- **Vulnerability:** Hardcoded fallback password
- **Severity:** High
- **Location:** OTA และ `/api/bt` มี fallback เป็น `"unikernel"`

### Why vulnerable
หาก EEPROM ยังไม่ตั้งค่ารหัสผ่าน ระบบจะใช้ค่าดีฟอลต์ที่เดาง่ายและถูกเปิดเผยในซอร์ส

### PoC (HTTP)
> ใช้ในเครือข่ายทดสอบเท่านั้น

```sh
curl "http://<device-ip>/api/bt?pass=unikernel&val=1"
```

**ผลที่คาดหวัง:**
- ได้ `200 OK` และเปิด BT ได้ในอุปกรณ์ ESP32 (ถ้าไม่มีการตั้งรหัสใหม่)

### Fix
- บังคับตั้งรหัสผ่านเฉพาะเครื่องตอน provisioning
- ปฏิเสธการทำงานทั้งหมดเมื่อยังเป็นค่า default

---

## 4) Unauthenticated `/api/stats` Information Disclosure
- **Vulnerability:** No auth on telemetry endpoint
- **Severity:** Medium
- **Location:** route `/api/stats`

### Why vulnerable
เปิดเผย free heap/uptime/state ให้ผู้ไม่ยืนยันตัวตน ใช้ช่วย reconnaissance ได้

### PoC
```sh
curl "http://<device-ip>/api/stats"
```

**ผลที่คาดหวัง:**
- ได้ JSON สถานะเครื่องโดยไม่ต้อง login

### Fix
- บังคับ auth ทุก API
- จำกัดเครือข่ายที่เข้าถึงได้ + rate limit

---

## 5) OTA Integrity ยังไม่ใช่ Signed Firmware
- **Vulnerability:** Password-only OTA (no digital signature validation)
- **Severity:** High
- **Location:** `ArduinoOTA.setPassword(...)` ถูกใช้โดยไม่เห็น verify ลายเซ็น firmware

### Why vulnerable
หาก attacker ได้ OTA password สามารถอัปโหลด firmware ปลอมได้

### PoC (แนวคิดทดสอบ)
1) ตั้ง OTA ให้เปิดใช้งาน
2) ใช้เครื่องมือ OTA push binary ที่ไม่ได้ signed แต่รู้รหัสผ่าน
3) หากอัปโหลดผ่าน แสดงว่าไม่มี signature gate

> PoC นี้ควรรันเฉพาะในแล็บและ firmware ทดสอบเท่านั้น

### Fix
- ใช้ secure boot + signed image verification
- จำกัดช่วงเวลา/เครือข่ายที่เปิด OTA

---

## Hardening Checklist (Actionable)
1. แก้ `%s` ทั้งหมดให้มี width จำกัดทันที
2. ลบ default secret ทุกจุด และบังคับ first-boot enrollment
3. หยุดเก็บ credential แบบ plaintext ใน VFS/EEPROM
4. ปิด endpoint ที่ไม่ auth หรือย้ายไป authenticated-only
5. เพิ่ม signed firmware verification สำหรับ OTA
6. เพิ่ม logging + lockout + rate limiting ในทุก remote control API


---

## Deep-Dive Vulnerability Analysis (Static)

ส่วนนี้เป็นการอธิบายเชิงกลไกว่าโค้ดมีช่องโหว่ได้อย่างไรในระดับ low-level บน MCU (ESP8266/ESP32)

### A) Memory Safety & Pointer Integrity

#### A.1 `trigger` parser overflow chain
- จุดเริ่มต้นคือการรับ input จาก shell แล้วส่งเข้า parser ของคำสั่ง `trigger`.
- การใช้ `%s` แบบไม่จำกัดความยาวใน `sscanf` ทำให้ write ทับ stack frame ของฟังก์ชันเป้าหมายได้เมื่อ token ยาวเกิน buffer.
- ผลกระทบที่พบบ่อยใน MCU:
  - corruption ของตัวแปร local ถัดไป
  - return address corruption (ขึ้นกับ compiler/protection)
  - watchdog reset จาก flow ผิดปกติ
- ระดับความเสี่ยงเป็น **High** เพราะ attacker ต้องการเพียง input text เท่านั้น (ผ่าน serial/telnet).

#### A.2 Expansion path มีโอกาส overflow จาก pointer arithmetic
- ใน logic ขยายตัวแปร (`$VCC/$TEMP/$RAM`) มีการเลื่อน pointer ปลายทางแล้ว append string ต่อเนื่อง.
- ถึงแม้มี guard บางช่วง แต่การใช้ `sprintf` โดยไม่ส่งขนาด buffer ที่เหลือ ทำให้ไม่ enforce boundary แบบ end-to-end.
- ในระบบ embedded ที่ heap/stack จำกัด บั๊กลักษณะนี้อาจไม่ crash ทันทีแต่ทำให้เกิด behavior เพี้ยนแบบสุ่ม.

#### A.3 Secret lifetime ใน RAM/Storage
- credential ถูกประกอบเป็น command string แล้วเก็บใน VFS plaintext.
- secret อยู่ทั้งในหน่วยความจำ runtime และ persisted storage เพิ่ม attack surface (dump memory / dump flash).

### B) Hardware/RTOS/Concurrency Context

#### B.1 ISR shared state
- `system_ticks` เป็น `volatile` ช่วยให้ compiler ไม่ optimize ทิ้งการอ่าน/เขียนข้าม ISR.
- อย่างไรก็ตาม ถ้ามีการขยายฟีเจอร์ในอนาคต (เช่นอ่านหลายตัวแปรร่วมกัน) ควรใช้ critical section เพื่อ atomic snapshot.

#### B.2 Tasking model
- ไม่พบ `xTaskCreate`/mutex/semaphore ในไฟล์นี้ จึงยังไม่เห็น race แบบ FreeRTOS task-to-task จาก source นี้โดยตรง.
- แต่ยังมี logical concurrency ระหว่าง network handlers, OTA handler, และ main loop ที่ต้องรักษา invariant ของ state machine ให้ดี.

### C) Network & IoT Attack Surface

#### C.1 Missing auth boundary
- `/api/stats` เปิดข้อมูลระบบโดยไม่ยืนยันตัวตน -> ช่วย attacker ทำ fingerprinting และเลือกจังหวะโจมตี (เช่นช่วง heap ต่ำ).

#### C.2 Weak secret policy
- การมี default secret คงที่ (`unikernel`) ทำให้การโจมตีแบบ known-default credential มีโอกาสสำเร็จสูงมาก โดยเฉพาะอุปกรณ์ที่ยังไม่ provision.

#### C.3 OTA integrity model
- password-only OTA ป้องกันได้ระดับ channel access เท่านั้น แต่ไม่ป้องกัน firmware tampering เมื่อ secret หลุด.
- secure update ที่ถูกต้องต้องมี **authenticity + integrity** ของ image (digital signature verification).

### D) Storage Security (EEPROM/NVS)

#### D.1 No encryption-at-rest
- การเก็บ secret ใน EEPROM โดยตรงทำให้ physical attacker หรือผู้ที่เข้าถึง flash dump อ่านความลับได้.
- สำหรับ ESP32 ควรพิจารณา encrypted NVS + key management จาก hardware root-of-trust.

### E) Risk Prioritization

ลำดับการแก้ไขที่แนะนำ (มากไปน้อย):
1. ปิด overflow input path (`trigger`) ด้วย bounded parsing
2. ลบ default secret และบังคับ first-boot provisioning
3. ยุติการเก็บ plaintext Wi‑Fi credential ใน VFS
4. บังคับ auth สำหรับทุก API (รวม `/api/stats`)
5. เพิ่ม signed firmware verification ใน OTA

### F) Secure Coding Pattern ที่ควรใช้ต่อเนื่อง

- ใช้ `snprintf`/length-bounded parsing ทุกจุดที่รับ input ภายนอก
- ใช้ explicit null-termination หลัง `strncpy`
- แยก privilege: endpoint ควบคุมอุปกรณ์ต้อง auth เสมอ
- ทำ audit log สำหรับ event สำคัญ (auth fail, OTA start/end, config change)
- ใช้ defense-in-depth: network segmentation + rate limit + lockout

