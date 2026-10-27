#!/usr/bin/env bash
set -euo pipefail
task_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
task_tmp="$(mktemp -d)"
trap 'rm -rf -- "$task_tmp"' EXIT
export XDG_DATA_HOME="$task_tmp/data"
export XDG_STATE_HOME="$task_tmp/state"
godot --headless --path "$task_root/desktop-lab" --editor --import --quit
godot --headless --path "$task_root/desktop-lab" --script res://tests/test_state.gd
python -m py_compile "$task_root/scripts/launcher.py"
python "$task_root/scripts/verify_assets.py"
bash -n "$task_root/run.sh"
"$task_root/unreal/Mirmir/scripts/test-core.sh"
