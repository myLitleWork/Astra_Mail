#pragma once
#include <mutex>

namespace astra_mail {
// One account context for the duration of each mail operation. Recursive because
// startup checks/export/send pipelines call other guarded entry points.
struct AccountAccess {
    std::recursive_mutex mutex;
    unsigned operations = 0;
    static AccountAccess& instance() { static AccountAccess value; return value; }
};
class MailOperationScope {
public:
    MailOperationScope() : lock_(AccountAccess::instance().mutex) { ++AccountAccess::instance().operations; }
    explicit MailOperationScope(std::try_to_lock_t) : lock_(AccountAccess::instance().mutex, std::try_to_lock) {
        if (lock_.owns_lock()) ++AccountAccess::instance().operations;
    }
    ~MailOperationScope() { if (lock_.owns_lock()) --AccountAccess::instance().operations; }
    bool allowed() const { return lock_.owns_lock(); }
    MailOperationScope(const MailOperationScope&) = delete;
    MailOperationScope& operator=(const MailOperationScope&) = delete;
private:
    std::unique_lock<std::recursive_mutex> lock_;
};
class AccountChangeScope {
public:
    AccountChangeScope() : lock_(AccountAccess::instance().mutex, std::try_to_lock) {
        if (lock_.owns_lock() && AccountAccess::instance().operations) lock_.unlock();
    }
    bool allowed() const { return lock_.owns_lock(); }
private:
    std::unique_lock<std::recursive_mutex> lock_;
};
// Queue/contact/configuration mutations must not start inside a mail operation,
// including same-thread callbacks. Uses the same gate as account changes.
using MailDataChangeScope = AccountChangeScope;
inline const char* mailBusyMessage() {
    return "Команда не выполнена: идёт другая почтовая операция или изменение данных. Повторите позже.";
}
}
