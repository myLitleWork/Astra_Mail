#include "SmtpClient.h"
#include <netdb.h>
#include <unistd.h>
#include <sys/socket.h>
#include <cstdio>
#include <utility>
#include <vector>



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

bool SmtpClient::sendMailFromFile(
        const std::string& mimeFile,
        const std::string& rcpt,
        std::string& error)
{
    std::vector<std::string> rcpts;
    rcpts.push_back(rcpt);
    return sendMailFromFile(mimeFile, rcpts, error);
}

bool SmtpClient::sendMailFromFile(
        const std::string& mimeFile,
        const std::vector<std::string>& rcpts,
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

    if (rcpts.empty())
    {
        error = "No recipients provided";
        sendLine(s, "QUIT");
        close(s);
        return false;
    }

    sendLine(s, "MAIL FROM:<" + user_ + ">");
    recvResponse(s, reply);

    for (const auto& rcpt : rcpts)
    {
        if (!sendLine(s, "RCPT TO:<" + rcpt + ">") || !recvResponse(s, reply))
        {
            error = "SMTP RCPT TO failed for: " + rcpt;
            sendLine(s, "QUIT");
            close(s);
            return false;
        }
    }


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
    std::vector<std::string> rcpts;
    rcpts.push_back(rcpt);
    return sendMailFromFile(ses, mimeFile, rcpts, error);
}

bool SmtpClient::sendMailFromFile(Session& ses,
                                  const std::string& mimeFile,
                                  const std::vector<std::string>& rcpts,
                                  std::string& error)
{
    int s = ses.sock;
    std::string reply;
    if (rcpts.empty())
    {
        error = "No recipients provided";
        return false;
    }

    sendLine(s, "MAIL FROM:<" + user_ + ">");
    recvResponse(s, reply);

    for (const auto& rcpt : rcpts)
    {
        if (!sendLine(s, "RCPT TO:<" + rcpt + ">") || !recvResponse(s, reply))
        {
            error = "SMTP RCPT TO failed for: " + rcpt;
            return false;
        }
    }

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

