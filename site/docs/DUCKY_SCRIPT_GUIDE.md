# Ducky Script 2.0 & Keystroke Injection Engine

The ESP32-S3 firmware includes a comprehensive Ducky Script 2.0 and BadUSB keystroke injection engine executed directly on FreeRTOS Core 1 with 8 MB Octal PSRAM streaming.

---

## 1. Execution Modes

1. **Web Live Console Execution**: Write and execute scripts live in the browser editor.
2. **Flash Storage Execution (`/scripts`)**: Save scripts directly on the 9 MB LittleFS partition for repeatable autonomous execution.
3. **USB Mass Storage Staging**: Stage scripts to `PAYLOAD.PS1` on the virtual USB drive (`DUCKY_DRIVE`).

---

## 2. Command Reference

### Basic Commands
| Command | Description | Example |
| :--- | :--- | :--- |
| `REM` | Comment / remarks (ignored) | `REM This is a comment` |
| `STRING <text>` | Types text using configured typing delay and burst parameters | `STRING Hello World!` |
| `STRINGLN <text>` | Types text followed by an Enter keystroke | `STRINGLN notepad.exe` |
| `DELAY <ms>` | Pauses script execution for specified milliseconds | `DELAY 500` |
| `DEFAULT_DELAY <ms>` | Sets default delay between every subsequent command | `DEFAULT_DELAY 50` |
| `REPEAT <n>` | Repeats the immediately preceding command `n` times | `REPEAT 5` |
| `BLOCK` ... `ENDBLOCK` | Injects raw multi-line text at maximum hardware throughput | `BLOCK`<br>`Write-Output 'Hello'`<br>`ENDBLOCK` |

---

### Hardware Absolute Mouse Commands
| Command | Description | Example |
| :--- | :--- | :--- |
| `MOUSE_MOVE_ABS <x%> <y%>` | Moves cursor to exact screen percentage (0..100%) | `MOUSE_MOVE_ABS 50 50` |
| `MOUSE_CLICK_ABS <btn> <x%> <y%>` | Moves and clicks (`left`, `right`, `middle`) at exact percentage | `MOUSE_CLICK_ABS left 50 50` |

---

### Navigation & Function Keys
* `ENTER` / `RETURN`
* `TAB`
* `ESC` / `ESCAPE`
* `BACKSPACE`
* `DELETE` / `DEL`
* `INSERT` / `INS`
* `UP` / `DOWN` / `LEFT` / `RIGHT` (or `UPARROW`, `DOWNARROW`, `LEFTARROW`, `RIGHTARROW`)
* `PAGEUP` / `PAGEDOWN`
* `HOME` / `END`
* `CAPSLOCK` / `NUMLOCK` / `SCROLLLOCK`
* `PRINTSCREEN`
* `PAUSE` / `BREAK`
* `APP` / `MENU`
* `SPACE`
* `F1` through `F12`

---

### Modifier Combinations
Combine modifiers (`CTRL`, `ALT`, `SHIFT`, `GUI` / `WINDOWS`) with standard keys:

```text
GUI r
DELAY 300
STRINGLN powershell
DELAY 1000
CTRL ALT DELETE
CTRL SHIFT ESC
ALT F4
CTRL+ALT+T
GUI SHIFT S
```

---

## 3. Typing Engine Tuning Guide

To ensure error-free keystroke delivery across diverse host operating systems, virtual machines, and slow terminals, the engine provides 4 tunable timing parameters in the Web UI:

| Parameter | Recommended Range | Purpose |
| :--- | :---: | :--- |
| **Typing Delay** | `2 - 8 ms` | Inter-character delay. Lower values increase typing speed. |
| **Burst Characters** | `15 - 30` | Number of continuous keystrokes sent before taking a short rest. |
| **Burst Pause** | `8 - 16 ms` | Micro-pause between bursts allowing the host OS keyboard buffer to flush. |
| **Newline Pause** | `30 - 60 ms` | Extra settling delay after Enter/Return for shells (cmd, powershell, bash) to process commands. |

### Timing Presets

| Profile | Typing Delay | Burst Chars | Burst Pause | Newline Pause | Best Used For |
| :--- | :---: | :---: | :---: | :---: | :--- |
| **Reliable (Default)** | 5 ms | 20 | 14 ms | 50 ms | Universal stability across all platforms |
| **High Speed** | 2 ms | 28 | 8 ms | 30 ms | Fast modern PCs and direct terminals |
| **Slow Target / VM** | 10 ms | 12 | 20 ms | 80 ms | Virtual machines, remote desktop, legacy BIOS |
