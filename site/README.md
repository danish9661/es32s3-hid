# ESP32-S3 HID Console, FIDO2 Passkey & YubiKey 5A Security Hub

<p align="center">
  <img src="https://img.shields.io/badge/Hardware-ESP32--S3%20N16R8-2563eb?style=flat-square" alt="ESP32-S3 N16R8">
  <img src="https://img.shields.io/badge/USB-Triple%20Interface%20(MSC%20+%20HID%20+%20CCID)-0891b2?style=flat-square" alt="USB Triple Interface">
  <img src="https://img.shields.io/badge/Security-FIDO2%20%7C%20CTAP%202.1%20%7C%20WebAuthn-059669?style=flat-square" alt="FIDO2 WebAuthn">
  <img src="https://img.shields.io/badge/Yubico-YubiKey%205A%20Emulation%20(v5.4.3)-7c3aed?style=flat-square" alt="YubiKey 5A">
  <img src="https://img.shields.io/badge/KVM-Ultra--Low%20Latency%20UDP-d97706?style=flat-square" alt="KVM Bridge">
</p>

---

## Overview

The **ESP32-S3 HID Console & Security Hub** is a multi-role USB hardware security key, keystroke injection platform, and ultra-low latency KVM bridge designed for the **ESP32-S3-DevKitC-1-N16R8** (16 MB Quad-SPI Flash, 8 MB Octal PSRAM).

It unifies three major subsystems into a single hardware controller:
1. **FIDO2 / WebAuthn Hardware Security Key (CTAP 2.1)** with hardware touch confirmation, PIN protection, and dedicated X.509 Batch Attestation certificates.
2. **Yubico YubiKey 5A Security Engine** with native `ykman 5.9.2` management, ISO 7816 OATH (TOTP/HOTP) 2FA applet, KeePassXC 20-byte HMAC-SHA1 challenge-response, and dual touch slots.
3. **BadUSB & Low-Latency Web KVM Bridge** with 2 MB USB Virtual Storage (`DUCKY_DRIVE`), Ducky Script 2.0 parser, and cross-platform mouse/keyboard streaming client (`esp32_kvm.py`).

---

## Feature Matrix

| Feature Subsystem | Standard Keystroke Dongle | Standard FIDO2 Token | This ESP32-S3 Security Hub |
| :--- | :---: | :---: | :---: |
| **Passkeys / WebAuthn** | None | Basic CTAP 2.0 | **Full CTAP 2.1** (`rk`, `up`, `uv`, `minPinLength: 4..63`, `credProtect`) |
| **HMAC Secret / PRF** | None | Rare | **HMAC-Secret Extension** (1Password / Bitwarden offline vault encryption) |
| **Large Blobs Storage** | None | None | **2048-Byte Large Blobs** (SSH certificates & metadata storage) |
| **Attestation** | None | Self-Attested | **X.509 EC P-256 Batch Attestation Certificate Chain (`x5c`)** |
| **YubiKey Management** | None | None | **Natively detected by official `ykman 5.9.2`** as `YubiKey 5A (5.4.3)` |
| **OATH 2FA Authenticator** | None | None | **Yubico Authenticator Applet** with LittleFS flash storage (up to 32 accounts) |
| **HMAC Challenge-Response**| None | None | **20-byte HMAC-SHA1** for KeePassXC database physical unlock |
| **Dual OTP Touch Slots** | None | None | **Slot 1** (Short tap password type) & **Slot 2** (Long hold challenge-response) |
| **Native USB CCID** | None | None | **Class 0x0B SmartCard Descriptor** recognized by Linux `pcscd` & Windows SC |
| **Web KVM & Automation** | Basic Keystrokes | None | **16-byte UDP Bridge (`esp32_kvm.py`)**, Macro Replayer, Absolute Mouse |
| **Encrypted Backup** | None | None | **AES-256-GCM Encrypted `.esp32vault` export/import** + Dual NVS backup |

---

## Operating Modes

The device operates in 3 distinct profiles switchable via the Web Dashboard or by **holding the physical BOOT button (GPIO 0) for 2.5 seconds** (reboots automatically in 500ms):

