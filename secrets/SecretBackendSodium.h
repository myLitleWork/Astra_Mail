#pragma once
#include "ISecretBackend.h"

class SecretBackendSodium : public ISecretBackend {
public:

    bool initialized_ = false;


    SecretBackendSodium();

    bool isInitialized() const override;
    bool hasSecret(const std::string& context) const override;
    bool legacyAvailable(const std::string& context) const override;
    bool migrateLegacy(const std::string& context, const std::string& pin) override;
    bool store(const std::string& context, const std::string& password,
               const std::string& pin) override;
    bool load(const std::string& context, const std::string& pin,
              std::string& outPassword) override;
};
