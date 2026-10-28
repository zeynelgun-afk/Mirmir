#!/usr/bin/env python3
"""Create an isolated in-engine visual review for the free Blendkit cat.

The product preview Blueprint is never edited. All generated review assets stay
inside the ignored licensed-asset boundary and remain marked unaccepted until a
real rendered frame is inspected.
"""

from __future__ import annotations

import importlib.util
import json
from pathlib import Path

import unreal


ROOT = Path(__file__).resolve().parents[3]
IMPORT_REPORT = ROOT / "unreal/Mirmir/Saved/Mirmir/blendkit-cat-unreal-import.json"
GROOM_REPORT = ROOT / "unreal/Mirmir/Saved/Mirmir/blendkit-cat-groom-import.json"
OWNER = "mirmir-blendkit-cat-review-v1"


def load_report(path: Path) -> dict:
    if not path.is_file():
        raise RuntimeError(f"Missing verified import report: {path}")
    return json.loads(path.read_text(encoding="utf-8"))


def create_debug_hair_material(subsystem, prefix: str):
    path = prefix + "/M_BlendkitGroomBlackCandidate"
    if subsystem.does_asset_exist(path):
        material = subsystem.load_asset(path)
        if not isinstance(material, unreal.Material):
            raise RuntimeError("Existing Groom debug material has the wrong type")
    else:
        material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            "M_BlendkitGroomBlackCandidate",
            prefix,
            unreal.Material,
            unreal.MaterialFactoryNew(),
        )
        if not isinstance(material, unreal.Material):
            raise RuntimeError("Could not create Groom debug material")
        library = unreal.MaterialEditingLibrary
        color = library.create_material_expression(
            material, unreal.MaterialExpressionConstant3Vector, -300, 0
        )
        color.set_editor_property("constant", unreal.LinearColor(0.024, 0.030, 0.042, 1.0))
        roughness = library.create_material_expression(
            material, unreal.MaterialExpressionConstant, -300, 140
        )
        roughness.set_editor_property("r", 0.52)
        scatter = library.create_material_expression(
            material, unreal.MaterialExpressionConstant3Vector, -300, 280
        )
        scatter.set_editor_property("constant", unreal.LinearColor(0.09, 0.11, 0.16, 1.0))
        library.connect_material_property(color, "", unreal.MaterialProperty.MP_BASE_COLOR)
        library.connect_material_property(roughness, "", unreal.MaterialProperty.MP_ROUGHNESS)
        library.connect_material_property(scatter, "", unreal.MaterialProperty.MP_SUBSURFACE_COLOR)
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_HAIR)
    material.set_editor_property("two_sided", True)
    material.set_editor_property("used_with_hair_strands", True)
    library = unreal.MaterialEditingLibrary
    errors = list(library.recompile_material(material))
    if errors:
        raise RuntimeError("Groom debug material compile errors: " + str(errors))
    subsystem.set_metadata_tag(material, "MirmirLicensedAssetOwner", OWNER)
    subsystem.set_metadata_tag(material, "MirmirVisualAcceptance", "candidate-only")
    if not subsystem.save_loaded_asset(material, False):
        raise RuntimeError("Could not save Groom debug material")
    return material


