# รายงานช่องโหว่ความปลอดภัย (Defensive) — UniKernel

วันที่ตรวจสอบ: 2026-05-12  
ขอบเขต: `Unikernel.ino`, `src/auth.cpp`, `src/commands.cpp`, `UniAccelHost.py`, `README.md`, และ `docs/AUDIT_REPORT.md`

> เอกสารนี้เน้นการทดสอบเชิงป้องกันเท่านั้น ไม่ให้ขั้นตอนเจาะระบบเชิงรุก

## 1) ช่องโหว่ซับซ้อนที่พบเพิ่มเติม

### C-01: Authentication implementation gap บน ESP32 (`hashPass`)
- **คำอธิบาย:** ใน `src/auth.cpp` ฟังก์ชัน `hashPass` มีเฉพาะ implementation สำหรับ ESP8266 ส่วน branch `#else` (เช่น ESP32) ไม่มี logic คำนวณ hash
- **ผลกระทบ:** การยืนยันตัวตนอาจใช้ข้อมูลไม่ถูกต้อง/ไม่ได้ initialize ทำให้เกิดความเสี่ยง auth bypass หรือ auth failure แบบคาดเดาไม่ได้
- **PoC เชิงป้องกัน:**
  1. รัน build target ESP32
  2. ทดสอบ login/password check หลายรอบด้วย credential เดิม
  3. หากผลยืนยันตัวตนไม่ deterministic ให้ถือว่า fail และบล็อก release
- **แนวทางแก้ไข:** เพิ่ม implementation hash สำหรับ ESP32 ให้เทียบเท่า ESP8266 และเพิ่ม unit test cross-platform

### C-02: Policy bypass ผ่าน `shellDepth` ใน dispatcher
- **คำอธิบาย:** ใน `dispatchCommand` เงื่อนไขบังคับ auth คือ `if (c.authRequired && !currentAuth && shellDepth == 0)`
- **ผลกระทบ:** เมื่อ `shellDepth > 0` คำสั่งที่ต้อง auth อาจถูกรันได้แม้ยังไม่ login (ขึ้นกับ flow ที่เพิ่ม depth)
- **PoC เชิงป้องกัน:**
  1. สร้าง integration test จำลอง nested shell/context ที่ทำให้ `shellDepth` เพิ่ม
  2. เรียกคำสั่ง protected (`passwd`, `telnet`, `ota`, `firewall`)
  3. คาดหวังผล: ต้องถูกปฏิเสธทั้งหมดหากไม่ authenticated
- **แนวทางแก้ไข:** แยกนโยบาย auth ออกจาก shell depth โดยตรวจสิทธิ์ทุกครั้งก่อน execute คำสั่ง protected

### C-03: Salt handling ใช้ `strlen` กับข้อมูล EEPROM
- **คำอธิบาย:** salt ถูกอ่านจาก EEPROM แล้วนำไปใช้ผ่าน `strlen(salt)` ซึ่งไม่เหมาะกับข้อมูลไบนารี
- **ผลกระทบ:** ถ้า salt มี null byte ก่อนครบความยาว จะทำให้ entropy ที่ใช้ hash ลดลงโดยไม่ตั้งใจ
- **PoC เชิงป้องกัน:**
  1. เขียน salt ที่มี null byte ตรงกลาง
  2. ตรวจว่าผล hash ต้องยังพึ่งพา salt ครบ `PASS_SALT_LEN`
- **แนวทางแก้ไข:** ใช้ความยาวคงที่ (`PASS_SALT_LEN`) ใน hash function และเก็บ salt แบบไบนารีชัดเจน

### C-04: ความเสี่ยง DoS จาก dynamic allocation (`strdup`) ใน command path
- **คำอธิบาย:** `dispatchCommand` ใช้ `strdup(line)` ทุกคำสั่ง โดยไม่มีตรวจ null-return
- **ผลกระทบ:** เมื่อหน่วยความจำตึง อาจเกิด null dereference หรือ crash/reset ได้
- **PoC เชิงป้องกัน:**
  1. stress test ด้วยคำสั่งถี่/ยาวภายใต้หน่วยความจำต่ำ
  2. คาดหวังว่าเมื่อ alloc fail ต้องตอบ error และไม่ crash
- **แนวทางแก้ไข:** ตรวจผลลัพธ์ `strdup` ก่อนใช้ และพิจารณาใช้ buffer คงที่แทน allocation บ่อยครั้ง

## 2) ช่องโหว่ระดับสูง (High)

