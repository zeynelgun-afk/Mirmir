#!/usr/bin/env bash
set -euo pipefail
task_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
task_tmp="$(mktemp -d)"
trap 'rm -rf -- "$task_tmp"' EXIT
export XDG_DATA_HOME="$task_tmp/data"
export XDG_STATE_HOME="$task_tmp/state"
if [[ "${1:-}" == "--include-lab" ]]; then
  godot --headless --path "$task_root/desktop-lab" --editor --import --quit
  godot --headless --path "$task_root/desktop-lab" --script res://tests/test_state.gd
fi
python -m py_compile "$task_root"/scripts/*.py "$task_root"/native-shell/*.py "$task_root"/unreal/Mirmir/scripts/*.py
python "$task_root/scripts/verify-proprietary-boundary.py"
python "$task_root/native-shell/mirmir_shell.py" --self-test
python "$task_root/scripts/verify_assets.py"
bash -n "$task_root/run.sh"
"$task_root/unreal/Mirmir/scripts/test-core.sh"
