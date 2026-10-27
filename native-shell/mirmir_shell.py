#!/usr/bin/env python3
"""Native transparent shell for real Unreal-exported RGBA frames; no renderer fallback."""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import re
import random
import stat
import subprocess
import sys
import tempfile
import time
import uuid
import zlib

WINDOWS = sys.platform == "win32"
if not WINDOWS:
    import fcntl

try:
    import numpy as np
except ImportError:
    np = None

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen" if "--self-test" in sys.argv else ("windows" if WINDOWS else "xcb"))
from PyQt6.QtCore import QPoint, QRect, QSize, Qt, QTimer
from PyQt6.QtGui import QActionGroup, QBitmap, QColor, QCursor, QGuiApplication, QIcon, QImage, QPainter, QPixmap, QRegion
from PyQt6.QtWidgets import QApplication, QMenu, QSystemTrayIcon, QWidget

ROOT = Path(sys.executable).resolve().parent if getattr(sys, "frozen", False) else Path(__file__).resolve().parents[1]
MAX_PNG_BYTES = 16 * 1024 * 1024
ACTIVITIES = ["Keyfi yerinde", "Geziniyor", "Temizleniyor", "Dinleniyor", "Oynuyor", "Mama molası", "İlgi bekliyor", "Mırlıyor"]
APPEARANCES = {"van": "Van", "british": "British Shorthair", "tuxedo": "Siyah-beyaz"}


class FramePairMismatch(ValueError):
    """PNG and metadata may briefly refer to different atomic publications."""


def json_file(path):
    try:
        if path.stat().st_size > 65536:
            return {}
        data = json.loads(path.read_text(encoding="utf-8-sig"))
        return data if isinstance(data, dict) else {}
    except (OSError, ValueError):
        return {}


def atomic_json(path, data):
    path.parent.mkdir(mode=0o700, parents=True, exist_ok=True)
    temporary = path.with_name(path.name + "." + uuid.uuid4().hex + ".tmp")
    with temporary.open("x", encoding="utf-8") as output:
        json.dump(data, output, ensure_ascii=False)
    temporary.replace(path)


def windows_owned(path):
    """Compare the file owner SID to this process's user, without opening other processes."""
    import ctypes
    from ctypes import wintypes
    advapi, kernel = ctypes.WinDLL("advapi32", use_last_error=True), ctypes.WinDLL("kernel32", use_last_error=True)
    descriptor, owner, token = ctypes.c_void_p(), ctypes.c_void_p(), wintypes.HANDLE()
    advapi.GetNamedSecurityInfoW.argtypes = [wintypes.LPWSTR, ctypes.c_int, wintypes.DWORD,
        ctypes.POINTER(ctypes.c_void_p), ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p, ctypes.POINTER(ctypes.c_void_p)]
    advapi.OpenProcessToken.argtypes = [wintypes.HANDLE, wintypes.DWORD, ctypes.POINTER(wintypes.HANDLE)]
    advapi.GetTokenInformation.argtypes = [wintypes.HANDLE, ctypes.c_int, ctypes.c_void_p, wintypes.DWORD, ctypes.POINTER(wintypes.DWORD)]
    advapi.EqualSid.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
    kernel.GetCurrentProcess.restype = wintypes.HANDLE
    kernel.CloseHandle.argtypes = [wintypes.HANDLE]
    kernel.LocalFree.argtypes = [ctypes.c_void_p]
    try:
        if advapi.GetNamedSecurityInfoW(str(path), 1, 1, ctypes.byref(owner), None, None, None, ctypes.byref(descriptor)):
            return False
        if not advapi.OpenProcessToken(kernel.GetCurrentProcess(), 8, ctypes.byref(token)):
            return False
        size = wintypes.DWORD()
        advapi.GetTokenInformation(token, 1, None, 0, ctypes.byref(size))
        data = ctypes.create_string_buffer(size.value)
        if not advapi.GetTokenInformation(token, 1, data, size, ctypes.byref(size)):
            return False
        return bool(advapi.EqualSid(owner, ctypes.cast(data, ctypes.POINTER(ctypes.c_void_p))[0]))
    finally:
        if token:
            kernel.CloseHandle(token)
        if descriptor:
            kernel.LocalFree(descriptor)


def owned_path(path, info):
    if WINDOWS:
        return not getattr(info, "st_file_attributes", 0) & 0x400 and windows_owned(path)
    return info.st_uid == os.getuid()


def owned_directory(path, private=False):
    try:
        info = path.lstat()
        return stat.S_ISDIR(info.st_mode) and owned_path(path, info) and (WINDOWS or not private or info.st_mode & 0o077 == 0)
    except OSError:
        return False


def tmpfs_directory(path):
    """Use the longest matching Linux mount; a private disk dir is not tmpfs."""
    try:
        resolved = path.resolve()
        best = (0, "")
        for line in Path("/proc/self/mountinfo").read_text().splitlines():
            before, after = line.split(" - ", 1)
            encoded = before.split()[4]
            mount = Path(re.sub(r"\\([0-7]{3})", lambda match: chr(int(match[1], 8)), encoded))
            if resolved.is_relative_to(mount) and len(str(mount)) >= best[0]:
                best = (len(str(mount)), after.split()[0])
        return best[1] == "tmpfs"
    except (OSError, ValueError, IndexError):
        return False


def frame_paths(explicit=None, environment=None, platform=None):
    environment = os.environ if environment is None else environment
    windows = (sys.platform if platform is None else platform) == "win32"
    home = Path.home()
    def app_root(key, fallback):
        value = environment.get(key)
        path = Path(value).expanduser() if isinstance(value, str) and value else fallback
        return (path if path.is_absolute() else fallback).resolve() / "mirmir/unreal"
    local = Path(environment.get("LOCALAPPDATA", home / "AppData/Local")).expanduser()
    if not local.is_absolute():
        local = home / "AppData/Local"
    data = app_root("XDG_DATA_HOME", local / "Mirmir/data" if windows else home / ".local/share")
    state = app_root("XDG_STATE_HOME", local / "Mirmir/state" if windows else home / ".local/state")
    result = {"data": data, "state": state, "legacy": state}
    if explicit is not None:
        return dict(result, frames=Path(explicit).expanduser().absolute(), storage="explicit")
    if windows:
        namespace = hashlib.sha256((str(state) + "\0" + str(data)).encode()).hexdigest()[:16]
        return dict(result, frames=local / "Mirmir/runtime" / namespace, storage="windows_local_runtime")
    runtime_text = environment.get("XDG_RUNTIME_DIR", "")
    runtime = Path(runtime_text) if runtime_text else None
    if runtime is not None and runtime.is_absolute() and owned_directory(runtime, private=True) and tmpfs_directory(runtime):
        relative = ["mirmir", "unreal"]
        normal_data = (home / ".local/share").resolve() / "mirmir/unreal"
        normal_state = (home / ".local/state").resolve() / "mirmir/unreal"
        if (data, state) != (normal_data, normal_state):
            namespace = hashlib.sha256((str(state) + "\0" + str(data)).encode()).hexdigest()[:16]
            relative += ["instances", namespace]
        candidate = runtime
        try:
            for part in relative:
                candidate /= part
                candidate.mkdir(mode=0o700, exist_ok=True)
                if not owned_directory(candidate, private=True):
                    raise ValueError("Runtime output directory is not private and owned.")
            return dict(result, frames=candidate, storage="runtime_tmpfs")
        except (OSError, ValueError):
            pass
    return dict(result, frames=state, storage="state_fallback")


def instance_candidates(paths, explicit=False):
    candidates = [paths["frames"]]
    if not explicit:
        pointer = paths["state"] / "frame-location.json"
        try:
            info = pointer.lstat()
            if stat.S_ISREG(info.st_mode) and owned_path(pointer, info):
                record = json_file(pointer)
                name = record.get("frames_directory")
                if (record.get("data_directory") == str(paths["data"]) and record.get("state_directory") == str(paths["state"]) and isinstance(name, str) and
                        Path(name).is_absolute() and owned_directory(Path(name))):
                    candidates.append(Path(name))
        except OSError:
            pass
        candidates.append(paths["legacy"])
    return list(dict.fromkeys(candidates))


def open_owned_lock(frames, create=False):
    path = frames / "shell.lock"
    if path.exists() and (path.is_symlink() or not owned_path(path, path.lstat())):
        raise ValueError("Kabuğun kilidi bu kullanıcıya ait değil.")
    flags = os.O_RDWR | getattr(os, "O_CLOEXEC", 0) | getattr(os, "O_NOFOLLOW", 0) | (os.O_CREAT if create else 0)
    descriptor = os.open(path, flags, 0o600)
    info = os.fstat(descriptor)
    if not stat.S_ISREG(info.st_mode) or not owned_path(path, info) or info.st_size > 4096:
        os.close(descriptor)
        raise ValueError("Kabuğun kilidi bu kullanıcıya ait geçerli bir dosya değil.")
    return os.fdopen(descriptor, "r+", encoding="utf-8")


