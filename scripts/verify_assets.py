#!/usr/bin/env python3
"""Ensure attributed source artifacts have not been replaced silently."""
import hashlib
import json
from pathlib import Path
import struct

root = Path(__file__).resolve().parents[1]
sources = json.loads((root / "assets/sources.json").read_text())


def local_path(relative, base=root):
    path = (base / relative).resolve()
    if root not in path.parents or not path.is_file():
        raise SystemExit(f"Missing asset or path outside project: {relative}")
    return path


def verify_hash(relative, expected, base=root):
    path = local_path(relative, base)
    if hashlib.sha256(path.read_bytes()).hexdigest() != expected:
        raise SystemExit(f"Asset hash mismatch: {relative}")
    return path


def glb(path):
    data = path.read_bytes()
    if len(data) < 20 or struct.unpack_from("<III", data) != (0x46546C67, 2, len(data)):
        raise SystemExit(f"Invalid GLB header: {path.relative_to(root)}")
    offset = 12
    chunks = {}
    while offset < len(data):
        if offset + 8 > len(data):
            raise SystemExit("Truncated GLB chunk header")
        length, kind = struct.unpack_from("<II", data, offset)
        offset += 8
        if offset + length > len(data) or kind in chunks or length % 4:
            raise SystemExit("Invalid, duplicated or truncated GLB chunk")
        chunks[kind] = data[offset:offset + length]
        offset += length
    if 0x4E4F534A not in chunks or 0x004E4942 not in chunks:
        raise SystemExit("Expected an embedded GLB JSON/BIN pair")
    return json.loads(chunks[0x4E4F534A]), chunks[0x004E4942]


def records(value):
    if isinstance(value, dict):
        if "sha256" in value:
            yield value
        for child in value.values():
            yield from records(child)
    elif isinstance(value, list):
        for child in value:
            yield from records(child)

checked = 0
for entry in records(sources):
    relative = entry.get("local_path") or entry.get("path") or entry.get("file")
    if not relative:
        raise SystemExit("Asset manifest record is missing its local path")
    verify_hash(relative, entry["sha256"])
    checked += 1
if checked != 4:
    raise SystemExit(f"Expected four licensed assets; found {checked}")
print(f"MIRMIR_ASSETS {checked}/4 original runtime SHA256 verified")
optional = root / "assets/personal-use/sources.json"
if optional.exists():
    count = 0
    for entry in records(json.loads(optional.read_text())):
        verify_hash(entry["file"], entry["sha256"])
        count += 1
    if count != 6:
        raise SystemExit(f"Expected one personal-use GLB and five original source files, found {count}")
    print("MIRMIR_ASSETS personal-use GLB + 5 original sources SHA256 verified")
loop_manifest = root / "assets/personal-use/loop-variant.json"
if loop_manifest.exists():
    entry = json.loads(loop_manifest.read_text())
    for kind in ("source", "variant"):
        verify_hash(entry[kind + "_file"], entry[kind + "_sha256"])
    if not entry["all_original_authored_samples_bit_preserved"] or not entry["all_first_final_sample_blocks_bit_identical"]:
        raise SystemExit("Loop variant has no recorded sample-preservation verification")
    print("MIRMIR_ASSETS optional loop variant and preserved source SHA256 verified")

donor_dir = root / "assets/provenance/tuxedo-motion"
donor = json.loads((donor_dir / "source.json").read_text())
donor_files = {entry["file"]: entry for entry in donor["files"]}
if set(donor_files) != {"scene.gltf", "scene.bin", "LICENSE.md"}:
    raise SystemExit("Tuxedo donor must retain its glTF, BIN and accompanying attribution")
if (donor["uid"] != "783fcb78b55b4394a212c2b6392e1113"
        or donor["author"]["username"] != "DreamNoms"
        or donor["license"]["slug"] != "by"
        or donor["license"]["url"].replace("http://", "https://") != "https://creativecommons.org/licenses/by/4.0/"):
    raise SystemExit("Tuxedo donor identity/license provenance is inconsistent")
for name, entry in donor_files.items():
    path = verify_hash(name, entry["sha256"], donor_dir)
    if path.stat().st_size != entry["bytes"]:
        raise SystemExit(f"Tuxedo donor length mismatch: {name}")
donor_scene = json.loads((donor_dir / "scene.gltf").read_text())
if [buffer.get("uri") for buffer in donor_scene["buffers"]] != ["scene.bin"]:
    raise SystemExit("Tuxedo donor references an unexpected external buffer")
print("MIRMIR_ASSETS Tuxedo donor glTF/BIN/license SHA256 and identity verified")

