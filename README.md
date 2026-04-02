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
- KVM UDP bridge (esp32-kvm-ip 16-byte packet compatible)
- Action recorder file upload/run for keyboard + mouse playback
- USB VID/PID + vendor/product name settings (apply on reboot)
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
- `POST /api/hid_release_all`

KVM bridge:
- `GET /api/kvm_status`
- `POST /api/kvm_config`

Action files:
- `GET /api/action_files`
- `GET /api/action_file/load?name=...`
- `POST /api/action_file/save?name=...`
- `POST /api/action_file/run?name=...`
- `DELETE /api/action_file/delete?name=...`

Settings:
- `GET /api/get_settings`
- `POST /api/save_settings`
- `POST /api/reboot`
- `GET /api/proxy_profile`

## KVM Bridge (New)

The KVM tab adds a UDP listener compatible with the 16-byte packet design used in:
- https://github.com/KMChris/esp32-kvm-ip

Supported packet types:
- Mouse state packet (buttons + int16 dx/dy + wheel/pan)
- Keyboard state packet (modifiers + 6-key rollover)
- Consumer usage packet (media keys)

You can configure:
- Enable/disable listener
- UDP port (default `4210`)
- Optional allowed source IP filter

### Host server launch (updated)

Default toggle key is now `F8` (better for laptops). You can still use Scroll Lock if desired.

Examples:

```bash
python server/server.py --host 192.168.4.1 --port 4210 --toggle-key f8
```

With screenshot preview service:

```bash
python server/server.py --host 192.168.4.1 --port 4210 --toggle-key f8 --preview-port 9876
```

Supported `--toggle-key` values:
- `f8`
- `f9`
- `f10`
- `f12`
- `pause`
- `scrolllock`

Preview endpoint (host side):
- `http://127.0.0.1:9876/screenshot.bmp`

## Action Recording / Replay File

The KVM tab can record keyboard and mouse actions sent from the web UI, save them as files on ESP (`/actions`), and replay them later.

Replay file format is line-based:
- `delay_ms|key_tap|code|hold`
- `delay_ms|key_down|code`
- `delay_ms|key_up|code`
- `delay_ms|key_release_all`
- `delay_ms|combo|flags|code|hold`
- `delay_ms|mouse_move|dx|dy`
- `delay_ms|mouse_scroll|wheel|pan`
- `delay_ms|mouse_button|button|action`

Where combo `flags` bitmask is:
- `1`: Ctrl
- `2`: Alt
- `4`: Shift
- `8`: GUI/Win

## USB Identity Settings

Settings now include:
- Vendor preset dropdown
- Custom USB Vendor Name + Vendor ID
- Custom USB Product Name + Product ID

These values are stored in settings and applied before `USB.begin()` on next boot.

## Notes

- Use native USB port for HID.
- If LED pin differs on your board variant, set `STATUS_LED_PIN` in build flags.
- Script files are stored under `/scripts` in LittleFS.

## Performance / Core Split

- Core 1: script parsing + high-volume text worker queue
- Core 0: realtime HID events (keyboard/mouse)

This split keeps the UI responsive even when script jobs are active and improves input stability under load.
