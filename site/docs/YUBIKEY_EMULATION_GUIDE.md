# YubiKey 5A Emulation & Hardware Security Key Guide

The ESP32-S3 firmware includes a **complete, standalone Yubico YubiKey 5A Series hardware security engine** alongside standard FIDO2 WebAuthn Passkey and BadUSB HID capabilities.

---

## 1. How is this Different from a Standard FIDO2 Key?

| Capability | Standard FIDO2 Key | This YubiKey 5A Device |
| :--- | :--- | :--- |
| **Passkeys / WebAuthn** | Basic `makeCredential` / `getAssertion` | Full CTAP 2.1 Passkeys + `hmac-secret` + `largeBlobs` (2048B) + `credProtect` |
| **Attestation** | Self-attestation only | **X.509 Attestation Certificate (`x5c`)** + Self-attestation (`packed`) |
| **Yubico Management Applet** | Not available | Recognized natively by **`ykman` (v5.9.2)** as `YubiKey 5A (5.4.3)` |
| **OATH Authenticator (2FA)** | Not available | ISO 7816 OATH Applet for **Yubico Authenticator** Desktop & Mobile |
| **HMAC Challenge-Response** | Not available | **20-byte HMAC-SHA1** for **KeePassXC** / BitLocker physical unlocking |
| **Dual Physical Slots** | Not available | **Slot 1** (Short touch typing) & **Slot 2** (Long touch challenge-response) |
| **USB Interfaces** | Single HID Interface | **Triple Interface**: Mass Storage (`0x08`) + FIDO HID (`0x03`) + **CCID Smart Card (`0x0B`)** |

---

## 2. The 3 Hardware Operating Modes

The device operates in 3 distinct modes indicated by the onboard RGB status LED:

| Mode | USB Identity (VID:PID) | Status LED | Primary Features |
| :--- | :--- | :--- | :--- |
| **1. Normal HID Mode** | Custom / Spoofed (e.g. `Logitech`) | **Solid Green** `(0, 255, 0)` | • Ducky script injection<br>• Web KVM Bridge (`esp32_kvm.py`)<br>• 2 MB USB Virtual Drive (`DUCKY_DRIVE`)<br>• Touch button injects Slot 1 (OTP / Password) |
| **2. Standard Passkey Mode** | `0x10C4:0x8A2A`<br>*(FIDO Alliance)* | **Solid Cyan** `(0, 180, 255)` | • Dedicated FIDO2 / CTAP2 Security Key<br>• Universal Passkey login (Google, GitHub, Apple, Microsoft, Windows Hello) |
| **3. YubiKey 5A Mode** | `0x1050:0x0407`<br>*(Yubico)* | **Solid Dark Purple** `(120, 0, 180)` | • Spoofs official **YubiKey 5A Series FIDO+CCID**<br>• Full FIDO2 / CTAP 2.1 Passkey support with X.509 `x5c`<br>• Yubico Management Applet (`ykman` compatible)<br>• Yubico Authenticator (OATH-TOTP) Applet<br>• Dual-Slot Touch OTP (Modhex & Static Password)<br>• Native CCID Smart Card (`0x0B`) |

### How to Switch Modes:
1. **Toggle HID Mode ⇄ Passkey Mode**:
 - Hold the physical **BOOT button (GPIO 0)** on your board for **2.5 seconds**, or click the toggle button in the Web UI.
2. **Select Standard Profile vs YubiKey Profile** (when in Passkey Mode):
 - In the **Vault** tab (`🔐 Vault`), click **"Switch to YubiKey Mode"** or **"Switch to Standard Mode"**.
 - Reboot the device to apply the new USB descriptor.

---

## 3. Supported Yubico Protocols & Specifications

### A. USB Identity Spoofing
When YubiKey mode is active, the ESP32 presents genuine Yubico USB descriptors:
- **Vendor ID (VID)**: `0x1050` (Yubico)
- **Product ID (PID)**: `0x0407` (YubiKey 5 Series FIDO+CCID)
- **Manufacturer**: `Yubico`
- **Product Name**: `YubiKey FIDO+CCID`
- **Serial Number**: `17869661`
- **Firmware Version**: `5.4.3`

### B. Yubico Management Applet (`A0 00 00 05 27 47 11 17`)
- Implements standard ISO 7816-4 APDU commands:
 - **`SELECT APPLET`**: Returns firmware version `5.4.3`.
 - **`GET DEVICE INFO (0x1D)` & `CTAP_READ_CONFIG (0xC2 / 0x42)`**: Returns full TLV descriptor containing:
 - `TAG_USB_SUPPORTED` (`0x01`): `0x03FF` (All capabilities supported)
 - `TAG_SERIAL` (`0x02`): `17869661`
 - `TAG_USB_ENABLED` (`0x03`): `0x03FF` (All capabilities enabled)
 - `TAG_FORM_FACTOR` (`0x04`): `0x01` (USB-A Keychain)
 - `TAG_VERSION` (`0x05`): `5.4.3`
 - `TAG_DEVICE_FLAGS` (`0x08`): `0x00`
- **Verification**: Fully recognized by **`YubiKey Manager (ykman 5.9.2)`** CLI:
 ```bash
 $ ykman info
 Device type: YubiKey 5A
 Serial number: 17869661
 Firmware version: 5.4.3
 Form factor: Keychain (USB-A)
 Enabled USB interfaces: OTP, FIDO, CCID
 ```

