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
        static constexpr std::chrono::minutes idleThreshold_{1};
        static constexpr std::chrono::seconds checkInterval_{10};

        std::atomic<bool> stop_{false};
        std::thread thread_;
    };
}
