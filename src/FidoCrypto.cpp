#include "FidoCrypto.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include "esp_random.h"

static std::vector<FidoCredential> credentials;
static uint32_t globalCounter = 1;
static bool initialized = false;

static bool pinIsSet = false;
static uint8_t pinStoredHash[32] = {0};
static uint8_t pinRetriesLeft = 8;
static uint8_t currentPinToken[32] = {0};
static bool pinTokenValid = false;
static bool emulateBiometricUv = false;

// W3C WebAuthn Spec: For authenticators using self-attestation or none attestation,
// the AAGUID field MUST be set to 16 zero bytes (0x00).
static const uint8_t ESP32_AAGUID[16] = {
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

void FidoStore::getAaguid(uint8_t *out16) {
  memcpy(out16, ESP32_AAGUID, 16);
}

void FidoStore::sha256(const uint8_t *data, size_t len, uint8_t *out32) {
  mbedtls_sha256_context ctx;
  mbedtls_sha256_init(&ctx);
  mbedtls_sha256_starts(&ctx, 0);
  mbedtls_sha256_update(&ctx, data, len);
  mbedtls_sha256_finish(&ctx, out32);
  mbedtls_sha256_free(&ctx);
}

static int mbedtls_esp_rng(void *ctx, unsigned char *buf, size_t len) {
  esp_fill_random(buf, len);
  return 0;
}

bool FidoStore::generateKeyPair(
  std::vector<uint8_t> &outPriv,
  std::vector<uint8_t> &outPubX,
  std::vector<uint8_t> &outPubY
) {
  mbedtls_ecdsa_context ecdsa;
  mbedtls_ecdsa_init(&ecdsa);

  int ret = mbedtls_ecdsa_genkey(&ecdsa, MBEDTLS_ECP_DP_SECP256R1, mbedtls_esp_rng, nullptr);
  if (ret != 0) {
    mbedtls_ecdsa_free(&ecdsa);
    return false;
  }

  outPriv.resize(32);
  outPubX.resize(32);
  outPubY.resize(32);

  mbedtls_mpi_write_binary(&ecdsa.d, outPriv.data(), 32);
  mbedtls_mpi_write_binary(&ecdsa.Q.X, outPubX.data(), 32);
  mbedtls_mpi_write_binary(&ecdsa.Q.Y, outPubY.data(), 32);

  mbedtls_ecdsa_free(&ecdsa);
  return true;
}

bool FidoStore::signData(
  const std::vector<uint8_t> &privKey,
  const uint8_t *data,
  size_t dataLen,
  std::vector<uint8_t> &outDerSig
) {
  uint8_t hash[32];
  sha256(data, dataLen, hash);

  mbedtls_ecdsa_context ecdsa;
  mbedtls_ecdsa_init(&ecdsa);
  mbedtls_ecp_group_load(&ecdsa.grp, MBEDTLS_ECP_DP_SECP256R1);
  mbedtls_mpi_read_binary(&ecdsa.d, privKey.data(), privKey.size());

  mbedtls_mpi r, s;
  mbedtls_mpi_init(&r);
  mbedtls_mpi_init(&s);

  int ret = mbedtls_ecdsa_sign(&ecdsa.grp, &r, &s, &ecdsa.d, hash, 32, mbedtls_esp_rng, nullptr);
  if (ret != 0) {
    mbedtls_mpi_free(&r);
    mbedtls_mpi_free(&s);
    mbedtls_ecdsa_free(&ecdsa);
    return false;
  }

  // Encode DER sequence: 0x30 [len] 0x02 [r_len] [r] 0x02 [s_len] [s]
  uint8_t rBuf[33];
  uint8_t sBuf[33];
  size_t rLen = mbedtls_mpi_size(&r);
  size_t sLen = mbedtls_mpi_size(&s);

  mbedtls_mpi_write_binary(&r, rBuf, rLen);
  mbedtls_mpi_write_binary(&s, sBuf, sLen);

  bool rPad = (rBuf[0] & 0x80) != 0;
  bool sPad = (sBuf[0] & 0x80) != 0;

  size_t rFieldLen = rLen + (rPad ? 1 : 0);
  size_t sFieldLen = sLen + (sPad ? 1 : 0);
  size_t totalPayload = 2 + rFieldLen + 2 + sFieldLen;

  outDerSig.clear();
  outDerSig.push_back(0x30);
  outDerSig.push_back(static_cast<uint8_t>(totalPayload));

  outDerSig.push_back(0x02);
  outDerSig.push_back(static_cast<uint8_t>(rFieldLen));
  if (rPad) outDerSig.push_back(0x00);
  outDerSig.insert(outDerSig.end(), rBuf, rBuf + rLen);

  outDerSig.push_back(0x02);
  outDerSig.push_back(static_cast<uint8_t>(sFieldLen));
  if (sPad) outDerSig.push_back(0x00);
  outDerSig.insert(outDerSig.end(), sBuf, sBuf + sLen);

  mbedtls_mpi_free(&r);
  mbedtls_mpi_free(&s);
  mbedtls_ecdsa_free(&ecdsa);
  return true;
}

static String toHex(const uint8_t *data, size_t len) {
  String out = "";
  out.reserve(len * 2);
  for (size_t i = 0; i < len; i++) {
    char buf[3];
    snprintf(buf, sizeof(buf), "%02x", data[i]);
    out += buf;
  }
  return out;
}

static std::vector<uint8_t> fromHex(const String &hex) {
  std::vector<uint8_t> out;
  for (size_t i = 0; i + 1 < hex.length(); i += 2) {
    char sub[3] = { hex[i], hex[i + 1], '\0' };
    out.push_back(static_cast<uint8_t>(strtol(sub, nullptr, 16)));
  }
  return out;
}

void FidoStore::init() {
  if (initialized) return;
  initialized = true;
  loadFromStorage();
}

bool FidoStore::loadFromStorage() {
  credentials.clear();

  Preferences prefs;
  if (prefs.begin("fido_nvs", true)) {
    globalCounter = prefs.getUInt("counter", 1);
    pinIsSet = prefs.getBool("pin_set", false);
    pinRetriesLeft = prefs.getUChar("pin_retries", 8);
    emulateBiometricUv = prefs.getBool("emulate_uv", false);
    if (pinIsSet) {
      prefs.getBytes("pin_hash", pinStoredHash, 32);
    }
    prefs.end();
  }

  String jsonContent = "";
  if (LittleFS.exists("/passkeys.json")) {
    File f = LittleFS.open("/passkeys.json", "r");
    if (f) {
      jsonContent = f.readString();
      f.close();
    }
  }

  // If passkeys.json is missing in LittleFS (e.g. after uploadfs / OTA filesystem update),
  // automatically restore from non-volatile NVS flash backup!
  if (jsonContent.isEmpty()) {
    Preferences nvs;
    if (nvs.begin("fido_nvs", true)) {
      if (nvs.isKey("pkeys_json")) {
        jsonContent = nvs.getString("pkeys_json", "");
        if (!jsonContent.isEmpty()) {
          Serial.printf("[FIDO2] Restored passkeys.json (%u bytes) from NVS backup!\n", jsonContent.length());
          File f = LittleFS.open("/passkeys.json", "w");
          if (f) {
            f.print(jsonContent);
            f.close();
          }
        }
      }
      nvs.end();
    }
  }

  if (jsonContent.isEmpty()) {
    return false;
  }

  DynamicJsonDocument doc(16384);
  DeserializationError err = deserializeJson(doc, jsonContent);
  if (err != DeserializationError::Ok || !doc.is<JsonArray>()) {
    return false;
  }

  JsonArray arr = doc.as<JsonArray>();
  for (JsonObject obj : arr) {
    FidoCredential c;
    c.rpId = obj["rpId"].as<String>();
    c.userName = obj["userName"] | "";
    c.userDisplayName = obj["userDisplayName"] | "";
    c.userId = fromHex(obj["userId"] | "");
    c.credId = fromHex(obj["credId"] | "");
    c.privKey = fromHex(obj["privKey"] | "");
    c.pubKeyX = fromHex(obj["pubKeyX"] | "");
    c.pubKeyY = fromHex(obj["pubKeyY"] | "");
    c.signCounter = obj["signCounter"] | 0;
    c.createdAt = obj["createdAt"] | 0;
    c.credProtect = obj["credProtect"] | 1;
    c.algorithm = obj["algorithm"] | -7;
    c.hmacSecretKey = fromHex(obj["hmacSecretKey"] | "");
    if (c.hmacSecretKey.empty()) {
      c.hmacSecretKey.resize(32);
      esp_fill_random(c.hmacSecretKey.data(), 32);
    }

    if (!c.credId.empty() && !c.privKey.empty()) {
      credentials.push_back(c);
    }
  }

  return true;
}

bool FidoStore::saveToStorage() {
  DynamicJsonDocument doc(16384);
  JsonArray arr = doc.to<JsonArray>();

  for (const auto &c : credentials) {
    JsonObject obj = arr.createNestedObject();
    obj["rpId"] = c.rpId;
    obj["userName"] = c.userName;
    obj["userDisplayName"] = c.userDisplayName;
    obj["userId"] = toHex(c.userId.data(), c.userId.size());
    obj["credId"] = toHex(c.credId.data(), c.credId.size());
    obj["privKey"] = toHex(c.privKey.data(), c.privKey.size());
    obj["pubKeyX"] = toHex(c.pubKeyX.data(), c.pubKeyX.size());
    obj["pubKeyY"] = toHex(c.pubKeyY.data(), c.pubKeyY.size());
    obj["hmacSecretKey"] = toHex(c.hmacSecretKey.data(), c.hmacSecretKey.size());
    obj["signCounter"] = c.signCounter;
    obj["createdAt"] = c.createdAt;
    obj["credProtect"] = c.credProtect;
    obj["algorithm"] = c.algorithm;
  }

  String jsonStr;
  serializeJson(doc, jsonStr);

  // 1. Save to LittleFS
  File f = LittleFS.open("/passkeys.json", "w");
  if (f) {
    f.print(jsonStr);
    f.close();
  }

  // 2. Dual-save to NVS flash backup (survives uploadfs and OTA littlefs updates!)
  Preferences prefs;
  if (prefs.begin("fido_nvs", false)) {
    prefs.putUInt("counter", globalCounter);
    prefs.putString("pkeys_json", jsonStr);
    prefs.end();
  }

  return true;
}

bool FidoStore::createCredential(
  const String &rpId,
  const std::vector<uint8_t> &userId,
  const String &userName,
  const String &userDisplayName,
  FidoCredential &outCred
) {
  outCred.rpId = rpId;
  outCred.userId = userId;
  outCred.userName = userName;
  outCred.userDisplayName = userDisplayName;
  outCred.signCounter = 0;
  outCred.createdAt = static_cast<uint32_t>(time(nullptr));

  // Generate unique 32-byte Credential ID
  outCred.credId.resize(32);
  esp_fill_random(outCred.credId.data(), 32);

  // Generate 32-byte master HMAC secret for hmac-secret / prf extension
  outCred.hmacSecretKey.resize(32);
  esp_fill_random(outCred.hmacSecretKey.data(), 32);

  // Generate secp256r1 keypair
  if (!generateKeyPair(outCred.privKey, outCred.pubKeyX, outCred.pubKeyY)) {
    return false;
  }

  // If a credential already exists for this RP and User, update it
  bool updated = false;
  for (auto &existing : credentials) {
    if (existing.rpId == rpId && existing.userId == userId) {
      existing = outCred;
      updated = true;
      break;
    }
  }

  if (!updated) {
    credentials.push_back(outCred);
  }

  saveToStorage();
  return true;
}

bool FidoStore::deleteCredential(const std::vector<uint8_t> &credId) {
  for (auto it = credentials.begin(); it != credentials.end(); ++it) {
    if (it->credId == credId) {
      credentials.erase(it);
      saveToStorage();
      return true;
    }
  }
  return false;
}

static std::vector<uint8_t> largeBlobCache;

bool FidoStore::getLargeBlob(std::vector<uint8_t> &outBlob) {
  if (largeBlobCache.empty()) {
    Preferences prefs;
    if (prefs.begin("fido_blob", true)) {
      size_t len = prefs.getBytesLength("blob");
      if (len > 0 && len <= 2048) {
        largeBlobCache.resize(len);
        prefs.getBytes("blob", largeBlobCache.data(), len);
      }
      prefs.end();
    }
  }
  outBlob = largeBlobCache;
  return true;
}

bool FidoStore::setLargeBlob(const uint8_t *blobData, size_t len) {
  if (len > 2048) return false;
  if (blobData && len > 0) {
    largeBlobCache.assign(blobData, blobData + len);
  } else {
    largeBlobCache.clear();
  }
  Preferences prefs;
  if (prefs.begin("fido_blob", false)) {
    if (largeBlobCache.empty()) {
      prefs.remove("blob");
    } else {
      prefs.putBytes("blob", largeBlobCache.data(), largeBlobCache.size());
    }
    prefs.end();
  }
  return true;
}

void FidoStore::clearLargeBlob() {
  largeBlobCache.clear();
  Preferences prefs;
  if (prefs.begin("fido_blob", false)) {
    prefs.clear();
    prefs.end();
  }
}

void FidoStore::clearAll() {
  credentials.clear();
  globalCounter = 1;
  clearPin();
  clearLargeBlob();

  LittleFS.remove("/passkeys.json");
  LittleFS.remove("/fido_credentials.json");

  Preferences prefs;
  if (prefs.begin("fido_nvs", false)) {
    prefs.clear();
    prefs.end();
  }
}

void FidoStore::hmacSha256(const uint8_t *key, size_t keyLen, const uint8_t *data, size_t dataLen, uint8_t *out32) {
  const mbedtls_md_info_t *md_info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  mbedtls_md_hmac(md_info, key, keyLen, data, dataLen, out32);
}

bool FidoStore::computeSharedSecret(
  const std::vector<uint8_t> &privKey,
  const std::vector<uint8_t> &peerPubX,
  const std::vector<uint8_t> &peerPubY,
  std::vector<uint8_t> &outSharedKey32
) {
  mbedtls_ecp_group grp;
  mbedtls_ecp_group_init(&grp);
  mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_SECP256R1);

  mbedtls_mpi d;
  mbedtls_mpi_init(&d);
  mbedtls_mpi_read_binary(&d, privKey.data(), privKey.size());

  mbedtls_ecp_point Q_peer;
  mbedtls_ecp_point_init(&Q_peer);
  mbedtls_mpi_read_binary(&Q_peer.X, peerPubX.data(), peerPubX.size());
  mbedtls_mpi_read_binary(&Q_peer.Y, peerPubY.data(), peerPubY.size());
  mbedtls_mpi_lset(&Q_peer.Z, 1);

  mbedtls_ecp_point P_shared;
  mbedtls_ecp_point_init(&P_shared);

  int ret = mbedtls_ecp_mul(&grp, &P_shared, &d, &Q_peer, mbedtls_esp_rng, nullptr);

  bool ok = false;
  if (ret == 0) {
    uint8_t zBuf[32];
    mbedtls_mpi_write_binary(&P_shared.X, zBuf, 32);
    outSharedKey32.resize(32);
    sha256(zBuf, 32, outSharedKey32.data());
    ok = true;
  }

  mbedtls_ecp_point_free(&P_shared);
  mbedtls_ecp_point_free(&Q_peer);
  mbedtls_mpi_free(&d);
  mbedtls_ecp_group_free(&grp);
  return ok;
}

