#pragma once

#include <chrono>
#include <atomic>
#include <thread>
#include <mutex>

namespace astra_mail
{
    /**
     * Фоновый модуль: при бездействии пользователя (вариант B — сброс при выборе в меню)
     * выполняет экспорт непрочитанных в локальный inbox и отправку писем из outbox.
     * Поток запускается после успешной авторизации.
     */
    class IdleBackground
    {
    public:
        static IdleBackground& instance();

        void start();
        void stop();
        void resetTimer();

        /** Пока хотя бы один guard активен, фоновый поток не запускает runActions(). */
        void enterBusy();
        void leaveBusy();

        /** RAII: enterBusy в конструкторе, leaveBusy в деструкторе. */
        class BusyGuard
        {
        public:
            BusyGuard() { instance().enterBusy(); }
            ~BusyGuard() { instance().leaveBusy(); }
            BusyGuard(const BusyGuard&) = delete;
            BusyGuard& operator=(const BusyGuard&) = delete;
        };

        IdleBackground(const IdleBackground&) = delete;
        IdleBackground& operator=(const IdleBackground&) = delete;

    private:
        IdleBackground() = default;

        void threadLoop();

        /** Выполнить экспорт в inbox и отправку из outbox (после проверки серверов). */
        void runActions();

        size_t countInboxLetters() const;
        size_t countOutboxLetters() const;
        void showBackgroundMenu(size_t inboxCount, size_t outboxCount, const std::string& status) const;

        mutable std::mutex coutMutex_;
        std::chrono::steady_clock::time_point lastActivity_;

        std::atomic<bool> stop_{false};
        std::atomic<uint32_t> busyCount_{0};
        std::thread thread_;
    };
}
