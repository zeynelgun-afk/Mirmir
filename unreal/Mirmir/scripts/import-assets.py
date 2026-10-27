"""Import the project's existing licensed source assets in the Unreal Editor.

Not a runtime importer; not run/validated against Unreal on this workstation.
Uses UE AssetImportTask APIs and the editor's installed glTF
Interchange importer. Does not download files, change source bytes, overwrite
existing assets, or generate animation/Groom/Blueprints.
"""

from pathlib import Path

import unreal


ROOT = Path(__file__).resolve().parents[3]
SHARED = ROOT / "desktop-lab" / "assets"
PERSONAL_CAT = ROOT / "assets" / "personal-use" / "cat-walk.glb"
PERSONAL_LOOP = PERSONAL_CAT.with_name("cat-walk-loop.glb")
DESTINATION = "/Game/Mirmir/Shared"


def import_shared_assets():
    cat_source = PERSONAL_LOOP if PERSONAL_LOOP.is_file() else PERSONAL_CAT if PERSONAL_CAT.is_file() else SHARED / "cat" / "fripouille.glb"
    personal = cat_source in (PERSONAL_CAT, PERSONAL_LOOP)
    sources = (
        (cat_source, f"{DESTINATION}/Cat"),
        (SHARED / "audio" / "purr.ogg", f"{DESTINATION}/Audio"),
        (SHARED / "audio" / "meow-1.ogg", f"{DESTINATION}/Audio"),
        (SHARED / "audio" / "meow-2.ogg", f"{DESTINATION}/Audio"),
    )
    required = [ROOT / "ASSET-LICENSES.md", *(source for source, _ in sources)]
    if personal:
        required.append(PERSONAL_CAT.parent / "sources.json")
    if cat_source == PERSONAL_LOOP:
        required.append(PERSONAL_CAT.parent / "loop-variant.json")
    missing = [str(path) for path in required if not path.is_file()]
    if missing:
        raise RuntimeError("Missing shared asset/license files: " + ", ".join(missing))

    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    if registry.get_assets_by_path(DESTINATION, recursive=True):
        raise RuntimeError(
            f"{DESTINATION} already contains assets. Import stopped to preserve "
            "artist changes; inspect/reimport deliberately in the editor."
        )

    if personal:
        unreal.log_warning("LOCAL PERSONAL NONCOMMERCIAL ASSET: Cat Walk by LostBoyz2078, CC BY-NC 4.0.")
        unreal.log(f"Personal-use license and provenance: {PERSONAL_CAT.parent / 'sources.json'}")
    else:
        unreal.log("Mirmir source credit: Fripouille by guillaume bolis, CC BY 4.0; no authored walk clip.")
    unreal.log("Mirmir audio credit: Joseph SARDIN / BigSoundBank, CC0.")
    unreal.log(f"License details: {ROOT / 'ASSET-LICENSES.md'}")
    tasks = []
    for source, destination in sources:
        task = unreal.AssetImportTask()
        task.set_editor_property("filename", str(source))
        task.set_editor_property("destination_path", destination)
        task.set_editor_property("automated", True)
        task.set_editor_property("async_", False)
        task.set_editor_property("replace_existing", False)
        task.set_editor_property("replace_existing_settings", False)
        task.set_editor_property("save", True)
        tasks.append(task)

    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)
    meshes = []
    animations = []
    failed_sources = []
    for (source, _), task in zip(sources, tasks):
        objects = list(task.get_objects())
        unreal.log(f"Mirmir imported object paths for {source.name}:")
        for object_path in task.get_editor_property("imported_object_paths"):
            unreal.log(f"  {object_path}")
        if not objects:
            failed_sources.append(source.name)
            unreal.log_warning(f"No imported objects returned for {source.name}.")
        for asset in objects:
            unreal.log(f"  {asset.get_path_name()} [{asset.get_class().get_name()}]")
            if isinstance(asset, unreal.SkeletalMesh):
                meshes.append(asset)
            if isinstance(asset, unreal.AnimSequence):
                animations.append(asset)

    for mesh in meshes:
        skeleton = mesh.get_editor_property("skeleton")
        physics = mesh.get_editor_property("physics_asset")
        material_count = len(mesh.get_editor_property("materials"))
        unreal.log(f"Mirmir SkeletalMesh audit: {mesh.get_path_name()}")
        unreal.log(f"  Skeleton: {skeleton.get_path_name() if skeleton else 'NONE'}")
        unreal.log(f"  PhysicsAsset: {physics.get_path_name() if physics else 'NONE'}")
        unreal.log(f"  Material slots: {material_count}")

    for sequence in animations:
        skeleton = sequence.get_editor_property("skeleton")
        unreal.log(f"Mirmir AnimSequence audit: {sequence.get_path_name()}")
        unreal.log(f"  Length seconds: {sequence.get_play_length()}")
        unreal.log(f"  Skeleton: {skeleton.get_path_name() if skeleton else 'NONE'}")

    if failed_sources or not meshes or (personal and not animations):
        raise RuntimeError(
            "Mirmir import incomplete. Failed sources: "
            + (", ".join(failed_sources) or "none")
            + "; SkeletalMesh count: "
            + str(len(meshes))
            + "; AnimSequence count: " + str(len(animations))
            + ". Check Interchange Editor/Framework and glTF support, then inspect "
            "the partial /Game/Mirmir/Shared import. No source files were changed."
        )
    unreal.log(
        "Mirmir source assets imported; assign the audited mesh/skeleton in "
        "BP_MirmirCat and its walk AnimSequence to WalkAnimation. Actual gait/contact, "
        "idle animation, Groom, visual quality, UHT/module build "
        "and desktop overlay are not validated by this import helper."
    )


if __name__ == "__main__":
    import_shared_assets()
