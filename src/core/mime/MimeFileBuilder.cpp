#include "MimeFileBuilder.h"
#include "Utils/SmtpClient.h"
#include <ostream>
#include <streambuf>
#include <memory>
#include <cerrno>
#include <unistd.h>
#include <ctime>
#include <iostream>

namespace {
class FileSink : public std::streambuf {
public:
    explicit FileSink(FILE* file) : file_(file) {}
protected:
    std::streamsize xsputn(const char* data, std::streamsize size) override {
        std::streamsize done = 0;
        while (done < size) {
            errno = 0;
            const size_t written = std::fwrite(data + done, 1, static_cast<size_t>(size - done), file_);
            done += static_cast<std::streamsize>(written);
            if (written == 0 || std::ferror(file_)) {
                // After a stdio error the buffered position is not reliable.
                // Abort construction; the unchanged outbox can retry later.
                break;
            }
        }
        return done;
    }
    int_type overflow(int_type value) override {
        if (traits_type::eq_int_type(value, traits_type::eof())) return traits_type::not_eof(value);
        const char c = traits_type::to_char_type(value);
        return xsputn(&c, 1) == 1 ? value : traits_type::eof();
    }
    int sync() override {
        return std::fflush(file_) == 0 ? 0 : -1;
    }
private:
    FILE* file_;
};
}

static std::string rfc2822Date()
{
    std::time_t t = std::time(nullptr);
    std::tm tm{};
    gmtime_r(&t, &tm);

    char buf[64];
    std::strftime(buf, sizeof(buf),
                  "%a, %d %b %Y %H:%M:%S +0000", &tm);
    return buf;
}

static std::string genMsgId(const std::string& from)
{
    auto at = from.find('@');
    std::string dom = (at != std::string::npos) ? from.substr(at + 1) : "local";
    return std::to_string(std::time(nullptr)) + "." + std::to_string(rand()) + "@" + dom;
}

bool MimeFileBuilder::buildToStream(const EmailMessage& msg,
                                  FILE* file,
                                  std::string& err)
{
    err.clear();
    if (!file || ::fseeko(file, 0, SEEK_SET) != 0 || ::ftruncate(::fileno(file), 0) != 0)
    {
        err = "Cannot prepare MIME stream";
        return false;
    }
    std::clearerr(file);
    FileSink sink(file);
    std::ostream out(&sink);

    const std::string crlf = "\r\n";
    const std::string boundary = "----=_AstraMailBoundary_001";

    out << "Date: " << rfc2822Date() << crlf;
    out << "From: " << msg.from << crlf;
    out << "To: " << msg.to << crlf;
    out << "Subject: " << msg.subject << crlf;
    out << "Message-ID: <" << genMsgId(msg.from) << ">" << crlf;
    out << "MIME-Version: 1.0" << crlf;
    out << "Content-Type: multipart/mixed; boundary=\"" << boundary << "\"" << crlf;
    out << crlf;

    // ---- text
    out << "--" << boundary << crlf;
    out << "Content-Type: text/plain; charset=UTF-8" << crlf;
    out << "Content-Transfer-Encoding: 7bit" << crlf;
    out << crlf;
    out << msg.body << crlf;
    if (!out) { err = "Cannot write MIME file"; return false; }

    // ---- attachments (RAW, без base64!)
    for (const auto& a : msg.attachments)
    {
        out << "--" << boundary << crlf;
        out << "Content-Type: application/octet-stream; name=\"" << a.filename << "\"" << crlf;
        out << "Content-Transfer-Encoding: base64" << crlf;
        out << "Content-Disposition: attachment; filename=\"" << a.filename << "\"" << crlf;
        out << crlf;


        std::unique_ptr<FILE, decltype(&std::fclose)> f(std::fopen(a.path.c_str(), "rb"), &std::fclose);
        if (!f)
        {
            err = "Cannot open attachment: " + a.path;
            return false;
        }


        unsigned char buf[57];
        while (true)
        {
            size_t r = fread(buf, 1, sizeof(buf), f.get());
            if (r == 0) break;

            std::string chunk(reinterpret_cast<char*>(buf), r);
            out << SmtpClient::base64(chunk) << crlf;
            if (!out) { err = "Cannot write MIME file"; return false; }
        }
        const bool readOk = !ferror(f.get());
        if (!readOk) { err = "Cannot read attachment: " + a.path; return false; }
    }

    out << "--" << boundary << "--" << crlf;
    out.flush();
    if (!out || std::ferror(file)) { err = "Cannot write MIME file"; return false; }
    if (::fseeko(file, 0, SEEK_SET) != 0) { err = "Cannot rewind MIME file"; return false; }
    return true;
}
