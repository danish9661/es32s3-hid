#include "FidoCrypto.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include "esp_random.h"

static std::vector<FidoCredential> credentials;
static uint32_t globalCounter = 1;
static bool initialized = false;

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
    prefs.end();
  }

  if (!LittleFS.exists("/passkeys.json")) {
    return false;
  }

  File f = LittleFS.open("/passkeys.json", "r");
  if (!f) return false;

  DynamicJsonDocument doc(16384);
  DeserializationError err = deserializeJson(doc, f);
  f.close();

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

    if (!c.credId.empty() && !c.privKey.empty()) {
      credentials.push_back(c);
    }
  }

  return true;
}

bool FidoStore::saveToStorage() {
  Preferences prefs;
  if (prefs.begin("fido_nvs", false)) {
    prefs.putUInt("counter", globalCounter);
    prefs.end();
  }

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
    obj["signCounter"] = c.signCounter;
    obj["createdAt"] = c.createdAt;
  }

  File f = LittleFS.open("/passkeys.json", "w");
  if (!f) return false;
  serializeJson(doc, f);
  f.close();

  return true;
}

void FidoStore::clearAll() {
  credentials.clear();
  globalCounter = 1;
  if (LittleFS.exists("/passkeys.json")) {
    LittleFS.remove("/passkeys.json");
  }
  Preferences prefs;
  if (prefs.begin("fido_nvs", false)) {
    prefs.clear();
    prefs.end();
  }
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
