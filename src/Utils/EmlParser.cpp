#include "EmlParser.h"
#include <fstream>
#include <iostream>
#include "Utils/Base64.h"
#include "UtilsFS/FsUtils.h"


static inline int hexval(unsigned char c)
{
    if ('0' <= c && c <= '9') return c - '0';
    if ('A' <= c && c <= 'F') return c - 'A' + 10;
    if ('a' <= c && c <= 'f') return c - 'a' + 10;
    return -1;
}


bool EmlParser::splitToPartsImpl(const std::string& raw,
                             std::vector<MimePart>& outParts,
                             std::string& err)
{
    outParts.clear();
    err.clear();

    std::string topHeaders, topBody;
    EmlParser::splitHeadersBody(raw, topHeaders, topBody);

    const std::string boundary = EmlParser::extractBoundary(topHeaders);

    if (boundary.empty())
    {
        MimePart p;
        p.headers = topHeaders;
        p.body    = topBody;
        outParts.push_back(std::move(p));
        return true;
    }

    auto chunks = EmlParser::splitMultipart(topBody, boundary);
    if (chunks.empty())
    {
        err = "multipart but no parts found";
        return false;
    }

    for (auto& ch : chunks)
    {
        MimePart part;
        EmlParser::splitHeadersBody(ch, part.headers, part.body);
        outParts.push_back(std::move(part));
    }

    return true;
}

static std::string urlDecode(const std::string& s)
{
    std::string out;
    for (size_t i = 0; i < s.size(); ++i)
    {
        if (s[i] == '%' && i + 2 < s.size())
        {
            int hi = hexval(static_cast<unsigned char>(s[i + 1]));
            int lo = hexval(static_cast<unsigned char>(s[i + 2]));

            if (hi >= 0 && lo >= 0)
            {
                out.push_back(static_cast<char>((hi << 4) | lo));
                i += 2;
                continue;
            }
        }
        else if (s[i] == '+')
        {
            out.push_back(' ');
        }
        else
        {
            out.push_back(s[i]);
        }
    }
    return out;
}
static std::string extractRfc2231Filename(const std::string& headers)
{
    // 1️⃣ RFC 5987: одиночный вариант
    // filename*=UTF-8''%D0%A0%D0%93%5F%34%31%2E%54%56%44
    {
        const std::string key = "filename*=";
        auto p = headers.find(key);
        if (p != std::string::npos)
        {
            p += key.size();

            auto e = headers.find_first_of(";\r\n", p);
            if (e == std::string::npos)
                e = headers.size();

            std::string part = headers.substr(p, e - p);

            // убрать charset''
            auto q = part.find("''");
            if (q != std::string::npos && q + 2 <= part.size())
                part = part.substr(q + 2);

            return urlDecode(part);
        }
    }

    // 2️⃣ RFC 2231: составной вариант filename*0*= ...
    std::string result;
    std::string charset;

    for (int i = 0; i < 10; ++i) // обычно хватает 0..2
    {
        std::string key = "filename*" + std::to_string(i) + "*=";
        auto p = headers.find(key);
        if (p == std::string::npos)
            break;

        p += key.size();

        auto e = headers.find_first_of(";\r\n", p);
        if (e == std::string::npos)
            e = headers.size();

        std::string part = headers.substr(p, e - p);

        // первый кусок может содержать charset''
        if (i == 0)
        {
            auto q = part.find("''");
            if (q != std::string::npos && q + 2 <= part.size())
            {
                charset = part.substr(0, q); // пока не используем
                part = part.substr(q + 2);
            }
        }

        result += part;
    }

    if (result.empty())
        return {};

    return urlDecode(result);
}




static std::string getHeaderValue(const std::string& headers,
                                  const std::string& name)
{
    std::string key = name + ":";
    auto p = headers.find(key);
    if (p == std::string::npos)
        return {};

    p += key.size();

    // до конца строки
    auto e = headers.find('\n', p);
    if (e == std::string::npos)
        e = headers.size();

    std::string val = headers.substr(p, e - p);

    // trim
    while (!val.empty() && (val.front() == ' ' || val.front() == '\t'))
        val.erase(val.begin());
    while (!val.empty() && (val.back() == '\r' || val.back() == '\n'))
        val.pop_back();

    return val;
}

static std::string getParam(const std::string& s, const std::string& name)
{
    auto p = s.find(name + "=");
    if (p == std::string::npos)
        return {};

    p += name.size() + 1;

    if (p < s.size() && s[p] == '"')
    {
        auto e = s.find('"', p + 1);
        if (e == std::string::npos) return {};
        return s.substr(p + 1, e - p - 1);
    }

    auto e = s.find_first_of(";\r\n", p);
    if (e == std::string::npos) e = s.size();
    return s.substr(p, e - p);
}

