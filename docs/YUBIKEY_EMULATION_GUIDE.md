# YubiKey 5 Emulation & Hardware Security Key Guide

The ESP32-S3 firmware includes a **modular, standalone Yubico YubiKey 5 Series emulation engine** alongside standard FIDO2 WebAuthn and BadUSB HID capabilities.

---

## 1. The 3 Hardware Operating Modes

The device operates in 3 distinct modes indicated by the onboard RGB status LED:

| Mode | USB Identity (VID:PID) | Status LED | Primary Features |
| :--- | :--- | :--- | :--- |
| **1. Normal HID Mode** | Custom / Spoofed (e.g. `Logitech`) | 🟢 **Solid Green** `(0, 255, 0)` | • Ducky script injection<br>• Web KVM Bridge (`esp32_kvm.py`)<br>• 2 MB USB Virtual Drive (`DUCKY_DRIVE`)<br>• Touch button injects Slot 1 (OTP / Password) |
| **2. Standard Passkey Mode** | `0x10C4:0x8A2A`<br>*(FIDO Alliance)* | 🔷 **Solid Cyan** `(0, 180, 255)` | • Dedicated FIDO2 / CTAP2 Security Key<br>• Universal Passkey login (Google, GitHub, Apple, Microsoft, Windows Hello) |
| **3. YubiKey 5 Mode** | `0x1050:0x0407`<br>*(Yubico)* | 🟣 **Solid Dark Purple** `(120, 0, 180)` | • Spoofs official **YubiKey 5 Series FIDO+CCID**<br>• Full FIDO2 / CTAP2 Passkey support<br>• Yubico Management Applet (`ykman` compatible)<br>• Yubico Authenticator (OATH-TOTP) Applet<br>• Dual-Slot Touch OTP (44-char Modhex & Static) |

### How to Switch Modes:
1. **Toggle HID Mode ⇄ Passkey Mode**:
   - Hold the physical **BOOT button (GPIO 0)** on your board for **2.5 seconds**, or click the toggle button in the Web UI.
2. **Select Standard Profile vs YubiKey Profile** (when in Passkey Mode):
   - In the **Vault** tab (`🔐 Vault`), click **"Switch to YubiKey Mode"** or **"Switch to Standard Mode"**.
   - Reboot the device to apply the new USB descriptor.

---

## 2. Supported Yubico Protocols & Specifications

### A. USB Identity Spoofing
When YubiKey mode is active, the ESP32 presents genuine Yubico USB descriptors:
- **Vendor ID (VID)**: `0x1050` (Yubico)
- **Product ID (PID)**: `0x0407` (YubiKey 5 Series FIDO+CCID)
- **Manufacturer**: `Yubico`
- **Product Name**: `YubiKey FIDO+CCID`
- **Serial Number**: Formatted hardware MAC address / custom serial (e.g. `17869661`)

### B. Yubico Management Applet (`A0 00 00 05 27 47 11 17`)
- Implements standard ISO 7816-4 APDU commands:
  - **`SELECT APPLET`**: Returns firmware version `5.4.3`.
  - **`GET DEVICE INFO (0x1D)` & `READ CONFIG (0x1E)`**: Returns full TLV descriptor containing:
    - Firmware version: `5.4.3`
    - Serial number (4 bytes big-endian)
    - Form factor: `0x01` (USB-A Keychain)
    - Supported Capabilities: `OTP | U2F | FIDO2 | OATH | OPENPGP | PIV` (`0x00FF`)
    - Enabled Capabilities: `OTP | FIDO2 | OATH` (`0x00A7`)
- **Compatibility**: Fully recognized by **`YubiKey Manager (ykman)`** CLI and GUI.

### C. Yubico OATH Authenticator Applet (`A0 00 00 05 27 21 01`)
- Implements RFC 6238 TOTP and RFC 4226 HOTP 2FA engine:
  - **`SELECT (0xA4)`**: Returns OATH version tag `0x79` and device salt `0x71`.
  - **`LIST (0xA1 / 0x0A)`**: Enumerates stored account names and algorithm types.
  - **`CALCULATE (0xA2 / 0x04)`**: Calculates truncated 6-digit or 8-digit TOTP code for timestamp.
  - **`PUT (0x01)` & `DELETE (0x02)`**: Adds or removes 2FA accounts in hardware storage.
