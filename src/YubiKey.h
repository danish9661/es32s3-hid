#pragma once
#include <Arduino.h>
#include <vector>
#include <ArduinoJson.h>

enum YubiKeySlotMode {
  YUBI_SLOT_DISABLED = 0,
  YUBI_SLOT_OTP = 1,
  YUBI_SLOT_STATIC = 2,
  YUBI_SLOT_CHALLENGE_RESPONSE = 3
};

struct YubiKeySlotConfig {
  YubiKeySlotMode mode;
  String publicId;       // 12-char Modhex string (e.g. "cccccccccccb")
  String secretKeyHex;   // 32-char Hex string (16 bytes AES-128 key)
  String privateIdHex;   // 12-char Hex string (6 bytes Private ID)
  String staticPassword; // Custom string for static password injection
  bool sendEnter;        // Append Enter key after typing
};

struct YubiOathAccount {
  String name;           // Account identifier (e.g. "Google:user@gmail.com")
  String issuer;         // Service issuer (e.g. "Google", "GitHub")
  String secretBase32;   // Base32 secret key
  uint8_t digits;        // 6 or 8 digits
  uint32_t period;       // 30 or 60 seconds
  String algorithm;      // "SHA1" or "SHA256"
};

class YubiKeyEngine {
private:
  bool yubikeyModeEnabled;
  uint32_t serialNumber;
  uint16_t useCounter;
  uint8_t sessionCounter;

  YubiKeySlotConfig slot1;
  YubiKeySlotConfig slot2;
  std::vector<YubiOathAccount> oathAccounts;

  void generateDefaultSlotConfig();
  uint16_t calculateCrc16(const uint8_t *buffer, size_t len);
  std::vector<uint8_t> base32Decode(const String &b32);

public:
  YubiKeyEngine();

  void init();
  void loadConfig();
  void saveConfig();

  // Mode Selection
  bool isYubiKeyMode() const { return yubikeyModeEnabled; }
  void setYubiKeyMode(bool enabled);

  // Device Info
  uint32_t getSerialNumber() const { return serialNumber; }
  String getVersionString() const { return "5.4.3"; }

  // Slot Management
  YubiKeySlotConfig getSlotConfig(int slot) const;
  bool setSlotConfig(int slot, const YubiKeySlotConfig &config);
  void configureDefaultOtpSlot(int slot);

  // OTP & Static Generator
  String generateOtpString(int slot);
  String getStaticPasswordString(int slot);

  // Modhex Helpers
  static String hexToModhex(const String &hexStr);
  static String modhexToHex(const String &modhexStr);
  static bool isValidModhex(const String &modhexStr);

  // OATH-TOTP Accounts
  String getOathAccountsJson(uint32_t currentEpoch = 0);
  bool addOathAccount(const String &name, const String &secretBase32, const String &issuer = "", uint8_t digits = 6, uint32_t period = 30, const String &algo = "SHA1");
  bool deleteOathAccount(const String &name);
  String calculateTotp(const YubiOathAccount &acc, uint32_t currentEpoch);
  void clearAllOathAccounts();

  // YubiKey APDU & CCID SmartCard Protocol Engine
  std::vector<uint8_t> processApdu(const uint8_t *apdu, size_t len);
  std::vector<uint8_t> processCcidMessage(const uint8_t *msg, size_t len);

  // JSON Status
  String getStatusJson();
};

extern YubiKeyEngine YubiKey;
