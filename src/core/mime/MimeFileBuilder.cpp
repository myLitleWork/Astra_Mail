#include "MimeFileBuilder.h"
#include "Utils/SmtpClient.h"
#include <fstream>
#include <ctime>
#include <iostream>

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

bool MimeFileBuilder::buildToFile(const EmailMessage& msg,
                                  const std::string& outPath,
                                  std::string& err)
{
    std::ofstream out(outPath, std::ios::binary);
    if (!out)
    {
        err = "Cannot create MIME temp file";
        return false;
    }

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

    // ---- attachments (RAW, без base64!)
    for (const auto& a : msg.attachments)
    {
        out << "--" << boundary << crlf;
        out << "Content-Type: application/octet-stream; name=\"" << a.filename << "\"" << crlf;
        out << "Content-Transfer-Encoding: base64" << crlf;
        out << "Content-Disposition: attachment; filename=\"" << a.filename << "\"" << crlf;
        out << crlf;


        FILE* f = fopen(a.path.c_str(), "rb");
        if (!f)
        {
            out << "[MIME] cannot open attachment: " << a.path << "\n";
            continue;
        }


        unsigned char buf[57];
        while (true)
        {
            size_t r = fread(buf, 1, sizeof(buf), f);
            if (r == 0) break;

            std::string chunk(reinterpret_cast<char*>(buf), r);
            out << SmtpClient::base64(chunk) << crlf;
        }
        fclose(f);
    }

    out << "--" << boundary << "--" << crlf;
    return true;
}
