#pragma once
#include <Arduino.h>
#include <vector>

class Bip39 {
public:
  static String generateMnemonic24();
  static bool validateMnemonic(const String &mnemonic);
  static bool mnemonicToSeed(const String &mnemonic, const String &passphrase, uint8_t *seed64);
  static const char* getWord(uint16_t index);
  static int findWordIndex(const String &word);
};
