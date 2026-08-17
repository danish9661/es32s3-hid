#include "KeePassXC.h"
#include <Preferences.h>
#include "mbedtls/md.h"
#include "esp_random.h"

static bool keepassConfigured = false;
static uint8_t keepassSecret[20] = {0};

void KeePassXC::init() {
  Preferences prefs;
  if (prefs.begin("keepass_nvs", true)) {
    if (prefs.isKey("secret")) {
      size_t readLen = prefs.getBytes("secret", keepassSecret, 20);
      keepassConfigured = (readLen == 20);
    } else {
      keepassConfigured = false;
    }
    prefs.end();
  }
}

bool KeePassXC::isConfigured() {
  return keepassConfigured;
}

bool KeePassXC::setSecret(const uint8_t *secret20) {
  if (!secret20) return false;
  memcpy(keepassSecret, secret20, 20);
  keepassConfigured = true;

  Preferences prefs;
  if (prefs.begin("keepass_nvs", false)) {
    prefs.putBytes("secret", keepassSecret, 20);
    prefs.end();
    return true;
  }
  return false;
}

bool KeePassXC::setSecretHex(const String &hexStr) {
  String clean = hexStr;
  clean.trim();
  if (clean.length() != 40) return false;

  uint8_t buf[20];
  for (size_t i = 0; i < 20; i++) {
    char sub[3] = { clean[i*2], clean[i*2+1], '\0' };
    buf[i] = static_cast<uint8_t>(strtol(sub, nullptr, 16));
  }
  return setSecret(buf);
}

bool KeePassXC::getSecret(uint8_t *outSecret20) {
  if (!keepassConfigured || !outSecret20) return false;
  memcpy(outSecret20, keepassSecret, 20);
  return true;
}

String KeePassXC::getSecretHex() {
  if (!keepassConfigured) return "";
  String hex = "";
  for (size_t i = 0; i < 20; i++) {
    char buf[3];
    snprintf(buf, sizeof(buf), "%02x", keepassSecret[i]);
    hex += buf;
  }
  return hex;
}

String KeePassXC::getSecretBase32() {
  if (!keepassConfigured) return "";
  const char b32chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
  String res = "";
  uint32_t buffer = 0;
  int bitsLeft = 0;

  for (size_t i = 0; i < 20; i++) {
    buffer = (buffer << 8) | keepassSecret[i];
    bitsLeft += 8;
    while (bitsLeft >= 5) {
      res += b32chars[(buffer >> (bitsLeft - 5)) & 0x1F];
      bitsLeft -= 5;
    }
  }
  if (bitsLeft > 0) {
    res += b32chars[(buffer << (5 - bitsLeft)) & 0x1F];
  }
  return res;
}

void KeePassXC::generateRandomSecret(uint8_t *outSecret20) {
  esp_fill_random(keepassSecret, 20);
  keepassConfigured = true;

  Preferences prefs;
  if (prefs.begin("keepass_nvs", false)) {
    prefs.putBytes("secret", keepassSecret, 20);
    prefs.end();
  }
  if (outSecret20) {
    memcpy(outSecret20, keepassSecret, 20);
  }
}

void KeePassXC::clearSecret() {
  memset(keepassSecret, 0, 20);
  keepassConfigured = false;

  Preferences prefs;
  if (prefs.begin("keepass_nvs", false)) {
    prefs.remove("secret");
    prefs.end();
  }
}

bool KeePassXC::computeResponse(const uint8_t *challenge, size_t challengeLen, uint8_t *outResponse20) {
  if (!keepassConfigured || !challenge || !outResponse20) return false;

  mbedtls_md_context_t ctx;
  mbedtls_md_init(&ctx);
  const mbedtls_md_info_t *info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA1);
  if (!info) {
    mbedtls_md_free(&ctx);
    return false;
  }

  if (mbedtls_md_setup(&ctx, info, 1) != 0) {
    mbedtls_md_free(&ctx);
    return false;
  }

  if (mbedtls_md_hmac_starts(&ctx, keepassSecret, 20) != 0 ||
      mbedtls_md_hmac_update(&ctx, challenge, challengeLen) != 0 ||
      mbedtls_md_hmac_finish(&ctx, outResponse20) != 0) {
    mbedtls_md_free(&ctx);
    return false;
  }

  mbedtls_md_free(&ctx);
  return true;
}
