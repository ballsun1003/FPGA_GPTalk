#define _GNU_SOURCE
#include <alloca.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <stdarg.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *redirect_path(const char *path) {
    if (path == 0) return path;
    if (strcmp(path, "/usr/bin/gcc") == 0) return "/tools/Xilinx/Vivado/2024.2/tps/lnx64/gcc-9.3.0/bin/gcc";
    if (strcmp(path, "/usr/bin/g++") == 0) return "/tools/Xilinx/Vivado/2024.2/tps/lnx64/gcc-9.3.0/bin/g++";
    return path;
}

static char **redirect_argv(const char *new_path, const char *old_path, char *const argv[]) {
    if (new_path == old_path) return (char **)argv;
    int argc = 0;
    while (argv && argv[argc]) argc++;
    char **new_argv = alloca((argc + 1) * sizeof(char *));
    for (int i = 0; i < argc; i++) new_argv[i] = argv[i];
    new_argv[argc] = 0;
    if (argc > 0) new_argv[0] = (char *)new_path;
    return new_argv;
}

int access(const char *path, int mode) {
    static int (*real_access)(const char *, int) = 0;
    if (!real_access) real_access = dlsym(RTLD_NEXT, "access");
    return real_access(redirect_path(path), mode);
}

int faccessat(int dirfd, const char *path, int mode, int flags) {
    static int (*real_faccessat)(int, const char *, int, int) = 0;
    if (!real_faccessat) real_faccessat = dlsym(RTLD_NEXT, "faccessat");
    return real_faccessat(dirfd, redirect_path(path), mode, flags);
}

int stat(const char *path, struct stat *buf) {
    static int (*real_stat)(const char *, struct stat *) = 0;
    if (!real_stat) real_stat = dlsym(RTLD_NEXT, "stat");
    return real_stat(redirect_path(path), buf);
}

int lstat(const char *path, struct stat *buf) {
    static int (*real_lstat)(const char *, struct stat *) = 0;
    if (!real_lstat) real_lstat = dlsym(RTLD_NEXT, "lstat");
    return real_lstat(redirect_path(path), buf);
}

int stat64(const char *path, struct stat64 *buf) {
    static int (*real_stat64)(const char *, struct stat64 *) = 0;
    if (!real_stat64) real_stat64 = dlsym(RTLD_NEXT, "stat64");
    return real_stat64(redirect_path(path), buf);
}

int lstat64(const char *path, struct stat64 *buf) {
    static int (*real_lstat64)(const char *, struct stat64 *) = 0;
    if (!real_lstat64) real_lstat64 = dlsym(RTLD_NEXT, "lstat64");
    return real_lstat64(redirect_path(path), buf);
}

int __xstat(int ver, const char *path, struct stat *buf) {
    static int (*real_xstat)(int, const char *, struct stat *) = 0;
    if (!real_xstat) real_xstat = dlsym(RTLD_NEXT, "__xstat");
    return real_xstat(ver, redirect_path(path), buf);
}

int __lxstat(int ver, const char *path, struct stat *buf) {
    static int (*real_lxstat)(int, const char *, struct stat *) = 0;
    if (!real_lxstat) real_lxstat = dlsym(RTLD_NEXT, "__lxstat");
    return real_lxstat(ver, redirect_path(path), buf);
}

int __xstat64(int ver, const char *path, struct stat64 *buf) {
    static int (*real_xstat64)(int, const char *, struct stat64 *) = 0;
    if (!real_xstat64) real_xstat64 = dlsym(RTLD_NEXT, "__xstat64");
    return real_xstat64(ver, redirect_path(path), buf);
}

int __lxstat64(int ver, const char *path, struct stat64 *buf) {
    static int (*real_lxstat64)(int, const char *, struct stat64 *) = 0;
    if (!real_lxstat64) real_lxstat64 = dlsym(RTLD_NEXT, "__lxstat64");
    return real_lxstat64(ver, redirect_path(path), buf);
}

int fstatat(int dirfd, const char *path, struct stat *buf, int flags) {
    static int (*real_fstatat)(int, const char *, struct stat *, int) = 0;
    if (!real_fstatat) real_fstatat = dlsym(RTLD_NEXT, "fstatat");
    return real_fstatat(dirfd, redirect_path(path), buf, flags);
}

