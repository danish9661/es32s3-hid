#include "YubiKey.h"
#include <LittleFS.h>
#include "mbedtls/aes.h"
#include "mbedtls/md.h"
#include "esp_system.h"
#include "esp_random.h"

YubiKeyEngine YubiKey;

static const char MODHEX_ALPHABET[] = "cbdefghijklnrtuv";

YubiKeyEngine::YubiKeyEngine()
  : yubikeyModeEnabled(false),
    serialNumber(17869661),
    useCounter(1),
    sessionCounter(0) {
  generateDefaultSlotConfig();
}

void YubiKeyEngine::generateDefaultSlotConfig() {
  // Slot 1: Default Yubico OTP
  slot1.mode = YUBI_SLOT_OTP;
  slot1.publicId = "cccccccccccb";
  slot1.secretKeyHex = "000102030405060708090a0b0c0d0e0f";
  slot1.privateIdHex = "010203040506";
  slot1.staticPassword = "";
  slot1.sendEnter = true;

  // Slot 2: Default Static Password
  slot2.mode = YUBI_SLOT_STATIC;
  slot2.publicId = "";
  slot2.secretKeyHex = "";
  slot2.privateIdHex = "";
  slot2.staticPassword = "MySecretMasterPassword123!";
  slot2.sendEnter = true;
}

void YubiKeyEngine::init() {
  loadConfig();
  useCounter++;
  sessionCounter = 0;
  saveConfig();
}

void YubiKeyEngine::setYubiKeyMode(bool enabled) {
  yubikeyModeEnabled = enabled;
  saveConfig();
}

YubiKeySlotConfig YubiKeyEngine::getSlotConfig(int slot) const {
  if (slot == 2) return slot2;
  return slot1;
}

bool YubiKeyEngine::setSlotConfig(int slot, const YubiKeySlotConfig &config) {
  if (slot == 2) {
    slot2 = config;
  } else {
    slot1 = config;
  }
  saveConfig();
  return true;
}

void YubiKeyEngine::configureDefaultOtpSlot(int slot) {
  uint8_t rndKey[16];
  uint8_t rndUid[6];
  esp_fill_random(rndKey, sizeof(rndKey));
  esp_fill_random(rndUid, sizeof(rndUid));

  String keyHex = "";
  for (int i = 0; i < 16; i++) {
    char buf[3];
    sprintf(buf, "%02x", rndKey[i]);
    keyHex += buf;
  }

  String uidHex = "";
  for (int i = 0; i < 6; i++) {
    char buf[3];
    sprintf(buf, "%02x", rndUid[i]);
    uidHex += buf;
  }

  YubiKeySlotConfig cfg;
  cfg.mode = YUBI_SLOT_OTP;
  cfg.publicId = (slot == 2) ? "cccccccccccf" : "cccccccccccb";
  cfg.secretKeyHex = keyHex;
  cfg.privateIdHex = uidHex;
  cfg.staticPassword = "";
  cfg.sendEnter = true;

  setSlotConfig(slot, cfg);
}

// --- MODHEX HELPERS ---

String YubiKeyEngine::hexToModhex(const String &hexStr) {
  String out = "";
  for (size_t i = 0; i < hexStr.length(); i++) {
    char c = tolower(hexStr[i]);
    int val = -1;
    if (c >= '0' && c <= '9') val = c - '0';
    else if (c >= 'a' && c <= 'f') val = c - 'a' + 10;
    if (val >= 0 && val < 16) {
      out += MODHEX_ALPHABET[val];
    }
  }
  return out;
}

String YubiKeyEngine::modhexToHex(const String &modhexStr) {
  String out = "";
  for (size_t i = 0; i < modhexStr.length(); i++) {
    char c = tolower(modhexStr[i]);
    const char *pos = strchr(MODHEX_ALPHABET, c);
    if (pos) {
      int val = pos - MODHEX_ALPHABET;
      char h[2];
      sprintf(h, "%x", val);
      out += h[0];
    }
  }
  return out;
}

bool YubiKeyEngine::isValidModhex(const String &modhexStr) {
  if (modhexStr.isEmpty()) return false;
  for (size_t i = 0; i < modhexStr.length(); i++) {
    if (!strchr(MODHEX_ALPHABET, tolower(modhexStr[i]))) {
      return false;
    }
  }
  return true;
}

