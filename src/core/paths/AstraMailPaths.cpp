#include "AstraMailPaths.h"
#include <cstdlib>
#include <stdexcept>

namespace astra_mail
{
    static fs::path getAstraMailDataRoot()
    {
        const char* home = getenv("HOME");
        if (!home)
            throw std::runtime_error("HOME not set");
        return fs::path(home) / ".local/share/AstraMail";
    }

    fs::path getOutboxRoot()
    {
        fs::path root = getAstraMailDataRoot() / "outbox";
        fs::create_directories(root);
        return root;
    }

    fs::path getInboxRoot()
    {
        fs::path root = getAstraMailDataRoot() / "inbox";
        fs::create_directories(root);
        return root;
    }
}
