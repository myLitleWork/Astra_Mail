#include "SecretManager.h"

#include "ISecretBackend.h"

// объявление функции-фабрики
ISecretBackend* createSecretBackend();


SecretManager& SecretManager::instance() {
    static SecretManager inst;
    return inst;
}

SecretManager::SecretManager()
{
    backend_.reset(createSecretBackend());
}



bool SecretManager::isInitialized() const {
    return backend_->isInitialized();
}

bool SecretManager::hasSecret(const std::string& context) const { return backend_->hasSecret(context); }
bool SecretManager::legacyAvailable(const std::string& context) const { return backend_->legacyAvailable(context); }
bool SecretManager::migrateLegacy(const std::string& context, const std::string& pin) { return backend_->migrateLegacy(context, pin); }

bool SecretManager::store(const std::string& context, const std::string& p,
                          const std::string& pin) {
    return backend_->store(context, p, pin);
}

bool SecretManager::load(const std::string& context, const std::string& pin,
                         std::string& out) {
    return backend_->load(context, pin, out);
}
