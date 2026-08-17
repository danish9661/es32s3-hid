#!/usr/bin/env python3
"""
=============================================================================
ESP32-S3 Wireless KVM Client (Universal: Windows, Linux, macOS)
=============================================================================
A standalone, single-file zero-latency KVM transmitter, Action Recorder,
Macro Replay Engine, and screenshot server.
Transmits USB HID keyboard & mouse events to the ESP32-S3 over low-latency UDP.

Features:
  - 100% Cross-Platform (Windows, Linux, macOS)
  - Native Linux evdev Kernel Support (Works on Wayland & X11)
  - Zero-latency 16-byte binary UDP protocol (0xCAFE)
  - Configurable hotkey toggle (F8 toggle KVM, F9 toggle Record)
  - Full Mouse & Keyboard Macro Recorder (.txt Action Files)
  - Direct Action File Upload & Hardware Replay on ESP32
  - Direct Clipboard Text Typer (Ctrl+Alt+V)
  - Built-in live screen preview HTTP server (port 8080) for ESP32 Web Console
  - Ultra-lightweight: Single file (< 30 KB)
=============================================================================
"""

import sys
import os
import time
import socket
import struct
import threading
import argparse
import platform
import subprocess

# -----------------------------------------------------------------------------
# Binary Protocol Constants
# -----------------------------------------------------------------------------
PACKET_MAGIC   = 0xCAFE
DEFAULT_PORT   = 4210
PACKET_SIZE    = 16

EVENT_MOUSE    = 0x01
EVENT_KEYBOARD = 0x02
EVENT_CONSUMER = 0x03

# Struct formats: Header(8 bytes) + Payload(8 bytes) = 16 bytes (Little-Endian)
FMT_MOUSE    = '<HIBxBhhbbx'
FMT_KEYBOARD = '<HIBxBB6s'
FMT_CONSUMER = '<HIBxH6x'

# -----------------------------------------------------------------------------
# USB HID Keycode & Modifier Tables
# -----------------------------------------------------------------------------
MOD_LCTRL  = 0x01
MOD_LSHIFT = 0x02
MOD_LALT   = 0x04
MOD_LMETA  = 0x08  # Win / Cmd
MOD_RCTRL  = 0x10
MOD_RSHIFT = 0x20
MOD_RALT   = 0x40
MOD_RMETA  = 0x80

# Linux evdev keycode to USB HID keycode mapping
EVDEV_TO_HID = {
    1: 0x29,   # KEY_ESC -> 0x29
    2: 0x1E, 3: 0x1F, 4: 0x20, 5: 0x21, 6: 0x22, 7: 0x23, 8: 0x24, 9: 0x25, 10: 0x26, 11: 0x27, # 1-9, 0
    12: 0x2D, 13: 0x2E, 14: 0x2A, 15: 0x2B, # MINUS, EQUAL, BACKSPACE, TAB
    16: 0x14, 17: 0x1A, 18: 0x08, 19: 0x15, 20: 0x17, 21: 0x1C, 22: 0x18, 23: 0x0C, 24: 0x12, 25: 0x13, # Q W E R T Y U I O P
    26: 0x2F, 27: 0x30, 28: 0x28, # LEFTBRACE, RIGHTBRACE, ENTER
    29: MOD_LCTRL, # KEY_LEFTCTRL (Modifier handled separately)
    30: 0x04, 31: 0x16, 32: 0x07, 33: 0x09, 34: 0x0A, 35: 0x0B, 36: 0x0D, 37: 0x0E, 38: 0x0F, # A S D F G H J K L
    39: 0x33, 40: 0x34, 41: 0x35, # SEMICOLON, APOSTROPHE, GRAVE
    42: MOD_LSHIFT, # KEY_LEFTSHIFT
    43: 0x31, 44: 0x1D, 45: 0x1B, 46: 0x06, 47: 0x19, 48: 0x05, 49: 0x11, 50: 0x10, # BACKSLASH, Z X C V B N M
    51: 0x36, 52: 0x37, 53: 0x38, # COMMA, DOT, SLASH
    54: MOD_RSHIFT, 55: 0x55, 56: MOD_LALT, 57: 0x2C, 58: 0x39, # KEY_RIGHTSHIFT, KPASTERISK, LEFTALT, SPACE, CAPSLOCK
    59: 0x3A, 60: 0x3B, 61: 0x3C, 62: 0x3D, 63: 0x3E, 64: 0x3F, 65: 0x40, 66: 0x41, 67: 0x42, 68: 0x43, # F1 - F10
    69: 0x53, 70: 0x47, 87: 0x44, 88: 0x45, # NUMLOCK, SCROLLLOCK, F11, F12
    97: MOD_RCTRL, 100: MOD_RALT, 102: 0x4A, 103: 0x52, 104: 0x4B, 105: 0x50, 106: 0x4F, 107: 0x4D, 108: 0x51, 109: 0x4E, 110: 0x49, 111: 0x4C, # HOME, UP, PAGEUP, LEFT, RIGHT, END, DOWN, PAGEDOWN, INSERT, DELETE
    119: 0x48, 125: MOD_LMETA, 126: MOD_RMETA # PAUSE, LEFTMETA, RIGHTMETA
}

EVDEV_MODIFIERS = {
    29: MOD_LCTRL, 97: MOD_RCTRL,
    42: MOD_LSHIFT, 54: MOD_RSHIFT,
    56: MOD_LALT, 100: MOD_RALT,
    125: MOD_LMETA, 126: MOD_RMETA
}