def lock_instance(handle):
    if not WINDOWS:
        fcntl.flock(handle, fcntl.LOCK_EX | fcntl.LOCK_NB)
        return
    # Windows locks are mandatory: keep the locked byte beyond the small JSON
    # metadata so another instance can read the owner's stop token safely.
    import ctypes
    import msvcrt
    from ctypes import wintypes
    class Overlapped(ctypes.Structure):
        _fields_ = [("Internal", ctypes.c_size_t), ("InternalHigh", ctypes.c_size_t),
                    ("Offset", wintypes.DWORD), ("OffsetHigh", wintypes.DWORD), ("hEvent", wintypes.HANDLE)]
    overlap = Overlapped(); overlap.Offset = 4096
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.LockFileEx.argtypes = [wintypes.HANDLE, wintypes.DWORD, wintypes.DWORD, wintypes.DWORD, wintypes.DWORD, ctypes.POINTER(Overlapped)]
    if not kernel.LockFileEx(msvcrt.get_osfhandle(handle.fileno()), 3, 0, 1, 0, ctypes.byref(overlap)):
        code = ctypes.get_last_error()
        if code in (33, 158):
            raise BlockingIOError("Mırmır kilidi kullanımda.")
        raise ctypes.WinError(code)


def active_instance(candidates):
    for frames in candidates:
        try:
            with open_owned_lock(frames) as handle:
                try:
                    lock_instance(handle)
                except BlockingIOError:
                    return frames
        except FileNotFoundError:
            continue
    return None


def runtime_ui_state(metadata, frames, data):
    needs = metadata.get("needs")
    complete = isinstance(needs, dict) and "appearance" in metadata and "appearance_ready" in metadata
    state = {} if complete else (json_file(frames / "desktop-status.json") or json_file(data / "desktop-status.json"))
    if isinstance(needs, dict):
        state.update(needs)
    for key in ("appearance", "appearance_ready"):
        if key in metadata:
            state[key] = metadata[key]
    return state


def alpha_region(image, ratio, threshold=32, use_numpy=True):
    """Build logical-pixel input/visual mask from this image's own alpha only."""
    logical = image.scaled(math.ceil(image.width() / ratio), math.ceil(image.height() / ratio),
                           Qt.AspectRatioMode.IgnoreAspectRatio, Qt.TransformationMode.FastTransformation)
    rgba = logical.convertToFormat(QImage.Format.Format_RGBA8888)
    raw = rgba.constBits().asstring(rgba.sizeInBytes())
    width, height, stride = rgba.width(), rgba.height(), rgba.bytesPerLine()
    if np is not None and use_numpy:
        alpha = np.frombuffer(raw, dtype=np.uint8).reshape(height, stride)[:, 3:width * 4:4]
        packed = np.packbits(alpha >= threshold, axis=1, bitorder="little")
        # Qt consumes the packed one-bit rows in C++; no per-pixel Python loop.
        bitmap = QBitmap.fromData(QSize(width, height), packed.tobytes(), QImage.Format.Format_MonoLSB)
        return QRegion(bitmap), int(alpha.min()), int(alpha.max())
    region, minimum, maximum = QRegion(), 255, 0
    for y in range(height):
        alphas = raw[y * stride + 3:y * stride + width * 4:4]
        start = None
        for x, alpha in enumerate(alphas):
            minimum, maximum = min(minimum, alpha), max(maximum, alpha)
            if alpha >= threshold and start is None:
                start = x
            elif alpha < threshold and start is not None:
                region |= QRegion(start, y, x - start, 1)
                start = None
        if start is not None:
            region |= QRegion(start, y, width - start, 1)
    return region, minimum, maximum


def frame_identity(metadata):
    session = metadata.get("session_id")
    if not isinstance(session, str) or not session.strip() or len(session) > 128:
        raise FramePairMismatch("Unreal oturum kimliği geçersiz.")
    values = []
    for key, low, high in [("frame_seq", 1, 2**53 - 1), ("width", 1, 2048), ("height", 1, 2048),
                           ("png_bytes", 1, MAX_PNG_BYTES), ("png_crc32", 0, 2**32 - 1)]:
        value = metadata.get(key)
        if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value) or int(value) != value or not low <= value <= high:
            raise FramePairMismatch("Unreal kare bilgisi geçersiz: " + key)
        values.append(int(value))
    return (session, *values)


def decode_frame(metadata, png):
    identity = frame_identity(metadata)
    _, _, width, height, size, checksum = identity
    if len(png) != size or zlib.crc32(png) & 0xffffffff != checksum:
        raise FramePairMismatch("Unreal PNG ve kare bilgisi eşleşmesi bekleniyor.")
    image = QImage.fromData(png, "PNG")
    if image.isNull() or not image.hasAlphaChannel() or image.width() != width or image.height() != height:
        raise FramePairMismatch("Unreal PNG boyutu veya alpha biçimi kare bilgisiyle eşleşmiyor.")
    return image, identity


def activity_label(activity):
    if isinstance(activity, (int, float)) and not isinstance(activity, bool) and math.isfinite(activity) and int(activity) == activity and 0 <= activity < len(ACTIVITIES):
        return ACTIVITIES[int(activity)]
    aliases = dict(zip(["idle", "wander", "groom", "sleep", "play", "eat", "seekattention", "petted"], ACTIVITIES))
    return aliases.get(str(activity).lower().replace("_", ""), "Hazır")


def clamp_position(point, size, available):
    return QPoint(max(available.left(), min(point.x(), available.right() - size.width() + 1)),
                  max(available.top(), min(point.y(), available.bottom() - size.height() + 1)))


def stroke_target(position, size):
    return [max(-1.0, min(1.0, position.x() * 2 / max(1, size.width()) - 1)),
            max(-1.0, min(1.0, position.y() * 2 / max(1, size.height()) - 1))]


class CursorInterest:
    """Sparse attention windows; stores timing only, never pointer history."""
    def __init__(self, now=None, random_delay=random.uniform):
        self.random_delay = random_delay
        self.next_window = (time.monotonic() if now is None else now) + random_delay(8, 14)
        self.until, self.next_sample = 0.0, 0.0

    def due(self, now):
        if self.until and now >= self.until:
            self.until = 0.0
            self.next_window = now + self.random_delay(12, 20)
        if not self.until and now >= self.next_window:
            self.until, self.next_sample = now + 1.5, now
        if self.until and now >= self.next_sample:
            self.next_sample = now + 0.2  # No catch-up/backlog after a slow frame.
            return True
        return False


def cursor_payload(pointer, rectangle, ratio, metadata, wall_ms):
    width, height = metadata.get("width"), metadata.get("height")
    producer, session = metadata.get("producer_pid"), metadata.get("session_id")
    if (not isinstance(session, str) or not session or isinstance(producer, bool) or not isinstance(producer, int) or producer <= 0 or
            any(isinstance(v, bool) or not isinstance(v, (int, float)) or not math.isfinite(v) or v <= 0 for v in (width, height, ratio))):
        raise ValueError("Fare ilgisi için geçerli motor karesi gerekiyor.")
    dx = (pointer.x() - rectangle.x() - rectangle.width() / 2) * ratio
    dy = (pointer.y() - rectangle.y() - rectangle.height() / 2) * ratio
    dx, dy = max(-width / 2, min(dx, width / 2)), max(-height / 2, min(dy, height / 2))
    return {"schema_version": 1, "session_id": session, "producer_pid": producer, "active": True,
            "sampled_at_unix_ms": int(wall_ms), "expires_at_unix_ms": int(wall_ms) + 750,
            "target_offset_px": [dx, dy], "target_normalized": [dx / (width / 2), dy / (height / 2)]}


def child_environment(frames, data, logs, environment=None, platform=None):
    child = dict(os.environ if environment is None else environment)
    windows = (sys.platform if platform is None else platform) == "win32"
    child.update(MIRMIR_FRAME_DIR=str(frames), XDG_DATA_HOME=str(data.parents[1]), XDG_STATE_HOME=str(logs.parents[1]))
    if not windows:
        child["SDL_VIDEODRIVER"] = "x11"
        child.setdefault("SDL_AUDIODRIVER", "pulseaudio")
        child.setdefault("SDL_AUDIO_DRIVER", child["SDL_AUDIODRIVER"])
    return child


def windows_overlay(window_id):
    """Qt owns per-pixel alpha/region; keep this HWND layered and nonactivating."""
    if not WINDOWS:
        return
    import ctypes
    from ctypes import wintypes
    user = ctypes.WinDLL("user32", use_last_error=True)
    get_style = user.GetWindowLongPtrW if ctypes.sizeof(ctypes.c_void_p) == 8 else user.GetWindowLongW
    set_style = user.SetWindowLongPtrW if ctypes.sizeof(ctypes.c_void_p) == 8 else user.SetWindowLongW
    get_style.argtypes = [wintypes.HWND, ctypes.c_int]; get_style.restype = ctypes.c_ssize_t
    set_style.argtypes = [wintypes.HWND, ctypes.c_int, ctypes.c_ssize_t]; set_style.restype = ctypes.c_ssize_t
    user.SetWindowPos.argtypes = [wintypes.HWND, wintypes.HWND, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int, wintypes.UINT]
    # WS_EX_TRANSPARENT would also discard clicks on the cat. Qt's alpha32
    # mask limits our region; zero-alpha layered pixels pass through natively.
    style = get_style(window_id, -20) | 0x80000 | 0x80 | 0x8000000
    ctypes.set_last_error(0)
    if not set_style(window_id, -20, style) and ctypes.get_last_error():
        raise ctypes.WinError(ctypes.get_last_error())
    if not user.SetWindowPos(window_id, wintypes.HWND(-1), 0, 0, 0, 0, 0x1 | 0x2 | 0x10 | 0x20):
        raise ctypes.WinError(ctypes.get_last_error())


