# Web KVM Bridge, Action Replay & Absolute Mouse

The ESP32-S3 firmware provides a dedicated FreeRTOS Core 0 UDP KVM ingestion engine paired with a universal cross-platform desktop client (`esp32_kvm.py`) and hardware Absolute Mouse support.

---

## 1. Universal KVM Client (`esp32_kvm.py`)

A single-file (<30 KB), zero-dependency Python script located in `server/esp32_kvm.py` and stored directly on the virtual USB drive (`DUCKY_DRIVE`).

### Supported Platforms:
* **Linux (Wayland & X11)**: Direct kernel `evdev` integration for 0ms global input capture without root Wayland sandboxing issues.
* **Windows**: Native Win32 Low-Level Keyboard/Mouse Hooks (`SetWindowsHookEx`).
* **macOS**: Quartz Event Taps (`CGEventTap`).

---

### Command-Line Usage

#### Live Forwarding Mode
```bash
# On Linux (Wayland / X11)
sudo python3 server/esp32_kvm.py --ip 192.168.4.1

# On Windows / macOS
python server/esp32_kvm.py --ip 192.168.4.1
```

#### Hotkeys & Shortcuts
* **`[F8]`**: Toggle KVM Mode (**Active**: forwards mouse/keyboard to target machine; **Inactive**: control remains on host).
* **`[F9]`**: Start / Stop Macro Recording.
* **`[Ctrl + Alt + V]`**: Direct Clipboard Typer — types host clipboard contents into target computer.
* **`[Ctrl + C]`**: Exit KVM client.

---

## 2. Hardware Absolute Mouse (`0..32767`)

Standard USB mice report relative coordinate deltas ($dx, dy$), which suffer from host OS acceleration curves, display scaling factors, and screen resolution differences.

The ESP32-S3 implements a **dedicated USB HID Absolute Mouse** descriptor alongside the standard relative mouse:

```
(0, 0) Top-Left ──────────────────────────┐
│                                         │
│          Hardware Coordinate Plane      │
│               0 .. 32767                │
│                                         │
└─────────────────────── (32767, 32767) Bottom-Right
```

### Key Advantages:
1. **Resolution Independence**: `(16384, 16384)` is exactly 50% X and 50% Y regardless of whether the target monitor is 1080p, 4K, or multi-display.
2. **Deterministic UI Automation**: Recorded click coordinates will hit the exact target UI elements on different target operating systems.

```bash
# Record with Absolute Mouse coordinates
sudo python3 server/esp32_kvm.py --ip 192.168.4.1 --abs-mouse --screen-width 1920 --screen-height 1080
```

---

## 3. Action Recorder & Binary `.krec` Macro Format

Recorded actions can be saved in the high-efficiency `.krec` binary stream format and executed on hardware with 0ms parsing delay.

### Binary `.krec` File Format

* **File Header (8 bytes)**:
  - `magic` (4 bytes): `0x4345524B` (`KREC`)
  - `version` (uint16_t): `1`
  - `reserved` (uint16_t): `0`
* **Event Records (8 bytes each)**:
  - `delay_ms` (uint16_t): Delta time before executing event (0..65535 ms)
  - `type` (uint8_t): Event code (1=key_down, 2=key_up, 3=key_tap, 4=release_all, 5=combo, 6=mouse_move, 7=mouse_abs, 8=mouse_scroll, 9=mouse_button, 10=consumer)
  - `flags` (uint8_t): Modifier mask / button bitmask
  - `param1` (int16_t): HID key code / dx / Absolute X / button id
  - `param2` (int16_t): Hold duration / dy / Absolute Y / pan

### Macro Conversion & Inspection
```bash
# View table of events in a .krec recording
python3 server/esp32_kvm.py --view macro.krec

# Convert binary .krec to editable text format
python3 server/esp32_kvm.py --to-txt macro.krec -o macro.txt

# Convert edited text format back to binary .krec
python3 server/esp32_kvm.py --to-krec macro.txt -o macro.krec
```
