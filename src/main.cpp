#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <Adafruit_NeoPixel.h>
#include "USB.h"
#include "USBHIDKeyboard.h"
#include "esp_heap_caps.h"
#include "esp_system.h"

// --- BOARD / DEVICE CONFIG ---
#ifndef STATUS_LED_PIN
#define STATUS_LED_PIN 38
#endif

constexpr uint8_t NUMPIXELS = 1;
constexpr size_t BUFFER_SIZE = 1024 * 1024 * 2; // 2 MB in PSRAM
constexpr uint32_t SESSION_TTL_MS = 15UL * 60UL * 1000UL;
constexpr const char *SETTINGS_FILE = "/settings.json";
constexpr const char *SCRIPTS_DIR = "/scripts";

// Key values used by the web keyboard shortcuts.
constexpr uint8_t APP_KEY_ENTER = 176;
constexpr uint8_t APP_KEY_ESC = 177;
constexpr uint8_t APP_KEY_BACKSPACE = 178;
constexpr uint8_t APP_KEY_TAB = 179;
constexpr uint8_t APP_KEY_DELETE = 212;
constexpr uint8_t APP_KEY_RIGHT = 215;
constexpr uint8_t APP_KEY_LEFT = 216;
constexpr uint8_t APP_KEY_DOWN = 217;
constexpr uint8_t APP_KEY_UP = 218;
constexpr uint8_t APP_KEY_GUI = 131;

// --- USER SETTINGS (with defaults) ---
String ap_ssid = "ESP32-Ducky-Pro";
String ap_pass = "password123";
String sta_ssid = "";
String sta_pass = "";
String admin_user = "admin";
String admin_pass = "admin123";

int typeDelay = 6;
int burstChars = 24;
int burstPauseMs = 10;
int lineDelayMs = 40;
int ledBrightness = 50;

// --- RUNTIME OBJECTS ---
USBHIDKeyboard Keyboard;
AsyncWebServer server(80);
Adafruit_NeoPixel pixels(NUMPIXELS, STATUS_LED_PIN, NEO_GRB + NEO_KHZ800);

char *psramBuffer = nullptr;
size_t bufferIndex = 0;

volatile bool isWorkerBusy = false;
volatile bool stopScriptFlag = false;
volatile bool isJobQueued = false;
volatile bool isInputLocked = false;

String activeSessionToken = "";
IPAddress activeSessionIp;
uint32_t activeSessionLastSeen = 0;

struct DuckyJob {
  size_t length;
  bool isRawText;
};

QueueHandle_t jobQueue = nullptr;

// --- HELPERS ---
int clampInt(int value, int minimum, int maximum) {
  if (value < minimum) return minimum;
  if (value > maximum) return maximum;
  return value;
}

