#pragma once
#include <Arduino.h>
#include <vector>
#include "mbedtls/ecdsa.h"
#include "mbedtls/ecp.h"
#include "mbedtls/sha256.h"
#include "mbedtls/md.h"

// FIDO2 / CTAP2 Constants
constexpr uint8_t CTAP2_OK = 0x00;
constexpr uint8_t CTAP2_ERR_INVALID_COMMAND = 0x01;
constexpr uint8_t CTAP2_ERR_INVALID_PARAMETER = 0x02;
constexpr uint8_t CTAP2_ERR_INVALID_LENGTH = 0x03;
constexpr uint8_t CTAP2_ERR_UNSUPPORTED_ALGORITHM = 0x26;
constexpr uint8_t CTAP2_ERR_OPERATION_DENIED = 0x27;
constexpr uint8_t CTAP2_ERR_UNSUPPORTED_OPTION = 0x2B;
constexpr uint8_t CTAP2_ERR_NO_CREDENTIALS = 0x2E;
constexpr uint8_t CTAP2_ERR_USER_ACTION_TIMEOUT = 0x32;
constexpr uint8_t CTAP2_ERR_NOT_ALLOWED = 0x30;

// Authenticator Flags
constexpr uint8_t FLAG_USER_PRESENT = 0x01;
constexpr uint8_t FLAG_USER_VERIFIED = 0x04;
constexpr uint8_t FLAG_ATTESTED_CRED_DATA = 0x40;
constexpr uint8_t FLAG_EXTENSION_DATA = 0x80;

struct FidoCredential {
  String rpId;              // e.g. "webauthn.io", "github.com", "google.com"
  String userName;          // e.g. "admin", "alice@example.com"
  String userDisplayName;
  std::vector<uint8_t> userId;
  std::vector<uint8_t> credId;   // 32-byte unique credential ID
  std::vector<uint8_t> privKey;  // 32-byte raw EC private key scalar
  std::vector<uint8_t> pubKeyX;  // 32-byte X coordinate
  std::vector<uint8_t> pubKeyY;  // 32-byte Y coordinate
  uint32_t signCounter;
  uint32_t createdAt;
};

class FidoStore {
public:
  static void init();
  static bool loadFromStorage();
  static bool saveToStorage();
  static void clearAll();

  static bool createCredential(
    const String &rpId,
    const std::vector<uint8_t> &userId,
    const String &userName,
    const String &userDisplayName,
    FidoCredential &outCred
  );

  static FidoCredential* findCredential(const std::vector<uint8_t> &credId);
  static FidoCredential* findCredentialByRp(const String &rpId);
  static std::vector<FidoCredential>& getAllCredentials();
  static uint32_t getNextGlobalCounter();

  static void getAaguid(uint8_t *out16);

  // ECDSA secp256r1 helper functions
  static bool generateKeyPair(
    std::vector<uint8_t> &outPriv,
    std::vector<uint8_t> &outPubX,
    std::vector<uint8_t> &outPubY
  );

  static bool signData(
    const std::vector<uint8_t> &privKey,
    const uint8_t *data,
    size_t dataLen,
    std::vector<uint8_t> &outDerSig
  );

  static void sha256(const uint8_t *data, size_t len, uint8_t *out32);
};