def configured_map(project_root=ROOT):
    preview = json_file(project_root / ".local/preview.json")
    package = preview.get("preview_map") or preview.get("map_path")
    if not isinstance(package, str) or not re.fullmatch(r"/Game/[A-Za-z0-9_/-]+", package) or ".." in package:
        raise ValueError("Unreal önizleme haritası hazır değil; önce bootstrap-preview.py çalıştırılmalı.")
    world = project_root / "unreal/Mirmir/Content" / (package.removeprefix("/Game/") + ".umap")
    if not world.is_file():
        raise ValueError("Hazırlanmış Unreal harita dosyası bulunamadı: " + package)
    return package


def launch_plan(project_root, frames, environment=None, log_dir=None, platform=None):
    """Prefer a completed local Shipping archive, invoking its binary directly."""
    environment = os.environ if environment is None else environment
    windows = (sys.platform if platform is None else platform) == "win32"
    target = "Win64" if windows else "Linux"
    package = json_file(project_root / ".local/package.json")
    def package_path(value):
        path = Path(value)
        return (path if path.is_absolute() else project_root / path).resolve()
    if package.get("configuration") == "Shipping" and package.get("platform") == target and package.get("packaging_command_succeeded") is True:
        try:
            binary, launcher = package_path(package["binary"]), package_path(package["launcher"])
            archive = package_path(package["archive_directory"])
            binary.relative_to(archive); launcher.relative_to(archive)
            world = package.get("map")
            if (binary.name == ("Mirmir-Win64-Shipping.exe" if windows else "Mirmir-Linux-Shipping") and binary.is_file() and (windows or os.access(binary, os.X_OK)) and
                    launcher.name == ("Mirmir.exe" if windows else "Mirmir.sh") and launcher.is_file() and isinstance(world, str) and
                    re.fullmatch(r"/Game/[A-Za-z0-9_/-]+", world) and ".." not in world):
                # Official LinuxPlatform.Automation code-based bootstrap supplies
                # ShortProjectName (Mirmir) before user args. Preserve it without
                # the .sh intermediate process, so producer_pid matches Popen.pid.
                prefix = [str(binary), "Mirmir", world]
                kind, cwd = "shipping", launcher.parent
            else:
                raise ValueError("Incomplete package")
        except (KeyError, OSError, TypeError, ValueError):
            prefix = None
    else:
        prefix = None
    if prefix is None:
        config = json_file(project_root / ".local/engine.json")
        engine = environment.get("MIRMIR_UE_ROOT") or config.get("engine_root")
        editor_path = "Engine/Binaries/Win64/UnrealEditor.exe" if windows else "Engine/Binaries/Linux/UnrealEditor"
        binary = Path(engine) / editor_path if isinstance(engine, str) and engine else None
        if not binary or not binary.is_file() or not (windows or os.access(binary, os.X_OK)):
            raise ValueError("Çalışır Shipping paketi veya Unreal motoru bulunamadı; engine.json / MIRMIR_UE_ROOT gerekli.")
        prefix = [str(binary), str(project_root / "unreal/Mirmir/Mirmir.uproject"), configured_map(project_root), "-game"]
        kind, cwd = "editor", project_root
    log_dir = frames if log_dir is None else log_dir
    return {"kind": kind, "cwd": cwd, "command": prefix + ["-windowed", "-MirmirDesktop", "-RenderOffscreen", "-nosplash",
            "-ResX=640", "-ResY=640", "-ExecCmds=t.MaxFPS 30", f"-abslog={log_dir / 'engine-session.log'}"]}


def acquire_instance(frames):
    frames.mkdir(mode=0o700, parents=True, exist_ok=True)
    if not owned_directory(frames, private=True):
        raise ValueError("Canlı kare dizini özel ve bu kullanıcıya ait olmalı.")
    handle = open_owned_lock(frames, create=True)
    try:
        lock_instance(handle)
    except BlockingIOError:
        handle.close(); return None
    owner = {"owner_token": uuid.uuid4().hex, "started_at": time.time(), "shell_pid": os.getpid()}
    handle.seek(0); handle.truncate(); json.dump(owner, handle); handle.flush()
    return handle, owner


def valid_stop(command, token, started):
    created = command.get("created_at")
    return (command.get("command") == "quit" and command.get("owner_token") == token and
            isinstance(created, (int, float)) and not isinstance(created, bool) and math.isfinite(created) and
            started <= created <= time.time() + 5 and time.time() - created <= 30)


def request_stop(frames, wait=True):
    try:
        with open_owned_lock(frames) as handle:
            try:
                lock_instance(handle)
                return 0  # A stale lock file is not a running process.
            except BlockingIOError:
                owner = json_file(frames / "shell.lock")
                if not isinstance(owner.get("owner_token"), str):
                    raise ValueError("Çalışan kabuğun sahiplik bilgisi henüz hazır değil.")
                atomic_json(frames / "shell-command.json", {"command": "quit", "owner_token": owner["owner_token"], "created_at": time.time()})
            if wait:
                deadline = time.monotonic() + 12
                while time.monotonic() < deadline:
                    try:
                        lock_instance(handle)
                        return 0
                    except BlockingIOError:
                        time.sleep(0.1)
                raise ValueError("Kapatma komutu iletildi; kabuk henüz kapanmadı. Başka süreç sonlandırılmadı.")
        return 0
    except FileNotFoundError:
        return 0
    except (OSError, ValueError) as failure:
        print(str(failure), file=sys.stderr); return 1


class PropsWindow(QWidget):
    """Only real Unreal habitat pixels; fixed to the companion's home anchor."""
    def __init__(self, companion):
        super().__init__(None, Qt.WindowType.Tool | Qt.WindowType.FramelessWindowHint |
                         Qt.WindowType.WindowStaysOnTopHint | Qt.WindowType.WindowDoesNotAcceptFocus)
        self.companion = companion
        self.setAttribute(Qt.WidgetAttribute.WA_TranslucentBackground)
        self.setAttribute(Qt.WidgetAttribute.WA_ShowWithoutActivating)
        self.setFocusPolicy(Qt.FocusPolicy.NoFocus)
        self.setCursor(Qt.CursorShape.PointingHandCursor)
        self.setWindowTitle("Mırmır · Unreal yaşam alanı")
        self.image, self.key, self.metadata = QImage(), None, {}
        self.ready = False
        self.pair_until = 0.0

    def update_frame(self, metadata, image, key, region):
        self.image, self.key, self.metadata = image, key, metadata
        ratio = self.companion.devicePixelRatioF()
        self.resize(math.ceil(image.width() / ratio), math.ceil(image.height() / ratio))
        self.setMask(region)
        self.update()

    def show_at_home(self):
        if self.companion.home is None or self.image.isNull():
            self.hide(); return
        offset = self.metadata.get("desktop_offset_px", [0, 0])
        if not isinstance(offset, list) or len(offset) != 2 or any(isinstance(v, bool) or not isinstance(v, (int, float)) or not math.isfinite(v) for v in offset):
            self.hide(); return
        ratio = self.companion.devicePixelRatioF()
        anchor = self.companion.home + QPoint(round(offset[0] / ratio), round(offset[1] / ratio))
        screen = QGuiApplication.screenAt(self.companion.home) or QGuiApplication.primaryScreen()
        self.move(clamp_position(anchor, self.size(), screen.availableGeometry()))
        was_visible = self.isVisible()
        self.show()
        if not was_visible:
            self.companion.raise_()  # Keep the cat above its habitat, without activation.
        if WINDOWS and QGuiApplication.platformName() == "windows" and not getattr(self, "windows_style_ready", False):
            windows_overlay(int(self.winId())); self.windows_style_ready = True

    def paintEvent(self, event):
        painter = QPainter(self)
        painter.drawImage(self.rect(), self.image)

    def mousePressEvent(self, event):
        if event.button() == Qt.MouseButton.RightButton:
            self.companion.tray.contextMenu().popup(event.globalPosition().toPoint())
        elif event.button() == Qt.MouseButton.LeftButton:
            if not self.mask().contains(QPoint(int(event.position().x()), int(event.position().y()))):
                return
            if event.modifiers() & Qt.KeyboardModifier.ShiftModifier:
                self.companion.drag_start = event.globalPosition().toPoint()
                self.companion.drag_home = QPoint(self.companion.home)
                self.setCursor(Qt.CursorShape.ClosedHandCursor)
                return
            point = [event.position().x() * self.image.width() / max(1, self.width()),
                     event.position().y() * self.image.height() / max(1, self.height())]
            centers = [self.metadata.get("bed_center_px"), self.metadata.get("bowl_center_px")]
            if all(isinstance(center, list) and len(center) == 2 and all(isinstance(v, (int, float)) and not isinstance(v, bool) and math.isfinite(v) for v in center) for center in centers):
                distances = [sum((point[i] - center[i]) ** 2 for i in (0, 1)) for center in centers]
                self.companion.send("rest" if distances[0] <= distances[1] else "feed")

    def mouseMoveEvent(self, event):
        if self.companion.drag_start is not None:
            self.companion.home = self.companion.drag_home + event.globalPosition().toPoint() - self.companion.drag_start

    def mouseReleaseEvent(self, event):
        self.companion.drag_start = self.companion.drag_home = None
        self.setCursor(Qt.CursorShape.PointingHandCursor)


