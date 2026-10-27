#!/usr/bin/env python3
"""Native transparent shell for real Unreal-exported RGBA frames; no renderer fallback."""
from __future__ import annotations

import argparse
import fcntl
import json
import math
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import time
import uuid
import zlib

try:
    import numpy as np
except ImportError:
    np = None

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen" if "--self-test" in sys.argv else "xcb")
from PyQt6.QtCore import QPoint, QRect, QSize, Qt, QTimer
from PyQt6.QtGui import QBitmap, QColor, QGuiApplication, QIcon, QImage, QPainter, QPixmap, QRegion
from PyQt6.QtWidgets import QApplication, QMenu, QSystemTrayIcon, QWidget

ROOT = Path(__file__).resolve().parents[1]
MAX_PNG_BYTES = 16 * 1024 * 1024
ACTIVITIES = ["Keyfi yerinde", "Geziniyor", "Temizleniyor", "Dinleniyor", "Oynuyor", "Mama molası", "İlgi bekliyor", "Mırlıyor"]


class FramePairMismatch(ValueError):
    """PNG and metadata may briefly refer to different atomic publications."""


def json_file(path):
    try:
        if path.stat().st_size > 65536:
            return {}
        data = json.loads(path.read_text())
        return data if isinstance(data, dict) else {}
    except (OSError, ValueError):
        return {}


def atomic_json(path, data):
    path.parent.mkdir(mode=0o700, parents=True, exist_ok=True)
    temporary = path.with_name(path.name + "." + uuid.uuid4().hex + ".tmp")
    with temporary.open("x") as output:
        json.dump(data, output, ensure_ascii=False)
    temporary.replace(path)


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


def configured_map(project_root=ROOT):
    preview = json_file(project_root / ".local/preview.json")
    package = preview.get("preview_map") or preview.get("map_path")
    if not isinstance(package, str) or not re.fullmatch(r"/Game/[A-Za-z0-9_/-]+", package) or ".." in package:
        raise ValueError("Unreal önizleme haritası hazır değil; önce bootstrap-preview.py çalıştırılmalı.")
    world = project_root / "unreal/Mirmir/Content" / (package.removeprefix("/Game/") + ".umap")
    if not world.is_file():
        raise ValueError("Hazırlanmış Unreal harita dosyası bulunamadı: " + package)
    return package


def acquire_instance(frames):
    frames.mkdir(mode=0o700, parents=True, exist_ok=True)
    descriptor = os.open(frames / "shell.lock", os.O_RDWR | os.O_CREAT | os.O_CLOEXEC, 0o600)
    handle = os.fdopen(descriptor, "r+")
    try:
        fcntl.flock(handle, fcntl.LOCK_EX | fcntl.LOCK_NB)
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
        with (frames / "shell.lock").open("r+") as handle:
            try:
                fcntl.flock(handle, fcntl.LOCK_EX | fcntl.LOCK_NB)
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
                        fcntl.flock(handle, fcntl.LOCK_EX | fcntl.LOCK_NB)
                        return 0
                    except BlockingIOError:
                        time.sleep(0.1)
                raise ValueError("Kapatma komutu iletildi; kabuk henüz kapanmadı. Başka süreç sonlandırılmadı.")
        return 0
    except FileNotFoundError:
        return 0
    except (OSError, ValueError) as failure:
        print(str(failure), file=sys.stderr); return 1


