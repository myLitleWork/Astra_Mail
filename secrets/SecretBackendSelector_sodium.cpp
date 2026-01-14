//
// Created by Sg on 16.12.2025.
//
#include "SecretBackendSodium.h"
#include "ISecretBackend.h"

ISecretBackend* createSecretBackend()
{
    return new SecretBackendSodium();
}
