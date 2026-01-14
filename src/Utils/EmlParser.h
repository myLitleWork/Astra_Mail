#pragma once
#include <string>
#include <vector>

struct MimePart
{
    std::string headers;
    std::string body;
};

struct ParsedPart
{
    std::string contentType;
    std::string charset;
    std::string transferEncoding;

    bool isTextPlain = false;
    bool isAttachment = false;

    std::string filename; // если attachment
    std::string body;     // пока сырой
};


class EmlParser
{
public:

    static void parseAndSaveAttachmentsFromRaw(
            const std::string& raw,
            const std::string& outDir);

    static bool extractTopHeaders(const std::string& rawEml,
                                  std::string& headers,
                                  std::string& body);

    static std::string extractSubject(const std::string& headers);

    // читает .eml, возвращает parts (если не multipart — будет 1 part: headers+body)
    static bool splitToParts(const std::string& emlPath,
                             std::vector<MimePart>& outParts,
                             std::string& err);
    static bool splitToPartsFromRaw(const std::string& rawEml,
                             std::vector<MimePart>& outParts,
                             std::string& err);
    static ParsedPart parsePart(const MimePart& p);

    static bool loadFile(const std::string& path, std::string& out, std::string& err);

private:

    static bool splitToPartsImpl(const std::string& raw,
                                 std::vector<MimePart>& outParts,
                                 std::string& err);

    static void splitHeadersBody(const std::string& src, std::string& headers, std::string& body);
    static std::string extractBoundary(const std::string& headers);
    static std::vector<std::string> splitMultipart(const std::string& body, const std::string& boundary);
    static void trimLineEnd(std::string& s);
};
