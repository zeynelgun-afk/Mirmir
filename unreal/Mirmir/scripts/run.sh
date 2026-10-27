#!/usr/bin/env bash
set -euo pipefail
project_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
source "$project_dir/scripts/engine-path.sh"
preview_map=$(python -c 'import json,sys; print(json.load(open(sys.argv[1]))["preview_map"])' "$project_dir/../../.local/preview.json")
state_base=${XDG_STATE_HOME:-$HOME/.local/state}
if [[ "$state_base" != /* ]]; then state_base="$HOME/.local/state"; fi
mkdir -p -- "$state_base/mirmir/unreal"
# A conventional windowed preview. No OS transparency or click-through claim.
exec "$MIRMIR_UE_ROOT/Engine/Binaries/Linux/UnrealEditor" \
  "$project_dir/Mirmir.uproject" "$preview_map" -game -windowed -ResX=960 -ResY=640 \
  -ExecCmds='t.MaxFPS 30' -abslog="$state_base/mirmir/unreal/session.log" "$@"
