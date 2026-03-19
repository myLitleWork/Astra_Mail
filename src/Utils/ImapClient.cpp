// ImapClient.cpp
#include "ImapClient.h"
#include "core/thunderbird/ThunderbirdSettings.h"

// POSIX / sockets
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/select.h>
#include <cerrno>


// STL
#include <string>
#include <vector>
#include <sstream>
#include <iostream>
#include <fstream>

#if __has_include(<filesystem>)
#include <filesystem>
    namespace fs = std::filesystem;
#else
#include <experimental/filesystem>
namespace fs = std::experimental::filesystem;
#endif


static bool sendAll(int s, const void* data, size_t len)
{
    const char* p = static_cast<const char*>(data);
    while (len > 0)
    {
        ssize_t n = send(s, p, len, 0);
        if (n <= 0)
            return false;
        p   += n;
        len -= n;
    }
    return true;
}

static bool tcpConnectQuick(const std::string& host, int port, int timeoutMs, std::string& err)
{
    err.clear();

    addrinfo hints{}, *res = nullptr;
    hints.ai_socktype = SOCK_STREAM;

    if (::getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &res) != 0)
    {
        err = "DNS resolve failed";
        return false;
    }

    int sock = ::socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sock < 0)
    {
        ::freeaddrinfo(res);
        err = "socket() failed";
        return false;
    }

    // non-blocking connect with select timeout
    int flags = ::fcntl(sock, F_GETFL, 0);
    if (flags >= 0)
        ::fcntl(sock, F_SETFL, flags | O_NONBLOCK);

    int rc = ::connect(sock, res->ai_addr, res->ai_addrlen);
    if (rc == 0)
    {
        ::close(sock);
        ::freeaddrinfo(res);
        return true;
    }

    if (errno != EINPROGRESS)
    {
        ::close(sock);
        ::freeaddrinfo(res);
        err = "connect() failed";
        return false;
    }

    fd_set wfds;
    FD_ZERO(&wfds);
    FD_SET(sock, &wfds);

    timeval tv{};
    tv.tv_sec  = timeoutMs / 1000;
    tv.tv_usec = (timeoutMs % 1000) * 1000;

    rc = ::select(sock + 1, nullptr, &wfds, nullptr, &tv);
    if (rc <= 0)
    {
        ::close(sock);
        ::freeaddrinfo(res);
        err = (rc == 0) ? "connect timeout" : "select() failed";
        return false;
    }

    int so_error = 0;
    socklen_t len = sizeof(so_error);
    if (::getsockopt(sock, SOL_SOCKET, SO_ERROR, &so_error, &len) != 0 || so_error != 0)
    {
        ::close(sock);
        ::freeaddrinfo(res);
        err = "connect() failed";
        return false;
    }

    ::close(sock);
    ::freeaddrinfo(res);
    return true;
}

static bool recvLine(int s, std::string& out)
{
    out.clear();
    char c;
    while (true) {
        ssize_t n = recv(s, &c, 1, 0);

        if (n <= 0) return false;
        out.push_back(c);
        if (c == '\n') break;
    }
    return true;
}

static void sendCmd(int s, const std::string& cmd)
{
    std::string line = cmd + "\r\n";
    send(s, line.c_str(), line.size(), 0);
}





    static void sendLine(int s, const std::string& line)
    {
        std::string l = line + "\r\n";
        sendAll(s, l.data(), l.size());
    }

    static std::string nextTag(ImapSession& ses)
    {
        return "A" + std::to_string(ses.nextTag++);
    }

    static std::string sendTagged(ImapSession& ses, const std::string& cmd)
    {
        std::string tag = "A" + std::to_string(ses.nextTag++);
        sendLine(ses.sock, tag + " " + cmd);
        return tag;
    }


