"""Own-window X11 policy and input shape; no desktop/session configuration.

Uses system X11/Xext already required by Qt's xcb backend. Calls stay on the
GUI thread and accept only the window IDs created by the companion itself.
"""
from __future__ import annotations

import ctypes as C
from ctypes.util import find_library


class _MessageData(C.Union):
    _fields_ = [("b", C.c_char * 20), ("s", C.c_short * 10), ("l", C.c_long * 5)]


class _ClientMessage(C.Structure):
    _fields_ = [("type", C.c_int), ("serial", C.c_ulong), ("send_event", C.c_int),
                ("display", C.c_void_p), ("window", C.c_ulong),
                ("message_type", C.c_ulong), ("format", C.c_int), ("data", _MessageData)]


class _Event(C.Union):
    _fields_ = [("client", _ClientMessage), ("padding", C.c_long * 24)]


class X11WindowPolicy:
    def __init__(self):
        self.x11 = C.CDLL(find_library("X11") or "libX11.so.6")
        self.xext = C.CDLL(find_library("Xext") or "libXext.so.6")
        x = self.x11
        x.XOpenDisplay.argtypes = [C.c_char_p]; x.XOpenDisplay.restype = C.c_void_p
        x.XCloseDisplay.argtypes = [C.c_void_p]
        x.XInternAtom.argtypes = [C.c_void_p, C.c_char_p, C.c_int]; x.XInternAtom.restype = C.c_ulong
        x.XDefaultRootWindow.argtypes = [C.c_void_p]; x.XDefaultRootWindow.restype = C.c_ulong
        x.XGetWindowProperty.argtypes = [C.c_void_p, C.c_ulong, C.c_ulong, C.c_long, C.c_long,
            C.c_int, C.c_ulong, C.POINTER(C.c_ulong), C.POINTER(C.c_int), C.POINTER(C.c_ulong),
            C.POINTER(C.c_ulong), C.POINTER(C.POINTER(C.c_ubyte))]
        x.XChangeProperty.argtypes = [C.c_void_p, C.c_ulong, C.c_ulong, C.c_ulong, C.c_int,
                                      C.c_int, C.POINTER(C.c_ubyte), C.c_int]
        x.XSendEvent.argtypes = [C.c_void_p, C.c_ulong, C.c_int, C.c_long, C.POINTER(_Event)]
        x.XSync.argtypes = [C.c_void_p, C.c_int]
        x.XFree.argtypes = [C.c_void_p]
        x.XSetErrorHandler.argtypes = [C.c_void_p]; x.XSetErrorHandler.restype = C.c_void_p
        x.XCreateBitmapFromData.argtypes = [C.c_void_p, C.c_ulong, C.c_char_p, C.c_uint, C.c_uint]
        x.XCreateBitmapFromData.restype = C.c_ulong
        x.XFreePixmap.argtypes = [C.c_void_p, C.c_ulong]
        self.xext.XShapeCombineMask.argtypes = [C.c_void_p, C.c_ulong, C.c_int,
            C.c_int, C.c_int, C.c_ulong, C.c_int]
        self.display = x.XOpenDisplay(None)
        if not self.display:
            raise OSError("Cannot connect own window policy to the X11 display.")
        self.errors = 0
        handler = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_void_p)
        self.error_handler = handler(self._on_error)
        self.state = self.atom("_NET_WM_STATE")
        self.skip_taskbar = self.atom("_NET_WM_STATE_SKIP_TASKBAR")
        self.skip_pager = self.atom("_NET_WM_STATE_SKIP_PAGER")

    def _on_error(self, display, event):
        self.errors += 1
        return 0

    def atom(self, name):
        return self.x11.XInternAtom(self.display, name.encode("ascii"), False)

    def close(self):
        if self.display:
            self.x11.XCloseDisplay(self.display)
            self.display = None

    def _atoms(self, window):
        actual, fmt, count, after = C.c_ulong(), C.c_int(), C.c_ulong(), C.c_ulong()
        data = C.POINTER(C.c_ubyte)()
        result = self.x11.XGetWindowProperty(self.display, window, self.state, 0, 256,
            False, 4, C.byref(actual), C.byref(fmt), C.byref(count), C.byref(after), C.byref(data))
        try:
            if result == 0 and actual.value == 4 and fmt.value == 32 and count.value <= 256:
                values = C.cast(data, C.POINTER(C.c_ulong))
                return [values[i] for i in range(count.value)]
            return []
        finally:
            if data:
                self.x11.XFree(data)

    def _begin(self):
        self.errors = 0
        return self.x11.XSetErrorHandler(C.cast(self.error_handler, C.c_void_p))

    def _finish(self, previous):
        self.x11.XSync(self.display, False)
        self.x11.XSetErrorHandler(previous)
        if self.errors:
            raise OSError("Own X11 window changed while applying its policy.")

    def hide_from_taskbar(self, window):
        previous = self._begin()
        try:
            values = list(dict.fromkeys(self._atoms(window) + [self.skip_taskbar, self.skip_pager]))
            atoms = (C.c_ulong * len(values))(*values)
            self.x11.XChangeProperty(self.display, window, self.state, 4, 32, 0,
                C.cast(atoms, C.POINTER(C.c_ubyte)), len(values))
            event = _Event()
            event.client.type = 33  # ClientMessage
            event.client.send_event = True
            event.client.display = self.display
            event.client.window = window
            event.client.message_type = self.state
            event.client.format = 32
            event.client.data.l[:] = [1, self.skip_taskbar, self.skip_pager, 1, 0]
            self.x11.XSendEvent(self.display, self.x11.XDefaultRootWindow(self.display), False,
                (1 << 20) | (1 << 19), C.byref(event))
        finally:
            self._finish(previous)

    def set_input_bitmap(self, window, width, height, bitmap):
        if not (1 <= width <= 2048 and 1 <= height <= 2048 and len(bitmap) == ((width + 7) // 8) * height):
            raise ValueError("Invalid own-window input bitmap.")
        previous = self._begin()
        pixmap = 0
        try:
            pixmap = self.x11.XCreateBitmapFromData(self.display, window, bitmap, width, height)
            if not pixmap:
                raise OSError("Cannot create own-window input bitmap.")
            # ShapeInput=2, ShapeSet=0. Bounding/render shape remains untouched.
            self.xext.XShapeCombineMask(self.display, window, 2, 0, 0, pixmap, 0)
        finally:
            if pixmap:
                self.x11.XFreePixmap(self.display, pixmap)
            self._finish(previous)
