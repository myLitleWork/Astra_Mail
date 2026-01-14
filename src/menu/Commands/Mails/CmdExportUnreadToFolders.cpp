#include "CmdExportUnreadToFolders.h"
#include "Utils/ImapClient.h"
#include "Utils/EmlParser.h"
#include "Utils/Base64.h"
#include "Resolver/Resolvers/SubjectFolderResolver.h"
#include <iostream>
#include <sys/stat.h>
#include <cerrno>
#include "UtilsFS/FsUtils.h"
#include "Utils/Utils.h"
#include "Configs/Confg/ConfigPaths.h"



namespace astra_mail {




    static std::string unfoldHeaderValue(std::string s)
    {
        // RFC5322 unfolding: "\r\n " or "\r\n\t" -> " "
        for (;;)
        {
            auto p = s.find("\r\n");
            if (p == std::string::npos) break;
            if (p + 2 < s.size() && (s[p+2] == ' ' || s[p+2] == '\t'))
            {
                s.replace(p, 3, " "); // \r\n + (space/tab) -> space
            }
            else
                break;
        }
        return s;
    }

    static std::vector<uint8_t> decodeQuotedPrintable(const std::string& in)
    {
        std::vector<uint8_t> out;
        out.reserve(in.size());

        for (size_t i = 0; i < in.size(); ++i)
        {
            unsigned char c = in[i];

            if (c == '=')
            {
                // soft line break: =\r\n or =\n
                if (i + 1 < in.size() && in[i + 1] == '\n')
                {
                    ++i;
                    continue;
                }
                if (i + 2 < in.size() && in[i + 1] == '\r' && in[i + 2] == '\n')
                {
                    i += 2;
                    continue;
                }

                // =HH
                if (i + 2 < in.size())
                {
                    auto hex = [](unsigned char x) -> int
                    {
                        if (x >= '0' && x <= '9') return x - '0';
                        if (x >= 'A' && x <= 'F') return x - 'A' + 10;
                        if (x >= 'a' && x <= 'f') return x - 'a' + 10;
                        return -1;
                    };

                    int h1 = hex(in[i + 1]);
                    int h2 = hex(in[i + 2]);

                    if (h1 >= 0 && h2 >= 0)
                    {
                        out.push_back(static_cast<uint8_t>((h1 << 4) | h2));
                        i += 2;
                        continue;
                    }
                }

                // некорректная форма — сохраняем как есть
                out.push_back(c);
            }
            else
            {
                out.push_back(c);
            }
        }

        return out;
    }


    static std::string decodeRfc2047_B_words(const std::string& input)
    {
        std::string s = unfoldHeaderValue(input);
        std::string out;
        out.reserve(s.size());

        size_t i = 0;
        while (i < s.size())
        {
            size_t p = s.find("=?", i);
            if (p == std::string::npos)
            {
                out.append(s, i, std::string::npos);
                break;
            }
            out.append(s, i, p - i);

            // =?charset?B?payload?=
            size_t p1 = s.find('?', p + 2);
            size_t p2 = (p1 == std::string::npos) ? std::string::npos : s.find('?', p1 + 1);
            size_t p3 = (p2 == std::string::npos) ? std::string::npos : s.find("?=", p2 + 1);

            if (p1 == std::string::npos || p2 == std::string::npos || p3 == std::string::npos)
            {
                // не похоже на encoded-word — копируем как есть и двигаемся
                out.push_back(s[p]);
                i = p + 1;
                continue;
            }

            std::string enc = s.substr(p1 + 1, p2 - p1 - 1);
            std::string payload = s.substr(p2 + 1, p3 - p2 - 1);

            if (enc == "B" || enc == "b")
            {
                auto bytes = decodeBase64(payload);
                if (!bytes.empty())
                {
                    out.append(reinterpret_cast<const char*>(bytes.data()), bytes.size());
                    i = p3 + 2;
                    continue;
                }
            }

            // если не декоднули — оставим как есть
            out.append(s, p, (p3 + 2) - p);
            i = p3 + 2;
        }

        return out;
    }