// --- CRC16 CALCULATION ---

uint16_t YubiKeyEngine::calculateCrc16(const uint8_t *buffer, size_t len) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < len; i++) {
    crc ^= buffer[i];
    for (int j = 0; j < 8; j++) {
      if (crc & 1) {
        crc = (crc >> 1) ^ 0x8408;
      } else {
        crc = (crc >> 1);
      }
    }
  }
  return crc;
}

// --- OTP & STATIC PASSWORDS ---

String YubiKeyEngine::generateOtpString(int slot) {
  YubiKeySlotConfig cfg = getSlotConfig(slot);
  if (cfg.mode != YUBI_SLOT_OTP) return "";

  sessionCounter++;

  // 16-byte unencrypted token structure
  uint8_t plain[16];
  memset(plain, 0, sizeof(plain));

  // 1. Private ID (6 bytes)
  for (int i = 0; i < 6; i++) {
    if (i * 2 + 1 < (int)cfg.privateIdHex.length()) {
      String byteStr = cfg.privateIdHex.substring(i * 2, i * 2 + 2);
      plain[i] = static_cast<uint8_t>(strtol(byteStr.c_str(), nullptr, 16));
    }
  }

  // 2. Use Counter (2 bytes, LE)
  plain[6] = static_cast<uint8_t>(useCounter & 0xFF);
  plain[7] = static_cast<uint8_t>((useCounter >> 8) & 0xFF);

  // 3. Timestamp (3 bytes, 8Hz clock tick, LE)
  uint32_t ts = millis() / 125;
  plain[8] = static_cast<uint8_t>(ts & 0xFF);
  plain[9] = static_cast<uint8_t>((ts >> 8) & 0xFF);
  plain[10] = static_cast<uint8_t>((ts >> 16) & 0xFF);

  // 4. Session Counter (1 byte)
  plain[11] = sessionCounter;

  // 5. Random 16-bit number (2 bytes)
  uint16_t rnd = static_cast<uint16_t>(esp_random() & 0xFFFF);
  plain[12] = static_cast<uint8_t>(rnd & 0xFF);
  plain[13] = static_cast<uint8_t>((rnd >> 8) & 0xFF);

  // 6. CRC-16 Checksum (2 bytes)
  uint16_t crc = calculateCrc16(plain, 14);
  crc = ~crc; // Inverted CRC
  plain[14] = static_cast<uint8_t>(crc & 0xFF);
  plain[15] = static_cast<uint8_t>((crc >> 8) & 0xFF);

  // 7. Parse 16-byte AES key
  uint8_t key[16];
  memset(key, 0, sizeof(key));
  for (int i = 0; i < 16; i++) {
    if (i * 2 + 1 < (int)cfg.secretKeyHex.length()) {
      String byteStr = cfg.secretKeyHex.substring(i * 2, i * 2 + 2);
      key[i] = static_cast<uint8_t>(strtol(byteStr.c_str(), nullptr, 16));
    }
  }

  // 8. AES-128-ECB Encrypt
  uint8_t encrypted[16];
  mbedtls_aes_context aes;
  mbedtls_aes_init(&aes);
  mbedtls_aes_setkey_enc(&aes, key, 128);
  mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_ENCRYPT, plain, encrypted);
  mbedtls_aes_free(&aes);

  // 9. Convert Encrypted Block to 32 Modhex characters
  String encryptedHex = "";
  for (int i = 0; i < 16; i++) {
    char h[3];
    sprintf(h, "%02x", encrypted[i]);
    encryptedHex += h;
  }
  String encryptedModhex = hexToModhex(encryptedHex);

  // 10. Combine with Public ID (12 Modhex chars)
  String publicId = cfg.publicId;
  publicId.toLowerCase();
  while (publicId.length() < 12) publicId = "c" + publicId;
  if (publicId.length() > 12) publicId = publicId.substring(0, 12);

  return publicId + encryptedModhex;
}

String YubiKeyEngine::getStaticPasswordString(int slot) {
  YubiKeySlotConfig cfg = getSlotConfig(slot);
  if (cfg.mode == YUBI_SLOT_STATIC) {
    return cfg.staticPassword;
  }
  return "";
}

