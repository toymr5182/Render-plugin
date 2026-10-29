// xclog.h - diagnostics for the plugin's native libraries (libglxshim.so, libEGL_xclipse.so).
//
// Three sinks, because each one is broken somewhere:
//   * logcat  - needs adb, but it is the only place that keeps a native crash backtrace.
//   * stderr  - the launcher's own log viewer shows the game process' stdout/stderr.
//   * a file  - the launcher's "share log" button crashes in its own FileProvider config
//               (IllegalArgumentException: Failed to find configured root that contains
//               .../latest_game.log), so a plain file the user opens with a file manager is the
//               only reliable way to get diagnostics off the device.
//
// The file lives in the launcher's external files dir: the game process runs as the launcher's uid,
// so that directory is writable without any permission. The launcher package is discovered by
// scanning /proc/self/maps for its data dir (Mesa and the game files are mapped from there), which
// keeps this working for every Poxjav/FCL/Zalith-style launcher without hard-coding a package name.
// $XCLIPSE_LOG_FILE overrides the path.

#ifndef XCLIPSE_XCLOG_H
#define XCLIPSE_XCLOG_H

#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifdef __ANDROID__
#include <android/log.h>
#endif

static pthread_once_t xc_log_once = PTHREAD_ONCE_INIT;
static int xc_log_fd = -1;
static char xc_log_file[512];

// Extract the owning package name from the first mapped path under /data/user/0/<pkg>/ or
// /data/data/<pkg>/. Empty string when nothing matches (e.g. a process with no data dir mapped).
static void xc_detect_package(char *out, size_t out_size) {
    static const char *roots[2] = { "/data/user/0/", "/data/data/" };
    FILE *maps = fopen("/proc/self/maps", "r");
    out[0] = '\0';
    if (!maps) return;

    char line[1024];
    while (fgets(line, sizeof line, maps)) {
        for (int i = 0; i < 2; i++) {
            char *p = strstr(line, roots[i]);
            if (!p) continue;
            p += strlen(roots[i]);
            size_t n = strcspn(p, "/\n");
            if (n == 0 || n >= out_size) continue;
            memcpy(out, p, n);
            out[n] = '\0';
            fclose(maps);
            return;
        }
    }
    fclose(maps);
}

static void xc_log_open(void) {
    char candidates[4][512];
    int n = 0;

    const char *forced = getenv("XCLIPSE_LOG_FILE");
    if (forced && *forced) snprintf(candidates[n++], sizeof candidates[0], "%s", forced);

    char pkg[128] = "";
    xc_detect_package(pkg, sizeof pkg);
    if (pkg[0]) {
        snprintf(candidates[n++], sizeof candidates[0],
                 "/storage/self/primary/Android/data/%s/files/xclipse_rdna.log", pkg);
        snprintf(candidates[n++], sizeof candidates[0],
                 "/storage/self/primary/Android/data/%s/xclipse_rdna.log", pkg);
        snprintf(candidates[n++], sizeof candidates[0],
                 "/data/user/0/%s/files/xclipse_rdna.log", pkg);
    }

    for (int i = 0; i < n; i++) {
        int fd = open(candidates[i], O_WRONLY | O_CREAT | O_TRUNC | O_APPEND | O_CLOEXEC, 0644);
        if (fd < 0) continue;
        xc_log_fd = fd;
        snprintf(xc_log_file, sizeof xc_log_file, "%s", candidates[i]);
        char head[600];
        int len = snprintf(head, sizeof head, "[xclipse] log file: %s\n", xc_log_file);
        if (len > 0) {
            ssize_t ignored = write(xc_log_fd, head, (size_t)len);
            (void)ignored;
        }
        return;
    }
}

// prio: 0 = info, 1 = warning.
static void xc_log(int prio, const char *tag, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));

static void xc_log(int prio, const char *tag, const char *fmt, ...) {
    char body[1024];

    va_list ap;
    va_start(ap, fmt);
    vsnprintf(body, sizeof body, fmt, ap);
    va_end(ap);

#ifdef __ANDROID__
    __android_log_write(prio ? ANDROID_LOG_WARN : ANDROID_LOG_INFO, tag, body);
#else
    (void)prio;
#endif

    fprintf(stderr, "%s: %s\n", tag, body);
    fflush(stderr);

    pthread_once(&xc_log_once, xc_log_open);
    if (xc_log_fd >= 0) {
        char line[1200];
        int len = snprintf(line, sizeof line, "%s: %s\n", tag, body);
        if (len > 0) {
            size_t len2 = (size_t)len < sizeof line ? (size_t)len : sizeof line - 1;
            ssize_t ignored = write(xc_log_fd, line, len2);  // O_APPEND: one call per line
            (void)ignored;
        }
    }
}

#endif  // XCLIPSE_XCLOG_H
