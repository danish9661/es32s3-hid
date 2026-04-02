"""
ESP32-S3 KVM - Server (Windows)

Captures keyboard events (LL Hook) and mouse events (Raw Input + LL Hook)
and sends them as UDP packets to ESP32-S3.

Usage:
    python server.py --host <ESP32_IP> [--port 4210] [--rate 125]
"""

import argparse
import sys
import threading

from protocol import UDP_PORT
from state import InputState
from udp_sender import sender_thread
from winapi_hooks import InputHookManager
from preview_http import start_preview_server


TOGGLE_KEY_MAP = {
    "f8": (0x77, "F8"),
    "f9": (0x78, "F9"),
    "f10": (0x79, "F10"),
    "f12": (0x7B, "F12"),
    "pause": (0x13, "Pause"),
    "scrolllock": (0x91, "Scroll Lock"),
}

def main():
    parser = argparse.ArgumentParser(
        description="ESP32-S3 KVM Server (KVM via configurable hotkey)"
    )
    parser.add_argument("--host", required=True, help="ESP32 IP address")
    parser.add_argument("--port", type=int, default=UDP_PORT,
                        help=f"UDP port (default: {UDP_PORT})")
    parser.add_argument("--rate", type=int, default=125,
                        help="Polling rate in Hz (default: 125)")
    parser.add_argument("--toggle-key", default="f8",
                        help="KVM toggle key: f8, f9, f10, f12, pause, scrolllock (default: f8)")
    parser.add_argument("--jiggle", action="store_true",
                        help="Enable invisible mouse jiggler to prevent PC from sleeping")
    parser.add_argument("--preview-port", type=int, default=9876,
                        help="HTTP screenshot preview port (default: 9876)")
    parser.add_argument("--no-preview", action="store_true",
                        help="Disable HTTP screenshot preview server")
    parser.add_argument(
        "--block-local-input",
        action="store_true",
        help="Consume host keyboard/mouse input while KVM is ON (exclusive mode)",
    )
    args = parser.parse_args()

    if not (60 <= args.rate <= 1000):
        print("ERROR: --rate must be between 60 and 1000", file=sys.stderr)
        sys.exit(1)

    toggle_key = args.toggle_key.strip().lower()
    if toggle_key not in TOGGLE_KEY_MAP:
        print(
            "ERROR: --toggle-key must be one of: " + ", ".join(sorted(TOGGLE_KEY_MAP.keys())),
            file=sys.stderr,
        )
        sys.exit(1)

    toggle_vk, toggle_label = TOGGLE_KEY_MAP[toggle_key]

    stop_event = threading.Event()
    state = InputState()

    preview_server = None
    if not args.no_preview:
        try:
            preview_server = start_preview_server(args.preview_port)
            print(f"[PREVIEW] Screenshot endpoint: http://127.0.0.1:{args.preview_port}/screenshot.bmp")
        except Exception as e:
            print(f"[PREVIEW] Failed to start screenshot server: {e}", file=sys.stderr)

    print("[INIT] Starting sender thread...")
    sender = threading.Thread(
        target=sender_thread,
        args=(args.host, args.port, args.rate, stop_event, state, args.jiggle),
        daemon=True,
    )
    sender.start()

    print("[INIT] Installing WinAPI hooks...")
    hook_manager = InputHookManager(
        state,
        toggle_vk=toggle_vk,
        toggle_label=toggle_label,
        block_local_input=args.block_local_input,
    )
    try:
        hook_manager.start()
    except Exception as e:
        print(f"ERROR: {e}", file=sys.stderr)
        stop_event.set()
        sys.exit(1)

    print(f"[KVM] Press {toggle_label} to toggle KVM mode")
    print("[KVM] OFF (input goes to Host PC)")
    if args.block_local_input:
        print("[KVM] Exclusive mode enabled (host input is blocked while KVM is ON)")
    else:
        print("[KVM] Shared mode enabled (host input remains usable while KVM is ON)")

    try:
        hook_manager.process_messages()
    except KeyboardInterrupt:
        print("\n[MAIN] Shutting down...")

    # Cleanup
    stop_event.set()
    hook_manager.stop()
    sender.join(timeout=2)

    if preview_server:
        preview_server.shutdown()
        preview_server.server_close()

    print("[MAIN] Done")

if __name__ == "__main__":
    main()
