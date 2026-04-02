import ctypes
import ctypes.wintypes as wintypes
from state import InputState
from hid_keymap import VK_MODIFIER_MAP, VK_TO_HID, VK_TO_CONSUMER

# ═══════════════════════════════════════════════════════════════════
#  WinAPI Constants
# ═══════════════════════════════════════════════════════════════════

user32 = ctypes.windll.user32
kernel32 = ctypes.windll.kernel32

WH_KEYBOARD_LL = 13
WH_MOUSE_LL    = 14

WM_KEYDOWN     = 0x0100;  WM_KEYUP       = 0x0101
WM_SYSKEYDOWN  = 0x0104;  WM_SYSKEYUP    = 0x0105
WM_LBUTTONDOWN = 0x0201;  WM_LBUTTONUP   = 0x0202
WM_RBUTTONDOWN = 0x0204;  WM_RBUTTONUP   = 0x0205
WM_MBUTTONDOWN = 0x0207;  WM_MBUTTONUP   = 0x0208
WM_MOUSEWHEEL  = 0x020A;  WM_MOUSEHWHEEL = 0x020E
WM_XBUTTONDOWN = 0x020B;  WM_XBUTTONUP   = 0x020C
WM_MOUSEMOVE   = 0x0200

WM_INPUT       = 0x00FF
WM_DESTROY     = 0x0002

VK_SCROLL      = 0x91
VK_F8          = 0x77
WHEEL_DELTA    = 120

LLKHF_EXTENDED = 0x01

# Raw Input constants
RID_INPUT             = 0x10000003
RIM_TYPEMOUSE         = 0
RIDEV_INPUTSINK       = 0x00000100
MOUSE_MOVE_RELATIVE   = 0x00

RI_MOUSE_BUTTON_1_DOWN = 0x0001
RI_MOUSE_BUTTON_1_UP   = 0x0002
RI_MOUSE_BUTTON_2_DOWN = 0x0004
RI_MOUSE_BUTTON_2_UP   = 0x0008
RI_MOUSE_BUTTON_3_DOWN = 0x0010
RI_MOUSE_BUTTON_3_UP   = 0x0020
RI_MOUSE_BUTTON_4_DOWN = 0x0040
RI_MOUSE_BUTTON_4_UP   = 0x0080
RI_MOUSE_BUTTON_5_DOWN = 0x0100
RI_MOUSE_BUTTON_5_UP   = 0x0200
RI_MOUSE_WHEEL         = 0x0400
RI_MOUSE_HWHEEL        = 0x0800

CS_HREDRAW     = 0x0002
HWND_MESSAGE   = wintypes.HWND(-3)

# ═══════════════════════════════════════════════════════════════════
#  WinAPI Structures
# ═══════════════════════════════════════════════════════════════════

class KBDLLHOOKSTRUCT(ctypes.Structure):
    _fields_ = [
        ("vkCode",      wintypes.DWORD),
        ("scanCode",    wintypes.DWORD),
        ("flags",       wintypes.DWORD),
        ("time",        wintypes.DWORD),
        ("dwExtraInfo", ctypes.POINTER(ctypes.c_ulong)),
    ]

class POINT(ctypes.Structure):
    _fields_ = [("x", ctypes.c_long), ("y", ctypes.c_long)]

class MSLLHOOKSTRUCT(ctypes.Structure):
    _fields_ = [
        ("pt",          POINT),
        ("mouseData",   wintypes.DWORD),
        ("flags",       wintypes.DWORD),
        ("time",        wintypes.DWORD),
        ("dwExtraInfo", ctypes.POINTER(ctypes.c_ulong)),
    ]

HOOKPROC = ctypes.CFUNCTYPE(
    ctypes.c_long,      # LRESULT
    ctypes.c_int,       # nCode
    wintypes.WPARAM,    # wParam
    wintypes.LPARAM,    # lParam
)

class RAWINPUTDEVICE(ctypes.Structure):
    _fields_ = [
        ("usUsagePage", ctypes.c_ushort),
        ("usUsage",     ctypes.c_ushort),
        ("dwFlags",     wintypes.DWORD),
        ("hwndTarget",  wintypes.HWND),
    ]

class RAWINPUTHEADER(ctypes.Structure):
    _fields_ = [
        ("dwType",  wintypes.DWORD),
        ("dwSize",  wintypes.DWORD),
        ("hDevice", wintypes.HANDLE),
        ("wParam",  wintypes.WPARAM),
    ]

