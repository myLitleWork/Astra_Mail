#include "SmtpClient.h"
#include <netdb.h>
#include <unistd.h>
#include <sys/socket.h>
#include <cstdio>
#include <ios>
#include <ctime>
#include <sstream>
#include <utility>
#include "Utils/Utils.h"



SmtpClient::SmtpClient(std::string  host, int port)
        : host_(std::move(host)), port_(port) {}

void SmtpClient::setCredentials(const std::string& user, const std::string& password)
{
    user_ = user;
    pass_ = password;
}
bool SmtpClient::openSession(Session& ses, std::string& error)
{
    ses.sock = socketConnect(error);
    if (ses.sock < 0)
        return false;

    std::string reply;

    recvResponse(ses.sock, reply);              // banner
    sendLine(ses.sock, "EHLO astra");
    recvResponse(ses.sock, reply);

    sendLine(ses.sock, "AUTH LOGIN");
    recvResponse(ses.sock, reply);

    sendLine(ses.sock, base64(user_));
    recvResponse(ses.sock, reply);

    sendLine(ses.sock, base64(pass_));
    recvResponse(ses.sock, reply);

    return true;
}

void SmtpClient::closeSession(Session& ses)
{
    if (ses.sock >= 0)
    {
        sendLine(ses.sock, "QUIT");
        close(ses.sock);
        ses.sock = -1;
    }
}


// ---------------- BASE64 ---------------------

std::string SmtpClient::base64(const std::string& in)
{
    static const char* t =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    std::string out;
    int val = 0, valb = -6;

    for (unsigned char c : in)
    {
        val = (val << 8) + c;
        valb += 8;
        while (valb >= 0)
        {
            out.push_back(t[(val >> valb) & 0x3F]);
            valb -= 6;
        }
    }
    if (valb > -6)
        out.push_back(t[((val << 8) >> (valb + 8)) & 0x3F]);

    while (out.size() % 4)
        out.push_back('=');

    return out;
}

// ---------------- SOCKET ---------------------

int SmtpClient::socketConnect(std::string& error)
{
    addrinfo hints{}, *res;

    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(host_.c_str(), std::to_string(port_).c_str(), &hints, &res) != 0)
    {
        error = "DNS resolution failed";
        return -1;
    }

    int s = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (s < 0)
    {
        error = "Cannot create socket";
        freeaddrinfo(res);
        return -1;
    }

    if (connect(s, res->ai_addr, res->ai_addrlen) != 0)
    {
        error = "Connect failed";
        close(s);
        freeaddrinfo(res);
        return -1;
    }

    freeaddrinfo(res);
    return s;
}

bool SmtpClient::sendLine(int s, const std::string& text)
{
    std::string t = text + "\r\n";
    return send(s, t.c_str(), t.size(), 0) == (ssize_t)t.size();
}

bool SmtpClient::recvResponse(int s, std::string& reply)
{
    char buf[2048];
    ssize_t r = recv(s, buf, sizeof(buf)-1, 0);

    if (r <= 0) return false;

    buf[r] = 0;
    reply = buf;
    return true;
}

// ---------------- SEND MAIL ---------------------


bool SmtpClient::sendMail(const EmailMessage& msg, std::string& error)
{
    Session ses;
    if (!openSession(ses, error))
        return false;

    bool ok = sendMail(ses, msg, error);
    closeSession(ses);
    return ok;
}



std::string currentRfc2822Date()
{
    std::time_t t = std::time(nullptr);
    std::tm tm{};
    gmtime_r(&t, &tm);

    char buf[64];
    std::strftime(buf, sizeof(buf),
                  "%a, %d %b %Y %H:%M:%S +0000", &tm);
    return buf;
}

std::string generateMessageId(const std::string& from)
{
    auto at = from.find('@');
    std::string domain = (at != std::string::npos)
                         ? from.substr(at + 1)
                         : "localhost";

    std::ostringstream ss;
    ss << std::hex << std::time(nullptr)
       << "." << astra_mail::rnd()
       << "@" << domain;

    return ss.str();
}




std::string SmtpClient::buildRawMime(const EmailMessage& msg)
{
    const std::string crlf = "\r\n";

    std::ostringstream out;

    out << "Date: " << currentRfc2822Date() << crlf;
    out << "From: " << msg.from << crlf;
    out << "To: " << msg.to << crlf;
    out << "Subject: " << msg.subject << crlf;
    out << "Message-ID: <" << generateMessageId(msg.from) << ">" << crlf;

    // Для нормального отображения в клиентах
    out << "MIME-Version: 1.0" << crlf;
    out << "Content-Type: text/plain; charset=UTF-8" << crlf;
    out << "Content-Transfer-Encoding: 7bit" << crlf;
    out << crlf;

    out << msg.body;   // БЕЗ \r\n В КОНЦЕ
    return out.str();

}