// --- BASE32 DECODER FOR OATH ---

std::vector<uint8_t> YubiKeyEngine::base32Decode(const String &b32) {
  std::vector<uint8_t> out;
  int buffer = 0;
  int bitsLeft = 0;

  for (size_t i = 0; i < b32.length(); i++) {
    char c = toupper(b32[i]);
    if (c == ' ' || c == '-' || c == '=') continue;
    int val = -1;
    if (c >= 'A' && c <= 'Z') val = c - 'A';
    else if (c >= '2' && c <= '7') val = c - '2' + 26;
    if (val < 0) continue;

    buffer = (buffer << 5) | val;
    bitsLeft += 5;
    if (bitsLeft >= 8) {
      out.push_back(static_cast<uint8_t>((buffer >> (bitsLeft - 8)) & 0xFF));
      bitsLeft -= 8;
    }
  }
  return out;
}

// --- OATH-TOTP CALCULATION ---

String YubiKeyEngine::calculateTotp(const YubiOathAccount &acc, uint32_t currentEpoch) {
  if (currentEpoch == 0) return "000000";
  uint32_t period = (acc.period == 0) ? 30 : acc.period;
  uint64_t counter = currentEpoch / period;

  // 8-byte Big Endian counter
  uint8_t counterBytes[8];
  for (int i = 7; i >= 0; i--) {
    counterBytes[i] = static_cast<uint8_t>(counter & 0xFF);
    counter >>= 8;
  }

  std::vector<uint8_t> key = base32Decode(acc.secretBase32);
  if (key.empty()) return "000000";

  mbedtls_md_type_t mdType = (acc.algorithm == "SHA256") ? MBEDTLS_MD_SHA256 : MBEDTLS_MD_SHA1;
  size_t hashLen = (acc.algorithm == "SHA256") ? 32 : 20;
  uint8_t hmacRes[32];

  const mbedtls_md_info_t *mdInfo = mbedtls_md_info_from_type(mdType);
  if (!mdInfo) return "000000";

  mbedtls_md_context_t mdCtx;
  mbedtls_md_init(&mdCtx);
  mbedtls_md_setup(&mdCtx, mdInfo, 1);
  mbedtls_md_hmac_starts(&mdCtx, key.data(), key.size());
  mbedtls_md_hmac_update(&mdCtx, counterBytes, sizeof(counterBytes));
  mbedtls_md_hmac_finish(&mdCtx, hmacRes);
  mbedtls_md_free(&mdCtx);

  // Dynamic Truncation
  int offset = hmacRes[hashLen - 1] & 0x0F;
  uint32_t code = ((hmacRes[offset] & 0x7F) << 24) |
                  ((hmacRes[offset + 1] & 0xFF) << 16) |
                  ((hmacRes[offset + 2] & 0xFF) << 8) |
                  (hmacRes[offset + 3] & 0xFF);

  uint32_t mod = (acc.digits == 8) ? 100000000 : 1000000;
  code %= mod;

  char fmt[16];
  if (acc.digits == 8) {
    sprintf(fmt, "%08u", code);
  } else {
    sprintf(fmt, "%06u", code);
  }
  return String(fmt);
}

// --- OATH ACCOUNTS JSON ---

String YubiKeyEngine::getOathAccountsJson(uint32_t currentEpoch) {
  DynamicJsonDocument doc(8192);
  JsonArray arr = doc.createNestedArray("accounts");

  for (const auto &acc : oathAccounts) {
    JsonObject obj = arr.createNestedObject();
    obj["name"] = acc.name;
    obj["issuer"] = acc.issuer;
    obj["digits"] = acc.digits;
    obj["period"] = acc.period;
    obj["algorithm"] = acc.algorithm;
    if (currentEpoch > 0) {
      obj["code"] = calculateTotp(acc, currentEpoch);
      obj["remaining_sec"] = acc.period - (currentEpoch % acc.period);
    }
  }

  String out;
  serializeJson(doc, out);
  return out;
}