class MirmirShell(QWidget):
    def __init__(self, args, lease=None):
        super().__init__(None, Qt.WindowType.Tool | Qt.WindowType.FramelessWindowHint |
                         Qt.WindowType.WindowStaysOnTopHint | Qt.WindowType.WindowDoesNotAcceptFocus)
        self.setAttribute(Qt.WidgetAttribute.WA_TranslucentBackground)
        self.setAttribute(Qt.WidgetAttribute.WA_ShowWithoutActivating)
        self.setFocusPolicy(Qt.FocusPolicy.NoFocus)
        self.setMouseTracking(True)
        self.setCursor(Qt.CursorShape.PointingHandCursor)
        self.setWindowTitle("Mırmır · Unreal masaüstü katmanı")
        self.args, self.process, self.engine_log = args, None, None
        self.engine_kind = "none"
        self.appearance_ready = False
        self.confirmed_appearance = None
        self.last_appearance_state = {}
        paths = frame_paths(args.frame_dir)
        self.data, self.logs, self.frames = paths["data"], paths["state"], paths["frames"]
        self.frame_storage = paths["storage"]
        self.image, self.frame_key, self.home = QImage(), None, None
        self.valid_until, self.last_metadata = 0.0, {}
        self.offset, self.drag_start, self.drag_home = QPoint(), None, None
        self.last_pet, self.closed, self.last_error = 0.0, False, ""
        self.last_boundary_rest = 0.0
        self.cursor_interest = CursorInterest()
        self.cursor_enabled = not getattr(args, "no_cursor_interest", False)
        self.cursor_published = False
        self.props_window = PropsWindow(self)
        self.last_interaction_serial = 0
        self.started = time.monotonic()
        self.started_wall = lease[1]["started_at"] if lease else time.time()
        self.owner_token = lease[1]["owner_token"] if lease else uuid.uuid4().hex
        atomic_json(self.logs / "frame-location.json", {"schema_version": 1, "frames_directory": str(self.frames),
                    "data_directory": str(self.data), "state_directory": str(self.logs), "storage": self.frame_storage,
                    "shell_pid": os.getpid(), "created_at": self.started_wall})
        icon_image = QPixmap(64, 64)
        icon_image.fill(Qt.GlobalColor.transparent)
        painter = QPainter(icon_image)
        painter.setBrush(QColor("#d99c61")); painter.setPen(Qt.PenStyle.NoPen)
        painter.drawEllipse(4, 4, 56, 56); painter.setPen(QColor("#30231b"))
        painter.drawText(icon_image.rect(), Qt.AlignmentFlag.AlignCenter, "M"); painter.end()
        icon = QIcon(str(ROOT / "assets/icon.svg")) if (ROOT / "assets/icon.svg").is_file() else QIcon(icon_image)
        self.tray = QSystemTrayIcon(icon, self)
        menu = QMenu()
        self.summary = menu.addAction("Unreal görüntüsü bekleniyor…"); self.summary.setEnabled(False)
        self.needs = menu.addAction(""); self.needs.setEnabled(False)
        self.feedback = menu.addAction(""); self.feedback.setEnabled(False); self.feedback.setVisible(False)
        menu.addSeparator()
        for label, command in [("Sev", "pet"), ("Mama ver", "feed"), ("Oyna", "play"), ("Dinlen", "rest"), ("Sesi aç / kapat", "mute")]:
            action = menu.addAction(label)
            action.triggered.connect(lambda checked=False, cmd=command: self.send(cmd))
        self.appearance_menu = menu.addMenu("Kedi görünümü")
        self.appearance_group = QActionGroup(self)
        self.appearance_group.setExclusive(True)
        self.appearance_actions = {}
        for key, label in APPEARANCES.items():
            action = self.appearance_menu.addAction(label)
            action.setCheckable(True)
            self.appearance_group.addAction(action)
            action.triggered.connect(lambda checked=False, appearance=key: self.send("appearance", appearance))
            self.appearance_actions[key] = action
        self.appearance_menu.setEnabled(False)
        cursor_action = menu.addAction("Fareye arada ilgi göster")
        cursor_action.setCheckable(True); cursor_action.setChecked(self.cursor_enabled)
        cursor_action.toggled.connect(self.set_cursor_interest)
        menu.addSeparator()
        menu.addAction("Kapat", self.close_companion)
        self.tray.setContextMenu(menu); self.tray.show()
        self.timer = QTimer(self); self.timer.setTimerType(Qt.TimerType.PreciseTimer)
        self.timer.timeout.connect(self.poll); self.timer.start(33)
        if not args.no_engine:
            self.start_engine()
        QApplication.instance().aboutToQuit.connect(self.stop_engine)

    def start_engine(self):
        try:
            plan = launch_plan(ROOT, self.frames, log_dir=self.logs)
        except ValueError as failure:
            self.error(str(failure), notify=True); return
        self.frames.mkdir(mode=0o700, parents=True, exist_ok=True)
        # Commands are transient UI events, never a persistent queue. In
        # particular, a second quit left by a previous shutdown must not close
        # a newly launched game. Touch only our small UUID-named command files.
        try:
            for pending in (self.data / "commands").glob("*.json"):
                queued_command = json_file(pending).get("command")
                if (re.fullmatch(r"[0-9a-f]{32}\.json", pending.name) and not pending.is_symlink() and
                        pending.stat().st_size <= 1024 and pending.stat().st_mtime < self.started_wall and
                        isinstance(queued_command, str) and
                        queued_command in {"pet", "feed", "play", "rest", "mute", "quit", "appearance"}):
                    pending.unlink(missing_ok=True)
        except OSError as failure:
            self.error("Önceki oturumun geçici komutları temizlenemedi: " + str(failure), True)
            return
        self.logs.mkdir(mode=0o700, parents=True, exist_ok=True)
        self.engine_log = (self.logs / "engine-shell.log").open("a")
        try:
            child_env = child_environment(self.frames, self.data, self.logs)
            # UE 5.8's Linux offscreen path otherwise selects SDL's silent dummy
            # backend. The old variable bypasses that gate; SDL3 reads the new
            # hint name. Keep both local to this child and respect user choices.
            options = {"creationflags": subprocess.CREATE_NO_WINDOW} if WINDOWS else {}
            self.process = subprocess.Popen(plan["command"], cwd=plan["cwd"], env=child_env, stdout=self.engine_log, stderr=subprocess.STDOUT, **options)
            self.engine_kind = plan["kind"]
        except OSError as failure:
            self.error("Unreal başlatılamadı: " + str(failure), notify=True)

    def sync_appearance(self, state):
        self.last_appearance_state = dict(state)
        appearance = state.get("appearance")
        appearance = appearance.lower() if isinstance(appearance, str) else None
        self.confirmed_appearance = appearance if appearance in APPEARANCES else None
        self.appearance_ready = state.get("appearance_ready") is True and self.confirmed_appearance is not None
        # Reflect the engine-confirmed choice, including a rejected/failed
        # request; never present the menu click itself as a completed change.
        self.appearance_group.setExclusive(False)
        for key, action in self.appearance_actions.items():
            action.setChecked(key == self.confirmed_appearance)
        self.appearance_group.setExclusive(True)
        self.appearance_menu.setEnabled(self.appearance_ready)
        self.appearance_menu.menuAction().setToolTip("" if self.appearance_ready else "Kedi görünümü henüz hazır değil.")

    def send(self, command, appearance=None, stroke_normalized=None):
        payload = {"command": command}
        if stroke_normalized is not None:
            if (command != "pet" or not isinstance(stroke_normalized, (list, tuple)) or len(stroke_normalized) != 2 or
                    any(isinstance(v, bool) or not isinstance(v, (int, float)) or not math.isfinite(v) for v in stroke_normalized)):
                return False
            payload["stroke_normalized"] = [max(-1.0, min(1.0, float(v))) for v in stroke_normalized]
        if command == "appearance":
            if not isinstance(appearance, str) or appearance not in APPEARANCES or not self.appearance_ready:
                return False
            payload["appearance"] = appearance
        elif appearance is not None:
            return False
        if command == "pet":
            now = time.monotonic()
            if now - self.last_pet < 0.7:
                return False
            self.last_pet = now
        try:
            atomic_json(self.data / "commands" / (uuid.uuid4().hex + ".json"), payload)
            if command == "appearance":
                self.sync_appearance(self.last_appearance_state)
            return True
        except OSError as failure:
            self.error("Komut kaydedilemedi: " + str(failure), notify=True)
            return False

    def error(self, message, notify=False):
        self.clear_cursor()
        self.props_window.hide()
        self.props_window.ready = False
        self.hide()
        self.valid_until = 0.0
        self.appearance_ready = False
        self.appearance_menu.setEnabled(False)
        self.summary.setText(message); self.tray.setToolTip("Mırmır: " + message)
        if notify and message != self.last_error:
            self.tray.showMessage("Mırmır", message, QSystemTrayIcon.MessageIcon.Warning, 6000)
        if message != self.last_error:
            try:
                atomic_json(self.frames / "shell-status.json", {"implementation": "qt-unreal-frame-shell", "ready": False,
                            "engine_started": self.process is not None, "test_mode": self.args.no_engine, "error": message,
                            "appearance": self.confirmed_appearance, "appearance_ready": False})
            except OSError:
                pass  # The tray remains usable even if the state disk is unwritable.
        self.last_error = message

    def clear_cursor(self):
        if self.cursor_published:
            try:
                (self.frames / "cursor.json").unlink(missing_ok=True)
            except OSError:
                pass  # Producer also enforces the short expiration time.
            self.cursor_published = False

    def set_cursor_interest(self, enabled):
        self.cursor_enabled = enabled
        self.cursor_interest = CursorInterest()
        self.clear_cursor()

    def sample_cursor(self):
        # Isolated synthetic tests and Qt offscreen never read the real pointer.
        if (not self.cursor_enabled or self.args.no_engine or self.process is None or not self.isVisible() or
                self.drag_start is not None or QGuiApplication.platformName() in ("offscreen", "minimal") or
                time.monotonic() >= self.valid_until or not owned_directory(self.frames, private=True)):
            self.clear_cursor(); return
        due = self.cursor_interest.due(time.monotonic())
        if not self.cursor_interest.until:
            self.clear_cursor(); return
        if not due:
            return
        pointer = QCursor.pos()  # Ephemeral current position; never retained/logged.
        if QGuiApplication.screenAt(pointer) != self.screen():
            self.clear_cursor(); return
        try:
            payload = cursor_payload(pointer, self.geometry(), self.devicePixelRatioF(), self.last_metadata, time.time() * 1000)
            atomic_json(self.frames / "cursor.json", payload)
            self.cursor_published = True
        except (OSError, ValueError):
            self.clear_cursor()

    def poll_props(self, parent_metadata):
        props = parent_metadata.get("props")
        window = self.props_window
        if (self.args.no_engine or self.process is None or
                parent_metadata.get("producer_pid") != self.process.pid or
                parent_metadata.get("running") is not True or parent_metadata.get("ready") is not True or
                not isinstance(props, dict) or props.get("ready") is not True):
            window.ready = False; window.hide(); return
        try:
            name = props.get("frame_file")
            if not isinstance(name, str) or Path(name).name != name:
                raise ValueError("Invalid habitat frame path")
            metadata = dict(props, session_id=parent_metadata.get("session_id"))
            key = frame_identity(metadata)
            if window.key and key[0] == window.key[0] and (key[1] < window.key[1] or (key[:2] == window.key[:2] and key != window.key)):
                raise FramePairMismatch("Habitat frame identity changed inconsistently")
            if key != window.key:
                with (self.frames / name).open("rb") as source:
                    if os.fstat(source.fileno()).st_size > MAX_PNG_BYTES:
                        raise ValueError("Habitat PNG too large")
                    png = source.read(MAX_PNG_BYTES + 1)
                image, key = decode_frame(metadata, png)
                region, low, high = alpha_region(image, self.devicePixelRatioF())
                if region.isEmpty() or low >= 32 or high < 32:
                    raise ValueError("Habitat requires a transparent RGBA silhouette")
                window.update_frame(props, image, key, region)
            else:
                window.metadata = props
            # A static habitat PNG may be old. Current validated parent frames
            # establish liveness; this CRC identity establishes its own pixels.
            window.pair_until = time.monotonic() + 3
            window.ready = True
            window.show_at_home()
        except (OSError, ValueError, TypeError):
            window.ready = False
            if (window.key and window.key[0] == parent_metadata.get("session_id") and time.monotonic() < window.pair_until):
                window.show_at_home()
            else:
                window.hide()

    def sync_interaction(self, metadata):
        interaction = metadata.get("interaction")
        if not isinstance(interaction, dict):
            return
        serial = interaction.get("serial")
        if isinstance(serial, bool) or not isinstance(serial, (int, float)) or not math.isfinite(serial) or serial <= self.last_interaction_serial:
            return
        self.last_interaction_serial = serial
        action = interaction.get("action")
        message = interaction.get("message")
        if isinstance(action, str) and action.lower() in {"feed", "play", "rest"} and isinstance(message, str) and message.strip():
            self.feedback.setText(message[:240]); self.feedback.setVisible(True)

    def wait_for_pair(self, message):
        if not self.image.isNull() and time.monotonic() < self.valid_until:
            # Keep only the last matched image and its matched movement offset.
            self.show()
            self.tray.setToolTip("Mırmır · Kare eşleşmesi bekleniyor")
            self.publish_status(waiting_for_pair=True)
        else:
            self.error(message, notify=time.monotonic() - self.started > 30)

    def publish_status(self, waiting_for_pair=False):
        try:
            atomic_json(self.frames / "shell-status.json", {"implementation": "qt-unreal-frame-shell", "engine_started": self.process is not None,
                        "engine_kind": self.engine_kind,
                        "test_mode": self.args.no_engine, "ready": True, "waiting_for_pair": waiting_for_pair,
                        "session_id": self.last_metadata.get("session_id"), "frame_seq": self.last_metadata.get("frame_seq"),
                        "geometry": [self.x(), self.y(), self.width(), self.height()], "device_pixel_ratio": self.devicePixelRatioF(),
                        "window_id": int(self.winId()), "appearance": self.confirmed_appearance, "appearance_ready": self.appearance_ready,
                        "props_ready": self.props_window.ready, "props_visible": self.props_window.isVisible(),
                        "props_frame_seq": self.props_window.metadata.get("frame_seq"),
                        "props_geometry": [self.props_window.x(), self.props_window.y(), self.props_window.width(), self.props_window.height()],
                        "props_window_id": int(self.props_window.winId()) if self.props_window.isVisible() else None,
                        "interaction_serial": self.last_interaction_serial})
        except OSError:
            self.error("Masaüstü durumu yazılamadı.")

    def poll(self):
        if self.closed:
            return
        command_path = self.frames / "shell-command.json"
        claimed = self.frames / (".shell-command-" + self.owner_token + ".tmp")
        try:
            command_path.replace(claimed)
            command = json_file(claimed)
            claimed.unlink(missing_ok=True)
            if valid_stop(command, self.owner_token, self.started_wall):
                self.close_companion(); return
        except FileNotFoundError:
            pass
        except OSError:
            self.error("Kabuğun kapatma komutu okunamadı.")
        if self.process is not None and self.process.poll() is not None:
            self.error(f"Unreal kapandı (kod {self.process.returncode}). Günlük: {self.logs / 'engine-session.log'}", True)
            return
        if not self.args.no_engine and self.process is None:
            return  # Do not display a stale PNG when no engine was launched.
        metadata = json_file(self.frames / "runtime.json")
        if metadata.get("error") or metadata.get("status") in ["error", "stopped", "failed"] or metadata.get("running") is not True or metadata.get("ready") is not True:
            self.error(str(metadata.get("error") or "Unreal görüntüsü henüz hazır değil."))
            return
        if not self.args.no_engine and metadata.get("producer_pid") != self.process.pid:
            self.error("Kare bilgisi başlatılan Unreal sürecine ait değil.")
            return
        name = metadata.get("frame_file", "frame.png")
        if not isinstance(name, str) or Path(name).name != name:
            self.error("Unreal görüntü yolu geçersiz."); return
        path = self.frames / name
        try:
            key = frame_identity(metadata)
            if self.frame_key and key[0] == self.frame_key[0] and key[1] < self.frame_key[1]:
                raise FramePairMismatch("Eski Unreal kare sırası reddedildi.")
            if self.frame_key and key[:2] == self.frame_key[:2] and key != self.frame_key:
                raise FramePairMismatch("Aynı Unreal kare kimliğinde çelişkili bilgi reddedildi.")
            if key != self.frame_key or time.monotonic() >= self.valid_until:
                with path.open("rb") as source:
                    stat = os.fstat(source.fileno())
                    if stat.st_size > MAX_PNG_BYTES or time.time() - stat.st_mtime > 3:
                        raise FramePairMismatch("Unreal görüntüsü güncel değil.")
                    if not self.args.no_engine and stat.st_mtime < self.started_wall:
                        raise FramePairMismatch("Yeni Unreal sürecinin ilk karesi bekleniyor.")
                    png = source.read(MAX_PNG_BYTES + 1)  # One file handle/read, then decode these same bytes.
                image, key = decode_frame(metadata, png)
                ratio = self.devicePixelRatioF()
                region, low, high = alpha_region(image, ratio)
                if region.isEmpty() or low >= 32 or high < 32:
                    raise ValueError("Kediyi ayıran saydam alpha kanalı bekleniyor.")
                self.image, self.frame_key = image, key
                self.last_metadata = metadata
                self.valid_until = time.monotonic() + max(0.0, 3.0 - (time.time() - stat.st_mtime))
                self.resize(math.ceil(image.width() / ratio), math.ceil(image.height() / ratio))
                self.setMask(region)
                if self.home is None:
                    available = QGuiApplication.primaryScreen().availableGeometry()
                    # Leave space for the first walks in both screen directions.
                    self.home = QPoint(available.right() - self.width() - 24 - min(280, available.width() // 5),
                                       available.bottom() - self.height() - 24 - min(90, available.height() // 10))
                self.update()
            raw_offset = self.last_metadata.get("desktop_offset_px", [0, 0])
            if not isinstance(raw_offset, list) or len(raw_offset) != 2 or not all(isinstance(v, (int, float)) and math.isfinite(v) for v in raw_offset):
                raise ValueError("Unreal hareket konumu geçersiz.")
            self.offset = QPoint(round(raw_offset[0] / self.devicePixelRatioF()), round(raw_offset[1] / self.devicePixelRatioF()))
            screen = QGuiApplication.screenAt(self.home) or QGuiApplication.primaryScreen()
            requested = self.home + self.offset
            bounded = clamp_position(requested, self.size(), screen.availableGeometry())
            self.move(bounded)
            if bounded != requested and time.monotonic() - self.last_boundary_rest > 3:
                # Stop the actual Unreal actor at a screen edge, rather than
                # letting a clamped window show an endless walking-in-place pose.
                needs = metadata.get("needs")
                activity = needs.get("activity") if isinstance(needs, dict) else None
                # A user-requested game must not be silently cancelled by the
                # window's edge clamp. The actor steers its own path home.
                if activity == "Wander":
                    self.send("rest")
                    self.last_boundary_rest = time.monotonic()
            self.show()
            if WINDOWS and QGuiApplication.platformName() == "windows" and not getattr(self, "windows_style_ready", False):
                windows_overlay(int(self.winId())); self.windows_style_ready = True
            self.last_error = ""
            state = runtime_ui_state(self.last_metadata, self.frames, self.data)
            self.sync_appearance(state)
            self.sync_interaction(metadata)
            self.poll_props(metadata)
            label = "GÖRSEL TEST · Unreal başlatılmadı" if self.args.no_engine else "Mırmır · Unreal Engine"
            self.summary.setText(label)
            self.tray.setToolTip(label + " · " + activity_label(state.get("activity")))
            def percent(key):
                value = state.get(key, 0)
                if not isinstance(value, (int, float)) or not math.isfinite(value):
                    return 0
                return round(value * 100) if value <= 1 else round(value)
            self.needs.setText(f"Tokluk {percent('satiety')} · Enerji {percent('energy')} · Bağ {percent('bond')}")
            self.publish_status()
            self.sample_cursor()
        except FramePairMismatch as failure:
            self.wait_for_pair(str(failure))
        except (OSError, ValueError, TypeError) as failure:
            message = str(failure) if isinstance(failure, ValueError) else "Unreal görüntüsü bekleniyor…"
            if isinstance(failure, OSError):
                self.wait_for_pair(message)
            else:
                self.error(message, notify=time.monotonic() - self.started > 30)

    def paintEvent(self, event):
        painter = QPainter(self)
        painter.drawImage(self.rect(), self.image)

    def mousePressEvent(self, event):
        if event.button() == Qt.MouseButton.LeftButton:
            self.setCursor(Qt.CursorShape.ClosedHandCursor)
            if event.modifiers() & Qt.KeyboardModifier.ShiftModifier:
                self.drag_start, self.drag_home = event.globalPosition().toPoint(), QPoint(self.home)
            else:
                self.send("pet", stroke_normalized=stroke_target(event.position(), self.size()))
        elif event.button() == Qt.MouseButton.RightButton:
            self.tray.contextMenu().popup(event.globalPosition().toPoint())

    def mouseMoveEvent(self, event):
        if self.drag_start is not None:
            self.home = self.drag_home + event.globalPosition().toPoint() - self.drag_start
        elif event.buttons() & Qt.MouseButton.LeftButton:
            # Qt keeps a mouse grab until release. Moving that grab off the
            # alpha silhouette is no longer a stroke on the cat.
            if not self.mask().contains(QPoint(int(event.position().x()), int(event.position().y()))):
                self.unsetCursor(); return
            self.setCursor(Qt.CursorShape.ClosedHandCursor)
            self.send("pet", stroke_normalized=stroke_target(event.position(), self.size()))
        else:
            self.setCursor(Qt.CursorShape.PointingHandCursor)

    def mouseReleaseEvent(self, event):
        self.drag_start = self.drag_home = None
        self.setCursor(Qt.CursorShape.PointingHandCursor)

    def enterEvent(self, event):
        self.setCursor(Qt.CursorShape.PointingHandCursor)

    def leaveEvent(self, event):
        self.unsetCursor()

    def close_companion(self):
        self.clear_cursor()
        self.props_window.hide()
        self.closed = True
        self.timer.stop(); self.hide()
        # aboutToQuit/stop_engine sends exactly one quit and waits for its child.
        QApplication.instance().quit()

    def stop_engine(self):
        self.clear_cursor()
        self.props_window.hide()
        if self.process is not None and self.process.poll() is None:
            self.send("quit")
            try:
                self.process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.process.terminate()
                try:
                    self.process.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    self.process.kill(); self.process.wait(timeout=3)
        if self.engine_log:
            self.engine_log.close()


def self_test():
    assert stroke_target(QPoint(128, 128), QSize(256, 256)) == [0, 0]
    assert stroke_target(QPoint(-100, 1000), QSize(256, 256)) == [-1, 1]
    attention = CursorInterest(now=0, random_delay=lambda low, high: low)
    assert not attention.due(7.9) and attention.due(8) and not attention.due(8.1) and attention.due(8.2)
    assert not attention.due(30) and attention.next_window == 42 and attention.due(42)
    cursor_metadata = {"producer_pid": 123, "session_id": "isolated-session", "width": 512, "height": 512}
    centered = cursor_payload(QPoint(-172, 228), QRect(-300, 100, 256, 256), 2, cursor_metadata, 1000)
    assert centered["target_offset_px"] == [0, 0] and centered["target_normalized"] == [0, 0]
    bounded = cursor_payload(QPoint(1000, -1000), QRect(-300, 100, 256, 256), 2, cursor_metadata, 1000)
    assert bounded["target_offset_px"] == [256, -256] and bounded["target_normalized"] == [1, -1]
    assert bounded["expires_at_unix_ms"] - bounded["sampled_at_unix_ms"] == 750
    assert set(bounded) == {"schema_version", "session_id", "producer_pid", "active", "sampled_at_unix_ms", "expires_at_unix_ms", "target_offset_px", "target_normalized"}
    image = QImage(8, 8, QImage.Format.Format_RGBA8888); image.fill(Qt.GlobalColor.transparent)
    for y in range(2, 6):
        for x in range(2, 6):
            image.setPixelColor(x, y, QColor(200, 140, 80, 255))
    image.setPixelColor(0, 0, QColor(200, 140, 80, 31))
    region, low, high = alpha_region(image, 2)
    assert not region.contains(QPoint(0, 0)) and region.contains(QPoint(1, 1))
    assert region.boundingRect() == QRect(1, 1, 2, 2) and (low, high) == (0, 255)
    assert clamp_position(QPoint(100, -20), QRect(0, 0, 30, 40).size(), QRect(0, 0, 100, 100)) == QPoint(70, 0)
    with tempfile.TemporaryDirectory(prefix="mirmir-shell-unit-") as temporary:
        runtime = Path(temporary) / "runtime"; runtime.mkdir(mode=0o700)
        environment = {"LOCALAPPDATA": str(Path(temporary) / "Local"), "XDG_RUNTIME_DIR": str(runtime), "XDG_DATA_HOME": str(Path(temporary) / "namespace-data"),
                       "XDG_STATE_HOME": str(Path(temporary) / "namespace-state")}
        selected = frame_paths(environment=environment)
        if not WINDOWS and tmpfs_directory(runtime):
            assert selected["storage"] == "runtime_tmpfs" and selected["frames"].is_relative_to(runtime)
            assert owned_directory(selected["frames"], private=True)
            other = frame_paths(environment=dict(environment, XDG_STATE_HOME=str(Path(temporary) / "other-state")))
            assert other["frames"] != selected["frames"]
            assert frame_paths(environment=environment)["frames"] == selected["frames"]
        explicit = frame_paths(Path(temporary) / "explicit", environment)
        assert explicit["storage"] == "explicit" and explicit["frames"] == Path(temporary) / "explicit"
        windows_paths = frame_paths(environment=dict(environment, LOCALAPPDATA=str(Path(temporary) / "Local")), platform="win32")
        assert windows_paths["storage"] == "windows_local_runtime" and windows_paths["frames"].is_relative_to(Path(temporary) / "Local/Mirmir/runtime")
        assert frame_paths(Path(temporary) / "forced", environment, platform="win32")["storage"] == "explicit"
        win_environment = child_environment(windows_paths["frames"], windows_paths["data"], windows_paths["state"], environment={}, platform="win32")
        assert "SDL_VIDEODRIVER" not in win_environment and "SDL_AUDIODRIVER" not in win_environment
        assert win_environment["XDG_DATA_HOME"] == environment["XDG_DATA_HOME"]
        if not WINDOWS:
            runtime.chmod(0o755)
            assert frame_paths(environment=environment)["storage"] == "state_fallback"
            runtime.chmod(0o700)
            link = Path(temporary) / "runtime-link"; link.symlink_to(runtime, target_is_directory=True)
            assert frame_paths(environment=dict(environment, XDG_RUNTIME_DIR=str(link)))["storage"] == "state_fallback"
        legacy = selected["legacy"]
        legacy_lease = acquire_instance(legacy)
        assert active_instance(instance_candidates(selected)) == legacy
        assert request_stop(legacy, wait=False) == 0
        assert valid_stop(json_file(legacy / "shell-command.json"), legacy_lease[1]["owner_token"], legacy_lease[1]["started_at"])
        assert not (selected["frames"] / "shell-command.json").exists() or selected["frames"] == legacy
        legacy_lease[0].close()
        assert active_instance(instance_candidates(selected)) is None
        fixture = Path(temporary) / "preview-project"
        try:
            configured_map(fixture)
            raise AssertionError("Missing preview metadata accepted")
        except ValueError:
            pass
        world = fixture / "unreal/Mirmir/Content/Mirmir/Preview/MirmirPreview.umap"
        world.parent.mkdir(parents=True); world.touch()
        atomic_json(fixture / ".local/preview.json", {"preview_map": "/Game/Mirmir/Preview/MirmirPreview"})
        assert configured_map(fixture) == "/Game/Mirmir/Preview/MirmirPreview"
        editor = fixture / "engine/Engine/Binaries/Linux/UnrealEditor"
        editor.parent.mkdir(parents=True); editor.touch(); editor.chmod(0o755)
        atomic_json(fixture / ".local/engine.json", {"engine_root": str(fixture / "engine")})
        fallback = launch_plan(fixture, fixture / "frames", environment={}, platform="linux")
        assert fallback["kind"] == "editor" and fallback["command"][2] == "/Game/Mirmir/Preview/MirmirPreview"
        assert f"-abslog={fixture / 'logs/engine-session.log'}" in launch_plan(fixture, fixture / "frames", environment={}, log_dir=fixture / "logs", platform="linux")["command"]
        archive = fixture / "archive"
        shipping = archive / "Linux/Mirmir/Binaries/Linux/Mirmir-Linux-Shipping"
        shipping.parent.mkdir(parents=True); shipping.touch(); shipping.chmod(0o755)
        launcher = archive / "Linux/Mirmir.sh"; launcher.touch(); launcher.chmod(0o755)
        atomic_json(fixture / ".local/package.json", {"configuration": "Shipping", "platform": "Linux", "packaging_command_succeeded": True,
                    "archive_directory": str(archive), "binary": str(shipping), "launcher": str(launcher), "map": "/Game/Mirmir/Preview/MirmirPreview"})
        packaged = launch_plan(fixture, fixture / "frames", environment={}, platform="linux")
        assert packaged["kind"] == "shipping" and packaged["command"][:3] == [str(shipping), "Mirmir", "/Game/Mirmir/Preview/MirmirPreview"]
        editor.unlink()
        assert launch_plan(fixture, fixture / "frames", environment={}, platform="linux")["kind"] == "shipping"
        editor.touch(); editor.chmod(0o755); shipping.unlink()
        assert launch_plan(fixture, fixture / "frames", environment={}, platform="linux")["kind"] == "editor"
        windows_binary = archive / "Win64/Mirmir/Binaries/Win64/Mirmir-Win64-Shipping.exe"
        windows_binary.parent.mkdir(parents=True); windows_binary.touch()
        windows_launcher = archive / "Win64/Mirmir.exe"; windows_launcher.touch()
        atomic_json(fixture / ".local/package.json", {"configuration": "Shipping", "platform": "Win64", "packaging_command_succeeded": True,
                    "archive_directory": "archive", "binary": str(windows_binary.relative_to(fixture)),
                    "launcher": str(windows_launcher.relative_to(fixture)), "map": "/Game/Mirmir/Preview/MirmirPreview"})
        assert launch_plan(fixture, fixture / "frames", environment={}, platform="win32")["command"][:3] == [str(windows_binary), "Mirmir", "/Game/Mirmir/Preview/MirmirPreview"]
        assert launch_plan(fixture, fixture / "frames", environment={}, platform="linux")["kind"] == "editor"  # Wrong-platform archive rejected.
        path = Path(temporary) / "commands/one.json"
        atomic_json(path, {"command": "pet"})
        assert json_file(path) == {"command": "pet"} and not list(path.parent.glob("*.tmp"))
        previous_data = os.environ.get("XDG_DATA_HOME")
        previous_state = os.environ.get("XDG_STATE_HOME")
        os.environ["XDG_DATA_HOME"] = str(Path(temporary) / "data")
        os.environ["XDG_STATE_HOME"] = str(Path(temporary) / "state")
        frames = Path(temporary) / "frames"; frames.mkdir(mode=0o700)
        def publish(sequence, **changes):
            image.save(str(frames / "frame.png"))
            png = (frames / "frame.png").read_bytes()
            metadata = {"running": True, "ready": True, "session_id": "synthetic-self-test", "frame_seq": sequence,
                        "width": image.width(), "height": image.height(), "png_bytes": len(png), "png_crc32": zlib.crc32(png), "desktop_offset_px": [0, 0]}
            metadata.update(changes)
            atomic_json(frames / "runtime.json", metadata)
            return metadata
        first = publish(1)
        png = (frames / "frame.png").read_bytes()
        for key, bad in [("session_id", ""), ("frame_seq", 0), ("frame_seq", 1.5), ("width", 9),
                         ("height", 9), ("png_bytes", len(png) + 1), ("png_crc32", zlib.crc32(png) ^ 1)]:
            invalid = dict(first); invalid[key] = bad
            try:
                decode_frame(invalid, png)
                raise AssertionError("Invalid frame accepted: " + key)
            except FramePairMismatch:
                pass
        shell = MirmirShell(argparse.Namespace(no_engine=True, frame_dir=frames))
        shell.timer.stop(); shell.poll()
        original_cursor = QCursor.pos
        def forbidden_pointer():
            raise AssertionError("Isolated test read the real pointer")
        QCursor.pos = forbidden_pointer
        try:
            shell.cursor_interest.next_window = 0
            shell.sample_cursor()
        finally:
            QCursor.pos = original_cursor
        assert not (frames / "cursor.json").exists()
        atomic_json(frames / "cursor.json", bounded); shell.cursor_published = True
        shell.set_cursor_interest(False)
        assert not (frames / "cursor.json").exists()
        pointer = json_file(shell.logs / "frame-location.json")
        assert pointer["frames_directory"] == str(frames) and pointer["data_directory"] == str(shell.data)
        assert shell.isVisible() and shell.mask().contains(QPoint(3, 3)) and not shell.mask().contains(QPoint(0, 0))
        assert json_file(frames / "shell-status.json")["test_mode"] is True
        shell.send("pet"); shell.send("pet")
        assert len(list((shell.data / "commands").glob("*.json"))) == 1
        shell.last_pet = 0
        before_stroke = set((shell.data / "commands").glob("*.json"))
        assert shell.send("pet", stroke_normalized=[-2, 0.5])
        stroke_file = (set((shell.data / "commands").glob("*.json")) - before_stroke).pop()
        assert json_file(stroke_file) == {"command": "pet", "stroke_normalized": [-1, 0.5]}
        assert not shell.send("pet", stroke_normalized=[0, 0])  # Same engagement cooldown.
        assert not shell.send("pet", stroke_normalized=[float("nan"), 0])
        assert not shell.send("play", stroke_normalized=[0, 0])
        local_press = argparse.Namespace(button=lambda: Qt.MouseButton.LeftButton,
            modifiers=lambda: Qt.KeyboardModifier.NoModifier, position=lambda: QPoint(4, 4))
        shell.mousePressEvent(local_press)
        assert shell.cursor().shape() == Qt.CursorShape.ClosedHandCursor
        shell.mouseReleaseEvent(None)
        assert shell.cursor().shape() == Qt.CursorShape.PointingHandCursor
        after_press = set((shell.data / "commands").glob("*.json"))
        shell.last_pet = 0
        shell.mouseMoveEvent(argparse.Namespace(buttons=lambda: Qt.MouseButton.LeftButton, position=lambda: QPoint(-5, -5)))
        assert set((shell.data / "commands").glob("*.json")) == after_press and shell.cursor().shape() == Qt.CursorShape.ArrowCursor
        shell.leaveEvent(None)
        assert shell.cursor().shape() == Qt.CursorShape.ArrowCursor
        shell.enterEvent(None)
        assert shell.cursor().shape() == Qt.CursorShape.PointingHandCursor
        status_path = shell.data / "desktop-status.json"
        atomic_json(status_path, {"appearance": "van", "appearance_ready": True})
        shell.poll()
        preferred = runtime_ui_state({"needs": {"satiety": 0.5, "activity": "Play"}, "appearance": "british", "appearance_ready": False}, frames, shell.data)
        assert preferred == {"satiety": 0.5, "activity": "Play", "appearance": "british", "appearance_ready": False}
        # Regression: edge clamping used to send Rest during user-requested
        # Play, ending a newly tossed ball within one engine tick.
        before_boundary = set((shell.data / "commands").glob("*.json"))
        publish(1, session_id="synthetic-boundary-test", desktop_offset_px=[10000, 0], needs={"activity": "Play"})
        shell.poll()
        assert set((shell.data / "commands").glob("*.json")) == before_boundary
        publish(1); shell.poll()

        # Habitat uses its own CRC and remains fixed at home while the cat
        # moves. Old static pixels are valid only with a live matched parent.
        image.save(str(frames / "props.png")); props_png = (frames / "props.png").read_bytes()
        props = {"ready": True, "frame_file": "props.png", "frame_seq": 1, "width": 8, "height": 8,
                 "png_bytes": len(props_png), "png_crc32": zlib.crc32(props_png),
                 "desktop_offset_px": [0, 0], "bed_center_px": [2, 3], "bowl_center_px": [6, 3]}
        props_parent = dict(first, producer_pid=123, props=props)
        os.utime(frames / "props.png", (time.time() - 100, time.time() - 100))
        shell.poll_props(props_parent)
        assert not shell.props_window.isVisible()  # No-engine mode cannot show a false habitat.
        shell.args.no_engine = False
        shell.process = argparse.Namespace(pid=123)
        shell.poll_props(props_parent)
        assert shell.props_window.ready and shell.props_window.isVisible()
        expected_anchor = clamp_position(shell.home, shell.props_window.size(), QGuiApplication.primaryScreen().availableGeometry())
        assert shell.props_window.pos() == expected_anchor
        shell.offset = QPoint(40, 50); shell.props_window.show_at_home()
        assert shell.props_window.pos() == expected_anchor
        for point, expected_command in [(QPoint(3, 3), "rest"), (QPoint(5, 3), "feed")]:
            before = set((shell.data / "commands").glob("*.json"))
            shell.props_window.mousePressEvent(argparse.Namespace(button=lambda: Qt.MouseButton.LeftButton,
                modifiers=lambda: Qt.KeyboardModifier.NoModifier, position=lambda: point))
            created = set((shell.data / "commands").glob("*.json")) - before
            assert len(created) == 1 and json_file(created.pop()) == {"command": expected_command}
        before = set((shell.data / "commands").glob("*.json"))
        shell.props_window.mousePressEvent(argparse.Namespace(button=lambda: Qt.MouseButton.LeftButton,
            modifiers=lambda: Qt.KeyboardModifier.NoModifier, position=lambda: QPoint(-1, -1)))
        assert set((shell.data / "commands").glob("*.json")) == before
        shell.props_window.pair_until = 0
        shell.poll_props(dict(props_parent, props=dict(props, frame_file="../foreign.png")))
        assert not shell.props_window.isVisible()
        shell.poll_props(props_parent); assert shell.props_window.isVisible()
        shell.poll_props(dict(props_parent, producer_pid=999)); assert not shell.props_window.isVisible()
        shell.poll_props(props_parent); assert shell.props_window.isVisible()
        shell.poll_props(dict(props_parent, running=False)); assert not shell.props_window.isVisible()
        shell.process = None; shell.args.no_engine = True; shell.offset = QPoint()

        shell.sync_interaction({"interaction": {"serial": 1, "action": "feed", "accepted": True, "message": "Mama kabına gidiyor"}})
        assert shell.feedback.isVisible() and shell.feedback.text() == "Mama kabına gidiyor"
        shell.sync_interaction({"interaction": {"serial": 1, "action": "play", "message": "Stale acknowledgement"}})
        shell.sync_interaction({"interaction": {"serial": 2, "action": "pet", "message": "Pet acknowledgement"}})
        assert shell.feedback.text() == "Mama kabına gidiyor"
        shell.sync_interaction({"interaction": {"serial": 3, "action": "play", "accepted": False, "message": "Dinlenmeye ihtiyacı var"}})
        assert shell.feedback.text() == "Dinlenmeye ihtiyacı var"
        assert shell.appearance_menu.isEnabled() and shell.appearance_actions["van"].isChecked()
        for choice in APPEARANCES:
            before = set((shell.data / "commands").glob("*.json"))
            shell.appearance_actions[choice].trigger()
            new_commands = set((shell.data / "commands").glob("*.json")) - before
            assert len(new_commands) == 1 and json_file(new_commands.pop()) == {"command": "appearance", "appearance": choice}
            assert shell.appearance_actions["van"].isChecked()  # Wait for engine confirmation.
        atomic_json(status_path, {"appearance": "british", "appearance_ready": True})
        shell.poll()
        assert shell.appearance_actions["british"].isChecked() and sum(a.isChecked() for a in shell.appearance_actions.values()) == 1
        atomic_json(status_path, {"appearance": "british", "appearance_ready": False})
        shell.poll()
        before = set((shell.data / "commands").glob("*.json"))
        assert not shell.appearance_menu.isEnabled() and not shell.send("appearance", "van")
        assert not shell.send("appearance", "../unknown") and not shell.send("appearance", [])
        assert set((shell.data / "commands").glob("*.json")) == before
        atomic_json(status_path, {"appearance": "van", "appearance_ready": True})
        shell.poll()
        publish(2, png_crc32=0)
        shell.poll()
        preserved = json_file(frames / "shell-status.json")
        assert shell.isVisible() and preserved["waiting_for_pair"] is True and preserved["frame_seq"] == 1
        shell.valid_until = time.monotonic() - 1
        shell.poll()
        assert not shell.isVisible() and json_file(frames / "shell-status.json")["ready"] is False
        lease = acquire_instance(frames)
        assert lease is not None and acquire_instance(frames) is None
        assert request_stop(frames, wait=False) == 0
        stop = json_file(frames / "shell-command.json")
        assert valid_stop(stop, lease[1]["owner_token"], lease[1]["started_at"])
        assert not valid_stop(stop, "foreign-owner", lease[1]["started_at"])
        assert not valid_stop(dict(stop, created_at=0), lease[1]["owner_token"], lease[1]["started_at"])
        shell.owner_token, shell.started_wall = lease[1]["owner_token"], lease[1]["started_at"]
        stopped = []
        shell.close_companion = lambda: stopped.append(True)
        shell.poll()
        assert stopped == [True] and not (frames / "shell-command.json").exists()
        lease[0].close()
        reopened = acquire_instance(frames); assert reopened is not None; reopened[0].close()
        publish(3); shell.poll(); assert shell.isVisible()
        publish(2); shell.poll()
        assert json_file(frames / "shell-status.json")["frame_seq"] == 3
        publish(4, ready=False); shell.poll(); assert not shell.isVisible()
        publish(5); shell.poll(); assert shell.isVisible()
        publish(6, status="error", error="synthetic producer error")
        shell.poll(); assert not shell.isVisible()
        image.fill(QColor(0, 0, 0, 255)); image.save(str(frames / "frame.png"))
        publish(7)
        shell.poll()
        assert not shell.isVisible() and json_file(frames / "shell-status.json")["ready"] is False
        shell.closed = True; shell.tray.hide(); shell.close()
        if previous_data is None:
            os.environ.pop("XDG_DATA_HOME", None)
        else:
            os.environ["XDG_DATA_HOME"] = previous_data
        if previous_state is None:
            os.environ.pop("XDG_STATE_HOME", None)
        else:
            os.environ["XDG_STATE_HOME"] = previous_state
    assert activity_label(1) == "Geziniyor" and activity_label(0) == "Keyfi yerinde"
    odd = QImage(33, 17, QImage.Format.Format_RGBA8888); odd.fill(Qt.GlobalColor.transparent)
    painter = QPainter(odd); painter.fillRect(QRect(3, 4, 27, 10), QColor(180, 120, 70, 255)); painter.end()
    assert alpha_region(odd, 1)[0] == alpha_region(odd, 1, use_numpy=False)[0]
    benchmark = QImage(512, 512, QImage.Format.Format_RGBA8888); benchmark.fill(Qt.GlobalColor.transparent)
    painter = QPainter(benchmark); painter.setBrush(QColor(180, 120, 70, 255)); painter.drawEllipse(64, 48, 384, 416); painter.end()
    started = time.perf_counter()
    for _ in range(5):
        measured = alpha_region(benchmark, 1)
    milliseconds = (time.perf_counter() - started) * 200
    started = time.perf_counter(); fallback = alpha_region(benchmark, 1, use_numpy=False)
    fallback_ms = (time.perf_counter() - started) * 1000
    assert measured[0] == fallback[0]
    print("PASS: habitat CRC/static age/home anchor/input gates, engine acknowledgements, Play edge-clamp regression, cursor bounds/TTL/isolation, portable launch selection, private tmpfs/namespaces, legacy lease/stop, paired frames, alpha/DPI, IPC; Unreal/Windows runtime not tested.")
    print(json.dumps({"synthetic_mask_size": [512, 512], "mask_backend": "numpy+Qt-bitmap" if np is not None else "Python-fallback",
                      "mask_ms_mean_5": round(milliseconds, 3), "fallback_ms_one": round(fallback_ms, 3)}))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--no-engine", action="store_true", help="Isolated frame tests only; visibly labeled as not Unreal proof.")
    parser.add_argument("--frame-dir", type=Path)
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--no-cursor-interest", action="store_true", help="Disable occasional ephemeral pointer attention.")
    parser.add_argument("--stop", action="store_true", help="Ask the currently locked owned shell to quit; never kill a PID.")
    args = parser.parse_args()
    if args.self_test:
        app = QApplication(sys.argv); app.setQuitOnLastWindowClosed(False)
        self_test(); return 0
    paths = frame_paths(args.frame_dir)
    frames = paths["frames"]
    candidates = instance_candidates(paths, explicit=args.frame_dir is not None)
    if args.stop:
        try:
            active = active_instance(candidates)
            return request_stop(active) if active is not None else 0
        except (OSError, ValueError) as failure:
            print(str(failure), file=sys.stderr); return 1
    lease = None
    if not args.no_engine:
        active = active_instance(candidates)
        if active is not None:
            print("Mırmır masaüstü kabuğu zaten çalışıyor."); return 0
        lease = acquire_instance(frames)
        if lease is None:
            print("Mırmır masaüstü kabuğu zaten çalışıyor."); return 0
        os.environ["QT_QPA_PLATFORM"] = "windows" if WINDOWS else "xcb"  # This process only.
    app = QApplication(sys.argv); app.setQuitOnLastWindowClosed(False)
    shell = MirmirShell(args, lease)
    try:
        return app.exec()
    finally:
        shell.stop_engine()
        if lease:
            lease[0].close()


if __name__ == "__main__":
    raise SystemExit(main())
