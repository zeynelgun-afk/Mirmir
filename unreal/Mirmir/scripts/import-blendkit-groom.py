#!/usr/bin/env python3
"""Import and bind the audited Blendkit particle-hair groom in Unreal.

This is a candidate-only pipeline. It never assigns the groom or skeletal mesh
to the shipping cat Blueprint; visual acceptance remains a separate gate.
"""

from __future__ import annotations

from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path

import unreal


ROOT = Path(__file__).resolve().parents[3]
EXPORT_DIR = ROOT / "unreal/Mirmir/Content/Animals/BlendkitDomesticCat/Export"
GROOM_SOURCE = EXPORT_DIR / "G_BlendkitDomesticCat.abc"
GROOM_PROVENANCE = GROOM_SOURCE.with_suffix(".provenance.json")
FBX_SOURCE = EXPORT_DIR / "SK_BlendkitDomesticCat.fbx"
FBX_PROVENANCE = FBX_SOURCE.with_suffix(".provenance.json")
EXPECTED_SOURCE_BLEND_SHA256 = "5559921e22fee4af399e2ae640d7b77269c794e2a5aa14ab9ad5bf05b743d709"
DESTINATION_BASE = "/Game/Animals/BlendkitDomesticCat"
OWNER = "blendkit-domestic-cat-6b3bbcc7"
CONVERSION_TAG = "rxm90_rz180"
CONVERSION_ROTATION = unreal.Vector(-90.0, 0.0, 180.0)


def digest(path: Path) -> str:
    result = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            result.update(chunk)
    return result.hexdigest()


def checked_provenance(path: Path, provenance_path: Path, digest_key: str) -> tuple[str, dict]:
    if not path.is_file() or not provenance_path.is_file():
        raise RuntimeError(f"Missing audited source or provenance: {path}")
    provenance = json.loads(provenance_path.read_text(encoding="utf-8"))
    source_sha = digest(path)
    if source_sha != provenance.get(digest_key):
        raise RuntimeError(f"{path.name} differs from its export provenance")
    if provenance.get("source_blend_sha256") != EXPECTED_SOURCE_BLEND_SHA256:
        raise RuntimeError(f"{path.name} was not exported from the reviewed Blendkit source")
    return source_sha, provenance


def metadata_is_owned(subsystem, asset) -> bool:
    return subsystem.get_metadata_tag(asset, "MirmirLicensedAssetOwner") == OWNER


def set_candidate_metadata(subsystem, asset, groom_sha: str) -> None:
    subsystem.set_metadata_tag(asset, "MirmirLicensedAssetOwner", OWNER)
    subsystem.set_metadata_tag(asset, "MirmirGroomSourceSHA256", groom_sha)
    subsystem.set_metadata_tag(asset, "MirmirVisualAcceptance", "false")


