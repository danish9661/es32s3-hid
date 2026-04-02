"""
Target screenshot HTTP server.

Run this on the target machine that you want to capture.
It serves /screenshot.bmp using built-in Win32 capture with no third-party dependencies.
"""

import argparse
import signal
import sys
import threading

from preview_http import start_preview_server


def main() -> None:
    parser = argparse.ArgumentParser(description="Target screenshot server for ESP32 KVM preview")
    parser.add_argument("--port", type=int, default=9988, help="HTTP port (default: 9988)")
    args = parser.parse_args()

    if args.port < 1 or args.port > 65535:
        print("ERROR: --port must be between 1 and 65535", file=sys.stderr)
        sys.exit(1)

    server = start_preview_server(args.port)
    print(f"[TARGET PREVIEW] Running on 0.0.0.0:{args.port}")
    print(f"[TARGET PREVIEW] Endpoint: http://<TARGET_IP>:{args.port}/screenshot.bmp")
    print("[TARGET PREVIEW] Press Ctrl+C to stop")

    shutdown_event = threading.Event()

    def _stop(_signum=None, _frame=None):
        if shutdown_event.is_set():
            return
        shutdown_event.set()
        server.shutdown()
        server.server_close()

    signal.signal(signal.SIGINT, _stop)
    signal.signal(signal.SIGTERM, _stop)

    try:
        while not shutdown_event.is_set():
            shutdown_event.wait(0.2)
    finally:
        _stop()


if __name__ == "__main__":
    main()
