//
// Created by Sg on 18.12.2025.
//

#include "CmdParseEmlFolder.h"
#include "UtilsFS/FsUtils.h"
#include "Utils/Base64.h"
#include "Utils/EmlParser.h"

#include <iostream>
#include <fstream>
#include <dirent.h>


namespace astra_mail {


    static bool decodeRfc2047(const std::string& in, std::string& out)
    {
        // формат: =?charset?B?base64?=
        out.clear();

        if (in.size() < 8)
            return false;

        if (in.substr(0, 2) != "=?")
            return false;

        auto p1 = in.find('?', 2);
        if (p1 == std::string::npos) return false;

        auto p2 = in.find('?', p1 + 1);
        if (p2 == std::string::npos) return false;

        auto p3 = in.find("?=", p2 + 1);
        if (p3 == std::string::npos) return false;

        std::string charset  = in.substr(2, p1 - 2);
        std::string encoding = in.substr(p1 + 1, p2 - p1 - 1);
        std::string payload  = in.substr(p2 + 1, p3 - p2 - 1);

        if (encoding != "B" && encoding != "b")
            return false;

        auto decoded = decodeBase64(payload);
        if (decoded.empty())
            return false;

        out.assign(reinterpret_cast<const char*>(decoded.data()),
                   decoded.size());

        return true;
    }



    static std::string getHeaderValue(const std::string& headers,
                                      const std::string& name)
    {
        std::string key = name + ":";
        auto pos = headers.find(key);
        if (pos == std::string::npos)
            return "";

        pos += key.size();
        while (pos < headers.size() &&
               (headers[pos] == ' ' || headers[pos] == '\t'))
            ++pos;

        auto end = headers.find('\n', pos);
        if (end == std::string::npos)
            end = headers.size();

        std::string v = headers.substr(pos, end - pos);
        if (!v.empty() && v.back() == '\r')
            v.pop_back();

        return v;
    }

    static std::string normalizeSubjectDir(const std::string& s)
    {
        std::string out;
        out.reserve(s.size());

        for (unsigned char c : s)
        {
            if (std::isalnum(c) || c == '-' || c == '_')
                out.push_back(c);
            else if (std::isspace(c))
                out.push_back('_');
            // остальное выкидываем
        }

        if (out.empty())
            out = "NO_SUBJECT";

        if (out.size() > 64)
            out.resize(64);

        return out;
    }


    CmdParseEmlFolder::CmdParseEmlFolder(std::string inDir,
                                         std::string outDir)
            : inDir_(std::move(inDir))
            , outDir_(std::move(outDir))
    {
    }

    std::string CmdParseEmlFolder::title() const {
        return "Парсинг EML → вложения";
    }

    CommandResult CmdParseEmlFolder::execute()
    {
        using namespace fsu;
        ensureDir(outDir_);

        DIR* d = opendir(inDir_.c_str());
        if (!d)
        {
            std::cout << "[FS] cannot open dir: " << inDir_ << "\n";
            return CommandResult::ExitMenu;
        }

        dirent* de = nullptr;
        while ((de = readdir(d)) != nullptr)
        {
            if (de->d_type != DT_REG)
                continue;

            std::string name = de->d_name;
            if (name.size() < 4 || name.substr(name.size() - 4) != ".eml")
                continue;

            std::string emlPath = inDir_ + "/" + name;


            std::string mailId = name;
            if (mailId.size() > 4 && mailId.substr(mailId.size() - 4) == ".eml")
                mailId.resize(mailId.size() - 4);


            std::string raw;
            std::string err;

            if (!EmlParser::loadFile(emlPath, raw, err))
            {
                std::cout << "[ERR] " << err << "\n";
                continue;
            }

            std::string headers, body;

            // отделяем заголовки письма
            {
                auto p = raw.find("\r\n\r\n");
                if (p != std::string::npos)
                {
                    headers = raw.substr(0, p);
                    body    = raw.substr(p + 4);
                }
                else
                {
                    headers = raw;
                }
            }

            std::string subject = getHeaderValue(headers, "Subject");

            std::string decoded;
            if (decodeRfc2047(subject, decoded))
                subject = decoded;

            std::string subjectDir = normalizeSubjectDir(subject);

            std::string finalOutDir = outDir_ + "/" + subjectDir;
            ensureDir(finalOutDir);
            std::string mailOutDir = finalOutDir + "/" + mailId;
            ensureDir(mailOutDir);



            std::vector<MimePart> parts;
            if (!EmlParser::splitToPartsFromRaw(raw, parts, err))
            {
                std::cout << "[PARSE] " << err << "\n";
                continue;
            }

            for (const auto& p : parts)
            {
                ParsedPart pp = EmlParser::parsePart(p);

                if (!pp.isAttachment)
                    continue;

                if (pp.transferEncoding != "base64")
                    continue;

                auto data = decodeBase64(pp.body);
                if (data.empty())
                    continue;

                std::string outFile = mailOutDir + "/" + pp.filename;

                std::ofstream f(outFile, std::ios::binary);
                if (!f)
                {
                    std::cout << "[FS] cannot write " << outFile << "\n";
                    continue;
                }

                f.write(reinterpret_cast<const char*>(data.data()),
                        data.size());

                std::cout << "[OK] saved " << outFile
                          << " (" << data.size() << " bytes)\n";
            }
        }

        closedir(d);
        return CommandResult::ExitMenu;
    }


} // astra_mail