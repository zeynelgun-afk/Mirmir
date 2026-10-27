#!/usr/bin/env python3
"""Estimate in-place authored walk speed from glTF joint tracks, never edit motion.

Requires numpy. Run from any directory; --source and --output override defaults.
The contact points are distal bone proxies, not measured paw-pad surface contacts.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import numpy as np

ROOT = Path(__file__).resolve().parents[1]


def read_accessor(gltf, blobs, index):
    a = gltf["accessors"][index]
    view = gltf["bufferViews"][a["bufferView"]]
    dtype = {5120: "i1", 5121: "u1", 5122: "<i2", 5123: "<u2", 5125: "<u4", 5126: "<f4"}[a["componentType"]]
    columns = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT4": 16}[a["type"]]
    size = np.dtype(dtype).itemsize
    values = np.ndarray((a["count"], columns), dtype=dtype,
        buffer=blobs[view["buffer"]],
        offset=view.get("byteOffset", 0) + a.get("byteOffset", 0),
        strides=(view.get("byteStride", columns * size), size)).astype(np.float64)
    return values


def rotation_matrix(quaternion):
    x, y, z, w = quaternion / np.linalg.norm(quaternion)
    return np.array([[1-2*(y*y+z*z), 2*(x*y-z*w), 2*(x*z+y*w)],
        [2*(x*y+z*w), 1-2*(x*x+z*z), 2*(y*z-x*w)],
        [2*(x*z-y*w), 2*(y*z+x*w), 1-2*(x*x+y*y)]])


def interpolate(times, values, time, quaternion=False):
    upper = int(np.searchsorted(times, time, side="right"))
    if upper == 0:
        return values[0]
    if upper >= len(times):
        return values[-1]
    lower = upper - 1
    fraction = (time - times[lower]) / (times[upper] - times[lower])
    left, right = values[lower], values[upper]
    if not quaternion:
        return left * (1-fraction) + right * fraction
    dot = float(np.dot(left, right))
    if dot < 0:
        right, dot = -right, -dot
    if dot > 0.9995:
        result = left * (1-fraction) + right * fraction
        return result / np.linalg.norm(result)
    theta = np.arccos(np.clip(dot, -1, 1))
    return (left * np.sin((1-fraction)*theta) + right * np.sin(fraction*theta)) / np.sin(theta)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=ROOT / "assets/provenance/cat-walk-personal/original/scene.gltf")
    parser.add_argument("--output", type=Path, default=ROOT / "docs/walk-stride-analysis.json")
    parser.add_argument("--target-body-length-cm", type=float, default=70.0)
    parser.add_argument("--source-body-length", type=float, default=69.0270767211914,
        help="Full source model longitudinal AABB from the rendered asset review, including tail.")
    args = parser.parse_args()
    if args.target_body_length_cm <= 0 or args.source_body_length <= 0:
        parser.error("Body lengths must be positive.")
    gltf = json.loads(args.source.read_text())
    blobs = [(args.source.parent / b["uri"]).read_bytes() for b in gltf["buffers"]]
    nodes = gltf["nodes"]
    animation = gltf["animations"][0]
    tracks = []
    for channel in animation["channels"]:
        sampler = animation["samplers"][channel["sampler"]]
        if sampler.get("interpolation", "LINEAR") != "LINEAR":
            raise ValueError("Only source LINEAR tracks are supported; no motion is approximated.")
        path = channel["target"]["path"]
        if path not in ("translation", "rotation", "scale"):
            continue
        tracks.append((channel["target"]["node"], path,
            read_accessor(gltf, blobs, sampler["input"]).ravel(),
            read_accessor(gltf, blobs, sampler["output"])))
    times = np.unique(np.concatenate([track[2] for track in tracks]))
    parents = {child: parent for parent, n in enumerate(nodes) for child in n.get("children", [])}
    frames = []
    for time in times:
        overrides = {}
        for index, path, stamps, values in tracks:
            overrides[index, path] = interpolate(stamps, values, time, path == "rotation")
        cache = {}
        def world(index):
            if index in cache:
                return cache[index]
            n = nodes[index]
            if "matrix" in n:
                local = np.array(n["matrix"], dtype=np.float64).reshape(4, 4, order="F")
            else:
                local = np.eye(4)
                local[:3, 3] = overrides.get((index, "translation"), np.array(n.get("translation", [0, 0, 0])))
                quat = overrides.get((index, "rotation"), np.array(n.get("rotation", [0, 0, 0, 1])))
                scale = overrides.get((index, "scale"), np.array(n.get("scale", [1, 1, 1])))
                local[:3, :3] = rotation_matrix(quat) @ np.diag(scale)
            cache[index] = world(parents[index]) @ local if index in parents else local
            return cache[index]
        frames.append(np.array([world(i)[:3, 3] for i in range(len(nodes))]))
    positions = np.array(frames)
    def find(fragment):
        indices = [i for i, n in enumerate(nodes) if fragment in n.get("name", "")]
        if len(indices) != 1:
            raise ValueError(f"Expected exactly one {fragment!r}, found {indices}")
        return indices[0]
    head, pelvis = find("cat_bip-Head_"), find("cat_bip-Pelvis_")
    forward = positions[:, head].mean(axis=0) - positions[:, pelvis].mean(axis=0)
    forward[1] = 0.0  # glTF scene world is Y-up after all ancestor matrices.
    forward /= np.linalg.norm(forward)
    scale = args.target_body_length_cm / args.source_body_length
    paws = {
        "front_left": [find("cat_bip-L-Finger01_"), find("cat_bip-L-Finger11_")],
        "front_right": [find("cat_bip-R-Finger01_"), find("cat_bip-R-Finger11_")],
        "hind_left": [find("cat_bip-L-Toe01_")],
        "hind_right": [find("cat_bip-R-Toe01_")],
    }
    proximal = {"front_left": find("cat_bip-L-Hand_"), "front_right": find("cat_bip-R-Hand_"),
        "hind_left": find("cat_bip-L-Foot_"), "hind_right": find("cat_bip-R-Foot_")}
    results = {}
    pooled = []
    sensitivity = {fraction: [] for fraction in [0.10, 0.20, 0.30]}
    all_samples = {}
    for name, indices in paws.items():
        foot = positions[:, indices].mean(axis=1) * scale
        horizontal = foot @ forward
        height = foot[:, 1]
        velocity = np.gradient(horizontal, times)
        vertical_velocity = np.gradient(height, times)
        interior = np.ones(len(times), dtype=bool)
        interior[[0, -1]] = False  # no derivative across the clip's loop seam.
        height_span = float(np.ptp(height))
        for fraction in sensitivity:
            threshold = float(height.min() + max(0.35, fraction*height_span))
            mask = interior & (height <= threshold) & (velocity < -1.0)
            sensitivity[fraction].extend((-velocity[mask]).tolist())
            if fraction == 0.20:
                stance = mask
                window = threshold
        values = -velocity[stance]
        pooled.extend(values.tolist())
        joint = positions[:, proximal[name]] * scale
        joint_speed = np.gradient(joint @ forward, times)
        results[name] = {
            "distal_contact_proxy_bones": [nodes[i]["name"] for i in indices],
            "requested_wrist_or_hock_bone": nodes[proximal[name]]["name"],
            "height_range_cm": [float(height.min()), float(height.max())],
            "contact_height_max_cm": window,
            "contact_key_indices": np.flatnonzero(stance).tolist(),
            "contact_sample_count": int(len(values)),
            "stance_backward_speed_median_cm_s": float(np.median(values)) if len(values) else None,
            "stance_backward_speed_range_cm_s": [float(values.min()), float(values.max())] if len(values) else None,
            "same_phase_wrist_or_hock_backward_speed_median_cm_s": float(np.median(-joint_speed[stance])) if len(values) else None,
            "forward_excursion_cm": float(np.ptp(horizontal)),
            "loop_endpoint_contact_proxy_distance_cm": float(np.linalg.norm(foot[-1]-foot[0])),
        }
        all_samples[name] = [{"time_s": float(t), "forward_cm": float(s), "height_cm": float(h),
            "forward_velocity_cm_s": float(v), "vertical_velocity_cm_s": float(vh), "contact": bool(c)}
            for t,s,h,v,vh,c in zip(times,horizontal,height,velocity,vertical_velocity,stance)]
    if not pooled:
        raise ValueError("No low-height backward contact phase was identified.")
    speed = float(np.median(pooled))
    out = {
        "method": "World-space glTF TRS hierarchy evaluation of authored keys; constant forward actor speed cancels backward foot velocity during low-height stance proxies.",
        "source_file": str(args.source.relative_to(ROOT)) if args.source.is_relative_to(ROOT) else str(args.source),
        "source_gltf_sha256": hashlib.sha256(args.source.read_bytes()).hexdigest(),
        "source_binary_sha256": hashlib.sha256(blobs[0]).hexdigest(),
        "animation_name": animation.get("name"), "key_samples": int(len(times)),
        "duration_s": float(times[-1]-times[0]), "key_rate_hz": float(1/np.median(np.diff(times))),
        "target_full_model_body_length_cm": args.target_body_length_cm,
        "source_full_model_longitudinal_aabb_units": args.source_body_length,
        "scale_cm_per_source_unit": scale, "gltf_world_up_axis": "+Y",
        "forward_axis_world": forward.tolist(),
        "contact_rule": "Interior keys only; distal proxy height <= per-paw minimum + max(0.35 cm, 20% vertical excursion); backward horizontal velocity >1 cm/s. No sole-surface inference.",
        "per_paw": results,
        "pooled_contact_samples": len(pooled),
        "estimated_speed_median_cm_s": speed,
        "candidate_authored_walk_speed_cm_s": float(5*round(speed/5)),
        "stance_speed_percentiles_cm_s": {str(p): float(np.percentile(pooled,p)) for p in [10,25,50,75,90]},
        "contact_height_sensitivity_median_cm_s": {str(f): float(np.median(v)) if v else None for f,v in sensitivity.items()},
        "previous_guess_cm_s": 35.0,
        "previous_guess_ratio_to_estimated_speed": 35.0/speed,
        "stride_distance_at_estimated_speed_cm": speed*float(times[-1]-times[0]),
        "uncertainty": ["Bone tips approximate contact points, not paw pads or a solved contact plane.",
            "Different paws and contact-window definitions imply different speeds; the median is a calibration start, not a biomechanical guarantee.",
            "Scale depends on a 70 cm full longitudinal AABB including tail; recalibrate if import applies a different scale or orientation.",
            "No game-world floor contact, Unreal importer scale/root-motion handling, or actual Unreal playback has been verified.",
            "No claim that the walking complaint is fixed; verify side-on contact and scene displacement in Unreal."],
        "samples": all_samples,
    }
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps(out,indent=2)+"\n")
    print(json.dumps({k:out[k] for k in ["estimated_speed_median_cm_s","candidate_authored_walk_speed_cm_s","stance_speed_percentiles_cm_s","contact_height_sensitivity_median_cm_s"]},indent=2))
    print("Per-paw medians:", {k:round(v["stance_backward_speed_median_cm_s"],2) if v["stance_backward_speed_median_cm_s"] is not None else None for k,v in results.items()})
    print(args.output)


if __name__ == "__main__":
    main()
