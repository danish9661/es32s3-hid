#!/usr/bin/env python3
"""
FIDO2 Live Debug Monitor
========================
Run this BEFORE clicking Register on webauthn.io.
It shows:
  - HID descriptor analysis (does Chrome see this as FIDO?)
  - Every packet Chrome sends to ESP32
  - Every packet ESP32 sends back
  - ESP32 serial logs (what the firmware prints)
"""

import os, sys, struct, threading, time, select, fcntl
from array import array

HIDRAW = "/dev/hidraw2"
BAUD = 115200

RED    = "\033[91m"
GREEN  = "\033[92m"
YELLOW = "\033[93m"
CYAN   = "\033[96m"
BLUE   = "\033[94m"
BOLD   = "\033[1m"
RESET  = "\033[0m"

CTAPHID_NAMES = {
    0x81: "PING",   0x83: "MSG",    0x84: "LOCK",
    0x86: "INIT",   0x88: "WINK",  0x90: "CBOR",
    0x91: "CANCEL", 0xBB: "KEEPALIVE", 0xBF: "ERROR",
}

def decode_pkt(data):
    if len(data) < 5:
        return f"[short {len(data)}B]"
    offset = 1 if data[0] < 0x10 else 0
    if len(data) - offset < 7:
        return f"[too short]"
    cid = struct.unpack_from(">I", data, offset)[0]
    cmd = data[offset + 4]
    if cmd & 0x80:
        length = struct.unpack_from(">H", data, offset + 5)[0]
        payload = data[offset + 7:]
        name = CTAPHID_NAMES.get(cmd, f"0x{cmd:02X}")
        if cmd == 0x86 and len(payload) >= 8:
            nonce = payload[:8].hex()
            if cid == 0xFFFFFFFF:
                return f"INIT req  nonce={nonce}"
            else:
                new_cid = struct.unpack_from(">I", payload, 8)[0] if len(payload) >= 12 else 0
                return f"INIT resp new_cid=0x{new_cid:08x} proto={payload[12] if len(payload)>12 else 0}"
        elif cmd == 0x90:
            ctap_names = {0x01:"MakeCredential",0x02:"GetAssertion",0x04:"GetInfo",0x06:"ClientPIN",0x07:"Reset"}
            ctap_cmd = payload[0] if payload else 0
            return f"CBOR/{ctap_names.get(ctap_cmd,f'0x{ctap_cmd:02x}')} CID=0x{cid:08x} len={length}"
        return f"{name} CID=0x{cid:08x} len={length} payload={payload[:6].hex()}"
    else:
        return f"CONT seq={cmd & 0x7F} CID=0x{cid:08x}"

def analyze_descriptor():
    print(f"\n{BOLD}{'='*60}{RESET}")
    print(f"{BOLD}  HID DESCRIPTOR ANALYSIS{RESET}")
    print(f"{BOLD}{'='*60}{RESET}")
    try:
        fd = os.open(HIDRAW, os.O_RDONLY)
        buf = array("B", [0]*4)
        fcntl.ioctl(fd, 0x80044801, buf, True)
        size = struct.unpack("<I", bytes(buf))[0]
        print(f"\n  Descriptor size: {size} bytes")
        buf2 = array("B", [0]*(4+size))
        struct.pack_into("<I", buf2, 0, size)
        fcntl.ioctl(fd, 0x90044802, buf2, True)
        raw = bytes(buf2[4:4+size])
        print(f"  First 10 bytes: {raw[:10].hex()}")
        os.close(fd)
        
        if b'\x06\xd0\xf1' in raw:
            pos = raw.find(b'\x06\xd0\xf1')
            if pos == 0:
                print(f"  {GREEN}✅ FIDO (0xF1D0) is FIRST in descriptor{RESET}")
            else:
                first_up = raw[:3].hex() if len(raw) >= 3 else raw.hex()
                print(f"  {RED}❌ FIDO found at byte {pos}, but {first_up} comes first{RESET}")
                if raw[:2] == bytes([0x05, 0x01]):
                    print(f"  {RED}   → Keyboard/Desktop (0x01) is first - Chrome REJECTS device{RESET}")
        else:
            print(f"  {RED}❌ FIDO Usage Page NOT in descriptor at all!{RESET}")
        
        try:
            from fido2.hid.base import parse_report_descriptor
            max_in, max_out = parse_report_descriptor(raw)
            print(f"  {GREEN}✅ fido2 lib ACCEPTS: max_in={max_in} max_out={max_out}{RESET}")
            print(f"  {GREEN}   → Chrome WILL send packets to this device{RESET}")
        except Exception as e:
            print(f"  {RED}❌ fido2 lib REJECTS: {e}{RESET}")
            print(f"  {RED}   → Chrome will NOT send any packets (this is why nothing happens){RESET}")
    except Exception as e:
        print(f"  {RED}Error: {e}{RESET}")
    print(f"{BOLD}{'='*60}{RESET}\n")

def serial_reader():
    for port in ["/dev/ttyACM0", "/dev/ttyACM1"]:
        if not os.path.exists(port):
            continue
        try:
            import serial
            ser = serial.Serial(port, BAUD, timeout=0.1)
            print(f"{CYAN}[SERIAL] Monitoring {port}{RESET}")
            while True:
                line = ser.readline()
                if line:
                    ts = time.strftime("%H:%M:%S")
                    print(f"{CYAN}[{ts}] ESP32 LOG: {line.decode('utf-8','replace').rstrip()}{RESET}")
            return
        except Exception as e:
            print(f"{YELLOW}[SERIAL] {port} failed: {e}{RESET}")

def send_probe(fd):
    time.sleep(0.3)
    nonce = bytes([0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88])
    pkt = struct.pack(">IBH", 0xFFFFFFFF, 0x86, 8) + nonce + bytes(48)
    try:
        os.write(fd, b"\x07" + pkt[:63])
        ts = time.strftime("%H:%M:%S")
        print(f"{BLUE}[{ts}] PROBE → ESP32: CTAPHID_INIT{RESET}")
    except Exception as e:
        print(f"{RED}[PROBE] write failed: {e}{RESET}")

# ── MAIN ──
print(f"\n{BOLD}{CYAN}FIDO2 LIVE DEBUG MONITOR{RESET}")
analyze_descriptor()

threading.Thread(target=serial_reader, daemon=True).start()

try:
    fd = os.open(HIDRAW, os.O_RDWR | os.O_NONBLOCK)
    print(f"{GREEN}[HID] Opened {HIDRAW} - monitoring all packets{RESET}")
    print(f"{YELLOW}>>> NOW open https://webauthn.io and click Register <<<{RESET}\n")
    
    threading.Thread(target=send_probe, args=(fd,), daemon=True).start()
    
    while True:
        r, _, _ = select.select([fd], [], [], 60)
        if not r:
            print(f"{YELLOW}[{time.strftime('%H:%M:%S')}] Still waiting for HID activity... (Chrome not sending anything){RESET}")
            continue
        data = os.read(fd, 65)
        ts = time.strftime("%H:%M:%S")
        decoded = decode_pkt(data)
        print(f"{GREEN}[{ts}] ← ESP32 ({len(data):2d}B): {decoded}  raw={data[:10].hex()}{RESET}")
except PermissionError:
    print(f"{RED}Permission denied. Run: sudo chmod 666 /dev/hidraw*{RESET}")
except Exception as e:
    print(f"{RED}Error: {e}{RESET}")
finally:
    try: os.close(fd)
    except: pass