bool YubiKeyEngine::addOathAccount(const String &name, const String &secretBase32, const String &issuer, uint8_t digits, uint32_t period, const String &algo) {
  if (name.isEmpty() || secretBase32.isEmpty()) return false;
  // Remove duplicate if exists
  deleteOathAccount(name);

  YubiOathAccount acc;
  acc.name = name;
  acc.issuer = issuer;
  acc.secretBase32 = secretBase32;
  acc.digits = (digits == 8) ? 8 : 6;
  acc.period = (period == 60) ? 60 : 30;
  acc.algorithm = (algo == "SHA256") ? "SHA256" : "SHA1";

  oathAccounts.push_back(acc);
  saveConfig();
  return true;
}

bool YubiKeyEngine::deleteOathAccount(const String &name) {
  for (auto it = oathAccounts.begin(); it != oathAccounts.end(); ++it) {
    if (it->name == name) {
      oathAccounts.erase(it);
      saveConfig();
      return true;
    }
  }
  return false;
}

void YubiKeyEngine::clearAllOathAccounts() {
  oathAccounts.clear();
  saveConfig();
}

// --- STATUS JSON ---

String YubiKeyEngine::getStatusJson() {
  DynamicJsonDocument doc(2048);
  doc["yubikey_mode"] = yubikeyModeEnabled;
  doc["serial_number"] = serialNumber;
  doc["version"] = getVersionString();
  doc["use_counter"] = useCounter;

  JsonObject s1 = doc.createNestedObject("slot1");
  s1["mode"] = static_cast<int>(slot1.mode);
  s1["public_id"] = slot1.publicId;
  s1["static_configured"] = !slot1.staticPassword.isEmpty();
  s1["send_enter"] = slot1.sendEnter;

  JsonObject s2 = doc.createNestedObject("slot2");
  s2["mode"] = static_cast<int>(slot2.mode);
  s2["public_id"] = slot2.publicId;
  s2["static_configured"] = !slot2.staticPassword.isEmpty();
  s2["send_enter"] = slot2.sendEnter;

  doc["oath_count"] = oathAccounts.size();

  String out;
  serializeJson(doc, out);
  return out;
}

// --- PERSISTENCE ---

void YubiKeyEngine::saveConfig() {
  if (!LittleFS.exists("/vault")) {
    LittleFS.mkdir("/vault");
  }

  DynamicJsonDocument doc(8192);
  doc["yubikey_mode"] = yubikeyModeEnabled;
  doc["serial_number"] = serialNumber;
  doc["use_counter"] = useCounter;

  JsonObject s1 = doc.createNestedObject("slot1");
  s1["mode"] = static_cast<int>(slot1.mode);
  s1["public_id"] = slot1.publicId;
  s1["secret_key"] = slot1.secretKeyHex;
  s1["private_id"] = slot1.privateIdHex;
  s1["static_password"] = slot1.staticPassword;
  s1["send_enter"] = slot1.sendEnter;

  JsonObject s2 = doc.createNestedObject("slot2");
  s2["mode"] = static_cast<int>(slot2.mode);
  s2["public_id"] = slot2.publicId;
  s2["secret_key"] = slot2.secretKeyHex;
  s2["private_id"] = slot2.privateIdHex;
  s2["static_password"] = slot2.staticPassword;
  s2["send_enter"] = slot2.sendEnter;

  JsonArray oathArr = doc.createNestedArray("oath");
  for (const auto &acc : oathAccounts) {
    JsonObject a = oathArr.createNestedObject();
    a["name"] = acc.name;
    a["issuer"] = acc.issuer;
    a["secret"] = acc.secretBase32;
    a["digits"] = acc.digits;
    a["period"] = acc.period;
    a["algo"] = acc.algorithm;
  }

  File f = LittleFS.open("/vault/yubikey.json", "w");
  if (f) {
    serializeJson(doc, f);
    f.close();
  }
}

