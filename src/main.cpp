#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <Adafruit_NeoPixel.h>
#include <Update.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include "USB.h"
#include "USBMSC.h"
#include "USBHIDKeyboard.h"
#include "USBHIDMouse.h"
#include "USBHIDConsumerControl.h"
#include "USBHIDFIDO.h"
#include "BLEFIDO.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "esp_random.h"
#include "mbedtls/gcm.h"
#include "mbedtls/pkcs5.h"
#include "mbedtls/sha256.h"
#include "mbedtls/md.h"
#include "mbedtls/base64.h"
#include <esp_ota_ops.h>
#include <esp_app_format.h>
#include <cstring>
#include <time.h>
#include <sys/time.h>

// --- KEY DEFINITIONS ---
#ifndef KEY_PRINT_SCREEN
#define KEY_PRINT_SCREEN 206
#endif
#ifndef KEY_SCROLL_LOCK
#define KEY_SCROLL_LOCK 207
#endif
#ifndef KEY_PAUSE
#define KEY_PAUSE 208
#endif
#ifndef KEY_INSERT
#define KEY_INSERT 209
#endif
#ifndef KEY_HOME
#define KEY_HOME 210
#endif
#ifndef KEY_PAGE_UP
#define KEY_PAGE_UP 211
#endif
#ifndef KEY_DELETE
#define KEY_DELETE 212
#endif
#ifndef KEY_END
#define KEY_END 213
#endif
#ifndef KEY_PAGE_DOWN
#define KEY_PAGE_DOWN 214
#endif
#ifndef KEY_NUM_LOCK
#define KEY_NUM_LOCK 219
#endif
#ifndef KEY_CAPS_LOCK
#define KEY_CAPS_LOCK 193
#endif
#ifndef KEY_APPLICATION
#define KEY_APPLICATION 101
#endif
#ifndef KEY_F1
#define KEY_F1 194
#endif
#ifndef KEY_F2
#define KEY_F2 195
#endif
#ifndef KEY_F3
#define KEY_F3 196
#endif
#ifndef KEY_F4
#define KEY_F4 197
#endif
#ifndef KEY_F5
#define KEY_F5 198
#endif
#ifndef KEY_F6
#define KEY_F6 199
#endif
#ifndef KEY_F7
#define KEY_F7 200
#endif
#ifndef KEY_F8
#define KEY_F8 201
#endif
#ifndef KEY_F9
#define KEY_F9 202
#endif
#ifndef KEY_F10
#define KEY_F10 203
#endif
#ifndef KEY_F11
#define KEY_F11 204
#endif
#ifndef KEY_F12
#define KEY_F12 205
#endif

#define FIRMWARE_VERSION "2.5.0"
#define BUILD_DATE       __DATE__ " " __TIME__

static constexpr size_t MAX_LOG_LINES = 120;
static String sysLogs[MAX_LOG_LINES];
static size_t sysLogHead = 0;
static size_t sysLogCount = 0;

void updateMscLogFile();

void logSystem(const String &msg) {
  unsigned long now = millis();
  char timeBuf[24];
  snprintf(timeBuf, sizeof(timeBuf), "[%02lu:%02lu.%03lu] ", (now / 60000) % 60, (now / 1000) % 60, now % 1000);
  String line = String(timeBuf) + msg;
  Serial.println(line);
  
  sysLogs[sysLogHead] = line;
  sysLogHead = (sysLogHead + 1) % MAX_LOG_LINES;
  if (sysLogCount < MAX_LOG_LINES) sysLogCount++;

  // Append to LittleFS file /system.log
  File f = LittleFS.open("/system.log", "a");
  if (f) {
    if (f.size() > 100 * 1024) { // Auto-rotate if > 100 KB
      f.close();
      LittleFS.remove("/system.log.old");
      LittleFS.rename("/system.log", "/system.log.old");
      f = LittleFS.open("/system.log", "w");
    }
    if (f) {
      f.println(line);
      f.close();
    }
  }

  updateMscLogFile();
}

#ifndef STATUS_LED_PIN
#define STATUS_LED_PIN 38
#endif

constexpr uint8_t NUMPIXELS = 1;
constexpr size_t BUFFER_SIZE = 1024 * 1024 * 2; // 2 MB in PSRAM
constexpr uint32_t SESSION_TTL_MS = 15UL * 60UL * 1000UL;
constexpr uint32_t LOGIN_BLOCK_MS = 5UL * 60UL * 1000UL;
constexpr uint32_t LOGIN_RESET_MS = 10UL * 60UL * 1000UL;
constexpr uint8_t LOGIN_MAX_FAILURES = 5;
constexpr uint8_t LOGIN_SLOT_COUNT = 12;
constexpr const char *SETTINGS_FILE = "/settings.json";
constexpr const char *SCRIPTS_DIR = "/scripts";
constexpr const char *ACTIONS_DIR = "/actions";
constexpr size_t ACTION_FILE_MAX_SIZE = 512UL * 1024UL;

constexpr uint16_t KVM_DEFAULT_PORT = 4210;
constexpr uint16_t KVM_PACKET_MAGIC = 0xCAFE;
constexpr size_t KVM_PACKET_SIZE = 16;

enum : uint8_t {
  KVM_EVENT_MOUSE = 0x01,
  KVM_EVENT_KEYBOARD = 0x02,
  KVM_EVENT_CONSUMER = 0x03,
};

struct __attribute__((packed)) KvmMousePayload {
  uint8_t buttons;
  int16_t dx;
  int16_t dy;
  int8_t wheel;
  int8_t pan;
  uint8_t pad;
};

struct __attribute__((packed)) KvmKeyboardPayload {
  uint8_t modifiers;
  uint8_t reserved;
  uint8_t keycodes[6];
};

struct __attribute__((packed)) KvmConsumerPayload {
  uint16_t usageId;
  uint8_t pad[6];
};

struct __attribute__((packed)) KvmPacket {
  uint16_t magic;
  uint32_t sequence;
  uint8_t type;
  uint8_t reserved;

  union {
    KvmMousePayload mouse;
    KvmKeyboardPayload keyboard;
    KvmConsumerPayload consumer;
  } payload;
};

static_assert(sizeof(KvmPacket) == KVM_PACKET_SIZE, "KVM packet must be exactly 16 bytes");

// --- USER SETTINGS (with defaults) ---
String ap_ssid = "ESP32-Ducky-Pro";
String ap_pass = "password123";
String sta_ssid = "";
String sta_pass = "";
String admin_user = "admin";
String admin_pass = "admin123";

bool loginRateLimitEnabled = true;
bool proxyAuthEnabled = false;
String proxyAuthToken = "";

bool kvmEnabled = false;
uint16_t kvmPort = KVM_DEFAULT_PORT;
String kvmAllowedIp = "";

uint16_t usbVendorId = 0x303A;
uint16_t usbProductId = 0x0002;
String usbVendorName = "Espressif";
String usbProductName = "ESP32-S3 HID Console";

int typeDelay = 6;
int burstChars = 24;
int burstPauseMs = 10;
int lineDelayMs = 40;
int ledBrightness = 50;
int kvmMouseSmoothness = 100;

bool usbMscEnabled = true;
String usbMscVolumeLabel = "DUCKY_DRIVE";
bool fidoSecurityKeyMode = false;
bool bleFidoEnabled = false;

constexpr size_t MSC_SECTOR_SIZE = 512;
constexpr size_t MSC_SECTOR_COUNT = 4096; // 2 MB
constexpr size_t MSC_DISK_SIZE = MSC_SECTOR_COUNT * MSC_SECTOR_SIZE;

// --- RUNTIME OBJECTS ---
// IMPORTANT: FIDO must be declared FIRST so its HID descriptor registers before
// Keyboard/Mouse/Consumer. Chrome's parse_report_descriptor() reads only the FIRST
// USAGE_PAGE from the combined HID descriptor; if Keyboard (0x01) comes first,
// Chrome rejects the device as "Not a FIDO device".
USBHIDFIDO FIDO;
USBHIDKeyboard* Keyboard = nullptr;
USBHIDMouse* Mouse = nullptr;
USBHIDConsumerControl* Consumer = nullptr;
USBMSC MSC;
AsyncWebServer server(80);
Adafruit_NeoPixel pixels(NUMPIXELS, 48, NEO_RGB + NEO_KHZ800);
Adafruit_NeoPixel pixels_alt(NUMPIXELS, 38, NEO_RGB + NEO_KHZ800);
WiFiUDP kvmUdp;

char *psramBuffer = nullptr;
size_t bufferIndex = 0;
uint8_t *mscDiskBuffer = nullptr;

volatile bool isWorkerBusy = false;
volatile bool stopScriptFlag = false;
volatile bool isJobQueued = false;
volatile bool isInputLocked = false;
volatile uint32_t inputLockTimestamp = 0;

volatile size_t scriptCurrentLine = 0;
volatile size_t scriptTotalLines = 0;
volatile uint8_t scriptProgressPercent = 0;
String scriptCurrentCommand = "";

String activeSessionToken = "";
IPAddress activeSessionIp;
uint32_t activeSessionLastSeen = 0;

struct LoginAttemptSlot {
  bool used = false;
  IPAddress ip = IPAddress(0, 0, 0, 0);
  uint8_t failures = 0;
  uint32_t blockedUntil = 0;
  uint32_t lastTouched = 0;
};
LoginAttemptSlot loginSlots[LOGIN_SLOT_COUNT];

struct DuckyJob {
  uint8_t type;
  size_t length;
  char fileName[68];
};

QueueHandle_t jobQueue = nullptr;

enum : uint8_t {
  JOB_SCRIPT = 0,
  JOB_RAW_TEXT = 1,
  JOB_ACTION_FILE = 2,
};

uint8_t parseMouseButton(const String &name);

enum class HidRealtimeType : uint8_t {
  KeyTap,
  KeyDown,
  KeyUp,
  KeyReleaseAll,
  Combo,
  MouseMove,
  MouseButton,
  MouseScroll,
  KvmKeyboardState,
  KvmMouseState,
  ConsumerControl,
};

enum : uint8_t {
  MOUSE_ACTION_CLICK = 0,
  MOUSE_ACTION_DOWN = 1,
  MOUSE_ACTION_UP = 2,
};

struct HidRealtimeEvent {
  HidRealtimeType type;
  uint8_t keyCode;
  bool ctrl;
  bool alt;
  bool shift;
  bool gui;
  int8_t dx;
  int8_t dy;
  int8_t wheel;
  int8_t pan;
  uint8_t mouseButton;
  uint8_t mouseAction;
  uint16_t holdMs;

  uint8_t kvmModifiers;
  uint8_t kvmKeys[6];
  uint8_t kvmButtons;
  int16_t kvmDx;
  int16_t kvmDy;
  int8_t kvmWheel;
  int8_t kvmPan;

  uint16_t consumerUsage;
};

QueueHandle_t hidEventQueue = nullptr;

bool kvmUdpBound = false;
uint16_t kvmBoundPort = 0;
IPAddress kvmLastSourceIp = IPAddress(0, 0, 0, 0);
uint32_t kvmLastSequence = 0;
uint32_t kvmPacketsRx = 0;
uint32_t kvmPacketsDropped = 0;
uint32_t kvmPacketsEnqueued = 0;
uint32_t kvmLastPacketMs = 0;
bool kvmHasSequence = false;
String kvmBindError = "";
SemaphoreHandle_t kvmUdpMutex = nullptr;

constexpr size_t KVM_BRIDGE_RECORD_MAX_EVENTS = 3000;

struct KvmBridgeRecordEvent {
  uint32_t dtMs;
  uint8_t type;
  uint8_t buttons;
  int16_t dx;
  int16_t dy;
  int8_t wheel;
  int8_t pan;
  uint8_t modifiers;
  uint8_t keys[6];
  uint16_t usageId;
};

KvmBridgeRecordEvent kvmBridgeRecordEvents[KVM_BRIDGE_RECORD_MAX_EVENTS];
size_t kvmBridgeRecordCount = 0;
uint32_t kvmBridgeRecordDropped = 0;
uint32_t kvmBridgeRecordStartMs = 0;
bool kvmBridgeRecordEnabled = false;
SemaphoreHandle_t kvmBridgeRecordMutex = nullptr;

// --- HELPERS ---
int clampInt(int value, int minimum, int maximum) {
  if (value < minimum) return minimum;
  if (value > maximum) return maximum;
  return value;
}

int8_t clampInt8(int value, int minimum, int maximum) {
  return static_cast<int8_t>(clampInt(value, minimum, maximum));
}

int16_t scaleMouseDelta(int16_t value, int percent) {
  long scaled = static_cast<long>(value) * static_cast<long>(percent);
  if (scaled >= 0) {
    scaled = (scaled + 50) / 100;
  } else {
    scaled = (scaled - 50) / 100;
  }

  if (scaled > 32767) scaled = 32767;
  if (scaled < -32768) scaled = -32768;
  return static_cast<int16_t>(scaled);
}

// --- USB MSC DISK HANDLERS ---
static int32_t onMscRead(uint32_t lba, uint32_t offset, void* buffer, uint32_t bufsize) {
  if (!mscDiskBuffer || lba >= MSC_SECTOR_COUNT) return -1;
  uint32_t srcOffset = (lba * MSC_SECTOR_SIZE) + offset;
  if (srcOffset + bufsize > MSC_DISK_SIZE) return -1;
  memcpy(buffer, mscDiskBuffer + srcOffset, bufsize);
  return bufsize;
}

static int32_t onMscWrite(uint32_t lba, uint32_t offset, uint8_t* buffer, uint32_t bufsize) {
  if (!mscDiskBuffer || lba >= MSC_SECTOR_COUNT) return -1;
  uint32_t dstOffset = (lba * MSC_SECTOR_SIZE) + offset;
  if (dstOffset + bufsize > MSC_DISK_SIZE) return -1;
  memcpy(mscDiskBuffer + dstOffset, buffer, bufsize);
  return bufsize;
}

static bool onMscStartStop(uint8_t power_condition, bool start, bool load_eject) {
  return true;
}

void initVirtualFatDisk() {
  if (!mscDiskBuffer) return;
  memset(mscDiskBuffer, 0, MSC_DISK_SIZE);

  uint8_t *bs = mscDiskBuffer;
  bs[0] = 0xEB; bs[1] = 0x3C; bs[2] = 0x90;
  memcpy(bs + 3, "MSWIN4.1", 8);
  bs[11] = 0x00; bs[12] = 0x02; // 512 bytes/sector
  bs[13] = 0x01;               // 1 sector/cluster
  bs[14] = 0x01; bs[15] = 0x00; // 1 reserved sector
  bs[16] = 0x02;               // 2 FATs
  bs[17] = 0x80; bs[18] = 0x00; // 128 root entries (8 sectors)
  bs[19] = 0x00; bs[20] = 0x10; // 4096 sectors (2 MB)
  bs[21] = 0xF8;               // Media descriptor
  bs[22] = 0x0C; bs[23] = 0x00; // 12 sectors/FAT
  bs[24] = 0x20; bs[25] = 0x00; // 32 sectors/track
  bs[26] = 0x40; bs[27] = 0x00; // 64 heads
  bs[28] = 0x00; bs[29] = 0x00; bs[30] = 0x00; bs[31] = 0x00;
  bs[36] = 0x80;
  bs[38] = 0x29;
  bs[39] = 0x75; bs[40] = 0x10; bs[41] = 0x96; bs[42] = 0x61;
  memcpy(bs + 43, "DUCKY_DRIVE", 11);
  memcpy(bs + 54, "FAT12   ", 8);
  bs[510] = 0x55; bs[511] = 0xAA;

  uint8_t *fat1 = mscDiskBuffer + (1 * 512);
  uint8_t *fat2 = mscDiskBuffer + (13 * 512);
  fat1[0] = 0xF8; fat1[1] = 0xFF; fat1[2] = 0xFF;
  fat2[0] = 0xF8; fat2[1] = 0xFF; fat2[2] = 0xFF;

  uint8_t *rootDir = mscDiskBuffer + (25 * 512);
  memcpy(rootDir + 0, "DUCKY_DRIVE", 11);
  rootDir[11] = 0x08;

  auto addFat12File = [&](int dirIndex, const char *name83, const char *content, uint16_t cluster) {
    uint8_t *entry = rootDir + (dirIndex * 32);
    memcpy(entry, name83, 11);
    entry[11] = 0x20;
    entry[22] = 0x00; entry[23] = 0x50;
    entry[24] = 0x2F; entry[25] = 0x5D;
    entry[26] = static_cast<uint8_t>(cluster & 0xFF);
    entry[27] = static_cast<uint8_t>((cluster >> 8) & 0xFF);
    uint32_t len = strlen(content);
    memcpy(entry + 28, &len, 4);

    int fatOffset = (cluster * 3) / 2;
    if (cluster % 2 == 0) {
      fat1[fatOffset] = 0xFF;
      fat1[fatOffset + 1] = (fat1[fatOffset + 1] & 0xF0) | 0x0F;
      fat2[fatOffset] = 0xFF;
      fat2[fatOffset + 1] = (fat2[fatOffset + 1] & 0xF0) | 0x0F;
    } else {
      fat1[fatOffset] = (fat1[fatOffset] & 0x0F) | 0xF0;
      fat1[fatOffset + 1] = 0xFF;
      fat2[fatOffset] = (fat2[fatOffset] & 0x0F) | 0xF0;
      fat2[fatOffset + 1] = 0xFF;
    }

    uint32_t dataSector = 33 + (cluster - 2);
    memcpy(mscDiskBuffer + (dataSector * 512), content, len);
  };

  const char *runBat = "@echo off\r\necho Executing Staged Payload...\r\npowershell -ExecutionPolicy Bypass -File payload.ps1\r\npause\r\n";
  const char *payloadPs1 = "# ESP32-S3 HID Staged Payload\r\nWrite-Host \"[+] Running Staged Payload from ESP32-S3 MSC Drive\" -ForegroundColor Green\r\nGet-Date | Out-File -Append exfil.txt\r\n";
  const char *readmeTxt = "ESP32-S3 HID Console - Virtual Mass Storage Drive\r\nUse this drive to store staged payloads or collect exfiltrated data.\r\n";
  const char *logInit = "=== ESP32-S3 System Log ===\r\n";

  addFat12File(1, "RUN     BAT", runBat, 2);
  addFat12File(2, "PAYLOAD PS1", payloadPs1, 3);
  addFat12File(3, "README  TXT", readmeTxt, 4);
  addFat12File(4, "SYSTEM  LOG", logInit, 5);
}

void updateMscLogFile() {
  if (!mscDiskBuffer) return;
  uint8_t *rootDir = mscDiskBuffer + (25 * 512);
  uint8_t *entry = rootDir + (4 * 32); // 4th file directory entry
  
  String logsDump = "";
  size_t start = (sysLogCount < MAX_LOG_LINES) ? 0 : sysLogHead;
  for (size_t i = 0; i < sysLogCount; i++) {
    size_t idx = (start + i) % MAX_LOG_LINES;
    logsDump += sysLogs[idx] + "\r\n";
  }
  
  uint32_t len = logsDump.length();
  if (len > 512 * 8) len = 512 * 8; // Cap at 4 KB on FAT12 cluster 5
  memcpy(entry + 28, &len, 4); // update length in directory
  
  uint32_t dataSector = 33 + (5 - 2); // cluster 5 sector
  memcpy(mscDiskBuffer + (dataSector * 512), logsDump.c_str(), len);
}

void mouseMoveAbsolute(float xPct, float yPct, uint8_t clickButton = 0, uint8_t clickAction = 0) {
  xPct = clampInt(static_cast<int>(xPct * 10.0f), 0, 1000) / 10.0f;
  yPct = clampInt(static_cast<int>(yPct * 10.0f), 0, 1000) / 10.0f;

  for (int i = 0; i < 20; i++) {
    if(Mouse) Mouse->move(-127, -127, 0, 0);
    delay(1);
  }
  delay(10);

  int targetX = static_cast<int>((xPct / 100.0f) * 1920.0f);
  int targetY = static_cast<int>((yPct / 100.0f) * 1080.0f);

  while (targetX > 0 || targetY > 0) {
    int8_t stepX = static_cast<int8_t>(clampInt(targetX, 0, 120));
    int8_t stepY = static_cast<int8_t>(clampInt(targetY, 0, 120));
    if(Mouse) Mouse->move(stepX, stepY, 0, 0);
    targetX -= stepX;
    targetY -= stepY;
    if (targetX > 0 || targetY > 0) delay(1);
  }

  if (clickButton != 0) {
    delay(15);
    if (clickAction == MOUSE_ACTION_DOWN) {
      if(Mouse) Mouse->press(clickButton);
    } else if (clickAction == MOUSE_ACTION_UP) {
      if(Mouse) Mouse->release(clickButton);
    } else {
      if(Mouse) Mouse->click(clickButton);
    }
  }
}

// --- ENCRYPTED VAULT & 2FA HARDWARE AUTHENTICATOR SUBSYSTEM ---
static constexpr const char *VAULT_FILE = "/vault.enc";
static constexpr size_t VAULT_SALT_LEN = 16;
static constexpr size_t VAULT_IV_LEN = 12;
static constexpr size_t VAULT_TAG_LEN = 16;
static constexpr size_t VAULT_KEY_LEN = 32;
static constexpr uint32_t VAULT_PBKDF2_ITERATIONS = 10000;
static constexpr uint32_t VAULT_AUTO_LOCK_MS = 300000;

struct __attribute__((packed)) VaultHeader {
  char magic[4];
  uint8_t salt[VAULT_SALT_LEN];
  uint8_t iv[VAULT_IV_LEN];
  uint8_t tag[VAULT_TAG_LEN];
  uint32_t ciphertextLen;
};

static bool vaultUnlocked = false;
static uint8_t vaultKey[VAULT_KEY_LEN] = {0};
static uint8_t vaultSalt[VAULT_SALT_LEN] = {0};
static uint32_t vaultLastActiveMs = 0;
static String vaultCachedJson = "[]";

void lockVault() {
  vaultUnlocked = false;
  memset(vaultKey, 0, sizeof(vaultKey));
  vaultCachedJson = "[]";
  vaultLastActiveMs = 0;
}

void touchVaultActivity() {
  if (vaultUnlocked) {
    vaultLastActiveMs = millis();
  }
}

void checkVaultAutoLock() {
  if (vaultUnlocked && (millis() - vaultLastActiveMs > VAULT_AUTO_LOCK_MS)) {
    lockVault();
  }
}

bool pbkdf2DeriveKey(const String &password, const uint8_t *salt, size_t saltLen, uint8_t *keyOut) {
  mbedtls_md_context_t ctx;
  mbedtls_md_init(&ctx);
  const mbedtls_md_info_t *info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  if (!info) return false;

  int ret = mbedtls_md_setup(&ctx, info, 1);
  if (ret != 0) {
    mbedtls_md_free(&ctx);
    return false;
  }

  ret = mbedtls_pkcs5_pbkdf2_hmac(
    &ctx,
    reinterpret_cast<const unsigned char *>(password.c_str()),
    password.length(),
    salt,
    saltLen,
    VAULT_PBKDF2_ITERATIONS,
    VAULT_KEY_LEN,
    keyOut
  );
  mbedtls_md_free(&ctx);
  return (ret == 0);
}

bool aes256GcmEncrypt(
  const uint8_t *key,
  const uint8_t *iv,
  size_t ivLen,
  const uint8_t *plaintext,
  size_t plainLen,
  uint8_t *ciphertext,
  uint8_t *tag,
  size_t tagLen
) {
  mbedtls_gcm_context gcm;
  mbedtls_gcm_init(&gcm);
  int ret = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, key, 256);
  if (ret != 0) {
    mbedtls_gcm_free(&gcm);
    return false;
  }

  ret = mbedtls_gcm_crypt_and_tag(
    &gcm,
    MBEDTLS_GCM_ENCRYPT,
    plainLen,
    iv,
    ivLen,
    nullptr,
    0,
    plaintext,
    ciphertext,
    tagLen,
    tag
  );
  mbedtls_gcm_free(&gcm);
  return (ret == 0);
}

