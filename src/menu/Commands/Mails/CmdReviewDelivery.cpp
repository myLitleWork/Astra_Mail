#include "Utils/ConsoleInput.h"
#include "menu/MailCommandAccess.h"
#include "CmdReviewDelivery.h"
#include "core/mail/MailDeliveryState.h"
#include "core/paths/AstraMailPaths.h"
#include "core/idle/IdleBackground.h"
#include "Utils/Utils.h"
#include <iostream>
#include <sstream>

namespace astra_mail {
CommandResult CmdReviewDelivery::execute() {
        MailDataChangeScope change;
        if (!mailCommandAllowed(change.allowed())) return CommandResult::Continue;
    IdleBackground::BusyGuard busy;
    const auto root = getOutboxRoot();
    std::error_code ec;
    std::vector<fs::path> folders;
    for (fs::directory_iterator it(root, ec); !ec && it != fs::end(it); it.increment(ec)) {
        if (!fs::is_directory(it->path(), ec) || ec) continue;
        if (fs::exists(MailDeliveryState::path(it->path()), ec) && !ec) folders.push_back(it->path());
    }
    if (folders.empty()) {
        std::cout << "Сохранённых результатов отправки нет.\n";
        my::pause(); return CommandResult::Continue;
    }
    for (size_t i = 0; i < folders.size(); ++i)
        std::cout << i + 1 << ". " << folders[i].filename().string() << "\n";
    std::cout << "Номер письма (0 — назад): ";
    std::string input;
    if (!readConsoleLine(input)) return CommandResult::Continue;
    size_t index = 0;
    { std::istringstream line(input); if (!(line >> index) || !index || index > folders.size()) return CommandResult::Continue; }
    const auto folder = folders[index - 1];
    MailDeliveryLock lock(folder);
    std::string error;
    MailDeliveryState state;
    if (!lock.locked() || !state.load(folder, error) || !state.exists) {
        std::cout << "Письмо занято или состояние недоступно. " << error << "\n";
        my::pause(); return CommandResult::Continue;
    }
    std::cout << state.summary() << "\n";
    std::vector<size_t> unknown;
    for (size_t i = 0; i < state.recipients.size(); ++i) {
        if (state.recipients[i].status == RecipientDelivery::Delivered) continue;
        unknown.push_back(i);
        std::cout << unknown.size() << ". "
                  << (state.recipients[i].status == RecipientDelivery::Unknown ? "Не подтверждено: " : "Ожидает отправки: ")
                  << state.recipients[i].address << "\n";
    }
    if (unknown.empty()) { my::pause(); return CommandResult::Continue; }
    std::cout << "Номер адресата (0 — назад): ";
    if (!readConsoleLine(input)) return CommandResult::Continue;
    index = 0;
    { std::istringstream line(input); if (!(line >> index) || !index || index > unknown.size()) return CommandResult::Continue; }
    auto& entry = state.recipients[unknown[index - 1]];
    const bool uncertain = entry.status == RecipientDelivery::Unknown;
    std::cout << "Адресат: " << entry.address << "\n";
    if (uncertain)
        std::cout << "Автоповтор уже запланирован. При потере подтверждения возможен дубль.\n"
                  << "1. Приём подтверждён по серверу/адресату — больше не отправлять\n";
    else std::cout << "3. Исправить адрес для следующей попытки\n";
    std::cout << "0. Назад\nВыбор: ";
    if (!readConsoleLine(input)) return CommandResult::Continue;
    if (!uncertain && input == "3") {
        std::cout << "Новый email: ";
        std::string address;
        if (!readConsoleLine(address)) return CommandResult::Continue;
        if (address.find('@') == std::string::npos) return CommandResult::Continue;
        std::cout << "Заменить " << entry.address << " на " << address << "? Введите yes: ";
        if (!readConsoleLine(input)) return CommandResult::Continue;
        if (input != "yes") return CommandResult::Continue;
        entry.address = address;
        if (!state.save(folder, error)) std::cout << "Изменение не подтверждено: " << error << "\n";
        else std::cout << "Новый адрес сохранён для повторной отправки.\n";
        my::pause(); return CommandResult::Continue;
    }
    if (!uncertain || input != "1") return CommandResult::Continue;
    std::cout << "Подтвердить приём для " << entry.address << "? Введите yes: ";
    if (!readConsoleLine(input)) return CommandResult::Continue;
    if (input != "yes") return CommandResult::Continue;
    entry.status = RecipientDelivery::Delivered;
    if (!state.save(folder, error)) std::cout << "Изменение не подтверждено: " << error << "\n";
    else std::cout << "Решение сохранено. " << state.summary() << "\n";
    my::pause(); return CommandResult::Continue;
}
}
