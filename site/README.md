# ESP32-S3 HID Console, FIDO2 Passkey & YubiKey 5A Security Hub

<p align="center">
  <img src="https://img.shields.io/badge/Hardware-ESP32--S3%20N16R8-2563eb?style=flat-square" alt="ESP32-S3 N16R8">
  <img src="https://img.shields.io/badge/USB-Triple%20Interface%20(MSC%20+%20HID%20+%20CCID)-0891b2?style=flat-square" alt="USB Triple Interface">
  <img src="https://img.shields.io/badge/Security-FIDO2%20%7C%20CTAP%202.1%20%7C%20WebAuthn-059669?style=flat-square" alt="FIDO2 WebAuthn">
  <img src="https://img.shields.io/badge/Yubico-YubiKey%205A%20Emulation%20(v5.4.3)-7c3aed?style=flat-square" alt="YubiKey 5A">
  <img src="https://img.shields.io/badge/KVM-Ultra--Low%20Latency%20UDP-d97706?style=flat-square" alt="KVM Bridge">
</p>

---

## 1. Overview

The **ESP32-S3 HID Console & Security Hub** is a multi-role hardware platform designed for the **ESP32-S3 N16R8** (16 MB Flash, 8 MB Octal PSRAM).

It unifies three major subsystems into a single pocket-sized microcontroller:
1. **FIDO2 / WebAuthn Hardware Security Key (CTAP 2.1)** with hardware touch confirmation, PIN protection, and dedicated X.509 Batch Attestation certificates.
2. **Yubico YubiKey 5A Security Engine** with native `ykman 5.9.2` management, ISO 7816 OATH (TOTP/HOTP) 2FA applet, KeePassXC 20-byte HMAC-SHA1 challenge-response, and dual touch slots.
3. **BadUSB & Low-Latency Web KVM Bridge** with 2 MB USB Virtual Storage (`DUCKY_DRIVE`), Ducky Script 2.0 parser, and cross-platform mouse/keyboard streaming client (`esp32_kvm.py`).

---

## 2. Complete Capabilities Matrix

| Feature Category | Specific Capability | Description |
| :--- | :--- | :--- |
| **Passkeys / WebAuthn** | FIDO2 / CTAP 2.1 | Resident credentials (`rk`), user presence (`up`), user verification (`uv`), `credProtect` Level 1–3, and 2048-byte `largeBlobs`. |
| **Attestation** | X.509 EC P-256 Chain | Genuine DER-encoded X.509 Batch Certificate returned during enterprise registration. |
| **Yubico Emulation** | Official YubiKey 5A (5.4.3) | Detected natively by `ykman` CLI & GUI; supports ISO 7816 OATH 2FA applet and KeePassXC 20-byte HMAC-SHA1 challenge-response. |
| **Physical Touch** | BOOT Button Gestures | Short press confirms passkey authentication; 2.5s hold toggles between HID and Security Key modes with auto-reboot. |
| **BadUSB Scripting** | Ducky Script 2.0 Engine | Full support for `REPEAT`, `BLOCK...ENDBLOCK`, delays, functional keys (`F1`–`F12`), and compound shortcuts. |
| **Typing Engine** | 4-Parameter Tuning | Custom typing delay, burst size, burst pause, and newline settling delay to prevent dropped keys on slow targets. |
| **Web KVM Bridge** | 16-byte UDP Protocol | Low-latency mouse, keyboard, and multimedia streaming client (`esp32_kvm.py`) for Linux (Wayland/X11), Windows, and macOS. |
| **Absolute Mouse** | Coordinate Plane (0..32767) | Resolution-independent cursor positioning immune to OS mouse acceleration and monitor scaling factors. |
| **Action Macro Engine** | Binary `.krec` Streaming | Live recording and high-speed execution of keyboard/mouse macros directly from hardware flash memory. |
| **USB Mass Storage** | 2 MB Virtual RAM-Disk | Emulates `DUCKY_DRIVE` (FAT12) with LittleFS flash source of truth, automated host sync, and drag-and-drop OTA updates. |
| **Encrypted Vault** | Zero-Knowledge AES-256-GCM | Encrypted storage for TOTP seeds and passkeys with 1-click `.esp32vault` backup and dual NVS flash persistence. |
| **Network & Discovery**| mDNS Responder & Telemetry | Access directly via `http://esp32-hid.local`; displays Home Station IP, direct AP IP, signal strength (RSSI), and gateway. |
| **OTA Updates** | Web & USB Drag-and-Drop | Flash firmware and filesystem images over Wi-Fi or by copying `firmware.bin` directly onto the USB drive. |
| **Hardware Spoofing** | Custom USB Descriptors | Spoof Vendor ID, Product ID, Manufacturer Name, and Product Name with presets for Logitech, Microsoft, Apple, etc. |

---

## 3. The 3 Hardware Operating Modes

The device switches between 3 distinct profiles via the Web Dashboard or by **holding the physical BOOT button (GPIO 0) for 2.5 seconds** (reboots automatically in 500ms):

