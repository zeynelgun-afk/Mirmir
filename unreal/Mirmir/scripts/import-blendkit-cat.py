#!/usr/bin/env python3
"""Import the audited zero-cost Blendkit cat FBX into an isolated Unreal path.

The import never assigns the candidate to the product Blueprint. The source and
generated Unreal assets live below the ignored licensed-asset boundary.
"""

from __future__ import annotations

from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path

import unreal


ROOT = Path(__file__).resolve().parents[3]
SOURCE = (
    ROOT
    / "unreal/Mirmir/Content/Animals/BlendkitDomesticCat/Export/SK_BlendkitDomesticCat.fbx"
)
PROVENANCE = SOURCE.with_suffix(".provenance.json")
EXPECTED_SOURCE_BLEND_SHA256 = "5559921e22fee4af399e2ae640d7b77269c794e2a5aa14ab9ad5bf05b743d709"
DESTINATION_BASE = "/Game/Animals/BlendkitDomesticCat"
OWNER = "blendkit-domestic-cat-6b3bbcc7"


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> None:
    if not SOURCE.is_file():
        raise RuntimeError(
            f"Audited FBX missing: {SOURCE}. Run process-blendkit-cat.sh --export first."
        )
    if not PROVENANCE.is_file():
        raise RuntimeError("FBX provenance missing; rerun process-blendkit-cat.sh --export")
    provenance = json.loads(PROVENANCE.read_text(encoding="utf-8"))
    source_sha = digest(SOURCE)
    if source_sha != provenance.get("fbx_sha256"):
        raise RuntimeError("Blendkit cat FBX differs from its export provenance")
    if provenance.get("source_blend_sha256") != EXPECTED_SOURCE_BLEND_SHA256:
        raise RuntimeError("FBX was not exported from the reviewed free Blendkit source")
    destination = f"{DESTINATION_BASE}/Export_{source_sha[:12]}"

    subsystem = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    registry.scan_paths_synchronous([destination], force_rescan=True)
    existing_data = list(registry.get_assets_by_path(destination, recursive=True))
    if existing_data:
        assets = [value.get_asset() for value in existing_data]
        if any(subsystem.get_metadata_tag(asset, "MirmirLicensedAssetOwner") != OWNER for asset in assets):
            raise RuntimeError(
                f"{destination} contains assets not owned by this import; nothing was replaced"
            )
    else:
        options = unreal.FbxImportUI()
        options.set_editor_property("import_mesh", True)
        options.set_editor_property("import_as_skeletal", True)
        options.set_editor_property("import_animations", False)
        options.set_editor_property("import_materials", True)
        options.set_editor_property("import_textures", False)
        options.set_editor_property("create_physics_asset", True)
        mesh_options = options.get_editor_property("skeletal_mesh_import_data")
        mesh_options.set_editor_property("import_morph_targets", False)
        mesh_options.set_editor_property("update_skeleton_reference_pose", False)
        mesh_options.set_editor_property("use_t0_as_ref_pose", False)

        task = unreal.AssetImportTask()
        for name, value in {
            "filename": str(SOURCE),
            "destination_path": destination,
            "destination_name": "SK_BlendkitDomesticCat",
            "automated": True,
            "async_": False,
            "replace_existing": False,
            "replace_existing_settings": False,
            "save": False,
            "options": options,
        }.items():
            task.set_editor_property(name, value)
        unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
        assets = list(task.get_objects())
        if not assets:
            raise RuntimeError("Unreal FBX import returned no assets; inspect the editor log")
        for asset in assets:
            subsystem.set_metadata_tag(asset, "MirmirLicensedAssetOwner", OWNER)
            subsystem.set_metadata_tag(asset, "MirmirSourceSHA256", source_sha)
            subsystem.set_metadata_tag(asset, "MirmirVisualAcceptance", "false")
            if not subsystem.save_loaded_asset(asset, False):
                raise RuntimeError(f"Could not save imported asset {asset.get_path_name()}")

    meshes = [asset for asset in assets if isinstance(asset, unreal.SkeletalMesh)]
    if len(meshes) != 1:
        raise RuntimeError(f"Expected one SkeletalMesh, found {len(meshes)}")
    mesh = meshes[0]
    skeleton = mesh.get_editor_property("skeleton")
    physics_asset = mesh.get_editor_property("physics_asset")
    if not isinstance(skeleton, unreal.Skeleton):
        raise RuntimeError("Imported cat has no Unreal Skeleton")
    if not isinstance(physics_asset, unreal.PhysicsAsset):
        mesh_editor = unreal.get_editor_subsystem(unreal.SkeletalMeshEditorSubsystem)
        physics_asset = mesh_editor.create_physics_asset(mesh, True, 0)
        if not isinstance(physics_asset, unreal.PhysicsAsset):
            raise RuntimeError("Unreal could not generate a PhysicsAsset for the cat")
        subsystem.set_metadata_tag(physics_asset, "MirmirLicensedAssetOwner", OWNER)
        subsystem.set_metadata_tag(physics_asset, "MirmirSourceSHA256", source_sha)
        subsystem.set_metadata_tag(physics_asset, "MirmirVisualAcceptance", "false")
        if not subsystem.save_loaded_asset(physics_asset, False):
            raise RuntimeError("Could not save generated cat PhysicsAsset")
        if not subsystem.save_loaded_asset(mesh, False):
            raise RuntimeError("Could not save cat mesh after assigning PhysicsAsset")

    report = {
        "schema": 1,
        "generated_utc": datetime.now(timezone.utc).isoformat(),
        "source_fbx_sha256": source_sha,
        "destination": destination,
        "skeletal_mesh": mesh.get_path_name(),
        "skeleton": skeleton.get_path_name(),
        "physics_asset": (
            physics_asset.get_path_name()
            if isinstance(physics_asset, unreal.PhysicsAsset)
            else None
        ),
        "material_slots": len(mesh.get_editor_property("materials")),
        "imported_assets": sorted(asset.get_path_name() for asset in assets),
        "real_unreal_import_verified": True,
        "legacy_particle_fur_imported": False,
        "physics_asset_generated": isinstance(physics_asset, unreal.PhysicsAsset),
        "product_blueprint_changed": False,
        "visual_acceptance": False,
        "animation_library_present": False,
    }
    output = Path(unreal.Paths.project_saved_dir()) / "Mirmir" / "blendkit-cat-unreal-import.json"
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    unreal.log("MIRMIR_FREE_CAT_UNREAL_IMPORT " + json.dumps(report))


if __name__ == "__main__":
    main()
