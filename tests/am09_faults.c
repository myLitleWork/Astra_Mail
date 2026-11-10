// Faults affect only disposable synthetic MIME descriptors in this test process.
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

static int mime_fd(int fd) {
    char path[64], target[512];
    snprintf(path,sizeof(path),"/proc/self/fd/%d",fd);
    ssize_t length=readlink(path,target,sizeof(target)-1);
    if(length<0) return 0;
    target[length]=0;
    return !strncmp(target,"/tmp/astramail-mime-",20);
}
int mkostemp(char* name,int flags) {
    int (*real_call)(char*,int)=dlsym(RTLD_NEXT,"mkostemp");
    if(getenv("AM09_FAIL_CREATE") && strstr(name,"/tmp/astramail-mime-")) {errno=ENOSPC;return -1;}
    return real_call(name,flags);
}
int fchmod(int fd,mode_t mode) {
    int (*real_call)(int,mode_t)=dlsym(RTLD_NEXT,"fchmod");
    if(getenv("AM09_FAIL_MODE") && mime_fd(fd)) {errno=EPERM;return -1;}
    return real_call(fd,mode);
}
int unlink(const char* name) {
    int (*real_call)(const char*)=dlsym(RTLD_NEXT,"unlink");
    if(getenv("AM09_FAIL_UNLINK") && !strncmp(name,"/tmp/astramail-mime-",20)) {errno=EACCES;return -1;}
    return real_call(name);
}
FILE* fdopen(int fd,const char* mode) {
    FILE* (*real_call)(int,const char*)=dlsym(RTLD_NEXT,"fdopen");
    if(getenv("AM09_FAIL_FDOPEN") && mime_fd(fd)) {errno=ENOMEM;return NULL;}
    return real_call(fd,mode);
}
size_t fwrite(const void* data,size_t size,size_t count,FILE* file) {
    size_t (*real_call)(const void*,size_t,size_t,FILE*)=dlsym(RTLD_NEXT,"fwrite");
    if(mime_fd(fileno(file))) {
        const char* fail=getenv("AM09_FAIL_WRITE");
        if(fail) {errno=!strcmp(fail,"EINTR")?EINTR:ENOSPC;return 0;}
        if(getenv("AM09_SHORT_WRITE") && count>7) count=7;
    }
    return real_call(data,size,count,file);
}
int fflush(FILE* file) {
    int (*real_call)(FILE*)=dlsym(RTLD_NEXT,"fflush");
    if(file && getenv("AM09_FAIL_FLUSH") && mime_fd(fileno(file))) {errno=EIO;return EOF;}
    return real_call(file);
}
int fseeko(FILE* file,off_t offset,int whence) {
    static unsigned calls;
    int (*real_call)(FILE*,off_t,int)=dlsym(RTLD_NEXT,"fseeko");
    const char* fail=getenv("AM09_FAIL_SEEK_AT");
    if(fail && mime_fd(fileno(file)) && ++calls==(unsigned)atoi(fail)) {errno=EIO;return -1;}
    return real_call(file,offset,whence);
}
FILE* fopen(const char* path,const char* mode) {
    FILE* (*real_call)(const char*,const char*)=dlsym(RTLD_NEXT,"fopen");
    const char* fail=getenv("AM09_FAIL_ATTACHMENT");
    if(fail && !strcmp(path,fail)) {errno=ENOENT;return NULL;}
    return real_call(path,mode);
}
