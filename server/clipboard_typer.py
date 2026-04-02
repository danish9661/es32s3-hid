"""
Clipboard text paste support for ESP32-S3 KVM.

Reads Windows clipboard and converts text characters to HID keystroke
sequences (US keyboard layout).

Characters outside the supported set are silently skipped.
"""

import ctypes
import ctypes.wintypes as wintypes

# ═══════════════════════════════════════════════════════════════════
#  Character → HID Keycode Mapping (US Layout)
# ═══════════════════════════════════════════════════════════════════

_SHIFT = 0x02  # Left Shift modifier bit
_RALT  = 0x40  # Right Alt (AltGr) modifier bit

# char → (hid_keycode, modifiers)
CHAR_TO_HID: dict[str, tuple[int, int]] = {
    # ── Whitespace ────────────────────────────────────────────────
    ' ':  (0x2C, 0),       # Space
    '\t': (0x2B, 0),       # Tab
    '\n': (0x28, 0),       # Enter

    # ── Digits (top row) ─────────────────────────────────────────
    '1': (0x1E, 0),  '!': (0x1E, _SHIFT),
    '2': (0x1F, 0),  '@': (0x1F, _SHIFT),
    '3': (0x20, 0),  '#': (0x20, _SHIFT),
    '4': (0x21, 0),  '$': (0x21, _SHIFT),
    '5': (0x22, 0),  '%': (0x22, _SHIFT),
    '6': (0x23, 0),  '^': (0x23, _SHIFT),
    '7': (0x24, 0),  '&': (0x24, _SHIFT),
    '8': (0x25, 0),  '*': (0x25, _SHIFT),
    '9': (0x26, 0),  '(': (0x26, _SHIFT),
    '0': (0x27, 0),  ')': (0x27, _SHIFT),

    # ── Letters ───────────────────────────────────────────────────
    'a': (0x04, 0),  'A': (0x04, _SHIFT),
    'b': (0x05, 0),  'B': (0x05, _SHIFT),
    'c': (0x06, 0),  'C': (0x06, _SHIFT),
    'd': (0x07, 0),  'D': (0x07, _SHIFT),
    'e': (0x08, 0),  'E': (0x08, _SHIFT),
    'f': (0x09, 0),  'F': (0x09, _SHIFT),
    'g': (0x0A, 0),  'G': (0x0A, _SHIFT),
    'h': (0x0B, 0),  'H': (0x0B, _SHIFT),
    'i': (0x0C, 0),  'I': (0x0C, _SHIFT),
    'j': (0x0D, 0),  'J': (0x0D, _SHIFT),
    'k': (0x0E, 0),  'K': (0x0E, _SHIFT),
    'l': (0x0F, 0),  'L': (0x0F, _SHIFT),
    'm': (0x10, 0),  'M': (0x10, _SHIFT),
    'n': (0x11, 0),  'N': (0x11, _SHIFT),
    'o': (0x12, 0),  'O': (0x12, _SHIFT),
    'p': (0x13, 0),  'P': (0x13, _SHIFT),
    'q': (0x14, 0),  'Q': (0x14, _SHIFT),
    'r': (0x15, 0),  'R': (0x15, _SHIFT),
    's': (0x16, 0),  'S': (0x16, _SHIFT),
    't': (0x17, 0),  'T': (0x17, _SHIFT),
    'u': (0x18, 0),  'U': (0x18, _SHIFT),
    'v': (0x19, 0),  'V': (0x19, _SHIFT),
    'w': (0x1A, 0),  'W': (0x1A, _SHIFT),
    'x': (0x1B, 0),  'X': (0x1B, _SHIFT),
    'y': (0x1C, 0),  'Y': (0x1C, _SHIFT),
    'z': (0x1D, 0),  'Z': (0x1D, _SHIFT),

    # ── Punctuation (US layout) ──────────────────────────────────
    '-': (0x2D, 0),  '_': (0x2D, _SHIFT),
    '=': (0x2E, 0),  '+': (0x2E, _SHIFT),
    '[': (0x2F, 0),  '{': (0x2F, _SHIFT),
    ']': (0x30, 0),  '}': (0x30, _SHIFT),
    '\\': (0x31, 0), '|': (0x31, _SHIFT),
    ';': (0x33, 0),  ':': (0x33, _SHIFT),
    "'": (0x34, 0),  '"': (0x34, _SHIFT),
    '`': (0x35, 0),  '~': (0x35, _SHIFT),
    ',': (0x36, 0),  '<': (0x36, _SHIFT),
    '.': (0x37, 0),  '>': (0x37, _SHIFT),
    '/': (0x38, 0),  '?': (0x38, _SHIFT),

    # ── Polish layout (AltGr + letter) ───────────────
    'ą': (0x04, _RALT),  'Ą': (0x04, _RALT | _SHIFT),   # AltGr + a/A
    'ć': (0x06, _RALT),  'Ć': (0x06, _RALT | _SHIFT),   # AltGr + c/C
    'ę': (0x08, _RALT),  'Ę': (0x08, _RALT | _SHIFT),   # AltGr + e/E
    'ł': (0x0F, _RALT),  'Ł': (0x0F, _RALT | _SHIFT),   # AltGr + l/L
    'ń': (0x11, _RALT),  'Ń': (0x11, _RALT | _SHIFT),   # AltGr + n/N
    'ó': (0x12, _RALT),  'Ó': (0x12, _RALT | _SHIFT),   # AltGr + o/O
    'ś': (0x16, _RALT),  'Ś': (0x16, _RALT | _SHIFT),   # AltGr + s/S
    'ź': (0x1B, _RALT),  'Ź': (0x1B, _RALT | _SHIFT),   # AltGr + x/X
    'ż': (0x1D, _RALT),  'Ż': (0x1D, _RALT | _SHIFT),   # AltGr + z/Z
}


