// src/secrets/ISecretBackend.h
#pragma once
#include <string>

class ISecretBackend {
public:
    virtual ~ISecretBackend() = default;

    virtual bool isInitialized() const = 0;
    virtual bool hasSecret(const std::string& context) const = 0;
    virtual bool legacyAvailable(const std::string& context) const = 0;
    virtual bool migrateLegacy(const std::string& context, const std::string& pin) = 0;
    virtual bool store(const std::string& context, const std::string& password,
                       const std::string& pin) = 0;
    virtual bool load(const std::string& context, const std::string& pin,
                      std::string& outPassword) = 0;
};