class RAWMOUSE_BUTTONS(ctypes.Structure):
    _fields_ = [
        ("usButtonFlags", ctypes.c_ushort),
        ("usButtonData",  ctypes.c_ushort),
    ]

class RAWMOUSE_UNION(ctypes.Union):
    _fields_ = [
        ("ulButtons", wintypes.ULONG),
        ("buttons",   RAWMOUSE_BUTTONS),
    ]

class RAWMOUSE(ctypes.Structure):
    _fields_ = [
        ("usFlags",            ctypes.c_ushort),
        ("_buttons",           RAWMOUSE_UNION),
        ("ulRawButtons",       wintypes.ULONG),
        ("lLastX",             ctypes.c_long),
        ("lLastY",             ctypes.c_long),
        ("ulExtraInformation", wintypes.ULONG),
    ]

class RAWINPUT_MOUSE(ctypes.Structure):
    """Simplified RAWINPUT structure - header + mouse only."""
    _fields_ = [
        ("header", RAWINPUTHEADER),
        ("mouse",  RAWMOUSE),
    ]

class WNDCLASSEXW(ctypes.Structure):
    _fields_ = [
        ("cbSize",        wintypes.UINT),
        ("style",         wintypes.UINT),
        ("lpfnWndProc",   ctypes.c_void_p),
        ("cbClsExtra",    ctypes.c_int),
        ("cbWndExtra",    ctypes.c_int),
        ("hInstance",     wintypes.HINSTANCE),
        ("hIcon",         wintypes.HICON),
        ("hCursor",       wintypes.HANDLE),
        ("hbrBackground", wintypes.HANDLE),
        ("lpszMenuName",  wintypes.LPCWSTR),
        ("lpszClassName", wintypes.LPCWSTR),
        ("hIconSm",       wintypes.HICON),
    ]

WNDPROC = ctypes.WINFUNCTYPE(
    ctypes.c_long,      # LRESULT
    wintypes.HWND,      # hWnd
    wintypes.UINT,      # uMsg
    wintypes.WPARAM,    # wParam
    wintypes.LPARAM,    # lParam
)

user32.RegisterRawInputDevices.argtypes = [ctypes.POINTER(RAWINPUTDEVICE), wintypes.UINT, wintypes.UINT]
user32.RegisterRawInputDevices.restype = wintypes.BOOL
user32.GetRawInputData.argtypes = [wintypes.HANDLE, wintypes.UINT, ctypes.c_void_p, ctypes.POINTER(wintypes.UINT), wintypes.UINT]
user32.GetRawInputData.restype = wintypes.UINT
user32.SetWindowsHookExW.argtypes = [ctypes.c_int, HOOKPROC, wintypes.HINSTANCE, wintypes.DWORD]
user32.SetWindowsHookExW.restype = ctypes.c_void_p
user32.CallNextHookEx.argtypes = [ctypes.c_void_p, ctypes.c_int, wintypes.WPARAM, wintypes.LPARAM]
user32.CallNextHookEx.restype = ctypes.c_long
user32.UnhookWindowsHookEx.argtypes = [ctypes.c_void_p]
user32.UnhookWindowsHookEx.restype = wintypes.BOOL
kernel32.GetModuleHandleW.argtypes = [wintypes.LPCWSTR]
kernel32.GetModuleHandleW.restype = wintypes.HINSTANCE
user32.RegisterClassExW.argtypes = [ctypes.POINTER(WNDCLASSEXW)]
user32.RegisterClassExW.restype = wintypes.ATOM
user32.CreateWindowExW.argtypes = [
    wintypes.DWORD, wintypes.LPCWSTR, wintypes.LPCWSTR, wintypes.DWORD,
    ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int,
    wintypes.HWND, wintypes.HMENU, wintypes.HINSTANCE, wintypes.LPVOID
]
user32.CreateWindowExW.restype = wintypes.HWND
user32.DefWindowProcW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
user32.DefWindowProcW.restype = ctypes.c_long

# ═══════════════════════════════════════════════════════════════════
#  Manager to handle hooks and state
# ═══════════════════════════════════════════════════════════════════

