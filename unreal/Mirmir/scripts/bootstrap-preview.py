"""Create an owned UE preview from assets already imported into /Game/Mirmir/Shared.

Editor-only, to run after the real UE C++ build and import-assets.py. This
source has not been executed in Unreal on the workstation. It does not create
fake animation, Groom, or a desktop overlay, and never replaces foreign assets.
"""

import json
import os
from pathlib import Path
import tempfile

import unreal


PROJECT = Path(__file__).resolve().parents[3]
SHARED = "/Game/Mirmir/Shared"
PREVIEW = "/Game/Mirmir/Preview"
CAT_PATH = PREVIEW + "/BP_MirmirCat"
MODE_PATH = PREVIEW + "/BP_MirmirGameMode"
MAP_PATH = PREVIEW + "/MirmirPreview"
OWNER_TAG = "MirmirBootstrapOwner"
STATUS_TAG = "MirmirBootstrapStatus"
OWNER = "mirmir-preview-bootstrap-v1"


def log(message):
    unreal.log("Mirmir bootstrap: " + message)


def object_path(value):
    return value.get_path_name() if value else None


def find_imports(registry):
    assets = [data.get_asset() for data in registry.get_assets_by_path(SHARED, recursive=True)]
    meshes = sorted((asset for asset in assets if isinstance(asset, unreal.SkeletalMesh)), key=object_path)
    sequences = sorted((asset for asset in assets if isinstance(asset, unreal.AnimSequence)), key=object_path)
    sounds = sorted((asset for asset in assets if isinstance(asset, unreal.SoundWave)), key=object_path)
    pairs = []
    for mesh in meshes:
        skeleton = mesh.get_editor_property("skeleton")
        for sequence in sequences:
            if (skeleton and object_path(sequence.get_editor_property("skeleton")) == object_path(skeleton)
                    and abs(sequence.get_play_length() - 0.7666667) < 0.05):
                pairs.append((mesh, sequence))
    if not pairs:
        raise RuntimeError("No imported SkeletalMesh has a compatible ~0.767-second authored walk. Run import-assets.py and inspect its audit.")
    if len(pairs) != 1:
        scored = []
        for mesh, sequence in pairs:
            materials = len(mesh.get_editor_property("materials"))
            extent = 0.0
            bounds_getter = getattr(mesh, "get_bounds", None)
            if bounds_getter:
                bounds = bounds_getter()
                extent = max(bounds.box_extent.x, bounds.box_extent.y, bounds.box_extent.z)
            log("Mesh completeness audit: " + object_path(mesh) + "; material slots=" + str(materials) + "; maximum bound extent=" + str(extent))
            scored.append(((extent, materials), mesh, sequence))
        best_score = max(item[0] for item in scored)
        best = [(mesh, sequence) for score, mesh, sequence in scored if score == best_score]
        if len(best) != 1:
            raise RuntimeError("Compatible mesh/walk selection remains ambiguous after bounds/material audit; imports left untouched.")
        pairs = best
        log("Selected the most complete bounds/material candidate; inspect for missing separate body parts in the real preview.")
    purr = next((sound for sound in sounds if "purr" in sound.get_name().lower()), None)
    meow = next((sound for sound in sounds if "meow" in sound.get_name().lower()), None)
    feed = None  # No feeding recording is supplied; never relabel a meow.
    if not purr:
        raise RuntimeError("No imported real purr SoundWave found.")
    if not feed:
        log("No feeding recording exists; FeedSound stays unassigned. Meows are not relabeled as food sounds.")
    mesh, walk = pairs[0]
    extras = [part for part in meshes if part != mesh and
              object_path(part.get_editor_property("skeleton")) == object_path(mesh.get_editor_property("skeleton"))]
    return mesh, walk, purr, meow, feed, sounds, extras


def existing_owned(subsystem, path, expected_type):
    # Loading a missing object creates an empty package in UE 5.8; the
    # Blueprint creation API then correctly refuses that already-loaded package.
    if not subsystem.does_asset_exist(path):
        return None
    asset = unreal.load_asset(path)
    if not asset:
        return None
    if not isinstance(asset, expected_type):
        raise RuntimeError("Existing asset has unexpected type; left untouched: " + path)
    if subsystem.get_metadata_tag(asset, OWNER_TAG) != OWNER:
        raise RuntimeError("Existing asset is not owned by this bootstrap; left untouched: " + path)
    if subsystem.get_metadata_tag(asset, STATUS_TAG) != "complete":
        raise RuntimeError("Existing bootstrap asset is incomplete; inspect it before retrying: " + path)
    return asset


def mark_owned(subsystem, asset):
    subsystem.set_metadata_tag(asset, OWNER_TAG, OWNER)
    subsystem.set_metadata_tag(asset, STATUS_TAG, "complete")


def create_blueprint(subsystem, path, parent_class, defaults):
    existing = existing_owned(subsystem, path, unreal.Blueprint)
    if existing:
        log("Reusing owned Blueprint without overwriting defaults: " + path)
        return existing
    blueprint = unreal.BlueprintEditorLibrary.create_blueprint_asset_with_parent(path, parent_class)
    if not blueprint or not unreal.BlueprintEditorLibrary.compile_blueprint(blueprint):
        raise RuntimeError("Could not create/compile Blueprint: " + path)
    generated = unreal.BlueprintEditorLibrary.generated_class(blueprint)
    if not generated:
        raise RuntimeError("Blueprint has no generated class: " + path)
    default_object = unreal.get_default_object(generated)
    for name, value in defaults.items():
        if name == "normalize_imported_mesh":
            # Python bool-name normalization can vary for project properties;
            # support the reflected native name without silently dropping it.
            for alias in (name, "b_normalize_imported_mesh", "bNormalizeImportedMesh"):
                try:
                    default_object.set_editor_property(alias, value)
                    break
                except Exception:
                    if alias == "bNormalizeImportedMesh":
                        raise
        else:
            default_object.set_editor_property(name, value)
    if not unreal.BlueprintEditorLibrary.compile_blueprint(blueprint):
        raise RuntimeError("Blueprint defaults did not compile: " + path)
    mark_owned(subsystem, blueprint)
    if not subsystem.save_loaded_asset(blueprint, False):
        raise RuntimeError("Could not save Blueprint: " + path)
    return blueprint


