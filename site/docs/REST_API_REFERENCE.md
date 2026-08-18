# REST API Reference & Integration Guide

The ESP32-S3 firmware provides a high-throughput REST API for remote scripting, real-time HID injection, and device management.

---

## 1. Authentication & Session Management

All endpoints require authentication using the session cookie (`sid`) or the `X-Proxy-Token` header.

### Endpoints
* **`GET /api/login_status`**: Returns authentication status, active session, rate limit delay, and brute-force attempt counters.
* **`POST /api/login`**: Authenticates operator with JSON body `{"user":"admin","pass":"admin123"}`. Returns session cookie.
* **`POST /api/logout`**: Invalidates the active session cookie.

---

## 2. Real-Time USB HID Injection

### Endpoints
* **`POST /api/kbd_event`**: Sends individual key actions (`tap`, `down`, `up`) with raw HID keycode.
* **`POST /api/live_key`**: Taps key code with optional hold duration in milliseconds:
  ```json
  { "code": 65, "hold": 35 }
  ```
* **`POST /api/live_combo`**: Executes modifier combinations (GUI, CTRL, ALT, SHIFT):
  ```json
  { "char": "r", "gui": true }
  ```
* **`POST /api/mouse_move`**: Injects delta coordinate movements:
  ```json
  { "dx": 15, "dy": -8 }
  ```
* **`POST /api/mouse_scroll`**: Injects vertical wheel and horizontal pan deltas:
  ```json
  { "wheel": 3, "pan": 0 }
  ```
* **`POST /api/mouse_button`**: Injects button actions (`click`, `down`, `up`, `dblclick`) for `left`, `right`, or `middle`.
* **`POST /api/hid_release_all`**: Emergency release of all held keys and mouse buttons.

---

## 3. Script Storage & Execution

* **`GET /api/list`**: Lists all scripts stored in LittleFS `/scripts`.
* **`GET /api/load?name=script.txt`**: Reads script content.
* **`POST /api/edit`**: Saves or updates a script file in flash storage.
* **`DELETE /api/delete?name=script.txt`**: Deletes a script file.
* **`POST /api/run`**: Streams the script into the 64 KB PSRAM execution buffer and runs the FreeRTOS Ducky worker task.
* **`POST /api/live_text`**: Directly injects a string of text into the active USB keyboard interface without saving to flash.
* **`POST /api/stop`**: Aborts currently running script or payload injection.

---

## 4. KVM Bridge & Macro Management

* **`GET /api/kvm_status`**: Retrieves live UDP packet telemetry, link state, and sequence number resync status.
* **`POST /api/kvm_config`**: Sets KVM UDP listening port (default `8888`) and restricts accepted traffic to a specific client IP.
* **`GET /api/action_files`**: Lists recorded `.krec` / `.txt` macros stored in `/actions`.
* **`POST /api/action_file/run?name=macro.krec`**: Replays an action file directly from hardware memory.
* **`DELETE /api/action_file/delete?name=macro.krec`**: Deletes an action file.

---

## 5. Device Configuration & Telemetry

* **`GET /api/get_settings`**: Returns all device settings, Wi-Fi parameters, typing engine tuning values, and hardware telemetry.
* **`POST /api/save_settings`**: Saves configuration changes to Dual-Layer Storage (LittleFS + Hardware NVS).
* **`POST /api/reboot`**: Performs a clean software restart of the ESP32-S3.
* **`POST /api/ota`**: Multipart upload for firmware (`.bin`) or filesystem (`littlefs.bin`) updates.
