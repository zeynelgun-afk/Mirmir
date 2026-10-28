#!/usr/bin/env bash
set -euo pipefail

project_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
blender_bin=${BLENDER_BIN:-/home/zeynel/Applications/blender-4.5.15-linux-x64/blender}
source_blend=${MIRMIR_BLENDKIT_CAT_SOURCE:-$project_dir/Content/Animals/BlendkitDomesticCat/Source/domestic-cat-rigged.blend}
report=${MIRMIR_BLENDKIT_CAT_REPORT:-$project_dir/Saved/Mirmir/blendkit-cat-audit.json}
output=${MIRMIR_BLENDKIT_CAT_FBX:-$project_dir/Content/Animals/BlendkitDomesticCat/Export/SK_BlendkitDomesticCat.fbx}
groom_output=${MIRMIR_BLENDKIT_CAT_GROOM:-$project_dir/Content/Animals/BlendkitDomesticCat/Export/G_BlendkitDomesticCat.abc}

if [[ ! -x "$blender_bin" ]]; then
  printf 'Blender executable not found: %s\n' "$blender_bin" >&2
  exit 2
fi
if [[ ! -f "$source_blend" ]]; then
  printf 'Local licensed cat source not found: %s\n' "$source_blend" >&2
  exit 2
fi

pipeline=(
  "$blender_bin" --background "$source_blend"
  --python "$project_dir/scripts/blendkit_cat_pipeline.py"
  -- --report "$report"
)
case "${1:---audit}" in
  --audit)
    ;;
  --export)
    pipeline+=(--output "$output")
    ;;
  --export-groom)
    # Groom iteration must not rewrite FBX. Blender's FBX container contains
    # volatile metadata, so rewriting it would create a false mesh revision.
    pipeline+=(--groom-output "$groom_output")
    ;;
  *)
    printf 'Usage: %s [--audit|--export|--export-groom]\n' "$0" >&2
    exit 2
    ;;
esac

exec "${pipeline[@]}"
