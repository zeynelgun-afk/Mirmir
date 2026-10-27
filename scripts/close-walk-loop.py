#!/usr/bin/env python3
"""Add one closing first-pose key to a separate authored-walk GLB variant.

Preserves all original binary data and all authored key values. Handles LINEAR
TRS samplers and SCALAR morph-weight blocks. Never edits the source file.
"""
from __future__ import annotations
import argparse
import copy
import hashlib
import json
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[1]
ELEMENTS = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT2": 4, "MAT3": 9, "MAT4": 16}
COMPONENT_BYTES = {5120: 1, 5121: 1, 5122: 2, 5123: 2, 5125: 4, 5126: 4}


def sha(data):
    return hashlib.sha256(data).hexdigest()


def read_glb(path):
    data = path.read_bytes()
    magic, version, length = struct.unpack_from("<III", data)
    if magic != 0x46546C67 or version != 2 or length != len(data):
        raise ValueError("Expected a complete glTF 2.0 GLB.")
    chunks = {}
    offset = 12
    while offset < len(data):
        size, kind = struct.unpack_from("<II", data, offset)
        offset += 8
        if kind in chunks or offset + size > len(data):
            raise ValueError("Duplicate or incomplete GLB chunk.")
        chunks[kind] = data[offset:offset+size]
        offset += size
    if set(chunks) != {0x4E4F534A, 0x004E4942}:
        raise ValueError("Expected one JSON and one BIN chunk.")
    gltf = json.loads(chunks[0x4E4F534A])
    if len(gltf.get("buffers", [])) != 1 or "uri" in gltf["buffers"][0]:
        raise ValueError("Only one self-contained GLB buffer is supported.")
    return data, gltf, chunks[0x004E4942]


def accessor_bytes(gltf, binary, index):
    accessor = gltf["accessors"][index]
    if "sparse" in accessor:
        raise ValueError("Sparse animation accessors are not supported.")
    view = gltf["bufferViews"][accessor["bufferView"]]
    if view.get("buffer", 0) != 0:
        raise ValueError("Animation references a non-GLB buffer.")
    width = ELEMENTS[accessor["type"]] * COMPONENT_BYTES[accessor["componentType"]]
    offset = view.get("byteOffset", 0) + accessor.get("byteOffset", 0)
    stride = view.get("byteStride", width)
    end = offset + max(0, accessor["count"]-1)*stride + width
    if stride < width or end > len(binary):
        raise ValueError("Invalid accessor range/stride.")
    return b"".join(binary[offset+i*stride:offset+i*stride+width] for i in range(accessor["count"]))


def append_accessor(gltf, binary, source_accessor, payload, count, minimum=None, maximum=None):
    while len(binary) % 4:
        binary.append(0)
    gltf["bufferViews"].append({"buffer": 0, "byteOffset": len(binary), "byteLength": len(payload)})
    binary.extend(payload)
    accessor = copy.deepcopy(source_accessor)
    accessor["bufferView"] = len(gltf["bufferViews"])-1
    accessor["byteOffset"] = 0
    accessor["count"] = count
    if minimum is not None:
        accessor["min"] = minimum
    if maximum is not None:
        accessor["max"] = maximum
    gltf["accessors"].append(accessor)
    return len(gltf["accessors"])-1


