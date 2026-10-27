#!/bin/sh
# Run inside an ephemeral official Debian/Ubuntu container, never on the host.
set -eu
if [ ! -f /.dockerenv ]; then
    printf '%s\n' 'This verification script must run inside a Docker container.' >&2
    exit 2
fi
: "${PACKAGE:?Absolute mounted .deb path required}"
export DEBIAN_FRONTEND=noninteractive
apt-get update
apt-get install -y --no-install-recommends "$PACKAGE"
dpkg-query -W -f='${Package} ${Version} ${Status}\n' mirmir
/opt/mirmir/mirmir --diagnose
/opt/mirmir/python/bin/python3 - <<'PY'
import ctypes
from pathlib import Path
import PyQt6
plugin = Path(PyQt6.__file__).parent / 'Qt6/plugins/platforms/libqxcb.so'
ctypes.CDLL(str(plugin))
print('Private Qt XCB platform plugin and its linked libraries loaded.')
PY
QT_QPA_PLATFORM=offscreen /opt/mirmir/python/bin/python3 /opt/mirmir/native-shell/mirmir_shell.py --self-test
