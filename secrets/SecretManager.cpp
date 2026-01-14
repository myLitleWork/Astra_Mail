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

bool SecretManager::store(const std::string& p,
                          const std::string& pin) {
    return backend_->store(p, pin);
}

bool SecretManager::load(const std::string& pin,
                         std::string& out) {
    return backend_->load(pin, out);
}
