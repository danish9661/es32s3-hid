#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <Adafruit_NeoPixel.h>
#include "USB.h"
#include "USBHIDKeyboard.h"
#include "USBHIDMouse.h"
#include "USBHIDConsumerControl.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include <cstring>

// --- BOARD / DEVICE CONFIG ---
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

// --- RUNTIME OBJECTS ---
USBHIDKeyboard Keyboard;
USBHIDMouse Mouse;
USBHIDConsumerControl Consumer;
AsyncWebServer server(80);
Adafruit_NeoPixel pixels(NUMPIXELS, STATUS_LED_PIN, NEO_GRB + NEO_KHZ800);
WiFiUDP kvmUdp;

char *psramBuffer = nullptr;
size_t bufferIndex = 0;

volatile bool isWorkerBusy = false;
volatile bool stopScriptFlag = false;
volatile bool isJobQueued = false;
volatile bool isInputLocked = false;

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

  out["delay"] = typeDelay;
  out["burst_chars"] = burstChars;
  out["burst_pause"] = burstPauseMs;
  out["line_delay"] = lineDelayMs;
  out["bright"] = ledBrightness;
  out["kvm_mouse_smooth"] = kvmMouseSmoothness;

  File file = LittleFS.open(SETTINGS_FILE, "w");
  if (!file) {
    Serial.println("Failed to open settings file for write");
    return;
  }

  serializeJson(out, file);
  file.close();
}

void loadSettings() {
  if (!LittleFS.exists(SETTINGS_FILE)) {
    pixels.setBrightness(clampInt(ledBrightness, 0, 255));
    return;
  }

  File file = LittleFS.open(SETTINGS_FILE, "r");
  if (!file) {
    Serial.println("Failed to open settings file for read");
    return;
  }

  DynamicJsonDocument doc(2304);
  DeserializationError err = deserializeJson(doc, file);
  file.close();

  if (err) {
    Serial.printf("Settings parse error: %s\n", err.c_str());
    return;
  }

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

  usbVendorName.trim();
  usbProductName.trim();
  if (usbVendorName.isEmpty()) usbVendorName = "Espressif";
  if (usbProductName.isEmpty()) usbProductName = "ESP32-S3 HID Console";
  if (usbVendorName.length() > 48) usbVendorName = usbVendorName.substring(0, 48);
  if (usbProductName.length() > 48) usbProductName = usbProductName.substring(0, 48);

  typeDelay = clampInt(doc["delay"] | typeDelay, 0, 40);
  burstChars = clampInt(doc["burst_chars"] | burstChars, 6, 96);
  burstPauseMs = clampInt(doc["burst_pause"] | burstPauseMs, 0, 120);
  lineDelayMs = clampInt(doc["line_delay"] | lineDelayMs, 0, 250);
  ledBrightness = clampInt(doc["bright"] | ledBrightness, 0, 255);
  kvmMouseSmoothness = clampInt(doc["kvm_mouse_smooth"] | kvmMouseSmoothness, 25, 250);

  if (proxyAuthToken.length() > 128) proxyAuthToken = proxyAuthToken.substring(0, 128);
  if (proxyAuthToken.length() < 16) proxyAuthEnabled = false;

  pixels.setBrightness(ledBrightness);
}

bool applySettingsJson(const String &jsonBody, bool &usbIdentityChanged) {
  usbIdentityChanged = false;

  DynamicJsonDocument doc(2304);
  DeserializationError err = deserializeJson(doc, jsonBody);
  if (err) return false;

  uint16_t oldUsbVid = usbVendorId;
  uint16_t oldUsbPid = usbProductId;
  String oldUsbVendorName = usbVendorName;
  String oldUsbProductName = usbProductName;

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

  typeDelay = clampInt(doc["delay"] | typeDelay, 0, 40);
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
    (oldUsbProductName != usbProductName);

  return true;
}

void keyboardTap(uint8_t keyCode, uint16_t holdMs = 35) {
  Keyboard.press(keyCode);
  delay(holdMs);
  Keyboard.releaseAll();
}