def main():
    record_path = PROJECT / ".local" / "preview.json"
    if record_path.exists():
        previous = json.loads(record_path.read_text(encoding="utf-8"))
        if previous.get("owner") != OWNER:
            raise RuntimeError("Existing preview.json is not owned by this bootstrap; left untouched.")
    subsystem = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    registry.scan_paths_synchronous([SHARED], False)
    mesh, walk, purr, meow, feed, sounds, extras = find_imports(registry)
    cat_parent = unreal.load_class(None, "/Script/Mirmir.MirmirCat")
    mode_parent = unreal.load_class(None, "/Script/Mirmir.MirmirGameMode")
    if not cat_parent or not mode_parent:
        raise RuntimeError("Mirmir C++ classes are unavailable; build/load the real UE module before bootstrap.")

    # Inspect every existing destination before creating anything.
    existing_owned(subsystem, CAT_PATH, unreal.Blueprint)
    existing_owned(subsystem, MODE_PATH, unreal.Blueprint)
    existing_owned(subsystem, MAP_PATH, unreal.World)
    cat = create_blueprint(subsystem, CAT_PATH, cat_parent, {
        "cat_skeletal_mesh": mesh,
        "additional_skeletal_meshes": extras,
        "walk_animation": walk,
        "authored_walk_speed": 68.0,
        "normalize_imported_mesh": True,
        "target_body_length": 70.0,
        "purr_sound": purr,
        "meow_sound": meow,
        "feed_sound": feed,
    })
    cat_class = unreal.BlueprintEditorLibrary.generated_class(cat)
    mode = create_blueprint(subsystem, MODE_PATH, mode_parent, {"cat_class": cat_class})
    mode_class = unreal.BlueprintEditorLibrary.generated_class(mode)
    if not cat_class or not mode_class:
        raise RuntimeError("Preview Blueprint classes are missing after compile.")
    world = existing_owned(subsystem, MAP_PATH, unreal.World)
    if not world:
        world = unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
        if not world:
            raise RuntimeError("Could not create a blank preview world.")
        world.get_world_settings().set_editor_property("default_game_mode", mode_class)
        mark_owned(subsystem, world)
        if not unreal.EditorLoadingAndSavingUtils.save_map(world, MAP_PATH):
            raise RuntimeError("Could not save preview map.")
    else:
        log("Reusing owned preview map without overwriting it.")

    # Read back actual defaults. Existing owned assets may have been edited;
    # report their current values rather than claiming new defaults were applied.
    cat_defaults = unreal.get_default_object(cat_class)
    mode_defaults = unreal.get_default_object(mode_class)
    record = {
        "owner": OWNER,
        "schema_version": 1,
        "bootstrap_editor_api_executed": True,
        "preview_map": MAP_PATH,
        "preview_world_object": object_path(world),
        "cat_blueprint": object_path(cat),
        "game_mode_blueprint": object_path(mode),
        "cat_class": object_path(cat_class),
        "game_mode_class": object_path(mode_class),
        "configured_cat_class": object_path(mode_defaults.get_editor_property("cat_class")),
        "configured_skeletal_mesh": object_path(cat_defaults.get_editor_property("cat_skeletal_mesh")),
        "configured_additional_meshes": [object_path(part) for part in cat_defaults.get_editor_property("additional_skeletal_meshes")],
        "configured_walk_animation": object_path(cat_defaults.get_editor_property("walk_animation")),
        "configured_authored_walk_speed": cat_defaults.get_editor_property("authored_walk_speed"),
        "configured_purr_sound": object_path(cat_defaults.get_editor_property("purr_sound")),
        "configured_feed_sound": object_path(cat_defaults.get_editor_property("feed_sound")),
        "configured_meow_sound": object_path(cat_defaults.get_editor_property("meow_sound")),
        "configured_world_game_mode": object_path(world.get_world_settings().get_editor_property("default_game_mode")),
        "audited_walk_length_seconds": walk.get_play_length(),
        "audited_real_sound_waves": [object_path(sound) for sound in sounds],
        "gait_contact_verified": False,
        "mesh_orientation_verified": False,
        "authored_walk_speed_calibrated": False,
        "desktop_overlay_verified": False,
        "notes": [
            "The 68 cm/s walk reference is estimated from authored contact-joint data at 70 cm body length; real UE contact remains unverified.",
            "No idle/run/sit clips, Groom or fake procedural leg motion were generated.",
            "Editor bootstrap success is not runtime playback, UHT build, gait/contact or desktop integration verification.",
            "Cat Walk source is CC BY-NC 4.0, local personal noncommercial use only; preserve its separate source/license record.",
        ],
    }
    record_path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary = tempfile.mkstemp(prefix=".preview-", suffix=".json", dir=record_path.parent)
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8") as output:
            json.dump(record, output, indent=2)
            output.write("\n")
        os.replace(temporary, record_path)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)
    log("Saved owned preview assets and actual configuration audit: " + str(record_path))
    log("Preview map: " + MAP_PATH + "; play it in the real editor and validate gait, orientation and sound.")


if __name__ == "__main__":
    main()