bool aes256GcmDecrypt(
  const uint8_t *key,
  const uint8_t *iv,
  size_t ivLen,
  const uint8_t *tag,
  size_t tagLen,
  const uint8_t *ciphertext,
  size_t cipherLen,
  uint8_t *plaintext
) {
  mbedtls_gcm_context gcm;
  mbedtls_gcm_init(&gcm);
  int ret = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, key, 256);
  if (ret != 0) {
    mbedtls_gcm_free(&gcm);
    return false;
  }

  ret = mbedtls_gcm_auth_decrypt(
    &gcm,
    cipherLen,
    iv,
    ivLen,
    nullptr,
    0,
    tag,
    tagLen,
    ciphertext,
    plaintext
  );
  mbedtls_gcm_free(&gcm);
  return (ret == 0);
}

void saveVaultToNvs(const uint8_t *fullData, size_t dataLen) {
  Preferences prefs;
  if (prefs.begin("vault_nvs", false)) {
    prefs.putBytes("vdata", fullData, dataLen);
    prefs.putBool("has_vault", true);
    prefs.end();
  }
}

bool restoreVaultFromNvs() {
  Preferences prefs;
  if (!prefs.begin("vault_nvs", true)) return false;
  if (!prefs.getBool("has_vault", false)) {
    prefs.end();
    return false;
  }
  size_t len = prefs.getBytesLength("vdata");
  if (len < sizeof(VaultHeader)) {
    prefs.end();
    return false;
  }
  uint8_t *buf = static_cast<uint8_t *>(malloc(len));
  if (!buf) {
    prefs.end();
    return false;
  }
  prefs.getBytes("vdata", buf, len);
  prefs.end();

  File f = LittleFS.open(VAULT_FILE, "w");
  if (!f) {
    Serial.println("[VAULT] restoreVaultFromNvs: ERROR: Cannot open vault.enc for writing!");
    free(buf);
    return false;
  }
  size_t written = f.write(buf, len);
  f.close();
  free(buf);
  if (written != len) {
    Serial.printf("[VAULT] restoreVaultFromNvs: Write incomplete %u/%u\n", (unsigned)written, (unsigned)len);
    LittleFS.remove(VAULT_FILE);
    return false;
  }
  Serial.printf("[VAULT] restoreVaultFromNvs: Restored vault.enc (%u bytes) from NVS\n", (unsigned)len);
  return true;
}

bool isVaultInitialized() {
  if (LittleFS.exists(VAULT_FILE)) return true;
  return restoreVaultFromNvs();
}

bool saveVaultEncrypted(const String &jsonPlaintext, const uint8_t *key) {
  size_t plainLen = jsonPlaintext.length();
  uint8_t iv[VAULT_IV_LEN];
  uint8_t tag[VAULT_TAG_LEN];
  esp_fill_random(iv, sizeof(iv));

  uint8_t *cipherBuf = static_cast<uint8_t *>(malloc(plainLen + 1));
  if (!cipherBuf) return false;

  bool ok = aes256GcmEncrypt(
    key,
    iv,
    sizeof(iv),
    reinterpret_cast<const uint8_t *>(jsonPlaintext.c_str()),
    plainLen,
    cipherBuf,
    tag,
    sizeof(tag)
  );

  if (!ok) {
    free(cipherBuf);
    return false;
  }

  VaultHeader hdr;
  memcpy(hdr.magic, "VLT1", 4);
  memcpy(hdr.salt, vaultSalt, VAULT_SALT_LEN);
  memcpy(hdr.iv, iv, VAULT_IV_LEN);
  memcpy(hdr.tag, tag, VAULT_TAG_LEN);
  hdr.ciphertextLen = static_cast<uint32_t>(plainLen);

  // Write to LittleFS
  File f = LittleFS.open(VAULT_FILE, "w");
  if (!f) {
    Serial.println("[VAULT] ERROR: Cannot open vault.enc for writing! LittleFS may be full or read-only.");
    free(cipherBuf);
    return false;
  }
  size_t hdrWritten = f.write(reinterpret_cast<const uint8_t *>(&hdr), sizeof(hdr));
  size_t dataWritten = (plainLen > 0) ? f.write(cipherBuf, plainLen) : 0;
  f.close();

  if (hdrWritten != sizeof(hdr) || (plainLen > 0 && dataWritten != plainLen)) {
    Serial.printf("[VAULT] ERROR: Write incomplete: hdr=%u/%u data=%u/%u\n",
      (unsigned)hdrWritten, (unsigned)sizeof(hdr), (unsigned)dataWritten, (unsigned)plainLen);
    LittleFS.remove(VAULT_FILE);
    free(cipherBuf);
    return false;
  }
  Serial.printf("[VAULT] vault.enc written: %u bytes\n", (unsigned)(sizeof(hdr) + plainLen));

  // Mirror to NVS so LittleFS flashing doesn't erase it
  size_t totalDataLen = sizeof(VaultHeader) + plainLen;
  uint8_t *nvsBlob = static_cast<uint8_t *>(malloc(totalDataLen));
  if (nvsBlob) {
    memcpy(nvsBlob, &hdr, sizeof(VaultHeader));
    if (plainLen > 0) {
      memcpy(nvsBlob + sizeof(VaultHeader), cipherBuf, plainLen);
    }
    saveVaultToNvs(nvsBlob, totalDataLen);
    free(nvsBlob);
  }

  free(cipherBuf);
  return true;
}

bool unlockVaultWithPassword(const String &password, uint64_t clientEpoch = 0) {
  if (password.isEmpty()) return false;
  if (!isVaultInitialized()) return false;

  File f = LittleFS.open(VAULT_FILE, "r");
  if (!f) return false;

  if (f.size() < sizeof(VaultHeader)) {
    f.close();
    return false;
  }

  VaultHeader hdr;
  if (f.read(reinterpret_cast<uint8_t *>(&hdr), sizeof(hdr)) != sizeof(hdr)) {
    f.close();
    return false;
  }

  if (memcmp(hdr.magic, "VLT1", 4) != 0) {
    f.close();
    return false;
  }

  uint32_t cipherLen = hdr.ciphertextLen;
  if (f.size() < sizeof(VaultHeader) + cipherLen) {
    f.close();
    return false;
  }

  uint8_t *cipherBuf = static_cast<uint8_t *>(malloc(cipherLen + 1));
  if (!cipherBuf) {
    f.close();
    return false;
  }

  if (cipherLen > 0 && f.read(cipherBuf, cipherLen) != cipherLen) {
    free(cipherBuf);
    f.close();
    return false;
  }
  f.close();

  memcpy(vaultSalt, hdr.salt, VAULT_SALT_LEN);
  Serial.printf("[VAULT] Unlock: file salt[0]=%02x salt[1]=%02x salt[2]=%02x cipherLen=%u\n", vaultSalt[0], vaultSalt[1], vaultSalt[2], (unsigned)cipherLen);

  uint8_t derivedKey[VAULT_KEY_LEN];
  if (!pbkdf2DeriveKey(password, vaultSalt, VAULT_SALT_LEN, derivedKey)) {
    free(cipherBuf);
    return false;
  }
  Serial.printf("[VAULT] Unlock: derived key[0]=%02x key[1]=%02x\n", derivedKey[0], derivedKey[1]);

  uint8_t *plainBuf = static_cast<uint8_t *>(malloc(cipherLen + 1));
  if (!plainBuf) {
    free(cipherBuf);
    return false;
  }

  bool ok = aes256GcmDecrypt(
    derivedKey,
    hdr.iv,
    VAULT_IV_LEN,
    hdr.tag,
    VAULT_TAG_LEN,
    cipherBuf,
    cipherLen,
    plainBuf
  );
  free(cipherBuf);

  if (!ok) {
    Serial.println("[VAULT] Unlock: AES-GCM auth decrypt FAILED (wrong password or corrupted data)");
    free(plainBuf);
    return false;
  }
  Serial.println("[VAULT] Unlock: SUCCESS");

  plainBuf[cipherLen] = '\0';
  vaultCachedJson = reinterpret_cast<char *>(plainBuf);
  free(plainBuf);

  memcpy(vaultKey, derivedKey, VAULT_KEY_LEN);
  vaultUnlocked = true;
  vaultLastActiveMs = millis();

  if (clientEpoch > 1700000000ULL) {
    struct timeval tv;
    tv.tv_sec = static_cast<time_t>(clientEpoch);
    tv.tv_usec = 0;
    settimeofday(&tv, nullptr);
  }

  return true;
}

size_t decodeBase32(const String &b32, uint8_t *out, size_t maxOutLen) {
  String clean = b32;
  clean.toUpperCase();
  clean.trim();
  size_t outIdx = 0;
  uint32_t buffer = 0;
  int bitsLeft = 0;

  for (size_t i = 0; i < clean.length(); i++) {
    char c = clean[i];
    if (c == ' ' || c == '-' || c == '=') continue;

    int val = -1;
    if (c >= 'A' && c <= 'Z') val = c - 'A';
    else if (c >= '2' && c <= '7') val = c - '2' + 26;
    if (val < 0) continue;

    buffer = (buffer << 5) | (val & 0x1F);
    bitsLeft += 5;
    if (bitsLeft >= 8) {
      bitsLeft -= 8;
      if (outIdx < maxOutLen) {
        out[outIdx++] = static_cast<uint8_t>((buffer >> bitsLeft) & 0xFF);
      }
    }
  }
  return outIdx;
}

String calculateTotp(const String &secretBase32, uint64_t epochSeconds, int period = 30, int digits = 6) {
  if (period <= 0) period = 30;
  if (digits < 6 || digits > 8) digits = 6;

  uint8_t keyBytes[64];
  size_t keyLen = decodeBase32(secretBase32, keyBytes, sizeof(keyBytes));
  if (keyLen == 0) return "000000";

  uint64_t step = epochSeconds / period;
  uint8_t msg[8];
  for (int i = 7; i >= 0; i--) {
    msg[i] = static_cast<uint8_t>(step & 0xFF);
    step >>= 8;
  }

  uint8_t hmacRes[20];
  mbedtls_md_context_t md_ctx;
  mbedtls_md_init(&md_ctx);
  const mbedtls_md_info_t *md_info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA1);
  if (!md_info) {
    mbedtls_md_free(&md_ctx);
    return "000000";
  }
  if (mbedtls_md_setup(&md_ctx, md_info, 1) != 0) {
    mbedtls_md_free(&md_ctx);
    return "000000";
  }
  mbedtls_md_hmac_starts(&md_ctx, keyBytes, keyLen);
  mbedtls_md_hmac_update(&md_ctx, msg, sizeof(msg));
  mbedtls_md_hmac_finish(&md_ctx, hmacRes);
  mbedtls_md_free(&md_ctx);

  int offset = hmacRes[19] & 0x0F;
  uint32_t truncated = (
    ((hmacRes[offset] & 0x7F) << 24) |
    ((hmacRes[offset + 1] & 0xFF) << 16) |
    ((hmacRes[offset + 2] & 0xFF) << 8) |
    (hmacRes[offset + 3] & 0xFF)
  );

  uint32_t mod = 1;
  for (int d = 0; d < digits; d++) mod *= 10;
  uint32_t code = truncated % mod;

  char buf[12];
  if (digits == 8) {
    snprintf(buf, sizeof(buf), "%08u", code);
  } else if (digits == 7) {
    snprintf(buf, sizeof(buf), "%07u", code);
  } else {
    snprintf(buf, sizeof(buf), "%06u", code);
  }
  return String(buf);
}

bool parseUint16String(const String &raw, uint16_t &out) {
  String value = raw;
  value.trim();
  if (value.isEmpty()) return false;

  int base = 10;
  if (value.startsWith("0x") || value.startsWith("0X")) {
    base = 16;
    value = value.substring(2);
  }

  if (value.isEmpty()) return false;

  char *endPtr = nullptr;
  long parsed = strtol(value.c_str(), &endPtr, base);
  if (endPtr == value.c_str() || *endPtr != '\0') return false;
  if (parsed < 0 || parsed > 0xFFFF) return false;

  out = static_cast<uint16_t>(parsed);
  return true;
}

bool parseUint16JsonValue(const JsonVariantConst &variant, uint16_t &out) {
  if (variant.is<uint16_t>()) {
    out = variant.as<uint16_t>();
    return true;
  }

  if (variant.is<int>()) {
    int value = variant.as<int>();
    if (value < 0 || value > 0xFFFF) return false;
    out = static_cast<uint16_t>(value);
    return true;
  }

  if (variant.is<const char *>()) {
    String text = variant.as<const char *>();
    return parseUint16String(text, out);
  }

  if (variant.is<String>()) {
    String text = variant.as<String>();
    return parseUint16String(text, out);
  }

  return false;
}

void setStatus(uint8_t r, uint8_t g, uint8_t b) {
  pixels.setPixelColor(0, pixels.Color(r, g, b));
  pixels.show();
  pixels_alt.setPixelColor(0, pixels_alt.Color(r, g, b));
  pixels_alt.show();
}

void indicateRebootAndRestart(uint32_t delayMs = 450) {
  // Dark Brown Visual Indicator for Reboot / Restart (RGB: 110, 35, 5)
  setStatus(110, 35, 5);
  delay(delayMs);
  ESP.restart();
}

bool isPrivateIPv4(const IPAddress &ip) {
  if (ip[0] == 10) return true;
  if (ip[0] == 172 && ip[1] >= 16 && ip[1] <= 31) return true;
  if (ip[0] == 192 && ip[1] == 168) return true;
  if (ip[0] == 127) return true;
  if (ip[0] == 169 && ip[1] == 254) return true;
  return false;
}

String normalizeOptionalIp(const String &raw) {
  String v = raw;
  v.trim();
  if (v.isEmpty()) return "";

  IPAddress ip;
  if (!ip.fromString(v)) return "";
  return ip.toString();
}

void updateKvmUdpBinding();

void resetKvmBridgeRecordingLocked(uint32_t nowMs) {
  kvmBridgeRecordCount = 0;
  kvmBridgeRecordDropped = 0;
  kvmBridgeRecordStartMs = nowMs;
}

void captureKvmBridgeEvent(const KvmPacket &packet, uint32_t nowMs) {
  if (!kvmBridgeRecordMutex) return;
  if (xSemaphoreTake(kvmBridgeRecordMutex, 0) != pdTRUE) return;

  if (!kvmBridgeRecordEnabled) {
    xSemaphoreGive(kvmBridgeRecordMutex);
    return;
  }

  if (kvmBridgeRecordCount >= KVM_BRIDGE_RECORD_MAX_EVENTS) {
    kvmBridgeRecordDropped++;
    xSemaphoreGive(kvmBridgeRecordMutex);
    return;
  }

  KvmBridgeRecordEvent &record = kvmBridgeRecordEvents[kvmBridgeRecordCount++];
  record.dtMs = nowMs - kvmBridgeRecordStartMs;
  record.type = packet.type;
  record.buttons = 0;
  record.dx = 0;
  record.dy = 0;
  record.wheel = 0;
  record.pan = 0;
  record.modifiers = 0;
  memset(record.keys, 0, sizeof(record.keys));
  record.usageId = 0;

  if (packet.type == KVM_EVENT_MOUSE) {
    record.buttons = packet.payload.mouse.buttons & MOUSE_ALL;
    record.dx = packet.payload.mouse.dx;
    record.dy = packet.payload.mouse.dy;
    record.wheel = packet.payload.mouse.wheel;
    record.pan = packet.payload.mouse.pan;
  } else if (packet.type == KVM_EVENT_KEYBOARD) {
    record.modifiers = packet.payload.keyboard.modifiers;
    memcpy(record.keys, packet.payload.keyboard.keycodes, sizeof(record.keys));
  } else if (packet.type == KVM_EVENT_CONSUMER) {
    record.usageId = packet.payload.consumer.usageId;
  }

  xSemaphoreGive(kvmBridgeRecordMutex);
}

IPAddress extractClientIp(AsyncWebServerRequest *request) {
  if (request->hasHeader("X-Forwarded-For")) {
    String forwarded = request->getHeader("X-Forwarded-For")->value();
    int comma = forwarded.indexOf(',');
    if (comma > 0) forwarded = forwarded.substring(0, comma);
    forwarded.trim();

    IPAddress parsed;
    if (parsed.fromString(forwarded)) {
      return parsed;
    }
  }

  return request->client() ? request->client()->remoteIP() : IPAddress(0, 0, 0, 0);
}

void clearSession() {
  activeSessionToken = "";
  activeSessionIp = IPAddress(0, 0, 0, 0);
  activeSessionLastSeen = 0;
}

bool isSessionExpired() {
  if (activeSessionToken.isEmpty()) return true;
  return (millis() - activeSessionLastSeen) > SESSION_TTL_MS;
}

String buildSessionCookie(uint32_t maxAgeSeconds) {
  String cookie = "sid=" + activeSessionToken;
  cookie += "; Path=/; HttpOnly; SameSite=Strict; Max-Age=" + String(maxAgeSeconds);
  return cookie;
}

String buildExpiredSessionCookie() {
  return "sid=deleted; Path=/; HttpOnly; SameSite=Strict; Max-Age=0";
}

String getCookieValue(AsyncWebServerRequest *request, const String &key) {
  if (!request->hasHeader("Cookie")) return "";

  String cookieHeader = request->getHeader("Cookie")->value();
  String needle = key + "=";
  int start = cookieHeader.indexOf(needle);
  if (start < 0) return "";

  start += needle.length();
  int end = cookieHeader.indexOf(';', start);
  if (end < 0) end = cookieHeader.length();

  String value = cookieHeader.substring(start, end);
  value.trim();
  return value;
}

String getBearerToken(AsyncWebServerRequest *request) {
  if (!request->hasHeader("Authorization")) return "";

  String auth = request->getHeader("Authorization")->value();
  if (!auth.startsWith("Bearer ")) return "";

  String token = auth.substring(7);
  token.trim();
  return token;
}

String generateSessionToken() {
  char token[49];
  for (int i = 0; i < 24; i++) {
    uint8_t rnd = static_cast<uint8_t>(esp_random() & 0xFF);
    sprintf(token + (i * 2), "%02x", rnd);
  }
  token[48] = '\0';
  return String(token);
}

bool isSessionAuthorized(AsyncWebServerRequest *request, bool refreshSession = true) {
  if (activeSessionToken.isEmpty()) return false;

  if (isSessionExpired()) {
    clearSession();
    return false;
  }

  String sid = getCookieValue(request, "sid");
  if (sid.isEmpty() || sid != activeSessionToken) return false;

  IPAddress remoteIp = extractClientIp(request);
  if (remoteIp != activeSessionIp) return false;

  if (refreshSession) {
    activeSessionLastSeen = millis();
  }

  return true;
}

bool isProxyTokenAuthorized(AsyncWebServerRequest *request) {
  if (!proxyAuthEnabled || proxyAuthToken.length() < 16) return false;

  String token = "";
  if (request->hasHeader("X-Proxy-Token")) {
    token = request->getHeader("X-Proxy-Token")->value();
  } else {
    token = getBearerToken(request);
  }

  token.trim();
  if (token.isEmpty() || token != proxyAuthToken) return false;

  if (request->hasHeader("X-Forwarded-Proto")) {
    String proto = request->getHeader("X-Forwarded-Proto")->value();
    proto.toLowerCase();
    proto.trim();
    if (proto != "https") return false;
  } else {
    IPAddress remoteIp = request->client() ? request->client()->remoteIP() : IPAddress(0, 0, 0, 0);
    if (!isPrivateIPv4(remoteIp)) return false;
  }

  return true;
}

bool hasAccess(AsyncWebServerRequest *request, bool refreshSession = true) {
  if (isSessionAuthorized(request, refreshSession)) return true;
  return isProxyTokenAuthorized(request);
}

bool requireAuth(AsyncWebServerRequest *request) {
  if (hasAccess(request)) return true;
  request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
  return false;
}

LoginAttemptSlot *findLoginSlot(const IPAddress &ip, bool createIfMissing) {
  for (uint8_t i = 0; i < LOGIN_SLOT_COUNT; i++) {
    if (loginSlots[i].used && loginSlots[i].ip == ip) {
      return &loginSlots[i];
    }
  }

  if (!createIfMissing) return nullptr;

  int8_t emptyIndex = -1;
  uint32_t oldest = UINT32_MAX;
  int8_t oldestIndex = 0;

  for (uint8_t i = 0; i < LOGIN_SLOT_COUNT; i++) {
    if (!loginSlots[i].used) {
      emptyIndex = static_cast<int8_t>(i);
      break;
    }

    if (loginSlots[i].lastTouched < oldest) {
      oldest = loginSlots[i].lastTouched;
      oldestIndex = static_cast<int8_t>(i);
    }
  }

  int8_t index = (emptyIndex >= 0) ? emptyIndex : oldestIndex;
  loginSlots[index].used = true;
  loginSlots[index].ip = ip;
  loginSlots[index].failures = 0;
  loginSlots[index].blockedUntil = 0;
  loginSlots[index].lastTouched = millis();
  return &loginSlots[index];
}

bool isLoginBlocked(const IPAddress &ip, uint32_t &retryMs) {
  retryMs = 0;
  if (!loginRateLimitEnabled) return false;

  LoginAttemptSlot *slot = findLoginSlot(ip, false);
  if (!slot) return false;

  uint32_t now = millis();

  if ((slot->blockedUntil != 0) && (static_cast<int32_t>(slot->blockedUntil - now) > 0)) {
    retryMs = slot->blockedUntil - now;
    return true;
  }

  if ((slot->blockedUntil != 0) && (static_cast<int32_t>(now - slot->blockedUntil) >= 0)) {
    slot->blockedUntil = 0;
    slot->failures = 0;
  }

  if (static_cast<int32_t>(now - slot->lastTouched) > static_cast<int32_t>(LOGIN_RESET_MS)) {
    slot->failures = 0;
  }

  return false;
}

void recordLoginFailure(const IPAddress &ip) {
  if (!loginRateLimitEnabled) return;

  LoginAttemptSlot *slot = findLoginSlot(ip, true);
  if (!slot) return;

  uint32_t now = millis();

  if (static_cast<int32_t>(now - slot->lastTouched) > static_cast<int32_t>(LOGIN_RESET_MS)) {
    slot->failures = 0;
    slot->blockedUntil = 0;
  }

  slot->failures++;
  slot->lastTouched = now;

  if (slot->failures >= LOGIN_MAX_FAILURES) {
    slot->blockedUntil = now + LOGIN_BLOCK_MS;
    slot->failures = 0;
  }
}

void clearLoginFailures(const IPAddress &ip) {
  LoginAttemptSlot *slot = findLoginSlot(ip, false);
  if (!slot) return;

  slot->failures = 0;
  slot->blockedUntil = 0;
  slot->lastTouched = millis();
}

void pruneLoginSlots() {
  const uint32_t staleMs = 30UL * 60UL * 1000UL;
  uint32_t now = millis();

  for (uint8_t i = 0; i < LOGIN_SLOT_COUNT; i++) {
    if (!loginSlots[i].used) continue;

    bool expired = static_cast<int32_t>(now - loginSlots[i].lastTouched) > static_cast<int32_t>(staleMs);
    bool notBlocked = (loginSlots[i].blockedUntil == 0) || (static_cast<int32_t>(now - loginSlots[i].blockedUntil) >= 0);

    if (expired && notBlocked) {
      loginSlots[i].used = false;
      loginSlots[i].ip = IPAddress(0, 0, 0, 0);
      loginSlots[i].failures = 0;
      loginSlots[i].blockedUntil = 0;
      loginSlots[i].lastTouched = 0;
    }
  }
}