| Mode | USB Identity (VID:PID) | LED Status | Functionality & Interfaces |
| :--- | :--- | :---: | :--- |
| **1. Normal Ducky Mode** | Custom / Spoofed (e.g. `Logitech`) | Solid Green | Ducky script injection, Web KVM, 2 MB USB Mass Storage (`DUCKY_DRIVE`), Slot 1 OTP typing |
| **2. Standard Passkey Mode** | `0x10C4:0x8A2A` (FIDO Alliance) | Solid Cyan | Standalone FIDO2 security key (Chrome, Edge, Safari, Firefox, Windows Hello, Android) |
| **3. YubiKey 5A Mode** | `0x1050:0x0407` (Yubico) | Solid Dark Purple | Official Yubico 5A emulation, `ykman` management, OATH 2FA applet, KeePassXC challenge-response, CCID smartcard |

---

## 4. Hardware & System Architecture

```text
ESP32-S3 Project Structure:
├── boards/
│   └── esp32-s3-devkitc-1-n16r8.json    # Board definition (16MB Flash, 8MB OPI PSRAM)
├── data/                               # LittleFS Web UI Assets & USB Drive
│   ├── app.html                        # Main console UI
│   ├── app.js                          # Web client application logic
│   ├── login.html                      # Single-operator login page
│   ├── styles.css                      # Modern dark stylesheet
│   └── usb_drive/                      # Source of truth for USB Mass Storage drive
│       ├── esp32_kvm.py                # Universal KVM client & macro engine
│       ├── PAYLOAD.PS1                 # Staged PowerShell payload
│       ├── README.TXT                  # USB drive documentation
│       └── RUN.BAT                     # Execution launcher script
├── server/                             # Standalone Host Utilities (Python)
│   ├── esp32_kvm.py                    # Universal KVM Client & Macro Engine
│   └── target_screenshot_server.py     # Lightweight screenshot agent
├── src/
│   ├── main.cpp                        # Dual-core firmware source code (FIDO2 + HID + Web)
│   ├── FidoEngine.cpp                  # CTAP 2.0 / 2.1 protocol implementation
│   ├── FidoCertificates.cpp            # X.509 EC P-256 Batch Attestation Certificate
│   ├── YubiKey.cpp                     # Yubico Management, OATH Applet & HMAC-SHA1
│   └── USBHIDFIDO.cpp                  # CTAPHID USB transport & CCID SmartCard
├── partitions.csv                      # Partition table (OTA0: 3MB, OTA1: 3MB, LittleFS: 9MB)
└── platformio.ini                      # PlatformIO configuration
```

---

## 5. Quick Start Guide

### Step 1: Build and Flash Firmware
Connect the ESP32-S3 board to your PC via the **native USB port** and run:

```bash
# 1. Build and flash firmware binary
pio run -t upload

# 2. Upload Web UI files to LittleFS (required)
pio run -t uploadfs
```

### Step 2: Access the Web Dashboard
1. Connect to the Wi-Fi AP (`ESP32-HID-Console`, Password: `Password123`) or access via Home Wi-Fi Station IP.
2. Open your browser to `http://esp32-hid.local` (or `http://192.168.4.1`).
3. Default credentials:
   - **Username**: `admin`
   - **Password**: `admin123`

---

## 6. Complete Documentation Index

### Hardware Security & Passkeys
* [**FIDO2 / WebAuthn User Guide**](docs/FIDO2_PASSKEY_USER_GUIDE.md) — Step-by-step passkey registration, Android/Windows Hello pairing, and PIN management.
* [**FIDO2 & CTAP2 Technical Specification**](docs/FIDO2_PASSKEY_SPECIFICATION.md) — Low-level packet structures, cryptographic primitives, and state machines.
* [**YubiKey 5A Emulation Guide**](docs/YUBIKEY_EMULATION_GUIDE.md) — `ykman` setup, ISO 7816 OATH applet, KeePassXC sync, and CCID SmartCard details.

### HID Injection & Automation
* [**Ducky Script 2.0 & Typing Engine Guide**](docs/DUCKY_SCRIPT_GUIDE.md) — Ducky syntax, modifier combinations, and typing engine tuning parameters.
* [**Web KVM, Macro Replay & Absolute Mouse**](docs/KVM_AND_AUTOMATION_GUIDE.md) — Universal cross-platform client, `.krec` binary macros, and absolute coordinates.
* [**Virtual USB Storage & Web OTA Updates**](docs/USB_STORAGE_AND_OTA_GUIDE.md) — Virtual FAT12 drive, automatic synchronization, and drag-and-drop firmware flashing.

### Integration & System
* [**REST API Reference**](docs/REST_API_REFERENCE.md) — Complete endpoint reference for remote scripting, real-time HID injection, and device telemetry.
* [**Settings & Hardware Reboot Guide**](docs/SETTINGS_REBOOT_GUIDE.md) — Real-time vs reboot configuration reference.
