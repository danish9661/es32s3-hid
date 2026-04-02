import threading
from clipboard_typer import read_clipboard_text, text_to_keystrokes

class InputState:
    """Thread-safe input state - updated by hooks and Raw Input."""

    def __init__(self):
        self.lock = threading.Lock()
        self.sequence: int = 0

        # KVM toggle
        self.kvm_active: bool = False

        # Mouse (accumulated by Raw Input, reset by sender)
        self.mouse_buttons: int = 0
        self.mouse_dx: int = 0
        self.mouse_dy: int = 0
        self.mouse_wheel: int = 0
        self.mouse_pan: int = 0
        self.mouse_buttons_prev: int = 0  # For change detection

        # Keyboard
        self.modifiers: int = 0
        self.pressed_keys: set[int] = set()
        self.kbd_dirty: bool = False  # Keyboard state change flag

        # Consumer (multimedia / browser)
        self.consumer_usage: int = 0  # Currently pressed consumer usage (0 = none)
        self.consumer_dirty: bool = False

        # Clipboard paste
        self.pasting: bool = False
        self.paste_chars: list[tuple[int, int]] = []  # (hid_keycode, modifiers)
        self.paste_index: int = 0
        self.paste_phase: int = 0  # 0=press, 1=release
        self.paste_tick_counter: int = 0  # To pace typing

    def next_seq(self) -> int:
        self.sequence = (self.sequence + 1) & 0xFFFFFFFF
        return self.sequence

    def reset_state(self):
        """Resets the state when KVM is disabled."""
        self.modifiers = 0
        self.pressed_keys.clear()
        self.mouse_buttons = 0
        self.mouse_dx = 0
        self.mouse_dy = 0
        self.mouse_wheel = 0
        self.mouse_pan = 0
        self.kbd_dirty = True
        self.consumer_usage = 0
        self.consumer_dirty = True
        self.cancel_paste()

    def start_paste(self) -> bool:
        """Attempts to start pacing the clipboard text. Returns True if successful."""
        self.modifiers = 0
        self.pressed_keys.clear()
        self.kbd_dirty = True
        text = read_clipboard_text()
        if text:
            chars = text_to_keystrokes(text)
            if chars:
                self.paste_chars = chars
                self.paste_index = 0
                self.paste_phase = 0
                self.paste_tick_counter = 0
                self.pasting = True
                print(f"[PASTE] {len(chars)} keystroke(s) queued")
                return True
            else:
                print("[PASTE] No typeable characters in clipboard")
        else:
            print("[PASTE] Clipboard empty or no text")
        return False

    def cancel_paste(self):
        """Cancels any active paste operation."""
        if self.pasting:
            done = self.paste_index
            total = len(self.paste_chars)
            self.pasting = False
            self.paste_chars = []
            self.paste_index = 0
            self.paste_tick_counter = 0
            self.kbd_dirty = True
            print(f"[PASTE] Cancelled ({done}/{total})")