CHAR_TO_HID = {
    'a': (0x04, False), 'b': (0x05, False), 'c': (0x06, False), 'd': (0x07, False),
    'e': (0x08, False), 'f': (0x09, False), 'g': (0x0A, False), 'h': (0x0B, False),
    'i': (0x0C, False), 'j': (0x0D, False), 'k': (0x0E, False), 'l': (0x0F, False),
    'm': (0x10, False), 'n': (0x11, False), 'o': (0x12, False), 'p': (0x13, False),
    'q': (0x14, False), 'r': (0x15, False), 's': (0x16, False), 't': (0x17, False),
    'u': (0x18, False), 'v': (0x19, False), 'w': (0x1A, False), 'x': (0x1B, False),
    'y': (0x1C, False), 'z': (0x1D, False),
    'A': (0x04, True),  'B': (0x05, True),  'C': (0x06, True),  'D': (0x07, True),
    'E': (0x08, True),  'F': (0x09, True),  'G': (0x0A, True),  'H': (0x0B, True),
    'I': (0x0C, True),  'J': (0x0D, True),  'K': (0x0E, True),  'L': (0x0F, True),
    'M': (0x10, True),  'N': (0x11, True),  'O': (0x12, True),  'P': (0x13, True),
    'Q': (0x14, True),  'R': (0x15, True),  'S': (0x16, True),  'T': (0x17, True),
    'U': (0x18, True),  'V': (0x19, True),  'W': (0x1A, True),  'X': (0x1B, True),
    'Y': (0x1C, True),  'Z': (0x1D, True),
    '1': (0x1E, False), '2': (0x1F, False), '3': (0x20, False), '4': (0x21, False),
    '5': (0x22, False), '6': (0x23, False), '7': (0x24, False), '8': (0x25, False),
    '9': (0x26, False), '0': (0x27, False),
    '!': (0x1E, True),  '@': (0x1F, True),  '#': (0x20, True),  '$': (0x21, True),
    '%': (0x22, True),  '^': (0x23, True),  '&': (0x24, True),  '*': (0x25, True),
    '(': (0x26, True),  ')': (0x27, True),
    '\n': (0x28, False), '\r': (0x28, False), '\t': (0x2B, False), ' ': (0x2C, False),
    '-': (0x2D, False), '_': (0x2D, True),  '=': (0x2E, False), '+': (0x2E, True),
    '[': (0x2F, False), '{': (0x2F, True),  ']': (0x30, False), '}': (0x30, True),
    '\\': (0x31, False), '|': (0x31, True),  ';': (0x33, False), ':': (0x33, True),
    "'": (0x34, False), '"': (0x34, True),  '`': (0x35, False), '~': (0x35, True),
    ',': (0x36, False), '<': (0x36, True),  '.': (0x37, False), '>': (0x37, True),
    '/': (0x38, False), '?': (0x38, True)
}

# -----------------------------------------------------------------------------
# Input State & Thread-Safe Queue
# -----------------------------------------------------------------------------
class KvmState:
    MOUSE_BUCKET_MS = 8  # Accumulate mouse moves into 8ms buckets during recording

    def __init__(self, abs_mode=False, screen_width=1920, screen_height=1080):
        self.lock = threading.Lock()
        self.kvm_active = False
        self.seq = 0

        # Action Recorder
        self.recording = False
        self.record_start_ms = 0
        self.last_record_ms = 0   # timestamp of last recorded event (for delta calc)
        self.recorded_lines = []
        self.abs_mode = abs_mode
        self.screen_width = screen_width
        self.screen_height = screen_height
        self.curr_abs_x = screen_width // 2
        self.curr_abs_y = screen_height // 2

        # Mouse accumulator for recording (bucket mouse_move events into time windows)
        self.rec_acc_dx = 0
        self.rec_acc_dy = 0
        self.rec_acc_wheel = 0
        self.rec_acc_pan = 0
        self.rec_bucket_start_ms = 0  # when current accumulation bucket started

        # Keyboard state
        self.modifiers = 0
        self.pressed_keys = set()
        self.kbd_dirty = False

        # Mouse state (for live forwarding to ESP32)
        self.mouse_dx = 0
        self.mouse_dy = 0
        self.mouse_wheel = 0
        self.mouse_pan = 0
        self.mouse_buttons = 0
        self.last_pos = None

    def next_seq(self):
        self.seq = (self.seq + 1) & 0xFFFFFFFF
        return self.seq

    def reset_inputs(self):
        with self.lock:
            self.modifiers = 0
            self.pressed_keys.clear()
            self.kbd_dirty = True
            self.mouse_dx = 0
            self.mouse_dy = 0
            self.mouse_wheel = 0
            self.mouse_pan = 0
            self.mouse_buttons = 0

    def _flush_mouse_bucket(self, now_ms):
        """Flush accumulated mouse movement as a single recorded event. Call with lock held."""
        if not self.recording:
            return
        dx, dy = self.rec_acc_dx, self.rec_acc_dy
        wheel, pan = self.rec_acc_wheel, self.rec_acc_pan
        self.rec_acc_dx = self.rec_acc_dy = 0
        self.rec_acc_wheel = self.rec_acc_pan = 0
        self.rec_bucket_start_ms = now_ms

        if dx or dy:
            dt = int(now_ms - self.last_record_ms) if self.last_record_ms > 0 else 0
            self.last_record_ms = now_ms
            if self.abs_mode:
                self.curr_abs_x = max(0, min(self.screen_width, self.curr_abs_x + dx))
                self.curr_abs_y = max(0, min(self.screen_height, self.curr_abs_y + dy))
                norm_x = int((self.curr_abs_x / self.screen_width) * 32767)
                norm_y = int((self.curr_abs_y / self.screen_height) * 32767)
                self.recorded_lines.append(f"{dt}|mouse_abs|{norm_x}|{norm_y}")
            else:
                self.recorded_lines.append(f"{dt}|mouse_move|{dx}|{dy}")
        if wheel or pan:
            dt = int(now_ms - self.last_record_ms) if self.last_record_ms > 0 else 0
            self.last_record_ms = now_ms
            self.recorded_lines.append(f"{dt}|mouse_scroll|{wheel}|{pan}")

    def add_mouse_record(self, dx=0, dy=0, wheel=0, pan=0):
        """Accumulate mouse movement into current time bucket; flush when bucket expires."""
        if not self.recording:
            return
        now_ms = time.time() * 1000
        if self.rec_bucket_start_ms == 0:
            self.rec_bucket_start_ms = now_ms

        self.rec_acc_dx += dx
        self.rec_acc_dy += dy
        self.rec_acc_wheel += wheel
        self.rec_acc_pan += pan

        # Flush bucket if time window has elapsed
        if (now_ms - self.rec_bucket_start_ms) >= self.MOUSE_BUCKET_MS:
            self._flush_mouse_bucket(now_ms)

    def flush_mouse_record_now(self):
        """Force-flush mouse accumulator (call before key events so timing is correct)."""
        if not self.recording:
            return
        now_ms = time.time() * 1000
        if (self.rec_acc_dx or self.rec_acc_dy or self.rec_acc_wheel or self.rec_acc_pan):
            self._flush_mouse_bucket(now_ms)

    def add_record_line(self, line):
        """Record a keyboard or mouse-button event (non-movement). Flushes any pending mouse move first."""
        if self.recording:
            # Always flush pending mouse accumulation before a key/button event
            self.flush_mouse_record_now()
            now_ms = time.time() * 1000
            dt = int(now_ms - self.last_record_ms) if self.last_record_ms > 0 else 0
            self.last_record_ms = now_ms
            pipe_line = line.replace(" ", "|")
            self.recorded_lines.append(f"{dt}|{pipe_line}")




