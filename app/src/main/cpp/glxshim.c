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
// Launcher log viewers show the game process' stdout/stderr, not logcat, so write to both: logcat
// for `adb logcat` and stderr so the lines appear in the launcher's own log / crash report.
#define LOGI(...) do { \
    __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__); \
    fprintf(stderr, "XclipseRDNA/glx: " __VA_ARGS__); fputc('\n', stderr); fflush(stderr); \
} while (0)
#define LOGW(...) do { \
    __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__); \
    fprintf(stderr, "XclipseRDNA/glx: " __VA_ARGS__); fputc('\n', stderr); fflush(stderr); \
} while (0)
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

static void load_egl(void);

__attribute__((constructor)) static void glxshim_init(void) {
    LOGI("glxshim loaded (GLX entry points are forwarded to EGL)");
    // Bind EGL eagerly so the log says which library is used before the launcher touches GL.
    pthread_once(&g_once, load_egl);
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
    if (forced && *forced) g_egl_lib = open_egl(forced);
    if (!g_egl_lib) g_egl_lib = open_egl("libEGL_mesa.so");
    if (!g_egl_lib) g_egl_lib = open_egl("libEGL.so");
    if (!g_egl_lib) {
        LOGW("no EGL library found; GL functions can only resolve via dlsym(RTLD_DEFAULT)");
        return;
    }
    g_egl_get_proc_address = (PFNEGLGETPROCADDRESSPROC)dlsym(g_egl_lib, "eglGetProcAddress");
    if (!g_egl_get_proc_address) {
        LOGW("the loaded EGL library does not export eglGetProcAddress; falling back to dlsym()");
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

// ---- glGetString guard ---------------------------------------------------------------------
// Launchers and LWJGL parse the version string straight away ("EGLBridge: Binding to OpenGL" then
// strtol(glGetString(GL_VERSION))). While no context is current that returns NULL and the process
// dies with SIGSEGV inside libc's strtol. Hand back an inert placeholder instead of NULL so the
// launch continues to real context creation and reports its own error, and log the moment it
// happens.

#define GL_VENDOR                   0x1F00
#define GL_RENDERER                 0x1F01
#define GL_VERSION                  0x1F02
#define GL_EXTENSIONS               0x1F03
#define GL_SHADING_LANGUAGE_VERSION 0x8B8C

typedef const unsigned char *(*PFNGLGETSTRINGPROC)(unsigned int name);
typedef const unsigned char *(*PFNGLGETSTRINGIPROC)(unsigned int index, unsigned int name);

static PFNGLGETSTRINGPROC g_real_get_string;
static PFNGLGETSTRINGIPROC g_real_get_string_i;

static const unsigned char *shim_glGetString(unsigned int name) {
    if (!g_real_get_string) g_real_get_string = (PFNGLGETSTRINGPROC)resolve("glGetString");
    const unsigned char *value = g_real_get_string ? g_real_get_string(name) : NULL;
    if (value) return value;

    switch (name) {
        case GL_VERSION:
            LOGW("glGetString(GL_VERSION) returned NULL (no context current yet); returning a "
                 "placeholder so the launcher's version parse cannot crash");
            return (const unsigned char *)"0.0 Xclipse-RDNA-no-context";
        case GL_SHADING_LANGUAGE_VERSION:
            return (const unsigned char *)"0.00";
        case GL_RENDERER:
        case GL_VENDOR:
            return (const unsigned char *)"Xclipse RDNA";
        case GL_EXTENSIONS:
            return (const unsigned char *)"";
        default:
            return NULL;
    }
}

static const unsigned char *shim_glGetStringi(unsigned int index, unsigned int name) {
    if (!g_real_get_string_i) g_real_get_string_i = (PFNGLGETSTRINGIPROC)resolve("glGetStringi");
    const unsigned char *value = g_real_get_string_i ? g_real_get_string_i(index, name) : NULL;
    if (value || name != GL_EXTENSIONS) return value;
    return (const unsigned char *)"";
}

// ---- GLX resolver API used by LWJGL -------------------------------------------------------

static void *resolve_gl(const char *name) {
    if (name) {
        if (strcmp(name, "glGetString") == 0) return (void *)shim_glGetString;
        if (strcmp(name, "glGetStringi") == 0) return (void *)shim_glGetStringi;
    }
    return resolve(name);
}

XclipseProc glXGetProcAddressARB(const unsigned char *procName) {
    return (XclipseProc)resolve_gl((const char *)procName);
}

XclipseProc glXGetProcAddress(const unsigned char *procName) {
    return (XclipseProc)resolve_gl((const char *)procName);
}

// ---- "There is no GLX context here" stubs ------------------------------------------------
// Context management on Android is EGL (the launcher owns it), so these report GLX as unavailable
// instead of handing out garbage pointers. They exist only so a GLX-based code path fails cleanly
// rather than aborting on a missing symbol.

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
