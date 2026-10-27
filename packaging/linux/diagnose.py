"""Read-only bundle diagnostic; it never starts/stops the user's cat."""
from pathlib import Path
import ctypes
import json
import os
import platform
import subprocess
import sys

root=Path(__file__).resolve().parent
manifest=json.loads((root/".local/package.json").read_text())
binary=(root/manifest["binary"]).resolve()
record={"platform":platform.platform(),"glibc":platform.libc_ver(),"python":sys.version.split()[0],
        "shipping_binary_present":binary.is_file(),"engine_sdk_required":False,
        "display":bool(os.environ.get("DISPLAY")),"wayland_display":bool(os.environ.get("WAYLAND_DISPLAY"))}
from PyQt6.QtCore import PYQT_VERSION_STR,QT_VERSION_STR,qVersion
record.update(pyqt=PYQT_VERSION_STR,qt=qVersion(),qt_build=QT_VERSION_STR)
try:
    import numpy
    record["numpy"]=numpy.__version__
except ImportError:record["numpy"]=None
for lib in ["libvulkan.so.1","libX11.so.6","libxcb.so.1","libxcb-cursor.so.0"]:
    try:ctypes.CDLL(lib);record[lib]="available"
    except OSError:record[lib]="missing"
print(json.dumps(record,ensure_ascii=False,indent=2))
raise SystemExit(0 if binary.is_file() else 1)
