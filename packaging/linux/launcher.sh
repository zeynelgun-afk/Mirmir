#!/bin/sh
set -eu
app_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
if [ "${1:-}" = "--diagnose" ]; then
    exec "$app_dir/python/bin/python3" "$app_dir/diagnose.py"
fi
# Python and Qt are private to this bundle; GPU/X11 drivers remain system-owned.
export PYTHONNOUSERSITE=1
export PYTHONDONTWRITEBYTECODE=1
unset PYTHONPATH PYTHONHOME
exec "$app_dir/python/bin/python3" "$app_dir/native-shell/mirmir_shell.py" "$@"
