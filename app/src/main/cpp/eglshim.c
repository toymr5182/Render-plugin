// libEGL_xclipse.so
// EGL front library for the plugin, following the architecture working launcher plugins use
// (MobileGL/MobileGlues declares one plugin-owned library as BOTH the renderer GL and EGL path).
//
// Why: when the plugin let the launcher dlopen Mesa's libEGL_mesa.so directly, Zink could not bring
// up a context on this device ("MESA-LOADER: failed to retrieve device information") and the
// launcher's EGL bridge then parsed glGetString(GL_VERSION) == NULL with strtol, crashing with
// SIGSEGV in libc. By owning the EGL side we (a) load the real EGL ourselves by name, (b) hand out
// guarded glGetString/glGetStringi pointers from eglGetProcAddress, and (c) never return NULL from
// eglQueryString for the version strings the launcher parses.
//
// Every EGL entry point below is a thin forwarder to the real implementation, which is dlopen()ed
// by name: $XCLIPSE_EGL_LIB, then libEGL_mesa.so, then libEGL.so. Mesa's library is opened with
// RTLD_LOCAL so its symbols do not join the global namespace and shadow this library.

#define _GNU_SOURCE
#include <dlfcn.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <EGL/egl.h>

#ifdef __ANDROID__
#include <android/log.h>
#define LOG_TAG "XclipseRDNA/egl"
#define LOGI(...) do { \
    __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__); \
    fprintf(stderr, "XclipseRDNA/egl: " __VA_ARGS__); fputc('\n', stderr); fflush(stderr); \
} while (0)
#define LOGW(...) do { \
    __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__); \
    fprintf(stderr, "XclipseRDNA/egl: " __VA_ARGS__); fputc('\n', stderr); fflush(stderr); \
} while (0)
#else
#define LOGI(...) do { fprintf(stderr, "[I] " __VA_ARGS__); fputc('\n', stderr); } while (0)
#define LOGW(...) do { fprintf(stderr, "[W] " __VA_ARGS__); fputc('\n', stderr); } while (0)
#endif

#define GL_VENDOR                   0x1F00
#define GL_RENDERER                 0x1F01
#define GL_VERSION                  0x1F02
#define GL_EXTENSIONS               0x1F03
#define GL_SHADING_LANGUAGE_VERSION 0x8B8C

typedef const unsigned char *(*PFNGLGETSTRINGPROC)(unsigned int name);
typedef const unsigned char *(*PFNGLGETSTRINGIPROC)(unsigned int index, unsigned int name);

static pthread_once_t g_once = PTHREAD_ONCE_INIT;
static void *g_real;                       // handle of the real EGL implementation
static const char *g_real_name = "(none)";

static void load_real(void);

__attribute__((constructor)) static void eglshim_init(void) {
    LOGI("egl front loaded");
    pthread_once(&g_once, load_real);
}

static void load_real(void) {
    const char *forced = getenv("XCLIPSE_EGL_LIB");
    const char *candidates[4];
    int n = 0;
    if (forced && *forced) candidates[n++] = forced;
    candidates[n++] = "libEGL_mesa.so";
    candidates[n++] = "libEGL.so";
    candidates[n] = NULL;

    for (int i = 0; candidates[i]; i++) {
        // RTLD_LOCAL: keep the real EGL's symbols out of the global namespace.
        g_real = dlopen(candidates[i], RTLD_NOW | RTLD_LOCAL);
        if (g_real) {
            g_real_name = candidates[i];
            LOGI("real EGL library: %s", candidates[i]);
            return;
        }
        const char *err = dlerror();
        LOGW("cannot open %s: %s", candidates[i], err ? err : "unknown error");
    }
    LOGW("no real EGL library found; EGL entry points will fail");
}

static void *sym(const char *name) {
    pthread_once(&g_once, load_real);
    if (!g_real) return NULL;
    return dlsym(g_real, name);
}