bool FidoStore::aes256CbcDecrypt(
  const uint8_t *key32,
  const uint8_t *iv16,
  const uint8_t *input,
  size_t len,
  std::vector<uint8_t> &output
) {
  if (len == 0 || (len % 16) != 0) return false;
  output.resize(len);

  uint8_t iv[16];
  if (iv16) memcpy(iv, iv16, 16);
  else memset(iv, 0, 16);

  mbedtls_aes_context aes;
  mbedtls_aes_init(&aes);
  mbedtls_aes_setkey_dec(&aes, key32, 256);
  int ret = mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_DECRYPT, len, iv, input, output.data());
  mbedtls_aes_free(&aes);
  return ret == 0;
}

bool FidoStore::aes256CbcEncrypt(
  const uint8_t *key32,
  const uint8_t *iv16,
  const uint8_t *input,
  size_t len,
  std::vector<uint8_t> &output
) {
  if (len == 0 || (len % 16) != 0) return false;
  output.resize(len);

  uint8_t iv[16];
  if (iv16) memcpy(iv, iv16, 16);
  else memset(iv, 0, 16);

  mbedtls_aes_context aes;
  mbedtls_aes_init(&aes);
  mbedtls_aes_setkey_enc(&aes, key32, 256);
  int ret = mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_ENCRYPT, len, iv, input, output.data());
  mbedtls_aes_free(&aes);
  return ret == 0;
}

