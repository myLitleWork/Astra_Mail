// secrets/SecretBackendStub.h
#pragma once

#include "ISecretBackend.h"

class SecretBackendStub final : public ISecretBackend
{
public:
    SecretBackendStub();

    bool isInitialized() const override;
    bool store(const std::string& password,
               const std::string& pin) override;
    bool load(const std::string& pin,
              std::string& outPassword) override;
};