// ---- guarded GL entry points handed out through eglGetProcAddress --------------------------
// The launcher parses the GL version right after binding ("EGLBridge: Binding to OpenGL" followed
// by strtol(glGetString(GL_VERSION))). While no context is current the real implementation returns
// NULL, which crashes that parse. Return an inert placeholder and log it instead.

static PFNGLGETSTRINGPROC g_real_get_string;
static PFNGLGETSTRINGIPROC g_real_get_string_i;

typedef __eglMustCastToProperFunctionPointerType (*PFNEGLGETPROCADDRESSPROC_LOCAL)(const char *procname);

static PFNEGLGETPROCADDRESSPROC_LOCAL real_egl_get_proc_address;

static void load_gpa(void) {
    if (!real_egl_get_proc_address) {
        real_egl_get_proc_address = (PFNEGLGETPROCADDRESSPROC_LOCAL)sym("eglGetProcAddress");
    }
}

static const unsigned char *shim_glGetString(unsigned int name) {
    load_gpa();
    if (!g_real_get_string && real_egl_get_proc_address) {
        g_real_get_string = (PFNGLGETSTRINGPROC)real_egl_get_proc_address("glGetString");
    }
    const unsigned char *value = g_real_get_string ? g_real_get_string(name) : NULL;
    if (value) return value;

    switch (name) {
        case GL_VERSION:
            LOGW("glGetString(GL_VERSION) returned NULL (no current context); returning a "
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
    load_gpa();
    if (!g_real_get_string_i && real_egl_get_proc_address) {
        g_real_get_string_i = (PFNGLGETSTRINGIPROC)real_egl_get_proc_address("glGetStringi");
    }
    const unsigned char *value = g_real_get_string_i ? g_real_get_string_i(index, name) : NULL;
    if (value || name != GL_EXTENSIONS) return value;
    return (const unsigned char *)"";
}

// ---- EGL API ------------------------------------------------------------------------------

__eglMustCastToProperFunctionPointerType eglGetProcAddress(const char *procname) {
    load_gpa();
    if (!procname) return NULL;
    if (strcmp(procname, "glGetString") == 0) {
        return (__eglMustCastToProperFunctionPointerType)shim_glGetString;
    }
    if (strcmp(procname, "glGetStringi") == 0) {
        return (__eglMustCastToProperFunctionPointerType)shim_glGetStringi;
    }
    if (real_egl_get_proc_address) {
        __eglMustCastToProperFunctionPointerType proc =
            (__eglMustCastToProperFunctionPointerType)real_egl_get_proc_address(procname);
        if (proc) return proc;
    }
    return (__eglMustCastToProperFunctionPointerType)sym(procname);
}

EGLDisplay eglGetDisplay(EGLNativeDisplayType display_id) {
    typedef EGLDisplay (*F)(EGLNativeDisplayType);
    F f = (F)sym("eglGetDisplay");
    return f ? f(display_id) : EGL_NO_DISPLAY;
}

EGLDisplay eglGetPlatformDisplay(EGLenum platform, void *native_display, const EGLAttrib *attrib_list) {
    typedef EGLDisplay (*F)(EGLenum, void *, const EGLAttrib *);
    F f = (F)sym("eglGetPlatformDisplay");
    if (f) return f(platform, native_display, attrib_list);
    // Fall back to the EXT variant, which the launcher also probes for.
    typedef EGLDisplay (*FE)(EGLenum, void *, const EGLint *);
    FE fe = (FE)sym("eglGetPlatformDisplayEXT");
    if (fe) {
        return fe(platform, native_display, (const EGLint *)attrib_list);
    }
    return eglGetDisplay((EGLNativeDisplayType)native_display);
}

EGLBoolean eglInitialize(EGLDisplay dpy, EGLint *major, EGLint *minor) {
    typedef EGLBoolean (*F)(EGLDisplay, EGLint *, EGLint *);
    F f = (F)sym("eglInitialize");
    EGLBoolean ok = f ? f(dpy, major, minor) : EGL_FALSE;
    LOGI("eglInitialize -> %s (%s)", ok ? "EGL_TRUE" : "EGL_FALSE", g_real_name);
    return ok;
}

EGLBoolean eglTerminate(EGLDisplay dpy) {
    typedef EGLBoolean (*F)(EGLDisplay);
    F f = (F)sym("eglTerminate");
    return f ? f(dpy) : EGL_FALSE;
}

EGLBoolean eglChooseConfig(EGLDisplay dpy, const EGLint *attrib_list, EGLConfig *configs, EGLint config_size, EGLint *num_config) {
    typedef EGLBoolean (*F)(EGLDisplay, const EGLint *, EGLConfig *, EGLint, EGLint *);
    F f = (F)sym("eglChooseConfig");
    return f ? f(dpy, attrib_list, configs, config_size, num_config) : EGL_FALSE;
}

EGLBoolean eglGetConfigs(EGLDisplay dpy, EGLConfig *configs, EGLint config_size, EGLint *num_config) {
    typedef EGLBoolean (*F)(EGLDisplay, EGLConfig *, EGLint, EGLint *);
    F f = (F)sym("eglGetConfigs");
    return f ? f(dpy, configs, config_size, num_config) : EGL_FALSE;
}

EGLBoolean eglGetConfigAttrib(EGLDisplay dpy, EGLConfig config, EGLint attribute, EGLint *value) {
    typedef EGLBoolean (*F)(EGLDisplay, EGLConfig, EGLint, EGLint *);
    F f = (F)sym("eglGetConfigAttrib");
    return f ? f(dpy, config, attribute, value) : EGL_FALSE;
}

EGLContext eglCreateContext(EGLDisplay dpy, EGLConfig config, EGLContext share_context, const EGLint *attrib_list) {
    typedef EGLContext (*F)(EGLDisplay, EGLConfig, EGLContext, const EGLint *);
    F f = (F)sym("eglCreateContext");
    EGLContext ctx = f ? f(dpy, config, share_context, attrib_list) : EGL_NO_CONTEXT;
    LOGI("eglCreateContext -> %p", (void *)ctx);
    if (ctx == EGL_NO_CONTEXT) {
        LOGW("eglCreateContext failed (eglGetError=0x%x) - the driver rejected the context",
             (unsigned)eglGetError());
    }
    return ctx;
}

EGLBoolean eglDestroyContext(EGLDisplay dpy, EGLContext ctx) {
    typedef EGLBoolean (*F)(EGLDisplay, EGLContext);
    F f = (F)sym("eglDestroyContext");
    return f ? f(dpy, ctx) : EGL_FALSE;
}

EGLSurface eglCreateWindowSurface(EGLDisplay dpy, EGLConfig config, EGLNativeWindowType win, const EGLint *attrib_list) {
    typedef EGLSurface (*F)(EGLDisplay, EGLConfig, EGLNativeWindowType, const EGLint *);
    F f = (F)sym("eglCreateWindowSurface");
    return f ? f(dpy, config, win, attrib_list) : EGL_NO_SURFACE;
}

EGLSurface eglCreatePbufferSurface(EGLDisplay dpy, EGLConfig config, const EGLint *attrib_list) {
    typedef EGLSurface (*F)(EGLDisplay, EGLConfig, const EGLint *);
    F f = (F)sym("eglCreatePbufferSurface");
    return f ? f(dpy, config, attrib_list) : EGL_NO_SURFACE;
}

EGLBoolean eglDestroySurface(EGLDisplay dpy, EGLSurface surface) {
    typedef EGLBoolean (*F)(EGLDisplay, EGLSurface);
    F f = (F)sym("eglDestroySurface");
    return f ? f(dpy, surface) : EGL_FALSE;
}

EGLBoolean eglMakeCurrent(EGLDisplay dpy, EGLSurface draw, EGLSurface read, EGLContext ctx) {
    typedef EGLBoolean (*F)(EGLDisplay, EGLSurface, EGLSurface, EGLContext);
    F f = (F)sym("eglMakeCurrent");
    EGLBoolean ok = f ? f(dpy, draw, read, ctx) : EGL_FALSE;
    LOGI("eglMakeCurrent -> %s", ok ? "EGL_TRUE" : "EGL_FALSE");
    return ok;
}

EGLBoolean eglSwapBuffers(EGLDisplay dpy, EGLSurface surface) {
    typedef EGLBoolean (*F)(EGLDisplay, EGLSurface);
    F f = (F)sym("eglSwapBuffers");
    return f ? f(dpy, surface) : EGL_FALSE;
}

EGLBoolean eglSwapInterval(EGLDisplay dpy, EGLint interval) {
    typedef EGLBoolean (*F)(EGLDisplay, EGLint);
    F f = (F)sym("eglSwapInterval");
    return f ? f(dpy, interval) : EGL_FALSE;
}

EGLBoolean eglQuerySurface(EGLDisplay dpy, EGLSurface surface, EGLint attribute, EGLint *value) {
    typedef EGLBoolean (*F)(EGLDisplay, EGLSurface, EGLint, EGLint *);
    F f = (F)sym("eglQuerySurface");
    return f ? f(dpy, surface, attribute, value) : EGL_FALSE;
}

EGLint eglGetError(void) {
    typedef EGLint (*F)(void);
    F f = (F)sym("eglGetError");
    return f ? f() : EGL_SUCCESS;
}

EGLBoolean eglBindAPI(EGLenum api) {
    typedef EGLBoolean (*F)(EGLenum);
    F f = (F)sym("eglBindAPI");
    return f ? f(api) : EGL_FALSE;
}

EGLenum eglQueryAPI(void) {
    typedef EGLenum (*F)(void);
    F f = (F)sym("eglQueryAPI");
    return f ? f() : EGL_OPENGL_ES_API;
}

EGLBoolean eglReleaseThread(void) {
    typedef EGLBoolean (*F)(void);
    F f = (F)sym("eglReleaseThread");
    return f ? f() : EGL_FALSE;
}

EGLBoolean eglWaitClient(void) {
    typedef EGLBoolean (*F)(void);
    F f = (F)sym("eglWaitClient");
    return f ? f() : EGL_FALSE;
}

EGLBoolean eglWaitGL(void) {
    typedef EGLBoolean (*F)(void);
    F f = (F)sym("eglWaitGL");
    return f ? f() : EGL_FALSE;
}

EGLBoolean eglWaitNative(EGLint engine) {
    typedef EGLBoolean (*F)(EGLint);
    F f = (F)sym("eglWaitNative");
    return f ? f(engine) : EGL_FALSE;
}

const char *eglQueryString(EGLDisplay dpy, EGLint name) {
    typedef const char *(*F)(EGLDisplay, EGLint);
    F f = (F)sym("eglQueryString");
    const char *value = f ? f(dpy, name) : NULL;
    if (value) return value;
    // The launcher parses EGL_VERSION / EGL_VENDOR with strtol; never hand it NULL.
    if (name == EGL_VERSION) return "1.5 Xclipse-RDNA";
    if (name == EGL_VENDOR) return "Xclipse RDNA";
    if (name == EGL_EXTENSIONS) return "";
    return NULL;
}

EGLContext eglGetCurrentContext(void) {
    typedef EGLContext (*F)(void);
    F f = (F)sym("eglGetCurrentContext");
    return f ? f() : EGL_NO_CONTEXT;
}

EGLSurface eglGetCurrentSurface(EGLint readdraw) {
    typedef EGLSurface (*F)(EGLint);
    F f = (F)sym("eglGetCurrentSurface");
    return f ? f(readdraw) : EGL_NO_SURFACE;
}

EGLDisplay eglGetCurrentDisplay(void) {
    typedef EGLDisplay (*F)(void);
    F f = (F)sym("eglGetCurrentDisplay");
    return f ? f() : EGL_NO_DISPLAY;
}