void YubiKeyEngine::loadConfig() {
  if (!LittleFS.exists("/vault/yubikey.json")) return;

  File f = LittleFS.open("/vault/yubikey.json", "r");
  if (!f) return;

  DynamicJsonDocument doc(8192);
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err) return;

  yubikeyModeEnabled = doc["yubikey_mode"] | false;
  serialNumber = doc["serial_number"] | 17869661;
  useCounter = doc["use_counter"] | 1;

  if (doc.containsKey("slot1")) {
    JsonObject s1 = doc["slot1"];
    slot1.mode = static_cast<YubiKeySlotMode>(s1["mode"] | 1);
    slot1.publicId = s1["public_id"] | "cccccccccccb";
    slot1.secretKeyHex = s1["secret_key"] | "000102030405060708090a0b0c0d0e0f";
    slot1.privateIdHex = s1["private_id"] | "010203040506";
    slot1.staticPassword = s1["static_password"] | "";
    slot1.sendEnter = s1["send_enter"] | true;
  }

  if (doc.containsKey("slot2")) {
    JsonObject s2 = doc["slot2"];
    slot2.mode = static_cast<YubiKeySlotMode>(s2["mode"] | 2);
    slot2.publicId = s2["public_id"] | "";
    slot2.secretKeyHex = s2["secret_key"] | "";
    slot2.privateIdHex = s2["private_id"] | "";
    slot2.staticPassword = s2["static_password"] | "";
    slot2.sendEnter = s2["send_enter"] | true;
  }

  oathAccounts.clear();
  if (doc.containsKey("oath")) {
    JsonArray oathArr = doc["oath"];
    for (JsonObject a : oathArr) {
      YubiOathAccount acc;
      acc.name = a["name"] | "";
      acc.issuer = a["issuer"] | "";
      acc.secretBase32 = a["secret"] | "";
      acc.digits = a["digits"] | 6;
      acc.period = a["period"] | 30;
      acc.algorithm = a["algo"] | "SHA1";
      if (!acc.name.isEmpty() && !acc.secretBase32.isEmpty()) {
        oathAccounts.push_back(acc);
      }
    }
  }
}

// --- APDU & CCID SMARTCARD PROTOCOL ENGINE ---

static const uint8_t AID_MGMT[] = {0xA0, 0x00, 0x00, 0x05, 0x27, 0x47, 0x11, 0x17};
static const uint8_t AID_OATH[] = {0xA0, 0x00, 0x00, 0x05, 0x27, 0x21, 0x01};
static const uint8_t AID_OTP[]  = {0xA0, 0x00, 0x00, 0x05, 0x27, 0x20, 0x01};
static const uint8_t AID_PIV[]  = {0xA0, 0x00, 0x00, 0x03, 0x08};

enum SelectedApplet {
  APPLET_NONE = 0,
  APPLET_MGMT = 1,
  APPLET_OATH = 2,
  APPLET_OTP  = 3,
  APPLET_PIV  = 4
};

static SelectedApplet currentApplet = APPLET_MGMT;