bool SmtpClient::sendMailFromFile(
        const std::string& mimeFile,
        const std::string& rcpt,
        std::string& error)
{
    int s = socketConnect(error);
    if (s < 0)
        return false;

    std::string reply;
    recvResponse(s, reply);               // banner
    sendLine(s, "EHLO astra");
    recvResponse(s, reply);

    sendLine(s, "AUTH LOGIN");
    recvResponse(s, reply);
    sendLine(s, base64(user_));
    recvResponse(s, reply);
    sendLine(s, base64(pass_));
    recvResponse(s, reply);

    // MAIL FROM / RCPT TO берём ИЗ MIME
    // (серверу этого достаточно, Thunderbird делает так же)
    sendLine(s, "MAIL FROM:<" + user_ + ">");
    recvResponse(s, reply);

    sendLine(s, "RCPT TO:<" + rcpt + ">");
    recvResponse(s, reply);


    sendLine(s, "DATA");
    recvResponse(s, reply);

    // === ТЕЛО: читаем MIME-файл и шлём ===
    FILE* f = fopen(mimeFile.c_str(), "rb");
    if (!f)
    {
        error = "Cannot open MIME file";
        close(s);
        return false;
    }

    char buf[2048];
    while (!feof(f))
    {
        size_t r = fread(buf, 1, sizeof(buf), f);
        if (r > 0)
            send(s, buf, r, 0);
    }
    fclose(f);

    // конец DATA
    sendLine(s, ".");
    recvResponse(s, reply);

    sendLine(s, "QUIT");
    close(s);
    return true;
}


bool SmtpClient::sendMailFromFile(Session& ses,
                                  const std::string& mimeFile,
                                  const std::string& rcpt,
                                  std::string& error)
{
    int s = ses.sock;
    std::string reply;

    sendLine(s, "MAIL FROM:<" + user_ + ">");
    recvResponse(s, reply);

    sendLine(s, "RCPT TO:<" + rcpt + ">");
    recvResponse(s, reply);

    sendLine(s, "DATA");
    recvResponse(s, reply);

    FILE* f = fopen(mimeFile.c_str(), "rb");
    if (!f)
    {
        error = "Cannot open MIME file";
        return false;
    }

    char buf[2048];
    while (!feof(f))
    {
        size_t r = fread(buf, 1, sizeof(buf), f);
        if (r > 0)
            send(s, buf, r, 0);
    }
    fclose(f);

    sendLine(s, ".");
    recvResponse(s, reply);
    return true;
}



bool SmtpClient::sendMail(Session& ses,
                          const EmailMessage& msg,
                          std::string& error)
{
    int s = ses.sock;
    std::string reply;

    sendLine(s, "MAIL FROM:<" + msg.from + ">");
    recvResponse(s, reply);

    sendLine(s, "RCPT TO:<" + msg.to + ">");
    recvResponse(s, reply);

    sendLine(s, "DATA");
    recvResponse(s, reply);

    sendLine(s, "From: " + msg.from);
    sendLine(s, "To: " + msg.to);
    sendLine(s, "Subject: " + msg.subject);

    if (msg.attachments.empty())
    {
        sendLine(s, "");
        sendLine(s, msg.body);
    }
    else
    {
        std::string boundary = "----myboundary12345";

        sendLine(s, "MIME-Version: 1.0");
        sendLine(s, "Content-Type: multipart/mixed; boundary=\"" + boundary + "\"");
        sendLine(s, "");

        sendLine(s, "--" + boundary);
        sendLine(s, "Content-Type: text/plain; charset=utf-8");
        sendLine(s, "");
        sendLine(s, msg.body);

        for (const auto& a : msg.attachments)
        {
            sendLine(s, "--" + boundary);
            sendLine(s, "Content-Type: application/octet-stream");
            sendLine(s, "Content-Disposition: attachment; filename=\"" + a.filename + "\"");
            sendLine(s, "Content-Transfer-Encoding: base64");
            sendLine(s, "");

            FILE* f = fopen(a.path.c_str(), "rb");
            if (!f) continue;

            unsigned char buf[57];
            while (true)
            {
                size_t r = fread(buf, 1, sizeof(buf), f);
                if (r == 0) break;

                std::string chunk(reinterpret_cast<char*>(buf), r);
                sendLine(s, base64(chunk));
            }
            fclose(f);
        }

        sendLine(s, "--" + boundary + "--");
    }

    sendLine(s, ".");
    recvResponse(s, reply);
    return true;
}

