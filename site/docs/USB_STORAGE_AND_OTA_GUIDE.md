# Virtual USB Mass Storage (DUCKY_DRIVE) & Web OTA Updates

The ESP32-S3 firmware includes a 2 MB virtual USB Mass Storage Class (MSC) driver and comprehensive Over-The-Air (OTA) update mechanisms.

---

## 1. Virtual USB Drive (`DUCKY_DRIVE`)

When in Normal HID Mode, the ESP32 presents a **2 MB FAT12 virtual USB flash drive** to the plugged-in computer.

```
Target PC (Windows / Linux / macOS)
        │
        ▼ (USB Mass Storage 0x08)
┌────────────────────────────────────────┐
│     DUCKY_DRIVE (2 MB FAT12 RAM-Disk)   │
├────────────────────────────────────────┤
│ • esp32_kvm.py  (KVM Client Tool)      │
│ • PAYLOAD.PS1   (Staged Script)        │
│ • RUN.BAT       (Execution Launcher)   │
│ • README.TXT    (Drive Documentation)  │
└────────────────────────────────────────┘
        ▲
        │ (Journaled Flash Sync)
LittleFS Flash Partition (/usb_drive/)
```

### Architecture Highlights:
* **LittleFS Flash Source of Truth**: Files reside as real files in the `/usb_drive/` directory within the 9 MB LittleFS partition.
* **No Boot Flash Wear**: In Passkey/YubiKey mode or when MSC is disabled, no PSRAM is allocated and no disk loading occurs.
* **Automatic Host File Sync**: Dropping files onto `DUCKY_DRIVE` from Windows Explorer or Linux file managers automatically synchronizes the files back into LittleFS flash after a debounce timer.
* **Web UI Staging & Dynamic Rebuild**: Click **Stage Script to USB** in the web editor to deploy the active script directly into `PAYLOAD.PS1`.

---

## 2. Drag-and-Drop USB Firmware Flashing

You can update the ESP32-S3 firmware by dragging and dropping `firmware.bin` directly onto the `DUCKY_DRIVE` USB disk from your computer's file manager.

### How It Works:
1. Copy `firmware.bin` to `DUCKY_DRIVE`.
2. The onboard driver detects the Espressif flash image header magic byte (`0xE9`).
3. The internal OTA updater streams the binary directly into the inactive OTA partition (`app0` or `app1`).
4. Once verified, the device sets the active boot partition and restarts with the new firmware.

---

## 3. Web-Based OTA Updates

Update firmware or web UI assets over Wi-Fi without physical access or USB cables:

```bash
# 1. Upload new application firmware binary
curl -X POST -F "file=@.pio/build/esp32-s3-n16r8/firmware.bin" \
  -H "Cookie: sid=<your-session-cookie>" \
  http://esp32-hid.local/api/ota

# 2. Upload LittleFS Web UI filesystem binary
curl -X POST -F "file=@.pio/build/esp32-s3-n16r8/littlefs.bin" \
  -H "Cookie: sid=<your-session-cookie>" \
  "http://esp32-hid.local/api/ota?type=littlefs"
```

The Web UI also provides direct drag-and-drop file upload fields under the **Settings** tab.
