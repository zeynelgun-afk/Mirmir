#!/usr/bin/env bash
# Source from the other scripts. Set MIRMIR_UE_ROOT to the extracted engine root.
if [[ -z "${MIRMIR_UE_ROOT:-}" || ! -x "$MIRMIR_UE_ROOT/Engine/Binaries/Linux/UnrealEditor" ]]; then
  printf '%s\n' 'Unreal is not configured. Set MIRMIR_UE_ROOT to an official extracted Linux engine.' >&2
  printf '%s\n' 'Official engine download requires Epic account login: https://www.unrealengine.com/en-US/linux' >&2
  exit 2
fi