# -----------------------------------------------------------------------------
# Network UDP Sender
# -----------------------------------------------------------------------------
class UdpSender(threading.Thread):
    def __init__(self, host, port, state, rate=125, jiggle=False):
        super().__init__(daemon=True)
        self.target = (host, port)
        self.state = state
        self.interval = 1.0 / rate
        self.jiggle = jiggle
        self.running = True
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

    def run(self):
        heartbeat_count = 0

        while self.running:
            t0 = time.perf_counter()
            with self.state.lock:
                active = self.state.kvm_active

                # 1. Keyboard
                if self.state.kbd_dirty:
                    keys = list(self.state.pressed_keys)[:6]
                    key_bytes = bytes(keys + [0] * (6 - len(keys)))
                    pkt = struct.pack(FMT_KEYBOARD, PACKET_MAGIC, self.state.next_seq(), EVENT_KEYBOARD,
                                      self.state.modifiers, 0x00, key_bytes)
                    try:
                        self.sock.sendto(pkt, self.target)
                    except Exception:
                        pass
                    self.state.kbd_dirty = False

                # 2. Mouse
                if active:
                    dx, dy = self.state.mouse_dx, self.state.mouse_dy
                    wheel, pan = self.state.mouse_wheel, self.state.mouse_pan
                    buttons = self.state.mouse_buttons

                    self.state.mouse_dx = 0
                    self.state.mouse_dy = 0
                    self.state.mouse_wheel = 0
                    self.state.mouse_pan = 0

                    if dx != 0 or dy != 0 or wheel != 0 or pan != 0 or buttons != 0:
                        dx = max(-32767, min(32767, dx))
                        dy = max(-32767, min(32767, dy))
                        wheel = max(-127, min(127, wheel))
                        pan = max(-127, min(127, pan))

                        pkt = struct.pack(FMT_MOUSE, PACKET_MAGIC, self.state.next_seq(), EVENT_MOUSE,
                                          buttons, dx, dy, wheel, pan)
                        try:
                            self.sock.sendto(pkt, self.target)
                        except Exception:
                            pass

                        if self.state.recording:
                            if dx != 0 or dy != 0:
                                self.state.add_mouse_record(dx=dx, dy=dy)
                            if wheel != 0 or pan != 0:
                                self.state.add_mouse_record(wheel=wheel, pan=pan)

                # 3. Heartbeat / Link Alive
                heartbeat_count += 1
                if heartbeat_count >= 100:
                    heartbeat_count = 0
                    if not active:
                        pkt = struct.pack(FMT_MOUSE, PACKET_MAGIC, self.state.next_seq(), EVENT_MOUSE, 0, 0, 0, 0, 0)
                        try:
                            self.sock.sendto(pkt, self.target)
                        except Exception:
                            pass

            elapsed = time.perf_counter() - t0
            sleep_time = max(0.001, self.interval - elapsed)
            time.sleep(sleep_time)

    def stop(self):
        self.running = False


# -----------------------------------------------------------------------------
# Cross-Platform Clipboard Reader & Typer
# -----------------------------------------------------------------------------
def get_clipboard_text():
    """Reads system clipboard cross-platform."""
    try:
        import tkinter as tk
        root = tk.Tk()
        root.withdraw()
        text = root.clipboard_get()
        root.destroy()
        if text:
            return text
    except Exception:
        pass

    system = platform.system()
    try:
        if system == 'Darwin':
            return subprocess.check_output(['pbpaste'], text=True)
        elif system == 'Linux':
            if os.environ.get('WAYLAND_DISPLAY'):
                return subprocess.check_output(['wl-paste'], text=True)
            else:
                return subprocess.check_output(['xclip', '-selection', 'clipboard', '-o'], text=True)
        elif system == 'Windows':
            import ctypes
            ctypes.windll.user32.OpenClipboard(0)
            p = ctypes.windll.user32.GetClipboardData(13)
            text = ctypes.c_wchar_p(p).value
            ctypes.windll.user32.CloseClipboard()
            return text or ""
    except Exception:
        pass

    return ""