### C. Dedicated X.509 Batch Attestation Certificate Chain
- Embedded genuine Yubico-style X.509 EC P-256 Batch Attestation Certificate:
 - **Subject**: `CN=YubiKey 5A Serial 17869661, OU=Authenticator Attestation, O=Yubico AB, C=SE`
 - **Issuer**: `CN=Yubico Root CA, OU=Authenticator Attestation, O=Yubico AB, C=SE`
 - **Serial**: `17869661`
 - **Signature Algorithm**: `ecdsa-with-SHA256` (`secp256r1`)
 - **Validity Period**: `2020-01-01` to `2045-01-01`
- Supports both standard self-attestation (`fmt: "packed"`) and enterprise direct certificate attestation (`x5c` certificate array in `attStmt`).

### D. Yubico OATH Authenticator Applet (`A0 00 00 05 27 21 01`)
- Implements RFC 6238 TOTP and RFC 4226 HOTP 2FA engine:
 - **`SELECT (0xA4)`**: Returns OATH version tag `0x79` and device salt `0x71`.
 - **`LIST (0xA1 / 0x0A)`**: Enumerates stored account names and algorithm types.
 - **`CALCULATE (0xA2 / 0x04)`**: Calculates full 20-byte HMAC (Tag `0x75`) or truncated 6-digit TOTP code (Tag `0x76`).
 - **`PUT (0x01)` & `DELETE (0x02)`**: Adds or removes 2FA accounts in hardware storage.
- **Compatibility**: Directly compatible with **Yubico Authenticator** desktop and mobile apps, as well as the Web Vault interface.

### E. Dual-Slot Physical Touch Trigger
Physical YubiKey touch slots are emulated via the onboard **`BOOT` button**:
- **Slot 1 (Short Tap < 1s)**:
 - **Yubico OTP Mode**: Generates authentic 44-character Modhex string (`12-char Public ID + 32-char AES-128 encrypted payload with inverted CRC-16, session counter, and timestamp`).
 - **Static Password Mode**: Automatically types your master password + Enter key.
 - **Disabled**: Pass-through.
- **Slot 2 (Long Press >2.5s)**:
 - Configurable for **20-byte HMAC-SHA1 Challenge-Response** (KeePassXC / BitLocker) or secondary static password.

### F. KeePassXC Challenge-Response (HMAC-SHA1)
- Implements 20-byte HMAC-SHA1 challenge-response matching genuine YubiKey Slot 2 (INS `0x38`).
- Fully compatible with **KeePassXC** password database physical key unlocking.

### G. Native USB CCID SmartCard (`Class 0x0B`) Interface
- Implemented USB Class `0x0B` CCID descriptor with Bulk-IN (`0x82`) and Bulk-OUT (`0x02`) endpoints.
- Fully recognized by Linux PC/SC daemon `pcscd` (`Yubico YubiKey OTP+FIDO+CCID`).
- Handles `PC_to_RDR_IccPowerOn` (ATR `3B 8D 01 80 F2 80 00 ...`), `PC_to_RDR_GetSlotStatus`, and `PC_to_RDR_XfrBlock`.
- CTAPHID Vendor Tunnel: Dispatches vendor commands `0xC0`, `0xC1`, `0xC2` directly between CTAPHID and the APDU router.

---

## 4. Web UI Vault Configuration

In the Web UI under the **Vault** tab (`http://192.168.4.1`):

1. **Security Key Profile Selector**:
 - Shows active mode badge and a one-click switch button.
2. **Slot 1 & Slot 2 Configurator**:
 - Select mode: `Yubico OTP`, `Static Password`, or `Disabled`.
 - Set custom Modhex Public ID (e.g. `cccccccccccb`).
 - Click ** Test Type** to test typing the token over USB keyboard directly into any text field.
3. **Yubico Authenticator Accounts**:
 - View all stored 2FA accounts with live countdown progress circles and real-time codes.
 - Click **+ Add 2FA Secret** to import accounts with Base32 secret keys.
 - One-click copy or instant typing into host forms.

---

## 5. REST API Reference

| Method | Endpoint | Description |
| :--- | :--- | :--- |
| `GET` | `/api/yubikey/status` | Returns profile mode, serial number, use counter, and slot configurations. |
| `POST` | `/api/yubikey/mode?enabled=1` | Switches between Standard FIDO2 profile and YubiKey 5 profile. |
| `POST` | `/api/yubikey/slot/config` | Saves Slot 1 or Slot 2 parameters (mode, public ID, secret key, static password). |
| `POST` | `/api/yubikey/slot/test?slot=1` | Injects the configured slot token via USB keyboard. |
| `GET` | `/api/yubikey/oath/accounts` | Lists all stored 2FA accounts with live calculated codes. |
| `POST` | `/api/yubikey/oath/add` | Adds a new 2FA account (`name`, `secret`, `issuer`, `digits`, `period`). |
| `POST` | `/api/yubikey/oath/delete` | Deletes an OATH account by name. |
| `GET` | `/api/vault/credentials` | Lists stored WebAuthn / FIDO2 Passkeys. |
| `POST` | `/api/vault/credentials/delete` | Deletes a stored Passkey credential. |