fk = json.loads((root / "assets/personal-use/tuxedo-motion-candidate.json").read_text())
ik = json.loads((root / "assets/personal-use/tuxedo-motion-ik-candidate.json").read_text())
target_path = verify_hash(fk["target_source"], fk["target_source_sha256"])
target_json, target_binary = glb(target_path)
fk_path = verify_hash(fk["output"], fk["output_sha256"])
fk_json, fk_binary = glb(fk_path)
ik_path = verify_hash(ik["output"], ik["output_sha256"])
ik_json, ik_binary = glb(ik_path)
if (fk["donor_gltf_sha256"] != donor_files["scene.gltf"]["sha256"]
        or fk["donor_bin_sha256"] != donor_files["scene.bin"]["sha256"]
        or local_path(fk["donor_source"]) != donor_dir / "scene.gltf"
        or local_path(ik["base_candidate"]) != fk_path
        or ik["base_candidate_sha256"] != fk["output_sha256"]):
    raise SystemExit("Retarget donor/base provenance does not match the pinned sources")
target_prefix_sha = hashlib.sha256(target_binary).hexdigest()
if (fk_binary[:len(target_binary)] != target_binary
        or ik_binary[:len(fk_binary)] != fk_binary
        or fk["original_target_binary_prefix_sha256"] != target_prefix_sha
        or fk["preserved_binary_prefix_sha256"] != target_prefix_sha):
    raise SystemExit("Retarget output replaced original geometry/animation binary data")
for label, manifest, document, path in [("FK", fk, fk_json, fk_path), ("IK", ik, ik_json, ik_path)]:
    if path.stat().st_size != manifest["output_bytes"]:
        raise SystemExit(f"{label} candidate size differs from its provenance")
    for key in ("nodes", "meshes", "skins", "materials", "images", "textures"):
        if document.get(key) != target_json.get(key):
            raise SystemExit(f"{label} candidate changed target {key}")
    if document["animations"][:len(target_json["animations"])] != target_json["animations"]:
        raise SystemExit(f"{label} candidate changed the original authored walk")
    animations = {animation["name"]: animation for animation in document["animations"]}
    for name, clip in manifest["clips"].items():
        if name not in animations:
            raise SystemExit(f"{label} candidate is missing its recorded clip: {name}")
        for sampler in animations[name]["samplers"]:
            if (sampler.get("interpolation", "LINEAR") != "LINEAR"
                    or document["accessors"][sampler["input"]]["count"] != clip["sample_count"]):
                raise SystemExit(f"{label} candidate clip sampling differs from its provenance: {name}")
for animation in fk_json["animations"]:
    if animation["name"] not in {"Tuxedo_SitDown", "Tuxedo_IdleSit", "Tuxedo_StandUp"} and animation not in ik_json["animations"]:
        raise SystemExit("IK variant changed a preserved original walk/idle clip")
print("MIRMIR_ASSETS both retarget GLBs SHA256, target binary prefixes, geometry and authored walk verified")

fur_expected = "43e54a8ed44e9cefd045e971759f6e604dd70ed65e115e96c7d03479816a3455"
fur = json.loads((root / "assets/generated/fur-detail-v1.json").read_text())
if (fur["sha256"] != fur_expected or fur["tool"] != "image_gen.imagegen"
        or not fur.get("prompt") or fur.get("input_images") != [] or fur["source_bitmap_modified"]):
    raise SystemExit("Generated fur origin/prompt or immutable source SHA is inconsistent")
verify_hash("assets/generated/fur-detail-v1.png", fur_expected)
print("MIRMIR_ASSETS immutable generated fur bitmap SHA256 and prompt/origin record verified")

current = json.loads((root / "assets/provenance/appearance/current-assets.json").read_text())
expected_current = {
    "unreal/Mirmir/Content/Mirmir/Appearance/M_CatCoat.uasset",
    "unreal/Mirmir/Content/Mirmir/Appearance/M_CatEyes.uasset",
    "unreal/Mirmir/Content/Mirmir/Appearance/M_CatFur.uasset",
    "unreal/Mirmir/Content/Mirmir/Appearance/T_FineFurDetail.uasset",
    "unreal/Mirmir/Content/Mirmir/Toys/M_ToyBall.uasset",
}
if (len(current["current_assets"]) != 5
        or {entry["file"] for entry in current["current_assets"]} != expected_current):
    raise SystemExit("Current appearance/toy snapshot does not contain the five expected assets")
for entry in current["current_assets"] + current["scripts"] + current["original_imported_assets"]:
    path = verify_hash(entry["file"], entry["sha256"])
    if path.stat().st_size != entry["bytes"]:
        raise SystemExit(f"Snapshot file size mismatch: {entry['file']}")
if len(current["original_imported_assets"]) != 14:
    raise SystemExit("Expected fourteen unchanged original Unreal cat imports")
# Only the original-import baseline is immutable in this historical record.
# Its earlier generated material hashes describe prior editor snapshots.
baseline = json.loads((root / "assets/provenance/appearance/materials.json").read_text())
recorded_imports = {entry["file"]: entry["sha256"] for entry in current["original_imported_assets"]}
if recorded_imports != baseline["original_imported_asset_sha256"]:
    raise SystemExit("Current snapshot changed the original imported-asset baseline")
print("MIRMIR_ASSETS current appearance/toy assets, recipe scripts and 14 original imports SHA256 verified")
print("MIRMIR_ASSETS integrity checks passed; rendered quality and runtime physics are separate verification")