def main() -> None:
    mesh_report = load_report(IMPORT_REPORT)
    groom_report = load_report(GROOM_REPORT)
    if not mesh_report.get("real_unreal_import_verified"):
        raise RuntimeError("Skeletal mesh import has not been verified")
    if not groom_report.get("skeletal_binding_created"):
        raise RuntimeError("Groom binding has not been verified")
    if groom_report.get("skeletal_mesh") != mesh_report.get("skeletal_mesh"):
        raise RuntimeError("Groom binding and skeletal mesh reports do not match")

    mesh = unreal.load_asset(mesh_report["skeletal_mesh"])
    groom = unreal.load_asset(groom_report["groom_asset"])
    binding = unreal.load_asset(groom_report["binding_asset"])
    if not isinstance(mesh, unreal.SkeletalMesh):
        raise RuntimeError("Review SkeletalMesh could not be loaded")
    if not isinstance(groom, unreal.GroomAsset):
        raise RuntimeError("Review GroomAsset could not be loaded")
    if not isinstance(binding, unreal.GroomBindingAsset):
        raise RuntimeError("Review GroomBindingAsset could not be loaded")

    spec = importlib.util.spec_from_file_location(
        "mirmir_bootstrap_helpers", Path(__file__).with_name("bootstrap-preview.py")
    )
    helper = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(helper)
    helper.OWNER = OWNER

    revision = (
        mesh_report["source_fbx_sha256"][:8]
        + "_"
        + groom_report["source_groom_sha256"][:8]
        + "_"
        + groom_report.get("conversion_tag", "legacy")
        + "_w004_black_lit_late"
    )
    prefix = f"/Game/Animals/BlendkitDomesticCat/Review_{revision}_groomdebug"
    subsystem = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
    debug_hair_material = create_debug_hair_material(subsystem, prefix)
    cat_parent = unreal.load_class(None, "/Script/Mirmir.MirmirCat")
    mode_parent = unreal.load_class(None, "/Script/Mirmir.MirmirGameMode")
    if not cat_parent or not mode_parent:
        raise RuntimeError("The built Mirmir C++ classes are unavailable")

    cat = helper.create_blueprint(
        subsystem,
        prefix + "/BP_BlendkitCatReview",
        cat_parent,
        {
            "cat_skeletal_mesh": mesh,
            "cat_groom_asset": groom,
            "cat_groom_binding": binding,
            "cat_groom_material": debug_hair_material,
            # Four tenths of a millimetre remains visible at companion camera
            # distance without turning strands into the ribbon-like debug coat.
            "groom_hair_width_cm": 0.04,
            "normalize_imported_mesh": True,
            "target_body_length": 70.0,
            "enable_fur_shells": False,
            "use_native_animation_blending": False,
            "wander_radius": 0.0,
            "walk_speed": 0.0,
            "enable_curiosity_approach": False,
        },
    )
    cat_class = unreal.BlueprintEditorLibrary.generated_class(cat)
    mode = helper.create_blueprint(
        subsystem,
        prefix + "/BP_BlendkitReviewMode",
        mode_parent,
        {
            "cat_class": cat_class,
            "ambient_intensity": 1.25,
            "review_screenshot_delay_seconds": 3.0,
        },
    )
    mode_class = unreal.BlueprintEditorLibrary.generated_class(mode)
    map_path = prefix + "/BlendkitCatReview"
    world = helper.existing_owned(subsystem, map_path, unreal.World)
    if not world:
        world = unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
        if not world:
            raise RuntimeError("Could not create the isolated cat review map")
        world.get_world_settings().set_editor_property("default_game_mode", mode_class)
        helper.mark_owned(subsystem, world)
        if not unreal.EditorLoadingAndSavingUtils.save_map(world, map_path):
            raise RuntimeError("Could not save the isolated cat review map")

    defaults = unreal.get_default_object(cat_class)
    report = {
        "schema": 1,
        "review_map": map_path,
        "cat_blueprint": cat.get_path_name(),
        "skeletal_mesh": mesh.get_path_name(),
        "groom_asset": groom.get_path_name(),
        "binding_asset": binding.get_path_name(),
        "configured_skeletal_mesh": helper.object_path(
            defaults.get_editor_property("cat_skeletal_mesh")
        ),
        "configured_groom_asset": helper.object_path(
            defaults.get_editor_property("cat_groom_asset")
        ),
        "configured_groom_binding": helper.object_path(
            defaults.get_editor_property("cat_groom_binding")
        ),
        "configured_groom_hair_width_cm": defaults.get_editor_property(
            "groom_hair_width_cm"
        ),
        "product_blueprint_changed": False,
        "rendered_frame_reviewed": False,
        "visual_acceptance": False,
    }
    output = Path(unreal.Paths.project_saved_dir()) / "Mirmir" / "blendkit-cat-review.json"
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    unreal.log("MIRMIR_BLENDKIT_REVIEW " + json.dumps(report))


if __name__ == "__main__":
    main()
