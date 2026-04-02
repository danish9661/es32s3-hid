"""
Binary UDP protocol for ESP32-S3 KVM.

Packet format: 16 bytes, little-endian.
"""

import struct

PACKET_MAGIC   = 0xCAFE
UDP_PORT       = 4210
PACKET_SIZE    = 16

EVENT_MOUSE    = 0x01
EVENT_KEYBOARD = 0x02
EVENT_CONSUMER = 0x03

# Struct.pack formats (little-endian '<'):
#
# Header (common):     H=magic(2)  I=sequence(4)  B=type(1)  x=pad(1)  = 8 bytes
#
# Mouse payload:       B=buttons(1) h=dx(2) h=dy(2) b=wheel(1) b=pan(1) x=pad(1) = 8 bytes
# Keyboard payload:    B=modifiers(1) B=reserved(1) 6s=keycodes(6)                = 8 bytes
# Consumer payload:    H=usage_id(2) 6x=pad(6)                                    = 8 bytes

FMT_MOUSE    = '<HIBxBhhbbx'   # 16 bytes
FMT_KEYBOARD = '<HIBxBB6s'     # 16 bytes
FMT_CONSUMER = '<HIBxH6x'     # 16 bytes


def pack_mouse(seq: int, buttons: int, dx: int, dy: int,
               wheel: int = 0, pan: int = 0) -> bytes:
    """Packs a mouse event into a UDP packet."""
    return struct.pack(FMT_MOUSE,
                       PACKET_MAGIC, seq, EVENT_MOUSE,
                       buttons, dx, dy, wheel, pan)


def pack_keyboard(seq: int, modifiers: int,
                  keycodes: bytes = b'\x00' * 6) -> bytes:
    """Packs a keyboard event into a UDP packet."""
    kc = (keycodes + b'\x00' * 6)[:6]
    return struct.pack(FMT_KEYBOARD,
                       PACKET_MAGIC, seq, EVENT_KEYBOARD,
                       modifiers, 0x00, kc)


def pack_consumer(seq: int, usage_id: int) -> bytes:
    """Packs a consumer control event into a UDP packet."""
    return struct.pack(FMT_CONSUMER,
                       PACKET_MAGIC, seq, EVENT_CONSUMER,
                       usage_id)


# Size verification
assert struct.calcsize(FMT_MOUSE)    == PACKET_SIZE
assert struct.calcsize(FMT_KEYBOARD) == PACKET_SIZE
assert struct.calcsize(FMT_CONSUMER) == PACKET_SIZE