String sanitizeScriptName(const String &rawName) {
  String name = rawName;
  name.trim();
  name.replace("\\", "/");

  if (name.startsWith("/")) name = name.substring(1);
  if (name.startsWith("scripts/")) name = name.substring(8);

  if (name.isEmpty() || name.length() > 64) return "";
  if (name.indexOf("..") >= 0) return "";

  for (size_t i = 0; i < name.length(); i++) {
    char c = name[i];
    bool ok = (c >= 'a' && c <= 'z') ||
              (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') ||
              c == '_' || c == '-' || c == '.' || c == ' ';
    if (!ok) return "";
  }

  return name;
}

String sanitizeActionName(const String &rawName) {
  String name = rawName;
  name.trim();
  name.replace("\\", "/");

  if (name.startsWith("/")) name = name.substring(1);
  if (name.startsWith("actions/")) name = name.substring(8);

  if (name.isEmpty() || name.length() > 64) return "";
  if (name.indexOf("..") >= 0) return "";

  for (size_t i = 0; i < name.length(); i++) {
    char c = name[i];
    bool ok = (c >= 'a' && c <= 'z') ||
              (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') ||
              c == '_' || c == '-' || c == '.' || c == ' ';
    if (!ok) return "";
  }

  return name;
}

String scriptPathFromName(const String &safeName) {
  return String(SCRIPTS_DIR) + "/" + safeName;
}

String actionPathFromName(const String &safeName) {
  return String(ACTIONS_DIR) + "/" + safeName;
}

bool ensureScriptDir() {
  if (LittleFS.exists(SCRIPTS_DIR)) return true;
  return LittleFS.mkdir(SCRIPTS_DIR);
}

bool ensureActionsDir() {
  if (LittleFS.exists(ACTIONS_DIR)) return true;
  return LittleFS.mkdir(ACTIONS_DIR);
}

void persistSettings() {
  DynamicJsonDocument out(2304);
  out["ap_ssid"] = ap_ssid;
  out["ap_pass"] = ap_pass;
  out["sta_ssid"] = sta_ssid;
  out["sta_pass"] = sta_pass;
  out["admin_user"] = admin_user;
  out["admin_pass"] = admin_pass;

  out["login_rate_limit"] = loginRateLimitEnabled;
  out["proxy_auth_enabled"] = proxyAuthEnabled;
  out["proxy_auth_token"] = proxyAuthToken;

  out["kvm_enabled"] = kvmEnabled;
  out["kvm_port"] = kvmPort;
  out["kvm_allowed_ip"] = kvmAllowedIp;

  out["usb_vid"] = usbVendorId;
  out["usb_pid"] = usbProductId;
  out["usb_vendor_name"] = usbVendorName;
  out["usb_product_name"] = usbProductName;
  out["usb_msc_enabled"] = usbMscEnabled;
  out["usb_msc_label"] = usbMscVolumeLabel;
  out["fido_mode"] = fidoSecurityKeyMode;
  out["ble_fido_enabled"] = bleFidoEnabled;

  out["delay"] = typeDelay;
  out["burst_chars"] = burstChars;
  out["burst_pause"] = burstPauseMs;
  out["line_delay"] = lineDelayMs;
  out["bright"] = ledBrightness;
  out["kvm_mouse_smooth"] = kvmMouseSmoothness;

  File file = LittleFS.open(SETTINGS_FILE, "w");
  if (file) {
    serializeJson(out, file);
    file.close();
  }

  // Persist backup to hardware NVS flash partition
  Preferences prefs;
  if (prefs.begin("sysconfig", false)) {
    prefs.putString("ap_ssid", ap_ssid);
    prefs.putString("ap_pass", ap_pass);
    prefs.putString("sta_ssid", sta_ssid);
    prefs.putString("sta_pass", sta_pass);
    prefs.putString("admin_user", admin_user);
    prefs.putString("admin_pass", admin_pass);
    prefs.putBool("rate_limit", loginRateLimitEnabled);
    prefs.putBool("proxy_auth", proxyAuthEnabled);
    prefs.putString("proxy_tok", proxyAuthToken);
    prefs.putBool("kvm_en", kvmEnabled);
    prefs.putUShort("kvm_port", kvmPort);
    prefs.putString("kvm_ip", kvmAllowedIp);
    prefs.putUShort("usb_vid", usbVendorId);
    prefs.putUShort("usb_pid", usbProductId);
    prefs.putString("usb_vn", usbVendorName);
    prefs.putString("usb_pn", usbProductName);
    prefs.putBool("usb_msc", usbMscEnabled);
    prefs.putString("usb_lbl", usbMscVolumeLabel);
    prefs.putBool("fido_mode", fidoSecurityKeyMode);
    prefs.putBool("ble_fido", bleFidoEnabled);
    prefs.putInt("delay", typeDelay);
    prefs.putInt("burst_c", burstChars);
    prefs.putInt("burst_p", burstPauseMs);
    prefs.putInt("line_d", lineDelayMs);
    prefs.putInt("bright", ledBrightness);
    prefs.putInt("kvm_ms", kvmMouseSmoothness);
    prefs.end();
  }
}

bool loadSettingsNVS() {
  Preferences prefs;
  if (!prefs.begin("sysconfig", true)) return false;
  if (!prefs.isKey("admin_user") && !prefs.isKey("sta_ssid")) {
    prefs.end();
    return false;
  }

  ap_ssid = prefs.getString("ap_ssid", ap_ssid);
  ap_pass = prefs.getString("ap_pass", ap_pass);
  sta_ssid = prefs.getString("sta_ssid", sta_ssid);
  sta_pass = prefs.getString("sta_pass", sta_pass);
  admin_user = prefs.getString("admin_user", admin_user);
  admin_pass = prefs.getString("admin_pass", admin_pass);
  loginRateLimitEnabled = prefs.getBool("rate_limit", loginRateLimitEnabled);
  proxyAuthEnabled = prefs.getBool("proxy_auth", proxyAuthEnabled);
  proxyAuthToken = prefs.getString("proxy_tok", proxyAuthToken);
  kvmEnabled = prefs.getBool("kvm_en", kvmEnabled);
  kvmPort = prefs.getUShort("kvm_port", kvmPort);
  kvmAllowedIp = prefs.getString("kvm_ip", kvmAllowedIp);
  usbVendorId = prefs.getUShort("usb_vid", usbVendorId);
  usbProductId = prefs.getUShort("usb_pid", usbProductId);
  usbVendorName = prefs.getString("usb_vn", usbVendorName);
  usbProductName = prefs.getString("usb_pn", usbProductName);
  usbMscEnabled = prefs.getBool("usb_msc", usbMscEnabled);
  usbMscVolumeLabel = prefs.getString("usb_lbl", usbMscVolumeLabel);
  fidoSecurityKeyMode = prefs.getBool("fido_mode", fidoSecurityKeyMode);
  bleFidoEnabled = prefs.getBool("ble_fido", bleFidoEnabled);
  typeDelay = prefs.getInt("delay", typeDelay);
  burstChars = prefs.getInt("burst_c", burstChars);
  burstPauseMs = prefs.getInt("burst_p", burstPauseMs);
  lineDelayMs = prefs.getInt("line_d", lineDelayMs);
  ledBrightness = prefs.getInt("bright", ledBrightness);
  kvmMouseSmoothness = prefs.getInt("kvm_ms", kvmMouseSmoothness);
  prefs.end();
  return true;
}

void loadSettings() {
  bool loadedFromFile = false;
  if (LittleFS.exists(SETTINGS_FILE)) {
    File file = LittleFS.open(SETTINGS_FILE, "r");
    if (file) {
      DynamicJsonDocument doc(2304);
      DeserializationError err = deserializeJson(doc, file);
      file.close();

      if (!err) {
        if (doc.containsKey("ap_ssid")) ap_ssid = doc["ap_ssid"].as<String>();
        if (doc.containsKey("ap_pass")) ap_pass = doc["ap_pass"].as<String>();
        if (doc.containsKey("sta_ssid")) sta_ssid = doc["sta_ssid"].as<String>();
        if (doc.containsKey("sta_pass")) sta_pass = doc["sta_pass"].as<String>();
        if (doc.containsKey("admin_user")) admin_user = doc["admin_user"].as<String>();
        if (doc.containsKey("admin_pass")) admin_pass = doc["admin_pass"].as<String>();

        loginRateLimitEnabled = doc["login_rate_limit"] | loginRateLimitEnabled;
        proxyAuthEnabled = doc["proxy_auth_enabled"] | proxyAuthEnabled;
        if (doc.containsKey("proxy_auth_token")) proxyAuthToken = doc["proxy_auth_token"].as<String>();

        if (doc.containsKey("kvm_enabled")) kvmEnabled = doc["kvm_enabled"].as<bool>();
        kvmPort = static_cast<uint16_t>(clampInt(doc["kvm_port"] | static_cast<int>(kvmPort), 1, 65535));
        if (doc.containsKey("kvm_allowed_ip")) kvmAllowedIp = doc["kvm_allowed_ip"].as<String>();
        kvmAllowedIp = normalizeOptionalIp(kvmAllowedIp);

        if (doc.containsKey("usb_vid")) {
          uint16_t parsed = usbVendorId;
          if (parseUint16JsonValue(doc["usb_vid"], parsed)) usbVendorId = parsed;
        }
        if (doc.containsKey("usb_pid")) {
          uint16_t parsed = usbProductId;
          if (parseUint16JsonValue(doc["usb_pid"], parsed)) usbProductId = parsed;
        }
        if (doc.containsKey("usb_vendor_name")) usbVendorName = doc["usb_vendor_name"].as<String>();
        if (doc.containsKey("usb_product_name")) usbProductName = doc["usb_product_name"].as<String>();
        if (doc.containsKey("usb_msc_enabled")) usbMscEnabled = doc["usb_msc_enabled"].as<bool>();
        if (doc.containsKey("usb_msc_label")) usbMscVolumeLabel = doc["usb_msc_label"].as<String>();
        if (doc.containsKey("ble_fido_enabled")) bleFidoEnabled = doc["ble_fido_enabled"].as<bool>();
        if (doc.containsKey("fido_mode")) {
          fidoSecurityKeyMode = doc["fido_mode"].as<bool>();
        } else {
          Preferences prefs;
          if (prefs.begin("sysconfig", true)) {
            fidoSecurityKeyMode = prefs.getBool("fido_mode", fidoSecurityKeyMode);
            prefs.end();
          }
        }

        usbVendorName.trim();
        usbProductName.trim();
        if (usbVendorName.isEmpty()) usbVendorName = "Espressif";
        if (usbProductName.isEmpty()) usbProductName = "ESP32-S3 HID Console";
        if (usbVendorName.length() > 48) usbVendorName = usbVendorName.substring(0, 48);
        if (usbProductName.length() > 48) usbProductName = usbProductName.substring(0, 48);

        typeDelay = clampInt(doc["delay"] | typeDelay, 0, 200);
        burstChars = clampInt(doc["burst_chars"] | burstChars, 6, 96);
        burstPauseMs = clampInt(doc["burst_pause"] | burstPauseMs, 0, 120);
        lineDelayMs = clampInt(doc["line_delay"] | lineDelayMs, 0, 250);
        ledBrightness = clampInt(doc["bright"] | ledBrightness, 0, 255);
        kvmMouseSmoothness = clampInt(doc["kvm_mouse_smooth"] | kvmMouseSmoothness, 25, 250);

        if (proxyAuthToken.length() > 128) proxyAuthToken = proxyAuthToken.substring(0, 128);
        if (proxyAuthToken.length() < 16) proxyAuthEnabled = false;

        loadedFromFile = true;
      }
    }
  }

  if (!loadedFromFile) {
    if (loadSettingsNVS()) {
      Serial.println("Restored settings from persistent NVS flash!");
      persistSettings();
    }
  } else {
    persistSettings();
  }

  pixels.setBrightness(ledBrightness);
}

bool applySettingsJson(const String &jsonBody, bool &usbIdentityChanged, bool &wifiChanged) {
  usbIdentityChanged = false;
  wifiChanged = false;

  DynamicJsonDocument doc(2304);
  DeserializationError err = deserializeJson(doc, jsonBody);
  if (err) return false;

  uint16_t oldUsbVid = usbVendorId;
  uint16_t oldUsbPid = usbProductId;
  String oldUsbVendorName = usbVendorName;
  String oldUsbProductName = usbProductName;
  bool oldUsbMscEnabled = usbMscEnabled;

  String oldApSsid = ap_ssid;
  String oldApPass = ap_pass;
  String oldStaSsid = sta_ssid;
  String oldStaPass = sta_pass;

  if (doc.containsKey("ap_ssid")) ap_ssid = doc["ap_ssid"].as<String>();
  if (doc.containsKey("ap_pass")) {
    String v = doc["ap_pass"].as<String>();
    if (v.isEmpty() || v.length() >= 8) ap_pass = v;
  }

  if (doc.containsKey("sta_ssid")) sta_ssid = doc["sta_ssid"].as<String>();
  if (doc.containsKey("sta_pass")) sta_pass = doc["sta_pass"].as<String>();

  if (doc.containsKey("admin_user")) {
    String v = doc["admin_user"].as<String>();
    if (v.length() >= 3 && v.length() <= 24) admin_user = v;
  }

  if (doc.containsKey("admin_pass")) {
    String v = doc["admin_pass"].as<String>();
    if (v.length() >= 6 && v.length() <= 64) admin_pass = v;
  }

  if (doc.containsKey("login_rate_limit")) loginRateLimitEnabled = doc["login_rate_limit"].as<bool>();

  if (doc.containsKey("proxy_auth_token")) {
    String token = doc["proxy_auth_token"].as<String>();
    token.trim();
    if (token.isEmpty()) {
      proxyAuthToken = "";
      proxyAuthEnabled = false;
    } else if (token.length() >= 16 && token.length() <= 128) {
      proxyAuthToken = token;
    }
  }

  if (doc.containsKey("proxy_auth_enabled")) {
    bool enabled = doc["proxy_auth_enabled"].as<bool>();
    proxyAuthEnabled = enabled && (proxyAuthToken.length() >= 16);
  }

  if (doc.containsKey("kvm_enabled")) kvmEnabled = doc["kvm_enabled"].as<bool>();
  if (doc.containsKey("kvm_port")) {
    uint16_t parsedPort = kvmPort;
    if (parseUint16JsonValue(doc["kvm_port"], parsedPort)) {
      kvmPort = static_cast<uint16_t>(clampInt(parsedPort, 1, 65535));
    }
  }
  if (doc.containsKey("kvm_allowed_ip")) {
    String parsedIp = doc["kvm_allowed_ip"].as<String>();
    kvmAllowedIp = normalizeOptionalIp(parsedIp);
  }

  if (doc.containsKey("usb_vid")) {
    uint16_t parsed = usbVendorId;
    if (parseUint16JsonValue(doc["usb_vid"], parsed)) usbVendorId = parsed;
  }
  if (doc.containsKey("usb_pid")) {
    uint16_t parsed = usbProductId;
    if (parseUint16JsonValue(doc["usb_pid"], parsed)) usbProductId = parsed;
  }

  if (doc.containsKey("usb_vendor_name")) {
    String v = doc["usb_vendor_name"].as<String>();
    v.trim();
    if (v.length() >= 1 && v.length() <= 48) usbVendorName = v;
  }

  if (doc.containsKey("usb_product_name")) {
    String v = doc["usb_product_name"].as<String>();
    v.trim();
    if (v.length() >= 1 && v.length() <= 48) usbProductName = v;
  }

  if (doc.containsKey("usb_msc_enabled")) {
    usbMscEnabled = doc["usb_msc_enabled"].as<bool>();
  }
  if (doc.containsKey("usb_msc_label")) {
    String lbl = doc["usb_msc_label"].as<String>();
    lbl.trim();
    if (!lbl.isEmpty() && lbl.length() <= 11) usbMscVolumeLabel = lbl;
  }

  bool oldBleFido = bleFidoEnabled;
  if (doc.containsKey("ble_fido_enabled")) {
    bleFidoEnabled = doc["ble_fido_enabled"].as<bool>();
  }

  typeDelay = clampInt(doc["delay"] | typeDelay, 0, 200);
  burstChars = clampInt(doc["burst_chars"] | burstChars, 6, 96);
  burstPauseMs = clampInt(doc["burst_pause"] | burstPauseMs, 0, 120);
  lineDelayMs = clampInt(doc["line_delay"] | lineDelayMs, 0, 250);
  ledBrightness = clampInt(doc["bright"] | ledBrightness, 0, 255);
  kvmMouseSmoothness = clampInt(doc["kvm_mouse_smooth"] | kvmMouseSmoothness, 25, 250);

  pixels.setBrightness(ledBrightness);
  persistSettings();

  updateKvmUdpBinding();

  usbIdentityChanged =
    (oldUsbVid != usbVendorId) ||
    (oldUsbPid != usbProductId) ||
    (oldUsbVendorName != usbVendorName) ||
    (oldUsbProductName != usbProductName) ||
    (oldUsbMscEnabled != usbMscEnabled) ||
    (oldBleFido != bleFidoEnabled);

  wifiChanged =
    (oldApSsid != ap_ssid) ||
    (oldApPass != ap_pass) ||
    (oldStaSsid != sta_ssid) ||
    (oldStaPass != sta_pass);

  return true;
}

void keyboardTap(uint8_t keyCode, uint16_t holdMs = 35) {
  if(Keyboard) Keyboard->press(keyCode);
  delay(holdMs);
  if(Keyboard) Keyboard->releaseAll();
}

void keyboardCombo(bool ctrl, bool alt, bool shift, bool gui, uint8_t keyCode, uint16_t holdMs = 40) {
  if (ctrl) if(Keyboard) Keyboard->press(KEY_LEFT_CTRL);
  if (alt) if(Keyboard) Keyboard->press(KEY_LEFT_ALT);
  if (shift) if(Keyboard) Keyboard->press(KEY_LEFT_SHIFT);
  if (gui) if(Keyboard) Keyboard->press(KEY_LEFT_GUI);

  if(Keyboard) Keyboard->press(keyCode);
  delay(holdMs);
  if(Keyboard) Keyboard->releaseAll();
}

bool queueHidEvent(const HidRealtimeEvent &event, TickType_t timeoutTicks = 0) {
  if (!hidEventQueue) return false;
  return xQueueSend(hidEventQueue, &event, timeoutTicks) == pdPASS;
}

void queueHidReleaseAll() {
  HidRealtimeEvent keyEvent = {};
  keyEvent.type = HidRealtimeType::KeyReleaseAll;
  queueHidEvent(keyEvent, pdMS_TO_TICKS(20));

  HidRealtimeEvent mouseEvent = {};
  mouseEvent.type = HidRealtimeType::MouseButton;
  mouseEvent.mouseButton = MOUSE_ALL;
  mouseEvent.mouseAction = MOUSE_ACTION_UP;
  queueHidEvent(mouseEvent, pdMS_TO_TICKS(20));

  HidRealtimeEvent consumerEvent = {};
  consumerEvent.type = HidRealtimeType::ConsumerControl;
  consumerEvent.consumerUsage = 0;
  queueHidEvent(consumerEvent, pdMS_TO_TICKS(20));
}

void hidRealtimeTask(void *parameter) {
  (void)parameter;

  HidRealtimeEvent event;
  uint8_t kvmButtons = 0;
  for (;;) {
    if (xQueueReceive(hidEventQueue, &event, portMAX_DELAY) != pdTRUE) continue;

    switch (event.type) {
      case HidRealtimeType::KeyTap:
        keyboardTap(event.keyCode, event.holdMs);
        break;
      case HidRealtimeType::KeyDown:
        if(Keyboard) Keyboard->press(event.keyCode);
        break;
      case HidRealtimeType::KeyUp:
        if(Keyboard) Keyboard->release(event.keyCode);
        break;
      case HidRealtimeType::KeyReleaseAll:
        if(Keyboard) Keyboard->releaseAll();
        if(Mouse) Mouse->release(MOUSE_ALL);
        if(Consumer) Consumer->release();
        kvmButtons = 0;
        break;
      case HidRealtimeType::Combo:
        keyboardCombo(event.ctrl, event.alt, event.shift, event.gui, event.keyCode, event.holdMs);
        break;
      case HidRealtimeType::MouseMove:
        if(Mouse) Mouse->move(event.dx, event.dy, 0, 0);
        break;
      case HidRealtimeType::MouseScroll:
        if(Mouse) Mouse->move(0, 0, event.wheel, event.pan);
        break;
      case HidRealtimeType::MouseButton:
        if (event.mouseAction == MOUSE_ACTION_DOWN) {
          if(Mouse) Mouse->press(event.mouseButton);
        } else if (event.mouseAction == MOUSE_ACTION_UP) {
          if(Mouse) Mouse->release(event.mouseButton);
        } else {
          if(Mouse) Mouse->click(event.mouseButton);
        }
        break;
      case HidRealtimeType::KvmKeyboardState: {
        KeyReport report = {};
        report.modifiers = event.kvmModifiers;
        memcpy(report.keys, event.kvmKeys, sizeof(report.keys));
        if(Keyboard) Keyboard->sendReport(&report);
      } break;
      case HidRealtimeType::KvmMouseState: {
        uint8_t changed = kvmButtons ^ event.kvmButtons;
        if (changed != 0) {
          uint8_t pressedMask = changed & event.kvmButtons;
          uint8_t releasedMask = changed & static_cast<uint8_t>(~event.kvmButtons);

          if (pressedMask) if(Mouse) Mouse->press(pressedMask);
          if (releasedMask) if(Mouse) Mouse->release(releasedMask);

          kvmButtons = event.kvmButtons;
        }

        int smoothPercent = clampInt(kvmMouseSmoothness, 25, 250);
        int16_t dx = scaleMouseDelta(event.kvmDx, smoothPercent);
        int16_t dy = scaleMouseDelta(event.kvmDy, smoothPercent);

        while (dx != 0 || dy != 0) {
          int8_t stepX = clampInt8(dx, -120, 120);
          int8_t stepY = clampInt8(dy, -120, 120);
          if(Mouse) Mouse->move(stepX, stepY, 0, 0);

          dx -= stepX;
          dy -= stepY;

          if (dx != 0 || dy != 0) {
            vTaskDelay(1);
          }
        }

        if (event.kvmWheel != 0 || event.kvmPan != 0) {
          if(Mouse) Mouse->move(0, 0, event.kvmWheel, event.kvmPan);
        }
      } break;
      case HidRealtimeType::ConsumerControl:
        if (event.consumerUsage == 0) {
          if(Consumer) Consumer->release();
        } else {
          if(Consumer) Consumer->press(event.consumerUsage);
          if(Consumer) Consumer->release();
        }
        break;
      default:
        break;
    }
  }
}

void typeTextInternal(size_t startIndex, size_t length) {
  if (!psramBuffer || startIndex >= BUFFER_SIZE) return;

  size_t safeLength = length;
  if (startIndex + safeLength > BUFFER_SIZE) {
    safeLength = BUFFER_SIZE - startIndex;
  }

  int charDelay = clampInt(typeDelay, 0, 200);
  int batchSize = clampInt(burstChars, 6, 96);
  int batchPause = clampInt(burstPauseMs, 0, 120);
  int newlinePause = clampInt(lineDelayMs, 0, 250);

  int sincePause = 0;
  for (size_t i = 0; i < safeLength; i++) {
    if (stopScriptFlag) {
      scriptProgressPercent = 0;
      return;
    }

    char c = psramBuffer[startIndex + i];
    if(Keyboard) Keyboard->write(static_cast<uint8_t>(c));

    // FIX: vTaskDelay yields to RTOS scheduler; stopScriptFlag is checked
    // immediately after each char rather than blocking Core 1 hard.
    if (charDelay > 0) vTaskDelay(pdMS_TO_TICKS(charDelay));

    // FIX: progress counter now synced to burstChars (batchSize)
    if (i % (size_t)batchSize == 0 && safeLength > 0) {
      scriptProgressPercent = static_cast<uint8_t>((i * 100) / safeLength);
    }

    sincePause++;
    if (c == '\n' || c == '\r') {
      sincePause = 0;
      if (newlinePause > 0) vTaskDelay(pdMS_TO_TICKS(newlinePause));
    } else if (sincePause >= batchSize) {
      sincePause = 0;
      if (batchPause > 0) vTaskDelay(pdMS_TO_TICKS(batchPause));
      vTaskDelay(1); // yield to web server task
    }
  }
  scriptProgressPercent = 0;
}

// --- ZERO-COPY DUCKY SCRIPT PARSER & FAST LOOKUP ENGINE ---

struct DuckyKeyMapEntry {
  const char *name;
  uint8_t scancode;
};

// FIX: Sorted alphabetically (case-insensitive) to enable O(log n) binary search.
static const DuckyKeyMapEntry FAST_DUCKY_KEYMAP[] = {
  {"APP",         KEY_APPLICATION},
  {"BACKSPACE",   KEY_BACKSPACE},
  {"BKSP",        KEY_BACKSPACE},
  {"BREAK",       KEY_PAUSE},
  {"CAPS",        KEY_CAPS_LOCK},
  {"CAPSLOCK",    KEY_CAPS_LOCK},
  {"DEL",         KEY_DELETE},
  {"DELETE",      KEY_DELETE},
  {"DOWN",        KEY_DOWN_ARROW},
  {"DOWNARROW",   KEY_DOWN_ARROW},
  {"END",         KEY_END},
  {"ENTER",       KEY_RETURN},
  {"ESC",         KEY_ESC},
  {"ESCAPE",      KEY_ESC},
  {"F1",          KEY_F1},
  {"F10",         KEY_F10},
  {"F11",         KEY_F11},
  {"F12",         KEY_F12},
  {"F2",          KEY_F2},
  {"F3",          KEY_F3},
  {"F4",          KEY_F4},
  {"F5",          KEY_F5},
  {"F6",          KEY_F6},
  {"F7",          KEY_F7},
  {"F8",          KEY_F8},
  {"F9",          KEY_F9},
  {"HOME",        KEY_HOME},
  {"INS",         KEY_INSERT},
  {"INSERT",      KEY_INSERT},
  {"LEFT",        KEY_LEFT_ARROW},
  {"LEFTARROW",   KEY_LEFT_ARROW},
  {"MENU",        KEY_APPLICATION},
  {"NUMLOCK",     KEY_NUM_LOCK},
  {"PAGE_DOWN",   KEY_PAGE_DOWN},
  {"PAGE_UP",     KEY_PAGE_UP},
  {"PAGEDOWN",    KEY_PAGE_DOWN},
  {"PAGEUP",      KEY_PAGE_UP},
  {"PAUSE",       KEY_PAUSE},
  {"PRINT",       KEY_PRINT_SCREEN},
  {"PRINTSCREEN", KEY_PRINT_SCREEN},
  {"PRINTSCRN",   KEY_PRINT_SCREEN},
  {"RETURN",      KEY_RETURN},
  {"RIGHT",       KEY_RIGHT_ARROW},
  {"RIGHTARROW",  KEY_RIGHT_ARROW},
  {"SCROLLLOCK",  KEY_SCROLL_LOCK},
  {"SPACE",       ' '},
  {"TAB",         KEY_TAB},
  {"UP",          KEY_UP_ARROW},
  {"UPARROW",     KEY_UP_ARROW},
};
constexpr size_t FAST_DUCKY_KEYMAP_SIZE = sizeof(FAST_DUCKY_KEYMAP) / sizeof(FAST_DUCKY_KEYMAP[0]);

static inline bool fastStrCaseEquals(const char *a, size_t aLen, const char *b) {
  size_t bLen = strlen(b);
  if (aLen != bLen) return false;
  return (strncasecmp(a, b, aLen) == 0);
}

static inline bool fastStrCaseStartsWith(const char *str, size_t len, const char *prefix, size_t prefixLen) {
  if (len < prefixLen) return false;
  return (strncasecmp(str, prefix, prefixLen) == 0);
}

// FIX: O(log n) binary search replaces O(n) linear scan.
uint8_t fastResolveDuckyKey(const char *token, size_t len) {
  if (!token || len == 0) return 0;

  int lo = 0, hi = (int)FAST_DUCKY_KEYMAP_SIZE - 1;
  while (lo <= hi) {
    int mid = (lo + hi) / 2;
    // Compare first `len` chars; then disambiguate on full key name length.
    int cmp = strncasecmp(token, FAST_DUCKY_KEYMAP[mid].name, len);
    if (cmp == 0) {
      size_t keyLen = strlen(FAST_DUCKY_KEYMAP[mid].name);
      if (keyLen == len) return FAST_DUCKY_KEYMAP[mid].scancode; // exact match
      if (keyLen > len)  hi = mid - 1; // token is prefix of this key, search left
      else               lo = mid + 1; // key is shorter than token, search right
    } else if (cmp < 0) {
      hi = mid - 1;
    } else {
      lo = mid + 1;
    }
  }

  if (len == 1) {
    char c = token[0];
    if (c >= 'A' && c <= 'Z') return static_cast<uint8_t>(c - 'A' + 'a');
    return static_cast<uint8_t>(c);
  }

  return 0;
}

uint8_t resolveDuckyKey(const String &token) {
  return fastResolveDuckyKey(token.c_str(), token.length());
}

bool executeDuckyCommandLineFast(const char *line, size_t len, int defaultDelay) {
  while (len > 0 && (*line == ' ' || *line == '\t' || *line == '\r')) {
    line++;
    len--;
  }
  while (len > 0 && (line[len - 1] == ' ' || line[len - 1] == '\t' || line[len - 1] == '\r')) {
    len--;
  }
  if (len == 0) return false;

  if (fastStrCaseStartsWith(line, len, "REM", 3)) return false;

  if (fastStrCaseStartsWith(line, len, "STRING ", 7)) {
    // FIX: Burst throttle and vTaskDelay now applied to STRING (same as BLOCK).
    const char *payload = line + 7;
    size_t payloadLen = len - 7;
    int charDelay  = clampInt(typeDelay,    0,   40);
    int batchSize  = clampInt(burstChars,   6,   96);
    int batchPause = clampInt(burstPauseMs, 0,  120);
    int sincePause = 0;
    for (size_t c = 0; c < payloadLen; c++) {
      if (stopScriptFlag) break;
      if(Keyboard) Keyboard->write(static_cast<uint8_t>(payload[c]));
      if (charDelay > 0) vTaskDelay(pdMS_TO_TICKS(charDelay));
      if (++sincePause >= batchSize) {
        sincePause = 0;
        if (batchPause > 0) vTaskDelay(pdMS_TO_TICKS(batchPause));
        vTaskDelay(1);
      }
    }
  } else if (fastStrCaseStartsWith(line, len, "STRINGLN ", 9)) {
    // FIX: Same burst throttle applied to STRINGLN.
    const char *payload = line + 9;
    size_t payloadLen = len - 9;
    int charDelay  = clampInt(typeDelay,    0,   40);
    int batchSize  = clampInt(burstChars,   6,   96);
    int batchPause = clampInt(burstPauseMs, 0,  120);
    int sincePause = 0;
    for (size_t c = 0; c < payloadLen; c++) {
      if (stopScriptFlag) break;
      if(Keyboard) Keyboard->write(static_cast<uint8_t>(payload[c]));
      if (charDelay > 0) vTaskDelay(pdMS_TO_TICKS(charDelay));
      if (++sincePause >= batchSize) {
        sincePause = 0;
        if (batchPause > 0) vTaskDelay(pdMS_TO_TICKS(batchPause));
        vTaskDelay(1);
      }
    }
    keyboardTap(KEY_RETURN);
  } else if (fastStrCaseStartsWith(line, len, "DELAY ", 6)) {
    int d = atoi(line + 6);
    if (d > 0) vTaskDelay(pdMS_TO_TICKS(d));
  } else if (fastStrCaseStartsWith(line, len, "WAIT_BUTTON ", 12)) {
    // FIX: Pause script execution until BOOT button (GPIO 0) is pressed or timeout.
    int timeoutMs = atoi(line + 12);
    if (timeoutMs <= 0) timeoutMs = 10000;
    uint32_t start = millis();
    while (!stopScriptFlag && (millis() - start < (uint32_t)timeoutMs)) {
      if (digitalRead(0) == LOW) break; // BOOT button is active-LOW
      vTaskDelay(pdMS_TO_TICKS(10));
    }
  } else if (fastStrCaseStartsWith(line, len, "MOUSE_MOVE_ABS ", 15) || fastStrCaseStartsWith(line, len, "MOUSEMOVEABS ", 13)) {
    size_t offset = (line[5] == '_') ? 15 : 13;
    const char *p = line + offset;
    float x = atof(p);
    while (*p && *p != ' ' && *p != '\t') p++;
    while (*p == ' ' || *p == '\t') p++;
    float y = atof(p);
    mouseMoveAbsolute(x, y);
  } else if (fastStrCaseStartsWith(line, len, "MOUSE_CLICK_ABS ", 16) || fastStrCaseStartsWith(line, len, "MOUSECLICKABS ", 14)) {
    size_t offset = (line[5] == '_') ? 16 : 14;
    const char *p = line + offset;
    while (*p == ' ' || *p == '\t') p++;
    const char *btnStart = p;
    while (*p && *p != ' ' && *p != '\t') p++;
    size_t btnLen = p - btnStart;
    while (*p == ' ' || *p == '\t') p++;
    float x = atof(p);
    while (*p && *p != ' ' && *p != '\t') p++;
    while (*p == ' ' || *p == '\t') p++;
    float y = atof(p);
    char btnBuf[16];
    size_t cpy = (btnLen < 15) ? btnLen : 15;
    memcpy(btnBuf, btnStart, cpy);
    btnBuf[cpy] = '\0';
    mouseMoveAbsolute(x, y, parseMouseButton(String(btnBuf)), MOUSE_ACTION_CLICK);
  } else {
    bool ctrl = false, alt = false, shift = false, gui = false;
    uint8_t targetKey = 0;
    int tokenCount = 0;

    const char *cursor = line;
    const char *end = line + len;

    while (cursor < end) {
      while (cursor < end && (*cursor == ' ' || *cursor == '\t' || *cursor == '+' || *cursor == '-')) cursor++;
      if (cursor >= end) break;

      const char *tokStart = cursor;
      while (cursor < end && *cursor != ' ' && *cursor != '\t' && *cursor != '+' && *cursor != '-') cursor++;
      size_t tokLen = cursor - tokStart;
      if (tokLen == 0) continue;
      tokenCount++;

      if (fastStrCaseEquals(tokStart, tokLen, "CTRL") || fastStrCaseEquals(tokStart, tokLen, "CONTROL")) {
        ctrl = true;
      } else if (fastStrCaseEquals(tokStart, tokLen, "ALT")) {
        alt = true;
      } else if (fastStrCaseEquals(tokStart, tokLen, "SHIFT")) {
        shift = true;
      } else if (fastStrCaseEquals(tokStart, tokLen, "GUI") || fastStrCaseEquals(tokStart, tokLen, "WINDOWS") || fastStrCaseEquals(tokStart, tokLen, "WIN") || fastStrCaseEquals(tokStart, tokLen, "COMMAND")) {
        gui = true;
      } else {
        uint8_t k = fastResolveDuckyKey(tokStart, tokLen);
        if (k != 0) {
          targetKey = k;
        } else if (tokLen == 1) {
          char c = tokStart[0];
          if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 32);
          targetKey = static_cast<uint8_t>(c);
        }
      }
    }

    if (tokenCount > 0) {
      if (ctrl || alt || shift || gui) {
        if (targetKey != 0) {
          keyboardCombo(ctrl, alt, shift, gui, targetKey);
        } else {
          if (gui) keyboardTap(KEY_LEFT_GUI, 80);
          else if (ctrl) keyboardTap(KEY_LEFT_CTRL, 40);
          else if (alt) keyboardTap(KEY_LEFT_ALT, 40);
          else if (shift) keyboardTap(KEY_LEFT_SHIFT, 40);
        }
      } else if (targetKey != 0) {
        keyboardTap(targetKey);
      }
    }
  }

  if (defaultDelay > 0) delay(defaultDelay);
  return true;
}

bool executeDuckyCommandLine(const String &rawLine, int defaultDelay) {
  return executeDuckyCommandLineFast(rawLine.c_str(), rawLine.length(), defaultDelay);
}

void parseAndExecuteInternal(size_t totalLength) {
  if (!psramBuffer || totalLength == 0) return;

  size_t i = 0;
  int defaultDelay = 0;
  int lineCounter = 0;

  scriptTotalLines = 0;
  const char *bufPtr = psramBuffer;
  for (size_t c = 0; c < totalLength; c++) {
    if (bufPtr[c] == '\n') scriptTotalLines++;
  }
  if (totalLength > 0 && bufPtr[totalLength - 1] != '\n') scriptTotalLines++;
  if (scriptTotalLines == 0) scriptTotalLines = 1;

  scriptCurrentLine = 0;
  scriptProgressPercent = 0;
  scriptCurrentCommand = "";

  char lastExecBuf[128] = {0};
  size_t lastExecLen = 0;

  while (i < totalLength) {
    if (stopScriptFlag) break;

    size_t lineStart = i;
    while (i < totalLength && psramBuffer[i] != '\n') i++;
    size_t lineEnd = i;
    if (i < totalLength && psramBuffer[i] == '\n') i++;

    const char *line = psramBuffer + lineStart;
    size_t lineLen = lineEnd - lineStart;

    while (lineLen > 0 && (*line == ' ' || *line == '\t' || *line == '\r')) {
      line++;
      lineLen--;
    }
    while (lineLen > 0 && (line[lineLen - 1] == ' ' || line[lineLen - 1] == '\t' || line[lineLen - 1] == '\r')) {
      lineLen--;
    }

    scriptCurrentLine++;
    scriptProgressPercent = static_cast<uint8_t>(clampInt(static_cast<int>((scriptCurrentLine * 100) / scriptTotalLines), 0, 100));

    if (lineLen == 0) continue;

    size_t previewLen = (lineLen < 48) ? lineLen : 48;
    char previewBuf[52];
    memcpy(previewBuf, line, previewLen);
    previewBuf[previewLen] = '\0';
    scriptCurrentCommand = previewBuf;

    if (fastStrCaseStartsWith(line, lineLen, "REM", 3)) continue;

    if (fastStrCaseStartsWith(line, lineLen, "DEFAULT_DELAY ", 14)) {
      defaultDelay = clampInt(atoi(line + 14), 0, 5000);
      continue;
    } else if (fastStrCaseStartsWith(line, lineLen, "DEFAULTDELAY ", 13)) {
      defaultDelay = clampInt(atoi(line + 13), 0, 5000);
      continue;
    }

    if (fastStrCaseEquals(line, lineLen, "BLOCK")) {
      size_t blockStart = i;
      size_t blockEnd = totalLength;
      bool foundEnd = false;

      size_t cursor = i;
      while (cursor < totalLength) {
        size_t mStart = cursor;
        while (cursor < totalLength && psramBuffer[cursor] != '\n') cursor++;
        size_t mEnd = cursor;
        if (cursor < totalLength && psramBuffer[cursor] == '\n') cursor++;

        const char *m = psramBuffer + mStart;
        size_t mLen = mEnd - mStart;
        while (mLen > 0 && (*m == ' ' || *m == '\t' || *m == '\r')) { m++; mLen--; }
        while (mLen > 0 && (m[mLen - 1] == ' ' || m[mLen - 1] == '\t' || m[mLen - 1] == '\r')) mLen--;

        if (fastStrCaseEquals(m, mLen, "ENDBLOCK")) {
          blockEnd = mStart;
          i = cursor;
          foundEnd = true;
          break;
        }
      }

      if (!foundEnd) {
        i = totalLength;
      }

      if (blockEnd > blockStart) {
        typeTextInternal(blockStart, blockEnd - blockStart);
      }
      lastExecLen = 0;
      continue;
    }

    if (fastStrCaseStartsWith(line, lineLen, "REPEAT ", 7)) {
      int repeatCount = clampInt(atoi(line + 7), 1, 1000);
      if (lastExecLen > 0) {
        for (int r = 0; r < repeatCount; r++) {
          if (stopScriptFlag) break;
          executeDuckyCommandLineFast(lastExecBuf, lastExecLen, defaultDelay);
          lineCounter++;
          if (lineCounter % 16 == 0) vTaskDelay(1);
        }
      }
      continue;
    }

    if (executeDuckyCommandLineFast(line, lineLen, defaultDelay)) {
      lastExecLen = (lineLen < 127) ? lineLen : 127;
      memcpy(lastExecBuf, line, lastExecLen);
      lastExecBuf[lastExecLen] = '\0';
    }

    lineCounter++;
    if (lineCounter % 16 == 0) {
      vTaskDelay(1);
    }
  }

  scriptCurrentLine = 0;
  scriptProgressPercent = 0;
  scriptCurrentCommand = "";
}

bool queueJob(size_t length, bool isRawText) {
  if (length == 0 || length >= BUFFER_SIZE) return false;
  if (isWorkerBusy || isJobQueued) return false;

  DuckyJob job = {};
  job.type = isRawText ? JOB_RAW_TEXT : JOB_SCRIPT;
  job.length = length;
  job.fileName[0] = '\0';

  if (xQueueSend(jobQueue, &job, 0) == pdPASS) {
    isJobQueued = true;
    return true;
  }
  return false;
}

bool queueActionFileJob(const String &safeName) {
  if (safeName.isEmpty()) return false;
  if (isWorkerBusy || isJobQueued) return false;

  DuckyJob job = {};
  job.type = JOB_ACTION_FILE;
  job.length = 0;
  safeName.toCharArray(job.fileName, sizeof(job.fileName));

  if (xQueueSend(jobQueue, &job, 0) == pdPASS) {
    isJobQueued = true;
    return true;
  }
  return false;
}

int splitPipe(const String &line, String parts[], int maxParts) {
  if (maxParts <= 0) return 0;

  int count = 0;
  int start = 0;

  while (count < maxParts) {
    if (count == maxParts - 1) {
      parts[count++] = line.substring(start);
      break;
    }

    int sep = line.indexOf('|', start);
    if (sep < 0) {
      parts[count++] = line.substring(start);
      break;
    }

    parts[count++] = line.substring(start, sep);
    start = sep + 1;
  }

  for (int i = 0; i < count; i++) {
    parts[i].trim();
  }

  return count;
}

void delayWithStop(uint32_t ms) {
  if (ms == 0) return;

  uint32_t start = millis();
  while (!stopScriptFlag && (millis() - start < ms)) {
    delay(1);
  }
}

uint8_t parseActionMouseButtonToken(const String &token) {
  String lowered = token;
  lowered.toLowerCase();
  lowered.trim();

  if (lowered == "left") return MOUSE_LEFT;
  if (lowered == "right") return MOUSE_RIGHT;
  if (lowered == "middle") return MOUSE_MIDDLE;
  if (lowered == "backward" || lowered == "back") return MOUSE_BACKWARD;
  if (lowered == "forward") return MOUSE_FORWARD;

  int numeric = lowered.toInt();
  if (numeric >= 1 && numeric <= 31) return static_cast<uint8_t>(numeric);
  return MOUSE_LEFT;
}

uint8_t parseActionMouseActionToken(const String &token) {
  String lowered = token;
  lowered.toLowerCase();
  lowered.trim();

  if (lowered == "down") return MOUSE_ACTION_DOWN;
  if (lowered == "up") return MOUSE_ACTION_UP;
  if (lowered == "click") return MOUSE_ACTION_CLICK;

  int numeric = lowered.toInt();
  if (numeric == 1) return MOUSE_ACTION_DOWN;
  if (numeric == 2) return MOUSE_ACTION_UP;
  return MOUSE_ACTION_CLICK;
}

void replayMouseDelta(int dx, int dy) {
  int remX = dx;
  int remY = dy;

  while (!stopScriptFlag && (remX != 0 || remY != 0)) {
    int8_t stepX = clampInt8(remX, -120, 120);
    int8_t stepY = clampInt8(remY, -120, 120);
    if(Mouse) Mouse->move(stepX, stepY, 0, 0);
    remX -= stepX;
    remY -= stepY;

    if (remX != 0 || remY != 0) {
      delay(1);
    }
  }
}

bool runActionFile(const String &safeName) {
  String filePath = actionPathFromName(safeName);
  if (!LittleFS.exists(filePath)) return false;

  File file = LittleFS.open(filePath, "r");
  if (!file) return false;

  while (file.available() && !stopScriptFlag) {
    String line = file.readStringUntil('\n');
    line.trim();

    if (line.isEmpty() || line.startsWith("#")) continue;

    String parts[8];
    int count = splitPipe(line, parts, 8);
    if (count < 2) continue;

    int delayMs = clampInt(parts[0].toInt(), 0, 60000);
    if (delayMs > 0) delayWithStop(static_cast<uint32_t>(delayMs));
    if (stopScriptFlag) break;

    String event = parts[1];
    event.toLowerCase();

    if (event == "key_tap" && count >= 4) {
      uint8_t code = static_cast<uint8_t>(clampInt(parts[2].toInt(), 0, 255));
      uint16_t hold = static_cast<uint16_t>(clampInt(parts[3].toInt(), 10, 300));
      keyboardTap(code, hold);
    } else if (event == "key_down" && count >= 3) {
      uint8_t code = static_cast<uint8_t>(clampInt(parts[2].toInt(), 0, 255));
      if(Keyboard) Keyboard->press(code);
    } else if (event == "key_up" && count >= 3) {
      uint8_t code = static_cast<uint8_t>(clampInt(parts[2].toInt(), 0, 255));
      if(Keyboard) Keyboard->release(code);
    } else if (event == "key_release_all") {
      if(Keyboard) Keyboard->releaseAll();
    } else if (event == "combo" && count >= 5) {
      int flags = clampInt(parts[2].toInt(), 0, 15);
      uint8_t code = static_cast<uint8_t>(clampInt(parts[3].toInt(), 0, 255));
      uint16_t hold = static_cast<uint16_t>(clampInt(parts[4].toInt(), 10, 300));

      keyboardCombo((flags & 0x1) != 0, (flags & 0x2) != 0, (flags & 0x4) != 0, (flags & 0x8) != 0, code, hold);
    } else if (event == "mouse_move" && count >= 4) {
      int dx = clampInt(parts[2].toInt(), -4096, 4096);
      int dy = clampInt(parts[3].toInt(), -4096, 4096);
      replayMouseDelta(dx, dy);
    } else if (event == "mouse_scroll" && count >= 4) {
      int wheel = clampInt(parts[2].toInt(), -127, 127);
      int pan = clampInt(parts[3].toInt(), -127, 127);
      if(Mouse) Mouse->move(0, 0, static_cast<int8_t>(wheel), static_cast<int8_t>(pan));
    } else if (event == "mouse_button" && count >= 4) {
      uint8_t button = parseActionMouseButtonToken(parts[2]);
      uint8_t action = parseActionMouseActionToken(parts[3]);
      if (action == MOUSE_ACTION_DOWN) {
        if(Mouse) Mouse->press(button);
      } else if (action == MOUSE_ACTION_UP) {
        if(Mouse) Mouse->release(button);
      } else {
        if(Mouse) Mouse->click(button);
      }
    } else if (event == "consumer" && count >= 3) {
      uint16_t usage = static_cast<uint16_t>(clampInt(parts[2].toInt(), 0, 0xFFFF));
      if (usage == 0) {
        if(Consumer) Consumer->release();
      } else {
        if(Consumer) Consumer->press(usage);
        if(Consumer) Consumer->release();
      }
    }
  }

  file.close();
  if(Keyboard) Keyboard->releaseAll();
  if(Mouse) Mouse->release(MOUSE_ALL);
  if(Consumer) Consumer->release();
  return true;
}

void duckyWorkerTask(void *parameter) {
  (void)parameter;

  DuckyJob job;
  for (;;) {
    if (xQueueReceive(jobQueue, &job, portMAX_DELAY) == pdTRUE) {
      isJobQueued = false;
      isWorkerBusy = true;
      stopScriptFlag = false;

      setStatus(0, 0, 255); // Blue
      vTaskDelay(pdMS_TO_TICKS(80));

      if (job.type == JOB_RAW_TEXT) {
        typeTextInternal(0, job.length);
      } else if (job.type == JOB_ACTION_FILE) {
        String safeName = String(job.fileName);
        runActionFile(safeName);
      } else {
        parseAndExecuteInternal(job.length);
      }

      if(Keyboard) Keyboard->releaseAll();
      if(Mouse) Mouse->release(MOUSE_ALL);
      if(Consumer) Consumer->release();
      setStatus(255, 255, 255); // White
      vTaskDelay(pdMS_TO_TICKS(120));
      setStatus(0, 255, 0); // Green

      stopScriptFlag = false;
      isWorkerBusy = false;
    }
  }
}

bool isKvmSourceAllowed(const IPAddress &ip) {
  if (kvmAllowedIp.isEmpty()) return true;

  IPAddress allowed;
  if (!allowed.fromString(kvmAllowedIp)) return true;
  return ip == allowed;
}

void updateKvmUdpBinding() {
  if (!kvmUdpMutex) return;
  if (xSemaphoreTake(kvmUdpMutex, pdMS_TO_TICKS(150)) != pdTRUE) return;

  if (!kvmEnabled) {
    if (kvmUdpBound) {
      kvmUdp.stop();
      kvmUdpBound = false;
      kvmBoundPort = 0;
      kvmHasSequence = false;
    }
    kvmBindError = "";
    xSemaphoreGive(kvmUdpMutex);
    return;
  }

  uint16_t desiredPort = static_cast<uint16_t>(clampInt(kvmPort, 1, 65535));
  if (kvmUdpBound && kvmBoundPort == desiredPort) {
    kvmBindError = "";
    xSemaphoreGive(kvmUdpMutex);
    return;
  }

  if (kvmUdpBound) {
    kvmUdp.stop();
    kvmUdpBound = false;
    kvmBoundPort = 0;
  }

  bool bound = kvmUdp.begin(desiredPort);
  kvmUdpBound = bound;

  if (bound) {
    kvmBoundPort = desiredPort;
    kvmHasSequence = false;
    kvmBindError = "";
  } else {
    kvmBoundPort = 0;
    kvmBindError = "udp-begin-failed";
  }

  xSemaphoreGive(kvmUdpMutex);
}

void kvmNetworkTask(void *parameter) {
  (void)parameter;

  uint8_t packetBuffer[KVM_PACKET_SIZE];

  for (;;) {
    bool enabled = false;
    bool bound = false;
    int packetSize = 0;
    int bytesRead = 0;
    IPAddress sourceIp = IPAddress(0, 0, 0, 0);

    if (kvmUdpMutex && xSemaphoreTake(kvmUdpMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
      enabled = kvmEnabled;
      bound = kvmUdpBound;

      if (enabled && bound) {
        packetSize = kvmUdp.parsePacket();
        if (packetSize > 0) {
          sourceIp = kvmUdp.remoteIP();
          bytesRead = kvmUdp.read(packetBuffer, KVM_PACKET_SIZE);
          while (kvmUdp.available() > 0) {
            kvmUdp.read();
          }
        }
      }

      xSemaphoreGive(kvmUdpMutex);
    }

    if (!enabled) {
      vTaskDelay(pdMS_TO_TICKS(120));
      continue;
    }

    if (!bound) {
      updateKvmUdpBinding();
      vTaskDelay(pdMS_TO_TICKS(80));
      continue;
    }

    uint32_t nowMs = millis();

    if (packetSize <= 0) {
      if (kvmLastPacketMs > 0 && (nowMs - kvmLastPacketMs > 1500)) {
        queueHidReleaseAll();
        kvmLastPacketMs = 0;
        kvmHasSequence = false;
      }
      vTaskDelay(pdMS_TO_TICKS(2));
      continue;
    }

    if (packetSize != static_cast<int>(KVM_PACKET_SIZE) || bytesRead != static_cast<int>(KVM_PACKET_SIZE)) {
      kvmPacketsDropped++;
      continue;
    }

    KvmPacket packet = {};
    memcpy(&packet, packetBuffer, sizeof(packet));

    if (packet.magic != KVM_PACKET_MAGIC) {
      kvmPacketsDropped++;
      continue;
    }

    if (!isKvmSourceAllowed(sourceIp)) {
      kvmPacketsDropped++;
      continue;
    }

    if (kvmHasSequence) {
      int32_t delta = static_cast<int32_t>(packet.sequence - kvmLastSequence);
      if (delta <= 0) {
        if ((nowMs - kvmLastPacketMs > 1500) || (packet.sequence <= 10 && kvmLastSequence > 50)) {
          kvmHasSequence = false;
        } else {
          kvmPacketsDropped++;
          continue;
        }
      }
    }

    kvmHasSequence = true;
    kvmLastSequence = packet.sequence;
    kvmLastSourceIp = sourceIp;
    kvmLastPacketMs = nowMs;
    kvmPacketsRx++;

    captureKvmBridgeEvent(packet, nowMs);

    if (isWorkerBusy || isJobQueued) {
      kvmPacketsDropped++;
      continue;
    }

    HidRealtimeEvent event = {};
    bool valid = true;

    if (packet.type == KVM_EVENT_MOUSE) {
      event.type = HidRealtimeType::KvmMouseState;
      event.kvmButtons = packet.payload.mouse.buttons & MOUSE_ALL;
      event.kvmDx = packet.payload.mouse.dx;
      event.kvmDy = packet.payload.mouse.dy;
      event.kvmWheel = packet.payload.mouse.wheel;
      event.kvmPan = packet.payload.mouse.pan;
    } else if (packet.type == KVM_EVENT_KEYBOARD) {
      event.type = HidRealtimeType::KvmKeyboardState;
      event.kvmModifiers = packet.payload.keyboard.modifiers;
      memcpy(event.kvmKeys, packet.payload.keyboard.keycodes, sizeof(event.kvmKeys));
    } else if (packet.type == KVM_EVENT_CONSUMER) {
      event.type = HidRealtimeType::ConsumerControl;
      event.consumerUsage = packet.payload.consumer.usageId;
    } else {
      valid = false;
    }

    if (!valid) {
      kvmPacketsDropped++;
      continue;
    }

    bool queued = queueHidEvent(event, 0);
    if (!queued) {
      kvmPacketsDropped++;
      continue;
    }

    kvmPacketsEnqueued++;
  }
}

String jsonStatus() {
  bool staConnected = (WiFi.status() == WL_CONNECTED);
  String staIp = staConnected ? WiFi.localIP().toString() : "";
  String apIp = WiFi.softAPIP().toString();

  String safeCmd = scriptCurrentCommand;
  safeCmd.replace("\"", "\\\"");
  safeCmd.replace("\r", "");
  safeCmd.replace("\n", " ");

  String json = "{";
  json += "\"busy\":" + String(isWorkerBusy ? "true" : "false");
  json += ",\"queued\":" + String(isJobQueued ? "true" : "false");
  json += ",\"core_script\":1";
  json += ",\"core_hid\":0";
  json += ",\"sta_connected\":" + String(staConnected ? "true" : "false");
  json += ",\"sta_ip\":\"" + staIp + "\"";
  json += ",\"ap_ip\":\"" + apIp + "\"";
  json += ",\"sta_rssi\":" + String(staConnected ? WiFi.RSSI() : 0);
  json += ",\"mdns_host\":\"http://esp32-hid.local\"";
  json += ",\"line_current\":" + String(scriptCurrentLine);
  json += ",\"line_total\":" + String(scriptTotalLines);
  json += ",\"progress\":" + String(scriptProgressPercent);
  json += ",\"current_cmd\":\"" + safeCmd + "\"";
  json += ",\"msc_enabled\":" + String(usbMscEnabled ? "true" : "false");
  json += "}";
  return json;
}

void connectWiFi() {
  if (bleFidoEnabled) {
    WiFi.setSleep(WIFI_PS_MIN_MODEM);
  } else {
    WiFi.setSleep(false);
  }
  WiFi.setAutoReconnect(true);
  WiFi.setHostname("esp32-hid");

  bool staConnected = false;
  if (!sta_ssid.isEmpty()) {
    WiFi.mode(WIFI_AP_STA);
    WiFi.begin(sta_ssid.c_str(), sta_pass.c_str());

    uint32_t start = millis();
    while (millis() - start < 15000) {
      if (WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0)) {
        staConnected = true;
        break;
      }
      delay(200);
    }
  }

  if (staConnected) {
    int channel = WiFi.channel();
    if (channel < 1 || channel > 13) channel = 1;
    WiFi.softAP(ap_ssid.c_str(), ap_pass.c_str(), channel);
    Serial.printf("[WIFI] Connected to Router! STA IP: %s (Channel: %d)\n", WiFi.localIP().toString().c_str(), channel);
  } else {
    WiFi.mode(WIFI_AP);
    WiFi.softAP(ap_ssid.c_str(), ap_pass.c_str(), 1);
    Serial.println("[WIFI] Running in AP Only mode.");
  }

  Serial.printf("[WIFI] SoftAP IP: %s\n", WiFi.softAPIP().toString().c_str());

  if (MDNS.begin("esp32-hid")) {
    MDNS.addService("http", "tcp", 80);
    MDNS.addServiceTxt("http", "tcp", "path", "/");
    Serial.println("[MDNS] Started: http://esp32-hid.local");
  }
}

bool uiFilesPresent() {
  return (LittleFS.exists("/login.html") || LittleFS.exists("/login.html.gz")) &&
         (LittleFS.exists("/app.html") || LittleFS.exists("/app.html.gz")) &&
         (LittleFS.exists("/styles.css") || LittleFS.exists("/styles.css.gz")) &&
         (LittleFS.exists("/app.js") || LittleFS.exists("/app.js.gz"));
}

void handlePayloadUpload(
  AsyncWebServerRequest *request,
  uint8_t *data,
  size_t len,
  size_t index,
  size_t total,
  bool isRawText
) {
  constexpr uintptr_t STATE_OK = 0;
  constexpr uintptr_t STATE_REJECTED = 1;
  constexpr uintptr_t STATE_OVERFLOW = 2;

  if (!hasAccess(request)) return;

  if (index == 0) {
    uint32_t now = millis();
    if (isInputLocked && (now - inputLockTimestamp > 5000)) {
      isInputLocked = false;
    }

    if (isWorkerBusy || isJobQueued || isInputLocked) {
      request->_tempObject = reinterpret_cast<void *>(STATE_REJECTED);
    } else {
      request->_tempObject = reinterpret_cast<void *>(STATE_OK);
      bufferIndex = 0;
      isInputLocked = true;
      inputLockTimestamp = now;
      request->onDisconnect([]() {
        isInputLocked = false;
      });
    }
  }

  uintptr_t state = reinterpret_cast<uintptr_t>(request->_tempObject);
  if (state == STATE_REJECTED) {
    if (index + len == total) {
      request->send(503, "application/json", "{\"error\":\"busy\"}");
    }
    return;
  }

  if (!isInputLocked) {
    if (index + len == total) {
      request->send(503, "application/json", "{\"error\":\"input-locked\"}");
    }
    return;
  }

  if (bufferIndex + len > BUFFER_SIZE - 1) {
    request->_tempObject = reinterpret_cast<void *>(STATE_OVERFLOW);
    bufferIndex = 0;
    isInputLocked = false;
    if (index + len == total) {
      request->send(413, "application/json", "{\"error\":\"payload-too-large\"}");
    }
    return;
  }

  memcpy(psramBuffer + bufferIndex, data, len);
  bufferIndex += len;

  if (index + len == total) {
    state = reinterpret_cast<uintptr_t>(request->_tempObject);
    if (state == STATE_OVERFLOW) {
      request->send(413, "application/json", "{\"error\":\"payload-too-large\"}");
      return;
    }

    psramBuffer[bufferIndex] = '\0';
    bool queued = queueJob(bufferIndex, isRawText);

    isInputLocked = false;
    if (queued) {
      request->send(200, "application/json", "{\"queued\":true}");
    } else {
      request->send(503, "application/json", "{\"error\":\"queue-failed\"}");
    }
  }
}

uint8_t parseMouseButton(const String &name) {
  String lowered = name;
  lowered.toLowerCase();
  lowered.trim();

  if (lowered == "left") return MOUSE_LEFT;
  if (lowered == "right") return MOUSE_RIGHT;
  if (lowered == "middle") return MOUSE_MIDDLE;
  if (lowered == "backward" || lowered == "back") return MOUSE_BACKWARD;
  if (lowered == "forward") return MOUSE_FORWARD;
  return 0;
}

void registerRoutes() {
  server.serveStatic("/styles.css", LittleFS, "/styles.css").setCacheControl("no-cache, must-revalidate");
  server.serveStatic("/app.js", LittleFS, "/app.js").setCacheControl("no-cache, must-revalidate");

  server.on("/favicon.ico", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (LittleFS.exists("/favicon.ico")) {
      request->send(LittleFS, "/favicon.ico", "image/x-icon");
      return;
    }

    request->send(204, "text/plain", "");
  });

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!uiFilesPresent()) {
      request->send(500, "text/plain", "Web UI files missing in LittleFS. Run: pio run -t uploadfs");
      return;
    }

    if (isSessionAuthorized(request, false)) {
      request->redirect("/app");
    } else {
      request->redirect("/login");
    }
  });

  server.on("/login", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!uiFilesPresent()) {
      request->send(500, "text/plain", "Web UI files missing in LittleFS. Run: pio run -t uploadfs");
      return;
    }

    AsyncWebServerResponse *response = request->beginResponse(LittleFS, "/login.html", "text/html");
    response->addHeader("Cache-Control", "no-cache, must-revalidate");
    request->send(response);
  });

  server.on("/app", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!uiFilesPresent()) {
      request->send(500, "text/plain", "Web UI files missing in LittleFS. Run: pio run -t uploadfs");
      return;
    }

    if (!isSessionAuthorized(request)) {
      request->redirect("/login");
      return;
    }

    AsyncWebServerResponse *response = request->beginResponse(LittleFS, "/app.html", "text/html");
    response->addHeader("Cache-Control", "no-cache, must-revalidate");
    request->send(response);
  });

  server.on("/api/login_status", HTTP_GET, [](AsyncWebServerRequest *request) {
    bool loggedIn = isSessionAuthorized(request, false);
    bool locked = false;
    uint32_t retryMs = 0;
    IPAddress remoteIp = extractClientIp(request);

    if (!activeSessionToken.isEmpty() && !isSessionExpired() && !loggedIn) {
      locked = true;
    }

    bool rateLimited = isLoginBlocked(remoteIp, retryMs);

    String json = "{";
    json += "\"loggedIn\":" + String(loggedIn ? "true" : "false");
    json += ",\"locked\":" + String(locked ? "true" : "false");
    json += ",\"rate_limited\":" + String(rateLimited ? "true" : "false");
    json += ",\"retry_after_ms\":" + String(retryMs);
    json += ",\"proxy_auth_enabled\":" + String(proxyAuthEnabled ? "true" : "false");
    json += "}";
    request->send(200, "application/json", json);
  });

  server.on(
    "/api/login",
    HTTP_POST,
    [](AsyncWebServerRequest *request) {},
    nullptr,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
      if (index == 0) {
        request->_tempObject = new String();
        request->onDisconnect([request]() {
          if (request->_tempObject) {
            delete reinterpret_cast<String *>(request->_tempObject);
            request->_tempObject = nullptr;
          }
        });
      }

      String *body = reinterpret_cast<String *>(request->_tempObject);
      if (!body) {
        request->send(500, "application/json", "{\"error\":\"alloc-failed\"}");
        return;
      }

      for (size_t i = 0; i < len; i++) {
        body->concat(static_cast<char>(data[i]));
      }

      if (index + len == total) {
        DynamicJsonDocument doc(512);
        DeserializationError err = deserializeJson(doc, *body);

        delete body;
        request->_tempObject = nullptr;

        if (err) {
          request->send(400, "application/json", "{\"error\":\"invalid-json\"}");
          return;
        }

        IPAddress remoteIp = extractClientIp(request);
        uint32_t retryMs = 0;
        if (isLoginBlocked(remoteIp, retryMs)) {
          request->send(429, "application/json", "{\"error\":\"rate-limited\",\"retry_after_ms\":" + String(retryMs) + "}");
          return;
        }

        String user = doc["user"] | "";
        String pass = doc["pass"] | "";

        if (user != admin_user || pass != admin_pass) {
          recordLoginFailure(remoteIp);
          request->send(401, "application/json", "{\"error\":\"invalid-credentials\"}");
          return;
        }

        clearLoginFailures(remoteIp);
        activeSessionToken = generateSessionToken();
        activeSessionIp = remoteIp;
        activeSessionLastSeen = millis();

        AsyncWebServerResponse *response = request->beginResponse(200, "application/json", "{\"ok\":true}");
        response->addHeader("Set-Cookie", buildSessionCookie(SESSION_TTL_MS / 1000));
        request->send(response);
      }
    }
  );

  server.on("/api/logout", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!requireAuth(request)) return;

    clearSession();
    AsyncWebServerResponse *response = request->beginResponse(200, "application/json", "{\"ok\":true}");
    response->addHeader("Set-Cookie", buildExpiredSessionCookie());
    request->send(response);
  });

  server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!requireAuth(request)) return;
    request->send(200, "application/json", jsonStatus());
  });

  server.on("/api/hid_release_all", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!requireAuth(request)) return;

    queueHidReleaseAll();
    request->send(200, "application/json", "{\"ok\":true}");
  });

  server.on("/api/kvm_status", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!requireAuth(request)) return;

    bool enabled = kvmEnabled;
    bool bound = kvmUdpBound;
    uint16_t configuredPort = kvmPort;
    uint16_t boundPort = kvmBoundPort;
    String allowedIp = kvmAllowedIp;
    String bindError = kvmBindError;
    uint32_t packetsRx = kvmPacketsRx;
    uint32_t packetsDropped = kvmPacketsDropped;
    uint32_t packetsEnqueued = kvmPacketsEnqueued;
    uint32_t lastSequence = kvmHasSequence ? kvmLastSequence : 0;
    IPAddress lastSourceIp = kvmLastSourceIp;
    uint32_t lastPacketMs = kvmLastPacketMs;

    if (kvmUdpMutex && xSemaphoreTake(kvmUdpMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
      enabled = kvmEnabled;
      bound = kvmUdpBound;
      configuredPort = kvmPort;
      boundPort = kvmBoundPort;
      allowedIp = kvmAllowedIp;
      bindError = kvmBindError;
      packetsRx = kvmPacketsRx;
      packetsDropped = kvmPacketsDropped;
      packetsEnqueued = kvmPacketsEnqueued;
      lastSequence = kvmHasSequence ? kvmLastSequence : 0;
      lastSourceIp = kvmLastSourceIp;
      lastPacketMs = kvmLastPacketMs;
      xSemaphoreGive(kvmUdpMutex);
    }

    String deviceIp = WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : WiFi.softAPIP().toString();
    uint32_t now = millis();
    uint32_t packetAgeMs = (lastPacketMs == 0) ? 0xFFFFFFFF : (now - lastPacketMs);

    String linkState = "disabled";
    bool connected = false;
    if (enabled) {
      if (!bound) {
        if (!bindError.isEmpty()) {
          linkState = "bind-failed";
        } else {
          linkState = "not-bound";
        }
      } else if (lastPacketMs == 0) {
        linkState = "waiting";
      } else if (packetAgeMs < 2500) {
        linkState = "connected";
        connected = true;
      } else {
        linkState = "stale";
      }
    }

    String json = "{";
    json += "\"enabled\":" + String(enabled ? "true" : "false");
    json += ",\"bound\":" + String(bound ? "true" : "false");
    json += ",\"connected\":" + String(connected ? "true" : "false");
    json += ",\"link_state\":\"" + linkState + "\"";
    json += ",\"port\":" + String(configuredPort);
    json += ",\"bound_port\":" + String(boundPort);
    json += ",\"allowed_ip\":\"" + allowedIp + "\"";
    json += ",\"bind_error\":\"" + bindError + "\"";
    json += ",\"packets_rx\":" + String(packetsRx);
    json += ",\"packets_dropped\":" + String(packetsDropped);
    json += ",\"packets_enqueued\":" + String(packetsEnqueued);
    json += ",\"last_sequence\":" + String(lastSequence);
    json += ",\"last_source_ip\":\"" + lastSourceIp.toString() + "\"";
    json += ",\"last_packet_ms\":" + String(lastPacketMs);
    json += ",\"packet_age_ms\":" + String(packetAgeMs);
    json += ",\"device_ip\":\"" + deviceIp + "\"";
    json += "}";

    request->send(200, "application/json", json);
  });

  server.on(
    "/api/kvm_config",
    HTTP_POST,
    [](AsyncWebServerRequest *request) {
      if (!requireAuth(request)) return;
    },
    nullptr,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
      if (!hasAccess(request)) return;

      if (index == 0) {
        request->_tempObject = new String();
        request->onDisconnect([request]() {
          if (request->_tempObject) {
            delete reinterpret_cast<String *>(request->_tempObject);
            request->_tempObject = nullptr;
          }
        });
      }
      String *body = reinterpret_cast<String *>(request->_tempObject);

      if (!body) {
        request->send(500, "application/json", "{\"error\":\"alloc-failed\"}");
        return;
      }

      for (size_t i = 0; i < len; i++) {
        body->concat(static_cast<char>(data[i]));
      }

      if (index + len == total) {
        DynamicJsonDocument doc(512);
        DeserializationError err = deserializeJson(doc, *body);

        delete body;
        request->_tempObject = nullptr;

        if (err) {
          request->send(400, "application/json", "{\"error\":\"invalid-json\"}");
          return;
        }

        if (doc.containsKey("enabled")) kvmEnabled = doc["enabled"].as<bool>();

        if (doc.containsKey("port")) {
          uint16_t parsedPort = kvmPort;
          if (parseUint16JsonValue(doc["port"], parsedPort)) {
            kvmPort = static_cast<uint16_t>(clampInt(parsedPort, 1, 65535));
          }
        }

        if (doc.containsKey("allowed_ip")) {
          String ip = doc["allowed_ip"].as<String>();
          kvmAllowedIp = normalizeOptionalIp(ip);
        }

        persistSettings();
        updateKvmUdpBinding();

        bool bound = kvmUdpBound;
        uint16_t boundPort = kvmBoundPort;
        String bindError = kvmBindError;
        if (kvmUdpMutex && xSemaphoreTake(kvmUdpMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
          bound = kvmUdpBound;
          boundPort = kvmBoundPort;
          bindError = kvmBindError;
          xSemaphoreGive(kvmUdpMutex);
        }

        String json = "{";
        json += "\"saved\":true";
        json += ",\"bound\":" + String(bound ? "true" : "false");
        json += ",\"bound_port\":" + String(boundPort);
        json += ",\"bind_error\":\"" + bindError + "\"";
        json += "}";

        request->send(200, "application/json", json);
      }
    }
  );

  server.on("/api/kvm_bridge_record_status", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!requireAuth(request)) return;

    bool enabled = false;
    size_t count = 0;
    uint32_t dropped = 0;
    uint32_t durationMs = 0;

    if (kvmBridgeRecordMutex && xSemaphoreTake(kvmBridgeRecordMutex, pdMS_TO_TICKS(40)) == pdTRUE) {
      enabled = kvmBridgeRecordEnabled;
      count = kvmBridgeRecordCount;
      dropped = kvmBridgeRecordDropped;
      if (enabled) {
        durationMs = millis() - kvmBridgeRecordStartMs;
      } else if (count > 0) {
        durationMs = kvmBridgeRecordEvents[count - 1].dtMs;
      }
      xSemaphoreGive(kvmBridgeRecordMutex);
    }

    String json = "{";
    json += "\"enabled\":" + String(enabled ? "true" : "false");
    json += ",\"count\":" + String(static_cast<uint32_t>(count));
    json += ",\"dropped\":" + String(dropped);
    json += ",\"capacity\":" + String(static_cast<uint32_t>(KVM_BRIDGE_RECORD_MAX_EVENTS));
    json += ",\"duration_ms\":" + String(durationMs);
    json += "}";

    request->send(200, "application/json", json);
  });

  server.on("/api/kvm_bridge_record_start", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!requireAuth(request)) return;
    if (!hasAccess(request)) return;

    if (kvmBridgeRecordMutex && xSemaphoreTake(kvmBridgeRecordMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
      kvmBridgeRecordEnabled = true;
      resetKvmBridgeRecordingLocked(millis());
      xSemaphoreGive(kvmBridgeRecordMutex);
    }

    request->send(200, "application/json", "{\"started\":true}");
  });

  server.on("/api/kvm_bridge_record_stop", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!requireAuth(request)) return;
    if (!hasAccess(request)) return;

    if (kvmBridgeRecordMutex && xSemaphoreTake(kvmBridgeRecordMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
      kvmBridgeRecordEnabled = false;
      xSemaphoreGive(kvmBridgeRecordMutex);
    }

    request->send(200, "application/json", "{\"stopped\":true}");
  });

  server.on("/api/kvm_bridge_record_clear", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!requireAuth(request)) return;
    if (!hasAccess(request)) return;

    if (kvmBridgeRecordMutex && xSemaphoreTake(kvmBridgeRecordMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
      resetKvmBridgeRecordingLocked(millis());
      xSemaphoreGive(kvmBridgeRecordMutex);
    }

    request->send(200, "application/json", "{\"cleared\":true}");
  });

  server.on("/api/kvm_bridge_record_export", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!requireAuth(request)) return;

    bool enabled = false;
    size_t count = 0;
    uint32_t dropped = 0;
    KvmBridgeRecordEvent *snapshot = nullptr;

    if (kvmBridgeRecordMutex && xSemaphoreTake(kvmBridgeRecordMutex, pdMS_TO_TICKS(120)) == pdTRUE) {
      enabled = kvmBridgeRecordEnabled;
      count = kvmBridgeRecordCount;
      dropped = kvmBridgeRecordDropped;

      if (count > 0) {
        size_t bytes = sizeof(KvmBridgeRecordEvent) * count;
        snapshot = static_cast<KvmBridgeRecordEvent *>(malloc(bytes));
        if (snapshot) {
          memcpy(snapshot, kvmBridgeRecordEvents, bytes);
        }
      }

      xSemaphoreGive(kvmBridgeRecordMutex);
    }

    if (count > 0 && !snapshot) {
      request->send(503, "application/json", "{\"error\":\"snapshot-failed\"}");
      return;
    }

    AsyncResponseStream *response = request->beginResponseStream("application/json");
    response->print("{\"enabled\":");
    response->print(enabled ? "true" : "false");
    response->print(",\"dropped\":");
    response->print(String(dropped));
    response->print(",\"count\":");
    response->print(String(static_cast<uint32_t>(count)));
    response->print(",\"events\":[");

    for (size_t i = 0; i < count; i++) {
      const KvmBridgeRecordEvent &ev = snapshot[i];
      if (i > 0) response->print(",");

      if (ev.type == KVM_EVENT_MOUSE) {
        response->print("{\"type\":\"mouse\",\"dt\":");
        response->print(String(ev.dtMs));
        response->print(",\"buttons\":");
        response->print(String(ev.buttons));
        response->print(",\"dx\":");
        response->print(String(ev.dx));
        response->print(",\"dy\":");
        response->print(String(ev.dy));
        response->print(",\"wheel\":");
        response->print(String(ev.wheel));
        response->print(",\"pan\":");
        response->print(String(ev.pan));
        response->print("}");
      } else if (ev.type == KVM_EVENT_KEYBOARD) {
        response->print("{\"type\":\"keyboard\",\"dt\":");
        response->print(String(ev.dtMs));
        response->print(",\"modifiers\":");
        response->print(String(ev.modifiers));
        response->print(",\"keys\":[");
        for (size_t k = 0; k < 6; k++) {
          if (k > 0) response->print(",");
          response->print(String(ev.keys[k]));
        }
        response->print("]}");
      } else if (ev.type == KVM_EVENT_CONSUMER) {
        response->print("{\"type\":\"consumer\",\"dt\":");
        response->print(String(ev.dtMs));
        response->print(",\"usage\":");
        response->print(String(ev.usageId));
        response->print("}");
      } else {
        response->print("{\"type\":\"unknown\",\"dt\":");
        response->print(String(ev.dtMs));
        response->print("}");
      }
    }

    response->print("]}");
    request->send(response);

    if (snapshot) {
      free(snapshot);
    }
  });

  server.on("/api/proxy_profile", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!requireAuth(request)) return;

    String json = "{";
    json += "\"proxy_auth_enabled\":" + String(proxyAuthEnabled ? "true" : "false");
    json += ",\"token_configured\":" + String(proxyAuthToken.length() >= 16 ? "true" : "false");
    json += ",\"required_header\":\"X-Proxy-Token\"";
    json += ",\"https_forward_header\":\"X-Forwarded-Proto=https\"";
    json += "}";
    request->send(200, "application/json", json);
  });

  server.on("/api/stop", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!requireAuth(request)) return;
    stopScriptFlag = true;
    request->send(200, "application/json", "{\"stopped\":true}");
  });

  server.on(
    "/api/run",
    HTTP_POST,
    [](AsyncWebServerRequest *request) {
      if (!requireAuth(request)) return;
    },
    nullptr,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
      handlePayloadUpload(request, data, len, index, total, false);
    }
  );

  server.on(
    "/api/live_text",
    HTTP_POST,
    [](AsyncWebServerRequest *request) {
      if (!requireAuth(request)) return;
    },
    nullptr,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
      handlePayloadUpload(request, data, len, index, total, true);
    }
  );

  server.on(
    "/api/kbd_event",
    HTTP_POST,
    [](AsyncWebServerRequest *request) {
      if (!requireAuth(request)) return;
    },
    nullptr,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
      if (!hasAccess(request)) return;
      if (index + len != total) return;

      if (isWorkerBusy || isJobQueued) {
        request->send(503, "application/json", "{\"error\":\"busy\"}");
        return;
      }

      DynamicJsonDocument doc(384);
      DeserializationError err = deserializeJson(doc, data, len);
      if (err) {
        request->send(400, "application/json", "{\"error\":\"invalid-json\"}");
        return;
      }

      String action = doc["action"] | "tap";
      action.toLowerCase();

      HidRealtimeEvent event = {};

      if (action == "release_all") {
        event.type = HidRealtimeType::KeyReleaseAll;
      } else {
        int code = doc["code"] | -1;
        if (code < 0 || code > 255) {
          request->send(400, "application/json", "{\"error\":\"invalid-key\"}");
          return;
        }

        event.keyCode = static_cast<uint8_t>(code);
        event.holdMs = clampInt(doc["hold"] | 30, 10, 300);

        if (action == "down") {
          event.type = HidRealtimeType::KeyDown;
        } else if (action == "up") {
          event.type = HidRealtimeType::KeyUp;
        } else {
          event.type = HidRealtimeType::KeyTap;
        }
      }

      bool queued = queueHidEvent(event, pdMS_TO_TICKS(20));
      if (!queued) {
        request->send(503, "application/json", "{\"error\":\"hid-queue-full\"}");
        return;
      }

      request->send(200, "application/json", "{\"ok\":true}");
    }
  );

  server.on(
    "/api/live_key",
    HTTP_POST,
    [](AsyncWebServerRequest *request) {
      if (!requireAuth(request)) return;
    },
    nullptr,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
      if (!hasAccess(request)) return;
      if (index + len != total) return;

      if (isWorkerBusy || isJobQueued) {
        request->send(503, "application/json", "{\"error\":\"busy\"}");
        return;
      }

      DynamicJsonDocument doc(256);
      DeserializationError err = deserializeJson(doc, data, len);
      if (err) {
        request->send(400, "application/json", "{\"error\":\"invalid-json\"}");
        return;
      }

      int code = doc["code"] | -1;
      if (code < 0 || code > 255) {
        request->send(400, "application/json", "{\"error\":\"invalid-key\"}");
        return;
      }

      HidRealtimeEvent event = {};
      event.type = HidRealtimeType::KeyTap;
      event.keyCode = static_cast<uint8_t>(code);
      event.holdMs = clampInt(doc["hold"] | 35, 10, 300);

      bool queued = queueHidEvent(event, pdMS_TO_TICKS(20));
      if (!queued) {
        request->send(503, "application/json", "{\"error\":\"hid-queue-full\"}");
        return;
      }

      request->send(200, "application/json", "{\"ok\":true}");
    }
  );

  server.on(
    "/api/live_combo",
    HTTP_POST,
    [](AsyncWebServerRequest *request) {
      if (!requireAuth(request)) return;
    },
    nullptr,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
      if (!hasAccess(request)) return;
      if (index + len != total) return;

      if (isWorkerBusy || isJobQueued) {
        request->send(503, "application/json", "{\"error\":\"busy\"}");
        return;
      }

      DynamicJsonDocument doc(320);
      DeserializationError err = deserializeJson(doc, data, len);
      if (err) {
        request->send(400, "application/json", "{\"error\":\"invalid-json\"}");
        return;
      }

      uint8_t keyCode = 0;
      if (doc.containsKey("code")) {
        int code = doc["code"] | -1;
        if (code >= 0 && code <= 255) keyCode = static_cast<uint8_t>(code);
      } else if (doc.containsKey("char")) {
        String ch = doc["char"] | "";
        if (!ch.isEmpty()) keyCode = static_cast<uint8_t>(ch[0]);
      }

      if (keyCode == 0) {
        request->send(400, "application/json", "{\"error\":\"invalid-key\"}");
        return;
      }

      HidRealtimeEvent event = {};
      event.type = HidRealtimeType::Combo;
      event.keyCode = keyCode;
      event.ctrl = doc["ctrl"] | false;
      event.alt = doc["alt"] | false;
      event.shift = doc["shift"] | false;
      event.gui = doc["gui"] | false;
      event.holdMs = clampInt(doc["hold"] | 45, 10, 300);

      bool queued = queueHidEvent(event, pdMS_TO_TICKS(20));
      if (!queued) {
        request->send(503, "application/json", "{\"error\":\"hid-queue-full\"}");
        return;
      }

      request->send(200, "application/json", "{\"ok\":true}");
    }
  );

  server.on(
    "/api/mouse_move",
    HTTP_POST,
    [](AsyncWebServerRequest *request) {
      if (!requireAuth(request)) return;
    },
    nullptr,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
      if (!hasAccess(request)) return;
      if (index + len != total) return;

      if (isWorkerBusy || isJobQueued) {
        request->send(503, "application/json", "{\"error\":\"busy\"}");
        return;
      }

      DynamicJsonDocument doc(256);
      DeserializationError err = deserializeJson(doc, data, len);
      if (err) {
        request->send(400, "application/json", "{\"error\":\"invalid-json\"}");
        return;
      }

      int dx = doc["dx"] | 0;
      int dy = doc["dy"] | 0;

      HidRealtimeEvent event = {};
      event.type = HidRealtimeType::MouseMove;
      event.dx = clampInt8(dx, -50, 50);
      event.dy = clampInt8(dy, -50, 50);

      bool queued = queueHidEvent(event, pdMS_TO_TICKS(20));
      if (!queued) {
        request->send(503, "application/json", "{\"error\":\"hid-queue-full\"}");
        return;
      }

      request->send(200, "application/json", "{\"ok\":true}");
    }
  );

  server.on(
    "/api/mouse_move_abs",
    HTTP_POST,
    [](AsyncWebServerRequest *request) {
      if (!requireAuth(request)) return;
    },
    nullptr,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
      if (!hasAccess(request)) return;
      if (index + len != total) return;

      if (isWorkerBusy || isJobQueued) {
        request->send(503, "application/json", "{\"error\":\"busy\"}");
        return;
      }

      DynamicJsonDocument doc(256);
      DeserializationError err = deserializeJson(doc, data, len);
      if (err) {
        request->send(400, "application/json", "{\"error\":\"invalid-json\"}");
        return;
      }

      float x = doc["x"] | 50.0f;
      float y = doc["y"] | 50.0f;
      String btnStr = doc["button"] | "";
      String actStr = doc["action"] | "click";

      uint8_t btn = parseMouseButton(btnStr);
      uint8_t act = MOUSE_ACTION_CLICK;
      if (actStr == "down") act = MOUSE_ACTION_DOWN;
      else if (actStr == "up") act = MOUSE_ACTION_UP;

      mouseMoveAbsolute(x, y, btn, act);
      request->send(200, "application/json", "{\"ok\":true}");
    }
  );

  server.on(
    "/api/mouse_scroll",
    HTTP_POST,
    [](AsyncWebServerRequest *request) {
      if (!requireAuth(request)) return;
    },
    nullptr,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
      if (!hasAccess(request)) return;
      if (index + len != total) return;

      if (isWorkerBusy || isJobQueued) {
        request->send(503, "application/json", "{\"error\":\"busy\"}");
        return;
      }

      DynamicJsonDocument doc(256);
      DeserializationError err = deserializeJson(doc, data, len);
      if (err) {
        request->send(400, "application/json", "{\"error\":\"invalid-json\"}");
        return;
      }

      int wheel = doc["wheel"] | 0;
      int pan = doc["pan"] | 0;

      HidRealtimeEvent event = {};
      event.type = HidRealtimeType::MouseScroll;
      event.wheel = clampInt8(wheel, -20, 20);
      event.pan = clampInt8(pan, -20, 20);

      bool queued = queueHidEvent(event, pdMS_TO_TICKS(20));
      if (!queued) {
        request->send(503, "application/json", "{\"error\":\"hid-queue-full\"}");
        return;
      }

      request->send(200, "application/json", "{\"ok\":true}");
    }
  );

  server.on(
    "/api/mouse_button",
    HTTP_POST,
    [](AsyncWebServerRequest *request) {
      if (!requireAuth(request)) return;
    },
    nullptr,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
      if (!hasAccess(request)) return;
      if (index + len != total) return;

      if (isWorkerBusy || isJobQueued) {
        request->send(503, "application/json", "{\"error\":\"busy\"}");
        return;
      }

      DynamicJsonDocument doc(256);
      DeserializationError err = deserializeJson(doc, data, len);
      if (err) {
        request->send(400, "application/json", "{\"error\":\"invalid-json\"}");
        return;
      }

      String buttonName = doc["button"] | "left";
      String actionName = doc["action"] | "click";
      actionName.toLowerCase();

      uint8_t button = parseMouseButton(buttonName);
      if (button == 0) {
        request->send(400, "application/json", "{\"error\":\"invalid-button\"}");
        return;
      }

      uint8_t action = MOUSE_ACTION_CLICK;
      if (actionName == "down") action = MOUSE_ACTION_DOWN;
      else if (actionName == "up") action = MOUSE_ACTION_UP;

      HidRealtimeEvent event = {};
      event.type = HidRealtimeType::MouseButton;
      event.mouseButton = button;
      event.mouseAction = action;

      bool queued = queueHidEvent(event, pdMS_TO_TICKS(20));
      if (!queued) {
        request->send(503, "application/json", "{\"error\":\"hid-queue-full\"}");
        return;
      }

      request->send(200, "application/json", "{\"ok\":true}");
    }
  );

  server.on("/api/list", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!requireAuth(request)) return;

    File root = LittleFS.open(SCRIPTS_DIR);
    if (!root || !root.isDirectory()) {
      request->send(200, "application/json", "[]");
      return;
    }

    String json = "[";
    File f = root.openNextFile();
    while (f) {
      if (!f.isDirectory()) {
        String fullName = String(f.name());
        String shortName = fullName;
        if (shortName.startsWith("/scripts/")) shortName = shortName.substring(9);

        if (!json.endsWith("[")) json += ",";
        json += "{\"name\":\"" + shortName + "\",\"size\":" + String(f.size()) + "}";
      }
      f = root.openNextFile();
    }
    json += "]";

    request->send(200, "application/json", json);
  });

  server.on("/api/action_files", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!requireAuth(request)) return;

    File root = LittleFS.open(ACTIONS_DIR);
    if (!root || !root.isDirectory()) {
      request->send(200, "application/json", "[]");
      return;
    }

    String json = "[";
    File f = root.openNextFile();
    while (f) {
      if (!f.isDirectory()) {
        String fullName = String(f.name());
        String shortName = fullName;
        if (shortName.startsWith("/actions/")) shortName = shortName.substring(9);

        if (!json.endsWith("[")) json += ",";
        json += "{\"name\":\"" + shortName + "\",\"size\":" + String(f.size()) + "}";
      }
      f = root.openNextFile();
    }
    json += "]";

    request->send(200, "application/json", json);
  });

  server.on("/api/action_file/load", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!requireAuth(request)) return;

    if (!request->hasParam("name")) {
      request->send(400, "application/json", "{\"error\":\"missing-name\"}");
      return;
    }

    String safeName = sanitizeActionName(request->getParam("name")->value());
    if (safeName.isEmpty()) {
      request->send(400, "application/json", "{\"error\":\"invalid-name\"}");
      return;
    }

    String filePath = actionPathFromName(safeName);
    if (!LittleFS.exists(filePath)) {
      request->send(404, "application/json", "{\"error\":\"not-found\"}");
      return;
    }

    request->send(LittleFS, filePath, "text/plain");
  });

  server.on("/api/action_file/delete", HTTP_DELETE, [](AsyncWebServerRequest *request) {
    if (!requireAuth(request)) return;

    if (!request->hasParam("name")) {
      request->send(400, "application/json", "{\"error\":\"missing-name\"}");
      return;
    }

    String safeName = sanitizeActionName(request->getParam("name")->value());
    if (safeName.isEmpty()) {
      request->send(400, "application/json", "{\"error\":\"invalid-name\"}");
      return;
    }

    String filePath = actionPathFromName(safeName);
    bool removed = LittleFS.exists(filePath) ? LittleFS.remove(filePath) : false;

    request->send(200, "application/json", removed ? "{\"deleted\":true}" : "{\"deleted\":false}");
  });

  server.on("/api/action_file/run", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!requireAuth(request)) return;

    if (!request->hasParam("name")) {
      request->send(400, "application/json", "{\"error\":\"missing-name\"}");
      return;
    }

    String safeName = sanitizeActionName(request->getParam("name")->value());
    if (safeName.isEmpty()) {
      request->send(400, "application/json", "{\"error\":\"invalid-name\"}");
      return;
    }

    String filePath = actionPathFromName(safeName);
    if (!LittleFS.exists(filePath)) {
      request->send(404, "application/json", "{\"error\":\"not-found\"}");
      return;
    }

    bool queued = queueActionFileJob(safeName);
    if (!queued) {
      request->send(503, "application/json", "{\"error\":\"busy\"}");
      return;
    }

    request->send(200, "application/json", "{\"queued\":true}");
  });

  server.on(
    "/api/action_file/save",
    HTTP_POST,
    [](AsyncWebServerRequest *request) {
      if (!requireAuth(request)) return;
    },
    nullptr,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
      if (!hasAccess(request)) return;

      constexpr uintptr_t STATE_OK = 0;
      constexpr uintptr_t STATE_BAD_NAME = 1;
      constexpr uintptr_t STATE_FILE_OPEN_FAIL = 2;
      constexpr uintptr_t STATE_TOO_LARGE = 3;

      if (index == 0) {
        if (!request->hasParam("name")) {
          request->_tempObject = reinterpret_cast<void *>(STATE_BAD_NAME);
        } else if (total > ACTION_FILE_MAX_SIZE) {
          request->_tempObject = reinterpret_cast<void *>(STATE_TOO_LARGE);
        } else {
          String safeName = sanitizeActionName(request->getParam("name")->value());
          if (safeName.isEmpty()) {
            request->_tempObject = reinterpret_cast<void *>(STATE_BAD_NAME);
          } else {
            String filePath = actionPathFromName(safeName);
            request->_tempFile = LittleFS.open(filePath, "w");
            if (!request->_tempFile) {
              request->_tempObject = reinterpret_cast<void *>(STATE_FILE_OPEN_FAIL);
            } else {
              request->_tempObject = reinterpret_cast<void *>(STATE_OK);
            }
          }
        }
      }

      uintptr_t state = reinterpret_cast<uintptr_t>(request->_tempObject);
      if (state == STATE_OK && request->_tempFile) {
        request->_tempFile.write(data, len);
      }

      if (index + len == total) {
        if (request->_tempFile) request->_tempFile.close();

        if (state == STATE_BAD_NAME) {
          request->send(400, "application/json", "{\"error\":\"invalid-name\"}");
        } else if (state == STATE_TOO_LARGE) {
          request->send(413, "application/json", "{\"error\":\"file-too-large\"}");
        } else if (state == STATE_FILE_OPEN_FAIL) {
          request->send(500, "application/json", "{\"error\":\"save-failed\"}");
        } else {
          request->send(200, "application/json", "{\"saved\":true}");
        }
      }
    }
  );

  server.on("/api/load", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!requireAuth(request)) return;

    if (!request->hasParam("name")) {
      request->send(400, "application/json", "{\"error\":\"missing-name\"}");
      return;
    }

    String safeName = sanitizeScriptName(request->getParam("name")->value());
    if (safeName.isEmpty()) {
      request->send(400, "application/json", "{\"error\":\"invalid-name\"}");
      return;
    }

    String filePath = scriptPathFromName(safeName);
    if (!LittleFS.exists(filePath)) {
      request->send(404, "application/json", "{\"error\":\"not-found\"}");
      return;
    }

    request->send(LittleFS, filePath, "text/plain");
  });

  server.on("/api/delete", HTTP_DELETE, [](AsyncWebServerRequest *request) {
    if (!requireAuth(request)) return;

    if (!request->hasParam("name")) {
      request->send(400, "application/json", "{\"error\":\"missing-name\"}");
      return;
    }

    String safeName = sanitizeScriptName(request->getParam("name")->value());
    if (safeName.isEmpty()) {
      request->send(400, "application/json", "{\"error\":\"invalid-name\"}");
      return;
    }

    String filePath = scriptPathFromName(safeName);
    bool removed = LittleFS.exists(filePath) ? LittleFS.remove(filePath) : false;

    request->send(200, "application/json", removed ? "{\"deleted\":true}" : "{\"deleted\":false}");
  });

  server.on(
    "/api/edit",
    HTTP_POST,
    [](AsyncWebServerRequest *request) {
      if (!requireAuth(request)) return;
      request->send(200, "application/json", "{\"saved\":true}");
    },
    [](AsyncWebServerRequest *request, const String &filename, size_t index, uint8_t *data, size_t len, bool final) {
      if (!hasAccess(request)) return;

      String safeName = sanitizeScriptName(filename);
      if (safeName.isEmpty()) return;

      if (index == 0) {
        String filePath = scriptPathFromName(safeName);
        request->_tempFile = LittleFS.open(filePath, "w");
      }

      if (request->_tempFile) {
        request->_tempFile.write(data, len);
        if (final) request->_tempFile.close();
      }
    }
  );

  server.on("/api/get_settings", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!requireAuth(request)) return;

    DynamicJsonDocument doc(2560);
    doc["ap_ssid"] = ap_ssid;
    doc["ap_pass"] = ap_pass;
    doc["sta_ssid"] = sta_ssid;
    doc["sta_pass"] = sta_pass;
    doc["admin_user"] = admin_user;

    bool staConnected = (WiFi.status() == WL_CONNECTED);
    doc["sta_connected"] = staConnected;
    doc["sta_ip"] = staConnected ? WiFi.localIP().toString() : "";
    doc["sta_gateway"] = staConnected ? WiFi.gatewayIP().toString() : "";
    doc["sta_subnet"] = staConnected ? WiFi.subnetMask().toString() : "";
    doc["sta_rssi"] = staConnected ? WiFi.RSSI() : 0;
    doc["ap_ip"] = WiFi.softAPIP().toString();
    doc["ap_clients"] = WiFi.softAPgetStationNum();
    doc["mac_address"] = WiFi.macAddress();
    doc["mdns_host"] = "http://esp32-hid.local";

    doc["login_rate_limit"] = loginRateLimitEnabled;
    doc["proxy_auth_enabled"] = proxyAuthEnabled;
    doc["proxy_auth_token"] = proxyAuthToken;

    doc["kvm_enabled"] = kvmEnabled;
    doc["kvm_port"] = kvmPort;
    doc["kvm_allowed_ip"] = kvmAllowedIp;

    doc["usb_vid"] = usbVendorId;
    doc["usb_pid"] = usbProductId;
    doc["usb_vendor_name"] = usbVendorName;
    doc["usb_product_name"] = usbProductName;
    doc["usb_msc_enabled"] = usbMscEnabled;
    doc["usb_msc_label"] = usbMscVolumeLabel;
    doc["ble_fido_enabled"] = bleFidoEnabled;
    doc["fido_mode"] = fidoSecurityKeyMode;

    doc["delay"] = typeDelay;
    doc["burst_chars"] = burstChars;
    doc["burst_pause"] = burstPauseMs;
    doc["line_delay"] = lineDelayMs;
    doc["bright"] = ledBrightness;
    doc["kvm_mouse_smooth"] = kvmMouseSmoothness;

    const esp_app_desc_t *app_desc = esp_ota_get_app_description();
    if (app_desc) {
      doc["fw_version"] = app_desc->version;
      doc["build_date"] = String(app_desc->date) + " " + String(app_desc->time);
      doc["idf_version"] = app_desc->idf_ver;
      doc["project_name"] = app_desc->project_name;
    } else {
      doc["fw_version"] = FIRMWARE_VERSION;
      doc["build_date"] = BUILD_DATE;
    }

    // Read UI version from /version.json in LittleFS
    if (LittleFS.exists("/version.json")) {
      File vFile = LittleFS.open("/version.json", "r");
      if (vFile) {
        DynamicJsonDocument vDoc(512);
        if (deserializeJson(vDoc, vFile) == DeserializationError::Ok) {
          doc["ui_version"] = vDoc["version"].as<String>();
          doc["ui_build_date"] = vDoc["build_date"] | BUILD_DATE;
        }
        vFile.close();
      }
    }
    if (!doc.containsKey("ui_version")) {
      doc["ui_version"] = "2.4.0";
    }

    String json;
    serializeJson(doc, json);
    request->send(200, "application/json", json);
  });

  server.on(
    "/api/save_settings",
    HTTP_POST,
    [](AsyncWebServerRequest *request) {
      if (!requireAuth(request)) return;
    },
    nullptr,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
      if (!hasAccess(request)) return;

      if (index == 0) {
        request->_tempObject = new String();
        request->onDisconnect([request]() {
          if (request->_tempObject) {
            delete reinterpret_cast<String *>(request->_tempObject);
            request->_tempObject = nullptr;
          }
        });
      }
      String *body = reinterpret_cast<String *>(request->_tempObject);

      if (!body) {
        request->send(500, "application/json", "{\"error\":\"alloc-failed\"}");
        return;
      }

      for (size_t i = 0; i < len; i++) {
        body->concat(static_cast<char>(data[i]));
      }

      if (index + len == total) {
        bool usbIdentityChanged = false;
        bool wifiChanged = false;
        bool parsed = applySettingsJson(*body, usbIdentityChanged, wifiChanged);

        delete body;
        request->_tempObject = nullptr;

        if (!parsed) {
          request->send(400, "application/json", "{\"error\":\"invalid-json\"}");
          return;
        }

        String response = "{\"saved\":true,\"usb_restart_required\":";
        response += usbIdentityChanged ? "true" : "false";
        response += ",\"wifi_restart_required\":";
        response += wifiChanged ? "true" : "false";
        response += ",\"restart_required\":";
        response += (usbIdentityChanged || wifiChanged) ? "true" : "false";
        response += "}";

        request->send(200, "application/json", response);
      }
    }
  );

  server.on("/api/reboot", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!requireAuth(request)) return;
    request->send(200, "application/json", "{\"rebooting\":true}");
    indicateRebootAndRestart(400);
  });

  server.on("/api/logs", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!requireAuth(request)) return;
    DynamicJsonDocument doc(16384);
    JsonArray arr = doc.createNestedArray("logs");
    size_t start = (sysLogCount < MAX_LOG_LINES) ? 0 : sysLogHead;
    for (size_t i = 0; i < sysLogCount; i++) {
      size_t idx = (start + i) % MAX_LOG_LINES;
      arr.add(sysLogs[idx]);
    }
    doc["count"] = sysLogCount;
    doc["uptime_sec"] = millis() / 1000;
    doc["free_heap"] = ESP.getFreeHeap();
    String out;
    serializeJson(doc, out);
    request->send(200, "application/json", out);
  });

  server.on("/api/logs/download", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!requireAuth(request)) return;
    if (LittleFS.exists("/system.log")) {
      request->send(LittleFS, "/system.log", "text/plain", true);
    } else {
      String out = "";
      size_t start = (sysLogCount < MAX_LOG_LINES) ? 0 : sysLogHead;
      for (size_t i = 0; i < sysLogCount; i++) {
        size_t idx = (start + i) % MAX_LOG_LINES;
        out += sysLogs[idx] + "\r\n";
      }
      AsyncWebServerResponse *resp = request->beginResponse(200, "text/plain", out);
      resp->addHeader("Content-Disposition", "attachment; filename=\"system.log\"");
      request->send(resp);
    }
  });

  server.on("/api/logs/clear", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!requireAuth(request)) return;
    sysLogHead = 0;
    sysLogCount = 0;
    if (LittleFS.exists("/system.log")) {
      LittleFS.remove("/system.log");
    }
    if (LittleFS.exists("/system.log.old")) {
      LittleFS.remove("/system.log.old");
    }
    updateMscLogFile();
    request->send(200, "application/json", "{\"ok\":true}");
  });

  server.on(
    "/api/ota",
    HTTP_POST,
    [](AsyncWebServerRequest *request) {
      if (!requireAuth(request)) return;
      bool shouldReboot = !Update.hasError();
      if (shouldReboot) {
        logSystem("[OTA] Binary flash successful! Initiating restart in 500ms...");
      } else {
        logSystem("[OTA] ERROR: Binary flash failed!");
      }
      AsyncWebServerResponse *response = request->beginResponse(
        shouldReboot ? 200 : 500,
        "application/json",
        shouldReboot ? "{\"ok\":true,\"rebooting\":true}" : "{\"error\":\"ota-failed\"}"
      );
      response->addHeader("Connection", "close");
      request->send(response);
      if (shouldReboot) {
        indicateRebootAndRestart(900);
      }
    },
    [](AsyncWebServerRequest *request, const String &filename, size_t index, uint8_t *data, size_t len, bool final) {
      if (!hasAccess(request)) return;

      if (index == 0) {
        Update.abort(); // Clear any stale state
        
        int cmd = U_FLASH;
        String typeStr = "Firmware";
        String lowerName = filename;
        lowerName.toLowerCase();

        bool isFsParam = false;
        if (request->hasParam("type")) {
          String type = request->getParam("type")->value();
          type.toLowerCase();
          if (type == "fs" || type == "littlefs" || type == "spiffs") {
            isFsParam = true;
          }
        }

        // Auto-detect partition type:
        // 1. Explicit parameter
        // 2. Filename contains littlefs or spiffs
        // 3. Or first byte is NOT 0xE9 (ESP32 app magic byte)
        if (isFsParam || lowerName.indexOf("littlefs") >= 0 || lowerName.indexOf("spiffs") >= 0 || (len > 0 && data[0] != 0xE9)) {
          cmd = U_SPIFFS;
          typeStr = "LittleFS / Web UI";
          LittleFS.end(); // Unmount LittleFS cleanly to release all flash write locks!
        } else {
          cmd = U_FLASH;
          typeStr = "Firmware";
        }

        logSystem("[OTA] Starting " + typeStr + " upload: " + filename);
        if (!Update.begin(UPDATE_SIZE_UNKNOWN, cmd)) {
          Update.printError(Serial);
          logSystem("[OTA] Update.begin failed: " + String(Update.errorString()));
        }
      }

      if (!Update.hasError()) {
        if (Update.write(data, len) != len) {
          Update.printError(Serial);
          logSystem("[OTA] Update.write error at index " + String(index) + ": " + String(Update.errorString()));
        }
        yield(); // Yield CPU so network TCP stack handles incoming packets without buffer congestion
      }

      if (final) {
        if (!Update.end(true)) {
          Update.printError(Serial);
          logSystem("[OTA] Update.end failed: " + String(Update.errorString()));
        } else {
          logSystem("[OTA] Flash verified (" + String(index + len) + " bytes)");
        }
      }
    }
  );

  // --- ENCRYPTED VAULT & 2FA AUTHENTICATOR ROUTES ---
  server.on("/api/vault/status", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!requireAuth(request)) return;
    checkVaultAutoLock();
    DynamicJsonDocument doc(256);
    doc["initialized"] = isVaultInitialized();
    doc["unlocked"] = vaultUnlocked;
    doc["lock_timeout_sec"] = vaultUnlocked ? (VAULT_AUTO_LOCK_MS - (millis() - vaultLastActiveMs)) / 1000 : 0;

    int count = 0;
    if (vaultUnlocked) {
      DynamicJsonDocument itemsDoc(4096);
      if (deserializeJson(itemsDoc, vaultCachedJson) == DeserializationError::Ok && itemsDoc.is<JsonArray>()) {
        count = itemsDoc.as<JsonArray>().size();
      }
    }
    doc["entry_count"] = count;

    String out;
    serializeJson(doc, out);
    request->send(200, "application/json", out);
  });

  server.on(
    "/api/vault/setup",
    HTTP_POST,
    [](AsyncWebServerRequest *request) {
      if (!requireAuth(request)) return;
    },
    nullptr,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
      if (!hasAccess(request)) return;
      if (isVaultInitialized()) {
        request->send(400, "application/json", "{\"error\":\"Vault already initialized\"}");
        return;
      }
      DynamicJsonDocument doc(512);
      if (deserializeJson(doc, data, len) != DeserializationError::Ok || !doc.containsKey("password")) {
        request->send(400, "application/json", "{\"error\":\"Invalid payload\"}");
        return;
      }
      String pass = doc["password"].as<String>();
      if (pass.length() < 6) {
        request->send(400, "application/json", "{\"error\":\"Password too short (min 6 chars)\"}");
        return;
      }
      esp_fill_random(vaultSalt, sizeof(vaultSalt));
      Serial.printf("[VAULT] Setup: salt[0]=%02x salt[1]=%02x salt[2]=%02x\n", vaultSalt[0], vaultSalt[1], vaultSalt[2]);
      uint8_t derivedKey[VAULT_KEY_LEN];
      if (!pbkdf2DeriveKey(pass, vaultSalt, sizeof(vaultSalt), derivedKey)) {
        request->send(500, "application/json", "{\"error\":\"Key derivation failed\"}");
        return;
      }
      Serial.printf("[VAULT] Setup: key[0]=%02x key[1]=%02x\n", derivedKey[0], derivedKey[1]);
      if (!saveVaultEncrypted("[]", derivedKey)) {
        request->send(500, "application/json", "{\"error\":\"Failed to initialize vault file\"}");
        return;
      }
      memcpy(vaultKey, derivedKey, VAULT_KEY_LEN);
      vaultUnlocked = true;
      vaultLastActiveMs = millis();
      vaultCachedJson = "[]";
      Serial.println("[VAULT] Setup complete, vault unlocked");
      request->send(200, "application/json", "{\"ok\":true}");
    }
  );

  server.on(
    "/api/vault/unlock",
    HTTP_POST,
    [](AsyncWebServerRequest *request) {
      if (!requireAuth(request)) return;
    },
    nullptr,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
      if (!hasAccess(request)) return;
      DynamicJsonDocument doc(512);
      if (deserializeJson(doc, data, len) != DeserializationError::Ok || !doc.containsKey("password")) {
        request->send(400, "application/json", "{\"error\":\"Invalid payload\"}");
        return;
      }
      String pass = doc["password"].as<String>();
      uint64_t epoch = doc["epoch"] | 0ULL;
      if (unlockVaultWithPassword(pass, epoch)) {
        request->send(200, "application/json", "{\"ok\":true}");
      } else {
        request->send(403, "application/json", "{\"error\":\"Incorrect Master Password\"}");
      }
    }
  );

  server.on("/api/vault/reset", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!requireAuth(request)) return;
    lockVault();
    memset(vaultSalt, 0, sizeof(vaultSalt));
    if (LittleFS.exists(VAULT_FILE)) {
      LittleFS.remove(VAULT_FILE);
      Serial.println("[VAULT] Removed vault.enc from LittleFS");
    }
    Preferences prefs;
    if (prefs.begin("vault_nvs", false)) {
      prefs.clear();
      prefs.end();
      Serial.println("[VAULT] Cleared vault_nvs NVS namespace");
    }
    Serial.println("[VAULT] Reset complete");
    request->send(200, "application/json", "{\"ok\":true,\"reset\":true}");
  });

  server.on("/api/vault/lock", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!requireAuth(request)) return;
    lockVault();
    request->send(200, "application/json", "{\"ok\":true}");
  });

  server.on("/api/vault/entries", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!requireAuth(request)) return;
    checkVaultAutoLock();
    if (!vaultUnlocked) {
      request->send(403, "application/json", "{\"error\":\"Vault is locked\"}");
      return;
    }
    touchVaultActivity();

    if (request->hasParam("epoch")) {
      uint64_t clientEpoch = strtoull(request->getParam("epoch")->value().c_str(), nullptr, 10);
      if (clientEpoch > 1700000000ULL) {
        struct timeval tv;
        tv.tv_sec = static_cast<time_t>(clientEpoch);
        tv.tv_usec = 0;
        settimeofday(&tv, nullptr);
      }
    }

    DynamicJsonDocument itemsDoc(8192);
    DeserializationError dErr = deserializeJson(itemsDoc, vaultCachedJson);
    if (dErr != DeserializationError::Ok || !itemsDoc.is<JsonArray>()) {
      itemsDoc.to<JsonArray>();
    }

    time_t nowSec = time(nullptr);
    JsonArray arr = itemsDoc.as<JsonArray>();
    for (JsonObject obj : arr) {
      String type = obj["type"] | "totp";
      if (type == "totp" && obj.containsKey("secret")) {
        int period = obj["period"] | 30;
        int digits = obj["digits"] | 6;
        String secret = obj["secret"].as<String>();
        obj["otp"] = calculateTotp(secret, nowSec, period, digits);
        obj["remaining"] = period - (nowSec % period);
      }
    }

    String out;
    serializeJson(itemsDoc, out);
    request->send(200, "application/json", out);
  });

  server.on(
    "/api/vault/entry/save",
    HTTP_POST,
    [](AsyncWebServerRequest *request) {
      if (!requireAuth(request)) return;
    },
    nullptr,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
      if (!hasAccess(request)) return;
      checkVaultAutoLock();
      if (!vaultUnlocked) {
        request->send(403, "application/json", "{\"error\":\"Vault is locked\"}");
        return;
      }
      touchVaultActivity();

      DynamicJsonDocument doc(1024);
      if (deserializeJson(doc, data, len) != DeserializationError::Ok) {
        request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
        return;
      }

      DynamicJsonDocument itemsDoc(8192);
      deserializeJson(itemsDoc, vaultCachedJson);
      JsonArray arr = itemsDoc.as<JsonArray>();

      String id = doc["id"] | "";
      if (id.isEmpty()) {
        id = String(millis()) + String(random(100, 999));
        doc["id"] = id;
      }

      bool updated = false;
      for (size_t i = 0; i < arr.size(); i++) {
        if (arr[i]["id"] == id) {
          arr[i] = doc.as<JsonObject>();
          updated = true;
          break;
        }
      }
      if (!updated) {
        arr.add(doc.as<JsonObject>());
      }

      String updatedJson;
      serializeJson(itemsDoc, updatedJson);
      if (saveVaultEncrypted(updatedJson, vaultKey)) {
        vaultCachedJson = updatedJson;
        request->send(200, "application/json", "{\"ok\":true}");
      } else {
        request->send(500, "application/json", "{\"error\":\"Failed to save encrypted vault\"}");
      }
    }
  );

  server.on(
    "/api/vault/entry/delete",
    HTTP_POST,
    [](AsyncWebServerRequest *request) {
      if (!requireAuth(request)) return;
    },
    nullptr,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
      if (!hasAccess(request)) return;
      checkVaultAutoLock();
      if (!vaultUnlocked) {
        request->send(403, "application/json", "{\"error\":\"Vault is locked\"}");
        return;
      }
      touchVaultActivity();

      DynamicJsonDocument doc(256);
      if (deserializeJson(doc, data, len) != DeserializationError::Ok || !doc.containsKey("id")) {
        request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
        return;
      }

      String id = doc["id"].as<String>();
      DynamicJsonDocument itemsDoc(8192);
      deserializeJson(itemsDoc, vaultCachedJson);
      JsonArray arr = itemsDoc.as<JsonArray>();

      for (size_t i = 0; i < arr.size(); i++) {
        if (arr[i]["id"] == id) {
          arr.remove(i);
          break;
        }
      }

      String updatedJson;
      serializeJson(itemsDoc, updatedJson);
      if (saveVaultEncrypted(updatedJson, vaultKey)) {
        vaultCachedJson = updatedJson;
        request->send(200, "application/json", "{\"ok\":true}");
      } else {
        request->send(500, "application/json", "{\"error\":\"Failed to save encrypted vault\"}");
      }
    }
  );

  server.on(
    "/api/vault/type",
    HTTP_POST,
    [](AsyncWebServerRequest *request) {
      if (!requireAuth(request)) return;
    },
    nullptr,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
      if (!hasAccess(request)) return;
      checkVaultAutoLock();
      if (!vaultUnlocked) {
        request->send(403, "application/json", "{\"error\":\"Vault is locked\"}");
        return;
      }
      touchVaultActivity();

      DynamicJsonDocument doc(512);
      if (deserializeJson(doc, data, len) != DeserializationError::Ok || !doc.containsKey("id")) {
        request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
        return;
      }

      String id = doc["id"].as<String>();
      String action = doc["action"] | "otp";

      DynamicJsonDocument itemsDoc(8192);
      deserializeJson(itemsDoc, vaultCachedJson);
      JsonArray arr = itemsDoc.as<JsonArray>();

      JsonObject target;
      for (JsonObject obj : arr) {
        if (obj["id"] == id) {
          target = obj;
          break;
        }
      }

      if (target.isNull()) {
        request->send(404, "application/json", "{\"error\":\"Entry not found\"}");
        return;
      }

      time_t nowSec = time(nullptr);
      if (action == "otp" && target.containsKey("secret")) {
        int period = target["period"] | 30;
        int digits = target["digits"] | 6;
        String otp = calculateTotp(target["secret"].as<String>(), nowSec, period, digits);
        for (size_t c = 0; c < otp.length(); c++) {
          if(Keyboard) Keyboard->write(static_cast<uint8_t>(otp[c]));
          if (typeDelay > 0) delay(typeDelay);
        }
        if (doc["enter"] | true) {
          delay(10);
          keyboardTap(KEY_RETURN);
        }
      } else if (action == "user" && target.containsKey("user")) {
        String u = target["user"].as<String>();
        for (size_t c = 0; c < u.length(); c++) {
          if(Keyboard) Keyboard->write(static_cast<uint8_t>(u[c]));
          if (typeDelay > 0) delay(typeDelay);
        }
      } else if (action == "pass" && target.containsKey("pass")) {
        String p = target["pass"].as<String>();
        for (size_t c = 0; c < p.length(); c++) {
          if(Keyboard) Keyboard->write(static_cast<uint8_t>(p[c]));
          if (typeDelay > 0) delay(typeDelay);
        }
        if (doc["enter"] | false) {
          delay(10);
          keyboardTap(KEY_RETURN);
        }
      } else if (action == "both" && target.containsKey("user") && target.containsKey("pass")) {
        String u = target["user"].as<String>();
        for (size_t c = 0; c < u.length(); c++) {
          if(Keyboard) Keyboard->write(static_cast<uint8_t>(u[c]));
          if (typeDelay > 0) delay(typeDelay);
        }
        delay(20);
        keyboardTap(KEY_TAB);
        delay(20);
        String p = target["pass"].as<String>();
        for (size_t c = 0; c < p.length(); c++) {
          if(Keyboard) Keyboard->write(static_cast<uint8_t>(p[c]));
          if (typeDelay > 0) delay(typeDelay);
        }
        if (doc["enter"] | true) {
          delay(10);
          keyboardTap(KEY_RETURN);
        }
      }

      request->send(200, "application/json", "{\"ok\":true}");
    }
  );

  // --- FIDO2 / WEBAUTHN PASSKEY ROUTES ---
  server.on("/api/fido/status", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!requireAuth(request)) return;
    DynamicJsonDocument doc(16384);
    JsonArray arr = doc.createNestedArray("credentials");
    for (const auto &c : FidoStore::getAllCredentials()) {
      JsonObject obj = arr.createNestedObject();
      obj["rpId"] = c.rpId;
      obj["userName"] = c.userName;
      obj["userDisplayName"] = c.userDisplayName;
      obj["signCounter"] = c.signCounter;
      obj["createdAt"] = c.createdAt;
      
      String idHex = "";
      for (uint8_t b : c.credId) {
        char buf[3];
        snprintf(buf, sizeof(buf), "%02x", b);
        idHex += buf;
      }
      obj["credId"] = idHex;

      String pubXHex = "";
      for (uint8_t b : c.pubKeyX) {
        char buf[3];
        snprintf(buf, sizeof(buf), "%02x", b);
        pubXHex += buf;
      }
      obj["pubKeyX"] = pubXHex;

      String pubYHex = "";
      for (uint8_t b : c.pubKeyY) {
        char buf[3];
        snprintf(buf, sizeof(buf), "%02x", b);
        pubYHex += buf;
      }
      obj["pubKeyY"] = pubYHex;

      String userHex = "";
      for (uint8_t b : c.userId) {
        char buf[3];
        snprintf(buf, sizeof(buf), "%02x", b);
        userHex += buf;
      }
      obj["userId"] = userHex;
      obj["hasHmacSecret"] = !c.hmacSecretKey.empty();
      obj["credProtect"] = c.credProtect;
    }
    doc["waiting_for_touch"] = FIDO.isWaitingForTouch();
    doc["pending_rp"] = FIDO.getPendingRpId();
    doc["security_key_mode"] = fidoSecurityKeyMode;
    doc["pin_set"] = FidoStore::isPinSet();
    doc["pin_retries"] = FidoStore::getPinRetries();
    doc["credential_count"] = FidoStore::getAllCredentials().size();
    doc["emulate_uv"] = FidoStore::getEmulateUv();
    String out;
    serializeJson(doc, out);
    request->send(200, "application/json", out);
  });

  server.on("/api/vault/backup/export", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!requireAuth(request)) return;
    checkVaultAutoLock();
    if (!vaultUnlocked) {
      request->send(403, "application/json", "{\"error\":\"Please unlock vault first\"}");
      return;
    }
    touchVaultActivity();

    // Construct unified backup JSON
    DynamicJsonDocument backupDoc(32768);
    backupDoc["format"] = "ESP32_VAULT_BACKUP";
    backupDoc["version"] = "2.5.0";
    backupDoc["exported_at"] = (uint32_t)(time(nullptr));

    // 1. Software Vault Entries
    DynamicJsonDocument vaultItemsDoc(16384);
    if (deserializeJson(vaultItemsDoc, vaultCachedJson) == DeserializationError::Ok) {
      backupDoc["software_vault"] = vaultItemsDoc.as<JsonArray>();
    } else {
      backupDoc.createNestedArray("software_vault");
    }

    // 2. FIDO2 Resident Passkeys
    JsonArray fidoArr = backupDoc.createNestedArray("fido_credentials");
    for (const auto &c : FidoStore::getAllCredentials()) {
      JsonObject obj = fidoArr.createNestedObject();
      obj["rpId"] = c.rpId;
      obj["userName"] = c.userName;
      obj["userDisplayName"] = c.userDisplayName;
      obj["signCounter"] = c.signCounter;
      obj["createdAt"] = c.createdAt;

      auto toHexStr = [](const std::vector<uint8_t> &vec) {
        String h = "";
        for (uint8_t b : vec) {
          char buf[3];
          snprintf(buf, sizeof(buf), "%02x", b);
          h += buf;
        }
        return h;
      };

      obj["userId"] = toHexStr(c.userId);
      obj["credId"] = toHexStr(c.credId);
      obj["privKey"] = toHexStr(c.privKey);
      obj["pubKeyX"] = toHexStr(c.pubKeyX);
      obj["pubKeyY"] = toHexStr(c.pubKeyY);
      obj["hmacSecretKey"] = toHexStr(c.hmacSecretKey);
    }

    // 3. FIDO Settings
    JsonObject fidoSettings = backupDoc.createNestedObject("fido_settings");
    fidoSettings["pin_set"] = FidoStore::isPinSet();
    fidoSettings["emulate_uv"] = FidoStore::getEmulateUv();

    String jsonStr;
    serializeJson(backupDoc, jsonStr);

    size_t plainLen = jsonStr.length();
    uint8_t iv[VAULT_IV_LEN];
    uint8_t tag[VAULT_TAG_LEN];
    esp_fill_random(iv, sizeof(iv));

    uint8_t *cipherBuf = static_cast<uint8_t *>(malloc(plainLen + 1));
    if (!cipherBuf) {
      request->send(500, "application/json", "{\"error\":\"Memory allocation failed\"}");
      return;
    }

    bool ok = aes256GcmEncrypt(
      vaultKey,
      iv,
      sizeof(iv),
      reinterpret_cast<const uint8_t *>(jsonStr.c_str()),
      plainLen,
      cipherBuf,
      tag,
      sizeof(tag)
    );

    if (!ok) {
      free(cipherBuf);
      request->send(500, "application/json", "{\"error\":\"Encryption failed\"}");
      return;
    }

    VaultHeader hdr;
    memcpy(hdr.magic, "EVLT", 4);
    memcpy(hdr.salt, vaultSalt, VAULT_SALT_LEN);
    memcpy(hdr.iv, iv, VAULT_IV_LEN);
    memcpy(hdr.tag, tag, VAULT_TAG_LEN);
    hdr.ciphertextLen = static_cast<uint32_t>(plainLen);

    size_t totalBackupSize = sizeof(VaultHeader) + plainLen;
    uint8_t *outBlob = static_cast<uint8_t *>(malloc(totalBackupSize));
    if (!outBlob) {
      free(cipherBuf);
      request->send(500, "application/json", "{\"error\":\"Buffer allocation failed\"}");
      return;
    }

    memcpy(outBlob, &hdr, sizeof(VaultHeader));
    memcpy(outBlob + sizeof(VaultHeader), cipherBuf, plainLen);
    free(cipherBuf);

    AsyncWebServerResponse *response = request->beginResponse(
      "application/octet-stream",
      totalBackupSize,
      [outBlob, totalBackupSize](uint8_t *buffer, size_t maxLen, size_t index) -> size_t {
        size_t available = totalBackupSize - index;
        size_t toSend = (available > maxLen) ? maxLen : available;
        memcpy(buffer, outBlob + index, toSend);
        if (index + toSend >= totalBackupSize) {
          free(outBlob);
        }
        return toSend;
      }
    );
    response->addHeader("Content-Disposition", "attachment; filename=\"esp32_hardware_vault_backup.esp32vault\"");
    request->send(response);
  });

  server.on("/api/vault/backup/import", HTTP_POST, [](AsyncWebServerRequest *request) {}, nullptr,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
      if (!requireAuth(request)) return;
      DynamicJsonDocument doc(65536);
      if (deserializeJson(doc, data, len) != DeserializationError::Ok) {
        request->send(400, "application/json", "{\"error\":\"Invalid payload\"}");
        return;
      }

      String backupBase64 = doc["backupBase64"] | "";
      String password = doc["password"] | "";

      if (backupBase64.isEmpty()) {
        request->send(400, "application/json", "{\"error\":\"Missing backup file data\"}");
        return;
      }

      // Base64 decode backup
      std::vector<uint8_t> rawBackup;
      // Strip data URL prefix if present (e.g. data:application/octet-stream;base64,)
      int commaIdx = backupBase64.indexOf(',');
      if (commaIdx >= 0) {
        backupBase64 = backupBase64.substring(commaIdx + 1);
      }

      // Decode base64
      int bLen = backupBase64.length();
      std::vector<uint8_t> decoded(bLen);
      size_t decodedLen = 0;
      mbedtls_base64_decode(decoded.data(), decoded.size(), &decodedLen, (const unsigned char*)backupBase64.c_str(), bLen);
      if (decodedLen < sizeof(VaultHeader)) {
        request->send(400, "application/json", "{\"error\":\"Invalid or corrupted backup format\"}");
        return;
      }

      VaultHeader hdr;
      memcpy(&hdr, decoded.data(), sizeof(VaultHeader));

      if (memcmp(hdr.magic, "EVLT", 4) != 0) {
        request->send(400, "application/json", "{\"error\":\"Unrecognized backup header magic\"}");
        return;
      }

      size_t cipherLen = hdr.ciphertextLen;
      if (sizeof(VaultHeader) + cipherLen > decodedLen) {
        request->send(400, "application/json", "{\"error\":\"Truncated backup payload\"}");
        return;
      }

      uint8_t key[VAULT_KEY_LEN];
      if (!password.isEmpty()) {
        if (!pbkdf2DeriveKey(password, hdr.salt, VAULT_SALT_LEN, key)) {
          request->send(500, "application/json", "{\"error\":\"Key derivation failed\"}");
          return;
        }
      } else if (vaultUnlocked) {
        memcpy(key, vaultKey, VAULT_KEY_LEN);
      } else {
        request->send(403, "application/json", "{\"error\":\"Master Password required to restore backup\"}");
        return;
      }

      const uint8_t *ciphertext = decoded.data() + sizeof(VaultHeader);
      std::vector<uint8_t> plaintext(cipherLen + 1, 0);

      bool ok = aes256GcmDecrypt(
        key,
        hdr.iv,
        VAULT_IV_LEN,
        hdr.tag,
        VAULT_TAG_LEN,
        ciphertext,
        cipherLen,
        plaintext.data()
      );

      if (!ok) {
        request->send(403, "application/json", "{\"error\":\"Decryption failed: Incorrect password or corrupted backup\"}");
        return;
      }

      String jsonStr = reinterpret_cast<const char *>(plaintext.data());
      DynamicJsonDocument restoredDoc(32768);
      if (deserializeJson(restoredDoc, jsonStr) != DeserializationError::Ok) {
        request->send(400, "application/json", "{\"error\":\"Corrupted backup JSON payload\"}");
        return;
      }

      uint32_t restoredTotpCount = 0;
      uint32_t restoredPasskeyCount = 0;

      // 1. Restore Software Vault Entries
      if (restoredDoc.containsKey("software_vault") && restoredDoc["software_vault"].is<JsonArray>()) {
        String vaultItemsStr;
        serializeJson(restoredDoc["software_vault"], vaultItemsStr);
        memcpy(vaultSalt, hdr.salt, VAULT_SALT_LEN);
        memcpy(vaultKey, key, VAULT_KEY_LEN);
        saveVaultEncrypted(vaultItemsStr, vaultKey);
        vaultCachedJson = vaultItemsStr;
        vaultUnlocked = true;
        vaultLastActiveMs = millis();
        restoredTotpCount = restoredDoc["software_vault"].as<JsonArray>().size();
      }

      // 2. Restore FIDO2 Passkeys
      if (restoredDoc.containsKey("fido_credentials") && restoredDoc["fido_credentials"].is<JsonArray>()) {
        std::vector<FidoCredential> imported;
        for (JsonObject obj : restoredDoc["fido_credentials"].as<JsonArray>()) {
          FidoCredential c;
          c.rpId = obj["rpId"].as<String>();
          c.userName = obj["userName"] | "";
          c.userDisplayName = obj["userDisplayName"] | "";
          c.signCounter = obj["signCounter"] | 0;
          c.createdAt = obj["createdAt"] | 0;

          auto fromHexStr = [](const String &h) {
            std::vector<uint8_t> v;
            for (size_t i = 0; i + 1 < h.length(); i += 2) {
              char sub[3] = { h[i], h[i+1], '\0' };
              v.push_back((uint8_t)strtol(sub, nullptr, 16));
            }
            return v;
          };

          c.userId = fromHexStr(obj["userId"] | "");
          c.credId = fromHexStr(obj["credId"] | "");
          c.privKey = fromHexStr(obj["privKey"] | "");
          c.pubKeyX = fromHexStr(obj["pubKeyX"] | "");
          c.pubKeyY = fromHexStr(obj["pubKeyY"] | "");
          c.hmacSecretKey = fromHexStr(obj["hmacSecretKey"] | "");
          if (c.hmacSecretKey.empty()) {
            c.hmacSecretKey.resize(32);
            esp_fill_random(c.hmacSecretKey.data(), 32);
          }

          if (!c.credId.empty() && !c.privKey.empty()) {
            imported.push_back(c);
          }
        }
        FidoStore::importCredentials(imported, true);
        restoredPasskeyCount = imported.size();
      }

      // 3. Restore FIDO Settings
      if (restoredDoc.containsKey("fido_settings")) {
        bool emUv = restoredDoc["fido_settings"]["emulate_uv"] | false;
        FidoStore::setEmulateUv(emUv);
      }

      logSystem(String("[VAULT] Backup restored successfully: ") + String(restoredTotpCount) + " 2FA/vault items, " + String(restoredPasskeyCount) + " FIDO2 passkeys");
      request->send(200, "application/json", "{\"ok\":true,\"restored_totp\":" + String(restoredTotpCount) + ",\"restored_passkeys\":" + String(restoredPasskeyCount) + "}");
    }
  );

  server.on("/api/fido/emulate_uv", HTTP_POST, [](AsyncWebServerRequest *request) {}, nullptr,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
      if (!requireAuth(request)) return;
      DynamicJsonDocument doc(256);
      deserializeJson(doc, data, len);
      bool enabled = doc["enabled"] | false;
      FidoStore::setEmulateUv(enabled);
      logSystem(String("[FIDO2] Biometric UV Emulation ") + (enabled ? "ENABLED" : "DISABLED"));
      request->send(200, "application/json", enabled ? "{\"ok\":true,\"emulate_uv\":true}" : "{\"ok\":true,\"emulate_uv\":false}");
    }
  );

  server.on("/api/fido/mode", HTTP_POST, [](AsyncWebServerRequest *request) {}, nullptr,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
      if (!hasAccess(request)) return;
      DynamicJsonDocument doc(256);
      deserializeJson(doc, data, len);
      fidoSecurityKeyMode = doc["enabled"] | !fidoSecurityKeyMode;
      Preferences prefs;
      if (prefs.begin("sysconfig", false)) {
        prefs.putBool("fido_mode", fidoSecurityKeyMode);
        prefs.end();
      }
      request->send(200, "application/json", "{\"ok\":true,\"rebooting\":true}");
      indicateRebootAndRestart(400);
    }
  );

  server.on("/api/fido/touch", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!requireAuth(request)) return;
    if (FIDO.isWaitingForTouch()) {
      FIDO.confirmTouch();
      request->send(200, "application/json", "{\"ok\":true,\"touched\":true}");
    } else {
      request->send(400, "application/json", "{\"error\":\"No pending FIDO request\"}");
    }
  });

  server.on("/api/fido/reset", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!requireAuth(request)) return;
    FidoStore::clearAll();
    logSystem("[FIDO2] Hardware Security Key Factory Reset: All passkeys and PIN cleared");
    request->send(200, "application/json", "{\"ok\":true,\"reset\":true}");
  });

  server.on("/api/fido/pin", HTTP_POST, [](AsyncWebServerRequest *request) {}, nullptr,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
      if (!requireAuth(request)) return;
      DynamicJsonDocument doc(512);
      deserializeJson(doc, data, len);
      String action = doc["action"] | "";
      String currentPin = doc["currentPin"] | "";
      String newPin = doc["newPin"] | "";

      if (action == "remove" || action == "clear") {
        FidoStore::clearPin();
        logSystem("[FIDO2] Security Key PIN removed");
        request->send(200, "application/json", "{\"ok\":true,\"removed\":true}");
        return;
      }

      if (action == "set") {
        if (newPin.length() < 4) {
          request->send(400, "application/json", "{\"error\":\"PIN must be at least 4 digits\"}");
          return;
        }
        if (FidoStore::isPinSet()) {
          // If PIN is already set, require valid current PIN to change
          if (currentPin.length() < 4) {
            request->send(400, "application/json", "{\"error\":\"Current PIN is required\"}");
            return;
          }
          bool ok = FidoStore::changePin(
            reinterpret_cast<const uint8_t*>(currentPin.c_str()), currentPin.length(),
            reinterpret_cast<const uint8_t*>(newPin.c_str()), newPin.length()
          );
          if (!ok) {
            request->send(403, "application/json", "{\"error\":\"Incorrect current PIN or max attempts exceeded\"}");
            return;
          }
          logSystem("[FIDO2] Security Key PIN changed successfully");
          request->send(200, "application/json", "{\"ok\":true,\"changed\":true}");
        } else {
          // Setting new PIN for the first time
          bool ok = FidoStore::setPin(reinterpret_cast<const uint8_t*>(newPin.c_str()), newPin.length());
          if (!ok) {
            request->send(400, "application/json", "{\"error\":\"Failed to set PIN\"}");
            return;
          }
          logSystem("[FIDO2] Security Key PIN configured successfully");
          request->send(200, "application/json", "{\"ok\":true,\"pin_set\":true}");
        }
        return;
      }

      request->send(400, "application/json", "{\"error\":\"Invalid action\"}");
    }
  );

  server.on("/api/fido/pin/clear", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!requireAuth(request)) return;
    FidoStore::clearPin();
    logSystem("[FIDO2] Security Key PIN cleared");
    request->send(200, "application/json", "{\"ok\":true,\"pin_cleared\":true}");
  });

  server.on("/api/fido/credential/delete", HTTP_POST, [](AsyncWebServerRequest *request) {}, nullptr,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
      if (!requireAuth(request)) return;
      DynamicJsonDocument doc(512);
      deserializeJson(doc, data, len);
      const char *credIdHex = doc["credId"] | "";
      if (strlen(credIdHex) == 0) {
        request->send(400, "application/json", "{\"error\":\"Missing credId\"}");
        return;
      }
      std::vector<uint8_t> credId;
      for (size_t i = 0; i + 1 < strlen(credIdHex); i += 2) {
        char sub[3] = { credIdHex[i], credIdHex[i+1], '\0' };
        credId.push_back(static_cast<uint8_t>(strtol(sub, nullptr, 16)));
      }
      bool ok = FidoStore::deleteCredential(credId);
      request->send(ok ? 200 : 404, "application/json", ok ? "{\"ok\":true,\"deleted\":true}" : "{\"error\":\"Credential not found\"}");
    }
  );

  server.on("/api/fido/credential/policy", HTTP_POST, [](AsyncWebServerRequest *request) {}, nullptr,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
      if (!requireAuth(request)) return;
      DynamicJsonDocument doc(512);
      deserializeJson(doc, data, len);
      const char *credIdHex = doc["credId"] | "";
      uint8_t policy = doc["credProtect"] | 1;
      if (policy < 1 || policy > 3) policy = 1;

      if (strlen(credIdHex) == 0) {
        request->send(400, "application/json", "{\"error\":\"Missing credId\"}");
        return;
      }
      std::vector<uint8_t> targetCredId;
      for (size_t i = 0; i + 1 < strlen(credIdHex); i += 2) {
        char sub[3] = { credIdHex[i], credIdHex[i+1], '\0' };
        targetCredId.push_back(static_cast<uint8_t>(strtol(sub, nullptr, 16)));
      }

      bool found = false;
      for (auto &c : FidoStore::getAllCredentials()) {
        if (c.credId == targetCredId) {
          c.credProtect = policy;
          found = true;
          break;
        }
      }
      if (found) {
        FidoStore::saveToStorage();
        request->send(200, "application/json", "{\"ok\":true,\"credProtect\":" + String(policy) + "}");
      } else {
        request->send(404, "application/json", "{\"error\":\"Credential not found\"}");
      }
    }
  );
}