bool imapOpen(ImapSession& ses,
                     const std::string& host,
                     int port,
                     std::string& err)
{
    addrinfo hints{}, *res = nullptr;
    hints.ai_socktype = SOCK_STREAM;

    if (::getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &res) != 0)
    {
        err = "DNS resolve failed";
        return false;
    }

    ses.sock = ::socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (ses.sock < 0)
    {
        ::freeaddrinfo(res);
        err = "socket() failed";
        return false;
    }

    if (::connect(ses.sock, res->ai_addr, res->ai_addrlen) != 0)
    {
        ::freeaddrinfo(res);
        ::close(ses.sock);
        ses.sock = -1;
        err = "connect() failed";
        return false;
    }

    ::freeaddrinfo(res);

    std::string banner;
    if (!recvLine(ses.sock, banner))
    {
        err = "no IMAP banner";
        ::close(ses.sock);
        ses.sock = -1;
        return false;
    }

    return true;
}

bool imapLogin(ImapSession& ses,
                      const std::string& user,
                      const std::string& pass,
                      std::string& err)
{
    std::string tag = sendTagged(ses, "LOGIN " + user + " " + pass);

    std::string line;
    while (recvLine(ses.sock, line))
    {
        if (line.rfind(tag + " OK", 0) == 0)
            return true;
        if (line.rfind(tag + " NO", 0) == 0 ||
            line.rfind(tag + " BAD", 0) == 0)
            break;
    }

    err = "IMAP LOGIN failed";
    return false;
}


bool imapSelect(ImapSession& ses,
                       const std::string& folder,
                       std::string& err)
{
    std::string tag = sendTagged(ses, "SELECT \"" + folder + "\"");

    std::string line;
    while (recvLine(ses.sock, line))
    {
        if (line.rfind(tag + " OK", 0) == 0)
            return true;

        if (line.rfind(tag + " NO", 0) == 0 ||
            line.rfind(tag + " BAD", 0) == 0)
            break;
    }

    err = "IMAP SELECT failed";
    return false;
}


void imapClose(ImapSession& ses)
{
    if (ses.sock >= 0)
    {
        // LOGOUT — тоже IMAP-команда → через tag
        sendTagged(ses, "LOGOUT");
        ::close(ses.sock);
        ses.sock = -1;
    }
}


bool ImapClient::listFolders(const std::string& host,
                             int port,
                             const std::string& user,
                             const std::string& pass,
                             std::string& outResult,
                             std::string& outError)
{
    outResult.clear();
    outError.clear();

    // DNS resolve
    addrinfo hints{}, *res = nullptr;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &res) != 0) {
        outError = "DNS resolve failed";
        return false;
    }

    int s = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (s < 0) {
        outError = "socket() failed";
        freeaddrinfo(res);
        return false;
    }

    if (connect(s, res->ai_addr, res->ai_addrlen) != 0) {
        outError = "connect() failed";
        freeaddrinfo(res);
        close(s);
        return false;
    }
    freeaddrinfo(res);

    // read greeting
    std::string line;
    if (!recvLine(s, line)) { outError = "No greeting"; close(s); return false; }
    outResult += line;

    // LOGIN
    sendCmd(s, "A1 LOGIN " + user + " " + pass);
    while (true) {
        if (!recvLine(s, line)) break;
        outResult += line;
        if (line.rfind("A1 ", 0) == 0) break;
    }

    // LIST "" "*"
    sendCmd(s, R"(A2 LIST "" "*")");
    while (true) {
        if (!recvLine(s, line)) break;
        outResult += line;
        if (line.rfind("A2 ", 0) == 0) break;
    }

    // LOGOUT
    sendCmd(s, "A3 LOGOUT");
    recvLine(s, line);
    outResult += line;

    close(s);
    return true;
}


bool ImapClient::imapAppendSentFromFile(const std::string& mimeFile, std::string& err)
{
    err.clear();

    std::ifstream f(mimeFile, std::ios::binary);
    if (!f)
    {
        err = "cannot open mime file: " + mimeFile;
        return false;
    }

    std::string raw((std::istreambuf_iterator<char>(f)),
                    std::istreambuf_iterator<char>());

    if(verbose_)
        std::cout << "[IMAP] loaded MIME from file, size=" << raw.size() << "\n";

    if (raw.empty())
    {
        err = "mime file empty";
        return false;
    }

    return ImapClient::imapAppendSent(raw, err);
}



