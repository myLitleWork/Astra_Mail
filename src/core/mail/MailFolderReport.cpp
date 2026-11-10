#include "MailFolderReport.h"
#include "MailFolderScanner.h"
#include "MailDeliveryState.h"

namespace astra_mail
{
    void MailFolderReport::print(std::ostream& os, const FlashMailScanResult& r)
    {
        os << "=== Анализ флешки: " << r.mountPoint << " ===\n\n";

        if (r.mails.empty())
        {
            os << "Писем не найдено (папки без файлов игнорируются).\n";
            return;
        }

        for (size_t i = 0; i < r.mails.size(); ++i)
        {
            const auto& m = r.mails[i];

            os << "[" << (i + 1) << "] Получатель: " << m.recipient
               << (m.isAlias ? " (alias)" : "") << "\n";
            os << "    Папка: " << m.folderName << "\n";
            MailDeliveryState state;
            std::string stateError;
            if (!state.load(m.folderPath, stateError))
                os << "    Отправка заблокирована: " << stateError << "\n";
            else if (state.exists)
                os << "    Результат: " << state.summary() << "\n";
            os << "    Вложений: " << m.attachments.size() << "\n";
            os << "    ---------------------------------\n";

            for (const auto& a : m.attachments)
            {
                os << "      - " << a.name << "    "
                   << MailFolderScanner::formatBytes(a.sizeBytes) << "\n";
            }

            os << "    ---------------------------------\n";
            os << "    ИТОГО: " << MailFolderScanner::formatBytes(m.totalSizeBytes) << "\n\n";
        }

        os << "-------------------------------------\n";
        os << "Найдено писем: " << r.totalMails << "\n";
        os << "Найдено вложений: " << r.totalAttachments << "\n";
        os << "Общий объём вложений: " << MailFolderScanner::formatBytes(r.totalSizeBytes) << "\n";
    }
}