def type_clipboard(sender_sock, target, text, speed_cps=50):
    """Types text directly into target machine via ESP32 HID keystrokes."""
    if not text:
        return
    delay = 1.0 / max(10, speed_cps)
    print(f"\n📋 [CLIPBOARD] Typing {len(text)} characters to target ({speed_cps} cps)...")

    seq = 0
    for char in text:
        if char in CHAR_TO_HID:
            hid_code, need_shift = CHAR_TO_HID[char]
            mod = MOD_LSHIFT if need_shift else 0

            # Key Press
            seq = (seq + 1) & 0xFFFFFFFF
            key_bytes = bytes([hid_code] + [0] * 5)
            pkt_press = struct.pack(FMT_KEYBOARD, PACKET_MAGIC, seq, EVENT_KEYBOARD, mod, 0x00, key_bytes)
            sender_sock.sendto(pkt_press, target)
            time.sleep(delay / 2)

            # Key Release
            seq = (seq + 1) & 0xFFFFFFFF
            pkt_rel = struct.pack(FMT_KEYBOARD, PACKET_MAGIC, seq, EVENT_KEYBOARD, 0x00, 0x00, b'\x00' * 6)
            sender_sock.sendto(pkt_rel, target)
            time.sleep(delay / 2)
        elif char == '\t':
            time.sleep(delay)

    print("✅ [CLIPBOARD] Typing complete!")


# -----------------------------------------------------------------------------
# Optional Live Screenshot Preview Server (HTTP Port 8080)
# -----------------------------------------------------------------------------
class ScreenshotServer(threading.Thread):
    def __init__(self, port=8080):
        super().__init__(daemon=True)
        self.port = port

    def run(self):
        try:
            from http.server import HTTPServer, BaseHTTPRequestHandler

            class ScreenHandler(BaseHTTPRequestHandler):
                def do_GET(self):
                    if self.path == '/screenshot.jpg' or self.path == '/':
                        img_bytes = self.capture_screen()
                        if img_bytes:
                            self.send_response(200)
                            self.send_header('Content-Type', 'image/jpeg')
                            self.send_header('Cache-Control', 'no-store, no-cache')
                            self.send_header('Access-Control-Allow-Origin', '*')
                            self.end_headers()
                            self.wfile.write(img_bytes)
                            return
                    self.send_response(404)
                    self.end_headers()

                def capture_screen(self):
                    try:
                        import mss
                        import io
                        from PIL import Image
                        with mss.mss() as sct:
                            monitor = sct.monitors[1]
                            sct_img = sct.grab(monitor)
                            img = Image.frombytes("RGB", sct_img.size, sct_img.bgra, "raw", "BGRX")
                            img.thumbnail((1280, 720))
                            buf = io.BytesIO()
                            img.save(buf, format='JPEG', quality=65)
                            return buf.getvalue()
                    except Exception:
                        pass

                    try:
                        from PIL import ImageGrab
                        import io
                        img = ImageGrab.grab()
                        img.thumbnail((1280, 720))
                        buf = io.BytesIO()
                        img.save(buf, format='JPEG', quality=65)
                        return buf.getvalue()
                    except Exception:
                        pass
                    return None

                def log_message(self, format, *args):
                    pass

            httpd = HTTPServer(('0.0.0.0', self.port), ScreenHandler)
            print(f"📷 [PREVIEW] Screenshot server active on http://0.0.0.0:{self.port}/screenshot.jpg")
            httpd.serve_forever()
        except Exception as e:
            print(f"⚠️ [PREVIEW] Could not start screenshot server: {e}")


# -----------------------------------------------------------------------------
# Action Replay Engine
# -----------------------------------------------------------------------------
def replay_action_file(host, port, file_path, scale=1.0):
    """Replays an action file locally by sending UDP packets."""
    if not os.path.exists(file_path):
        print(f"❌ Action file '{file_path}' not found.")
        return

    print(f"▶️ [REPLAY] Loading action file '{file_path}' (mouse_scale={scale})...")
    with open(file_path, 'r', encoding='utf-8', errors='ignore') as f:
        lines = [line.strip() for line in f if line.strip() and not line.startswith('#')]

    print(f"▶️ [REPLAY] Executing {len(lines)} action events...")
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    target = (host, port)
    seq = 0

    start_time = time.time()
    for line in lines:
        # Support both pipe-separated (new ESP32 format) and space-separated (old format)
        parts = line.split('|') if '|' in line else line.split()
        if len(parts) < 2:
            continue
        try:
            target_dt_ms = int(parts[0])
            elapsed_ms = int((time.time() - start_time) * 1000)
            if target_dt_ms > elapsed_ms:
                time.sleep((target_dt_ms - elapsed_ms) / 1000.0)

            cmd = parts[1].lower()
            if cmd == "mouse_move" and len(parts) >= 4:
                dx = int(int(parts[2]) * scale)
                dy = int(int(parts[3]) * scale)
                seq = (seq + 1) & 0xFFFFFFFF
                pkt = struct.pack(FMT_MOUSE, PACKET_MAGIC, seq, EVENT_MOUSE, 0, dx, dy, 0, 0)
                sock.sendto(pkt, target)
            elif cmd == "mouse_scroll" and len(parts) >= 4:
                wheel = int(parts[2])
                pan = int(parts[3])
                seq = (seq + 1) & 0xFFFFFFFF
                pkt = struct.pack(FMT_MOUSE, PACKET_MAGIC, seq, EVENT_MOUSE, 0, 0, 0, wheel, pan)
                sock.sendto(pkt, target)
            elif cmd == "mouse_button" and len(parts) >= 4:
                btn_str, act_str = parts[2].lower(), parts[3].lower()
                btn_map = {'left': 1, 'right': 2, 'middle': 4, 'backward': 8, 'forward': 16}
                btn = btn_map.get(btn_str, 1)
                seq = (seq + 1) & 0xFFFFFFFF
                if act_str == "down":
                    pkt = struct.pack(FMT_MOUSE, PACKET_MAGIC, seq, EVENT_MOUSE, btn, 0, 0, 0, 0)
                    sock.sendto(pkt, target)
                elif act_str == "up":
                    pkt = struct.pack(FMT_MOUSE, PACKET_MAGIC, seq, EVENT_MOUSE, 0, 0, 0, 0, 0)
                    sock.sendto(pkt, target)
                else:
                    pkt1 = struct.pack(FMT_MOUSE, PACKET_MAGIC, seq, EVENT_MOUSE, btn, 0, 0, 0, 0)
                    sock.sendto(pkt1, target)
                    time.sleep(0.02)
                    seq = (seq + 1) & 0xFFFFFFFF
                    pkt2 = struct.pack(FMT_MOUSE, PACKET_MAGIC, seq, EVENT_MOUSE, 0, 0, 0, 0, 0)
                    sock.sendto(pkt2, target)
            elif cmd == "key_down" and len(parts) >= 3:
                code = int(parts[2])
                seq = (seq + 1) & 0xFFFFFFFF
                pkt = struct.pack(FMT_KEYBOARD, PACKET_MAGIC, seq, EVENT_KEYBOARD, 0, 0, bytes([code] + [0]*5))
                sock.sendto(pkt, target)
            elif cmd == "key_up" and len(parts) >= 3:
                seq = (seq + 1) & 0xFFFFFFFF
                pkt = struct.pack(FMT_KEYBOARD, PACKET_MAGIC, seq, EVENT_KEYBOARD, 0, 0, b'\x00'*6)
                sock.sendto(pkt, target)
        except Exception:
            pass

    # Final release all
    seq = (seq + 1) & 0xFFFFFFFF
    sock.sendto(struct.pack(FMT_MOUSE, PACKET_MAGIC, seq, EVENT_MOUSE, 0, 0, 0, 0, 0), target)
    seq = (seq + 1) & 0xFFFFFFFF
    sock.sendto(struct.pack(FMT_KEYBOARD, PACKET_MAGIC, seq, EVENT_KEYBOARD, 0, 0, b'\x00'*6), target)
    print("✅ [REPLAY] Action file replay finished!")