int fstatat64(int dirfd, const char *path, struct stat64 *buf, int flags) {
    static int (*real_fstatat64)(int, const char *, struct stat64 *, int) = 0;
    if (!real_fstatat64) real_fstatat64 = dlsym(RTLD_NEXT, "fstatat64");
    return real_fstatat64(dirfd, redirect_path(path), buf, flags);
}

int __fxstatat(int ver, int dirfd, const char *path, struct stat *buf, int flags) {
    static int (*real_fxstatat)(int, int, const char *, struct stat *, int) = 0;
    if (!real_fxstatat) real_fxstatat = dlsym(RTLD_NEXT, "__fxstatat");
    return real_fxstatat(ver, dirfd, redirect_path(path), buf, flags);
}

int __fxstatat64(int ver, int dirfd, const char *path, struct stat64 *buf, int flags) {
    static int (*real_fxstatat64)(int, int, const char *, struct stat64 *, int) = 0;
    if (!real_fxstatat64) real_fxstatat64 = dlsym(RTLD_NEXT, "__fxstatat64");
    return real_fxstatat64(ver, dirfd, redirect_path(path), buf, flags);
}

int open(const char *path, int flags, ...) {
    static int (*real_open)(const char *, int, ...) = 0;
    mode_t mode = 0;
    if (!real_open) real_open = dlsym(RTLD_NEXT, "open");
    if (flags & O_CREAT) {
        va_list ap;
        va_start(ap, flags);
        mode = va_arg(ap, mode_t);
        va_end(ap);
        return real_open(redirect_path(path), flags, mode);
    }
    return real_open(redirect_path(path), flags);
}

int open64(const char *path, int flags, ...) {
    static int (*real_open64)(const char *, int, ...) = 0;
    mode_t mode = 0;
    if (!real_open64) real_open64 = dlsym(RTLD_NEXT, "open64");
    if (flags & O_CREAT) {
        va_list ap;
        va_start(ap, flags);
        mode = va_arg(ap, mode_t);
        va_end(ap);
        return real_open64(redirect_path(path), flags, mode);
    }
    return real_open64(redirect_path(path), flags);
}

int openat(int dirfd, const char *path, int flags, ...) {
    static int (*real_openat)(int, const char *, int, ...) = 0;
    mode_t mode = 0;
    if (!real_openat) real_openat = dlsym(RTLD_NEXT, "openat");
    if (flags & O_CREAT) {
        va_list ap;
        va_start(ap, flags);
        mode = va_arg(ap, mode_t);
        va_end(ap);
        return real_openat(dirfd, redirect_path(path), flags, mode);
    }
    return real_openat(dirfd, redirect_path(path), flags);
}

int openat64(int dirfd, const char *path, int flags, ...) {
    static int (*real_openat64)(int, const char *, int, ...) = 0;
    mode_t mode = 0;
    if (!real_openat64) real_openat64 = dlsym(RTLD_NEXT, "openat64");
    if (flags & O_CREAT) {
        va_list ap;
        va_start(ap, flags);
        mode = va_arg(ap, mode_t);
        va_end(ap);
        return real_openat64(dirfd, redirect_path(path), flags, mode);
    }
    return real_openat64(dirfd, redirect_path(path), flags);
}

int execve(const char *path, char *const argv[], char *const envp[]) {
    static int (*real_execve)(const char *, char *const [], char *const []) = 0;
    const char *new_path = redirect_path(path);
    if (!real_execve) real_execve = dlsym(RTLD_NEXT, "execve");
    return real_execve(new_path, redirect_argv(new_path, path, argv), envp);
}

int execv(const char *path, char *const argv[]) {
    static int (*real_execv)(const char *, char *const []) = 0;
    const char *new_path = redirect_path(path);
    if (!real_execv) real_execv = dlsym(RTLD_NEXT, "execv");
    return real_execv(new_path, redirect_argv(new_path, path, argv));
}

int execvp(const char *path, char *const argv[]) {
    static int (*real_execvp)(const char *, char *const []) = 0;
    const char *new_path = redirect_path(path);
    if (!real_execvp) real_execvp = dlsym(RTLD_NEXT, "execvp");
    return real_execvp(new_path, redirect_argv(new_path, path, argv));
}

