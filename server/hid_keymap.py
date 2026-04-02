"""
Mapping of Windows Virtual Key codes (VK_*) to HID Usage IDs.

Keyboard/Keypad Page (0x07) — regular keys
Consumer Page (0x0C) — multimedia, browser, and system keys

Source:  USB HID Usage Tables 1.4
         https://usb.org/document-library/hid-usage-tables-14
"""

# ═══════════════════════════════════════════════════════════════════
#  Modifiers - return bit number (0-7) in the modifiers byte
#  bit0=LCtrl  bit1=LShift  bit2=LAlt  bit3=LGUI
#  bit4=RCtrl  bit5=RShift  bit6=RAlt  bit7=RGUI
# ═══════════════════════════════════════════════════════════════════

VK_MODIFIER_MAP: dict[int, int] = {
    # Specific left/right
    0xA2: 0,   # VK_LCONTROL  → bit 0  (Left Ctrl)
    0xA0: 1,   # VK_LSHIFT    → bit 1  (Left Shift)
    0xA4: 2,   # VK_LMENU     → bit 2  (Left Alt)
    0x5B: 3,   # VK_LWIN      → bit 3  (Left GUI)
    0xA3: 4,   # VK_RCONTROL  → bit 4  (Right Ctrl)
    0xA1: 5,   # VK_RSHIFT    → bit 5  (Right Shift)
    0xA5: 6,   # VK_RMENU     → bit 6  (Right Alt / AltGr)
    0x5C: 7,   # VK_RWIN      → bit 7  (Right GUI)

    # Generic (fallback - when system doesn't distinguish L/R)
    0x11: 0,   # VK_CONTROL   → bit 0  (Left Ctrl by default)
    0x10: 1,   # VK_SHIFT     → bit 1  (Left Shift by default)
    0x12: 2,   # VK_MENU      → bit 2  (Left Alt by default)
}

# ═══════════════════════════════════════════════════════════════════
#  Regular keys - VK code → HID Usage ID (Keyboard/Keypad Page)
# ═══════════════════════════════════════════════════════════════════