void setup() {
  Serial.begin(115200);

  // BOOT button (GPIO 0) used by WAIT_BUTTON Ducky command & FIDO2 User Presence Touch
  pinMode(0, INPUT_PULLUP);

  pixels.begin();
  pixels.setBrightness(clampInt(ledBrightness, 0, 255));
  pixels_alt.begin();
  pixels_alt.setBrightness(clampInt(ledBrightness, 0, 255));
  setStatus(0, 0, 255);

  // Safe non-destructive LittleFS mount
  if (!LittleFS.begin(false)) {
    Serial.println("[FS] Initial LittleFS mount failed, retrying in 300ms...");
    delay(300);
    if (!LittleFS.begin(false)) {
      Serial.println("[FS] Retrying mount with formatOnFail=false...");
      delay(500);
      if (!LittleFS.begin(false)) {
        Serial.println("[FS] LittleFS unformatted. Initializing fresh filesystem...");
        LittleFS.begin(true);
      }
    }
  }

  ensureScriptDir();
  ensureActionsDir();
  loadSettings();

  if (psramFound()) {
    psramBuffer = static_cast<char *>(heap_caps_malloc(BUFFER_SIZE, MALLOC_CAP_SPIRAM));
  }
  if (!psramBuffer) {
    psramBuffer = static_cast<char *>(malloc(64 * 1024));
  }

  if (fidoSecurityKeyMode) {
    // Dedicated FIDO2 Hardware Security Key Profile
    FIDO.begin(true);
    USB.VID(0x10C4);
    USB.PID(0x8A2A);
    USB.manufacturerName("FIDO Alliance");
    USB.productName("ESP32-S3 FIDO2 Passkey");
    USB.begin();
    setStatus(0, 180, 255); // Solid Cyan LED in Passkey Mode!
  } else {
    // Normal Ducky Keyboard + Mouse + MSC Profile
    // Allocate here so their constructors (addDevice) only run in normal mode.
    // In passkey mode they must NOT exist — FIDO must be the only HID device.
    Keyboard = new USBHIDKeyboard();
    Mouse    = new USBHIDMouse();
    Consumer = new USBHIDConsumerControl();
    if(Keyboard) Keyboard->begin();
    if(Mouse) Mouse->begin();
    if(Consumer) Consumer->begin();
    FIDO.begin(false);
    if (psramFound()) {
      mscDiskBuffer = static_cast<uint8_t *>(heap_caps_malloc(MSC_DISK_SIZE, MALLOC_CAP_SPIRAM));
      if (mscDiskBuffer && usbMscEnabled) {
        initVirtualFatDisk();
        MSC.vendorID(usbVendorName.c_str());
        MSC.productID(usbMscVolumeLabel.c_str());
        MSC.productRevision("1.0");
        MSC.onRead(onMscRead);
        MSC.onWrite(onMscWrite);
        MSC.onStartStop(onMscStartStop);
        MSC.mediaPresent(true);
        MSC.begin(MSC_SECTOR_COUNT, MSC_SECTOR_SIZE);
      }
    }
    USB.VID(usbVendorId);
    USB.PID(usbProductId);
    USB.manufacturerName(usbVendorName.c_str());
    USB.productName(usbProductName.c_str());
    USB.begin();
    setStatus(0, 255, 0); // Solid Green LED in Normal Mode!
  }

  jobQueue = xQueueCreate(1, sizeof(DuckyJob));
  hidEventQueue = xQueueCreate(64, sizeof(HidRealtimeEvent));
  kvmUdpMutex = xSemaphoreCreateMutex();
  kvmBridgeRecordMutex = xSemaphoreCreateMutex();

  xTaskCreatePinnedToCore(duckyWorkerTask, "DuckyWorker", 16384, nullptr, 1, nullptr, 1);
  xTaskCreatePinnedToCore(hidRealtimeTask, "HidRealtime", 8192, nullptr, 2, nullptr, 0);
  xTaskCreatePinnedToCore(kvmNetworkTask, "KvmNetwork", 8192, nullptr, 2, nullptr, 0);

  if (bleFidoEnabled) {
    BleFido.begin("ESP32-S3 Passkey");
  }

  connectWiFi();
  updateKvmUdpBinding();
  registerRoutes();
  server.begin();

  logSystem("[BOOT] ESP32-S3 HID Console initialized (FW: v" + String(FIRMWARE_VERSION) + ")");
  logSystem("[NET] Web Server running. AP IP: " + WiFi.softAPIP().toString() + (WiFi.status() == WL_CONNECTED ? " | STA IP: " + WiFi.localIP().toString() : ""));
  if (fidoSecurityKeyMode) {
    logSystem("[USB] Dedicated FIDO2 Hardware Passkey active (Cyan LED)");
  } else {
    logSystem("[USB] Normal Ducky HID + MSC Storage active (Green LED)");
  }
}