bool FidoStore::isPinSet() {
  return pinIsSet;
}

uint8_t FidoStore::getPinRetries() {
  return pinRetriesLeft;
}

void FidoStore::decrementPinRetries() {
  if (pinRetriesLeft > 0) pinRetriesLeft--;
  Preferences prefs;
  if (prefs.begin("fido_nvs", false)) {
    prefs.putUChar("pin_retries", pinRetriesLeft);
    prefs.end();
  }
}

void FidoStore::resetPinRetries() {
  pinRetriesLeft = 8;
  Preferences prefs;
  if (prefs.begin("fido_nvs", false)) {
    prefs.putUChar("pin_retries", 8);
    prefs.end();
  }
}

bool FidoStore::setPin(const uint8_t *decryptedPin64, size_t pinLen) {
  if (pinLen < 4) return false;
  sha256(decryptedPin64, pinLen, pinStoredHash);
  pinIsSet = true;
  pinRetriesLeft = 8;

  Preferences prefs;
  if (prefs.begin("fido_nvs", false)) {
    prefs.putBool("pin_set", true);
    prefs.putBytes("pin_hash", pinStoredHash, 32);
    prefs.putUChar("pin_retries", 8);
    prefs.end();
  }
  return true;
}

bool FidoStore::changePin(const uint8_t *oldPin64, size_t oldLen, const uint8_t *newPin64, size_t newLen) {
  if (!pinIsSet || pinRetriesLeft == 0) return false;
  uint8_t oldHash[32];
  sha256(oldPin64, oldLen, oldHash);
  if (memcmp(oldHash, pinStoredHash, 32) != 0) {
    decrementPinRetries();
    return false;
  }
  return setPin(newPin64, newLen);
}

