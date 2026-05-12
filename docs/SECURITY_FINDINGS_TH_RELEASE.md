# UniKernel Security Findings (TH) — Release Note

Release Date: 2026-05-12  
Artifact: `docs/SECURITY_FINDINGS_TH.md`

## Scope
- `Unikernel.ino`
- `src/auth.cpp`
- `src/commands.cpp`
- `UniAccelHost.py`
- `include/common.h`

## Release Highlights
- จัดทำรายงานช่องโหว่เชิงป้องกันฉบับสมบูรณ์ (ภาษาไทย) พร้อมหลักฐานอ้างอิงจากโค้ดปัจจุบัน
- สรุปความเสี่ยงหลัก:
  - Cross-platform auth gap (`hashPass`)
  - Authorization gate ผูกกับ `shellDepth`
  - Dangerous host feature surface (`gpu_inject`)
  - Salt handling ไม่ binary-safe
  - `strdup` memory-safety concern ภายใต้ memory pressure
- เพิ่ม explain-chain เพื่อช่วยทีมออกแบบ mitigation ตามลำดับ attack path
- เพิ่ม prioritized hardening backlog (P0/P1/P2)

## Release Readiness Notes
- เอกสารนี้เป็น security assessment เชิงป้องกัน ไม่รวมขั้นตอนโจมตีเชิงรุก
- แนะนำให้ทีม implement รายการ P0 ก่อน release firmware/host ถัดไป

## References
- Full report: `docs/SECURITY_FINDINGS_TH.md`
