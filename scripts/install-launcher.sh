#!/usr/bin/env bash
set -euo pipefail
task_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
task_mode=product
if [[ $# -gt 0 ]]; then
  if [[ $# -eq 1 && "$1" == '--lab' ]]; then
    task_mode=lab
  else
    printf '%s\n' 'Usage: install-launcher.sh [--lab]' >&2
    exit 2
  fi
fi
task_data="${XDG_DATA_HOME:-$HOME/.local/share}"
if [[ "$task_data" != /* ]]; then task_data="$HOME/.local/share"; fi
task_apps="$task_data/applications"
mkdir -p -- "$task_apps"
python - "$task_root" "$task_apps" "$task_mode" <<'PY'
from pathlib import Path
import sys
root = Path(sys.argv[1])
lab = sys.argv[3] == 'lab'
target = Path(sys.argv[2]) / ('mirmir-lab.desktop' if lab else 'mirmir.desktop')
# Desktop-entry Exec quoting uses double quotes, not shell interpolation.
def quote(value):
    return '"' + str(value).replace('\\', '\\\\').replace('"', '\\"').replace('`', '\\`').replace('$', '\\$').replace('%', '%%') + '"'
name = 'Mırmır — Godot masaüstü deneyi' if lab else 'Mırmır — Masaüstü Kedisi'
launch = ' --lab' if lab else ''
stop = ' --stop-lab' if lab else ' --stop'
target.write_text('\n'.join([
    '[Desktop Entry]', 'Type=Application', 'Version=1.0',
    'Name=' + name, 'Name[tr]=' + name,
    'Comment=' + ('Godot fizibilite prototipi' if lab else 'Unreal ile çalışan 3B masaüstü kedisi'),
    'Exec=' + quote(root / 'run.sh') + launch,
    'Icon=' + str(root / 'desktop-lab/icon.svg'),
    'Terminal=false', 'Categories=Game;Simulation;', 'StartupNotify=false',
    'Actions=Stop;', '', '[Desktop Action Stop]', 'Name=Mırmır’ı kapat',
    'Exec=' + quote(root / 'run.sh') + stop, ''
]), encoding='utf-8')
target.chmod(0o644)
print(target)
PY
if command -v update-desktop-database >/dev/null; then
  update-desktop-database "$task_apps"
fi
