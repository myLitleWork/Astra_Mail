// secrets/SecretBackendStub.h
#pragma once

#include "ISecretBackend.h"

class SecretBackendStub final : public ISecretBackend
{
public:
    SecretBackendStub();

    bool isInitialized() const override;
    bool hasSecret(const std::string&) const override { return false; }
    bool legacyAvailable(const std::string&) const override { return false; }
    bool migrateLegacy(const std::string&, const std::string&) override { return false; }
    bool store(const std::string& context, const std::string& password,
               const std::string& pin) override;
    bool load(const std::string& context, const std::string& pin,
              std::string& outPassword) override;
};