| Mode | USB Identity (VID:PID) | LED Status | Functionality & Interfaces |
| :--- | :--- | :---: | :--- |
| **1. Normal Ducky Mode** | Custom / Spoofed (e.g. `Logitech`) | Solid Green | Ducky script injection, Web KVM, 2 MB USB Mass Storage (`DUCKY_DRIVE`), Slot 1 OTP typing |
| **2. Standard Passkey Mode** | `0x10C4:0x8A2A` (FIDO Alliance) | Solid Cyan | Standalone FIDO2 security key (Chrome, Edge, Safari, Firefox, Windows Hello, Android) |
| **3. YubiKey 5A Mode** | `0x1050:0x0407` (Yubico) | Solid Dark Purple | Official Yubico 5A emulation, `ykman` management, OATH 2FA applet, KeePassXC challenge-response, CCID smartcard |

---

## FIDO2 & WebAuthn Technical Specifications

The FIDO2 engine implements complete **Client-to-Authenticator Protocol (CTAP 2.0 / 2.1)** and **W3C WebAuthn Level 3**:

```text
USB HID Endpoints (Usage Page: 0xF1D0, Usage: 0x01)
├── CTAPHID Transport Layer (64-byte framing)
│   ├── 0x86 CTAPHID_INIT       (Nonce echo, CID assignment, protocol v2)
│   ├── 0x81 CTAPHID_PING       (Liveness & latency calibration)
│   ├── 0x88 CTAPHID_WINK       (Visual LED blink identification)
│   ├── 0x90 CTAPHID_CBOR       (Encapsulated CTAP2 command payload)
│   ├── 0x83 CTAPHID_MSG        (Legacy U2F fallback & vendor tunneling)
│   ├── 0x91 CTAPHID_CANCEL     (Asynchronous cancellation)
│   └── 0xBB CTAPHID_KEEPALIVE  (User presence periodic heartbeat)
└── CTAP2 Application Layer
    ├── 0x04 authenticatorGetInfo
    ├── 0x01 authenticatorMakeCredential (NIST P-256 key generation, packed / x5c attestation)
    ├── 0x02 authenticatorGetAssertion  (Touch-gated assertion signature)
    ├── 0x06 authenticatorClientPIN     (PIN Protocol 1: ECDH P-256 + AES-256-CBC + HMAC-SHA256)
    ├── 0x07 authenticatorReset         (Hardware-gated factory wipe)
    ├── 0x0A authenticatorCredentialManagement (Enumerate RPs, discoverable credentials, deletion)
    └── 0x0C authenticatorLargeBlobs    (2048 bytes flash/RAM storage)
```

### X.509 Hardware Attestation Certificate Chain
The firmware embeds a hardware-signed X.509 EC P-256 Batch Attestation Certificate returned during direct/enterprise registration:
* **Subject**: `CN=YubiKey 5A Serial 17869661, OU=Authenticator Attestation, O=Yubico AB, C=SE`
* **Issuer**: `CN=Yubico Root CA, OU=Authenticator Attestation, O=Yubico AB, C=SE`
* **Serial Number**: `17869661`
* **Curve & Algorithm**: `NIST P-256 (secp256r1)` with `ECDSA-SHA256`
* **Validity Period**: `2020-01-01` to `2045-01-01`

---

## Yubico YubiKey 5A Emulation Architecture

### 1. Official YubiKey Manager (`ykman 5.9.2`) CLI Output
```bash
$ ykman list
YubiKey 5A (5.4.3) [OTP+FIDO+CCID] Serial: 17869661

$ ykman info
Device type: YubiKey 5A
Serial number: 17869661
Firmware version: 5.4.3
Form factor: Keychain (USB-A)
Enabled USB interfaces: OTP, FIDO, CCID

Applications
Yubico OTP      Enabled
FIDO U2F        Enabled
FIDO2           Enabled
OATH            Enabled
PIV             Enabled
OpenPGP         Enabled
YubiHSM Auth    Enabled

$ ykman fido info
PIN:                8 attempt(s) remaining
Minimum PIN length: 4
```