std::vector<uint8_t> YubiKeyEngine::processApdu(const uint8_t *apdu, size_t len) {
  std::vector<uint8_t> resp;
  if (!apdu || len < 4) {
    resp.push_back(0x67); // Wrong length
    resp.push_back(0x00);
    return resp;
  }

  uint8_t cla = apdu[0];
  uint8_t ins = apdu[1];
  uint8_t p1  = apdu[2];
  uint8_t p2  = apdu[3];

  size_t dataLen = 0;
  const uint8_t *data = nullptr;
  if (len > 4) {
    dataLen = apdu[4];
    if (len >= 5 + dataLen) {
      data = apdu + 5;
    }
  }

  // 1. SELECT APPLET (ISO 7816-4: CLA=0x00, INS=0xA4, P1=0x04)
  if (ins == 0xA4 && p1 == 0x04) {
    if (data && dataLen >= 8 && memcmp(data, AID_MGMT, 8) == 0) {
      currentApplet = APPLET_MGMT;
      // Management Applet Select response: Version 5.4.3
      resp.push_back(0x05);
      resp.push_back(0x04);
      resp.push_back(0x03);
      resp.push_back(0x90);
      resp.push_back(0x00);
      return resp;
    } else if (data && dataLen >= 7 && memcmp(data, AID_OATH, 7) == 0) {
      currentApplet = APPLET_OATH;
      // OATH Applet Select response: Tag 0x79 (Version) + Tag 0x71 (ID)
      resp.push_back(0x79); // Version tag
      resp.push_back(0x03);
      resp.push_back(0x05);
      resp.push_back(0x04);
      resp.push_back(0x03);

      resp.push_back(0x71); // Name / Salt Tag
      resp.push_back(0x08);
      for (int i = 0; i < 8; i++) {
        resp.push_back((serialNumber >> (i * 4)) & 0xFF);
      }
      resp.push_back(0x90);
      resp.push_back(0x00);
      return resp;
    } else if (data && dataLen >= 7 && memcmp(data, AID_OTP, 7) == 0) {
      currentApplet = APPLET_OTP;
      resp.push_back(0x05);
      resp.push_back(0x04);
      resp.push_back(0x03);
      resp.push_back(0x90);
      resp.push_back(0x00);
      return resp;
    } else if (data && dataLen >= 5 && memcmp(data, AID_PIV, 5) == 0) {
      currentApplet = APPLET_PIV;
      resp.push_back(0x61); // Application template
      resp.push_back(0x05);
      resp.push_back(0x4F);
      resp.push_back(0x03);
      resp.push_back(0x05);
      resp.push_back(0x04);
      resp.push_back(0x03);
      resp.push_back(0x90);
      resp.push_back(0x00);
      return resp;
    } else {
      resp.push_back(0x6A); // File not found
      resp.push_back(0x82);
      return resp;
    }
  }

  // 2. MANAGEMENT APPLET DISPATCH
  if (currentApplet == APPLET_MGMT || cla == 0x00) {
    if (ins == 0x1D || ins == 0x1E) { // GET DEVICE INFO / READ CONFIG
      std::vector<uint8_t> tlv;

      // Tag 0x01: Firmware version (5.4.3)
      tlv.push_back(0x01);
      tlv.push_back(0x03);
      tlv.push_back(0x05);
      tlv.push_back(0x04);
      tlv.push_back(0x03);

      // Tag 0x02: Serial number (4 bytes big-endian)
      tlv.push_back(0x02);
      tlv.push_back(0x04);
      tlv.push_back((serialNumber >> 24) & 0xFF);
      tlv.push_back((serialNumber >> 16) & 0xFF);
      tlv.push_back((serialNumber >> 8) & 0xFF);
      tlv.push_back(serialNumber & 0xFF);

      // Tag 0x03: Form factor (0x01 = USB-A Keychain)
      tlv.push_back(0x03);
      tlv.push_back(0x01);
      tlv.push_back(0x01);

      // Tag 0x04: USB Supported Capabilities (0x00FF = OTP | U2F | FIDO2 | OATH | OPENPGP | PIV)
      tlv.push_back(0x04);
      tlv.push_back(0x02);
      tlv.push_back(0x00);
      tlv.push_back(0xFF);

      // Tag 0x05: USB Enabled Capabilities (0x00A7 = OTP | FIDO2 | OATH)
      tlv.push_back(0x05);
      tlv.push_back(0x02);
      tlv.push_back(0x00);
      tlv.push_back(0xA7);

      // Tag 0x07: Device Flags
      tlv.push_back(0x07);
      tlv.push_back(0x01);
      tlv.push_back(0x00);

      resp.push_back(tlv.size() & 0xFF);
      resp.insert(resp.end(), tlv.begin(), tlv.end());
      resp.push_back(0x90);
      resp.push_back(0x00);
      return resp;
    }
  }

  // 3. OATH APPLET DISPATCH
  if (currentApplet == APPLET_OATH) {
    if (ins == 0x0A || ins == 0xA1) { // LIST
      for (const auto &acc : oathAccounts) {
        resp.push_back(0x71); // Tag 0x71: Account Name
        resp.push_back(acc.name.length() + 1);
        resp.push_back(0x21); // TOTP HMAC-SHA1
        for (size_t c = 0; c < acc.name.length(); c++) {
          resp.push_back(static_cast<uint8_t>(acc.name[c]));
        }
      }
      resp.push_back(0x90);
      resp.push_back(0x00);
      return resp;
    } else if (ins == 0x04 || ins == 0xA2) { // CALCULATE
      time_t nowSec = time(nullptr);
      if (nowSec < 100000) nowSec = 1715000000;

      if (oathAccounts.empty()) {
        resp.push_back(0x69);
        resp.push_back(0x84);
        return resp;
      }

      String codeStr = calculateTotp(oathAccounts[0], nowSec);
      uint32_t codeNum = codeStr.toInt();

      resp.push_back(0x76); // Tag 0x76: Truncated TOTP
      resp.push_back(0x05);
      resp.push_back(oathAccounts[0].digits); // 6
      resp.push_back((codeNum >> 24) & 0xFF);
      resp.push_back((codeNum >> 16) & 0xFF);
      resp.push_back((codeNum >> 8) & 0xFF);
      resp.push_back(codeNum & 0xFF);
      resp.push_back(0x90);
      resp.push_back(0x00);
      return resp;
    } else if (ins == 0x01) { // PUT (Add account)
      resp.push_back(0x90);
      resp.push_back(0x00);
      return resp;
    } else if (ins == 0x02) { // DELETE (Remove account)
      resp.push_back(0x90);
      resp.push_back(0x00);
      return resp;
    }
  }

  // Default response: Success (0x9000) or Unsupported (0x6D00)
  resp.push_back(0x90);
  resp.push_back(0x00);
  return resp;
}

