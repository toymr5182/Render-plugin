// libxclipse_shim.so
// Loaded into the game process by FCL (DLOPEN=libxclipse_shim.so in pojavEnv).
// The constructor runs before Mesa initialises, so setenv() here changes how Zink behaves.
//
// 1. Reads /sdcard/XclipseRDNA/xclipse.env (KEY=VALUE per line) and applies it.
// 2. If XCLIPSE_MODE=radv, stages the RADV Xclipse driver (vulkan.radeon.so) into an
//    exec-capable directory and (optionally) asks libadrenotools to use it.

#define _GNU_SOURCE
#include <android/log.h>
#include <dlfcn.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define TAG "XclipseRDNA"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, TAG, __VA_ARGS__)

#define ENV_PATH "/sdcard/XclipseRDNA/xclipse.env"

static void apply_env_file(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) {
        LOGW("no %s (%s) - using manifest defaults", path, strerror(errno));
        return;
    }
    char line[PATH_MAX + 64];
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = 0;
        if (!line[0] || line[0] == '#') continue;
        char *eq = strchr(line, '=');
        if (!eq || eq == line) continue;
        *eq = 0;
        setenv(line, eq + 1, 1);
        LOGI("env %s=%s", line, eq + 1);
    }
    fclose(f);
}

static int copy_file(const char *src, const char *dst) {
    FILE *in = fopen(src, "rb");
    if (!in) return -1;
    FILE *out = fopen(dst, "wb");
    if (!out) { fclose(in); return -1; }
    char buf[1 << 16];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, in)) > 0) fwrite(buf, 1, n, out);
    fclose(in);
    fclose(out);
    chmod(dst, 0755);
    return 0;
}

#ifdef XCLIPSE_WITH_ADRENOTOOLS
#include <adrenotools/driver.h>
#endif

static void setup_radv(void) {
    const char *src_dir = getenv("XCLIPSE_RADV_DIR");
    const char *tmp = getenv("TMPDIR");  // FCL sets this to its private cache dir
    if (!src_dir || !tmp) { LOGW("radv: XCLIPSE_RADV_DIR or TMPDIR missing"); return; }

    char src[PATH_MAX], stage[PATH_MAX], dst[PATH_MAX];
    snprintf(src, sizeof src, "%s/vulkan.radeon.so", src_dir);
    snprintf(stage, sizeof stage, "%s/xclipse_radv", tmp);
    snprintf(dst, sizeof dst, "%s/vulkan.radeon.so", stage);

    // /sdcard is mounted noexec, so the driver must be copied into app-private storage first.
    mkdir(stage, 0755);
    if (copy_file(src, dst) != 0) {
        LOGW("radv: cannot copy %s (%s) - falling back to system driver", src, strerror(errno));
        return;
    }
    setenv("XCLIPSE_RADV_STAGED", dst, 1);

#ifdef XCLIPSE_WITH_ADRENOTOOLS
    // This plugin's own native dir == directory containing this shim.
    Dl_info info;
    char hook_dir[PATH_MAX] = {0};
    if (dladdr((void *)setup_radv, &info) && info.dli_fname) {
        strncpy(hook_dir, info.dli_fname, sizeof hook_dir - 1);
        char *slash = strrchr(hook_dir, '/');
        if (slash) *slash = 0;
    }
    char lib_tmp[PATH_MAX];
    snprintf(lib_tmp, sizeof lib_tmp, "%s/xclipse_libtmp", tmp);
    mkdir(lib_tmp, 0755);

    void *user_handle = NULL;
    void *vk = adrenotools_open_libvulkan(RTLD_NOW, ADRENOTOOLS_DRIVER_CUSTOM, lib_tmp, hook_dir,
                                          stage, "vulkan.radeon.so", NULL, &user_handle);
    if (!vk) {
        LOGW("radv: adrenotools_open_libvulkan failed - falling back to system driver");
        return;
    }
    // OPEN QUESTION (not solved yet): Mesa/Zink resolves Vulkan via dlopen("libvulkan.so"),
    // not via this handle. To make Zink use it we still need either an interposed dlopen()
    // that returns `vk` for "libvulkan.so", or a small patch in Mesa (zink_screen.c).
    LOGI("radv: custom driver loaded, handle=%p", vk);
#else
    LOGW("radv: built without libadrenotools (XCLIPSE_WITH_ADRENOTOOLS=OFF); driver staged at %s "
         "but NOT activated - system Vulkan driver stays in use", dst);
#endif
}

__attribute__((constructor)) static void xclipse_init(void) {
    LOGI("shim loaded");
    apply_env_file(ENV_PATH);
    const char *mode = getenv("XCLIPSE_MODE");
    if (mode && strcmp(mode, "radv") == 0) setup_radv();
}