### 2. ISO 7816-4 APDU & CCID SmartCard Routing
* **Management Applet (`A0 00 00 05 27 47 11 17`)**: Responds to `SELECT` and `CTAP_READ_CONFIG` (`0xC2` / `0x42`) with full TLV capabilities descriptors.
* **OATH Applet (`A0 00 00 05 27 21 01`)**: Implements `SELECT` (Tag `0x79` version, Tag `0x71` salt), `LIST`, `CALCULATE` (Tag `0x75` 20-byte HMAC or Tag `0x76` TOTP), `PUT`, `DELETE`.
* **KeePassXC HMAC-SHA1 Challenge-Response**: Generates deterministic 20-byte HMAC-SHA1 digests matching genuine Slot 2 responses (INS `0x38`).
* **Native USB CCID Driver (`Class 0x0B`)**: Dynamic TinyUSB driver with Bulk-IN (`0x82`) and Bulk-OUT (`0x02`) endpoints recognized by `pcscd` smart card daemon.

---

## Web KVM & BadUSB Automation Engine

* **Universal KVM Client (`server/esp32_kvm.py`)**:
  - Single-file standalone Python client under 30 KB.
  - Native Wayland Linux `evdev` integration, Windows Win32 API, and macOS Quartz.
  - Low-latency 16-byte UDP protocol (`0xCAFE`) with sequence resync and safety watchdogs.
* **Hardware Absolute Mouse (`0..32767`)**:
  - Resolution-independent coordinate mapping. `(0,0)` is always top-left across all monitor scalings and multi-head setups.
* **Ducky Script 2.0 Engine**:
  - Supports `REPEAT`, `BLOCK...ENDBLOCK`, delays, functional keys (`F1`..`F12`), multimedia keys, and custom typing profiles.

---

## Zero-Knowledge Encrypted Vault & Backup

The Web Dashboard features an encrypted hardware vault protected by **PBKDF2-HMAC-SHA256** (100,000 iterations) and **AES-256-GCM**:
* **Live 2FA TOTP Generator**: Real-time rotating codes with countdown timers and instant USB keyboard auto-fill.
* **Zero-Knowledge Encrypted Backup (`.esp32vault`)**: 1-click encrypted export and import of all Passkeys, OATH secrets, and KeePassXC configurations.
* **Dual NVS Flash Persistence**: Hardware NVS mirroring guarantees passkey survival across LittleFS filesystem reflashing and OTA updates.
* **BIP-39 24-Word Recovery Phrase**: Deterministic paper seed backup generated via hardware TRNG.

---

## Quick Start Guide

### 1. Build and Flash via PlatformIO
```bash
# Clone the repository
git clone https://github.com/danish9661/es32s3-hid.git
cd es32s3-hid

# Build and flash firmware
pio run -t upload

# Flash LittleFS WebUI filesystem
pio run -t uploadfs
```

### 2. Access the Web Dashboard
1. Connect to the ESP32 Wi-Fi AP (`ESP32-HID-Console`, Password: `Password123`) or via Home Wi-Fi STA IP.
2. Open your browser to `http://esp32-hid.local` (or `http://192.168.4.1`).
3. Default credentials:
   - **Username**: `admin`
   - **Password**: `admin123`

---

## Documentation Index

* [**FIDO2 / WebAuthn User Guide**](docs/FIDO2_PASSKEY_USER_GUIDE.md) — Step-by-step passkey registration, Android/Windows Hello pairing, and PIN management.
* [**FIDO2 & CTAP2 Technical Specification**](docs/FIDO2_PASSKEY_SPECIFICATION.md) — Low-level packet structures, cryptographic primitives, and state machines.
* [**YubiKey 5A Emulation Guide**](docs/YUBIKEY_EMULATION_GUIDE.md) — `ykman` setup, ISO 7816 OATH applet, KeePassXC sync, and CCID SmartCard details.
* [**Settings & Hardware Reboot Guide**](docs/SETTINGS_REBOOT_GUIDE.md) — Real-time vs reboot configuration reference.
