# ESP32-S3 HID Console (R8N16)

Web-based USB HID injector for ESP32-S3 with:
- Ducky Script editor
- Live text injection
- Remote hotkeys
- Single active login session
- PSRAM-backed 2 MB payload buffer

This project is tuned for ESP32-S3 N16R8 (16 MB flash, 8 MB PSRAM).

## Highlights

- Login page with cookie-based authentication
- Only one active user session at a time
- Safer request buffering and queue handling
- Improved typing speed controls (delay + burst tuning)
- Clean project structure with web assets in `data/`

## Project Structure

```text
ESP32S3_Project/
├── boards/
│   └── esp32-s3-devkitc-1-n16r8.json
├── data/
│   ├── app.html
│   ├── app.js
│   ├── login.html
│   └── styles.css
├── src/
│   └── main.cpp
├── partitions.csv
├── platformio.ini
└── README.md
```

## Build / Flash

1. Build + flash firmware:

```bash
pio run -t upload
```

2. Upload web UI files to LittleFS (required):

```bash
pio run -t uploadfs
```

3. Open serial monitor:

```bash
pio device monitor
```

## Default Access

- AP SSID: `ESP32-Ducky-Pro`
- AP password: `password123`
- Login user: `admin`
- Login pass: `admin123`

Change these in the Settings tab immediately after first login.

## Typing Speed Tuning (No Missing Characters)

Use all 4 settings together instead of only reducing `Typing Delay`:

- `Typing Delay (ms)`: delay between characters
- `Burst Characters`: number of chars before a short pause
- `Burst Pause (ms)`: short pause after each burst
- `Newline Pause (ms)`: extra delay on line breaks

Safe starting profiles:

- Reliable profile:
  - Typing Delay: `5`
  - Burst Characters: `20`
  - Burst Pause: `14`
  - Newline Pause: `50`

- Faster profile:
  - Typing Delay: `2`
  - Burst Characters: `28`
  - Burst Pause: `8`
  - Newline Pause: `30`

If characters drop on the target machine, increase `Burst Pause` first.

## Internet Remote Access

Yes, remote internet access is possible, but do not expose the board directly with raw port-forwarding.

Recommended options:

1. Tailscale (best for private use)
- Install Tailscale on router/host and client devices.
- Keep ESP32 accessible only inside your private tailnet.

2. Cloudflare Tunnel via local bridge host
- Run a small local proxy host in same LAN as ESP32.
- Put HTTPS + access policy on Cloudflare before forwarding to ESP32 HTTP.

3. Port forwarding (not recommended)
- If used, at minimum restrict source IPs and use strong credentials.

Security note:
- ESP32 AsyncWebServer in this project serves HTTP locally.
- For internet use, terminate HTTPS on a trusted tunnel/proxy layer.

### Recommended HTTPS + Token Flow (Reverse Proxy)

1. Keep ESP32 on LAN only (no direct public port forward).
2. Put Nginx/Caddy/Cloudflare Tunnel in front with HTTPS.
3. Enable `reverse-proxy token auth` in Settings and generate a token.
4. Configure proxy to add these headers to ESP32 upstream requests:
  - `X-Proxy-Token: <your-token>`
  - `X-Forwarded-Proto: https`
5. Keep app login enabled for user sessions (single active user mode).

This gives a two-layer model:
- HTTPS + token check at reverse-proxy boundary
- Application login/session control inside ESP32

## API Overview

Auth/session:
- `POST /api/login`
- `POST /api/logout`
- `GET /api/login_status`

Script flow:
- `GET /api/list`
- `GET /api/load?name=...`
- `POST /api/edit` (multipart)
- `DELETE /api/delete?name=...`
- `POST /api/run`
- `POST /api/stop`

Live control:
- `POST /api/live_text`
- `POST /api/live_key`
- `POST /api/live_combo`
- `POST /api/kbd_event`
- `POST /api/mouse_move`
- `POST /api/mouse_scroll`
- `POST /api/mouse_button`

Settings:
- `GET /api/get_settings`
- `POST /api/save_settings`
- `POST /api/reboot`
- `GET /api/proxy_profile`

## Notes

- Use native USB port for HID.
- If LED pin differs on your board variant, set `STATUS_LED_PIN` in build flags.
- Script files are stored under `/scripts` in LittleFS.

## Performance / Core Split

- Core 1: script parsing + high-volume text worker queue
- Core 0: realtime HID events (keyboard/mouse)

This split keeps the UI responsive even when script jobs are active and improves input stability under load.
