#!/usr/bin/env bash
# Source from other scripts; explicit environment wins over the local installer config.
if [[ -z "${MIRMIR_UE_ROOT:-}" ]]; then
  mirmir_project_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../../.." && pwd)
  if [[ -f "$mirmir_project_root/.local/engine.json" ]]; then
    MIRMIR_UE_ROOT=$(python -c 'import json,sys; print(json.load(open(sys.argv[1])).get("engine_root", ""))' "$mirmir_project_root/.local/engine.json")
    export MIRMIR_UE_ROOT
  fi
fi
if [[ -z "${MIRMIR_UE_ROOT:-}" || ! -x "$MIRMIR_UE_ROOT/Engine/Binaries/Linux/UnrealEditor" ]]; then
  printf '%s\n' 'Unreal is not configured. Set MIRMIR_UE_ROOT to an official extracted Linux engine.' >&2
  printf '%s\n' 'Official engine download requires Epic account login: https://www.unrealengine.com/en-US/linux' >&2
  exit 2
fi
