#pragma once
#include <memory>
#include <string>

class ISecretBackend;

class SecretManager {
public:
    static SecretManager& instance();

    bool isInitialized() const;
    bool hasSecret(const std::string& context) const;
    bool legacyAvailable(const std::string& context) const;
    bool migrateLegacy(const std::string& context, const std::string& pin);
    bool store(const std::string& context, const std::string& password,
               const std::string& pin);
    bool load(const std::string& context, const std::string& pin,
              std::string& outPassword);

private:
    SecretManager();
    std::unique_ptr<ISecretBackend> backend_;
};
