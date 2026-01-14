#pragma once
#include <ostream>
#include "MailFolderModels.h"

namespace astra_mail
{
    class MailFolderReport
    {
    public:
        static void print(std::ostream& os, const FlashMailScanResult& r);
    };
}