def main() -> None:
    groom_sha, groom_provenance = checked_provenance(
        GROOM_SOURCE, GROOM_PROVENANCE, "groom_alembic_sha256"
    )
    fbx_sha, _ = checked_provenance(FBX_SOURCE, FBX_PROVENANCE, "fbx_sha256")
    destination = f"{DESTINATION_BASE}/Export_{fbx_sha[:12]}"
    # Groom iterations are content-addressed independently of the skeletal
    # mesh. This prevents a newly audited Alembic from silently reusing an old
    # imported Groom/Binding pair.
    groom_destination = f"{destination}/Groom_{groom_sha[:12]}_{CONVERSION_TAG}"
    mesh_path = f"{destination}/SK_BlendkitDomesticCat.SK_BlendkitDomesticCat"
    groom_path = f"{groom_destination}/G_BlendkitDomesticCat.G_BlendkitDomesticCat"
    binding_package_path = f"{groom_destination}/GB_BlendkitDomesticCat"
    binding_path = f"{binding_package_path}.GB_BlendkitDomesticCat"

    subsystem = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
    mesh = subsystem.load_asset(mesh_path)
    if not isinstance(mesh, unreal.SkeletalMesh):
        raise RuntimeError("Audited skeletal mesh is missing; run import-blendkit-cat.sh first")

    groom = subsystem.load_asset(groom_path) if subsystem.does_asset_exist(groom_path) else None
    if groom is not None and not metadata_is_owned(subsystem, groom):
        raise RuntimeError("Existing Groom asset is not owned by this import; nothing was replaced")
    if groom is None:
        options = unreal.GroomImportOptions()
        conversion = options.get_editor_property("conversion_settings")
        conversion.set_editor_property("rotation", CONVERSION_ROTATION)
        options.set_editor_property("conversion_settings", conversion)
        task = unreal.AssetImportTask()
        for name, value in {
            "filename": str(GROOM_SOURCE),
            "destination_path": groom_destination,
            "destination_name": "G_BlendkitDomesticCat",
            "automated": True,
            "async_": False,
            "replace_existing": False,
            "replace_existing_settings": False,
            "save": False,
            "options": options,
        }.items():
            task.set_editor_property(name, value)
        unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
        imported = list(task.get_objects())
        grooms = [asset for asset in imported if isinstance(asset, unreal.GroomAsset)]
        if len(grooms) != 1:
            raise RuntimeError(
                f"Expected one GroomAsset, found {len(grooms)}; inspect the Unreal import log"
            )
        groom = grooms[0]
        set_candidate_metadata(subsystem, groom, groom_sha)
        if not subsystem.save_loaded_asset(groom, False):
            raise RuntimeError("Could not save imported Groom asset")

    binding = subsystem.load_asset(binding_path) if subsystem.does_asset_exist(binding_path) else None
    if binding is not None and not metadata_is_owned(subsystem, binding):
        raise RuntimeError("Existing GroomBinding asset is not owned by this import")
    if binding is None:
        groom_library = getattr(unreal, "GroomLibrary", None)
        if groom_library is None:
            groom_library = unreal.GroomBlueprintLibrary
        binding = groom_library.create_new_groom_binding_asset_with_path(
            binding_package_path, groom, mesh, 100, None, 0
        )
        if not isinstance(binding, unreal.GroomBindingAsset):
            raise RuntimeError("Unreal could not bind the Groom to the skeletal mesh")
        set_candidate_metadata(subsystem, binding, groom_sha)
        if not subsystem.save_loaded_asset(binding, False):
            raise RuntimeError("Could not save GroomBinding asset")

    group_summaries = []
    try:
        for group in groom.get_editor_property("hair_groups_info"):
            group_summaries.append(
                {
                    "name": str(group.get_editor_property("group_name")),
                    "curves": group.get_editor_property("num_curves"),
                    "guides": group.get_editor_property("num_guides"),
                    "curve_vertices": group.get_editor_property("num_curve_vertices"),
                    "max_curve_length_cm": group.get_editor_property("max_curve_length"),
                }
            )
    except Exception as error:
        unreal.log_warning(f"Could not read Groom group metrics: {error}")

    render_summaries = []
    try:
        for group in groom.get_editor_property("hair_groups_rendering"):
            geometry = group.get_editor_property("geometry_settings")
            render_summaries.append(
                {
                    "hair_width_cm": geometry.get_editor_property("hair_width"),
                    "hair_root_scale": geometry.get_editor_property("hair_root_scale"),
                    "hair_tip_scale": geometry.get_editor_property("hair_tip_scale"),
                }
            )
    except Exception as error:
        unreal.log_warning(f"Could not read Groom rendering metrics: {error}")

    lod_summaries = []
    try:
        for group in groom.get_editor_property("hair_groups_lod"):
            lod_summaries.append(
                [
                    {
                        "visible": lod.get_editor_property("visible"),
                        "geometry_type": str(lod.get_editor_property("geometry_type")),
                        "binding_type": str(lod.get_editor_property("binding_type")),
                        "curve_decimation": lod.get_editor_property("curve_decimation"),
                    }
                    for lod in group.get_editor_property("lods")
                ]
            )
    except Exception as error:
        unreal.log_warning(f"Could not read Groom LOD metrics: {error}")

    binding_summaries = []
    try:
        for group in binding.get_editor_property("group_infos"):
            binding_summaries.append(
                {
                    "render_roots": group.get_editor_property("ren_root_count"),
                    "render_lods": group.get_editor_property("ren_lod_count"),
                    "simulation_roots": group.get_editor_property("sim_root_count"),
                    "simulation_lods": group.get_editor_property("sim_lod_count"),
                }
            )
    except Exception as error:
        unreal.log_warning(f"Could not read Groom binding metrics: {error}")

    mesh_bounds = mesh.get_bounds()

    report = {
        "schema": 1,
        "generated_utc": datetime.now(timezone.utc).isoformat(),
        "source_groom_sha256": groom_sha,
        "source_fbx_sha256": fbx_sha,
        "source_particle_system_count": len(groom_provenance.get("source_systems", [])),
        "conversion_tag": CONVERSION_TAG,
        "conversion_rotation_degrees": [
            CONVERSION_ROTATION.x,
            CONVERSION_ROTATION.y,
            CONVERSION_ROTATION.z,
        ],
        "groom_asset": groom.get_path_name(),
        "binding_asset": binding.get_path_name(),
        "skeletal_mesh": mesh.get_path_name(),
        "groups": group_summaries,
        "rendering": render_summaries,
        "lods": lod_summaries,
        "binding_groups": binding_summaries,
        "mesh_bounds": {
            "origin": [mesh_bounds.origin.x, mesh_bounds.origin.y, mesh_bounds.origin.z],
            "box_extent": [
                mesh_bounds.box_extent.x,
                mesh_bounds.box_extent.y,
                mesh_bounds.box_extent.z,
            ],
        },
        "groom_import_verified": True,
        "skeletal_binding_created": True,
        "product_blueprint_changed": False,
        "visual_acceptance": False,
    }
    output = Path(unreal.Paths.project_saved_dir()) / "Mirmir" / "blendkit-cat-groom-import.json"
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    unreal.log("MIRMIR_FREE_CAT_GROOM_IMPORT " + json.dumps(report))


if __name__ == "__main__":
    main()
