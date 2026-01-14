
#include "SecretBackendStub.h"

SecretBackendStub::SecretBackendStub() = default;

bool SecretBackendStub::isInitialized() const
{
    return false;
}

bool SecretBackendStub::store(const std::string&, const std::string&)
{
    return false;
}

bool SecretBackendStub::load(const std::string&, std::string&)
{
    return false;
}
