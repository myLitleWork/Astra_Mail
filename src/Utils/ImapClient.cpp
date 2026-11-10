// ImapClient.cpp
#include "ImapClient.h"
#include "core/mail/AccountAccess.h"
#include "core/thunderbird/ThunderbirdSettings.h"
#include "SensitiveString.h"
#include <functional>
#include <initializer_list>
#include <memory>
#include <algorithm>
#include <cctype>

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


static bool wireSendAll(int s, const void* data, size_t len)
{
    return astra_mail::net::sendAll(s, data, len);
}
static bool recvLine(int s, std::string& out)
{
    out.clear();
    if (s < 0) return false;
    char c;
    while (true) {
        ssize_t n = astra_mail::net::receive(s, &c, 1);

        if (n <= 0) return false;
        out.push_back(c);
        if (out.size() > 65536) return false;
        if (c == '\n') break;
    }
    return true;
}





    static bool sendLine(int s, const std::string& line)
    {
        std::string l = line + "\r\n";
        astra_mail::ClearSensitiveOnExit wipe{l};
        if (!wireSendAll(s, l.data(), l.size())) return false;
        astra_mail::net::beginResponse(s, astra_mail::net::settingsFor(s).imapCommandMs);
        return true;
    }

    static std::string nextTag(ImapSession& ses)
    {
        return "A" + std::to_string(ses.nextTag++);
    }

    static std::string sendTagged(ImapSession& ses, const std::string& cmd)
    {
        std::string tag = "A" + std::to_string(ses.nextTag++);
        if (cmd.find_first_of("\r\n") != std::string::npos || cmd.find('\0') != std::string::npos) {
            ses.sock.reset(); return tag;
        }
        if (!sendLine(ses.sock, tag + " " + cmd)) ses.sock.reset();
        return tag;
    }

// Never include authentication arguments or server text in these errors:
// a server can echo credentials in a NO/BAD response.
static std::string taggedStatus(const std::string& line, const std::string& tag) {
    const auto prefix = tag + " ";
    if (line.compare(0, prefix.size(), prefix) != 0) return {};
    auto status = line.substr(prefix.size(), line.find_first_of(" \r\n", prefix.size()) - prefix.size());
    for (char& c : status) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return status;
}
static bool waitCommandReply(ImapSession& ses, const std::string& tag, bool continuation,
                             const char* stage, std::string& error) {
    std::string line;
    while (recvLine(ses.sock, line)) {
        const auto status = taggedStatus(line, tag);
        if (!status.empty()) {
            if (!continuation && status == "OK") return true;
            error = std::string(stage) + ": " +
                (status == "NO" || status == "BAD" ? status : "unexpected tagged reply");
            ses.sock.reset(); return false;
        }
        if (line[0] == '+') {
            if (continuation && (line.size() == 1 || line[1] == ' ' || line[1] == '\r' || line[1] == '\n')) return true;
            break;
        }
        // Untagged status/capability responses are allowed between replies.
        if (line.compare(0, 2, "* ") != 0 || line.compare(0, 5, "* BYE") == 0) break;
    }
    error = std::string(stage) + ": " + (astra_mail::net::lastError().empty() ?
        "connection/protocol error" : astra_mail::net::lastError());
    ses.sock.reset(); return false;
}
static bool sendStringCommand(ImapSession& ses, const char* command,
        std::initializer_list<std::reference_wrapper<const std::string>> arguments,
        std::string& tag, std::string& error) {
    error.clear();
    // Validate ALL arguments before any part of a command is put on the wire.
    for (const auto& argument : arguments) {
        const auto& value = argument.get();
        if (value.find_first_of("\r\n") != std::string::npos || value.find('\0') != std::string::npos) {
            error = std::string("IMAP ") + command + ": CR/LF/NUL in argument";
            ses.sock.reset(); return false;
        }
    }
    tag = nextTag(ses);
    std::string part = tag + " " + command;
    astra_mail::ClearSensitiveOnExit wipe{part};
    for (const auto& argument : arguments) {
        const auto& value = argument.get();
        part += ' ';
        const bool quoted = std::all_of(value.begin(), value.end(),
            [](unsigned char c) { return c >= 32 && c < 127; });
        if (quoted) {
            part += '"';
            for (char c : value) { if (c == '"' || c == '\\') part += '\\'; part += c; }
            part += '"';
        } else {
            part += "{" + std::to_string(value.size()) + "}";
            if (!sendLine(ses.sock, part)) {
                error = std::string("IMAP ") + command + ": " + astra_mail::net::lastError();
                ses.sock.reset(); return false;
            }
            astra_mail::clearSensitiveString(part);
            if (!waitCommandReply(ses, tag, true, command, error)) return false;
            if (!wireSendAll(ses.sock, value.data(), value.size())) {
                error = std::string("IMAP ") + command + ": " + astra_mail::net::lastError();
                ses.sock.reset(); return false;
            }
        }
    }
    if (!sendLine(ses.sock, part)) {
        error = std::string("IMAP ") + command + ": " + astra_mail::net::lastError();
        ses.sock.reset(); return false;
    }
    return true;
}
static bool validUid(const std::string& uid) {
    if (uid.empty() || uid.size() > 10 || uid[0] == '0') return false;
    uint64_t number = 0;
    for (unsigned char c : uid) {
        if (c < '0' || c > '9') return false;
        number = number * 10 + c - '0';
    }
    return number <= 4294967295ULL;
}