// CCID Frame Processor (USB Smart Card Class 0x0B)
std::vector<uint8_t> YubiKeyEngine::processCcidMessage(const uint8_t *msg, size_t len) {
  std::vector<uint8_t> resp;
  if (!msg || len < 10) return resp;

  uint8_t msgType = msg[0];
  uint32_t payloadLen = msg[1] | (msg[2] << 8) | (msg[3] << 16) | (msg[4] << 24);
  uint8_t slot = msg[5];
  uint8_t seq = msg[6];

  switch (msgType) {
    case 0x62: { // PC_to_RDR_IccPowerOn
      // Return RDR_to_PC_DataBlock (0x80) with YubiKey 5 ATR
      static const uint8_t YUBI_ATR[] = {
        0x3B, 0x8D, 0x01, 0x80, 0xF2, 0x80, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
      };
      resp.push_back(0x80); // RDR_to_PC_DataBlock
      resp.push_back(sizeof(YUBI_ATR) & 0xFF);
      resp.push_back(0x00);
      resp.push_back(0x00);
      resp.push_back(0x00);
      resp.push_back(slot);
      resp.push_back(seq);
      resp.push_back(0x00); // Status: Active
      resp.push_back(0x00); // Error: None
      resp.push_back(0x00);
      resp.insert(resp.end(), YUBI_ATR, YUBI_ATR + sizeof(YUBI_ATR));
      break;
    }

    case 0x65: { // PC_to_RDR_GetSlotStatus
      resp.push_back(0x81); // RDR_to_PC_SlotStatus
      resp.push_back(0x00);
      resp.push_back(0x00);
      resp.push_back(0x00);
      resp.push_back(0x00);
      resp.push_back(slot);
      resp.push_back(seq);
      resp.push_back(0x00); // Status: Card present and active
      resp.push_back(0x00);
      resp.push_back(0x00);
      break;
    }

    case 0x6F: { // PC_to_RDR_XfrBlock (Send APDU to Smart Card)
      const uint8_t *apdu = msg + 10;
      size_t apduLen = std::min(static_cast<size_t>(payloadLen), len - 10);
      std::vector<uint8_t> apduResp = processApdu(apdu, apduLen);

      resp.push_back(0x80); // RDR_to_PC_DataBlock
      resp.push_back(apduResp.size() & 0xFF);
      resp.push_back((apduResp.size() >> 8) & 0xFF);
      resp.push_back(0x00);
      resp.push_back(0x00);
      resp.push_back(slot);
      resp.push_back(seq);
      resp.push_back(0x00); // Status: Active
      resp.push_back(0x00); // Error: None
      resp.push_back(0x00);
      resp.insert(resp.end(), apduResp.begin(), apduResp.end());
      break;
    }

    default: {
      // Return Slot Status with Error
      resp.push_back(0x81);
      resp.push_back(0x00);
      resp.push_back(0x00);
      resp.push_back(0x00);
      resp.push_back(0x00);
      resp.push_back(slot);
      resp.push_back(seq);
      resp.push_back(0x00);
      resp.push_back(0x00);
      resp.push_back(0x00);
      break;
    }
  }

  return resp;
}