- **Compatibility**: Directly compatible with **Yubico Authenticator** desktop and mobile apps, as well as the Web Vault interface.

### D. Dual-Slot Physical Touch Trigger
Physical YubiKey touch slots are emulated via the onboard **`BOOT` button**:
- **Slot 1 (Short Tap)**:
  - **Yubico OTP Mode**: Generates authentic 44-character Modhex string (`12-char Public ID + 32-char AES-128 encrypted payload with inverted CRC-16, session counter, and timestamp`).
  - **Static Password Mode**: Automatically types your master password + Enter key.
  - **Disabled**: Pass-through.
- **Slot 2 (Long Press >2.5s)**:
  - Configurable for static password or secondary OTP payload.

### E. KeePassXC Challenge-Response (HMAC-SHA1)
- Implements 20-byte HMAC-SHA1 challenge-response matching genuine YubiKey Slot 2.
- Fully compatible with **KeePassXC** password database physical key unlocking.

### F. CCID & CTAPHID Vendor Transports
- **Native CCID (`Class 0x0B`)**: Responds to `PC_to_RDR_IccPowerOn` (ATR `3B 8D 01 80 F2 80 00 ...`), `PC_to_RDR_GetSlotStatus`, and `PC_to_RDR_XfrBlock`.
- **CTAPHID Vendor Tunnel**: Dispatches vendor commands `0xC0`, `0xC1`, `0xC2` directly between CTAPHID and the APDU router for driverless host communication.

---

## 3. Web UI Vault Configuration

In the Web UI under the **Vault** tab:

1. **Security Key Profile Selector**:
   - Shows active mode badge and a one-click switch button.
2. **Slot 1 & Slot 2 Configurator**:
   - Select mode: `Yubico OTP`, `Static Password`, or `Disabled`.
   - Set custom Modhex Public ID (e.g. `cccccccccccb`).
   - Click **⌨️ Test Type** to test typing the token over USB keyboard directly into any text field.
3. **Yubico Authenticator Accounts**:
   - View all stored 2FA accounts with live countdown progress circles and real-time codes.
   - Click **+ Add 2FA Secret** to import accounts with Base32 secret keys.
   - One-click copy or instant typing into host forms.

---

## 4. REST API Reference

| Method | Endpoint | Description |
| :--- | :--- | :--- |
| `GET` | `/api/yubikey/status` | Returns profile mode, serial number, use counter, and slot configurations. |
| `POST` | `/api/yubikey/mode?enabled=1` | Switches between Standard FIDO2 profile and YubiKey 5 profile. |
| `POST` | `/api/yubikey/slot/config` | Saves Slot 1 or Slot 2 parameters (mode, public ID, secret key, static password). |
| `POST` | `/api/yubikey/slot/test?slot=1` | Injects the configured slot token via USB keyboard. |
| `GET` | `/api/yubikey/oath/accounts` | Lists all stored 2FA accounts with live calculated codes. |
| `POST` | `/api/yubikey/oath/add` | Adds a new 2FA account (`name`, `secret`, `issuer`, `digits`, `period`). |
| `POST` | `/api/yubikey/oath/delete?name=...` | Deletes a 2FA account by name. |

---

## 5. Visual LED Reference

```text
[🟢 Normal HID Mode]      Solid Green     (0, 255, 0)
[🔷 Standard Passkey]     Solid Cyan      (0, 180, 255)
[🟣 YubiKey 5 Mode]       Dark Purple     (120, 0, 180)

[⚠️ Touch Prompt]         Rapid Blink     Magenta (Register) / Cyan (Login) / Amber (Reset)
[✅ Touch Confirmed]      Lime Flash      (0, 255, 60) -> returns to mode color
```
