#!/usr/bin/env bash
set -euo pipefail
project_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
source "$project_dir/scripts/engine-path.sh"
state_base=${XDG_STATE_HOME:-$HOME/.local/state}
if [[ "$state_base" != /* ]]; then state_base="$HOME/.local/state"; fi
mkdir -p -- "$state_base/mirmir/unreal"
test_log="$state_base/mirmir/unreal/automation-$(date -u +%Y%m%dT%H%M%SZ)-$$.log"
printf 'Unreal automation log: %s\n' "$test_log"
# -TestExit forces RequestExit(true), which exits 1 on Linux even when tests
# pass. Automation SoftQuit waits for its queue and supplies the test status.
exec "$MIRMIR_UE_ROOT/Engine/Binaries/Linux/UnrealEditor-Cmd" \
  "$project_dir/Mirmir.uproject" -unattended -nop4 -nosplash -NullRHI \
  "-abslog=$test_log" \
  -ExecCmds='Automation RunTests Mirmir;SoftQuit'
