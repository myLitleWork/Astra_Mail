
#include "CmdInfo.h"
#include "Utils/Utils.h"

#include <iostream>

namespace astra_mail
{

    CommandResult CmdInfo::execute()
    {
        std::cout << message_ << "\n";
        my::pause();
        return CommandResult::Continue;
    }

    CmdInfo::CmdInfo(std::string title, std::string message)
            : title_(std::move(title))
            , message_(std::move(message))
    {
    }

    std::string CmdInfo::title() const
    {
        return title_;
    }


}
