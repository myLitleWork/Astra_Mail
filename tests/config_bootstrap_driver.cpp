// Real bootstrap/defaults and reset command; paths stay relative to this copied
// executable and the synthetic HOME supplied by the integration test.
#include "Configs/Confg/ConfigBootstrap.h"
#include "Configs/Confg/ConfigDefaults.h"
#include "menu/Commands/Configs/CmdResetConfig.h"
#include <exception>
#include <iostream>
#include <string>

int main(int argc, char** argv)
{
    using namespace astra_mail;
    using Ensure = void (*)(bool);
    using Default = const char* (*)();
    const Ensure ensure[] = {
        config::ensureSubjectRules, config::ensureAttachmentGroups,
        config::ensureOutgoingSubjectRules, config::ensureIdleSettings,
        config::ensureNetworkSettings, config::ensureLocalMailSettings,
        config::ensureLocalContacts
    };
    const Default defaults[] = {
        config::defaults::subjectRules, config::defaults::attachmentGroups,
        config::defaults::outgoingSubjectRules, config::defaults::idleSettings,
        config::defaults::networkSettings, config::defaults::localMailSettings,
        config::defaults::localContacts
    };
    if (argc != 3) return 2;
    const int index = std::stoi(argv[2]);
    if (index < 0 || index >= 7) return 2;
    const std::string mode = argv[1];
    try
    {
        if (mode == "defaults") std::cout << defaults[index]();
        else if (mode == "ensure") ensure[index](false);
        else if (mode == "force") ensure[index](true);
        else if (mode == "reset")
        {
            CmdResetConfig command("Synthetic reset", ensure[index]);
            command.execute();
        }
        else return 2;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
