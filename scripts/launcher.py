#!/usr/bin/env python3
"""Native tray and single-instance launcher for the separate Godot desktop lab."""
from __future__ import annotations

import argparse
import fcntl
import json
import os
from pathlib import Path
import shutil
import signal
import subprocess
import sys
import time
import uuid

ROOT = Path(__file__).resolve().parents[1]
DATA = Path(os.environ.get("XDG_DATA_HOME", Path.home() / ".local/share")) / "mirmir"
STATE = Path(os.environ.get("XDG_STATE_HOME", Path.home() / ".local/state")) / "mirmir"
COMMANDS = {"pet", "feed", "play", "rest", "mute", "home", "menu", "quit"}


def send_command(command: str) -> None:
    if command not in COMMANDS:
        raise ValueError("Unknown Mırmır command")
    directory = DATA / "commands"
    directory.mkdir(parents=True, exist_ok=True, mode=0o700)
    filename = directory / f"{time.time_ns()}-{uuid.uuid4().hex}.json"
    temporary = filename.with_suffix(".tmp")
    temporary.write_text(json.dumps({"command": command}), encoding="utf-8")
    temporary.chmod(0o600)
    temporary.replace(filename)


def read_status() -> dict:
    path = DATA / "runtime-status.json"
    if not path.exists() or path.stat().st_size > 65536:
        return {}
    try:
        status = json.loads(path.read_text(encoding="utf-8"))
        return status if isinstance(status, dict) else {}
    except (OSError, ValueError):
        return {}


def main() -> int:
    parser = argparse.ArgumentParser(description="Mırmır masaüstü kedisi laboratuvarı")
    parser.add_argument("--command", choices=sorted(COMMANDS))
    parser.add_argument("--stop", action="store_true")
    parser.add_argument("--status", action="store_true")
    parser.add_argument("--no-tray", action="store_true")
    args = parser.parse_args()
    if args.status:
        print(json.dumps(read_status(), ensure_ascii=False, indent=2))
        return 0
    STATE.mkdir(parents=True, exist_ok=True, mode=0o700)
    DATA.mkdir(parents=True, exist_ok=True, mode=0o700)
    # flock follows process lifetime; no stale PID kill and no duplicate cats.
    lock = (STATE / "launcher.lock").open("w")
    try:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
    except BlockingIOError:
        send_command("quit" if args.stop else args.command or "menu")
        return 0
    if args.stop:
        print("Mırmır zaten kapalı.")
        return 0
    executable = shutil.which("godot") or shutil.which("godot4")
    if executable is None:
        print("Godot bulunamadı. Unreal kaynakları ayrı olarak unreal/ dizininde.", file=sys.stderr)
        return 1
    # Old commands must not close or unexpectedly feed a freshly opened cat.
    pending = DATA / "commands"
    pending.mkdir(exist_ok=True, mode=0o700)
    for path in pending.glob("*.json"):
        path.unlink()
    command = [executable, "--path", str(ROOT / "desktop-lab"), "--display-driver", "x11", "--rendering-method", "gl_compatibility"]
    log = (STATE / "desktop.log").open("w", encoding="utf-8")
    # Import explicitly in a fresh checkout so icons/audio are usable immediately.
    if not (ROOT / "desktop-lab/.godot/global_script_class_cache.cfg").exists():
        imported = subprocess.run([executable, "--headless", "--path", str(ROOT / "desktop-lab"), "--editor", "--import", "--quit"], stdout=log, stderr=subprocess.STDOUT, timeout=120)
        if imported.returncode:
            print(f"Varlıklar içe aktarılamadı: {STATE / 'desktop.log'}", file=sys.stderr)
            return imported.returncode
    child = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
    (STATE / "launcher.pid").write_text(str(os.getpid()), encoding="ascii")
    (STATE / "engine.pid").write_text(str(child.pid), encoding="ascii")
    stopping = False

    def stop(*_unused) -> None:
        nonlocal stopping
        if not stopping and child.poll() is None:
            stopping = True
            send_command("quit")

    signal.signal(signal.SIGTERM, stop)
    signal.signal(signal.SIGINT, stop)
    try:
        if args.command:
            send_command(args.command)
        if args.no_tray:
            while child.poll() is None:
                time.sleep(0.2)
            return child.returncode
        from PyQt6.QtCore import QTimer
        from PyQt6.QtGui import QAction, QIcon
        from PyQt6.QtWidgets import QApplication, QMenu, QSystemTrayIcon

        app = QApplication(["Mırmır"])
        app.setQuitOnLastWindowClosed(False)
        app.setApplicationName("Mırmır")
        tray = QSystemTrayIcon(QIcon(str(ROOT / "desktop-lab/icon.svg")), app)
        tray.setToolTip("Mırmır · Masaüstü kedin")
        menu = QMenu()
        title = QAction("Mırmır", menu)
        title.setEnabled(False)
        menu.addAction(title)
        for text, action in [("Sev ve mırlat", "pet"), ("Mama ver", "feed"), ("Top oyna", "play"), ("Dinlen", "rest"), ("Sesi aç / kapat", "mute"), ("Yanıma gel", "home"), ("Kedi menüsü", "menu")]:
            item = QAction(text, menu)
            item.triggered.connect(lambda _checked=False, action=action: send_command(action))
            menu.addAction(item)
        menu.addSeparator()
        exit_action = QAction("Kaydet ve çık", menu)
        exit_action.triggered.connect(stop)
        menu.addAction(exit_action)
        tray.setContextMenu(menu)
        tray.activated.connect(lambda reason: send_command("menu") if reason == QSystemTrayIcon.ActivationReason.Trigger else None)
        tray.show()
        timer = QTimer()
        stop_at = [None]

        def poll() -> None:
            if child.poll() is not None:
                app.quit()
            elif stopping:
                if stop_at[0] is None:
                    stop_at[0] = time.monotonic()
                elif time.monotonic() - stop_at[0] > 5:
                    child.terminate()
            status = read_status()
            if status:
                title.setText(f"Mırmır · Bağınız {round(status.get('bond', 0))}")

        timer.timeout.connect(poll)
        timer.start(500)
        app.exec()
        return child.wait(timeout=5)
    except ImportError:
        print("PyQt6 yok; kedi kendi menüsüyle çalışıyor. Çıkış: ./run.sh --stop", file=sys.stderr)
        return child.wait()
    finally:
        if child.poll() is None:
            stop()
            try:
                child.wait(timeout=5)
            except subprocess.TimeoutExpired:
                child.terminate()
                child.wait(timeout=5)
        for name in ("launcher.pid", "engine.pid"):
            (STATE / name).unlink(missing_ok=True)
        log.close()
        lock.close()


if __name__ == "__main__":
    raise SystemExit(main())