VK_TO_HID: dict[int, int] = {

    # ── Letters A-Z ───────────────────────────────────────────────────
    0x41: 0x04,  # A
    0x42: 0x05,  # B
    0x43: 0x06,  # C
    0x44: 0x07,  # D
    0x45: 0x08,  # E
    0x46: 0x09,  # F
    0x47: 0x0A,  # G
    0x48: 0x0B,  # H
    0x49: 0x0C,  # I
    0x4A: 0x0D,  # J
    0x4B: 0x0E,  # K
    0x4C: 0x0F,  # L
    0x4D: 0x10,  # M
    0x4E: 0x11,  # N
    0x4F: 0x12,  # O
    0x50: 0x13,  # P
    0x51: 0x14,  # Q
    0x52: 0x15,  # R
    0x53: 0x16,  # S
    0x54: 0x17,  # T
    0x55: 0x18,  # U
    0x56: 0x19,  # V
    0x57: 0x1A,  # W
    0x58: 0x1B,  # X
    0x59: 0x1C,  # Y
    0x5A: 0x1D,  # Z

    # ── Digits 0-9 (top row) ─────────────────────────────────────────
    0x30: 0x27,  # 0  → HID 0x27 (0 and ))
    0x31: 0x1E,  # 1  → HID 0x1E (1 and !)
    0x32: 0x1F,  # 2
    0x33: 0x20,  # 3
    0x34: 0x21,  # 4
    0x35: 0x22,  # 5
    0x36: 0x23,  # 6
    0x37: 0x24,  # 7
    0x38: 0x25,  # 8
    0x39: 0x26,  # 9

    # ── Function keys F1-F12 ────────────────────────────────────────
    0x70: 0x3A,  # F1
    0x71: 0x3B,  # F2
    0x72: 0x3C,  # F3
    0x73: 0x3D,  # F4
    0x74: 0x3E,  # F5
    0x75: 0x3F,  # F6
    0x76: 0x40,  # F7
    0x77: 0x41,  # F8
    0x78: 0x42,  # F9
    0x79: 0x43,  # F10
    0x7A: 0x44,  # F11
    0x7B: 0x45,  # F12

    # ── Function keys F13-F24 ───────────────────────────────────────
    0x7C: 0x68,  # F13
    0x7D: 0x69,  # F14
    0x7E: 0x6A,  # F15
    0x7F: 0x6B,  # F16
    0x80: 0x6C,  # F17
    0x81: 0x6D,  # F18
    0x82: 0x6E,  # F19
    0x83: 0x6F,  # F20
    0x84: 0x70,  # F21
    0x85: 0x71,  # F22
    0x86: 0x72,  # F23
    0x87: 0x73,  # F24

    # ── Special keys ─────────────────────────────────────────────────
    0x0D: 0x28,  # VK_RETURN       → Enter
    0x1B: 0x29,  # VK_ESCAPE       → Escape
    0x08: 0x2A,  # VK_BACK         → Backspace
    0x09: 0x2B,  # VK_TAB          → Tab
    0x20: 0x2C,  # VK_SPACE        → Space
    0x14: 0x39,  # VK_CAPITAL      → Caps Lock
    0x2C: 0x46,  # VK_SNAPSHOT     → Print Screen
    0x91: 0x47,  # VK_SCROLL       → Scroll Lock
    0x13: 0x48,  # VK_PAUSE        → Pause/Break
    0x90: 0x53,  # VK_NUMLOCK      → Num Lock

    # ── Navigation ────────────────────────────────────────────────────
    0x2D: 0x49,  # VK_INSERT       → Insert
    0x24: 0x4A,  # VK_HOME         → Home
    0x21: 0x4B,  # VK_PRIOR        → Page Up
    0x2E: 0x4C,  # VK_DELETE       → Delete Forward
    0x23: 0x4D,  # VK_END          → End
    0x22: 0x4E,  # VK_NEXT         → Page Down

    # ── Arrow keys ───────────────────────────────────────────────────
    0x27: 0x4F,  # VK_RIGHT        → Right Arrow
    0x25: 0x50,  # VK_LEFT         → Left Arrow
    0x28: 0x51,  # VK_DOWN         → Down Arrow
    0x26: 0x52,  # VK_UP           → Up Arrow

    # ── Numpad ───────────────────────────────────────────────────
    0x6F: 0x54,  # VK_DIVIDE       → Keypad /
    0x6A: 0x55,  # VK_MULTIPLY     → Keypad *
    0x6D: 0x56,  # VK_SUBTRACT     → Keypad -
    0x6B: 0x57,  # VK_ADD          → Keypad +
    # Numpad Enter: handled in server.py as VK_RETURN + extended → 0x58
    0x60: 0x62,  # VK_NUMPAD0      → Keypad 0
    0x61: 0x59,  # VK_NUMPAD1      → Keypad 1
    0x62: 0x5A,  # VK_NUMPAD2      → Keypad 2
    0x63: 0x5B,  # VK_NUMPAD3      → Keypad 3
    0x64: 0x5C,  # VK_NUMPAD4      → Keypad 4
    0x65: 0x5D,  # VK_NUMPAD5      → Keypad 5
    0x66: 0x5E,  # VK_NUMPAD6      → Keypad 6
    0x67: 0x5F,  # VK_NUMPAD7      → Keypad 7
    0x68: 0x60,  # VK_NUMPAD8      → Keypad 8
    0x69: 0x61,  # VK_NUMPAD9      → Keypad 9
    0x6E: 0x63,  # VK_DECIMAL      → Keypad .
    0x6C: 0x85,  # VK_SEPARATOR    → Keypad Comma (international)

    # ── Punctuation (US layout / OEM) ───────────────────────────────
    0xBD: 0x2D,  # VK_OEM_MINUS    → - and _
    0xBB: 0x2E,  # VK_OEM_PLUS     → = and +
    0xDB: 0x2F,  # VK_OEM_4        → [ and {
    0xDD: 0x30,  # VK_OEM_6        → ] and }
    0xDC: 0x31,  # VK_OEM_5        → \ and |
    0xBA: 0x33,  # VK_OEM_1        → ; and :
    0xDE: 0x34,  # VK_OEM_7        → ' and "
    0xC0: 0x35,  # VK_OEM_3        → ` and ~
    0xBC: 0x36,  # VK_OEM_COMMA    → , and <
    0xBE: 0x37,  # VK_OEM_PERIOD   → . and >
    0xBF: 0x38,  # VK_OEM_2        → / and ?
    0xE2: 0x64,  # VK_OEM_102      → Non-US \ and | (key next to LShift on ISO)
    0xDF: 0x38,  # VK_OEM_8        → varies by layout (mapped to / as fallback)

    # ── Application / context / system ──────────────────────────────
    0x5D: 0x65,  # VK_APPS         → Application (Context Menu)
    0x03: 0x7B,  # VK_CANCEL       → Cancel (Ctrl+Break) → HID Cancel
    0x0C: 0x9C,  # VK_CLEAR        → Clear (Numpad 5 without NumLock)
    0x29: 0x77,  # VK_SELECT       → Select
    0x2A: 0x74,  # VK_PRINT        → Execute (Print key, not PrintScreen)
    0x2B: 0x74,  # VK_EXECUTE      → Execute
    0x2F: 0x75,  # VK_HELP         → Help
    0x5F: 0x66,  # VK_SLEEP        → Power (closest HID mapping)

    # ── International keys (IME / Japanese / Korean) ───
    0x15: 0x90,  # VK_KANA / VK_HANGUL  → LANG1 (Hangul/English toggle)
    0x17: 0x91,  # VK_JUNJA             → LANG2 (Hanja conversion)
    0x19: 0x92,  # VK_HANJA / VK_KANJI  → LANG3
    0x1C: 0x8A,  # VK_CONVERT           → International4 (Henkan)
    0x1D: 0x8B,  # VK_NONCONVERT        → International5 (Muhenkan)
    0x18: 0x8B,  # VK_FINAL             → International5 (fallback)
    0x1F: 0x8C,  # VK_MODECHANGE        → International6 (Keypad Comma JP)

    # ── Legacy / rare keys ───────────────────────────────────────────
    0xF6: 0x9A,  # VK_ATTN              → SysReq/Attention
    0xF7: 0xA4,  # VK_CRSEL             → CrSel/Props
    0xF8: 0xA5,  # VK_EXSEL             → ExSel
    0xF9: 0xA6,  # VK_EREOF             → Erase-EOF
    0xFE: 0x9C,  # VK_OEM_CLEAR         → Clear
}

