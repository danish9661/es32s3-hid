"""
Minimal screenshot preview HTTP service for ESP32 KVM host.

No third-party dependencies are required.
The service captures the primary desktop using Win32 GDI and serves a BMP image.
"""

import ctypes
import ctypes.wintypes as wintypes
import json
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer


SRCCOPY = 0x00CC0020
DIB_RGB_COLORS = 0
BI_RGB = 0
SM_CXSCREEN = 0
SM_CYSCREEN = 1


class BITMAPFILEHEADER(ctypes.Structure):
    _pack_ = 1
    _fields_ = [
        ("bfType", ctypes.c_uint16),
        ("bfSize", ctypes.c_uint32),
        ("bfReserved1", ctypes.c_uint16),
        ("bfReserved2", ctypes.c_uint16),
        ("bfOffBits", ctypes.c_uint32),
    ]


class BITMAPINFOHEADER(ctypes.Structure):
    _pack_ = 1
    _fields_ = [
        ("biSize", ctypes.c_uint32),
        ("biWidth", ctypes.c_int32),
        ("biHeight", ctypes.c_int32),
        ("biPlanes", ctypes.c_uint16),
        ("biBitCount", ctypes.c_uint16),
        ("biCompression", ctypes.c_uint32),
        ("biSizeImage", ctypes.c_uint32),
        ("biXPelsPerMeter", ctypes.c_int32),
        ("biYPelsPerMeter", ctypes.c_int32),
        ("biClrUsed", ctypes.c_uint32),
        ("biClrImportant", ctypes.c_uint32),
    ]


class BITMAPINFO(ctypes.Structure):
    _pack_ = 1
    _fields_ = [
        ("bmiHeader", BITMAPINFOHEADER),
        ("bmiColors", ctypes.c_uint32 * 3),
    ]


def _capture_screen_bmp() -> bytes:
    user32 = ctypes.windll.user32
    gdi32 = ctypes.windll.gdi32

    width = user32.GetSystemMetrics(SM_CXSCREEN)
    height = user32.GetSystemMetrics(SM_CYSCREEN)
    if width <= 0 or height <= 0:
        raise RuntimeError("Invalid desktop size")

    hdc_screen = user32.GetDC(None)
    if not hdc_screen:
        raise RuntimeError("GetDC failed")

    hdc_mem = gdi32.CreateCompatibleDC(hdc_screen)
    if not hdc_mem:
        user32.ReleaseDC(None, hdc_screen)
        raise RuntimeError("CreateCompatibleDC failed")

    hbm = gdi32.CreateCompatibleBitmap(hdc_screen, width, height)
    if not hbm:
        gdi32.DeleteDC(hdc_mem)
        user32.ReleaseDC(None, hdc_screen)
        raise RuntimeError("CreateCompatibleBitmap failed")

    old_obj = gdi32.SelectObject(hdc_mem, hbm)

    success = gdi32.BitBlt(hdc_mem, 0, 0, width, height, hdc_screen, 0, 0, SRCCOPY)
    if not success:
        gdi32.SelectObject(hdc_mem, old_obj)
        gdi32.DeleteObject(hbm)
        gdi32.DeleteDC(hdc_mem)
        user32.ReleaseDC(None, hdc_screen)
        raise RuntimeError("BitBlt failed")

    bytes_per_row = ((width * 24 + 31) // 32) * 4
    image_size = bytes_per_row * height

    bmi = BITMAPINFO()
    bmi.bmiHeader.biSize = ctypes.sizeof(BITMAPINFOHEADER)
    bmi.bmiHeader.biWidth = width
    bmi.bmiHeader.biHeight = -height  # top-down for easy browser rendering
    bmi.bmiHeader.biPlanes = 1
    bmi.bmiHeader.biBitCount = 24
    bmi.bmiHeader.biCompression = BI_RGB
    bmi.bmiHeader.biSizeImage = image_size

    pixels = ctypes.create_string_buffer(image_size)

    lines = gdi32.GetDIBits(
        hdc_mem,
        hbm,
        0,
        height,
        pixels,
        ctypes.byref(bmi),
        DIB_RGB_COLORS,
    )

    gdi32.SelectObject(hdc_mem, old_obj)
    gdi32.DeleteObject(hbm)
    gdi32.DeleteDC(hdc_mem)
    user32.ReleaseDC(None, hdc_screen)

    if lines != height:
        raise RuntimeError("GetDIBits failed")

    file_header = BITMAPFILEHEADER()
    file_header.bfType = 0x4D42  # "BM"
    file_header.bfOffBits = ctypes.sizeof(BITMAPFILEHEADER) + ctypes.sizeof(BITMAPINFOHEADER)
    file_header.bfSize = file_header.bfOffBits + image_size

    return bytes(file_header) + bytes(bmi.bmiHeader) + pixels.raw


class _PreviewHandler(BaseHTTPRequestHandler):
    server_version = "ESP32KVMPreview/1.0"

    def _set_headers(self, status_code: int, content_type: str):
        self.send_response(status_code)
        self.send_header("Content-Type", content_type)
        self.send_header("Cache-Control", "no-store, no-cache, must-revalidate")
        self.send_header("Pragma", "no-cache")
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "GET, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Content-Type")
        self.end_headers()

    def do_OPTIONS(self):
        self._set_headers(204, "text/plain")

    def do_GET(self):
        if self.path.startswith("/health"):
            payload = {
                "ok": True,
                "timestamp": int(time.time()),
            }
            body = json.dumps(payload).encode("utf-8")
            self._set_headers(200, "application/json")
            self.wfile.write(body)
            return

        if self.path.startswith("/screenshot.bmp") or self.path.startswith("/screenshot"):
            try:
                image = _capture_screen_bmp()
                self._set_headers(200, "image/bmp")
                self.wfile.write(image)
            except Exception as exc:
                self._set_headers(500, "application/json")
                self.wfile.write(json.dumps({"error": str(exc)}).encode("utf-8"))
            return

        if self.path == "/" or self.path == "":
            info = {
                "service": "ESP32 KVM Screenshot Preview",
                "endpoints": ["/health", "/screenshot.bmp"],
            }
            self._set_headers(200, "application/json")
            self.wfile.write(json.dumps(info).encode("utf-8"))
            return

        self._set_headers(404, "application/json")
        self.wfile.write(b'{"error":"not-found"}')

    def log_message(self, _fmt: str, *_args):
        return


def start_preview_server(port: int = 9876) -> ThreadingHTTPServer:
    server = ThreadingHTTPServer(("0.0.0.0", int(port)), _PreviewHandler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    return server
