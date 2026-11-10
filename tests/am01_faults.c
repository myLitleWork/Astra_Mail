// Fault injection only in the disposable test process, using real loopback sockets.
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <termios.h>

int tcsetattr(int fd, int action, const struct termios* value) {
    static int calls;
    int (*real_set)(int, int, const struct termios*) = dlsym(RTLD_NEXT, "tcsetattr");
    if (getenv("AM06_FAIL_TTY_SET") && ++calls == 1) { errno = EIO; return -1; }
    return real_set(fd, action, value);
}

int getaddrinfo(const char* node, const char* service, const struct addrinfo* hints, struct addrinfo** result) {
    int (*real_resolve)(const char*, const char*, const struct addrinfo*, struct addrinfo**) = dlsym(RTLD_NEXT, "getaddrinfo");
    if (getenv("AM03_SLOW_DNS") && node && !strcmp(node, "am03-slow.invalid")) {
        sleep(5); return EAI_AGAIN;
    }
    return real_resolve(node, service, hints, result);
}

ssize_t send(int fd, const void* data, size_t size, int flags) {
    static int interrupted;
    ssize_t (*real_send)(int, const void*, size_t, int) = dlsym(RTLD_NEXT, "send");
    if (getenv("AM01_SHORT_IO")) {
        if (!interrupted++) { errno = EINTR; return -1; }
        if (size > 7) size = 7;
    }
    return real_send(fd, data, size, flags);
}
ssize_t recv(int fd, void* data, size_t size, int flags) {
    static int interrupted;
    ssize_t (*real_recv)(int, void*, size_t, int) = dlsym(RTLD_NEXT, "recv");
    if (getenv("AM01_SHORT_IO")) {
        if (!interrupted++) { errno = EINTR; return -1; }
        if (size > 13) size = 13;
    }
    return real_recv(fd, data, size, flags);
}
int rename(const char* old_path, const char* new_path) {
    static int receipt_writes;
    static int secret_writes;
    int (*real_rename)(const char*, const char*) = dlsym(RTLD_NEXT, "rename");
    const char* fail_at = getenv("AM01_FAIL_RECEIPT_RENAME_AT");
    if (fail_at && strstr(new_path, "/.astramail-delivery-state") &&
        ++receipt_writes == atoi(fail_at)) { errno = EIO; return -1; }
    fail_at = getenv("AM02_FAIL_SECRET_RENAME_AT");
    if (fail_at && strstr(new_path, "/accounts/") &&
        ++secret_writes == atoi(fail_at)) { errno = EIO; return -1; }
    return real_rename(old_path, new_path);
}