bool ImapClient::imapAppendSent(const std::string& rawMime, std::string& err)
{
    // Берём реальные параметры из активной конфигурации Thunderbird,
    // а не из хардкода (иначе "Sent" будет ломать отправку).
    auto& tb = ThunderbirdSettings::instance();
    if (!tb.active().valid() || !tb.hasMailPassword())
    {
        err = "Thunderbird account/password not set";
        return false;
    }

    const auto& a = tb.active();
    const auto& imapCfg = tb.imapServers().at(a.imapServerId);

    const std::string& hostS = imapCfg.host;
    int port = imapCfg.port;
    const std::string& userS = imapCfg.user;
    const std::string& passS = tb.mailPassword();

    // Если сети нет/IMAP недоступен — не тратим время на протокол APPEND.
    // SMTP уже мог успешно отправить письмо; APPEND в Sent — дополнительная операция.
    {
        std::string pingErr;
        if (!tcpConnectQuick(hostS, port, 3000, pingErr))
        {
            err = "IMAP unreachable: " + pingErr;
            return false;
        }
    }

    const char* host = hostS.c_str();
    const char* user = userS.c_str();
    const char* pass = passS.c_str();

    auto log = [this](const std::string& s)
    {
        if(!verbose_)
            return;

        std::cout << "[IMAP] " << s << "\n";
    };

    addrinfo hints{}, *res = nullptr;
    hints.ai_socktype = SOCK_STREAM;

    log(std::string("connect ") + host + ":" + std::to_string(port));

    if (getaddrinfo(host, std::to_string(port).c_str(), &hints, &res) != 0)
    {
        err = "DNS error";
        return false;
    }

    int sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sock < 0)
    {
        freeaddrinfo(res);
        err = "socket() failed";
        return false;
    }

    if (connect(sock, res->ai_addr, res->ai_addrlen) != 0)
    {
        freeaddrinfo(res);
        close(sock);
        err = "connect() failed";
        return false;
    }
    freeaddrinfo(res);

    auto recvUntil = [&](const std::string& token)
    {
        std::string all;
        char buf[2048];

        while (true)
        {
            ssize_t n = recv(sock, buf, sizeof(buf) - 1, 0);
            if (n <= 0) break;

            buf[n] = 0;
            all += buf;

            log(buf);

            if (all.find(token) != std::string::npos)
                break;
        }
        return all;
    };

    auto sendCmd = [&](const std::string& s)
    {
        log(s);
        std::string line = s + "\r\n";
        sendAll(sock, line.data(), line.size());
    };

    recvUntil("\n"); // banner

    sendCmd(std::string("a1 LOGIN ") + user + " " + pass);
    recvUntil("a1 OK");

    sendCmd("a2 SELECT Sent");
    recvUntil("a2 OK");

    const size_t sz = rawMime.size();
    log("APPEND size = " + std::to_string(sz));

    sendCmd("a3 APPEND Sent (\\Seen) {" + std::to_string(sz) + "}");

    // Сервер должен ответить continuation: "+ ..."
    // Если вместо этого вернёт "a3 NO/BAD ..." — покажем реальную строку.
    std::string r = recvUntil("\n");
    if (r.find('+') == std::string::npos)
    {
        // Попробуем дочитать ещё немного, чтобы захватить tagged NO/BAD, если он пришёл чуть позже.
        r += recvUntil("\n");
        if (r.find("a3 NO") != std::string::npos || r.find("a3 BAD") != std::string::npos)
            err = "APPEND rejected: " + r;
        else
            err = "Server did not accept literal: " + r;
        close(sock);
        return false;
    }

    log("sending MIME body");
    if (!sendAll(sock, rawMime.data(), rawMime.size()))
    {
        err = "sendAll(body) failed";
        close(sock);
        return false;
    }

// 🔥 ОБЯЗАТЕЛЬНО завершить APPEND
    if (!sendAll(sock, "\r\n", 2))
    {
        err = "sendAll(CRLF) failed";
        close(sock);
        return false;
    }

    log("waiting a3 OK");

    r = recvUntil("a3 OK");
    if (r.find("a3 OK") == std::string::npos)
    {
        err = "APPEND failed / no a3 OK";
        close(sock);
        return false;
    }

    sendCmd("a4 LOGOUT");
    recvUntil("OK");

    close(sock);
    log("done");
    return true;
}



