#pragma once
#include "ISecretBackend.h"

class SecretBackendSodium : public ISecretBackend {
public:

    bool initialized_ = false;


    SecretBackendSodium();

    bool isInitialized() const override;
    bool store(const std::string& password,
               const std::string& pin) override;
    bool load(const std::string& pin,
              std::string& outPassword) override;
};
