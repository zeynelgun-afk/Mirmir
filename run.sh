#!/usr/bin/env bash
set -euo pipefail
task_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
case "${1:-}" in
  --lab)
    shift
    exec python "$task_root/scripts/launcher.py" "$@"
    ;;
  --stop-lab)
    exec python "$task_root/scripts/launcher.py" --stop
    ;;
  --editor)
    shift
    source "$task_root/unreal/Mirmir/scripts/engine-path.sh"
    exec "$MIRMIR_UE_ROOT/Engine/Binaries/Linux/UnrealEditor" "$task_root/unreal/Mirmir/Mirmir.uproject" "$@"
    ;;
  *)
    exec "$task_root/unreal/Mirmir/scripts/run.sh" "$@"
    ;;
esac