void setStatus(uint8_t r, uint8_t g, uint8_t b) {
  pixels.setPixelColor(0, pixels.Color(r, g, b));
  pixels.show();
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

String generateSessionToken() {
  char token[33];
  for (int i = 0; i < 16; i++) {
    uint8_t rnd = static_cast<uint8_t>(esp_random() & 0xFF);
    sprintf(token + (i * 2), "%02x", rnd);
  }
  token[32] = '\0';
  return String(token);
}

bool isAuthorized(AsyncWebServerRequest *request, bool refreshSession = true) {
  if (activeSessionToken.isEmpty()) return false;

  if (isSessionExpired()) {
    clearSession();
    return false;
  }

  String sid = getCookieValue(request, "sid");
  if (sid.isEmpty() || sid != activeSessionToken) return false;

  IPAddress remoteIp = request->client() ? request->client()->remoteIP() : IPAddress(0, 0, 0, 0);
  if (remoteIp != activeSessionIp) return false;

  if (refreshSession) {
    activeSessionLastSeen = millis();
  }

  return true;
}

bool requireAuth(AsyncWebServerRequest *request) {
  if (isAuthorized(request)) return true;
  request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
  return false;
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

String scriptPathFromName(const String &safeName) {
  return String(SCRIPTS_DIR) + "/" + safeName;
}

bool ensureScriptDir() {
  if (LittleFS.exists(SCRIPTS_DIR)) return true;
  return LittleFS.mkdir(SCRIPTS_DIR);
}

void persistSettings() {
  DynamicJsonDocument out(1024);
  out["ap_ssid"] = ap_ssid;
  out["ap_pass"] = ap_pass;
  out["sta_ssid"] = sta_ssid;
  out["sta_pass"] = sta_pass;
  out["admin_user"] = admin_user;
  out["admin_pass"] = admin_pass;
  out["delay"] = typeDelay;
  out["burst_chars"] = burstChars;
  out["burst_pause"] = burstPauseMs;
  out["line_delay"] = lineDelayMs;
  out["bright"] = ledBrightness;

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

  DynamicJsonDocument doc(1024);
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

  typeDelay = clampInt(doc["delay"] | typeDelay, 0, 40);
  burstChars = clampInt(doc["burst_chars"] | burstChars, 6, 96);
  burstPauseMs = clampInt(doc["burst_pause"] | burstPauseMs, 0, 120);
  lineDelayMs = clampInt(doc["line_delay"] | lineDelayMs, 0, 250);
  ledBrightness = clampInt(doc["bright"] | ledBrightness, 0, 255);

  pixels.setBrightness(ledBrightness);
}

void applySettingsJson(const String &jsonBody) {
  DynamicJsonDocument doc(1024);
  DeserializationError err = deserializeJson(doc, jsonBody);
  if (err) return;

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

  typeDelay = clampInt(doc["delay"] | typeDelay, 0, 40);
  burstChars = clampInt(doc["burst_chars"] | burstChars, 6, 96);
  burstPauseMs = clampInt(doc["burst_pause"] | burstPauseMs, 0, 120);
  lineDelayMs = clampInt(doc["line_delay"] | lineDelayMs, 0, 250);
  ledBrightness = clampInt(doc["bright"] | ledBrightness, 0, 255);

  pixels.setBrightness(ledBrightness);
  persistSettings();
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
        keyboardTap(APP_KEY_ENTER);
      }
    } else if (upper.startsWith("DELAY ")) {
      int d = trimmed.substring(6).toInt();
      if (d > 0) delay(d);
    } else if (upper.startsWith("DEFAULT_DELAY ")) {
      defaultDelay = clampInt(trimmed.substring(14).toInt(), 0, 5000);
    } else if (upper.startsWith("DEFAULTDELAY ")) {
      defaultDelay = clampInt(trimmed.substring(13).toInt(), 0, 5000);
    } else if (upper == "ENTER") {
      keyboardTap(APP_KEY_ENTER);
    } else if (upper == "TAB") {
      keyboardTap(APP_KEY_TAB);
    } else if (upper == "ESC" || upper == "ESCAPE") {
      keyboardTap(APP_KEY_ESC);
    } else if (upper == "BACKSPACE") {
      keyboardTap(APP_KEY_BACKSPACE);
    } else if (upper == "DELETE" || upper == "DEL") {
      keyboardTap(APP_KEY_DELETE);
    } else if (upper == "UP" || upper == "UPARROW") {
      keyboardTap(APP_KEY_UP);
    } else if (upper == "DOWN" || upper == "DOWNARROW") {
      keyboardTap(APP_KEY_DOWN);
    } else if (upper == "LEFT" || upper == "LEFTARROW") {
      keyboardTap(APP_KEY_LEFT);
    } else if (upper == "RIGHT" || upper == "RIGHTARROW") {
      keyboardTap(APP_KEY_RIGHT);
    } else if (upper == "SPACE") {
      Keyboard.write(' ');
    } else if (upper == "GUI" || upper == "WINDOWS") {
      keyboardTap(APP_KEY_GUI, 80);
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

  DuckyJob job = { length, isRawText };
  if (xQueueSend(jobQueue, &job, 0) == pdPASS) {
    isJobQueued = true;
    return true;
  }
  return false;
}

void duckyWorkerTask(void *parameter) {
  (void) parameter;

  DuckyJob job;
  for (;;) {
    if (xQueueReceive(jobQueue, &job, portMAX_DELAY) == pdTRUE) {
      isJobQueued = false;
      isWorkerBusy = true;
      stopScriptFlag = false;

      setStatus(0, 0, 255); // Blue
      vTaskDelay(pdMS_TO_TICKS(80));

      if (job.isRawText) {
        typeTextInternal(0, job.length);
      } else {
        parseAndExecuteInternal(job.length);
      }

      Keyboard.releaseAll();
      setStatus(255, 255, 255); // White
      vTaskDelay(pdMS_TO_TICKS(120));
      setStatus(0, 255, 0); // Green

      stopScriptFlag = false;
      isWorkerBusy = false;
    }
  }
}

String jsonStatus() {
  String json = "{";
  json += "\"busy\":" + String(isWorkerBusy ? "true" : "false");
  json += ",\"queued\":" + String(isJobQueued ? "true" : "false");
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

  if (!isAuthorized(request)) return;

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

void registerRoutes() {
  server.serveStatic("/styles.css", LittleFS, "/styles.css").setCacheControl("max-age=300");
  server.serveStatic("/app.js", LittleFS, "/app.js").setCacheControl("max-age=300");

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!uiFilesPresent()) {
      request->send(500, "text/plain", "Web UI files missing in LittleFS. Run: pio run -t uploadfs");
      return;
    }

    if (isAuthorized(request, false)) {
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

    if (!isAuthorized(request)) {
      request->redirect("/login");
      return;
    }

    request->send(LittleFS, "/app.html", "text/html");
  });

  server.on("/api/login_status", HTTP_GET, [](AsyncWebServerRequest *request) {
    bool loggedIn = isAuthorized(request, false);
    bool locked = false;

    if (!activeSessionToken.isEmpty() && !isSessionExpired() && !loggedIn) {
      locked = true;
    }

    String json = "{";
    json += "\"loggedIn\":" + String(loggedIn ? "true" : "false");
    json += ",\"locked\":" + String(locked ? "true" : "false");
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

        String user = doc["user"] | "";
        String pass = doc["pass"] | "";

        if (user != admin_user || pass != admin_pass) {
          request->send(401, "application/json", "{\"error\":\"invalid-credentials\"}");
          return;
        }

        IPAddress remoteIp = request->client() ? request->client()->remoteIP() : IPAddress(0, 0, 0, 0);
        bool hasActiveSession = !activeSessionToken.isEmpty() && !isSessionExpired();

        if (hasActiveSession && remoteIp != activeSessionIp) {
          request->send(409, "application/json", "{\"error\":\"another-user-active\"}");
          return;
        }

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
      if (!isAuthorized(request)) return;

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

  server.on(
    "/api/live_key",
    HTTP_POST,
    [](AsyncWebServerRequest *request) {
      if (!requireAuth(request)) return;
    },
    nullptr,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
      if (!isAuthorized(request)) return;
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

      keyboardTap(static_cast<uint8_t>(code), 40);
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
      if (!isAuthorized(request)) return;
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

      bool ctrl = doc["ctrl"] | false;
      bool alt = doc["alt"] | false;
      bool shift = doc["shift"] | false;
      bool gui = doc["gui"] | false;

      uint8_t keyCode = 0;
      if (doc.containsKey("code")) {
        int code = doc["code"] | -1;
        if (code >= 0 && code <= 255) keyCode = static_cast<uint8_t>(code);
      } else {
        String ch = doc["char"] | "";
        if (!ch.isEmpty()) keyCode = static_cast<uint8_t>(ch[0]);
      }

      if (keyCode == 0) {
        request->send(400, "application/json", "{\"error\":\"invalid-key\"}");
        return;
      }

      keyboardCombo(ctrl, alt, shift, gui, keyCode, 45);
      request->send(200, "application/json", "{\"ok\":true}");
    }
  );

  server.on("/api/get_settings", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!requireAuth(request)) return;

    DynamicJsonDocument doc(1024);
    doc["ap_ssid"] = ap_ssid;
    doc["ap_pass"] = ap_pass;
    doc["sta_ssid"] = sta_ssid;
    doc["sta_pass"] = sta_pass;
    doc["admin_user"] = admin_user;
    doc["delay"] = typeDelay;
    doc["burst_chars"] = burstChars;
    doc["burst_pause"] = burstPauseMs;
    doc["line_delay"] = lineDelayMs;
    doc["bright"] = ledBrightness;

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
      if (!isAuthorized(request)) return;

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
        applySettingsJson(*body);

        delete body;
        request->_tempObject = nullptr;

        request->send(200, "application/json", "{\"saved\":true}");
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

  loadSettings();

  psramBuffer = static_cast<char *>(heap_caps_malloc(BUFFER_SIZE, MALLOC_CAP_SPIRAM));
  if (!psramBuffer) {
    Serial.println("PSRAM allocation failed");
    setStatus(255, 0, 0);
    while (true) delay(1000);
  }

  USB.begin();
  Keyboard.begin();

  jobQueue = xQueueCreate(1, sizeof(DuckyJob));
  if (!jobQueue) {
    Serial.println("Queue creation failed");
    setStatus(255, 0, 0);
    while (true) delay(1000);
  }

  xTaskCreatePinnedToCore(duckyWorkerTask, "DuckyWorker", 16384, nullptr, 1, nullptr, 1);

  connectWiFi();
  registerRoutes();
  server.begin();

  setStatus(0, 255, 0);
  Serial.println("Server started");
}

void loop() {
  if (!activeSessionToken.isEmpty() && isSessionExpired()) {
    clearSession();
  }
  vTaskDelay(pdMS_TO_TICKS(1000));
}