void keyboardCombo(bool ctrl, bool alt, bool shift, bool gui, uint8_t keyCode, uint16_t holdMs = 40) {
  if (ctrl) Keyboard.press(KEY_LEFT_CTRL);
  if (alt) Keyboard.press(KEY_LEFT_ALT);
  if (shift) Keyboard.press(KEY_LEFT_SHIFT);
  if (gui) Keyboard.press(KEY_LEFT_GUI);

  Keyboard.press(keyCode);
  delay(holdMs);
  Keyboard.releaseAll();
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
        Keyboard.press(event.keyCode);
        break;
      case HidRealtimeType::KeyUp:
        Keyboard.release(event.keyCode);
        break;
      case HidRealtimeType::KeyReleaseAll:
        Keyboard.releaseAll();
        Mouse.release(MOUSE_ALL);
        Consumer.release();
        kvmButtons = 0;
        break;
      case HidRealtimeType::Combo:
        keyboardCombo(event.ctrl, event.alt, event.shift, event.gui, event.keyCode, event.holdMs);
        break;
      case HidRealtimeType::MouseMove:
        Mouse.move(event.dx, event.dy, 0, 0);
        break;
      case HidRealtimeType::MouseScroll:
        Mouse.move(0, 0, event.wheel, event.pan);
        break;
      case HidRealtimeType::MouseButton:
        if (event.mouseAction == MOUSE_ACTION_DOWN) {
          Mouse.press(event.mouseButton);
        } else if (event.mouseAction == MOUSE_ACTION_UP) {
          Mouse.release(event.mouseButton);
        } else {
          Mouse.click(event.mouseButton);
        }
        break;
      case HidRealtimeType::KvmKeyboardState: {
        KeyReport report = {};
        report.modifiers = event.kvmModifiers;
        memcpy(report.keys, event.kvmKeys, sizeof(report.keys));
        Keyboard.sendReport(&report);
      } break;
      case HidRealtimeType::KvmMouseState: {
        uint8_t changed = kvmButtons ^ event.kvmButtons;
        if (changed != 0) {
          uint8_t pressedMask = changed & event.kvmButtons;
          uint8_t releasedMask = changed & static_cast<uint8_t>(~event.kvmButtons);

          if (pressedMask) Mouse.press(pressedMask);
          if (releasedMask) Mouse.release(releasedMask);

          kvmButtons = event.kvmButtons;
        }

        int smoothPercent = clampInt(kvmMouseSmoothness, 25, 250);
        int16_t dx = scaleMouseDelta(event.kvmDx, smoothPercent);
        int16_t dy = scaleMouseDelta(event.kvmDy, smoothPercent);

        while (dx != 0 || dy != 0) {
          int8_t stepX = clampInt8(dx, -120, 120);
          int8_t stepY = clampInt8(dy, -120, 120);
          Mouse.move(stepX, stepY, 0, 0);

          dx -= stepX;
          dy -= stepY;

          if (dx != 0 || dy != 0) {
            vTaskDelay(1);
          }
        }

        if (event.kvmWheel != 0 || event.kvmPan != 0) {
          Mouse.move(0, 0, event.kvmWheel, event.kvmPan);
        }
      } break;
      case HidRealtimeType::ConsumerControl:
        if (event.consumerUsage == 0) {
          Consumer.release();
        } else {
          Consumer.press(event.consumerUsage);
          Consumer.release();
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

  int charDelay = clampInt(typeDelay, 0, 40);
  int batchSize = clampInt(burstChars, 6, 96);
  int batchPause = clampInt(burstPauseMs, 0, 120);
  int newlinePause = clampInt(lineDelayMs, 0, 250);

  int sincePause = 0;
  for (size_t i = 0; i < safeLength; i++) {
    if (stopScriptFlag) return;

    char c = psramBuffer[startIndex + i];
    Keyboard.write(static_cast<uint8_t>(c));

    if (charDelay > 0) delay(charDelay);

    sincePause++;
    if (c == '\n' || c == '\r') {
      sincePause = 0;
      if (newlinePause > 0) delay(newlinePause);
    } else if (sincePause >= batchSize) {
      sincePause = 0;
      if (batchPause > 0) delay(batchPause);
      vTaskDelay(1);
    }
  }
}

String readLineRange(size_t lineStart, size_t lineEnd) {
  String line;
  if (!psramBuffer || lineEnd <= lineStart) return line;

  line.reserve(lineEnd - lineStart + 1);
  for (size_t i = lineStart; i < lineEnd; i++) {
    char c = psramBuffer[i];
    if (c != '\r') line += c;
  }
  return line;
}

size_t findLineEnd(size_t start, size_t totalLength) {
  size_t i = start;
  while (i < totalLength && psramBuffer[i] != '\n') i++;
  return i;
}

size_t nextLineStart(size_t lineEnd, size_t totalLength) {
  if (lineEnd >= totalLength) return totalLength;
  return lineEnd + 1;
}

void parseAndExecuteInternal(size_t totalLength) {
  size_t i = 0;
  int defaultDelay = 0;
  int lineCounter = 0;

  while (i < totalLength) {
    if (stopScriptFlag) break;

    size_t lineStart = i;
    size_t lineEnd = findLineEnd(lineStart, totalLength);
    i = nextLineStart(lineEnd, totalLength);

    String rawLine = readLineRange(lineStart, lineEnd);
    String trimmed = rawLine;
    trimmed.trim();

    if (trimmed.isEmpty()) continue;

    String upper = trimmed;
    upper.toUpperCase();

    if (upper.startsWith("REM")) continue;

    if (upper == "BLOCK") {
      size_t blockStart = i;
      size_t cursor = i;
      size_t blockEnd = totalLength;

      while (cursor < totalLength) {
        size_t markerEnd = findLineEnd(cursor, totalLength);
        String marker = readLineRange(cursor, markerEnd);
        marker.trim();
        marker.toUpperCase();
        if (marker == "ENDBLOCK") {
          blockEnd = cursor;
          i = nextLineStart(markerEnd, totalLength);
          break;
        }
        cursor = nextLineStart(markerEnd, totalLength);
      }

      if (blockEnd > blockStart) {
        typeTextInternal(blockStart, blockEnd - blockStart);
      }
      continue;
    }

    if (upper.startsWith("STRING ")) {
      int spacePos = rawLine.indexOf(' ');
      if (spacePos >= 0 && static_cast<size_t>(spacePos + 1) < rawLine.length()) {
        String payload = rawLine.substring(spacePos + 1);
        for (size_t c = 0; c < payload.length(); c++) {
          if (stopScriptFlag) break;
          Keyboard.write(static_cast<uint8_t>(payload[c]));
          if (typeDelay > 0) delay(typeDelay);
        }
      }
    } else if (upper.startsWith("STRINGLN ")) {
      int spacePos = rawLine.indexOf(' ');
      if (spacePos >= 0 && static_cast<size_t>(spacePos + 1) < rawLine.length()) {
        String payload = rawLine.substring(spacePos + 1);
        for (size_t c = 0; c < payload.length(); c++) {
          if (stopScriptFlag) break;
          Keyboard.write(static_cast<uint8_t>(payload[c]));
          if (typeDelay > 0) delay(typeDelay);
        }
        keyboardTap(KEY_RETURN);
      }
    } else if (upper.startsWith("DELAY ")) {
      int d = trimmed.substring(6).toInt();
      if (d > 0) delay(d);
    } else if (upper.startsWith("DEFAULT_DELAY ")) {
      defaultDelay = clampInt(trimmed.substring(14).toInt(), 0, 5000);
    } else if (upper.startsWith("DEFAULTDELAY ")) {
      defaultDelay = clampInt(trimmed.substring(13).toInt(), 0, 5000);
    } else if (upper == "ENTER") {
      keyboardTap(KEY_RETURN);
    } else if (upper == "TAB") {
      keyboardTap(KEY_TAB);
    } else if (upper == "ESC" || upper == "ESCAPE") {
      keyboardTap(KEY_ESC);
    } else if (upper == "BACKSPACE") {
      keyboardTap(KEY_BACKSPACE);
    } else if (upper == "DELETE" || upper == "DEL") {
      keyboardTap(KEY_DELETE);
    } else if (upper == "UP" || upper == "UPARROW") {
      keyboardTap(KEY_UP_ARROW);
    } else if (upper == "DOWN" || upper == "DOWNARROW") {
      keyboardTap(KEY_DOWN_ARROW);
    } else if (upper == "LEFT" || upper == "LEFTARROW") {
      keyboardTap(KEY_LEFT_ARROW);
    } else if (upper == "RIGHT" || upper == "RIGHTARROW") {
      keyboardTap(KEY_RIGHT_ARROW);
    } else if (upper == "SPACE") {
      Keyboard.write(' ');
    } else if (upper == "GUI" || upper == "WINDOWS") {
      keyboardTap(KEY_LEFT_GUI, 80);
    } else if (upper.startsWith("GUI ") || upper.startsWith("WINDOWS ")) {
      char key = trimmed.charAt(trimmed.length() - 1);
      keyboardCombo(false, false, false, true, static_cast<uint8_t>(key));
    } else if (upper.startsWith("CTRL ")) {
      char key = trimmed.charAt(trimmed.length() - 1);
      keyboardCombo(true, false, false, false, static_cast<uint8_t>(key));
    } else if (upper.startsWith("ALT ")) {
      char key = trimmed.charAt(trimmed.length() - 1);
      keyboardCombo(false, true, false, false, static_cast<uint8_t>(key));
    } else if (upper.startsWith("SHIFT ")) {
      char key = trimmed.charAt(trimmed.length() - 1);
      keyboardCombo(false, false, true, false, static_cast<uint8_t>(key));
    }

    if (defaultDelay > 0) delay(defaultDelay);

    lineCounter++;
    if (lineCounter % 8 == 0) {
      vTaskDelay(1);
    }
  }
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
    Mouse.move(stepX, stepY, 0, 0);
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
      Keyboard.press(code);
    } else if (event == "key_up" && count >= 3) {
      uint8_t code = static_cast<uint8_t>(clampInt(parts[2].toInt(), 0, 255));
      Keyboard.release(code);
    } else if (event == "key_release_all") {
      Keyboard.releaseAll();
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
      Mouse.move(0, 0, static_cast<int8_t>(wheel), static_cast<int8_t>(pan));
    } else if (event == "mouse_button" && count >= 4) {
      uint8_t button = parseActionMouseButtonToken(parts[2]);
      uint8_t action = parseActionMouseActionToken(parts[3]);
      if (action == MOUSE_ACTION_DOWN) {
        Mouse.press(button);
      } else if (action == MOUSE_ACTION_UP) {
        Mouse.release(button);
      } else {
        Mouse.click(button);
      }
    } else if (event == "consumer" && count >= 3) {
      uint16_t usage = static_cast<uint16_t>(clampInt(parts[2].toInt(), 0, 0xFFFF));
      if (usage == 0) {
        Consumer.release();
      } else {
        Consumer.press(usage);
        Consumer.release();
      }
    }
  }

  file.close();
  Keyboard.releaseAll();
  Mouse.release(MOUSE_ALL);
  Consumer.release();
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

      Keyboard.releaseAll();
      Mouse.release(MOUSE_ALL);
      Consumer.release();
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

    if (packetSize <= 0) {
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
        kvmPacketsDropped++;
        continue;
      }
    }

    uint32_t nowMs = millis();
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
  String json = "{";
  json += "\"busy\":" + String(isWorkerBusy ? "true" : "false");
  json += ",\"queued\":" + String(isJobQueued ? "true" : "false");
  json += ",\"core_script\":1";
  json += ",\"core_hid\":0";
  json += "}";
  return json;
}