class InputHookManager:
    def __init__(self, state: InputState, toggle_vk: int = VK_F8, toggle_label: str = "F8"):
        self.state = state
        self.toggle_vk = toggle_vk
        self.toggle_label = toggle_label
        self.toggle_pressed = False
        self.wnd_proc_cb = WNDPROC(self._wnd_proc)
        self.keyboard_proc_cb = HOOKPROC(self._keyboard_proc)
        self.mouse_proc_cb = HOOKPROC(self._mouse_proc)
        self.hwnd = None
        self.kbd_hook = None
        self.mouse_hook = None

    def start(self):
        self.hwnd = self._create_raw_input_window()
        self._register_raw_input(self.hwnd)
        self.kbd_hook = user32.SetWindowsHookExW(WH_KEYBOARD_LL, self.keyboard_proc_cb, kernel32.GetModuleHandleW(None), 0)
        self.mouse_hook = user32.SetWindowsHookExW(WH_MOUSE_LL, self.mouse_proc_cb, kernel32.GetModuleHandleW(None), 0)
        if not self.kbd_hook or not self.mouse_hook:
            raise RuntimeError("Failed to install hooks")

    def stop(self):
        if self.kbd_hook:
            user32.UnhookWindowsHookEx(self.kbd_hook)
        if self.mouse_hook:
            user32.UnhookWindowsHookEx(self.mouse_hook)
        if self.hwnd:
            user32.DestroyWindow(self.hwnd)

    def process_messages(self):
        msg = wintypes.MSG()
        while user32.GetMessageW(ctypes.byref(msg), None, 0, 0) != 0:
            user32.TranslateMessage(ctypes.byref(msg))
            user32.DispatchMessageW(ctypes.byref(msg))

    def _process_raw_input(self, lParam: int):
        dwSize = wintypes.UINT(0)
        user32.GetRawInputData(wintypes.HANDLE(lParam), RID_INPUT, None, ctypes.byref(dwSize), ctypes.sizeof(RAWINPUTHEADER))
        if dwSize.value == 0:
            return

        raw = RAWINPUT_MOUSE()
        result = user32.GetRawInputData(wintypes.HANDLE(lParam), RID_INPUT, ctypes.byref(raw), ctypes.byref(dwSize), ctypes.sizeof(RAWINPUTHEADER))
        if result == ctypes.c_uint(-1).value or raw.header.dwType != RIM_TYPEMOUSE:
            return

        mouse = raw.mouse
        with self.state.lock:
            if not self.state.kvm_active:
                return

            if mouse.usFlags == MOUSE_MOVE_RELATIVE:
                self.state.mouse_dx += mouse.lLastX
                self.state.mouse_dy += mouse.lLastY

            flags = mouse._buttons.buttons.usButtonFlags
            if flags & RI_MOUSE_BUTTON_1_DOWN: self.state.mouse_buttons |= 0x01
            if flags & RI_MOUSE_BUTTON_1_UP:   self.state.mouse_buttons &= ~0x01
            if flags & RI_MOUSE_BUTTON_2_DOWN: self.state.mouse_buttons |= 0x02
            if flags & RI_MOUSE_BUTTON_2_UP:   self.state.mouse_buttons &= ~0x02
            if flags & RI_MOUSE_BUTTON_3_DOWN: self.state.mouse_buttons |= 0x04
            if flags & RI_MOUSE_BUTTON_3_UP:   self.state.mouse_buttons &= ~0x04
            if flags & RI_MOUSE_BUTTON_4_DOWN: self.state.mouse_buttons |= 0x08
            if flags & RI_MOUSE_BUTTON_4_UP:   self.state.mouse_buttons &= ~0x08
            if flags & RI_MOUSE_BUTTON_5_DOWN: self.state.mouse_buttons |= 0x10
            if flags & RI_MOUSE_BUTTON_5_UP:   self.state.mouse_buttons &= ~0x10

            if flags & RI_MOUSE_WHEEL:
                delta = ctypes.c_short(mouse._buttons.buttons.usButtonData).value
                self.state.mouse_wheel += delta // WHEEL_DELTA

            if flags & RI_MOUSE_HWHEEL:
                delta = ctypes.c_short(mouse._buttons.buttons.usButtonData).value
                self.state.mouse_pan += delta // WHEEL_DELTA

    def _wnd_proc(self, hwnd, msg, wparam, lparam):
        if msg == WM_INPUT:
            self._process_raw_input(lparam)
            return 0
        if msg == WM_DESTROY:
            user32.PostQuitMessage(0)
            return 0
        return user32.DefWindowProcW(hwnd, msg, wparam, lparam)

    def _keyboard_proc(self, nCode: int, wParam: int, lParam: int) -> int:
        if nCode >= 0:
            kbd = KBDLLHOOKSTRUCT.from_address(lParam)
            vk = kbd.vkCode
            is_down = wParam in (WM_KEYDOWN, WM_SYSKEYDOWN)
            is_up = wParam in (WM_KEYUP, WM_SYSKEYUP)
            is_extended = bool(kbd.flags & LLKHF_EXTENDED)

            if vk == self.toggle_vk:
                if is_down and not self.toggle_pressed:
                    self.toggle_pressed = True
                    with self.state.lock:
                        self.state.kvm_active = not self.state.kvm_active
                        active = self.state.kvm_active
                        if not active:
                            self.state.reset_state()
                    status = "ON" if active else "OFF"
                    print(f"[KVM] {status} (toggle: {self.toggle_label})")
                elif is_up:
                    self.toggle_pressed = False
                return 1

            with self.state.lock:
                if self.state.kvm_active:
                    VK_INSERT = 0x2D
                    if vk == VK_INSERT and is_down and (self.state.modifiers & 0x22):
                        self.state.start_paste()
                        return 1

                    if self.state.pasting:
                        if vk == 0x1B and is_down:  # Escape
                            self.state.cancel_paste()
                        return 1

                    if vk == 0x0D and is_extended:
                        hid_code = 0x58
                        if is_down: self.state.pressed_keys.add(hid_code)
                        else: self.state.pressed_keys.discard(hid_code)
                        self.state.kbd_dirty = True
                    elif vk in VK_MODIFIER_MAP:
                        bit = VK_MODIFIER_MAP[vk]
                        if is_down: self.state.modifiers |= (1 << bit)
                        else: self.state.modifiers &= ~(1 << bit)
                        self.state.kbd_dirty = True
                    elif vk in VK_TO_CONSUMER:
                        usage = VK_TO_CONSUMER[vk]
                        if is_down: self.state.consumer_usage = usage
                        else:
                            if self.state.consumer_usage == usage:
                                self.state.consumer_usage = 0
                        self.state.consumer_dirty = True
                    else:
                        hid_code = VK_TO_HID.get(vk)
                        if hid_code is not None:
                            if is_down: self.state.pressed_keys.add(hid_code)
                            else: self.state.pressed_keys.discard(hid_code)
                            self.state.kbd_dirty = True

                    return 1

        return user32.CallNextHookEx(None, nCode, wParam, lParam)

    def _mouse_proc(self, nCode: int, wParam: int, lParam: int) -> int:
        if nCode >= 0:
            with self.state.lock:
                if self.state.kvm_active:
                    return 1
        return user32.CallNextHookEx(None, nCode, wParam, lParam)

    def _create_raw_input_window(self) -> wintypes.HWND:
        hInstance = kernel32.GetModuleHandleW(None)
        class_name = "ESP32_RawInput_Class"
        wc = WNDCLASSEXW()
        wc.cbSize = ctypes.sizeof(WNDCLASSEXW)
        wc.lpfnWndProc = ctypes.cast(self.wnd_proc_cb, ctypes.c_void_p)
        wc.hInstance = hInstance
        wc.lpszClassName = class_name
        atom = user32.RegisterClassExW(ctypes.byref(wc))
        if not atom:
            raise RuntimeError(f"RegisterClassExW failed: {ctypes.GetLastError()}")
        hwnd = user32.CreateWindowExW(0, class_name, "ESP32 Raw Input", 0, 0, 0, 0, 0, HWND_MESSAGE, None, hInstance, None)
        if not hwnd:
            raise RuntimeError(f"CreateWindowExW failed")
        return hwnd

    def _register_raw_input(self, hwnd: wintypes.HWND):
        rid = RAWINPUTDEVICE()
        rid.usUsagePage = 0x01
        rid.usUsage = 0x02
        rid.dwFlags = RIDEV_INPUTSINK
        rid.hwndTarget = hwnd
        if not user32.RegisterRawInputDevices(ctypes.byref(rid), 1, ctypes.sizeof(RAWINPUTDEVICE)):
            raise RuntimeError(f"RegisterRawInputDevices failed")
