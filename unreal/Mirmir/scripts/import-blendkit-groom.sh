#!/usr/bin/env bash
set -euo pipefail

project_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
source "$project_dir/scripts/engine-path.sh"
exec "$MIRMIR_UE_ROOT/Engine/Binaries/Linux/UnrealEditor-Cmd" \
  "$project_dir/Mirmir.uproject" \
  -run=pythonscript -script="$project_dir/scripts/import-blendkit-groom.py" \
  -unattended -nop4 -nosplash -NullRHI -stdout -FullStdOutLogOutput