def text_to_keystrokes(text: str) -> list[tuple[int, int]]:
    """Convert text to a list of (hid_keycode, modifiers) tuples.

    Handles \\r\\n, \\n, and standalone \\r as Enter.
    Unsupported characters are silently skipped.
    """
    text = text.replace('\r\n', '\n').replace('\r', '\n')
    result: list[tuple[int, int]] = []
    skipped = 0
    for ch in text:
        entry = CHAR_TO_HID.get(ch)
        if entry:
            result.append(entry)
        else:
            skipped += 1
    if skipped:
        print(f"[PASTE] Warning: {skipped} unsupported character(s) skipped")
    return result


# ═══════════════════════════════════════════════════════════════════
#  Windows Clipboard Access
# ═══════════════════════════════════════════════════════════════════

CF_UNICODETEXT = 13

_user32 = ctypes.windll.user32
_kernel32 = ctypes.windll.kernel32

_user32.OpenClipboard.argtypes = [wintypes.HWND]
_user32.OpenClipboard.restype = wintypes.BOOL
_user32.GetClipboardData.argtypes = [wintypes.UINT]
_user32.GetClipboardData.restype = wintypes.HANDLE
_user32.CloseClipboard.argtypes = []
_user32.CloseClipboard.restype = wintypes.BOOL

_kernel32.GlobalLock.argtypes = [wintypes.HANDLE]
_kernel32.GlobalLock.restype = ctypes.c_void_p
_kernel32.GlobalUnlock.argtypes = [wintypes.HANDLE]
_kernel32.GlobalUnlock.restype = wintypes.BOOL


def read_clipboard_text() -> str | None:
    """Read Unicode text from the Windows clipboard.

    Returns None if the clipboard doesn't contain text or can't be opened.
    """
    if not _user32.OpenClipboard(None):
        return None
    try:
        handle = _user32.GetClipboardData(CF_UNICODETEXT)
        if not handle:
            return None
        ptr = _kernel32.GlobalLock(handle)
        if not ptr:
            return None
        try:
            return ctypes.wstring_at(ptr)
        finally:
            _kernel32.GlobalUnlock(handle)
    finally:
        _user32.CloseClipboard()