    static bool isBadPathChar(unsigned char c)
    {
        switch (c)
        {
            case '/': case '\\': case ':': case '*': case '?':
            case '"': case '<': case '>': case '|':
                return true;
            default:
                return false;
        }
    }

    static std::string normalizeDirUtf8(const std::string& s)
    {
        std::string out;
        out.reserve(s.size());

        bool prevUnderscore = false;

        for (unsigned char c : s)
        {
            if (c == '\r' || c == '\n' || c == '\t' || c == ' ')
            {
                if (!prevUnderscore)
                {
                    out.push_back('_');
                    prevUnderscore = true;
                }
                continue;
            }

            if (c < 0x20) // прочие control
                continue;

            if (isBadPathChar(c))
            {
                if (!prevUnderscore)
                {
                    out.push_back('_');
                    prevUnderscore = true;
                }
                continue;
            }

            out.push_back(static_cast<char>(c));
            prevUnderscore = false;
        }

        // trim '_' по краям
        while (!out.empty() && out.front() == '_') out.erase(out.begin());
        while (!out.empty() && out.back()  == '_') out.pop_back();

        if (out.empty()) out = "NO_SUBJECT";
        if (out.size() > 64) out.resize(64);

        return out;
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

    static bool ensureDir(const std::string& path)
    {
        if (::mkdir(path.c_str(), 0755) == 0)
            return true;
        return errno == EEXIST;
    }

// ----- RFC2047 (B only) -----
    static bool decodeRfc2047(const std::string& in, std::string& out)
    {
        out.clear();
        if (in.substr(0,2) != "=?") return false;

        auto p1 = in.find('?', 2);
        auto p2 = in.find('?', p1 + 1);
        auto p3 = in.find("?=", p2 + 1);
        if (p1==std::string::npos || p2==std::string::npos || p3==std::string::npos)
            return false;

        std::string enc = in.substr(p1+1, p2-p1-1);
        if (enc != "B" && enc != "b") return false;

        std::string payload = in.substr(p2+1, p3-p2-1);
        auto decoded = decodeBase64(payload);
        if (decoded.empty()) return false;

        out.assign(reinterpret_cast<const char*>(decoded.data()), decoded.size());
        return true;
    }

    static std::string normalizeDir(const std::string& s)
    {
        std::string out;
        for (unsigned char c : s)
        {
            if (std::isalnum(c) || c=='_' || c=='-')
                out.push_back(c);
            else if (std::isspace(c))
                out.push_back('_');
        }
        if (out.empty()) out = "NO_SUBJECT";
        if (out.size() > 64) out.resize(64);
        return out;
    }

// --------------------------------

    CmdExportUnreadToFolders::CmdExportUnreadToFolders(
            std::string host, int port,
            std::string user, std::string pass,
            ImapFolder folder, std::string outDir,
            ExportStrategy strategy)
            : host_(std::move(host))
            , port_(port)
            , user_(std::move(user))
            , pass_(std::move(pass))
            , folders_{ std::move(folder) }
            , outDir_(std::move(outDir))
            , strategy_(strategy)
    {
    }

    std::string CmdExportUnreadToFolders::title() const
    {
        std::string scope;

        if (folders_.size() > 1)
            scope = "Все папки: ";
        else if (!folders_.empty())
            scope = "Папка \"" + folders_.front().displayName + "\": ";

        return scope + "экспорт → " + outDir_;
    }



    CommandResult CmdExportUnreadToFolders::execute()
    {
        //ImapClient imap(true);

        auto exit = []{my::pause();
            return CommandResult::ExitMenu;};

        // Экспортер: стратегия приходит из меню (BySubject / ByExtension)
        AttachmentExporter exporter(
                outDir_,
                strategy_,
                config::attachmentGroups()
        );

        // Группы нужны только для ByExtension, но вызывать можно всегда.
        exporter.loadGroups();

        // Subject rules: загружаем один раз
        SubjectFolderResolver subjResolver(config::subjectRules());
        subjResolver.load();

        ImapSession ses;
        std::string err;

        if (!imapOpen(ses, host_, port_, err)) return exit();
        if (!imapLogin(ses, user_, pass_, err)) {
            imapClose(ses);
            exit();
        }


        for (const auto& folder : folders_) {
            std::vector<std::string> uids;

            if (!ImapClient::searchUnreadUids(ses, folder.imapName, uids, err)) {
                std::cout << "[IMAP] search in " + folder.displayName + "failed: " << err << "\n";
            }

            if (uids.empty()) {
                std::cout << "[IMAP] непрочитанных писем " + folder.displayName + " нет\n";
            }

            ensureDir(outDir_);

            const size_t total = uids.size();
            size_t index = 0;

            for (const auto &uid: uids) {
                ++index;

                std::cout << "[IMAP] "
                          << index << "/" << total
                          << " uid=" << uid << "\n";


                std::string raw;

                if (!ImapClient::fetchRawEmlByUid(ses, uid, raw, err)) {
                    std::cout << "[IMAP] FETCH uid=" << uid << " failed\n";
                    continue;
                }

                // --- Subject ---
                std::string headers;
                auto p = raw.find("\r\n\r\n");
                headers = (p != std::string::npos) ? raw.substr(0, p) : raw;

                std::string subject = getHeaderValue(headers, "Subject");
                subject = decodeRfc2047_B_words(subject);

                std::string from = getHeaderValue(headers, "From");
                from = decodeRfc2047_B_words(from);

                if (from.empty())
                    from = folder.displayName; // fallback


                // "маска темы" по правилам (ШТ, Служебные, ...)
                subject = subjResolver.resolve(subject);

                // --- Attachments: собираем список в память, запись делает exporter ---
                std::vector<MimePart> parts;
                if (!EmlParser::splitToPartsFromRaw(raw, parts, err))
                    continue;

                std::vector<ExportedAttachment> attachments;
                attachments.reserve(parts.size());

                for (const auto &part: parts) {
                    ParsedPart pp = EmlParser::parsePart(part);
                    if (!pp.isAttachment)
                        continue;

                    std::vector<uint8_t> data;

                    if (pp.transferEncoding == "base64") {
                        data = decodeBase64(pp.body);
                    } else if (pp.transferEncoding == "quoted-printable") {
                        data = decodeQuotedPrintable(pp.body); // ⬅️ ОБЯЗАТЕЛЬНО
                    } else if (pp.transferEncoding.empty() ||
                               pp.transferEncoding == "7bit" ||
                               pp.transferEncoding == "8bit" ||
                               pp.transferEncoding == "binary") {
                        // сохраняем "как есть"
                        data.assign(pp.body.begin(), pp.body.end());
                    } else {
                        // неизвестное кодирование — логируем и пропускаем
                        std::cout << "[SKIP] " << pp.filename
                                  << " unsupported encoding="
                                  << pp.transferEncoding << "\n";
                        continue;
                    }

                    if (data.empty()) {
                        std::cout << "[SKIP] " << pp.filename
                                  << " decoded empty\n";
                        continue;
                    }

                    ExportedAttachment a;
                    a.filename = pp.filename;
                    a.data = std::move(data);

                    attachments.push_back(std::move(a));
                }


                // ВАЖНО:
                // - если вложений нет → exporter ничего не создаст (и папок не будет)
                // - BySubject: subject/uid_xxx
                // - ByExtension: общие папки по группам, dedupByHash
                std::cout
                        << "\n==================================================\n"
                        << "📨 От: " << from << "\n"
                        << "--------------------------------------------------\n";

                if (attachments.empty()) {
                    std::cout << "  (вложений нет)\n";
                }

                exporter.exportAttachments(subject, uid, attachments);

                std::cout
                        << "==================================================\n";


                // mark seen
                ImapClient::markSeenByUid(ses, uid, err);
            }
        }


        imapClose(ses);
        return exit();
    }

    CmdExportUnreadToFolders::CmdExportUnreadToFolders(
            std::string host, int port,
            std::string user, std::string pass,
            const std::vector<ImapFolder>& folders, std::string outDir,
            ExportStrategy strategy)
            : host_(std::move(host))
            , port_(port)
            , user_(std::move(user))
            , pass_(std::move(pass))
            , folders_(folders)
            , outDir_(std::move(outDir))
            , strategy_(strategy)
    {
    }


} // namespace astra_mail
