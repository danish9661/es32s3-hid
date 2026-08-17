#pragma once
#include <Arduino.h>
#include <vector>
#include "mbedtls/ecdsa.h"
#include "mbedtls/ecp.h"
#include "mbedtls/sha256.h"
#include "mbedtls/md.h"
#include "mbedtls/aes.h"

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
constexpr uint8_t CTAP2_ERR_PIN_INVALID = 0x31;
constexpr uint8_t CTAP2_ERR_PIN_BLOCKED = 0x32;
constexpr uint8_t CTAP2_ERR_PIN_AUTH_INVALID = 0x33;
constexpr uint8_t CTAP2_ERR_PIN_AUTH_BLOCKED = 0x34;
constexpr uint8_t CTAP2_ERR_PIN_NOT_SET = 0x35;
constexpr uint8_t CTAP2_ERR_PIN_REQUIRED = 0x36;
constexpr uint8_t CTAP2_ERR_PIN_POLICY_VIOLATION = 0x37;

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
  std::vector<uint8_t> hmacSecretKey; // 32-byte master HMAC secret for hmac-secret / prf extension
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
  static bool deleteCredential(const std::vector<uint8_t> &credId);
  static uint32_t getNextGlobalCounter();

  static bool getLargeBlob(std::vector<uint8_t> &outBlob);
  static bool setLargeBlob(const uint8_t *blobData, size_t len);
  static void clearLargeBlob();

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
  static void hmacSha256(const uint8_t *key, size_t keyLen, const uint8_t *data, size_t dataLen, uint8_t *out32);

  // ECDH & AES-256-CBC for CTAP2 PIN Protocol 1
  static bool computeSharedSecret(
    const std::vector<uint8_t> &privKey,
    const std::vector<uint8_t> &peerPubX,
    const std::vector<uint8_t> &peerPubY,
    std::vector<uint8_t> &outSharedKey32
  );

  static bool aes256CbcDecrypt(
    const uint8_t *key32,
    const uint8_t *iv16,
    const uint8_t *input,
    size_t len,
    std::vector<uint8_t> &output
  );

  static bool aes256CbcEncrypt(
    const uint8_t *key32,
    const uint8_t *iv16,
    const uint8_t *input,
    size_t len,
    std::vector<uint8_t> &output
  );

  // PIN Storage & Verification
  static bool isPinSet();
  static bool setPin(const uint8_t *decryptedPin64, size_t pinLen);
  static bool changePin(const uint8_t *oldPin64, size_t oldLen, const uint8_t *newPin64, size_t newLen);
  static bool verifyPinHash(const uint8_t *pinHash16);
  static uint8_t getPinRetries();
  static void decrementPinRetries();
  static void resetPinRetries();
  static void clearPin();

  static void getPinToken(uint8_t *out32);
  static bool verifyPinAuth(const uint8_t *clientDataHash32, const uint8_t *pinAuth16);

  // Biometric UV Emulation
  static bool getEmulateUv();
  static void setEmulateUv(bool enable);

  // Backup & Import
  static void importCredentials(const std::vector<FidoCredential> &list, bool replace = true);
};

struct CborWriter {
  std::vector<uint8_t> buf;

  void writeTypeAndVal(uint8_t majorType, uint64_t val) {
    uint8_t type = (majorType << 5);
    if (val <= 23) {
      buf.push_back(type | static_cast<uint8_t>(val));
    } else if (val <= 0xFF) {
      buf.push_back(type | 24);
      buf.push_back(static_cast<uint8_t>(val));
    } else if (val <= 0xFFFF) {
      buf.push_back(type | 25);
      buf.push_back(static_cast<uint8_t>((val >> 8) & 0xFF));
      buf.push_back(static_cast<uint8_t>(val & 0xFF));
    } else if (val <= 0xFFFFFFFF) {
      buf.push_back(type | 26);
      buf.push_back(static_cast<uint8_t>((val >> 24) & 0xFF));
      buf.push_back(static_cast<uint8_t>((val >> 16) & 0xFF));
      buf.push_back(static_cast<uint8_t>((val >> 8) & 0xFF));
      buf.push_back(static_cast<uint8_t>(val & 0xFF));
    }
  }

  void writeInt(int64_t val) {
    if (val >= 0) {
      writeTypeAndVal(0, static_cast<uint64_t>(val));
    } else {
      writeTypeAndVal(1, static_cast<uint64_t>(-1 - val));
    }
  }

  void writeBytes(const uint8_t *data, size_t len) {
    writeTypeAndVal(2, len);
    if (data && len > 0) {
      buf.insert(buf.end(), data, data + len);
    }
  }

  void writeText(const String &str) {
    writeTypeAndVal(3, str.length());
    buf.insert(buf.end(), str.begin(), str.end());
  }

  void writeArray(size_t size) { writeTypeAndVal(4, size); }
  void writeMap(size_t size) { writeTypeAndVal(5, size); }
  void writeBool(bool b) { buf.push_back(b ? 0xF5 : 0xF4); }
  void writeNull() { buf.push_back(0xF6); }
};


