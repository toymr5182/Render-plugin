# FCL Xclipse RDNA Renderer (Exynos 1580 / Xclipse 540)

ปลั๊กอิน renderer แบบ APK สำหรับ Fold Craft Launcher (FCL): Zink + Kopper (OpenGL 4.6 บน Vulkan) จูนสำหรับ Xclipse 540 (RDNA 3)

## สถานะ (อ่านก่อน)

| ส่วน | สถานะ |
|---|---|
| โครงปลั๊กอิน (manifest `fclPlugin`/`renderer`/`pojavEnv`) | เขียนตามรูปแบบปลั๊กอิน FCL ที่มีอยู่ **ยังไม่ได้ทดสอบกับ FCL จริง** |
| หน้าตั้งค่า + `xclipse.env` + shim (`DLOPEN`) | ตรวจ syntax แล้ว ยังไม่ได้รันบนเครื่อง |
| โหมด **System Vulkan** | น่าจะใช้ได้ ถ้ามี Mesa libs ครบ |
| โหมด **RADV Xclipse** | **ทดลอง** — shim ดึง `vulkan.radeon.so` มาไว้ที่ที่รันได้ แต่ยังไม่ทำให้ Zink ใช้ไดรเวอร์นั้น (ดู "ปัญหาที่ยังเปิดอยู่") |

## สิ่งที่ต้องมีก่อน build

1. Mesa arm64 build ที่มี `libglxshim.so`, `libEGL_mesa.so`, `libzink_dri.so` ตั้งเป็น repo variable `MESA_LIBS_URL` (หรือวางไฟล์ใน `app/src/main/jniLibs/arm64-v8a/` เอง)
2. (ไม่บังคับ) ตั้ง `WITH_ADRENOTOOLS=1` เพื่อ build โหมด RADV ทดลอง

Build: push ขึ้น GitHub แล้วรัน workflow `Build APK` หรือ `gradle assembleDebug` (JDK 17, Android SDK 35, NDK)

## ใช้งาน

1. ติดตั้ง APK แล้ว force-stop FCL
2. เปิดแอปนี้ → เลือกโหมด/preset → Save (ต้องอนุญาต All files access)
3. FCL → Settings → Renderer → **Xclipse RDNA**

โหมด RADV: ดาวน์โหลด zip จาก `JimVulkan/radv-xclipse` (รองรับ Xclipse 540, Android 14+) แล้วแตกไฟล์ `vulkan.radeon.so` ไปไว้ที่ `/sdcard/XclipseRDNA/radv/`

## ปัญหาที่ยังเปิดอยู่

- Zink เรียก `dlopen("libvulkan.so")` เอง ไม่ใช้ handle ที่ libadrenotools คืนมา ต้องทำ dlopen interposition หรือแพตช์ Mesa
- ไม่ทราบว่า libadrenotools ใช้กับ Xclipse ได้ในโปรเซสของ FCL หรือไม่ (README ของ RADV Xclipse ระบุแค่แอปอย่าง Eden/Winlator)
- ผู้พัฒนา RADV Xclipse ระบุว่า 540 ทดสอบเฉพาะบิลด์เก่ากับโปรแกรมทดสอบ ยังไม่จูนประสิทธิภาพ และ GPU hang อาจทำให้เครื่องรีบูต

## เครดิต / ไลเซนส์

โค้ดในรีโปนี้: GPL-3.0 (สอดคล้องกับ FCL) · Mesa/Zink/Kopper: MIT · radv-xclipse: ส่วนที่แก้เป็น MIT, ไฟล์ Mesa คงไลเซนส์เดิม · รูปแบบปลั๊กอินอ้างอิง ShirosakiMio/FCLRendererPlugin, Ironized Zink
