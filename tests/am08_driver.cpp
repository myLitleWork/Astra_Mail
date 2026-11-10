// Actual command/background/contact implementations; every fixture is synthetic.
#include "core/mail/AccountAccess.h"
#include "Utils/MailTransport.h"
#include "core/mail/MailDeliveryState.h"
#include "core/idle/IdleBackground.h"
#include "core/contacts/ContactsManager.h"
#include "Resolver/Resolvers/ContactsRecipientResolver.h"
#include "core/paths/AstraMailPaths.h"
#include "core/profile/LocalProfileCache.h"
#include "menu/Commands/Mails/CmdCleanupInbox.h"
#include "menu/Commands/Mails/CmdCleanupOutbox.h"
#include "menu/Commands/Mails/CmdReviewDelivery.h"
#include "menu/Commands/Mails/CmdSendMailsFromOutbox.h"
#include "menu/Commands/Mails/CmdSendMailsFromPath.h"
#include "menu/Commands/Mails/CmdExportUnreadToInbox.h"
#include "menu/Commands/Mails/CmdExportUnreadToFolders.h"
#include "menu/Commands/Mails/CmdExportLocalInboxToFlash.h"
#include "menu/Commands/Mails/CmdViewInboxMails.h"
#include "menu/Commands/Mails/CmdViewOutboxMails.h"
#include "menu/Commands/Mails/CmdParseEmlFolder.h"
#include "menu/Commands/Server/CmdImportCsvContacts.h"
#include "menu/Commands/Server/CmdEditLocalContacts.h"
#include "menu/Commands/Server/CmdEditLocalProfileSettings.h"
#include "menu/Commands/Server/CmdCheckServers.h"
#include "menu/Commands/Configs/CmdEditConfig.h"
#include "menu/Commands/Configs/CmdResetConfig.h"
#include <atomic>
#include <chrono>
#include <future>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <thread>
using namespace astra_mail;
using Clock=std::chrono::steady_clock;
static unsigned checks=0;
static void check(bool value,const std::string& label) {
    if(!value) throw std::runtime_error(label);
    ++checks; std::cout << "PASS " << label << "\n" << std::flush;
}
static void write(const fs::path& path,const std::string& text) {
    fs::create_directories(path.parent_path()); std::ofstream out(path.string()); out << text;
    if(!out) throw std::runtime_error("synthetic fixture write");
}
static std::string read(const fs::path& path) {
    std::ifstream in(path.string()); return std::string(std::istreambuf_iterator<char>(in),std::istreambuf_iterator<char>());
}
struct Input {
    std::istringstream text; std::streambuf* previous;
    explicit Input(const std::string& value):text(value),previous(std::cin.rdbuf(text.rdbuf())) { std::cin.clear(); }
    ~Input() { std::cin.rdbuf(previous); std::cin.clear(); }
};
static void control() { std::string line; if(!std::getline(std::cin,line)) throw std::runtime_error("missing test control"); }
static std::unordered_map<std::string,Contact> contacts(const std::string& email) {
    Contact contact; contact.key=contact.nickName="target"; contact.email=email;
    return {{"target",contact}};
}
static void load(const std::string& id) {
    const std::string email=id+"@example.invalid";
    check(ContactsManager::instance().loadFromCache(id,contacts(email),{{"circle",{email}}}),"seed contacts "+id);
}
static void contactTests(const fs::path& home) {
    auto& cm=ContactsManager::instance(); load("old");
    ContactsRecipientResolver old(cm);
    auto copy=cm.contacts(); copy.clear();
    check(cm.hasContacts(),"contact getters return independent values");
    {
        MailOperationScope operation;
        check(!cm.clear() && !cm.loadFromCache("wrong",{},{}),"same-thread mail operation refuses contact mutation");
        TbProfile profile; profile.id="wrong"; profile.path=(home/"unread-synthetic-profile").string(); bool duplicate=false;
        check(!cm.loadForProfile(profile,duplicate) && cm.activeProfileId()=="old","blocked profile contact load preserves old snapshot before filesystem access");
        MailOperationScope nested(std::try_to_lock);
        check(nested.allowed(),"nested mail operations remain supported");
    }
    std::promise<void> ready,release; auto released=release.get_future();
    std::thread worker([&](){MailOperationScope operation;ready.set_value();released.wait();}); ready.get_future().wait();
    auto start=Clock::now();
    const bool denied=!cm.clear() && !cm.loadFromCache("wrong",{},{});
    ContactsRecipientResolver during(cm);
    MailOperationScope attempt(std::try_to_lock);
    check(denied && !attempt.allowed() && Clock::now()-start<std::chrono::milliseconds(100),"cross-thread contact mutation/try-operation reject without network wait");
    check(during.resolve("target") && *during.resolve("target")=="old@example.invalid","contact snapshot remains readable while mail gate is held");
    release.set_value(); worker.join();
    load("new"); ContactsRecipientResolver fresh(cm);
    check(old.resolve("target") && *old.resolve("target")=="old@example.invalid" && old.resolve("circle") &&
          *old.resolve("circle")=="old@example.invalid","existing resolver keeps one immutable contact/list snapshot");
    check(fresh.resolve("target") && *fresh.resolve("target")=="new@example.invalid","new resolver uses newly published contacts");
    std::atomic<bool> finish{false},consistent{true}; std::atomic<unsigned> reads{0};
    std::thread reader([&](){while(!finish) { const auto state=cm.snapshot();
        if(state.contacts.at("target").email!=state.profileId+"@example.invalid" ||
           state.mailingLists.at("circle").front()!=state.profileId+"@example.invalid") consistent=false;
        ++reads;
    }});
    while(!reads.load()) std::this_thread::yield();
    for(unsigned i=0;i<1500;++i) { const auto id=std::to_string(i),email=id+"@example.invalid";
        if(!cm.loadFromCache(id,contacts(email),{{"circle",{email}}})) consistent=false;
    }
    finish=true;reader.join();
    check(consistent && reads>0,"concurrent snapshot readers see matching profile/contact/list across 1500 replacements");
    try {MailOperationScope operation(std::try_to_lock); throw std::runtime_error("synthetic unwind");} catch(...) {}
    {MailDataChangeScope change;check(change.allowed(),"exception unwinding releases operation counter and gate");}
    check(cm.clear() && !cm.hasContacts(),"contact clear succeeds after operation ends");
}
static void mailFixture() {
    write(getOutboxRoot()/"a@example.invalid"/"document.txt","synthetic queued attachment\n");
    write(getInboxRoot()/"topic"/"uid_1"/"document.txt","synthetic inbox attachment\n");
}
int main(int argc,char** argv) {
    try {
        if(argc<4) return 2;
        const std::string mode=argv[1]; const int smtpPort=std::stoi(argv[2]),imapPort=std::stoi(argv[3]);
        const fs::path home=std::getenv("HOME");
        if(mode=="contacts") contactTests(home);
        else {
            TbProfile profile;profile.id="synthetic-am08";
            LocalMailSettings settings;settings.profileId=profile.id;settings.accountId="synthetic";
            settings.email=settings.userName="sender@example.invalid"; settings.smtpHost=settings.imapHost="127.0.0.1";
            settings.smtpPort=smtpPort;settings.imapPort=imapPort;
            auto& tb=ThunderbirdSettings::instance();
            check(tb.loadFromLocalCache(profile,settings) && tb.setMailPassword("synthetic-only"),"initialize synthetic unlocked account");
            check(saveLocalMailSettings(settings),"seed synthetic local profile");
            load("old"); auto& cm=ContactsManager::instance(); check(saveLocalContacts(cm.contacts(),cm.mailingLists()),"seed contact cache");
            auto limits=net::currentSettings();limits.imapCommandMs=500;limits.smtpFinalMs=15000;net::configure(limits);
            write(home/"fixture.csv","Отображаемое имя,Псевдоним,Адрес электронной почты\nSynthetic CSV,csv_test,csv@example.invalid\n");
            mailFixture();
            if(mode=="stop") {
                { MailDataChangeScope change;
                  IdleBackground::instance().start(); std::this_thread::sleep_for(std::chrono::milliseconds(2300));
                  auto start=Clock::now();IdleBackground::instance().stop();
                  check(Clock::now()-start<std::chrono::milliseconds(500),"background stop does not wait behind foreground data-change gate");
                }
                check(fs::exists(getOutboxRoot()/"a@example.invalid"/"document.txt"),"skipped background cycle preserves queue");
            } else if(mode=="editor") {
                IdleBackground::instance().start();
                CmdEditConfig command(ConfigItem{"synthetic contacts",getLocalContactsPath()}); command.execute();
                check(fs::exists(home/"editor-started"),"real editor starts when gate is free");
                IdleBackground::instance().stop();
                check(fs::exists(getOutboxRoot()/"a@example.invalid"/"document.txt"),"background cannot send while external editor owns data-change gate");
            } else if(mode=="blocked") {
                int resets=0;
                std::vector<std::pair<std::string,std::unique_ptr<ICommand>>> commands;
                auto add=[&](const char* name,ICommand* command){commands.emplace_back(name,std::unique_ptr<ICommand>(command));};
                const auto flash=(home/"flash").string();fs::create_directories(flash);
                add("cleanup inbox",new CmdCleanupInbox);add("cleanup outbox",new CmdCleanupOutbox);
                add("delivery review",new CmdReviewDelivery);add("send outbox",new CmdSendMailsFromOutbox);
                add("import/send flash",new CmdSendMailsFromPath(flash));add("receive inbox",new CmdExportUnreadToInbox);
                add("online export",new CmdExportUnreadToFolders("127.0.0.1",imapPort,settings.userName,"synthetic-only",ImapFolder("INBOX","INBOX"),flash,ExportStrategy{ExportMode::BySubject,false}));
                add("offline export",new CmdExportLocalInboxToFlash(flash,ExportStrategy{ExportMode::BySubject,false}));
                add("view inbox",new CmdViewInboxMails);add("view outbox",new CmdViewOutboxMails);
                add("CSV import",new CmdImportCsvContacts);add("edit contacts",new CmdEditLocalContacts);
                add("edit profile",new CmdEditLocalProfileSettings);add("edit config",new CmdEditConfig(ConfigItem{"synthetic",getLocalContactsPath()}));
                add("reset config",new CmdResetConfig("synthetic",[&](bool){++resets;}));add("check servers",new CmdCheckServers);
                add("parse EML",new CmdParseEmlFolder(flash,flash));
                const auto cache=read(getLocalContactsPath()),profileCache=read(getLocalMailSettingsPath());
                IdleBackground::instance().start();std::cout<<"READY_FOR_CHECK\n"<<std::flush;control();
                const auto folder=getOutboxRoot()/"a@example.invalid";
                const auto payload=read(folder/"document.txt"),receipt=read(folder/".astramail-delivery-state");
                for(auto& entry:commands) {
                    Input input("\n"); auto start=Clock::now();const auto result=entry.second->execute();
                    check(result==CommandResult::Continue && Clock::now()-start<std::chrono::milliseconds(200),entry.first+" rejects promptly during real background SMTP");
                }
                check(read(folder/"document.txt")==payload && read(folder/".astramail-delivery-state")==receipt,"rejected queue commands preserve payload and delivery receipt");
                check(fs::exists(getInboxRoot()/"topic"/"uid_1"/"document.txt"),"rejected cleanup/export preserves inbox");
                check(read(getLocalContactsPath())==cache && read(getLocalMailSettingsPath())==profileCache && cm.activeProfileId()=="old","rejected edit/import/reset preserves disk caches and loaded contacts");
                check(!resets && !fs::exists(home/"editor-started"),"rejected commands start neither reset callback nor editor");
                check(!cm.clear() && !cm.loadFromCache("wrong",{},{}),"low-level contacts also reject during actual background operation");
                std::ostringstream detail;
                check(!CmdExportUnreadToFolders::runExportFromLocalInbox(flash,ExportStrategy{ExportMode::BySubject,false},&detail) && detail.str().find(mailBusyMessage())!=std::string::npos,"direct offline export entry point also rejects busy gate");
                std::cout<<"BLOCKED_DONE\n"<<std::flush;control();
                bool free=false; for(unsigned i=0;i<300&&!free;++i) { {MailOperationScope operation(std::try_to_lock);free=operation.allowed();} if(!free) std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
                check(free,"background releases gate after SMTP confirmation"); IdleBackground::instance().stop();
                write(getOutboxRoot()/"repeat@example.invalid"/"document.txt","synthetic repeat cleanup\n");
                {Input input("y\n");CmdCleanupOutbox command;command.execute();}
                check(!fs::exists(getOutboxRoot()/"repeat@example.invalid"),"outbox cleanup succeeds when operator repeats after completion");
                {Input input("y\n");CmdCleanupInbox command;command.execute();}
                check(!fs::exists(getInboxRoot()/"topic"),"inbox cleanup succeeds after completion");
                {Input input("2\n1\n\n0\n0\n");CmdImportCsvContacts command;command.execute();}
                ContactsRecipientResolver imported(cm);
                check(imported.resolve("csv_test") && *imported.resolve("csv_test")=="csv@example.invalid","CSV command succeeds after completion and publishes new contacts");
                {Input input("y\n");CmdResetConfig command("synthetic",[&](bool){++resets;});command.execute();}
                check(resets==1,"reset callback succeeds after completion");
            } else return 2;
        }
        std::cout<<"AM08_DRIVER_CHECKS="<<checks<<"\n";return 0;
    } catch(const std::exception& error) {std::cerr<<"AM08_DRIVER_FAILED: "<<error.what()<<"\n";return 1;}
}