void connectWiFi() {
  WiFi.setSleep(false);

  bool staConnected = false;
  if (!sta_ssid.isEmpty()) {
    WiFi.mode(WIFI_AP_STA);
    WiFi.begin(sta_ssid.c_str(), sta_pass.c_str());

    uint32_t start = millis();
    while (millis() - start < 12000) {
      if (WiFi.status() == WL_CONNECTED) {
        staConnected = true;
        break;
      }
      delay(300);
    }
  }

  if (staConnected) {
    WiFi.softAP(ap_ssid.c_str(), ap_pass.c_str());
  } else {
    WiFi.mode(WIFI_AP);
    WiFi.softAP(ap_ssid.c_str(), ap_pass.c_str());
  }

  Serial.printf("AP IP: %s\n", WiFi.softAPIP().toString().c_str());
  if (staConnected) {
    Serial.printf("STA IP: %s\n", WiFi.localIP().toString().c_str());
  }
}

bool uiFilesPresent() {
  return LittleFS.exists("/login.html") &&
         LittleFS.exists("/app.html") &&
         LittleFS.exists("/styles.css") &&
         LittleFS.exists("/app.js");
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
    if (isWorkerBusy || isJobQueued || isInputLocked) {
      request->_tempObject = reinterpret_cast<void *>(STATE_REJECTED);
    } else {
      request->_tempObject = reinterpret_cast<void *>(STATE_OK);
      bufferIndex = 0;
      isInputLocked = true;
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
  server.serveStatic("/styles.css", LittleFS, "/styles.css").setCacheControl("max-age=300");
  server.serveStatic("/app.js", LittleFS, "/app.js").setCacheControl("max-age=300");

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

    request->send(LittleFS, "/login.html", "text/html");
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

    request->send(LittleFS, "/app.html", "text/html");
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
      if (index == 0) request->_tempObject = new String();

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

        bool hasActiveSession = !activeSessionToken.isEmpty() && !isSessionExpired();
        if (hasActiveSession && remoteIp != activeSessionIp) {
          request->send(409, "application/json", "{\"error\":\"another-user-active\"}");
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

      if (index == 0) request->_tempObject = new String();
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

    DynamicJsonDocument doc(2304);
    doc["ap_ssid"] = ap_ssid;
    doc["ap_pass"] = ap_pass;
    doc["sta_ssid"] = sta_ssid;
    doc["sta_pass"] = sta_pass;
    doc["admin_user"] = admin_user;

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

    doc["delay"] = typeDelay;
    doc["burst_chars"] = burstChars;
    doc["burst_pause"] = burstPauseMs;
    doc["line_delay"] = lineDelayMs;
    doc["bright"] = ledBrightness;
    doc["kvm_mouse_smooth"] = kvmMouseSmoothness;

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

      if (index == 0) request->_tempObject = new String();
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
        bool parsed = applySettingsJson(*body, usbIdentityChanged);

        delete body;
        request->_tempObject = nullptr;

        if (!parsed) {
          request->send(400, "application/json", "{\"error\":\"invalid-json\"}");
          return;
        }

        String response = "{\"saved\":true,\"usb_restart_required\":";
        response += usbIdentityChanged ? "true" : "false";
        response += "}";

        request->send(200, "application/json", response);
      }
    }
  );

  server.on("/api/reboot", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!requireAuth(request)) return;
    request->send(200, "application/json", "{\"rebooting\":true}");
    delay(350);
    ESP.restart();
  });
}