# -----------------------------------------------------------------------------
# Linux Native evdev Backend (Direct Kernel Capture)
# -----------------------------------------------------------------------------
def run_evdev_linux(args, state, sender):
    try:
        import evdev
        from evdev import ecodes
    except ImportError:
        print("❌ evdev module not installed. Run: sudo dnf install -y python3-evdev")
        return False

    device_paths = evdev.list_devices()
    if not device_paths:
        print("\n🔒 Linux Device Permissions Notice:")
        print("Under Wayland / Linux, kernel device access (/dev/input/event*) requires permissions.")
        print("\n👉 To fix, simply run with sudo:")
        print(f"   sudo python3 {' '.join(sys.argv)}")
        print("\n👉 Or permanently add your user to the input group:")
        print("   sudo usermod -a -G input $USER (then log out & back in)\n")
        return False

    devices = []
    seen_paths = set()
    for path in device_paths:
        try:
            dev = evdev.InputDevice(path)
            if dev.path not in seen_paths:
                devices.append(dev)
                seen_paths.add(dev.path)
        except Exception:
            pass

    if args.debug:
        print("\n🔍 [DEBUG] Available input devices:")
        for dev in devices:
            caps = dev.capabilities(verbose=True)
            cap_names = [name for name, _ in caps.keys()] if caps else []
            print(f"   {dev.path} | {dev.name} | caps={cap_names}")
        print()

    # Filter: only devices that have keyboard OR mouse capabilities
    active_devices = []
    for dev in devices:
        caps = dev.capabilities()
        has_keys = ecodes.EV_KEY in caps
        has_rel = ecodes.EV_REL in caps
        has_abs = ecodes.EV_ABS in caps

        if has_keys or has_rel or has_abs:
            # Skip pure power buttons or video/media-only devices
            if has_keys:
                key_caps = caps[ecodes.EV_KEY]
                has_real_keys = any(k in key_caps for k in (ecodes.KEY_A, ecodes.KEY_SPACE, ecodes.BTN_LEFT, ecodes.BTN_MOUSE, ecodes.BTN_TOUCH))
                if not has_real_keys:
                    continue
            active_devices.append(dev)

    if not active_devices:
        active_devices = devices  # fallback: use all

    print(f"🎯 [EVDEV] Listening on {len(active_devices)} input device(s):")
    for dev in active_devices:
        print(f"   • {dev.path}  [{dev.name}]")

    toggle_evcodes = {
        'f8': ecodes.KEY_F8, 'f9': ecodes.KEY_F9, 'f10': ecodes.KEY_F10,
        'f12': ecodes.KEY_F12, 'pause': ecodes.KEY_PAUSE, 'scrolllock': ecodes.KEY_SCROLLLOCK
    }
    target_toggle_code = toggle_evcodes.get(args.toggle.lower(), ecodes.KEY_F8)
    record_toggle_code = toggle_evcodes.get(args.record_key.lower(), ecodes.KEY_F9)

    def handle_device(dev):
        """Unified handler: processes keyboard + mouse + touchpad events from one device."""
        last_abs_x = [None]
        last_abs_y = [None]

        try:
            for ev in dev.read_loop():

                # ── Keyboard events ────────────────────────────────────────────
                if ev.type == ecodes.EV_KEY:
                    code, val = ev.code, ev.value  # val: 1=down, 0=up, 2=repeat

                    # 1. KVM toggle
                    if code == target_toggle_code and val == 1:
                        with state.lock:
                            state.kvm_active = not state.kvm_active
                            status = "🟢 ON  (→ Target)" if state.kvm_active else "⚪ OFF (→ Host)"
                            print(f"\n[KVM] {status}")
                        state.reset_inputs()
                        continue

                    # 2. Record toggle
                    if code == record_toggle_code and val == 1:
                        with state.lock:
                            state.recording = not state.recording
                            if state.recording:
                                state.record_start_ms = time.time() * 1000
                                state.last_record_ms = 0
                                state.recorded_lines.clear()
                                print("\n🔴 [RECORD] STARTED — move mouse & type now...")
                            else:
                                # Flush any pending mouse accumulation before saving
                                state.flush_mouse_record_now()
                                state.rec_bucket_start_ms = 0
                                n = len(state.recorded_lines)
                                filename = args.record_file or f"macro_{int(time.time())}.txt"
                                with open(filename, 'w', encoding='utf-8') as f:
                                    f.write("# ESP32-S3 Action Macro Recording\n")
                                    f.write("\n".join(state.recorded_lines) + "\n")
                                print(f"\n⏹️  [RECORD] STOPPED — saved {n} events → '{filename}'")
                        continue

                    # 3. Mouse buttons (BTN_LEFT etc.)
                    btn_mask = 0
                    btn_name = "left"
                    if code in (ecodes.BTN_LEFT, ecodes.BTN_MOUSE):
                        btn_mask, btn_name = 0x01, "left"
                    elif code == ecodes.BTN_RIGHT:
                        btn_mask, btn_name = 0x02, "right"
                    elif code == ecodes.BTN_MIDDLE:
                        btn_mask, btn_name = 0x04, "middle"
                    elif code == ecodes.BTN_SIDE:
                        btn_mask, btn_name = 0x08, "backward"
                    elif code == ecodes.BTN_EXTRA:
                        btn_mask, btn_name = 0x10, "forward"

                    if btn_mask:
                        with state.lock:
                            if val == 1:
                                state.add_record_line(f"mouse_button {btn_name} down")
                                if state.kvm_active:
                                    state.mouse_buttons |= btn_mask
                            elif val == 0:
                                state.add_record_line(f"mouse_button {btn_name} up")
                                if state.kvm_active:
                                    state.mouse_buttons &= ~btn_mask
                        continue

                    # 4. Modifier keys
                    if code in EVDEV_MODIFIERS:
                        bit = EVDEV_MODIFIERS[code]
                        with state.lock:
                            if val in (1, 2):
                                state.modifiers |= bit
                            else:
                                state.modifiers &= ~bit
                            state.kbd_dirty = True
                        continue

                    # 5. Normal keyboard keys
                    hid = EVDEV_TO_HID.get(code)
                    if hid:
                        with state.lock:
                            if val == 1:
                                state.add_record_line(f"key_down {hid}")
                                if state.kvm_active:
                                    state.pressed_keys.add(hid)
                                    state.kbd_dirty = True
                            elif val == 0:
                                state.add_record_line(f"key_up {hid}")
                                if state.kvm_active:
                                    state.pressed_keys.discard(hid)
                                    state.kbd_dirty = True

                # ── Relative mouse movement (USB mice) ─────────────────────────
                elif ev.type == ecodes.EV_REL:
                    with state.lock:
                        dx = ev.value if ev.code == ecodes.REL_X else 0
                        dy = ev.value if ev.code == ecodes.REL_Y else 0
                        wheel = ev.value if ev.code == ecodes.REL_WHEEL else 0
                        pan = ev.value if ev.code == ecodes.REL_HWHEEL else 0

                        if dx or dy:
                            state.add_mouse_record(dx=dx, dy=dy)
                            if state.kvm_active:
                                state.mouse_dx += dx
                                state.mouse_dy += dy
                        if wheel or pan:
                            state.add_mouse_record(wheel=wheel, pan=pan)
                            if state.kvm_active:
                                state.mouse_wheel += wheel
                                state.mouse_pan += pan

                # ── Absolute pointer (touchpad) ─────────────────────────────────
                elif ev.type == ecodes.EV_ABS:
                    if ev.code == ecodes.ABS_X:
                        if last_abs_x[0] is not None:
                            dx = ev.value - last_abs_x[0]
                            with state.lock:
                                if abs(dx) < 200:
                                    state.add_mouse_record(dx=dx)
                                    if state.kvm_active:
                                        state.mouse_dx += dx
                        last_abs_x[0] = ev.value
                    elif ev.code == ecodes.ABS_Y:
                        if last_abs_y[0] is not None:
                            dy = ev.value - last_abs_y[0]
                            with state.lock:
                                if abs(dy) < 200:
                                    state.add_mouse_record(dy=dy)
                                    if state.kvm_active:
                                        state.mouse_dy += dy
                        last_abs_y[0] = ev.value

        except OSError:
            pass  # device disconnected
        except Exception as e:
            if args.debug:
                print(f"  [DBG] Error on {dev.path}: {e}")

    for dev in active_devices:
        threading.Thread(target=handle_device, args=(dev,), daemon=True, name=f"evdev-{dev.path}").start()

    return True