bool imapOpen(ImapSession& ses, const std::string& host, int port, std::string& err)
{
    ses.sock.reset(); ses.nextTag = 1;
    ses.sock = astra_mail::net::connectSocket(host, port, err);
    if (ses.sock < 0) return false;
    std::string banner;
    if (!recvLine(ses.sock, banner)) {
        err = "No IMAP banner: " + astra_mail::net::lastError();
        ses.sock.reset(); return false;
    }
    return true;
}
bool imapLogin(ImapSession& ses, const std::string& user, const std::string& pass, std::string& err)
{
    std::string tag;
    return sendStringCommand(ses, "LOGIN", {std::cref(user), std::cref(pass)}, tag, err) &&
           waitCommandReply(ses, tag, false, "IMAP LOGIN", err);
}

bool imapSelect(ImapSession& ses, const std::string& folder, std::string& err)
{
    std::string tag;
    return sendStringCommand(ses, "SELECT", {std::cref(folder)}, tag, err) &&
           waitCommandReply(ses, tag, false, "IMAP SELECT", err);
}
void imapClose(ImapSession& ses)
{
    if (ses.sock >= 0)
    {
        astra_mail::net::sendBestEffort(ses.sock, "A" + std::to_string(ses.nextTag++) + " LOGOUT\r\n");
        ses.sock.reset();
    }
}


bool ImapClient::listFolders(const std::string& host, int port, const std::string& user,
                             const std::string& pass, std::string& outResult, std::string& outError)
{
    outResult.clear(); outError.clear();
    ImapSession ses;
    if (!imapOpen(ses, host, port, outError) || !imapLogin(ses, user, pass, outError)) return false;
    const auto tag = sendTagged(ses, R"(LIST "" "*")");
    std::string line;
    while (recvLine(ses.sock, line)) {
        outResult += line;
        if (line.rfind(tag + " OK", 0) == 0) { imapClose(ses); return true; }
        if (line.rfind(tag + " NO", 0) == 0 || line.rfind(tag + " BAD", 0) == 0) break;
    }
    outError = "IMAP LIST failed: " + (astra_mail::net::lastError().empty() ? line : astra_mail::net::lastError());
    return false;
}
bool ImapClient::imapAppendSentFromFile(const std::string& mimeFile, std::string& err)
{
    std::unique_ptr<FILE, decltype(&std::fclose)> file(std::fopen(mimeFile.c_str(), "rb"), &std::fclose);
    if (!file) { err = "cannot open mime file: " + mimeFile; return false; }
    return imapAppendSentFromStream(file.get(), err);
}