void checkPhysical2faTrigger() {
  static uint32_t btnPressStart = 0;
  static uint32_t lastPulse = 0;
  static bool pulseState = false;

  // 1. FIDO2 / WebAuthn User Presence Touch Prompt (Unified Engine for USB & BLE)
  if (GlobalFidoEngine.isWaitingForTouch()) {
    GlobalFidoEngine.checkTimeout();

    FidoPendingAction act = GlobalFidoEngine.getPendingAction();
    uint32_t interval = (act == FIDO_ACTION_RESET) ? 120 : 160;

    // High-visibility rapid blinking with distinct Neon color per operation
    if (millis() - lastPulse > interval) {
      lastPulse = millis();
      pulseState = !pulseState;
      if (pulseState) {
        if (act == FIDO_ACTION_MAKE_CREDENTIAL) {
          setStatus(255, 0, 180); // Neon Electric Magenta / Pink (Registration)
        } else if (act == FIDO_ACTION_GET_ASSERTION) {
          setStatus(0, 255, 255); // Neon Electric Cyan / Blue (Login / Authentication)
        } else if (act == FIDO_ACTION_RESET) {
          setStatus(255, 80, 0);  // Neon Amber / Electric Orange (Factory Reset Warning)
        } else {
          setStatus(0, 255, 255); // Default Neon Cyan
        }
      } else {
        setStatus(0, 0, 0);     // OFF
      }
    }
  }

  // Physical BOOT Button Handler (GPIO 0, active LOW)
  if (digitalRead(0) == LOW) {
    if (btnPressStart == 0) {
      btnPressStart = millis();
    } else if (millis() - btnPressStart > 2500) {
      // 2.5s Long-press -> Toggle Security Key Mode / Normal Mode!
      btnPressStart = 0;
      fidoSecurityKeyMode = !fidoSecurityKeyMode;
      persistSettings();
      if (fidoSecurityKeyMode) {
        logSystem("[MODE] Switching to Dedicated FIDO2 Passkey Mode... Rebooting");
      } else {
        logSystem("[MODE] Switching to Normal HID/Ducky Mode... Rebooting");
      }
      indicateRebootAndRestart(500);
      return;
    }
  } else {
    if (btnPressStart > 0) {
      uint32_t pressDuration = millis() - btnPressStart;
      btnPressStart = 0;
      if (pressDuration < 2000) {
        // Short press: FIDO touch confirmation or Vault TOTP type
        if (GlobalFidoEngine.isWaitingForTouch()) {
          String rp = GlobalFidoEngine.getPendingRpId();
          GlobalFidoEngine.confirmTouch();
          logSystem("[FIDO2] Physical Touch confirmed for " + (rp.isEmpty() ? "Passkey operation" : rp));
          setStatus(0, 255, 60); // Neon Lime Green Confirmation
          delay(300);
          setStatus(fidoSecurityKeyMode ? 0 : 0, fidoSecurityKeyMode ? 180 : 255, fidoSecurityKeyMode ? 255 : 0);
        } else if (!fidoSecurityKeyMode && vaultUnlocked) {
          // Auto-type first TOTP in unlocked vault
          DynamicJsonDocument itemsDoc(8192);
          if (deserializeJson(itemsDoc, vaultCachedJson) == DeserializationError::Ok && itemsDoc.is<JsonArray>()) {
            JsonArray arr = itemsDoc.as<JsonArray>();
            for (JsonObject obj : arr) {
              String type = obj["type"] | "totp";
              if (type == "totp" && obj.containsKey("secret")) {
                time_t nowSec = time(nullptr);
                int period = obj["period"] | 30;
                int digits = obj["digits"] | 6;
                String otp = calculateTotp(obj["secret"].as<String>(), nowSec, period, digits);
                for (size_t c = 0; c < otp.length(); c++) {
                  if(Keyboard) Keyboard->write(static_cast<uint8_t>(otp[c]));
                  if (typeDelay > 0) delay(typeDelay);
                }
                delay(10);
                keyboardTap(KEY_RETURN);
                setStatus(0, 255, 0);
                delay(150);
                setStatus(0, 255, 0);
                break;
              }
            }
          }
        }
      }
    }
  }
}

void loop() {
  static uint32_t lastPrune = 0;
  static uint32_t lastKvmBindRefresh = 0;

  checkPhysical2faTrigger();

  if (!activeSessionToken.isEmpty() && isSessionExpired()) {
    clearSession();
  }

  uint32_t now = millis();
  if (now - lastPrune > 30000) {
    pruneLoginSlots();
    lastPrune = now;
  }

  if (now - lastKvmBindRefresh > 5000) {
    updateKvmUdpBinding();
    lastKvmBindRefresh = now;
  }

  vTaskDelay(pdMS_TO_TICKS(25));
}