bool FidoStore::verifyPinHash(const uint8_t *pinHash16) {
  if (!pinIsSet || pinRetriesLeft == 0) return false;
  if (memcmp(pinStoredHash, pinHash16, 16) == 0) {
    resetPinRetries();
    return true;
  }
  decrementPinRetries();
  return false;
}

void FidoStore::clearPin() {
  pinIsSet = false;
  pinRetriesLeft = 8;
  pinTokenValid = false;
  memset(pinStoredHash, 0, 32);
  memset(currentPinToken, 0, 32);

  Preferences prefs;
  if (prefs.begin("fido_nvs", false)) {
    prefs.putBool("pin_set", false);
    prefs.remove("pin_hash");
    prefs.putUInt("pin_retries", 8);
    prefs.end();
  }
}

void FidoStore::getPinToken(uint8_t *out32) {
  if (!pinTokenValid) {
    esp_fill_random(currentPinToken, 32);
    pinTokenValid = true;
  }
  memcpy(out32, currentPinToken, 32);
}

bool FidoStore::verifyPinAuth(const uint8_t *clientDataHash32, const uint8_t *pinAuth16) {
  if (!pinIsSet) return true; // If PIN not set, pinAuth is not required
  if (!pinTokenValid) return false;

  uint8_t computedAuth[32];
  hmacSha256(currentPinToken, 32, clientDataHash32, 32, computedAuth);
  return memcmp(computedAuth, pinAuth16, 16) == 0;
}

