import socket
import threading
import time

from protocol import pack_keyboard, pack_mouse, pack_consumer
from state import InputState

def sender_thread(host: str, port: int, rate: int, stop_event: threading.Event, state: InputState, jiggle: bool = False):
    """Thread that sends UDP packets to ESP32 at a fixed rate."""
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    target = (host, port)
    interval = 1.0 / rate

    print(f"[SENDER] Target: {host}:{port} @ {rate}Hz (interval {interval*1000:.1f}ms)")
    if jiggle:
        print("[SENDER] Jiggler enabled")

    jiggle_tick = 0
    heartbeat_tick = 0
    heartbeat_every = max(1, rate)

    while not stop_event.is_set():
        t0 = time.perf_counter()

        with state.lock:
            active = state.kvm_active
            mouse_changed = False

            # ── Keyboard (always - needed for "release all") ──
            kbd_dirty = state.kbd_dirty
            if kbd_dirty:
                modifiers = state.modifiers
                keys_list = list(state.pressed_keys)[:6]
                if len(state.pressed_keys) > 6:
                    keycodes = bytes([0x01] * 6)  # Phantom state
                else:
                    keycodes = bytes(keys_list + [0] * (6 - len(keys_list)))
                state.kbd_dirty = False

            # ── Consumer (multimedia / browser) ──────────────
            con_dirty = state.consumer_dirty
            if con_dirty:
                consumer_usage = state.consumer_usage
                state.consumer_dirty = False

            if active:
                # ── Mouse ────────────────────────────────────────
                dx = state.mouse_dx
                dy = state.mouse_dy
                wheel = state.mouse_wheel
                pan = state.mouse_pan
                buttons = state.mouse_buttons
                buttons_prev = state.mouse_buttons_prev

                # Reset accumulators
                state.mouse_dx = 0
                state.mouse_dy = 0
                state.mouse_wheel = 0
                state.mouse_pan = 0
                state.mouse_buttons_prev = buttons

                mouse_changed = (
                    dx != 0 or dy != 0 or
                    wheel != 0 or pan != 0 or
                    buttons != buttons_prev
                )

                # Clamp to int16/int8 range
                dx = max(-32767, min(32767, dx))
                dy = max(-32767, min(32767, dy))
                wheel = max(-127, min(127, wheel))
                pan = max(-127, min(127, pan))

            # ── Jiggler ─────────────────────────────────────────
            send_jiggle = False
            if jiggle:
                jiggle_tick += 1
                if jiggle_tick >= 25:
                    jiggle_tick = 0
                    send_jiggle = True

            send_heartbeat = False
            heartbeat_tick += 1
            if heartbeat_tick >= heartbeat_every:
                heartbeat_tick = 0
                send_heartbeat = True

            # ── Sending ─────────────────────────────────────────
            if active:
                if mouse_changed:
                    seq = state.next_seq()
                    pkt = pack_mouse(seq, buttons, dx, dy, wheel, pan)
                    sock.sendto(pkt, target)
                    send_jiggle = False

                if kbd_dirty:
                    seq = state.next_seq()
                    pkt = pack_keyboard(seq, modifiers, keycodes)
                    sock.sendto(pkt, target)
                elif state.pasting:
                    # Paste mode: type one phase per sender cycle, with pacing
                    PASTE_TICK_SKIP = 1

                    if state.paste_tick_counter > 0:
                        state.paste_tick_counter -= 1
                    else:
                        idx = state.paste_index
                        if idx < len(state.paste_chars):
                            hid_code, mods = state.paste_chars[idx]
                            if state.paste_phase == 0:
                                # Key press
                                seq = state.next_seq()
                                kc = bytes([hid_code, 0, 0, 0, 0, 0])
                                pkt = pack_keyboard(seq, mods, kc)
                                sock.sendto(pkt, target)
                                state.paste_phase = 1
                                state.paste_tick_counter = PASTE_TICK_SKIP
                            else:
                                # Key release
                                seq = state.next_seq()
                                pkt = pack_keyboard(seq, 0, bytes(6))
                                sock.sendto(pkt, target)
                                state.paste_phase = 0
                                state.paste_index += 1
                                state.paste_tick_counter = PASTE_TICK_SKIP
                        else:
                            count = len(state.paste_chars)
                            state.pasting = False
                            state.paste_chars = []
                            state.paste_index = 0
                            state.paste_tick_counter = 0
                            print(f"[PASTE] Done ({count} chars)")

                if con_dirty:
                    seq = state.next_seq()
                    pkt = pack_consumer(seq, consumer_usage)
                    sock.sendto(pkt, target)
            elif kbd_dirty or con_dirty:
                # KVM just disabled - send "release all" reports
                if kbd_dirty:
                    seq = state.next_seq()
                    pkt = pack_keyboard(seq, modifiers, keycodes)
                    sock.sendto(pkt, target)
                if con_dirty:
                    seq = state.next_seq()
                    pkt = pack_consumer(seq, consumer_usage)
                    sock.sendto(pkt, target)

            if send_heartbeat:
                # Keep-alive packet so ESP can report link status even when KVM mode is OFF.
                seq = state.next_seq()
                pkt = pack_mouse(seq, state.mouse_buttons, 0, 0, 0, 0)
                sock.sendto(pkt, target)

            if send_jiggle:
                seq = state.next_seq()
                pkt = pack_mouse(seq, state.mouse_buttons, 0, 0, 0, 0)
                sock.sendto(pkt, target)

        # Precise timing (busy-wait for the last µs)
        elapsed = time.perf_counter() - t0
        remaining = interval - elapsed
        if remaining > 0.001:
            time.sleep(remaining - 0.0005)
        while time.perf_counter() - t0 < interval:
            pass

    sock.close()
    print("[SENDER] Stopped")
