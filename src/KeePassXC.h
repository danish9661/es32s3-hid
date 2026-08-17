#pragma once
#include <Arduino.h>
#include <vector>

class KeePassXC {
public:
  static void init();
  static bool isConfigured();
  static bool setSecret(const uint8_t *secret20);
  static bool setSecretHex(const String &hexStr);
  static bool getSecret(uint8_t *outSecret20);
  static String getSecretHex();
  static String getSecretBase32();
  static void generateRandomSecret(uint8_t *outSecret20);
  static void clearSecret();

  // Compute 20-byte HMAC-SHA1 response from 0-64 byte challenge
  static bool computeResponse(const uint8_t *challenge, size_t challengeLen, uint8_t *outResponse20);
};