bool FidoStore::isPinTokenValid() {
  return pinTokenValid;
}

void FidoStore::invalidatePinToken() {
  pinTokenValid = false;
  memset(currentPinToken, 0, 32);
}

FidoCredential* FidoStore::findCredential(const std::vector<uint8_t> &credId) {
  for (auto &c : credentials) {
    if (c.credId == credId) {
      return &c;
    }
  }
  return nullptr;
}

FidoCredential* FidoStore::findCredentialByRp(const String &rpId) {
  for (auto &c : credentials) {
    if (c.rpId == rpId) {
      return &c;
    }
  }
  return nullptr;
}

std::vector<FidoCredential>& FidoStore::getAllCredentials() {
  return credentials;
}

uint32_t FidoStore::getNextGlobalCounter() {
  globalCounter++;
  Preferences prefs;
  if (prefs.begin("fido_nvs", false)) {
    prefs.putUInt("counter", globalCounter);
    prefs.end();
  }
  return globalCounter;
}

bool FidoStore::getEmulateUv() {
  return emulateBiometricUv;
}

void FidoStore::setEmulateUv(bool enable) {
  emulateBiometricUv = enable;
  Preferences prefs;
  if (prefs.begin("fido_nvs", false)) {
    prefs.putBool("emulate_uv", enable);
    prefs.end();
  }
}

void FidoStore::importCredentials(const std::vector<FidoCredential> &list, bool replace) {
  if (replace) {
    credentials.clear();
  }
  for (const auto &c : list) {
    if (!c.credId.empty() && !c.privKey.empty()) {
      // Check if already exists by credId
      bool exists = false;
      for (auto &existing : credentials) {
        if (existing.credId == c.credId) {
          existing = c;
          exists = true;
          break;
        }
      }
      if (!exists) {
        credentials.push_back(c);
      }
    }
  }
  saveToStorage();
}
