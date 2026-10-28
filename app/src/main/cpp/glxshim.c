// libglxshim.so
// GLX -> EGL function resolver for LWJGL on Android (Pojav / Zink / Kopper style setups).
//
// Why this file exists
// --------------------
// The launcher reads the plugin's `renderer` metadata ("Xclipse RDNA:libglxshim.so:libEGL_mesa.so")
// and passes -Dorg.lwjgl.opengl.libname=<plugin native dir>/libglxshim.so to the game.
// LWJGL then dlopen()s that exact absolute path (org.lwjgl.opengl.GL -> Library.loadNative).
// If the file is not inside the APK the game dies while "Loading library OpenGL" with
//   Failed to dynamically load library: .../libglxshim.so (error = null)
// which is what happens when this plugin is built without any Mesa archive: the APK used to
// declare a renderer whose GL library it did not contain.
//
// After the library loads, LWJGL resolves every GL entry point through the first of
// glXGetProcAddress / glXGetProcAddressARB / eglGetProcAddress / OSMesaGetProcAddress
// exported by that library (GL#create(SharedLibrary)), falling back to dlsym() on the library
// itself. So a resolver that forwards to eglGetProcAddress() is enough to drive the real
// implementation, which lives in Mesa's libEGL_mesa.so (Zink) or the system libEGL.so.
//
// Lookup order for the EGL library: $XCLIPSE_EGL_LIB, then libEGL_mesa.so, then libEGL.so.
// Both are resolved by name, so the plugin's own directory (it is on LD_LIBRARY_PATH) and the
// launcher's bundled Mesa are both usable. Set XCLIPSE_EGL_LIB in /sdcard/XclipseRDNA/xclipse.env
// to force a specific one.

#define _GNU_SOURCE
#include <dlfcn.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __ANDROID__
#include <android/log.h>
#define LOG_TAG "XclipseRDNA/glx"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)
#else
// Allows a plain `cc -shared -fPIC glxshim.c -ldl` syntax/symbol check off-device.
#define LOGI(...) do { fprintf(stderr, "[I] " __VA_ARGS__); fputc('\n', stderr); } while (0)
#define LOGW(...) do { fprintf(stderr, "[W] " __VA_ARGS__); fputc('\n', stderr); } while (0)
#endif

// eglGetProcAddress / glXGetProcAddress both return "some function pointer"; the exact type is
// intentionally opaque here, the caller casts.
typedef void (*XclipseProc)(void);
typedef XclipseProc (*PFNEGLGETPROCADDRESSPROC)(const char *procname);

static pthread_once_t g_once = PTHREAD_ONCE_INIT;
static void *g_egl_lib;
static PFNEGLGETPROCADDRESSPROC g_egl_get_proc_address;
static const char *g_egl_name = "(none)";

__attribute__((constructor)) static void glxshim_init(void) {
    LOGI("glxshim loaded (GLX entry points are forwarded to EGL)");
}

static void *open_egl(const char *name) {
    void *handle = dlopen(name, RTLD_NOW | RTLD_GLOBAL);
    if (handle) {
        LOGI("EGL library: %s", name);
    } else {
        const char *err = dlerror();
        LOGW("cannot open %s: %s", name, err ? err : "unknown error");
    }
    return handle;
}

static void load_egl(void) {
    const char *forced = getenv("XCLIPSE_EGL_LIB");
    if (forced && *forced) {
        g_egl_lib = open_egl(forced);
        if (g_egl_lib) g_egl_name = forced;
    }
    if (!g_egl_lib) {
        g_egl_lib = open_egl("libEGL_mesa.so");
        if (g_egl_lib) g_egl_name = "libEGL_mesa.so";
    }
    if (!g_egl_lib) {
        g_egl_lib = open_egl("libEGL.so");
        if (g_egl_lib) g_egl_name = "libEGL.so";
    }
    if (!g_egl_lib) {
        LOGW("no EGL library found; GL functions can only resolve via dlsym(RTLD_DEFAULT)");
        return;
    }
    g_egl_get_proc_address = (PFNEGLGETPROCADDRESSPROC)dlsym(g_egl_lib, "eglGetProcAddress");
    if (!g_egl_get_proc_address) {
        LOGW("%s does not export eglGetProcAddress; falling back to dlsym()", g_egl_name);
    }
}

static void *resolve(const char *name) {
    pthread_once(&g_once, load_egl);
    if (!name || !*name) return NULL;

    if (g_egl_get_proc_address) {
        void *proc = (void *)g_egl_get_proc_address(name);
        if (proc) return proc;
    }
    if (g_egl_lib) {
        void *proc = dlsym(g_egl_lib, name);
        if (proc) return proc;
    }
    return dlsym(RTLD_DEFAULT, name);
}

// ---- GLX resolver API used by LWJGL -------------------------------------------------------

XclipseProc glXGetProcAddressARB(const unsigned char *procName) {
    return (XclipseProc)resolve((const char *)procName);
}

XclipseProc glXGetProcAddress(const unsigned char *procName) {
    return (XclipseProc)resolve((const char *)procName);
}

// ---- "There is no GLX context here" stubs ------------------------------------------------
// Context management on Android is EGL (the launcher owns it), so these report GLX as
// unavailable instead of handing out garbage pointers. They exist only so a GLX-based code path
// fails cleanly rather than aborting on a missing symbol.

int glXQueryVersion(void *display, int *major, int *minor) {
    (void)display;
    if (major) *major = 0;
    if (minor) *minor = 0;
    return 0;  // False: no GLX on this device
}

void *glXGetCurrentContext(void) {
    return NULL;
}

void *glXGetCurrentDisplay(void) {
    return NULL;
}

unsigned long glXGetCurrentDrawable(void) {
    return 0;
}

int glXMakeCurrent(void *display, unsigned long drawable, void *context) {
    (void)display;
    (void)drawable;
    (void)context;
    return 0;  // False: context switching is EGL's job
}