ParsedPart EmlParser::parsePart(const MimePart& p)
{
    ParsedPart out;
    out.body = p.body;

    // Content-Type
    std::string ct = getHeaderValue(p.headers, "Content-Type");
    out.contentType = ct;

    if (!ct.empty())
    {
        out.charset = getParam(ct, "charset");
        if (ct.find("text/plain") != std::string::npos)
            out.isTextPlain = true;
    }

    // Content-Transfer-Encoding
    out.transferEncoding =
            getHeaderValue(p.headers, "Content-Transfer-Encoding");

    // Content-Disposition
    std::string cd = getHeaderValue(p.headers, "Content-Disposition");
    if (!cd.empty())
    {
        if (cd.find("attachment") != std::string::npos)
        {
            out.isAttachment = true;
            out.filename = getParam(cd, "filename");
        }
    }

    // fallback: имя файла может быть в Content-Type
    if (out.isAttachment && out.filename.empty())
    {
        std::string rfc2231 = extractRfc2231Filename(p.headers);
        if (!rfc2231.empty())
            out.filename = rfc2231;
    }




    return out;
}


static bool startsWith(const std::string& s, const std::string& p)
{
    return s.size() >= p.size() && s.compare(0, p.size(), p) == 0;
}

bool EmlParser::loadFile(const std::string& path, std::string& out, std::string& err)
{
    err.clear();
    std::ifstream f(path, std::ios::binary);
    if (!f)
    {
        err = "cannot open eml: " + path;
        return false;
    }

    out.assign(std::istreambuf_iterator<char>(f),
               std::istreambuf_iterator<char>());
    return true;
}

void EmlParser::splitHeadersBody(const std::string& src,
                                 std::string& headers,
                                 std::string& body)
{
    headers.clear();
    body.clear();

    auto pos = src.find("\r\n\r\n");
    if (pos != std::string::npos)
    {
        headers = src.substr(0, pos);
        body    = src.substr(pos + 4);
        return;
    }

    // fallback на \n\n
    pos = src.find("\n\n");
    if (pos != std::string::npos)
    {
        headers = src.substr(0, pos);
        body    = src.substr(pos + 2);
        return;
    }

    // совсем странно — считаем всё body
    body = src;
}

void EmlParser::trimLineEnd(std::string& s)
{
    while (!s.empty() && (s.back() == '\r' || s.back() == '\n'))
        s.pop_back();
}

std::string EmlParser::extractBoundary(const std::string& headers)
{
    // ищем boundary= в заголовках письма (не частей)
    auto p = headers.find("boundary=");
    if (p == std::string::npos) return {};

    p += 9;
    if (p >= headers.size()) return {};

    if (headers[p] == '"')
    {
        auto e = headers.find('"', p + 1);
        if (e == std::string::npos) return {};
        return headers.substr(p + 1, e - p - 1);
    }

    // до конца строки / ; / пробела
    auto e = headers.find_first_of(";\r\n", p);
    if (e == std::string::npos) e = headers.size();
    return headers.substr(p, e - p);
}

std::vector<std::string> EmlParser::splitMultipart(const std::string& body,
                                                   const std::string& boundary)
{
    std::vector<std::string> chunks;

    const std::string sep  = "--" + boundary;
    const std::string end  = "--" + boundary + "--";

    size_t pos = 0;

    // ищем первую границу
    size_t first = body.find(sep, pos);
    if (first == std::string::npos)
        return chunks;

    pos = first;

    while (true)
    {
        // граница
        size_t b = body.find(sep, pos);
        if (b == std::string::npos) break;

        // конец?
        if (startsWith(body.substr(b, end.size()), end))
            break;

        // перейти за строку boundary
        size_t after = body.find('\n', b);
        if (after == std::string::npos) break;
        after += 1;

        // следующий boundary
        size_t next = body.find(sep, after);
        if (next == std::string::npos) break;

        // вырезаем chunk между границами
        std::string chunk = body.substr(after, next - after);
        trimLineEnd(chunk);
        chunks.push_back(std::move(chunk));

        pos = next;
    }

    return chunks;
}

bool EmlParser::splitToParts(const std::string& emlPath,
                             std::vector<MimePart>& outParts,
                             std::string& err)
{
    std::string raw;
    if (!loadFile(emlPath, raw, err))
        return false;

    return splitToPartsImpl(raw, outParts, err);
}


bool EmlParser::extractTopHeaders(const std::string& rawEml,
                                  std::string& headers,
                                  std::string& body)
{
    headers.clear();
    body.clear();

    auto pos = rawEml.find("\r\n\r\n");
    if (pos != std::string::npos)
    {
        headers = rawEml.substr(0, pos);
        body    = rawEml.substr(pos + 4);
        return true;
    }

    pos = rawEml.find("\n\n");
    if (pos != std::string::npos)
    {
        headers = rawEml.substr(0, pos);
        body    = rawEml.substr(pos + 2);
        return true;
    }

    body = rawEml;
    return false;
}

std::string EmlParser::extractSubject(const std::string& headers)
{
    auto v = getHeaderValue(headers, "Subject");
    if (v.empty())
        return "NO_SUBJECT";

    return v;
}

bool EmlParser::splitToPartsFromRaw(const std::string& rawEml,
                                    std::vector<MimePart>& outParts,
                                    std::string& err)
{
    return splitToPartsImpl(rawEml, outParts, err);
}

