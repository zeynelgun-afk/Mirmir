#!/usr/bin/env python3
"""Ensure attributed source artifacts have not been replaced silently."""
import hashlib
import json
from pathlib import Path

root = Path(__file__).resolve().parents[1]
sources = json.loads((root / "assets/sources.json").read_text())
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
    path = root / relative
    actual = hashlib.sha256(path.read_bytes()).hexdigest()
    if actual != entry["sha256"]:
        raise SystemExit(f"Asset hash mismatch: {relative}")
    checked += 1
if checked != 4:
    raise SystemExit(f"Expected four licensed assets; found {checked}")
print(f"MIRMIR_ASSETS {checked}/4 original runtime SHA256 verified")
optional = root / "assets/personal-use/sources.json"
if optional.exists():
    count = 0
    for entry in records(json.loads(optional.read_text())):
        path = (root / entry["file"]).resolve()
        if root not in path.parents:
            raise SystemExit("Manifest path leaves project")
        if hashlib.sha256(path.read_bytes()).hexdigest() != entry["sha256"]:
            raise SystemExit(f"Personal-use asset/source hash mismatch: {entry['file']}")
        count += 1
    if count != 6:
        raise SystemExit(f"Expected one personal-use GLB and five original source files, found {count}")
    print("MIRMIR_ASSETS personal-use GLB + 5 original sources SHA256 verified")
loop_manifest = root / "assets/personal-use/loop-variant.json"
if loop_manifest.exists():
    entry = json.loads(loop_manifest.read_text())
    for kind in ("source", "variant"):
        path = (root / entry[kind + "_file"]).resolve()
        if root not in path.parents or hashlib.sha256(path.read_bytes()).hexdigest() != entry[kind + "_sha256"]:
            raise SystemExit(f"Loop {kind} hash mismatch")
    if not entry["all_original_authored_samples_bit_preserved"] or not entry["all_first_final_sample_blocks_bit_identical"]:
        raise SystemExit("Loop variant has no recorded sample-preservation verification")
    print("MIRMIR_ASSETS optional loop variant and preserved source SHA256 verified")
