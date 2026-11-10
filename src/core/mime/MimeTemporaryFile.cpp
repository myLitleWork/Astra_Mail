#include "MimeTemporaryFile.h"
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstdlib>
#include <cstring>
#include <cerrno>

namespace astra_mail {
namespace {
// On initialization failure only remove the newly owned directory entry.
// Nothing is written until the unlink has succeeded and st_nlink is zero.
void discard(int fd, const char* name) {
    struct stat owned{}, entry{};
    if (::fstat(fd, &owned) == 0 && ::lstat(name, &entry) == 0 &&
        owned.st_dev == entry.st_dev && owned.st_ino == entry.st_ino) {
        while (::unlink(name) != 0 && errno == EINTR) {}
    }
    ::close(fd);
}
}
MimeTemporaryFile::~MimeTemporaryFile() { if (file_) std::fclose(file_); }

bool MimeTemporaryFile::create(std::string& error) {
    error.clear();
    if (file_) { error = "MIME temporary file already initialized"; return false; }
    char name[] = "/tmp/astramail-mime-XXXXXX";
    const int fd = ::mkostemp(name, O_CLOEXEC);
    if (fd < 0) { error = "Cannot create private MIME temporary file: " + std::string(std::strerror(errno)); return false; }
    auto failed = [&](const char* stage) {
        const int code = errno;
        discard(fd, name);
        error = std::string("MIME temporary file ") + stage + ": " + std::strerror(code);
        return false;
    };
    // fchmod is explicit so even a restrictive umask cannot make the file
    // unreadable to its owner; no process-wide umask changes are needed.
    if (::fchmod(fd, S_IRUSR | S_IWUSR) != 0) return failed("permissions");
    struct stat info{};
    if (::fstat(fd, &info) != 0) return failed("stat");
    if (!S_ISREG(info.st_mode) || (info.st_mode & 0777) != 0600 || info.st_uid != ::geteuid()) {
        errno = EACCES; return failed("ownership/type");
    }
    int result;
    do { result = ::unlink(name); } while (result != 0 && errno == EINTR);
    if (result != 0) return failed("unlink before writing");
    if (::fstat(fd, &info) != 0) return failed("unlinked stat");
    if (info.st_nlink != 0) { errno = EACCES; return failed("still linked"); }
    file_ = ::fdopen(fd, "w+b");
    if (!file_) return failed("stream");
    return true;
}
}
