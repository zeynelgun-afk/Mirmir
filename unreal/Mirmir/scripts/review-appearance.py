"""Prepare owned breed-look review maps without changing the product defaults."""
from pathlib import Path
import importlib.util
import json
import os

import unreal

ROOT = Path(__file__).resolve().parents[3]


def main():
    use_fur = os.environ.get("MIRMIR_REVIEW_FUR", "on") == "on"
    spec = importlib.util.spec_from_file_location("appearance_bootstrap_helpers",
                                                Path(__file__).with_name("bootstrap-preview.py"))
    helper = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(helper)
    helper.OWNER = "mirmir-appearance-review-v1"
    source = json.loads((ROOT / ".local/motion-import.json").read_text())

    def animation(suffix):
        found = [item for item in source["imported_animations"]
                 if item["name"].replace("_", "").lower().endswith(suffix.replace("_", "").lower())]
        if len(found) != 1:
            raise RuntimeError("Expected exactly one reviewed motion: " + suffix)
        return unreal.load_asset(found[0]["path"])

    materials = {name: unreal.load_asset("/Game/Mirmir/Appearance/" + name + "." + name)
                 for name in ["M_CatCoat", "M_CatEyes", "M_CatFur"]}
    if any(not value for value in materials.values()):
        raise RuntimeError("Create and shader-compile all appearance templates first.")
    settings = {"coat_material_template": materials["M_CatCoat"],
                "eye_material_template": materials["M_CatEyes"],
                "fur_material_template": materials["M_CatFur"],
                "enable_fur_shells": use_fur, "idle_animation": animation("Tuxedo_Idle"),
                "idle_variation_animation": animation("Tuxedo_IdleTail"),
                "idle_play_rate": 0.35, "idle_variation_play_rate": 0.6,
                "prefer_sitting_when_resting": False,
                "sit_down_animation": None, "sit_idle_animation": None,
                "stand_up_animation": None, "sleep_animation": None}
    parent = unreal.load_class(None, "/Game/Mirmir/Preview/BP_MirmirCat.BP_MirmirCat_C")
    if not parent:
        raise RuntimeError("Configured original cat Blueprint is absent.")
    prefix = "/Game/Mirmir/Appearance/Review_" + ("fur" if use_fur else "base")
    subsystem = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
    cat = helper.create_blueprint(subsystem, prefix + "/BP_AppearanceCat", parent, settings)
    cat_class = unreal.BlueprintEditorLibrary.generated_class(cat)
    mode = helper.create_blueprint(subsystem, prefix + "/BP_AppearanceMode",
                                   unreal.load_class(None, "/Script/Mirmir.MirmirGameMode"),
                                   {"cat_class": cat_class, "toy_material": unreal.load_asset("/Game/Mirmir/Toys/M_ToyBall.M_ToyBall")})
    map_path = prefix + "/AppearanceReview"
    world = helper.existing_owned(subsystem, map_path, unreal.World)
    if not world:
        world = unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
        world.get_world_settings().set_editor_property("default_game_mode",
                                                       unreal.BlueprintEditorLibrary.generated_class(mode))
        helper.mark_owned(subsystem, world)
        if not unreal.EditorLoadingAndSavingUtils.save_map(world, map_path):
            raise RuntimeError("Appearance review map could not be saved.")
    record = {"review_map": map_path, "cat_blueprint": cat.get_path_name(),
              "fur_enabled": use_fur, "motion_source_sha256": source["source_sha256"],
              "product_blueprint_changed": False, "runtime_quality_accepted": False}
    target = ROOT / (".local/appearance-review-" + ("fur" if use_fur else "base") + ".json")
    target.write_text(json.dumps(record, indent=2) + "\n")
    unreal.log("Mirmir appearance review ready: " + json.dumps(record))


if __name__ == "__main__":
    main()
