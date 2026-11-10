#include "menu/MailCommandAccess.h"
#include "CmdExportUnreadToFolders.h"
#include "core/mail/AccountAccess.h"
#include "core/thunderbird/ThunderbirdSettings.h"
#include "Utils/SensitiveString.h"
#include "core/idle/IdleBackground.h"
#include "Utils/ImapClient.h"
#include "Utils/EmlParser.h"
#include "Utils/Base64.h"
#include "Resolver/Resolvers/SubjectFolderResolver.h"
#include "core/paths/AstraMailPaths.h"
#include "UtilsFS/FsCompat.h"
#include <iostream>
#include <sys/stat.h>
#include <cerrno>
#include "UtilsFS/FsUtils.h"
#include "Utils/Utils.h"
#include "Configs/Confg/ConfigPaths.h"
#include <map>
#include <set>
#include <string>
#include <vector>
#include <fstream>



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

    /** Сканирует inbox: uid -> (subject, полный путь к папке uid_xxx). Только BySubject-структура. */
    static std::map<std::string, std::pair<std::string, fs::path>> buildInboxUidMap()
    {
        std::map<std::string, std::pair<std::string, fs::path>> map;
        fs::path inboxRoot = getInboxRoot();
        std::error_code ec;
        if (!fs::exists(inboxRoot, ec))
            return map;
        for (const auto& subjEntry : fs::directory_iterator(inboxRoot, ec))
        {
            if (ec || ! fs::is_directory(subjEntry))
                continue;
            std::string subject = subjEntry.path().filename().string();
            for (const auto& uidEntry : fs::directory_iterator(subjEntry.path(), ec))
            {
                if (ec || !fs::is_directory(uidEntry))
                    continue;
                std::string name = uidEntry.path().filename().string();
                if (name.size() > 4 && name.compare(0, 4, "uid_") == 0)
                {
                    std::string uid = name.substr(4);
                    map[uid] = std::make_pair(subject, uidEntry.path());
                }
            }
        }
        return map;
    }

    /** Читает все файлы из папки письма в inbox (subject/uid_xxx) в вектор вложений для экспорта по типам. */
    static std::vector<ExportedAttachment> readInboxUidFolderAsAttachments(const fs::path& uidFolderPath)
    {
        std::vector<ExportedAttachment> out;
        std::error_code ec;
        if (!fs::is_directory(uidFolderPath, ec))
            return out;
        for (const auto& entry : fs::directory_iterator(uidFolderPath, ec))
        {
            if (ec)
                continue;
            if (!fs::is_regular_file(entry.path(), ec))
                continue;
            fs::path p = entry.path();
            std::ifstream f(p, std::ios::binary);
            if (!f)
                continue;
            ExportedAttachment a;
            a.filename = p.filename().string();
            a.data.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
            out.push_back(std::move(a));
        }
        return out;
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
            , accountContext_(ThunderbirdSettings::instance().secretContext())
            , folders_{ std::move(folder) }
            , outDir_(std::move(outDir))
            , strategy_(strategy)
    {
        clearSensitiveString(pass);
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



    bool CmdExportUnreadToFolders::runExport(const std::string& host, int port,
                                              const std::string& user, const std::string& pass,
                                              const std::vector<ImapFolder>& folders,
                                              const std::string& outDir,
                                              ExportStrategy strategy,
                                              std::ostream* log)
    {
        MailOperationScope operation;
        if (log) *log << "[Экспорт] Запуск: " << outDir
                      << (strategy.mode == ExportMode::BySubject ? " (по темам)" : " (по типам вложений)")
                      << "\n";

        AttachmentExporter exporter(
                outDir,
                strategy,
                config::attachmentGroups()
        );
        if (!exporter.loadGroups())
        {
            if (log) *log << "[Экспорт] Ошибка загрузки attachment_groups.conf\n";
            return false;
        }

        SubjectFolderResolver subjResolver(config::subjectRules());
        subjResolver.load();

        ImapSession ses;
        std::string err;

        // При экспорте на флешку используем inbox: по темам — копируем 1:1; по типам вложений — читаем папки и группируем по правилам.
        std::map<std::string, std::pair<std::string, fs::path>> inboxUidMap;
        fs::path outPath(outDir);
        fs::path inboxPath = getInboxRoot();
        const bool exportToFlash = !fs::equivalent(outPath, inboxPath);
        if (exportToFlash)
            inboxUidMap = buildInboxUidMap();

        std::set<std::string> processedUids;
        std::vector<fs::path> toDeleteFromInbox;
        size_t totalExported = 0;

        const auto exportLocalInboxOnly = [&]()
        {
            if (!exportToFlash)
                return;

            if (inboxUidMap.empty())
            {
                if (log) *log << "[INBOX] Локальных писем для переноса нет.\n";
                return;
            }

            ensureDir(outDir);
            for (const auto& kv : inboxUidMap)
            {
                const std::string& uid = kv.first;
                const fs::path& srcPath = kv.second.second;
                const std::string& subject = kv.second.first;

                if (log) *log << "[INBOX] uid=" << uid << " (из локального inbox)\n";
                if (strategy.mode == ExportMode::BySubject)
                {
                    fs::path destDir = fs::path(outDir) / subject / srcPath.filename();
                    std::error_code ec;
                    fs::create_directories(destDir.parent_path(), ec);
                    if (ec)
                    {
                        if (log) *log << "[INBOX] Ошибка создания папки " << destDir.parent_path() << ": " << ec.message() << "\n";
                    }
                    else
                    {
                        fs::copy(srcPath, destDir, fs::copy_options::recursive, ec);
                        if (ec)
                        {
                            if (log) *log << "[INBOX] Ошибка копирования " << srcPath << " → " << destDir << ": " << ec.message() << "\n";
                        }
                        else
                        {
                            if (log) *log << "[INBOX] перенесено на флешку (по темам): " << uid << "\n";
                            ++totalExported;
                            toDeleteFromInbox.push_back(srcPath);
                        }
                    }
                }
                else
                {
                    std::vector<ExportedAttachment> attachments = readInboxUidFolderAsAttachments(srcPath);
                    if (!attachments.empty())
                    {
                        exporter.exportAttachments("", uid, attachments);
                        if (log) *log << "[INBOX] перенесено на флешку (по типам вложений): " << uid << "\n";
                        ++totalExported;
                        toDeleteFromInbox.push_back(srcPath);
                    }
                }
            }
        };

        // --- IMAP доступен? Если нет, но есть локальный inbox → переносим офлайн ---
        bool imapOk = true;
        if (!imapOpen(ses, host, port, err))
        {
            imapOk = false;
            if (log) *log << "[Экспорт] IMAP подключение: " << err << "\n";
        }
        else if (!imapLogin(ses, user, pass, err))
        {
            imapOk = false;
            if (log) *log << "[Экспорт] IMAP логин: " << err << "\n";
            imapClose(ses);
        }

        if (!imapOk)
        {
            if (log) *log << "[Экспорт] Сервер недоступен. Переносим из локального inbox (офлайн).\n";
            exportLocalInboxOnly();
        }
        else
        {
            if (folders.empty())
            {
                if (log) *log << "[Экспорт] Список IMAP-папок пуст. Проверьте imap_folders.conf\n";
                imapClose(ses);
                return false;
            }

            for (const auto& folder : folders) {
                std::vector<std::string> uids;
                if (!ImapClient::searchUnreadUids(ses, folder.imapName, uids, err)) {
                    if (log) *log << "[IMAP] search in " << folder.displayName << " failed: " << err << "\n";
                }
                if (uids.empty() && !exportToFlash)
                    continue;

                ensureDir(outDir);
                const size_t total = uids.size();
                size_t index = 0;

                for (const auto &uid: uids) {
                    ++index;
                    if (log) *log << "[IMAP] " << index << "/" << total << " uid=" << uid << "\n";

                    auto it = inboxUidMap.find(uid);
                    if (it != inboxUidMap.end())
                    {
                        const fs::path& srcPath = it->second.second;
                        if (strategy.mode == ExportMode::BySubject)
                        {
                            const std::string& subject = it->second.first;
                            fs::path destDir = fs::path(outDir) / subject / srcPath.filename();
                            std::error_code ec;
                            fs::create_directories(destDir.parent_path(), ec);
                            if (ec) {
                                if (log) *log << "[INBOX] Ошибка создания папки " << destDir.parent_path() << ": " << ec.message() << "\n";
                            } else {
                                fs::copy(srcPath, destDir, fs::copy_options::recursive, ec);
                                if (ec) {
                                    if (log) *log << "[INBOX] Ошибка копирования " << srcPath << " → " << destDir << ": " << ec.message() << "\n";
                                } else {
                                    if (log) *log << "[INBOX] перенесено на флешку (по темам): " << uid << "\n";
                                    ++totalExported;
                                    processedUids.insert(uid);
                                    toDeleteFromInbox.push_back(srcPath);
                                }
                            }
                        }
                        else
                        {
                            std::vector<ExportedAttachment> attachments = readInboxUidFolderAsAttachments(srcPath);
                            if (!attachments.empty())
                            {
                                exporter.exportAttachments("", uid, attachments);
                                if (log) *log << "[INBOX] перенесено на флешку (по типам вложений): " << uid << "\n";
                                ++totalExported;
                                processedUids.insert(uid);
                                toDeleteFromInbox.push_back(srcPath);
                            }
                        }
                        continue;
                    }

                    std::string raw;
                    if (!ImapClient::fetchRawEmlByUid(ses, uid, raw, err)) {
                        if (log) *log << "[IMAP] FETCH uid=" << uid << " failed\n";
                        continue;
                    }

                    std::string headers;
                    auto p = raw.find("\r\n\r\n");
                    headers = (p != std::string::npos) ? raw.substr(0, p) : raw;

                    std::string subject = getHeaderValue(headers, "Subject");
                    subject = decodeRfc2047_B_words(subject);
                    std::string from = getHeaderValue(headers, "From");
                    from = decodeRfc2047_B_words(from);
                    if (from.empty())
                        from = folder.displayName;

                    subject = subjResolver.resolve(subject);

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
                            data = decodeQuotedPrintable(pp.body);
                        } else if (pp.transferEncoding.empty() ||
                                   pp.transferEncoding == "7bit" ||
                                   pp.transferEncoding == "8bit" ||
                                   pp.transferEncoding == "binary") {
                            data.assign(pp.body.begin(), pp.body.end());
                        } else {
                            if (log) *log << "[SKIP] " << pp.filename << " unsupported encoding=" << pp.transferEncoding << "\n";
                            continue;
                        }
                        if (data.empty()) {
                            if (log) *log << "[SKIP] " << pp.filename << " decoded empty\n";
                            continue;
                        }

                        ExportedAttachment a;
                        a.filename = pp.filename;
                        a.data = std::move(data);
                        attachments.push_back(std::move(a));
                    }

                    if (log) {
                        *log << "\n==================================================\n"
                             << "📨 От: " << from << "\n--------------------------------------------------\n";
                        if (attachments.empty()) *log << "  (вложений нет)\n";
                        *log << "==================================================\n";
                    }

                    exporter.exportAttachments(subject, uid, attachments);
                    ImapClient::markSeenByUid(ses, uid, err);
                    ++totalExported;
                    processedUids.insert(uid);
                }
            }

            // Письма, уже скачанные в inbox (помечены прочитанными), но ещё не перенесённые на флешку
            if (exportToFlash && !inboxUidMap.empty()) {
                ensureDir(outDir);
                for (const auto& kv : inboxUidMap) {
                    const std::string& uid = kv.first;
                    if (processedUids.count(uid))
                        continue;
                    const fs::path& srcPath = kv.second.second;
                    const std::string& subject = kv.second.first;
         
                    if (log) *log << "[INBOX] uid=" << uid << " (из локального inbox)\n";
                    if (strategy.mode == ExportMode::BySubject) {
                        fs::path destDir = fs::path(outDir) / subject / srcPath.filename();
                        std::error_code ec;
                        fs::create_directories(destDir.parent_path(), ec);
                        if (ec) {
                            if (log) *log << "[INBOX] Ошибка создания папки " << destDir.parent_path() << ": " << ec.message() << "\n";
                        } else {
                            fs::copy(srcPath, destDir, fs::copy_options::recursive, ec);
                            if (ec) {
                                if (log) *log << "[INBOX] Ошибка копирования " << srcPath << " → " << destDir << ": " << ec.message() << "\n";
                            } else {
                                if (log) *log << "[INBOX] перенесено на флешку (по темам): " << uid << "\n";
                                ++totalExported;
                                toDeleteFromInbox.push_back(srcPath);
                            }
                        }
                    } else {
                        std::vector<ExportedAttachment> attachments = readInboxUidFolderAsAttachments(srcPath);
                        if (!attachments.empty()) {
                            exporter.exportAttachments("", uid, attachments);
                            if (log) *log << "[INBOX] перенесено на флешку (по типам вложений): " << uid << "\n";
                            ++totalExported;
                            toDeleteFromInbox.push_back(srcPath);
                        }
                    }
                }
            }

            imapClose(ses);
        }

        for (const fs::path& p : toDeleteFromInbox) {
            std::error_code ec;
            fs::remove_all(p, ec);
            if (ec && log)
                *log << "[INBOX] Не удалось удалить " << p << ": " << ec.message() << "\n";
        }

        if (log && totalExported == 0)
            *log << "[Экспорт] Нет непрочитанных писем с вложениями для экспорта.\n";

        return true;
    }

    bool CmdExportUnreadToFolders::runExportFromLocalInbox(const std::string& outDir,
                                                           ExportStrategy strategy,
                                                           std::ostream* log)
    {
        MailOperationScope operation(std::try_to_lock);
        if (!operation.allowed()) { if (log) *log << mailBusyMessage() << "\n"; return false; }
        if (log) *log << "[Экспорт][OFFLINE] Перенос из локального inbox → " << outDir
                      << (strategy.mode == ExportMode::BySubject ? " (по темам)" : " (по типам вложений)")
                      << "\n";

        AttachmentExporter exporter(
                outDir,
                strategy,
                config::attachmentGroups()
        );
        if (!exporter.loadGroups())
        {
            if (log) *log << "[Экспорт][OFFLINE] Ошибка загрузки attachment_groups.conf\n";
            return false;
        }

        auto inboxUidMap = buildInboxUidMap();
        if (inboxUidMap.empty())
        {
            if (log) *log << "[Экспорт][OFFLINE] Локальных писем для переноса нет.\n";
            return true;
        }

        ensureDir(outDir);

        std::vector<fs::path> toDeleteFromInbox;
        size_t totalExported = 0;

        for (const auto& kv : inboxUidMap)
        {
            const std::string& uid = kv.first;
            const std::string& subject = kv.second.first;
            const fs::path& srcPath = kv.second.second;

            if (strategy.mode == ExportMode::BySubject)
            {
                fs::path destDir = fs::path(outDir) / subject / srcPath.filename();
                std::error_code ec;
                fs::create_directories(destDir.parent_path(), ec);
                if (ec)
                {
                    if (log) *log << "[INBOX][OFFLINE] Ошибка создания папки " << destDir.parent_path()
                                  << ": " << ec.message() << "\n";
                    continue;
                }
                fs::copy(srcPath, destDir, fs::copy_options::recursive, ec);
                if (ec)
                {
                    if (log) *log << "[INBOX][OFFLINE] Ошибка копирования " << srcPath << " → " << destDir
                                  << ": " << ec.message() << "\n";
                    continue;
                }
                ++totalExported;
                toDeleteFromInbox.push_back(srcPath);
            }
            else
            {
                std::vector<ExportedAttachment> attachments = readInboxUidFolderAsAttachments(srcPath);
                if (!attachments.empty())
                {
                    exporter.exportAttachments("", uid, attachments);
                    ++totalExported;
                    toDeleteFromInbox.push_back(srcPath);
                }
            }
        }

        for (const fs::path& p : toDeleteFromInbox)
        {
            std::error_code ec;
            fs::remove_all(p, ec);
            if (ec && log)
                *log << "[INBOX][OFFLINE] Не удалось удалить " << p << ": " << ec.message() << "\n";
        }

        if (log && totalExported == 0)
            *log << "[Экспорт][OFFLINE] Нет писем для переноса.\n";

        return true;
    }

    CommandResult CmdExportUnreadToFolders::execute()
    {
        MailOperationScope operation(std::try_to_lock);
        if (!mailCommandAllowed(operation.allowed())) return CommandResult::Continue;
        IdleBackground::BusyGuard busy;
        const auto& tb = ThunderbirdSettings::instance();
        if (accountContext_ != tb.secretContext() || !tb.hasMailPassword()) {
            std::cout << "Аккаунт изменён или заблокирован. Откройте экспорт заново после ввода PIN.\n";
            my::pause(); return CommandResult::Continue;
        }
        bool ok = runExport(host_, port_, user_, tb.mailPassword(), folders_, outDir_, strategy_, &std::cout);
        my::pause();
        return CommandResult::ExitMenu;
    }

    CmdExportUnreadToFolders::CmdExportUnreadToFolders(
            std::string host, int port,
            std::string user, std::string pass,
            const std::vector<ImapFolder>& folders, std::string outDir,
            ExportStrategy strategy)
            : host_(std::move(host))
            , port_(port)
            , user_(std::move(user))
            , accountContext_(ThunderbirdSettings::instance().secretContext())
            , folders_(folders)
            , outDir_(std::move(outDir))
            , strategy_(strategy)
    {
        clearSensitiveString(pass);
    }


} // namespace astra_mail