# ═══════════════════════════════════════════════════════════════════
#  Consumer keys - VK code → HID Consumer Usage ID (Page 0x0C)
#  Sent as a separate Consumer Control HID report.
# ═══════════════════════════════════════════════════════════════════

VK_TO_CONSUMER: dict[int, int] = {

    # ── Volume ───────────────────────────────────────────────────────
    0xAD: 0x00E2,  # VK_VOLUME_MUTE       → Mute
    0xAE: 0x00EA,  # VK_VOLUME_DOWN       → Volume Decrement
    0xAF: 0x00E9,  # VK_VOLUME_UP         → Volume Increment

    # ── Media transport ──────────────────────────────────────────────
    0xB0: 0x00B5,  # VK_MEDIA_NEXT_TRACK  → Scan Next Track
    0xB1: 0x00B6,  # VK_MEDIA_PREV_TRACK  → Scan Previous Track
    0xB2: 0x00B7,  # VK_MEDIA_STOP        → Stop
    0xB3: 0x00CD,  # VK_MEDIA_PLAY_PAUSE  → Play/Pause

    # ── Browser ──────────────────────────────────────────────────────
    0xA6: 0x0224,  # VK_BROWSER_BACK      → AC Back
    0xA7: 0x0225,  # VK_BROWSER_FORWARD   → AC Forward
    0xA8: 0x0227,  # VK_BROWSER_REFRESH   → AC Refresh
    0xA9: 0x0226,  # VK_BROWSER_STOP      → AC Stop
    0xAA: 0x0221,  # VK_BROWSER_SEARCH    → AC Search
    0xAB: 0x022A,  # VK_BROWSER_FAVORITES → AC Bookmarks
    0xAC: 0x0223,  # VK_BROWSER_HOME      → AC Home

    # ── Launch applications ──────────────────────────────────────────
    0xB4: 0x018A,  # VK_LAUNCH_MAIL       → AL Email Reader
    0xB5: 0x0183,  # VK_LAUNCH_MEDIA_SELECT → AL Consumer Control Config
    0xB6: 0x0194,  # VK_LAUNCH_APP1       → AL Local Machine Browser
    0xB7: 0x0192,  # VK_LAUNCH_APP2       → AL Calculator
}

# ═══════════════════════════════════════════════════════════════════
#  Mapping count verification
# ═══════════════════════════════════════════════════════════════════

_total_mappings = len(VK_MODIFIER_MAP) + len(VK_TO_HID) + len(VK_TO_CONSUMER)
assert _total_mappings >= 150, (
    f"Expected >=150 mappings, got {_total_mappings}"
)
