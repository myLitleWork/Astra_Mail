#include "CmdSendMailsFromPath.h"
#include "MailFolderScanner.h"
#include "MailFolderReport.h"
#include "MailSendPipeline.h"
#include "src/Utils/Utils.h"
#include "ThunderbirdSettings.h"
#include "Resolver/Resolvers/ContactsRecipientResolver.h"

#include <iostream>

#include "UtilsFS/FsCompat.h"


#include <stdexcept>
#include <fstream>


static fs::path getOutboxRoot()
{
    const char* home = getenv("HOME");
    if (!home)
        throw std::runtime_error("HOME not set");

    fs::path root = fs::path(home) / ".local/share/AstraMail/outbox";

    fs::create_directories(root);
    return root;
}



fs::path makeUnique(
        const fs::path& dir,
        fs::path name)
{
    while (fs::exists(dir / name))
    {
        name = name.stem().string() + "-" + name.extension().string();
    }
    return dir / name;
}


//
// Копирование папки письма целиком
// targetRoot/<recipient>/...
// Если папка адресата уже есть — файлы ДОБАВЛЯЮТСЯ
//






namespace astra_mail
{

    static std::string fileHash(const fs::path& p)
    {
        std::ifstream f(p, std::ios::binary);
        if (!f) return {};

        std::uint64_t h = 1469598103934665603ULL; // FNV-1a
        char buf[4096];

        while (f.read(buf, sizeof(buf)) || f.gcount())
        {
            for (std::streamsize i = 0; i < f.gcount(); ++i)
            {
                h ^= (unsigned char)buf[i];
                h *= 1099511628211ULL;
            }
        }
        return std::to_string(h);
    }

    static fs::path copyMailFolderToOutbox_1to1(
            const fs::path& srcFolder,
            const fs::path& outboxRoot,
            const std::string& recipient)
    {
        fs::path dstFolder = outboxRoot / recipient;
        fs::create_directories(dstFolder);

        if (!fs::is_directory(srcFolder))
        {
            std::cout << "[ERROR] Каталог письма недоступен: "
                      << srcFolder << "\n";
            return dstFolder;
        }

        std::error_code ec;

        for (const auto& it : fs::directory_iterator(srcFolder, ec))
        {
            if (ec) break;
            if (!fs::is_regular_file(it)) continue;

            fs::path filename = it.path().filename();

            fs::path dst = makeUnique(dstFolder, filename);

            fs::copy_file(it.path(), dst, fs::copy_options::none, ec);

            if (ec)
                std::cout << "[WARN] Не скопирован: "
                          << it.path() << ": " << ec.message() << "\n";
        }

        return dstFolder;
    }



    static fs::path moveMailFolderToOutbox_1to1(
            const fs::path& srcFolder,
            const fs::path& outboxRoot,
            const std::string& recipient)
    {
        fs::path dstFolder = outboxRoot / recipient;
        fs::create_directories(dstFolder);

        if (!fs::is_directory(srcFolder))
        {
            std::cout << "[ERROR] Каталог письма недоступен: "
                      << srcFolder << "\n";
            return dstFolder;
        }

        std::error_code ec;
        bool allMoved = true;

        for (const auto& it : fs::directory_iterator(srcFolder, ec))
        {
            if (ec) break;
            if (!fs::is_regular_file(it)) continue;

            fs::path src  = it.path();
            fs::path name = src.filename();
            fs::path dst  = dstFolder / name;

            if (fs::exists(dst))
            {
                if (fileHash(src) == fileHash(dst))
                {
                    fs::remove(src, ec);
                    if (ec) allMoved = false;
                    continue;
                }
                dst = makeUnique(dstFolder, name);
            }

            fs::rename(src, dst, ec);

            if (ec)
            {
                ec.clear();
                fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec);
                if (!ec)
                    fs::remove(src, ec);
            }

            if (ec)
            {
                allMoved = false;
                std::cout << "[WARN] Не перемещён: "
                          << src << ": " << ec.message() << "\n";
            }
        }

        // если всё перемещено — удаляем папку
        if (allMoved)
        {
            std::error_code rm_ec;
            if (fs::is_empty(srcFolder, rm_ec))
                fs::remove(srcFolder, rm_ec);
        }

        return dstFolder;
    }



    MailSendPipeline createPipelineFromThunderbird()
    {
        auto& tb = ThunderbirdSettings::instance();

        if (!tb.active().valid())
            throw std::runtime_error("Thunderbird account not selected");

        const auto& a = tb.active();

        const auto& imap = tb.imapServers().at(a.imapServerId);
        const auto& smtp = tb.smtpServers().at(a.smtpServerId);
        const auto& id   = tb.identities().at(a.identityId);


        return { smtp.host, smtp.port,
                 imap.host, imap.port,
                 id.email, tb.mailPassword() };  // ← пароль берётся из SMTP


    }



    CommandResult CmdSendMailsFromPath::execute()
        {

            std::string mountPoint = path_;

            // 2. Сканирование флешки
            auto resolver = std::make_shared<ContactsRecipientResolver>(ContactsManager::instance());
            MailFolderScanner scanner({true,true,false}, resolver);
            auto flashResult = scanner.scanMount(mountPoint);
            MailFolderReport::print(std::cout, flashResult);

            if(flashResult.mails.empty())
            {
                my::pause();
                return CommandResult::ExitMenu;
            }

            std::vector<MailFolderInfo> locals;
            locals.reserve(flashResult.mails.size());

            auto rootPath = getOutboxRoot();

            for (auto& mail : flashResult.mails)
            {
                fs::path newFolder = moveMailFolderToOutbox_1to1(
                        mail.folderPath,
                        rootPath,
                        mail.recipient);

                MailFolderInfo local = mail;
                local.folderPath = newFolder.string();

                for (auto& a : local.attachments)
                {
                    a.fullPath = (newFolder / fs::path(a.fullPath).filename()).string();
                }

                locals.push_back(local);
            }

            std::cout << "\nВсе письма перемещены в локальную папку:\n"
                      << path_ << "\n"
                      << "Флешку можно извлечь.\n";

            auto localResult = scanner.scanMount(rootPath);

            auto& tb = ThunderbirdSettings::instance();
            const auto& a = tb.active();

            MailSendPipeline pipe = createPipelineFromThunderbird();

        // SMTP создаётся ИЗ пайплайна
        SmtpClient smtp(pipe.smtpHost(), pipe.smtpPort());
        smtp.setCredentials(pipe.smtpUser(), pipe.smtpPass());

        SmtpClient::Session ses;
        std::string err;

        if (!smtp.openSession(ses, err))
        {
            std::cout << "\n[SMTP] session open failed: " << err << "\n";
            my::pause();
        }

        for (auto& mail : localResult.mails)
        {
            std::cout << "\n[ОТПРАВКА] " << mail.recipient << " ...";
            std::cout.flush();

            std::string log;

            bool ok = pipe.send(mail, log, smtp, ses);

            std::cout << "\r";

            if (ok)
            {
                std::cout << "[ОТПРАВЛЕНО] " << mail.recipient << " ✓    ";
                fs::remove_all(mail.folderPath);
            }
            else
            {
                std::cout << "[НЕ ОТПРАВЛЕНО] " << mail.recipient << " ✗    ";
            }

            std::cout.flush();
        }

        smtp.closeSession(ses);


            my::pause();
            return CommandResult::ExitMenu;
        }

    CmdSendMailsFromPath::CmdSendMailsFromPath(const std::string &path) :
            ICommand(), path_(path)
    {

    }

    std::string CmdSendMailsFromPath::title() const {
         return "Отправить письма из " + path_;
    }
}




