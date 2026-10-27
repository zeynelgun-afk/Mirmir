"""Create isolated idle/sitting review maps; leave the product Blueprint intact."""
from pathlib import Path
import importlib.util
import json
import os

import unreal

ROOT = Path(__file__).resolve().parents[3]


def main():
    imported = json.loads((ROOT / ".local/motion-import.json").read_text())
    posture = os.environ.get("MIRMIR_REVIEW_POSTURE", "idle")
    if posture not in {"idle", "sit", "tail"}:
        raise RuntimeError("Review posture must be idle, sit or tail.")
    specification = importlib.util.spec_from_file_location("mirmir_bootstrap_helpers",
                                                         Path(__file__).with_name("bootstrap-preview.py"))
    helper = importlib.util.module_from_spec(specification)
    specification.loader.exec_module(helper)
    helper.OWNER = "mirmir-motion-review-v1"
    prefix = imported["destination"] + "/Review_" + posture
    subsystem = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)

    def clip(suffix):
        normalized = suffix.replace("_", "").lower()
        matches = [entry for entry in imported["imported_animations"]
                   if entry["name"].replace("_", "").lower().endswith(normalized)]
        if len(matches) != 1:
            raise RuntimeError("Review clip is absent or ambiguous: " + suffix)
        return unreal.load_asset(matches[0]["path"])

    parent = unreal.load_class(None, "/Game/Mirmir/Preview/BP_MirmirCat.BP_MirmirCat_C")
    if not parent:
        raise RuntimeError("Original configured cat Blueprint is missing.")
    settings = {"idle_animation": clip("Tuxedo_IdleTail" if posture == "tail" else "Tuxedo_Idle"),
                "prefer_sitting_when_resting": posture == "sit",
                "sit_down_animation": clip("Tuxedo_SitDown") if posture == "sit" else None,
                "sit_idle_animation": clip("Tuxedo_IdleSit") if posture == "sit" else None,
                "stand_up_animation": clip("Tuxedo_StandUp") if posture == "sit" else None,
                "sleep_animation": None}
    cat = helper.create_blueprint(subsystem, prefix + "/BP_MotionCat", parent, settings)
    cat_class = unreal.BlueprintEditorLibrary.generated_class(cat)
    mode = helper.create_blueprint(subsystem, prefix + "/BP_MotionGameMode",
                                   unreal.load_class(None, "/Script/Mirmir.MirmirGameMode"),
                                   {"cat_class": cat_class})
    mode_class = unreal.BlueprintEditorLibrary.generated_class(mode)
    map_path = prefix + "/MotionReview"
    world = helper.existing_owned(subsystem, map_path, unreal.World)
    if not world:
        world = unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
        world.get_world_settings().set_editor_property("default_game_mode", mode_class)
        helper.mark_owned(subsystem, world)
        if not unreal.EditorLoadingAndSavingUtils.save_map(world, map_path):
            raise RuntimeError("Motion review map could not be saved.")
    defaults = unreal.get_default_object(cat_class)
    record = {"source_sha256": imported["source_sha256"], "posture": posture,
              "review_map": map_path, "cat_blueprint": cat.get_path_name(),
              "settings": {key: helper.object_path(defaults.get_editor_property(key))
                           if key.endswith("animation") else defaults.get_editor_property(key) for key in settings},
              "product_blueprint_changed": False, "visual_quality_accepted": False}
    (ROOT / (".local/motion-review-" + posture + ".json")).write_text(json.dumps(record, indent=2) + "\n")
    unreal.log("Mirmir isolated motion review ready: " + json.dumps(record))


if __name__ == "__main__":
    main()