def close_animation(source, source_binary, sample_rate):
    if len(source.get("animations", [])) != 1:
        raise ValueError("Select a source GLB containing exactly one animation.")
    result = copy.deepcopy(source)
    binary = bytearray(source_binary)
    animation = result["animations"][0]
    proof = []
    next_times = []
    for index, sampler in enumerate(animation["samplers"]):
        if sampler.get("interpolation", "LINEAR") != "LINEAR":
            raise ValueError("Only LINEAR samplers may receive the duplicate first pose.")
        input_accessor = source["accessors"][sampler["input"]]
        output_accessor = source["accessors"][sampler["output"]]
        if input_accessor["type"] != "SCALAR" or input_accessor["componentType"] != 5126:
            raise ValueError("Animation times must be float32 SCALAR values.")
        key_count = input_accessor["count"]
        if key_count < 2:
            raise ValueError("At least two authored keys are required.")
        input_bytes = accessor_bytes(source, source_binary, sampler["input"])
        output_bytes = accessor_bytes(source, source_binary, sampler["output"])
        times = struct.unpack("<"+"f"*key_count, input_bytes)
        if any(abs(t-(times[0]+i/sample_rate)) > 1e-6 for i,t in enumerate(times)):
            raise ValueError("Source keys are not on the expected regular sample grid.")
        paths = sorted({c["target"]["path"] for c in animation["channels"] if c["sampler"] == index})
        if not paths or any(p not in {"translation", "rotation", "scale", "weights"} for p in paths):
            raise ValueError("Unsupported or unreferenced animation sampler.")
        if "weights" in paths:
            if paths != ["weights"] or output_accessor["type"] != "SCALAR":
                raise ValueError("Morph weights require a dedicated SCALAR sampler.")
            if output_accessor["count"] % key_count:
                raise ValueError("Incomplete morph-weight block per key.")
            elements_per_key = output_accessor["count"] // key_count
        else:
            if output_accessor["count"] != key_count:
                raise ValueError("TRS output count differs from input key count.")
            elements_per_key = 1
        bytes_per_key = len(output_bytes) // key_count
        first_pose = output_bytes[:bytes_per_key]
        next_time = times[0] + key_count/sample_rate
        # Pack exactly one regular-sample timestamp, then copy the first block.
        new_input_bytes = input_bytes + struct.pack("<f", next_time)
        new_output_bytes = output_bytes + first_pose
        new_time = struct.unpack("<f", new_input_bytes[-4:])[0]
        sampler["input"] = append_accessor(result, binary, input_accessor,
            new_input_bytes, key_count+1, [times[0]], [new_time])
        sampler["output"] = append_accessor(result, binary, output_accessor,
            new_output_bytes, output_accessor["count"]+elements_per_key)
        actual_input = accessor_bytes(result, binary, sampler["input"])
        actual_output = accessor_bytes(result, binary, sampler["output"])
        assert actual_input[:-4] == input_bytes
        assert actual_output[:-bytes_per_key] == output_bytes
        assert actual_output[:bytes_per_key] == actual_output[-bytes_per_key:]
        old_last = struct.unpack("<"+"f"*(bytes_per_key//4), output_bytes[-bytes_per_key:]) if output_accessor["componentType"] == 5126 else []
        first = struct.unpack("<"+"f"*(bytes_per_key//4), first_pose) if old_last else []
        duration = new_time-times[-1]
        max_component_speed = max((abs(a-b)/duration for a,b in zip(first,old_last)), default=None)
        proof.append({"sampler": index, "paths": paths,
            "original_key_count": key_count, "variant_key_count": key_count+1,
            "elements_per_key": elements_per_key,
            "original_times_sha256": sha(input_bytes), "preserved_times_sha256": sha(actual_input[:-4]),
            "original_values_sha256": sha(output_bytes), "preserved_values_sha256": sha(actual_output[:-bytes_per_key]),
            "first_pose_sha256": sha(first_pose), "last_pose_sha256": sha(actual_output[-bytes_per_key:]),
            "first_and_final_pose_bit_identical": True,
            "previous_last_key_s": times[-1], "appended_key_s": new_time,
            "closing_interval_s": duration,
            "max_closing_component_speed_per_s": max_component_speed,
            "component_speed_note": "Raw component change per second: quaternion components are not angular velocity; not a paw-contact speed."})
        next_times.append(new_time)
    if max(next_times)-min(next_times) > 1e-6:
        raise ValueError("Source samplers have different durations; no boundary is inferred.")
    result["buffers"][0]["byteLength"] = len(binary)
    result["asset"].setdefault("extras", {})["mirmir_loop_variant"] = (
        "All authored samples preserved. One duplicate first pose appended at the next regular sample time to close loop interpolation; no newly invented leg poses.")
    assert bytes(binary[:len(source_binary)]) == source_binary
    for key in ["nodes", "meshes", "skins", "materials", "images", "textures"]:
        assert result.get(key) == source.get(key), key
    assert result["accessors"][:len(source["accessors"])] == source["accessors"]
    return result, binary, proof


def encode_glb(gltf, binary):
    text = json.dumps(gltf,separators=(",",":"),ensure_ascii=False).encode()
    text += b" "*((-len(text)) % 4)
    binary = bytes(binary) + b"\0"*((-len(binary)) % 4)
    return struct.pack("<III",0x46546C67,2,12+8+len(text)+8+len(binary)) + struct.pack("<II",len(text),0x4E4F534A)+text+struct.pack("<II",len(binary),0x004E4942)+binary


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source",type=Path,default=ROOT/"assets/personal-use/cat-walk.glb")
    parser.add_argument("--output",type=Path,default=ROOT/"assets/personal-use/cat-walk-loop.glb")
    parser.add_argument("--provenance",type=Path,default=ROOT/"assets/personal-use/loop-variant.json")
    parser.add_argument("--sample-rate",type=float,default=30.0)
    args=parser.parse_args()
    if args.source.resolve() == args.output.resolve():
        parser.error("Output must differ from source; the original cannot be overwritten.")
    if args.sample_rate <= 0:
        parser.error("Sample rate must be positive.")
    source_bytes,gltf,binary=read_glb(args.source)
    closed,new_binary,proof=close_animation(gltf,binary,args.sample_rate)
    result=encode_glb(closed,new_binary)
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_bytes(result)
    # Re-open the artifact and verify the encoded ranges, not only RAM objects.
    _,verify,verify_binary=read_glb(args.output)
    assert verify["animations"] == closed["animations"]
    assert verify_binary[:len(binary)] == binary
    for old,sampler,record in zip(gltf["animations"][0]["samplers"],verify["animations"][0]["samplers"],proof):
        original=accessor_bytes(gltf,binary,old["output"])
        derived=accessor_bytes(verify,verify_binary,sampler["output"])
        block=len(original)//record["original_key_count"]
        assert derived[:-block] == original and derived[:block] == derived[-block:]
    assert args.source.read_bytes() == source_bytes
    local=lambda p: str(p.relative_to(ROOT)) if p.is_relative_to(ROOT) else str(p)
    report={"schema_version":1,"optional_variant":True,
        "source_file":local(args.source),"source_sha256":sha(source_bytes),
        "variant_file":local(args.output),"variant_sha256":sha(result),"variant_bytes":len(result),
        "author":"LostBoyz2078","author_url":"https://sketchfab.com/LostModels2025",
        "original_work":"https://sketchfab.com/3d-models/cat-walk-915680200c064815bba75e008ba9efb5",
        "license":"CC-BY-NC-4.0","license_url":"https://creativecommons.org/licenses/by-nc/4.0/",
        "scope":"Optional derivative for the user's personal noncommercial Unreal review. No purchase or commercial permission inferred.",
        "modification":"Preserved all existing authored samples and appended only the exact first-pose block at the next regular 30 Hz sample (0.8 s). No procedural leg poses, geometry edits, or material edits.",
        "source_sample_rate_hz":args.sample_rate,"sampler_count":len(proof),
        "all_original_authored_samples_bit_preserved":True,"all_first_final_sample_blocks_bit_identical":True,
        "source_binary_prefix_sha256":sha(binary),"variant_preserved_binary_prefix_sha256":sha(verify_binary[:len(binary)]),
        "geometry_rig_material_definitions_unchanged":True,"source_file_unchanged":True,
        "source_last_key_s":max(p["previous_last_key_s"] for p in proof),
        "variant_last_key_s":max(p["appended_key_s"] for p in proof),
        "weights_tracks_in_this_asset":sum("weights" in p["paths"] for p in proof),
        "samplers":proof,
        "limitations":["Boundary pose equality prevents an instantaneous wrap in value, but interpolation over the added interval may still be fast.",
            "Quaternion component rates are not joint angular velocities; paw speeds require full hierarchy evaluation.",
            "The existing source front-right toe seam distance estimate is up to 8.84 cm at 70 cm body length; closing it in 1/30 s can imply roughly 265 cm/s endpoint displacement speed.",
            "The first-to-last pose match does not establish velocity continuity, ground contact, biological realism, or a corrected gait.",
            "Actual Unreal import, animation timing, and playback have not been verified."],
        "reproduce":"python scripts/close-walk-loop.py"}
    args.provenance.write_text(json.dumps(report,indent=2)+"\n")
    print(json.dumps({k:report[k] for k in ["source_sha256","variant_sha256","source_last_key_s","variant_last_key_s","sampler_count","all_original_authored_samples_bit_preserved","all_first_final_sample_blocks_bit_identical"]},indent=2))


if __name__ == "__main__":
    main()
