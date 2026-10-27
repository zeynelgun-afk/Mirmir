"""Promote the rendered appearance/idle assets to the owned product preview.

Preserves original source imports and user save data. Records defaults and
backs up the two owned Blueprint files before changing their references.
"""
from pathlib import Path
from datetime import datetime, timezone
import json
import shutil

import unreal

ROOT = Path(__file__).resolve().parents[3]


def main():
    subsystem = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
    paths = {"cat": "/Game/Mirmir/Preview/BP_MirmirCat.BP_MirmirCat",
             "mode": "/Game/Mirmir/Preview/BP_MirmirGameMode.BP_MirmirGameMode"}
    assets = {key: unreal.load_asset(path) for key, path in paths.items()}
    for asset in assets.values():
        if not isinstance(asset, unreal.Blueprint) or subsystem.get_metadata_tag(asset, "MirmirBootstrapOwner") != "mirmir-preview-bootstrap-v1":
            raise RuntimeError("Product Blueprint is absent or not owned; nothing was changed.")
    classes = {key: unreal.BlueprintEditorLibrary.generated_class(value) for key, value in assets.items()}
    defaults = {key: unreal.get_default_object(value) for key, value in classes.items()}
    imported = json.loads((ROOT / ".local/motion-import.json").read_text())

    def clip(suffix):
        matches = [entry for entry in imported["imported_animations"] if entry["name"].endswith(suffix)]
        if len(matches) != 1:
            raise RuntimeError("Reviewed idle clip is absent or ambiguous: " + suffix)
        return unreal.load_asset(matches[0]["path"])

    settings = {"coat_material_template": unreal.load_asset("/Game/Mirmir/Appearance/M_CatCoat.M_CatCoat"),
                "eye_material_template": unreal.load_asset("/Game/Mirmir/Appearance/M_CatEyes.M_CatEyes"),
                "fur_material_template": unreal.load_asset("/Game/Mirmir/Appearance/M_CatFur.M_CatFur"),
                "idle_animation": clip("Tuxedo_Idle"), "idle_variation_animation": clip("Tuxedo_IdleTail"),
                "idle_play_rate": 0.35, "idle_variation_play_rate": 0.6,
                "enable_fur_shells": True, "use_native_animation_blending": True,
                "prefer_sitting_when_resting": False, "sit_down_animation": None,
                "sit_idle_animation": None, "stand_up_animation": None, "sleep_animation": None}
    if any(not settings[name] for name in ["coat_material_template", "eye_material_template", "fur_material_template", "idle_animation", "idle_variation_animation"]):
        raise RuntimeError("A required reviewed asset is missing.")
    skeleton = defaults["cat"].get_editor_property("cat_skeletal_mesh").get_editor_property("skeleton")
    for name in ["idle_animation", "idle_variation_animation"]:
        if settings[name].get_editor_property("skeleton") != skeleton:
            raise RuntimeError("Reviewed motion does not match the product skeleton.")
    toy = unreal.load_asset("/Game/Mirmir/Toys/M_ToyBall.M_ToyBall")
    if not toy or defaults["mode"].get_editor_property("cat_class") != classes["cat"]:
        raise RuntimeError("Toy material missing or game mode uses another cat; nothing was changed.")
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    backup = ROOT / ".local/backups" / ("before-appearance-" + stamp)
    backup.mkdir(parents=True, exist_ok=False)

    def describe(value):
        return value.get_path_name() if isinstance(value, unreal.Object) else value

    before = {name: describe(defaults["cat"].get_editor_property(name)) for name in settings}
    for key, asset in assets.items():
        filename = ROOT / "unreal/Mirmir/Content" / (asset.get_path_name().split(".")[0].removeprefix("/Game/") + ".uasset")
        shutil.copy2(filename, backup / (key + ".uasset"))
    (backup / "defaults.json").write_text(json.dumps(before, indent=2) + "\n")
    for name, value in settings.items():
        defaults["cat"].set_editor_property(name, value)
    defaults["mode"].set_editor_property("toy_material", toy)
    for asset in assets.values():
        if not unreal.BlueprintEditorLibrary.compile_blueprint(asset) or not subsystem.save_loaded_asset(asset, False):
            raise RuntimeError("Could not compile/save product Blueprint; backup is at " + str(backup))
    record = {"schema_version": 1, "product_blueprint_changed": True, "backup": str(backup),
              "source_motion_sha256": imported["source_sha256"],
              "settings": {key: describe(defaults["cat"].get_editor_property(key)) for key in settings},
              "toy_material": toy.get_path_name(), "default_appearance_for_legacy_save": "van",
              "user_data_modified": False, "shipping_runtime_verified": False}
    (ROOT / ".local/promoted-appearance.json").write_text(json.dumps(record, indent=2) + "\n")
    unreal.log("Mirmir product defaults promoted: " + json.dumps(record))


if __name__ == "__main__":
    main()