bool ImapClient::markSeenByUid(const std::string& host,
                               int port,
                               const std::string& user,
                               const std::string& pass,
                               const std::string& folder,
                               const std::string& uid,
                               std::string& err)
{
    err.clear();

    ImapSession ses;
    if (!imapOpen(ses, host, port, err)) return false;
    if (!imapLogin(ses, user, pass, err)) { imapClose(ses); return false; }
    if (!imapSelect(ses, folder, err))    { imapClose(ses); return false; }

    bool ok = markSeenByUid(
            ses,
            uid,
            err
    );

    imapClose(ses);
    return ok;
}


bool ImapClient::fetchRawEmlByUid(const std::string& host,
                                  int port,
                                  const std::string& user,
                                  const std::string& pass,
                                  const std::string& folder,
                                  const std::string& uid,
                                  std::string& outRawEml,
                                  std::string& err)
{
    outRawEml.clear();
    err.clear();

    ImapSession ses;
    if (!imapOpen(ses, host, port, err)) return false;
    if (!imapLogin(ses, user, pass, err)) { imapClose(ses); return false; }
    if (!imapSelect(ses, folder, err))    { imapClose(ses); return false; }

    bool ok = fetchRawEmlByUid(
            ses,
            uid,
            outRawEml,
            err
    );

    imapClose(ses);
    return ok;
}


bool ImapClient::fetchRawEmlByUid(ImapSession& ses,
                                  const std::string& uid,
                                  std::string& outRawEml,
                                  std::string& err)
{
    outRawEml.clear();
    err.clear();

    // ⬇️ ОТПРАВКА С TAG
    std::string tag =
            sendTagged(ses, "UID FETCH " + uid + " (BODY.PEEK[])");

    std::string line;
    size_t literalSize = 0;

    // 1️⃣ Ищем {size}
    while (recvLine(ses.sock, line))
    {
        auto lb = line.find('{');
        auto rb = line.find('}');
        if (lb != std::string::npos && rb != std::string::npos && rb > lb)
        {
            literalSize = std::stoul(line.substr(lb + 1, rb - lb - 1));
            break;
        }
    }

    if (literalSize == 0)
    {
        err = "FETCH: no literal size";
        return false;
    }

    // 2️⃣ Читаем literal
    outRawEml.resize(literalSize);
    size_t readTotal = 0;

    while (readTotal < literalSize)
    {
        ssize_t n = ::recv(ses.sock,
                           &outRawEml[readTotal],
                           literalSize - readTotal,
                           0);
        if (n <= 0)
        {
            err = "FETCH body failed";
            return false;
        }
        readTotal += static_cast<size_t>(n);
    }

    // 3️⃣ Ждём OK именно по НАШЕМУ tag
    while (recvLine(ses.sock, line))
    {
        if (line.rfind(tag + " OK", 0) == 0)
            return true;

        if (line.rfind(tag + " NO", 0) == 0 ||
            line.rfind(tag + " BAD", 0) == 0)
        {
            err = "FETCH failed";
            return false;
        }
    }

    err = "FETCH: connection closed";
    return false;
}


bool ImapClient::markSeenByUid(ImapSession& ses,
                               const std::string& uid,
                               std::string& err)
{
    err.clear();

    // ⬇️ отправка с уникальным tag
    std::string tag =
            sendTagged(ses, "UID STORE " + uid + " +FLAGS (\\Seen)");

    std::string line;
    while (recvLine(ses.sock, line))
    {
        if (line.rfind(tag + " OK", 0) == 0)
            return true;

        if (line.rfind(tag + " NO", 0) == 0 ||
            line.rfind(tag + " BAD", 0) == 0)
            break;
    }

    err = "UID STORE failed";
    return false;
}


