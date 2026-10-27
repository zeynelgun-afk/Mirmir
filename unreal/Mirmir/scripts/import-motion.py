"""Import reviewed motion onto the existing cat skeleton, without replacing it.

Run inside UE Editor with -run=pythonscript. Assets remain in a source-hash
review directory; this helper never changes the product Blueprint or map.
"""
from pathlib import Path
import hashlib
import json
import os
import tempfile

import unreal


ROOT = Path(__file__).resolve().parents[3]
MANIFEST = ROOT / "assets/personal-use/tuxedo-motion-candidate.json"
SKELETON = "/Game/Mirmir/Shared/Cat/cat-walk-loop/SkeletalMeshes/Object_7_Skeleton.Object_7_Skeleton"


def main():
    provenance = json.loads(MANIFEST.read_text())
    source = (ROOT / provenance["output"]).resolve()
    source.relative_to(ROOT / "assets")
    digest = hashlib.sha256(source.read_bytes()).hexdigest()
    if digest != provenance["output_sha256"]:
        raise RuntimeError("Retarget source SHA256 does not match its provenance.")
    skeleton = unreal.load_asset(SKELETON)
    if not isinstance(skeleton, unreal.Skeleton):
        raise RuntimeError("The original cat skeleton must exist before motion import.")
    destination = "/Game/Mirmir/MotionReview/Retarget_" + digest[:12]
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    registry.scan_paths_synchronous(["/Game/Mirmir"], force_rescan=True)
    subsystem = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
    existing = list(registry.get_assets_by_path(destination, recursive=True))
    if existing:
        assets = [entry.get_asset() for entry in existing]
        if any(subsystem.get_metadata_tag(asset, "MirmirMotionSourceSHA") != digest for asset in assets):
            raise RuntimeError("Review destination contains unowned assets; no replacement was performed.")
    else:
        pipeline = unreal.InterchangeGenericAssetsPipeline()
        common = pipeline.get_editor_property("common_skeletal_meshes_and_animations_properties")
        common.set_editor_property("import_only_animations", True)
        common.set_editor_property("try_auto_select_skeleton", False)
        common.set_editor_property("skeleton", skeleton)
        common.set_editor_property("use_t0_as_ref_pose", False)
        pipeline.set_editor_property("asset_type_sub_folders", True)
        overrides = unreal.InterchangePipelineStackOverride()
        overrides.add_pipeline(pipeline)
        task = unreal.AssetImportTask()
        for key, value in {"filename": str(source), "destination_path": destination,
                           "automated": True, "async_": False, "replace_existing": False,
                           "replace_existing_settings": False, "save": True, "options": overrides}.items():
            task.set_editor_property(key, value)
        unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
        assets = list(task.get_objects())
        if not assets:
            raise RuntimeError("Interchange returned no motion assets. See the editor import log.")
        if any(isinstance(asset, unreal.SkeletalMesh) for asset in assets):
            raise RuntimeError("Animation-only import unexpectedly produced a mesh; review assets were not assigned.")
        for asset in assets:
            subsystem.set_metadata_tag(asset, "MirmirMotionSourceSHA", digest)
            subsystem.set_metadata_tag(asset, "MirmirMotionReview", "Unaccepted retarget candidate")
            subsystem.save_loaded_asset(asset)
    clips = []
    for asset in assets:
        if not isinstance(asset, unreal.AnimSequence):
            continue
        assigned = asset.get_editor_property("skeleton")
        if assigned != skeleton or asset.get_play_length() <= 0:
            raise RuntimeError("Imported animation has the wrong skeleton or an empty timeline.")
        clips.append({"name": asset.get_name(), "path": asset.get_path_name(),
                      "seconds": asset.get_play_length(), "skeleton": assigned.get_path_name()})
    if not clips:
        raise RuntimeError("No compatible animation sequences were imported.")
    record = {"schema_version": 1, "source_file": str(source.relative_to(ROOT)),
              "source_sha256": digest, "destination": destination, "skeleton": SKELETON,
              "imported_animations": clips, "product_blueprint_changed": False,
              "real_unreal_import_verified": True, "retarget_visual_quality_accepted": False}
    output = ROOT / ".local/motion-import.json"
    output.parent.mkdir(exist_ok=True)
    descriptor, temporary = tempfile.mkstemp(prefix=".motion-import-", dir=output.parent)
    with os.fdopen(descriptor, "w") as stream:
        json.dump(record, stream, indent=2)
        stream.write("\n")
    os.replace(temporary, output)
    unreal.log("Mirmir reviewed motion imported: " + json.dumps(record))


if __name__ == "__main__":
    main()
