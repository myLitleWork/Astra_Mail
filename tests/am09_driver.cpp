// Owned descriptor, MIME, SMTP and APPEND checks, linked to actual app libraries.
#include "core/mime/MimeTemporaryFile.h"
#include "core/mime/MimeFileBuilder.h"
#include "core/thunderbird/ThunderbirdSettings.h"
#include "core/profile/LocalProfileCache.h"
#include "menu/Commands/Mails/CmdSendMailsFromOutbox.h"
#include "Utils/SmtpClient.h"
#include "Utils/ImapClient.h"
#include <dirent.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <unistd.h>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace astra_mail;
static size_t descriptors() {
    DIR* dir=::opendir("/proc/self/fd");size_t count=0;while(::readdir(dir))++count;::closedir(dir);return count;
}
static EmailMessage message(const std::string& marker,const std::string& home) {
    EmailMessage value;value.from="sender@example.invalid";value.to="target@example.invalid";
    value.subject=marker;value.body="synthetic body "+marker+"\r\n.\r\nend";
    const auto path=home+"/owned-attachment.bin";
    std::ofstream file(path,std::ios::binary);file << marker;
    for(unsigned i=0;i<1024;++i) file.put(static_cast<char>(i%256));file.close();
    EmailAttachment attachment;attachment.filename="synthetic.bin";attachment.path=path;value.attachments.push_back(attachment);
    return value;
}
int main(int argc,char** argv) {
    try {
        if(argc>1 && std::string(argv[1])=="probe") {
            struct stat info{};
            return ::fstat(std::stoi(argv[2]),&info)==0 && info.st_dev==std::stoull(argv[3]) && info.st_ino==std::stoull(argv[4])?1:0;
        }
        if(argc<5) return 2;
        const std::string mode=argv[1],marker=argv[4],home=std::getenv("HOME");
        TbProfile profile;profile.id="synthetic-am09";
        LocalMailSettings settings;settings.profileId=profile.id;settings.accountId="synthetic";
        settings.email=settings.userName="sender@example.invalid";settings.smtpHost=settings.imapHost="127.0.0.1";
        settings.smtpPort=std::stoi(argv[2]);settings.imapPort=std::stoi(argv[3]);
        auto& tb=ThunderbirdSettings::instance();
        if(!tb.loadFromLocalCache(profile,settings) || !tb.setMailPassword("synthetic-only")) return 1;
        auto limits=net::currentSettings();limits.smtpCommandMs=15000;limits.smtpFinalMs=1200;
        limits.imapCommandMs=500;limits.imapAppendMs=800;net::configure(limits);
        const auto before=descriptors();
        if(mode=="outbox") {
            SendCycleReport result;
            const bool ok=CmdSendMailsFromOutbox::runSendFromOutbox(&std::cout,&result);
            std::cout<<"COMPLETE="<<ok<<" QUEUED="<<result.queuedMessages<<" UNKNOWN="<<result.unconfirmedRecipients<<"\n";
        } else if(mode=="owned" || mode=="restrictive" || mode=="exec") {
            const mode_t mask=mode=="restrictive"?0777:0000;
            const mode_t previous=::umask(mask);
            MimeTemporaryFile mime;std::string error;
            const bool created=mime.create(error);
            const mode_t after=::umask(previous);
            if(after!=mask) throw std::runtime_error("temporary creation changed umask");
            if(!created || !MimeFileBuilder::buildToStream(message(marker,home),mime.stream(),error)) throw std::runtime_error(error);
            struct stat info{};const int fd=::fileno(mime.stream());
            if(::fstat(fd,&info)!=0) return 1;
            std::cout<<"READY_FD="<<fd<<" MODE="<<(info.st_mode&0777)<<" LINKS="<<info.st_nlink
                     <<" CLOEXEC="<<bool(::fcntl(fd,F_GETFD)&FD_CLOEXEC)<<"\n"<<std::flush;
            if(mode=="exec") {
                const auto number=std::to_string(fd),device=std::to_string(info.st_dev),inode=std::to_string(info.st_ino);
                const pid_t child=::fork();
                if(child==0) {::execl(argv[0],argv[0],"probe",number.c_str(),device.c_str(),inode.c_str(),nullptr);::_exit(127);}
                int status=0;if(child<0 || ::waitpid(child,&status,0)<0) return 1;
                std::cout<<"EXEC_PRIVATE="<<(WIFEXITED(status)&&WEXITSTATUS(status)==0)<<"\n";
            } else {
                std::string control;if(!std::getline(std::cin,control)) return 1;
                SmtpClient smtp("127.0.0.1",settings.smtpPort);smtp.setCredentials(settings.userName,tb.mailPassword());
                SmtpClient::Session session;
                const bool ok=smtp.openSession(session,error) && smtp.deliver(session,mime.stream(),{"target@example.invalid"}).complete();
                ImapClient imap(false);
                const bool copied=ok && imap.imapAppendSentFromStream(mime.stream(),error);
                SmtpClient::closeSession(session);
                std::cout<<"SMTP_OK="<<ok<<" APPEND_OK="<<copied<<" ERROR="<<error<<"\n";
            }
        } else if(mode=="lifetime" || mode=="fault-loop") {
            unsigned built=0;
            const auto value=message(marker,home);
            for(unsigned i=0;i<80;++i) {
                try {MimeTemporaryFile mime;std::string error;
                    if(mime.create(error) && MimeFileBuilder::buildToStream(value,mime.stream(),error)) ++built;
                    if(mode=="lifetime") throw std::runtime_error("synthetic unwind");
                } catch(const std::runtime_error&) {}
            }
            std::cout<<"BUILT="<<built<<"\n";
        } else if(mode=="missing") {
            MimeTemporaryFile mime;std::string error;auto value=message(marker,home);
            value.attachments[0].path=home+"/synthetic-missing";
            const bool ok=mime.create(error) && MimeFileBuilder::buildToStream(value,mime.stream(),error);
            std::cout<<"BUILD_OK="<<ok<<" ERROR="<<error<<"\n";
        } else return 2;
        std::cout<<"FD_DELTA="<<(descriptors()-before)<<"\n";
        return 0;
    } catch(const std::exception& error) {std::cerr<<"AM09_DRIVER_FAILED: "<<error.what()<<"\n";return 1;}
}
