#!/usr/bin/env bash
# Local Linux Shipping package only. No deployment, remote execution or publish.
# Prepare after the actual editor build, import, bootstrap and render proof.
# Options below were checked in the installed UE 5.8.4 AutomationTool/UBT source:
# ProjectParams.cs (map, ubtargs, clientconfig, build/cook/stage/pak/package/archive),
# BuildProjectCommand.Automation.cs (UbtArgs forwarding), BuildConfiguration.cs
# (MaxParallelActions, NoUBA, NoXGE, NoFASTBuild, NoSNDBS), and LinuxPlatform.Automation.cs
# (Linux/Mirmir.sh bootstrap convention).
set -euo pipefail

project_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
project_root=$(cd -- "$project_dir/../.." && pwd)
source "$project_dir/scripts/engine-path.sh"

print_only=false
if [[ $# -gt 0 ]]; then
  if [[ $# -eq 1 && "$1" == '--print-command' ]]; then
    print_only=true
  else
    printf '%s\n' 'Usage: package-game.sh [--print-command]' >&2
    exit 2
  fi
fi

uat="$MIRMIR_UE_ROOT/Engine/Build/BatchFiles/RunUAT.sh"
if [[ ! -x "$uat" ]]; then
  printf '%s\n' "RunUAT.sh is not executable: $uat" >&2
  exit 2
fi

map='/Game/Mirmir/Preview/MirmirPreview'
project_version=$(python - "$project_dir/Config/DefaultGame.ini" <<'PY'
from pathlib import Path
import re, sys
match = re.search(r'^ProjectVersion=([0-9]+\.[0-9]+\.[0-9]+)$', Path(sys.argv[1]).read_text(), re.M)
if not match:
    raise SystemExit('A numeric ProjectVersion is required before packaging.')
print(match[1])
PY
)
stamp=$(date -u +%Y%m%dT%H%M%SZ)
# Each invocation archives to a new directory so previous packages survive.
archive_dir="$project_root/.local/dist/package-$stamp-$$"
command=(
  "$uat" BuildCookRun
  "-project=$project_dir/Mirmir.uproject"
  -platform=Linux
  -clientconfig=Shipping
  -nocompileeditor
  -unattended
  -NoP4
  -UTF8Output
  "-map=$map"
  -build -cook -stage -pak -package -archive
  "-archivedirectory=$archive_dir"
  '-ubtargs=-MaxParallelActions=2 -NoUBA -NoXGE -NoFASTBuild -NoSNDBS'
)

printf 'Packaging command:'
printf ' %q' "${command[@]}"
printf '\n'
if [[ "$print_only" == true ]]; then
  printf '%s\n' 'Print only: packaging, directory creation and metadata writes were not performed.'
  exit 0
fi

if [[ ! -f "$project_dir/Content/Mirmir/Preview/MirmirPreview.umap" ]]; then
  printf '%s\n' 'Preview map is missing. Complete the real import/bootstrap and render proof first.' >&2
  exit 2
fi
if [[ -e "$archive_dir" ]]; then
  printf '%s\n' "Refusing to replace existing package: $archive_dir" >&2
  exit 2
fi
state_base=${XDG_STATE_HOME:-$HOME/.local/state}
if [[ "$state_base" != /* ]]; then state_base="$HOME/.local/state"; fi
log_dir="$state_base/mirmir/unreal"
mkdir -p -- "$log_dir" "$archive_dir"
log_file="$log_dir/package-$stamp-$$.log"
# pipefail propagates AutomationTool failure; no package metadata is updated then.
"${command[@]}" 2>&1 | tee "$log_file"

python - "$project_root" "$archive_dir" "$map" "$log_file" "$project_version" <<'PY'
import json
import os
from pathlib import Path
import shutil
import re
import sys
import tempfile
from datetime import datetime, timezone

project, archive, preview_map, log = Path(sys.argv[1]), Path(sys.argv[2]), sys.argv[3], sys.argv[4]
version = sys.argv[5]
current = re.search(r'^ProjectVersion=([0-9]+\.[0-9]+\.[0-9]+)$',
    (project / 'unreal/Mirmir/Config/DefaultGame.ini').read_text(), re.M)
if not current or current[1] != version:
    raise SystemExit('ProjectVersion changed during packaging; metadata was not published.')
launchers = list(archive.rglob("Mirmir.sh"))
if len(launchers) != 1 or not os.access(launchers[0], os.X_OK):
    raise SystemExit("UAT returned success but exactly one executable Mirmir.sh was not found; metadata was not updated.")
launcher = launchers[0]
binaries = list(archive.rglob("Mirmir-Linux-Shipping"))
if len(binaries) != 1 or not os.access(binaries[0], os.X_OK):
    raise SystemExit("Expected executable Mirmir-Linux-Shipping missing/ambiguous; metadata was not updated.")
credits = project / "ASSET-LICENSES.md"
personal_sources = project / "assets/personal-use/sources.json"
if credits.is_file():
    shutil.copy2(credits, launcher.parent / "ASSET-LICENSES.md")
if personal_sources.is_file():
    shutil.copy2(personal_sources, launcher.parent / "PERSONAL-USE-SOURCES.json")
record = {
    "schema_version": 1,
    "configuration": "Shipping",
    "platform": "Linux",
    "project_version": version,
    "packaged_at_utc": datetime.now(timezone.utc).isoformat(),
    "archive_directory": str(archive),
    "launcher": str(launcher),
    "binary": str(binaries[0]),
    "map": preview_map,
    "arguments": [preview_map, "-windowed", "-ResX=960", "-ResY=640"],
    "packaging_command_succeeded": True,
    "runtime_launch_verified": False,
    "gait_verified": False,
    "desktop_overlay_verified": False,
    "editor_required_for_runtime": False,
    "local_personal_noncommercial_asset": True,
    "build_log": log,
    "notes": [
        "Run the packaged launcher with the explicit map argument; packaging success does not prove runtime behavior.",
        "Cat Walk asset is CC BY-NC 4.0; this archive is a local personal noncommercial prototype.",
    ],
}
destination = project / ".local/package.json"
descriptor, temporary = tempfile.mkstemp(prefix=".package-", suffix=".json", dir=destination.parent)
try:
    with os.fdopen(descriptor, "w", encoding="utf-8") as output:
        json.dump(record, output, indent=2)
        output.write("\n")
    os.replace(temporary, destination)
finally:
    if os.path.exists(temporary):
        os.unlink(temporary)
print(json.dumps(record, indent=2), flush=True)
PY
