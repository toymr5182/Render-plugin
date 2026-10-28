# FCL Xclipse RDNA Renderer (Exynos 1580 / Xclipse 540)

ปลั๊กอิน renderer แบบ APK สำหรับ launcher ตระกูล Pojav/FCL/Zalith: Zink + Kopper (OpenGL 4.6 บน Vulkan) จูนสำหรับ Xclipse 540 (RDNA 3)

## สถานะ (อ่านก่อน)

| ส่วน | สถานะ |
|---|---|
| โครงปลั๊กอิน (manifest `fclPlugin`/`renderer`/`pojavEnv`) | ทำงานกับ ZalithLauncher 3 ได้ (ตัวเลือก renderer ขึ้นในรายการ) |
| `libglxshim.so` (GLX→EGL resolver) | build จากรีโปนี้เอง ไม่ต้องพึ่ง Mesa archive แล้ว |
| หน้าตั้งค่า + `xclipse.env` + shim (`DLOPEN`) | shim ถูกโหลดจริงในโปรเซสเกม (ยืนยันจาก log) |
| โหมด **System Vulkan** | ต้องมี `libEGL_mesa.so` + `libzink_dri.so` จากที่ใดที่หนึ่ง (bundled หรือของ launcher) |
| โหมด **RADV Xclipse** | **ทดลอง** — shim ดึง `vulkan.radeon.so` มาไว้ที่ที่รันได้ แต่ยังไม่ทำให้ Zink ใช้ไดรเวอร์นั้น (ดู "ปัญหาที่ยังเปิดอยู่") |

## ทำไมเคย "เด้ง" ตอนเลือก renderer นี้ (แก้แล้ว)

Launcher อ่าน `renderer` จาก manifest (`Xclipse RDNA:libglxshim.so:libEGL_mesa.so`) แล้วส่ง
`-Dorg.lwjgl.opengl.libname=<native dir ของปลั๊กอิน>/libglxshim.so` ให้เกม จากนั้น LWJGL จะ `dlopen()`
**พาธแบบ absolute** นั้นทันที (`GL.create()` → `Library.loadNative`)

ถ้า APK ไม่มีไฟล์นั้น (เช่น build ตอนที่ยังไม่ได้ตั้ง `MESA_LIBS_URL` แล้วสคริปต์ fetch ถูกข้าม) เกมจะตายที่
`Loading library OpenGL` ด้วย

```
java.lang.UnsatisfiedLinkError: Failed to dynamically load library:
  /data/app/.../dev.tuip.xclipserdna.../lib/arm64/libglxshim.so (error = null)
```

ตอนนี้ `libglxshim.so` ถูกคอมไพล์จาก `app/src/main/cpp/glxshim.c` ในรีโปนี้เสมอ (forward
`glXGetProcAddress`/`glXGetProcAddressARB` ไป `eglGetProcAddress`, GLX context คืนค่า "ไม่มี GLX")
และ workflow จะ **fail** ถ้า APK ไม่มี lib ที่ manifest ประกาศไว้

## สิ่งที่ต้องมีก่อน build

1. รัน workflow `Build APK` (หรือ `gradle assembleDebug` ด้วย JDK 17 + SDK 35 + NDK)
2. (ไม่บังคับ) ตั้ง repo variable `MESA_LIBS_URL` เป็น zip/tar.gz ของ Mesa arm64 ที่มี
   `libEGL_mesa.so`, `libzink_dri.so` เพื่อฝังมาใน APK — ถ้าไม่ตั้ง ปลั๊กอินจะใช้ Mesa ที่มากับ launcher
   (resolve ตามชื่อผ่าน `LD_LIBRARY_PATH`) ถ้า launcher ไม่มี Mesa ให้ ขั้น EGL จะล้มเหลว
3. (ไม่บังคับ) ตั้ง `WITH_ADRENOTOOLS=1` เพื่อ build โหมด RADV ทดลอง

## ใช้งาน

1. ติดตั้ง APK แล้ว force-stop launcher
2. เปิดแอปนี้ → เลือกโหมด/preset → Save (ต้องอนุญาต All files access)
3. Settings → Renderer → **Xclipse RDNA**

`xclipse.env` รับค่าพิเศษเพิ่มได้: `XCLIPSE_EGL_LIB=<ชื่อหรือพาธของ EGL ที่ต้องการ>` (ค่าเริ่มต้น
ลอง `libEGL_mesa.so` แล้วจึง `libEGL.so`)

**ไม่บังคับ GL 4.6 แล้ว** — ค่าเริ่มต้นปล่อยให้ Mesa รายงานเวอร์ชันจริง เพราะการฝืน 4.6 บนไดรเวอร์ Vulkan
ที่รองรับไม่ถึงทำให้ Zink สร้าง context ไม่ได้ แล้ว launcher จะ crash ตอน parse เวอร์ชัน (SIGSEGV ใน `strtol`)
ถ้าอยากฝืน 3.3 ค่อยติ๊กในหน้าตั้งค่า

ใน `libglxshim.so` มี guard ให้ `glGetString(GL_VERSION)` คืนค่า placeholder แทน `NULL` เมื่อยังไม่มี
context current (log จะขึ้น `XclipseRDNA/glx: glGetString(GL_VERSION) returned NULL ...`) — กัน crash
ใน `strtol` ที่เห็นใน log ก่อนหน้า

**`mesa_glthread` ปิดเป็นค่าเริ่มต้น** (glthread batch thread เป็นต้นเหตุ crash ที่พบบ่อยกับ Kopper บนไดรเวอร์
Vulkan ของ vendors) — preset "Performance" เปิดให้

log ของ shim เขียนลง **ทั้ง logcat และ stderr** จึงเห็นในหน้า log ของ launcher:

```
XclipseRDNA/glx: glxshim loaded (GLX entry points are forwarded to EGL)
XclipseRDNA/glx: EGL library: libEGL_mesa.so
```

โหมด RADV: ดาวน์โหลด zip จาก `JimVulkan/radv-xclipse` (รองรับ Xclipse 540, Android 14+) แล้วแตกไฟล์
`vulkan.radeon.so` ไปไว้ที่ `/sdcard/XclipseRDNA/radv/`

## ปัญหาที่ยังเปิดอยู่

- Zink เรียก `dlopen("libvulkan.so")` เอง ไม่ใช้ handle ที่ libadrenotools คืนมา ต้องทำ dlopen interposition หรือแพตช์ Mesa
- ไม่ทราบว่า libadrenotools ใช้กับ Xclipse ได้ในโปรเซสของ launcher หรือไม่
- ยังไม่ได้ทดสอบบนเครื่องจริงหลังแก้ `libglxshim.so` — ต้องดู log ว่า EGL ที่ถูกเลือกคือ `libEGL_mesa.so` หรือ `libEGL.so`
- ผู้พัฒนา RADV Xclipse ระบุว่า 540 ทดสอบเฉพาะบิลด์เก่ากับโปรแกรมทดสอบ ยังไม่จูนประสิทธิภาพ และ GPU hang อาจทำให้เครื่องรีบูต

## เครดิต / ไลเซนส์

โค้ดในรีโปนี้: GPL-3.0 (สอดคล้องกับ FCL) · Mesa/Zink/Kopper: MIT · radv-xclipse: ส่วนที่แก้เป็น MIT, ไฟล์ Mesa คงไลเซนส์เดิม · รูปแบบปลั๊กอินอ้างอิง ShirosakiMio/FCLRendererPlugin, Ironized Zink