bool ImapClient::imapAppendSentFromStream(FILE* file, std::string& err)
{
    err.clear();
    if (!file || ::fseeko(file, 0, SEEK_SET) != 0) { err = "cannot rewind MIME stream"; return false; }
    std::clearerr(file);
    std::string raw;
    char buffer[8192];
    size_t count;
    while ((count = std::fread(buffer, 1, sizeof(buffer), file)) != 0) raw.append(buffer, count);
    if (std::ferror(file)) { err = "cannot read MIME stream"; return false; }
    if (raw.empty()) { err = "mime stream empty"; return false; }
    if (verbose_) std::cout << "[IMAP] loaded MIME stream, size=" << raw.size() << "\n";
    return imapAppendSent(raw, err);
}
bool ImapClient::imapAppendSent(const std::string& rawMime, std::string& err)
{
    astra_mail::MailOperationScope operation;
    auto& tb = ThunderbirdSettings::instance();
    if (!tb.active().valid() || !tb.hasMailPassword()) {
        err = "Thunderbird account/password not set"; return false;
    }
    const auto& config = tb.imapServers().at(tb.active().imapServerId);
    ImapSession ses;
    if (!imapOpen(ses, config.host, config.port, err) || !imapLogin(ses, config.user, tb.mailPassword(), err)) return false;
    const auto tag = sendTagged(ses, "APPEND Sent (\\Seen) {" + std::to_string(rawMime.size()) + "}");
    std::string line;
    bool continuation = false;
    while (recvLine(ses.sock, line)) {
        if (line.rfind("+", 0) == 0) { continuation = true; break; }
        if (line.rfind(tag + " NO", 0) == 0 || line.rfind(tag + " BAD", 0) == 0) break;
    }
    if (!continuation) { err = "APPEND rejected: " + (astra_mail::net::lastError().empty() ? line : astra_mail::net::lastError()); return false; }
    if (!wireSendAll(ses.sock, rawMime.data(), rawMime.size()) || !wireSendAll(ses.sock, "\r\n", 2)) {
        err = "APPEND transfer failed: " + astra_mail::net::lastError(); return false;
    }
    astra_mail::net::beginResponse(ses.sock, astra_mail::net::settingsFor(ses.sock).imapAppendMs);
    while (recvLine(ses.sock, line)) {
        if (line.rfind(tag + " OK", 0) == 0) { imapClose(ses); return true; }
        if (line.rfind(tag + " NO", 0) == 0 || line.rfind(tag + " BAD", 0) == 0) break;
    }
    err = "APPEND not confirmed: " + (astra_mail::net::lastError().empty() ? line : astra_mail::net::lastError());
    return false;
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

    if (!validUid(uid)) { err = "FETCH: invalid UID"; return false; }

    // ⬇️ ОТПРАВКА С TAG
    std::string tag =
            sendTagged(ses, "UID FETCH " + uid + " (BODY.PEEK[])");

    std::string line;
    size_t literalSize = 0;

    // 1️⃣ Ищем {size}
    while (recvLine(ses.sock, line))
    {
        if (line.rfind(tag + " NO", 0) == 0 || line.rfind(tag + " BAD", 0) == 0) {
            err = "FETCH rejected: " + line; return false;
        }
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
        err = "FETCH: no literal size: " + astra_mail::net::lastError();
        return false;
    }

    // 2️⃣ Читаем literal
    outRawEml.resize(literalSize);
    size_t readTotal = 0;

    while (readTotal < literalSize)
    {
        ssize_t n = astra_mail::net::receive(ses.sock,
                           &outRawEml[readTotal],
                           literalSize - readTotal,
                           true);
        if (n <= 0)
        {
            err = "FETCH body failed: " + astra_mail::net::lastError();
            return false;
        }
        readTotal += static_cast<size_t>(n);
    }

    // 3️⃣ Ждём OK именно по НАШЕМУ tag
    astra_mail::net::beginResponse(ses.sock, astra_mail::net::settingsFor(ses.sock).imapCommandMs);
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

    if (!validUid(uid)) { err = "STORE: invalid UID"; return false; }

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
            while (iss >> uid) {
                if (!validUid(uid)) { outUids.clear(); err = "SEARCH: invalid UID from server"; return false; }
                outUids.push_back(uid);
            }
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

bool ImapClient::selectMailbox(ImapSession& ses, const std::string& folder, std::string& err)
{
    return imapSelect(ses, folder, err);
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
        int r = astra_mail::net::receive(sock, p, n, true);
        if (r <= 0) return false;
        p += r;
        n -= (size_t)r;
    }
    astra_mail::net::beginResponse(sock, astra_mail::net::settingsFor(sock).imapCommandMs);
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

    err = "LIST failed: " + astra_mail::net::lastError();
    return false;
}








