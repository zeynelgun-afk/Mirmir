#!/usr/bin/env bash
set -euo pipefail
project_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
source "$project_dir/scripts/engine-path.sh"
exec "$MIRMIR_UE_ROOT/Engine/Build/BatchFiles/Linux/Build.sh" \
  MirmirEditor Linux Development "$project_dir/Mirmir.uproject" -WaitMutex "$@"