bool ImapClient::searchUnreadUids(ImapSession& ses,
                                  const std::string& folder,
                                  std::vector<std::string>& outUids,
                                  std::string& err)
{
    outUids.clear();
    err.clear();

    if (!selectMailbox(ses, folder, err))
        return false;

    // ⬇️ отправка с tag
    std::string tag =
            sendTagged(ses, "UID SEARCH UNSEEN");

    std::string line;
    while (recvLine(ses.sock, line))
    {
        // untagged результат
        if (line.rfind("* SEARCH", 0) == 0)
        {
            std::istringstream iss(line.substr(8));
            std::string uid;
            while (iss >> uid)
                outUids.push_back(uid);
        }

        // ⬇️ окончание команды — ТОЛЬКО по нашему tag
        if (line.rfind(tag + " OK", 0) == 0)
            return true;

        if (line.rfind(tag + " NO", 0) == 0 ||
            line.rfind(tag + " BAD", 0) == 0)
        {
            err = "UID SEARCH failed";
            return false;
        }
    }

    err = "UID SEARCH: connection closed";
    return false;
}



bool ImapClient::searchUnreadUids(const std::string& host,
                                  int port,
                                  const std::string& user,
                                  const std::string& pass,
                                  const std::string& folder,
                                  std::vector<std::string>& outUids,
                                  std::string& err)
{
    ImapSession ses;
    if (!imapOpen(ses, host, port, err)) return false;
    if (!imapLogin(ses, user, pass, err)) { imapClose(ses); return false; }

    bool ok = searchUnreadUids(ses, folder, outUids, err);
    imapClose(ses);
    return ok;
}

bool ImapClient::searchUnreadUids(ImapSession& ses,
                                  const std::vector<std::string>& folders,
                                  std::vector<std::string>& outUids,
                                  std::string& err)
{
    outUids.clear();
    err.clear();

    for (const auto& folder : folders)
    {

        std::vector<std::string> folderUids;
        if (!searchUnreadUids(ses, folder, folderUids, err))
            return false;

        outUids.insert(outUids.end(),
                       folderUids.begin(),
                       folderUids.end());
    }

    return true;
}

bool ImapClient::selectMailbox(ImapSession& ses,
                               const std::string& folder,
                               std::string& err)
{
    err.clear();

    std::string cmd = "SELECT \"" + folder + "\"";
    std::string tag = sendTagged(ses, cmd);

    std::string line;
    while (recvLine(ses.sock, line))
    {
        if (line.rfind(tag + " OK", 0) == 0)
            return true;

        if (line.rfind(tag + " NO", 0) == 0 ||
            line.rfind(tag + " BAD", 0) == 0)
        {
            err = "SELECT failed: " + folder;
            return false;
        }
    }

    err = "SELECT: connection closed";
    return false;
}


bool ImapClient::searchUnreadUids(const std::string &host, int port, const std::string &user, const std::string &pass,
                                  const std::vector<std::string> &folders, std::vector<std::string> &outUids,
                                  std::string &err) {

    if(folders.empty())
        return false;


    ImapSession ses;
    if (!imapOpen(ses, host, port, err)) return false;
    if (!imapLogin(ses, user, pass, err)) { imapClose(ses); return false; }

    bool ok = searchUnreadUids(ses, folders, outUids, err);
    imapClose(ses);
    return ok;

}

bool ImapClient::listMailboxes(const std::string &host, int port, const std::string &user, const std::string &pass,
                               std::vector<std::string> &folders, std::string &err) {

    ImapSession ses;
    if (!imapOpen(ses, host, port, err)) return false;
    if (!imapLogin(ses, user, pass, err)) { imapClose(ses); return false; }

    bool ok = listMailboxes(ses, folders, err);
    imapClose(ses);
    return ok;

}


#include <cctype>


static bool recvExact(int sock, void* buf, size_t n)
{
    char* p = static_cast<char*>(buf);
    while (n > 0)
    {
        int r = ::recv(sock, p, (int)n, 0);
        if (r <= 0) return false;
        p += r;
        n -= (size_t)r;
    }
    return true;
}

static void skipSpaces(const std::string& s, size_t& i)
{
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
}

static bool parseQuoted(const std::string& s, size_t& i, std::string& out)
{
    // expects s[i] == '"'
    out.clear();
    if (i >= s.size() || s[i] != '"') return false;
    ++i;

    while (i < s.size())
    {
        char c = s[i++];
        if (c == '"') return true;
        if (c == '\\')
        {
            if (i >= s.size()) return false;
            out.push_back(s[i++]); // \" or \\ or \x
        }
        else
        {
            out.push_back(c);
        }
    }
    return false;
}

