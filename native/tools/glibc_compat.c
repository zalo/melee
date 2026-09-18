/* Linked into the glibc 2.30 hybrid toolchain's libstdc++.a (build/glibc230/build.sh). The gcc 12
 * libstdc++.a and prebuilt Dawn were built against glibc 2.37 headers and reference symbols older CFW glibcs lack (ArkOS 2.30,
 * CrossMix 2.33). Hidden visibility keeps these private to the executable, so on newer systems the
 * device's own libc and libstdc++.so (loaded by libmali) never bind to them. */
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/syscall.h>
#include <unistd.h>

/* glibc 2.32: libstdc++ reads this to skip atomic refcounting before the first thread. Always 0
 * means "maybe multi-threaded": the atomic path is always taken, which is correct and only slower. */
__attribute__((visibility("hidden"))) char __libc_single_threaded = 0;

static void compat_fill_random(void *buffer, size_t size) {
    unsigned char *out = buffer;
    while (size > 0) {
        long got = syscall(SYS_getrandom, out, size, 0);
        if (got > 0) {
            out += got;
            size -= (size_t)got;
        } else if (got < 0 && errno == EINTR) {
            continue;
        } else {
            break;
        }
    }
    if (size == 0)
        return;
    int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    while (fd >= 0 && size > 0) {
        ssize_t got = read(fd, out, size);
        if (got > 0) {
            out += got;
            size -= (size_t)got;
        } else if (got < 0 && errno == EINTR) {
            continue;
        } else {
            break;
        }
    }
    if (fd >= 0)
        close(fd);
    if (size != 0)
        abort(); /* glibc's arc4random also aborts when no entropy source works. */
}

/* glibc 2.36: std::random_device's default token. */
__attribute__((visibility("hidden"))) uint32_t arc4random(void) {
    uint32_t value;
    compat_fill_random(&value, sizeof value);
    return value;
}