### H-01: ความสามารถฝั่งโฮสต์ที่เสี่ยงต่อ Remote Code Execution (`gpu_inject`)
- **คำอธิบาย:** รายงานเดิมระบุว่ามีคำสั่ง `gpu_inject` ที่ยอมรับโค้ด CUDA เพื่อคอมไพล์/รันบนโฮสต์
- **ผลกระทบ:** หาก endpoint ถูกเข้าถึงโดยผู้ไม่หวังดี อาจนำไปสู่การรันโค้ดที่ไม่ได้รับอนุญาตบนเครื่องโฮสต์
- **PoC เชิงป้องกัน (แนวคิดทดสอบ):**
  1. ยืนยันว่า build production ปิดฟีเจอร์ `gpu_inject` เป็นค่าเริ่มต้น
  2. ทดสอบ API แล้วคาดหวังผลลัพธ์เป็น `403/disabled` ทุกกรณีที่ไม่ใช่ admin channel
  3. เพิ่ม regression test: ถ้าเปิดฟีเจอร์โดยไม่ตั้ง ACL ต้อง fail ตั้งแต่ startup
- **แนวทางแก้ไข:** ปิดฟีเจอร์ default, บังคับ allowlist ต้นทาง, แยก admin channel เฉพาะ local/signed request

### H-02: OTA ใช้รหัสผ่าน fallback ที่คาดเดาง่าย (`admin`)
- **คำอธิบาย:** ถ้าไม่พบ OTA hash มี fallback รหัสผ่าน
- **ผลกระทบ:** เสี่ยงโดนยึดอุปกรณ์ผ่าน OTA หากอุปกรณ์อยู่ในเครือข่ายที่เข้าถึงได้
- **PoC เชิงป้องกัน:**
  1. boot ด้วยสถานะที่ยังไม่ตั้งค่า secret
  2. คาดหวังว่า OTA ต้อง **ไม่เริ่มทำงาน** และแจ้งสถานะ “ต้องตั้งรหัสผ่านก่อน”
  3. หลังตั้งรหัสผ่านแข็งแรงแล้ว OTA จึงเริ่มได้
- **แนวทางแก้ไข:** ยกเลิก fallback ทั้งหมด, บังคับ first-boot setup

### H-03: ส่งรหัสผ่านผ่าน query string (`/api/stats?pass=`)
- **คำอธิบาย:** credential ใน URL เสี่ยงหลุดผ่าน log/history/proxy
- **ผลกระทบ:** ข้อมูลลับรั่วโดยไม่ต้องโจมตีซับซ้อน
- **PoC เชิงป้องกัน:**
  1. ส่งคำขอแบบ query credential
  2. ระบบต้องตอบกลับว่าไม่รองรับ (400/401) และแนะนำใช้ Authorization header
  3. ตรวจ log ว่าไม่มีการบันทึก secret
- **แนวทางแก้ไข:** ใช้ header/token อายุสั้น + redaction log

### H-04: Telnet ยังคงมีโหมดเปิดใช้งานได้
- **คำอธิบาย:** telnet เป็น plaintext protocol
- **ผลกระทบ:** ดัก credential/คำสั่งได้ง่ายในเครือข่ายเดียวกัน
- **PoC เชิงป้องกัน:**
  1. build profile production ต้องไม่มี telnet listener
  2. สแกนพอร์ตบนอุปกรณ์หลังบูตแล้วต้องไม่พบพอร์ต telnet
- **แนวทางแก้ไข:** ปิดถาวรใน production หรือใช้ compile-time flag ที่ default = off

## 3) ช่องโหว่ระดับกลาง (Medium)

### M-01: Salt รหัสผ่านสั้นเกินไป (`PASS_SALT_LEN = 16` ควร enforce binary-safe + KDF)
- **ความเสี่ยง:** แม้ยาวขึ้นแล้ว แต่ถ้าไม่ใช้ KDF ที่เหมาะสมยังเสี่ยง offline cracking
- **PoC เชิงป้องกัน:** unit test ตรวจ policy (salt, rounds, format)
- **แนวทางแก้ไข:** คง salt >= 16 bytes, เพิ่ม KDF cost ตามข้อจำกัด MCU

### M-02: `except: pass` ในโฮสต์ Python
- **ความเสี่ยง:** กลบข้อผิดพลาดสำคัญ ทำให้ระบบ fallback แบบไม่ปลอดภัย
- **PoC เชิงป้องกัน:** บังคับ fault injection แล้วคาดหวังว่าระบบ log และ fail-closed
- **แนวทางแก้ไข:** จับ exception แบบเจาะจง + structured logging

## 4) แผนทดสอบยืนยันการแก้ไข (Verification Checklist)
1. **Cross-platform Auth**: ผล auth ต้อง deterministic ทั้ง ESP8266/ESP32
2. **Authorization Invariant**: protected command ต้อง require auth เสมอ ไม่ขึ้นกับ shell depth
3. **Crypto Hygiene**: salt/hash ใช้ binary-safe handling
4. **Dangerous Feature Flag**: `gpu_inject` ปิดใน production
5. **Transport Security**: ไม่มี telnet ใน production
6. **Logging Hygiene**: redact credential ทุกช่องทาง

## 5) สรุป
- ช่องโหว่ที่ซับซ้อนที่สุดรอบนี้คือ logic gap ข้ามแพลตฟอร์ม (ESP32 hash), policy bypass condition (`shellDepth`), และการจัดการ salt ที่ไม่ binary-safe
- ควรทำ security regression test ที่เจาะจง invariant เหล่านี้ก่อน release