# -----------------------------------------------------------------------------
# Main Cross-Platform Entry & Loop
# -----------------------------------------------------------------------------
def run_kvm(args):
    print("=" * 65)
    print("🚀 ESP32-S3 Wireless KVM Client (Universal: Win/Linux/macOS)")
    print("=" * 65)
    print(f"📡 Target ESP32 : {args.ip}:{args.port}")
    print(f"🔘 KVM Toggle   : [{args.toggle.upper()}]")
    print(f"🔴 Record Toggle: [{args.record_key.upper()}]")
    print(f"📋 Paste Hotkey : [Ctrl+Alt+V]")
    print(f"💻 Mode         : {'Exclusive' if args.exclusive else 'Shared'}")
    print("=" * 65)

    if args.replay:
        replay_action_file(args.ip, args.port, args.replay, scale=args.mouse_scale)
        return

    state = KvmState(abs_mode=args.abs_mouse, screen_width=args.screen_width, screen_height=args.screen_height)
    sender = UdpSender(args.ip, args.port, state, rate=args.rate, jiggle=args.jiggle)
    sender.start()

    if args.preview:
        preview_server = ScreenshotServer(args.preview_port)
        preview_server.start()

    # Try Linux native evdev first on Linux
    if platform.system() == 'Linux':
        if run_evdev_linux(args, state, sender):
            print("\n✨ Linux Kernel KVM Client is active!")
            if args.abs_mouse:
                print(f"🎯 [MODE] Absolute Mouse Recording Active ({args.screen_width}x{args.screen_height} → 0..32767)")
            print(f"👉 Press [{args.toggle.upper()}] to toggle KVM (mouse & keyboard → target machine).")
            print(f"👉 Press [{args.record_key.upper()}] to Start / Stop Macro Recording (works with or without KVM active).")
            print("👉 Press [Ctrl+C] to exit.")
            print("\n💡 Tip: Press F8 first to enable KVM, then F9 to record actions on target.\n")

            # Suppress terminal echo of F-key escape codes (^[[20~ etc.)
            import tty, termios
            old_tty_settings = None
            try:
                old_tty_settings = termios.tcgetattr(sys.stdin.fileno())
                tty.setraw(sys.stdin.fileno(), termios.TCSANOW)
            except Exception:
                pass

            try:
                while True:
                    time.sleep(0.5)
            except KeyboardInterrupt:
                pass
            finally:
                # Restore terminal settings
                if old_tty_settings:
                    try:
                        termios.tcsetattr(sys.stdin.fileno(), termios.TCSADRAIN, old_tty_settings)
                    except Exception:
                        pass
                print("\n🛑 Stopping KVM Client...")
                sender.stop()
            return
        else:
            sys.exit(1)

    # Windows / macOS using pynput
    try:
        from pynput import keyboard, mouse
    except ImportError:
        print("❌ 'pynput' library required on Windows/macOS. Run: pip install pynput")
        sys.exit(1)

    toggle_keys = {
        'f8': keyboard.Key.f8, 'f9': keyboard.Key.f9, 'f10': keyboard.Key.f10,
        'f12': keyboard.Key.f12, 'pause': keyboard.Key.pause, 'scroll_lock': keyboard.Key.scroll_lock
    }
    target_toggle_key = toggle_keys.get(args.toggle.lower(), keyboard.Key.f8)
    record_toggle_key = toggle_keys.get(args.record_key.lower(), keyboard.Key.f9)

    SPECIAL_TO_HID = {
        keyboard.Key.space: 0x2C, keyboard.Key.enter: 0x28, keyboard.Key.backspace: 0x2A,
        keyboard.Key.tab: 0x2B, keyboard.Key.esc: 0x29, keyboard.Key.caps_lock: 0x39,
        keyboard.Key.f1: 0x3A, keyboard.Key.f2: 0x3B, keyboard.Key.f3: 0x3C, keyboard.Key.f4: 0x3D,
        keyboard.Key.f5: 0x3E, keyboard.Key.f6: 0x3F, keyboard.Key.f7: 0x40, keyboard.Key.f8: 0x41,
        keyboard.Key.f9: 0x42, keyboard.Key.f10: 0x43, keyboard.Key.f11: 0x44, keyboard.Key.f12: 0x45,
        keyboard.Key.print_screen: 0x46, keyboard.Key.scroll_lock: 0x47, keyboard.Key.pause: 0x48,
        keyboard.Key.insert: 0x49, keyboard.Key.home: 0x4A, keyboard.Key.page_up: 0x4B,
        keyboard.Key.delete: 0x4C, keyboard.Key.end: 0x4D, keyboard.Key.page_down: 0x4E,
        keyboard.Key.right: 0x4F, keyboard.Key.left: 0x50, keyboard.Key.down: 0x51, keyboard.Key.up: 0x52,
        keyboard.Key.num_lock: 0x53
    }

    MODIFIER_KEYS = {
        keyboard.Key.ctrl_l: MOD_LCTRL, keyboard.Key.ctrl_r: MOD_RCTRL if hasattr(keyboard.Key, 'ctrl_r') else MOD_RCTRL,
        keyboard.Key.shift_l: MOD_LSHIFT, keyboard.Key.shift_r: MOD_RSHIFT,
        keyboard.Key.alt_l: MOD_LALT, keyboard.Key.alt_r: MOD_RALT,
        keyboard.Key.cmd: MOD_LMETA, keyboard.Key.cmd_r: MOD_RMETA if hasattr(keyboard.Key, 'cmd_r') else MOD_LMETA
    }

    def on_key_press(key):
        key_name = getattr(key, 'name', str(key)).lower().replace('key.', '')

        if (key == target_toggle_key) or (key_name == args.toggle.lower()):
            with state.lock:
                state.kvm_active = not state.kvm_active
                status = "🟢 ON (Forwarding to Target)" if state.kvm_active else "⚪ OFF (Host PC Active)"
                print(f"\n[KVM] {status}")
            state.reset_inputs()
            return

        if (key == record_toggle_key) or (key_name == args.record_key.lower()):
            with state.lock:
                state.recording = not state.recording
                if state.recording:
                    state.record_start_ms = time.time() * 1000
                    state.last_record_ms = 0
                    state.recorded_lines.clear()
                    print("\n🔴 [RECORD] Macro recording STARTED. Perform your actions...")
                else:
                    # Flush any pending mouse accumulation before saving
                    state.flush_mouse_record_now()
                    state.rec_bucket_start_ms = 0
                    filename = args.record_file or f"macro_{int(time.time())}.txt"
                    with open(filename, 'w', encoding='utf-8') as f:
                        f.write("# ESP32-S3 Action Macro Recording\n")
                        f.write("\n".join(state.recorded_lines) + "\n")
                    print(f"\n⏹️ [RECORD] Macro recording STOPPED. Saved {len(state.recorded_lines)} actions to '{filename}'!")
            return

        if getattr(key, 'char', None) == 'v' and (state.modifiers & (MOD_LCTRL | MOD_RCTRL)) and (state.modifiers & (MOD_LALT | MOD_RALT)):
            if state.kvm_active:
                text = get_clipboard_text()
                if text:
                    threading.Thread(target=type_clipboard, args=(sender.sock, sender.target, text), daemon=True).start()
                    return

        if key in MODIFIER_KEYS:
            with state.lock:
                state.modifiers |= MODIFIER_KEYS[key]
                state.kbd_dirty = True
            return

        hid_code = None
        if isinstance(key, keyboard.KeyCode):
            if key.char and key.char.lower() in CHAR_TO_HID:
                hid_code, _ = CHAR_TO_HID[key.char.lower()]
            elif key.vk is not None:
                if 0x41 <= key.vk <= 0x5A:
                    hid_code = 0x04 + (key.vk - 0x41)
                elif 0x30 <= key.vk <= 0x39:
                    hid_code = 0x1E + (key.vk - 0x31) if key.vk > 0x30 else 0x27
        elif key in SPECIAL_TO_HID:
            hid_code = SPECIAL_TO_HID[key]

        if hid_code and state.kvm_active:
            with state.lock:
                state.pressed_keys.add(hid_code)
                state.kbd_dirty = True
                state.add_record_line(f"key_down {hid_code}")

    def on_key_release(key):
        if key in MODIFIER_KEYS:
            with state.lock:
                state.modifiers &= ~MODIFIER_KEYS[key]
                state.kbd_dirty = True
            return

        hid_code = None
        if isinstance(key, keyboard.KeyCode):
            if key.char and key.char.lower() in CHAR_TO_HID:
                hid_code, _ = CHAR_TO_HID[key.char.lower()]
            elif key.vk is not None:
                if 0x41 <= key.vk <= 0x5A:
                    hid_code = 0x04 + (key.vk - 0x41)
                elif 0x30 <= key.vk <= 0x39:
                    hid_code = 0x1E + (key.vk - 0x31) if key.vk > 0x30 else 0x27
        elif key in SPECIAL_TO_HID:
            hid_code = SPECIAL_TO_HID[key]

        if hid_code:
            with state.lock:
                state.pressed_keys.discard(hid_code)
                state.kbd_dirty = True
                state.add_record_line(f"key_up {hid_code}")

    def on_mouse_move(x, y):
        if not state.kvm_active:
            state.last_pos = (x, y)
            return

        with state.lock:
            if state.last_pos is not None:
                dx = x - state.last_pos[0]
                dy = y - state.last_pos[1]
                state.mouse_dx += int(dx)
                state.mouse_dy += int(dy)
                state.add_mouse_record(dx=int(dx), dy=int(dy))
            state.last_pos = (x, y)

    def on_mouse_click(x, y, button, pressed):
        if not state.kvm_active:
            return

        btn_mask = 0
        btn_name = "left"
        if button == mouse.Button.left:
            btn_mask = 0x01
            btn_name = "left"
        elif button == mouse.Button.right:
            btn_mask = 0x02
            btn_name = "right"
        elif button == mouse.Button.middle:
            btn_mask = 0x04
            btn_name = "middle"
        elif hasattr(mouse.Button, 'x1') and button == mouse.Button.x1:
            btn_mask = 0x08
            btn_name = "backward"
        elif hasattr(mouse.Button, 'x2') and button == mouse.Button.x2:
            btn_mask = 0x10
            btn_name = "forward"

        with state.lock:
            if pressed:
                state.mouse_buttons |= btn_mask
                state.add_record_line(f"mouse_button {btn_name} down")
            else:
                state.mouse_buttons &= ~btn_mask
                state.add_record_line(f"mouse_button {btn_name} up")

    def on_mouse_scroll(x, y, dx, dy):
        if not state.kvm_active:
            return
        with state.lock:
            state.mouse_wheel += int(dy)
            state.mouse_pan += int(dx)
            state.add_mouse_record(wheel=int(dy), pan=int(dx))

    kbd_listener = keyboard.Listener(on_press=on_key_press, on_release=on_key_release)
    mouse_listener = mouse.Listener(on_move=on_mouse_move, on_click=on_mouse_click, on_scroll=on_mouse_scroll)

    kbd_listener.start()
    mouse_listener.start()

    print("\n✨ KVM Client is active!")
    print(f"👉 Press [{args.toggle.upper()}] to switch keyboard/mouse to target machine.")
    print(f"👉 Press [{args.record_key.upper()}] to Start / Stop Action Macro Recording.")
    print("👉 Press [Ctrl+Alt+V] to paste clipboard text into target.")
    print("👉 Press [Ctrl+C] to exit.\n")

    try:
        while True:
            time.sleep(0.5)
    except KeyboardInterrupt:
        print("\n🛑 Stopping KVM Client...")
    finally:
        sender.stop()
        kbd_listener.stop()
        mouse_listener.stop()