static bool parseAtom(const std::string& s, size_t& i, std::string& out)
{
    // atom: until SP or control/specials (упрощённо: до пробела/таба)
    out.clear();
    size_t start = i;
    while (i < s.size() && s[i] != ' ' && s[i] != '\t' && s[i] != '\r' && s[i] != '\n')
        ++i;
    if (i == start) return false;
    out = s.substr(start, i - start);
    return true;
}

static bool parseLiteralLenAtEnd(const std::string& s, size_t i, size_t& n)
{
    // ищем {...} в позиции i, обычно в конце: {123}
    // допускаем, что после '}' могут быть только пробелы (но обычно конец строки)
    if (i >= s.size() || s[i] != '{') return false;
    ++i;
    if (i >= s.size() || !std::isdigit((unsigned char)s[i])) return false;

    size_t val = 0;
    while (i < s.size() && std::isdigit((unsigned char)s[i]))
    {
        val = val * 10 + (size_t)(s[i] - '0');
        ++i;
    }
    if (i >= s.size() || s[i] != '}') return false;
    ++i;

    // дальше только пробелы
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
    if (i != s.size()) return false;

    n = val;
    return true;
}

static bool parseAStringOrLiteral(ImapSession& ses,
                                  const std::string& line,
                                  size_t& i,
                                  std::string& out,
                                  std::string& err)
{
    out.clear();
    skipSpaces(line, i);
    if (i >= line.size()) return false;

    if (line[i] == '"')
        return parseQuoted(line, i, out);

    if (line[i] == '{')
    {
        // literal почти всегда стоит в конце строки (после него байты уже НЕ в line)
        size_t n = 0;
        if (!parseLiteralLenAtEnd(line, i, n))
            return false;

        // прочитать n байт literal + затем CRLF
        out.resize(n);
        if (n > 0 && !recvExact(ses.sock, (void *) out.data(), n))
        {
            err = "LIST: literal read failed";
            return false;
        }
        char crlf[2];
        if (!recvExact(ses.sock, crlf, 2))
        {
            err = "LIST: literal CRLF read failed";
            return false;
        }
        return true;
    }

    return parseAtom(line, i, out);
}

static bool skipParenList(const std::string& s, size_t& i)
{
    skipSpaces(s, i);
    if (i >= s.size() || s[i] != '(') return false;
    int depth = 0;
    while (i < s.size())
    {
        char c = s[i++];
        if (c == '(') ++depth;
        else if (c == ')')
        {
            --depth;
            if (depth == 0) return true;
        }
    }
    return false;
}

bool ImapClient::listMailboxes(ImapSession& ses,
                               std::vector<std::string>& outFolders,
                               std::string& err)
{
    outFolders.clear();
    err.clear();

    const std::string tag = sendTagged(ses, R"(LIST "" "*")");

    std::string line;
    while (recvLine(ses.sock, line))
    {
        if (line.rfind("* LIST", 0) == 0)
        {
            size_t i = 6; // after "* LIST"
            // flags: (....)
            if (!skipParenList(line, i))
            {
                // не валим всю операцию: просто игнорим странную строку
                continue;
            }

            // delimiter: astring or NIL
            std::string delim;
            if (!parseAStringOrLiteral(ses, line, i, delim, err))
            {
                // delimiter может быть NIL
                skipSpaces(line, i);
                if (line.compare(i, 3, "NIL") == 0) i += 3;
                else continue;
            }

            // mailbox name: astring / literal
            std::string name;
            if (!parseAStringOrLiteral(ses, line, i, name, err))
            {
                // mailbox name не может быть NIL по RFC, но если вдруг — игнорим
                continue;
            }

            if (!name.empty() && name != "NIL")
                outFolders.push_back(std::move(name));

            continue;
        }

        if (line.rfind(tag + " OK", 0) == 0)
            return true;

        if (line.rfind(tag + " NO", 0) == 0 ||
            line.rfind(tag + " BAD", 0) == 0)
        {
            err = line; // оставляем реальную строку сервера
            return false;
        }
    }

    err = "LIST: connection closed";
    return false;
}








