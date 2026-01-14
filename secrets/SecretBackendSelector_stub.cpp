#include "SecretBackendStub.h"
#include "ISecretBackend.h"

ISecretBackend* createSecretBackend()
{
    return new SecretBackendStub();
}
