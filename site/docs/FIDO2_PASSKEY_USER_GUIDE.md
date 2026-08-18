# ESP32-S3 FIDO2 / WebAuthn Hardware Passkey User Guide

A complete guide to using the **ESP32-S3 Hardware Security Key & Encrypted Vault**, including passkey registration, mode switching, PIN management, biometric UV emulation, and zero-knowledge backups.

---

## Table of Contents
1. [Overview & Capabilities](#1-overview--capabilities)
2. [Operating Modes & How to Switch](#2-operating-modes--how-to-switch)
3. [Registering Your First Passkey](#3-registering-your-first-passkey)
4. [Logging In with Passkeys](#4-logging-in-with-passkeys)
5. [Managing Security Key PIN](#5-managing-security-key-pin)
6. [Biometric UV Emulation Toggle](#6-biometric-uv-emulation-toggle)
7. [Inspecting Passkey Metadata](#7-inspecting-passkey-metadata)
8. [Encrypted Vault & Passkey Backup (.esp32vault)](#8-encrypted-vault--passkey-backup-esp32vault)
9. [Android & Windows Hello Compatibility Notes](#9-android--windows-hello-compatibility-notes)
10. [Troubleshooting & FAQs](#10-troubleshooting--faqs)

---

## 1. Overview & Capabilities

Your ESP32-S3 acts as a high-security physical hardware authenticator implementing the **W3C WebAuthn** and **FIDO Alliance CTAP 2.0 / 2.1** standards.

```
┌─────────────────────────────────────────────────────────────┐
│ ESP32-S3 Hardware Passkey │
├──────────────────────────────┬──────────────────────────────┤
│ Cryptographic Engine │ 💾 Storage & Persistence │
├──────────────────────────────┼──────────────────────────────┤
│ • NIST P-256 (secp256r1) ECC │ • Hardware Resident Passkeys │
│ • SHA-256 Hashing │ • Dual-Layer Flash + NVS │
│ • HMAC-Secret (WebAuthn PRF) │ • Survives LittleFS Flashing │
│ • AES-256-GCM Zero-Knowledge │ • Monotonic Signature Counter│
└──────────────────────────────┴──────────────────────────────┘
```

### Supported Services:
* **Google** (Accounts, Gmail, Cloud)
* **GitHub** & **GitLab**
* **Microsoft Accounts** (Windows Hello, Azure AD, Outlook)
* **Apple ID** (iCloud WebAuthn)
* **Bitwarden & 1Password** (Full PRF vault encryption support)
* **AWS, Cloudflare, Fastmail, Dropbox, and all WebAuthn-compliant sites**

---

## 2. Operating Modes & How to Switch

The device has two distinct USB operating profiles:

| USB Profile | LED Color | Features Active | Best Used For |
| :--- | :---: | :--- | :--- |
| ** Dedicated Passkey Mode** | 🟦 **Neon Cyan** | Standalone FIDO2 Security Key only (Keyboard/Mouse disabled for anti-BadUSB security isolation) | Passkey logins on Google, GitHub, Android, Windows |
| **💻 Normal Ducky Mode** | 🟩 **Neon Green** | Ducky Keyboard, Mouse, Web Console, MSC Storage | Rubber Ducky payloads, Typing Engine, KVM |

### How to Switch Modes:
1. **Physical Button Gesture (Instant)**:
 * **Hold the physical `BOOT` button on the board for 2.5 seconds**. The RGB LED will flash and switch modes automatically!
2. **Web Dashboard**:
 * Open `http://esp32-hid.local` → **Vault** tab.
 * Click **` Switch to Passkey Mode`** or **`💻 Switch to Normal Ducky Mode`**.

---

## 3. Registering Your First Passkey

### Testing via WebAuthn.io:
1. Ensure the device is in ** Dedicated Passkey Mode** (or switch to it).
2. Open [https://webauthn.io](https://webauthn.io) in Chrome, Edge, Safari, or Firefox.
3. Enter a username (e.g. `danish`).
4. Click **Register**:
 * Your browser will display a security key prompt: *"Insert your security key and touch it"*.
 * The RGB LED on your ESP32 will pulse Cyan.
5. **Tap the physical `BOOT` button on the ESP32 board** to authorize registration.
6. The browser confirms: *"Registration successful!"*

### Registering on Real Services (Google / GitHub):
1. Go to **GitHub Settings** → **Password and authentication** → **Passkeys** → **Add a passkey**.
2. When prompted by your operating system, choose **"Security key"** or **"External device"**.
3. **Tap the `BOOT` button** on the ESP32 to confirm creation.

---

## 4. Logging In with Passkeys

1. On any registered website (e.g. GitHub login), click **"Sign in with a passkey"**.
2. Select your security key.
3. When the browser prompts for touch, **tap the physical `BOOT` button** on the ESP32.
4. You are instantly logged in without typing any password!

---

## 5. Managing Security Key PIN

To prevent unauthorized physical use if your key is lost, you can configure a hardware PIN:

1. Open `http://esp32-hid.local` → **Vault** tab.
2. Click **` Manage PIN`** (or ` Set PIN`).
3. Enter a **4 to 8 digit PIN** and confirm.
4. **PIN Protection**:
 * When a PIN is set, websites requiring User Verification (UV) will ask you to enter the PIN before the `BOOT` touch is accepted.
 * The ESP32 enforces an **8-retry hardware lockout**. If the wrong PIN is entered 8 times consecutively, passkey operations are locked until factory reset.
5. **Removing PIN**:
 * Click **` Change / Remove PIN`** → Click **`🔓 Remove PIN`** to return to standard PIN-free operation.

---

## 6. Biometric UV Emulation Toggle

Some corporate/enterprise websites mandate on-device biometric User Verification (UV).

```
┌─────────────────────────────────────────────────────────────┐
│ Emulate Biometric Verification (UV = true) [ SWITCH ON ] │
│ When enabled, BOOT button touch fulfills biometric User │
│ Verification (UV) without requiring a PIN. Dynamic 0ms! │
└─────────────────────────────────────────────────────────────┘
```

* **Standard Mode (`UV=false`)**: Reports as a standard roaming physical key. Complies strictly with Android Google Play Services and standard roaming flows.
* **Emulated Mode (`UV=true`)**: Reports as a biometric key. Tapping the `BOOT` button sets the `USER_VERIFIED` flag, allowing you to bypass PIN prompts on strict corporate sites!
* **0ms Dynamic Switching**: Flip the toggle switch anytime in the Vault tab — **no reboot is required**.

---

## 7. Inspecting Passkey Metadata

Inside the Vault tab, every registered passkey includes an **`👁 Details`** button. Clicking it opens the deep inspection modal:

* **Relying Party Domain**: Website domain (e.g. `github.com`).
* **Username & Display Name**: Account identity stored on hardware.
* **Signature Counter**: Monotonic login counter protecting against replay attacks.
* **Credential ID**: Full 32-byte hex identifier with 1-click Copy button.
* **Public Key Coordinates**: NIST P-256 $(X, Y)$ curve coordinates registered on the server.
* **PRF / HMAC Secret**: Indicates whether WebAuthn PRF encryption is active (used by Bitwarden/1Password).

---

## 8. Encrypted Vault & Passkey Backup (`.esp32vault`)

You can export a zero-knowledge, military-grade encrypted backup of all your 2FA TOTP accounts, saved passwords, and FIDO2 passkeys (including private keys and counters).

### Exporting a Backup:
1. Unlock the Vault on `http://esp32-hid.local`.
2. Scroll to the **"💾 Encrypted Vault & Passkey Backup"** card.
3. Click **` Download Backup (.esp32vault)`**.
4. The ESP32 derives an AES-256 key from your Master Password using **PBKDF2-HMAC-SHA256 (100,000 iterations)** and encrypts the bundle with **AES-256-GCM**.
5. The browser downloads `esp32_hardware_vault_backup_YYYY-MM-DD.esp32vault`.

### Restoring a Backup:
1. Click **` Upload & Restore`** and select your `.esp32vault` file.
2. Enter the **Master Password** that was used when the backup was created.
3. Click **"Restore Everything"**.
4. The ESP32 verifies the GCM authentication tag and restores all 2FA accounts and passkeys into flash + NVS flash partitions!

---

## 9. Android & Windows Hello Compatibility Notes

* **Android Mobile (USB OTG / Type-C)**:
 * In **Dedicated Passkey Mode**, connect your ESP32-S3 to your Android phone via USB-C.
 * When logging in via Chrome or Google Play Services, Android detects the ESP32 as a physical FIDO2 security key.
 * Keep **Biometric UV Emulation OFF (`UV=false`)** when using Android Google Play Services so Android does not expect on-device fingerprint sensor hardware.
* **Windows Hello**:
 * Windows Hello fully supports both `UV=false` (prompts for PIN or presence) and `UV=true` (bypasses Windows PIN when `BOOT` button is tapped).

---

## 10. Troubleshooting & FAQs

#### Q: I uploaded `littlefs.bin` or ran `uploadfs`. Did I lose my passkeys?
**No!** Our firmware features **Dual NVS Flash Persistence**. All passkeys are automatically backed up into the non-volatile NVS flash partition (`fido_nvs`), so formatting or uploading LittleFS never erases your passkeys.

#### Q: The website says "Security key not recognized"?
* Ensure you are in ** Dedicated Passkey Mode** (Neon Cyan LED).
* If testing on a local server, WebAuthn requires **HTTPS** or `localhost`. (Use [https://webauthn.io](https://webauthn.io) or an HTTPS reverse proxy).

#### Q: Why are the "Type" buttons in the software vault greyed out in Passkey Mode?
* In **Dedicated Passkey Mode**, the USB Keyboard interface is disabled to satisfy operating system security isolation (Anti-BadUSB).
* To type passwords automatically via USB HID, switch to **`💻 Normal Ducky Mode`**, or simply use the **`Copy`** button!
