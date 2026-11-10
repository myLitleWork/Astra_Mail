#pragma once
#include "MailFolderModels.h"
#include "UtilsFS/FsCompat.h"
#include <string>
#include <vector>

namespace astra_mail {
enum class RecipientDelivery { Pending, Delivered, Unknown };
struct RecipientProgress {
    std::string address;
    RecipientDelivery status = RecipientDelivery::Pending;
};

// Durable envelope snapshot: changes to the address book never reset delivery progress.
struct MailDeliveryState {
    bool exists = false;
    std::string context;
    std::string fingerprint;
    std::vector<RecipientProgress> recipients;

    static fs::path path(const fs::path& folder);
    bool load(const fs::path& folder, std::string& error);
    bool save(const fs::path& folder, std::string& error) const;
    bool complete() const;
    std::vector<std::string> pending() const;
    std::string summary() const;
};

class MailDeliveryLock {
public:
    explicit MailDeliveryLock(const fs::path& folder);
    ~MailDeliveryLock();
    bool locked() const { return fd_ >= 0; }
    MailDeliveryLock(const MailDeliveryLock&) = delete;
    MailDeliveryLock& operator=(const MailDeliveryLock&) = delete;
private:
    int fd_ = -1;
};

bool mailPayloadFingerprint(const MailFolderInfo& info, std::string& fingerprint, std::string& error);
}