class MirmirShell(QWidget):
    def __init__(self, args, lease=None):
        super().__init__(None, Qt.WindowType.Tool | Qt.WindowType.FramelessWindowHint |
                         Qt.WindowType.WindowStaysOnTopHint | Qt.WindowType.WindowDoesNotAcceptFocus)
        self.setAttribute(Qt.WidgetAttribute.WA_TranslucentBackground)
        self.setAttribute(Qt.WidgetAttribute.WA_ShowWithoutActivating)
        self.setFocusPolicy(Qt.FocusPolicy.NoFocus)
        self.setWindowTitle("Mırmır · Unreal masaüstü katmanı")
        self.args, self.process, self.engine_log = args, None, None
        self.data = Path(os.environ.get("XDG_DATA_HOME", str(Path.home() / ".local/share"))) / "mirmir/unreal"
        self.frames = args.frame_dir or Path(os.environ.get("XDG_STATE_HOME", str(Path.home() / ".local/state"))) / "mirmir/unreal"
        self.image, self.frame_key, self.home = QImage(), None, None
        self.valid_until, self.last_metadata = 0.0, {}
        self.offset, self.drag_start, self.drag_home = QPoint(), None, None
        self.last_pet, self.closed, self.last_error = 0.0, False, ""
        self.started = time.monotonic()
        self.started_wall = lease[1]["started_at"] if lease else time.time()
        self.owner_token = lease[1]["owner_token"] if lease else uuid.uuid4().hex
        icon_image = QPixmap(64, 64)
        icon_image.fill(Qt.GlobalColor.transparent)
        painter = QPainter(icon_image)
        painter.setBrush(QColor("#d99c61")); painter.setPen(Qt.PenStyle.NoPen)
        painter.drawEllipse(4, 4, 56, 56); painter.setPen(QColor("#30231b"))
        painter.drawText(icon_image.rect(), Qt.AlignmentFlag.AlignCenter, "M"); painter.end()
        self.tray = QSystemTrayIcon(QIcon(icon_image), self)
        menu = QMenu()
        self.summary = menu.addAction("Unreal görüntüsü bekleniyor…"); self.summary.setEnabled(False)
        self.needs = menu.addAction(""); self.needs.setEnabled(False)
        menu.addSeparator()
        for label, command in [("Sev", "pet"), ("Mama ver", "feed"), ("Oyna", "play"), ("Dinlen", "rest"), ("Sesi aç / kapat", "mute")]:
            action = menu.addAction(label)
            action.triggered.connect(lambda checked=False, cmd=command: self.send(cmd))
        menu.addSeparator()
        menu.addAction("Kapat", self.close_companion)
        self.tray.setContextMenu(menu); self.tray.show()
        self.timer = QTimer(self); self.timer.timeout.connect(self.poll); self.timer.start(67)
        if not args.no_engine:
            self.start_engine()
        QApplication.instance().aboutToQuit.connect(self.stop_engine)

    def start_engine(self):
        config = json_file(ROOT / ".local/engine.json")
        engine = os.environ.get("MIRMIR_UE_ROOT") or config.get("engine_root")
        binary = Path(engine) / "Engine/Binaries/Linux/UnrealEditor" if engine else None
        if not binary or not binary.is_file() or not os.access(binary, os.X_OK):
            self.error("Unreal motoru hazır değil; engine.json veya MIRMIR_UE_ROOT gerekli.", notify=True)
            return
        try:
            preview_map = configured_map()
        except ValueError as failure:
            self.error(str(failure), notify=True); return
        self.frames.mkdir(mode=0o700, parents=True, exist_ok=True)
        self.engine_log = (self.frames / "engine-shell.log").open("a")
        command = [str(binary), str(ROOT / "unreal/Mirmir/Mirmir.uproject"), preview_map, "-game", "-windowed",
                   "-MirmirDesktop", "-RenderOffscreen", "-nosplash", "-ResX=640", "-ResY=640",
                   "-ExecCmds=t.MaxFPS 15", f"-abslog={self.frames / 'engine-session.log'}"]
        try:
            child_env = os.environ.copy(); child_env["SDL_VIDEODRIVER"] = "x11"
            self.process = subprocess.Popen(command, env=child_env, stdout=self.engine_log, stderr=subprocess.STDOUT)
        except OSError as failure:
            self.error("Unreal başlatılamadı: " + str(failure), notify=True)

    def send(self, command):
        if command == "pet":
            now = time.monotonic()
            if now - self.last_pet < 0.7:
                return
            self.last_pet = now
        try:
            atomic_json(self.data / "commands" / (uuid.uuid4().hex + ".json"), {"command": command})
        except OSError as failure:
            self.error("Komut kaydedilemedi: " + str(failure), notify=True)

    def error(self, message, notify=False):
        self.hide()
        self.valid_until = 0.0
        self.summary.setText(message); self.tray.setToolTip("Mırmır: " + message)
        if notify and message != self.last_error:
            self.tray.showMessage("Mırmır", message, QSystemTrayIcon.MessageIcon.Warning, 6000)
        if message != self.last_error:
            try:
                atomic_json(self.frames / "shell-status.json", {"implementation": "qt-unreal-frame-shell", "ready": False,
                            "engine_started": self.process is not None, "test_mode": self.args.no_engine, "error": message})
            except OSError:
                pass  # The tray remains usable even if the state disk is unwritable.
        self.last_error = message

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
                        "test_mode": self.args.no_engine, "ready": True, "waiting_for_pair": waiting_for_pair,
                        "session_id": self.last_metadata.get("session_id"), "frame_seq": self.last_metadata.get("frame_seq"),
                        "geometry": [self.x(), self.y(), self.width(), self.height()], "device_pixel_ratio": self.devicePixelRatioF()})
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
            self.error(f"Unreal kapandı (kod {self.process.returncode}). Günlük: {self.frames / 'engine-session.log'}", True)
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
                    self.home = QPoint(available.right() - self.width() - 24, available.bottom() - self.height() - 24)
                self.update()
            raw_offset = self.last_metadata.get("desktop_offset_px", [0, 0])
            if not isinstance(raw_offset, list) or len(raw_offset) != 2 or not all(isinstance(v, (int, float)) and math.isfinite(v) for v in raw_offset):
                raise ValueError("Unreal hareket konumu geçersiz.")
            self.offset = QPoint(round(raw_offset[0] / self.devicePixelRatioF()), round(raw_offset[1] / self.devicePixelRatioF()))
            screen = QGuiApplication.screenAt(self.home) or QGuiApplication.primaryScreen()
            self.move(clamp_position(self.home + self.offset, self.size(), screen.availableGeometry()))
            self.show()
            self.last_error = ""
            state = json_file(self.data / "desktop-status.json")
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
            if event.modifiers() & Qt.KeyboardModifier.ShiftModifier:
                self.drag_start, self.drag_home = event.globalPosition().toPoint(), QPoint(self.home)
            else:
                self.send("pet")

    def mouseMoveEvent(self, event):
        if self.drag_start is not None:
            self.home = self.drag_home + event.globalPosition().toPoint() - self.drag_start
        elif event.buttons() & Qt.MouseButton.LeftButton:
            self.send("pet")

    def mouseReleaseEvent(self, event):
        self.drag_start = self.drag_home = None

    def close_companion(self):
        self.closed = True
        self.timer.stop(); self.hide(); self.send("quit")
        QApplication.instance().quit()

    def stop_engine(self):
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
        path = Path(temporary) / "commands/one.json"
        atomic_json(path, {"command": "pet"})
        assert json_file(path) == {"command": "pet"} and not list(path.parent.glob("*.tmp"))
        previous_data = os.environ.get("XDG_DATA_HOME")
        os.environ["XDG_DATA_HOME"] = str(Path(temporary) / "data")
        frames = Path(temporary) / "frames"; frames.mkdir()
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
        assert shell.isVisible() and shell.mask().contains(QPoint(3, 3)) and not shell.mask().contains(QPoint(0, 0))
        assert json_file(frames / "shell-status.json")["test_mode"] is True
        shell.send("pet"); shell.send("pet")
        assert len(list((shell.data / "commands").glob("*.json"))) == 1
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
    print("PASS: preview-map gate, single-instance/owned stop, paired frames, readiness, alpha/DPI mask, IPC/rate limit; Unreal rendering not tested.")
    print(json.dumps({"synthetic_mask_size": [512, 512], "mask_backend": "numpy+Qt-bitmap" if np is not None else "Python-fallback",
                      "mask_ms_mean_5": round(milliseconds, 3), "fallback_ms_one": round(fallback_ms, 3)}))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--no-engine", action="store_true", help="Isolated frame tests only; visibly labeled as not Unreal proof.")
    parser.add_argument("--frame-dir", type=Path)
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--stop", action="store_true", help="Ask the currently locked owned shell to quit; never kill a PID.")
    args = parser.parse_args()
    frames = args.frame_dir or Path(os.environ.get("XDG_STATE_HOME", str(Path.home() / ".local/state"))) / "mirmir/unreal"
    if args.stop:
        return request_stop(frames)
    lease = None
    if not args.no_engine and not args.self_test:
        lease = acquire_instance(frames)
        if lease is None:
            print("Mırmır masaüstü kabuğu zaten çalışıyor."); return 0
        os.environ["QT_QPA_PLATFORM"] = "xcb"  # This process only; keep Wayland user session unchanged.
    app = QApplication(sys.argv); app.setQuitOnLastWindowClosed(False)
    if args.self_test:
        self_test(); return 0
    shell = MirmirShell(args, lease)
    try:
        return app.exec()
    finally:
        shell.stop_engine()
        if lease:
            lease[0].close()


if __name__ == "__main__":
    raise SystemExit(main())
