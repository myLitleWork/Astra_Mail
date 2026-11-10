#pragma once
#include <cstdio>
#include <string>

namespace astra_mail {
// Owns an exclusive 0600 file with no directory entry. Callers borrow the FILE
// for MIME construction, SMTP and APPEND; they never reopen a temporary path.
class MimeTemporaryFile {
public:
    MimeTemporaryFile() = default;
    ~MimeTemporaryFile();
    MimeTemporaryFile(const MimeTemporaryFile&) = delete;
    MimeTemporaryFile& operator=(const MimeTemporaryFile&) = delete;
    bool create(std::string& error);
    FILE* stream() const { return file_; }
private:
    FILE* file_ = nullptr;
};
}
