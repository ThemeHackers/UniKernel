# รายงานช่องโหว่ความปลอดภัย (Defensive) — UniKernel

วันที่ตรวจสอบ: 2026-05-12  
ขอบเขต: `Unikernel.ino`, `UniAccelHost.py`, เอกสารประกอบใน `README.md`, และรายงานเดิมใน `docs/AUDIT_REPORT.md`

> เอกสารนี้เน้นการทดสอบเชิงป้องกันเท่านั้น ไม่ให้ขั้นตอนเจาะระบบเชิงรุก

## 1) ช่องโหว่ระดับสูง (High)

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

## 2) ช่องโหว่ระดับกลาง (Medium)

### M-01: Salt รหัสผ่านสั้นเกินไป (`PASS_SALT_LEN = 4`)
- **ความเสี่ยง:** ลดต้นทุน brute-force/offline cracking
- **PoC เชิงป้องกัน:** unit test ตรวจ policy ว่า salt ขั้นต่ำ 16 bytes
- **แนวทางแก้ไข:** เพิ่ม salt >= 16 bytes และใช้ KDF ที่แข็งแรงขึ้นตามข้อจำกัด MCU

### M-02: `except: pass` ในโฮสต์ Python
- **ความเสี่ยง:** กลบข้อผิดพลาดสำคัญ ทำให้ระบบ fallback แบบไม่ปลอดภัย
- **PoC เชิงป้องกัน:** บังคับ fault injection แล้วคาดหวังว่าระบบ log และ fail-closed
- **แนวทางแก้ไข:** จับ exception แบบเจาะจง + structured logging

## 3) แผนทดสอบยืนยันการแก้ไข (Verification Checklist)

1. **Auth/Session**
   - ปฏิเสธ credential ใน URL
   - จำกัดอายุ token + revoke ได้
2. **OTA Security**
   - OTA disabled จนกว่าจะตั้ง secret ที่ผ่าน policy
3. **Dangerous Feature Flag**
   - `gpu_inject` ปิดใน production และเปิดได้เฉพาะ local admin mode
4. **Transport Security**
   - ไม่มี telnet ใน production, ใช้ช่องทางเข้ารหัสเท่านั้น
5. **Logging Hygiene**
   - redaction ข้อมูลลับใน logs, query/body/header

## 4) สรุป
- ช่องโหว่สำคัญที่สุดคือความสามารถที่อาจนำไปสู่การรันโค้ดบนโฮสต์, การตั้งค่าค่าเริ่มต้นที่อ่อนแอ, และการส่ง credential แบบไม่ปลอดภัย
- ควรเริ่มแก้ที่การ “ปิดความเสี่ยงสูงโดย default” ก่อน แล้วค่อยทำ hardening ระยะกลาง
