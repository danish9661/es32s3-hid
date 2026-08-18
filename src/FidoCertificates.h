#pragma once
#include <stdint.h>
#include <stddef.h>

// YubiKey 5A X.509 EC P-256 Batch Attestation Certificate
extern const uint8_t YUBIKEY_ATTESTATION_CERT[463];
extern const size_t YUBIKEY_ATTESTATION_CERT_LEN;

// Attestation Private Key (32-byte SECP256R1 scalar)
extern const uint8_t YUBIKEY_ATTESTATION_PRIVKEY[32];
