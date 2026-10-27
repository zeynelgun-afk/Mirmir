#!/usr/bin/env bash
set -euo pipefail
task_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
if [[ "${1:-}" != "--lab" ]]; then
  printf '%s\n' 'Unreal sürümü henüz derlenmedi; ürün başlatıcısı kurulmadı.' >&2
  printf '%s\n' 'Eski Godot deneyini açıkça kurmak için --lab kullanın.' >&2
  exit 2
fi
task_apps="${XDG_DATA_HOME:-$HOME/.local/share}/applications"
mkdir -p -- "$task_apps"
python - "$task_root" "$task_apps/mirmir.desktop" <<'PY'
from pathlib import Path
import sys
root = Path(sys.argv[1])
target = Path(sys.argv[2])
# Desktop-entry Exec quoting uses double quotes, not shell interpolation.
def quote(value):
    return '"' + str(value).replace('\\', '\\\\').replace('"', '\\"').replace('`', '\\`').replace('$', '\\$') + '"'
target.write_text('\n'.join([
    '[Desktop Entry]', 'Type=Application', 'Version=1.0',
    'Name=Mırmır — Godot masaüstü deneyi', 'Name[tr]=Mırmır — Godot masaüstü deneyi',
    'Comment=Animasyonlu 3B masaüstü kedisi — fizibilite prototipi',
    'Exec=' + quote(root / 'run.sh') + ' --lab',
    'Icon=' + str(root / 'desktop-lab/icon.svg'),
    'Terminal=false', 'Categories=Game;Simulation;', 'StartupNotify=false',
    'Actions=Stop;', '', '[Desktop Action Stop]', 'Name=Mırmır’ı kapat',
    'Exec=' + quote(root / 'run.sh') + ' --stop-lab', ''
]), encoding='utf-8')
target.chmod(0o644)
print(target)
PY
if command -v update-desktop-database >/dev/null; then
  update-desktop-database "$task_apps"
fi