# -----------------------------------------------------------------------------
# Entry Point
# -----------------------------------------------------------------------------
if __name__ == '__main__':
    parser = argparse.ArgumentParser(description="ESP32-S3 Wireless KVM Universal Client")
    parser.add_argument('--ip', default='192.168.4.1', help="Target ESP32 IP address (default: 192.168.4.1)")
    parser.add_argument('--port', type=int, default=DEFAULT_PORT, help="Target UDP port (default: 4210)")
    parser.add_argument('--rate', type=int, default=125, help="Packet rate in Hz (default: 125)")
    parser.add_argument('--toggle', default='f8', help="Toggle KVM hotkey: f8, f9, f10, f12, pause, scrolllock (default: f8)")
    parser.add_argument('--record-key', default='f9', help="Toggle Macro Recording hotkey (default: f9)")
    parser.add_argument('--record-file', default='', help="Output filename for macro recording")
    parser.add_argument('--replay', default='', help="Replay a recorded .txt action file")
    parser.add_argument('--mouse-scale', type=float, default=1.0, help="Movement scaling multiplier for replay (default: 1.0)")
    parser.add_argument('--abs-mouse', action='store_true', help="Record coordinates in 0..32767 Absolute Mouse mode for 100% resolution independence")
    parser.add_argument('--screen-width', type=int, default=1920, help="Source screen width for absolute normalization (default: 1920)")
    parser.add_argument('--screen-height', type=int, default=1080, help="Source screen height for absolute normalization (default: 1080)")
    parser.add_argument('--exclusive', action='store_true', help="Enable exclusive capture mode")
    parser.add_argument('--jiggle', action='store_true', help="Enable subtle keep-awake mouse jiggler")
    parser.add_argument('--preview', action='store_true', help="Start screen capture preview HTTP server")
    parser.add_argument('--preview-port', type=int, default=8080, help="Screenshot server port (default: 8080)")
    parser.add_argument('--debug', action='store_true', help="Print debug info: list devices, log mouse events")

    args = parser.parse_args()
    run_kvm(args)