void setup() {
  Serial.begin(115200);

  pixels.begin();
  pixels.setBrightness(clampInt(ledBrightness, 0, 255));
  setStatus(0, 0, 255);

  if (!LittleFS.begin(true)) {
    Serial.println("LittleFS mount failed");
    setStatus(255, 0, 0);
    while (true) delay(1000);
  }

  if (!ensureScriptDir()) {
    Serial.println("Failed to create /scripts directory");
  }
  if (!ensureActionsDir()) {
    Serial.println("Failed to create /actions directory");
  }

  loadSettings();

  psramBuffer = static_cast<char *>(heap_caps_malloc(BUFFER_SIZE, MALLOC_CAP_SPIRAM));
  if (!psramBuffer) {
    Serial.println("PSRAM allocation failed");
    setStatus(255, 0, 0);
    while (true) delay(1000);
  }

  USB.VID(usbVendorId);
  USB.PID(usbProductId);
  USB.manufacturerName(usbVendorName.c_str());
  USB.productName(usbProductName.c_str());

  USB.begin();
  Keyboard.begin();
  Mouse.begin();
  Consumer.begin();

  jobQueue = xQueueCreate(1, sizeof(DuckyJob));
  hidEventQueue = xQueueCreate(64, sizeof(HidRealtimeEvent));
  if (!jobQueue || !hidEventQueue) {
    Serial.println("Queue creation failed");
    setStatus(255, 0, 0);
    while (true) delay(1000);
  }

  kvmUdpMutex = xSemaphoreCreateMutex();
  kvmBridgeRecordMutex = xSemaphoreCreateMutex();
  if (!kvmUdpMutex || !kvmBridgeRecordMutex) {
    Serial.println("Mutex creation failed");
    setStatus(255, 0, 0);
    while (true) delay(1000);
  }

  xTaskCreatePinnedToCore(duckyWorkerTask, "DuckyWorker", 16384, nullptr, 1, nullptr, 1);
  xTaskCreatePinnedToCore(hidRealtimeTask, "HidRealtime", 8192, nullptr, 2, nullptr, 0);
  xTaskCreatePinnedToCore(kvmNetworkTask, "KvmNetwork", 8192, nullptr, 2, nullptr, 0);

  connectWiFi();
  updateKvmUdpBinding();
  registerRoutes();
  server.begin();

  setStatus(0, 255, 0);
  Serial.println("Server started");
}

void loop() {
  static uint32_t lastPrune = 0;
  static uint32_t lastKvmBindRefresh = 0;

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

  vTaskDelay(pdMS_TO_TICKS(1000));
}
