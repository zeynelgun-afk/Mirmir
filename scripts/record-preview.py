#!/usr/bin/env python3
"""Record this application's own Unreal RGBA files; never capture the desktop.

The output uses a neutral preview background. Raw producer PNG/JSON bytes are
retained separately. No pointer files, other windows, microphone or audio are
read. A renderer session change aborts recording rather than joining producers.
"""
from __future__ import annotations

import argparse
import hashlib
import io
import json
import math
import os
from pathlib import Path
import shutil
import stat
import subprocess
import tempfile
import time
import zlib

from PIL import Image, ImageDraw, ImageFont

MAX_PNG = 16 * 1024 * 1024
CAPTION = "Actual Unreal Engine render • preview background"


def owned_bytes(path, maximum):
    descriptor = os.open(path, os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0) | getattr(os, "O_CLOEXEC", 0))
    with os.fdopen(descriptor, "rb") as source:
        details = os.fstat(source.fileno())
        if (not stat.S_ISREG(details.st_mode) or details.st_size > maximum or
                (hasattr(os, "getuid") and details.st_uid != os.getuid())):
            raise ValueError("Expected a small regular file owned by this user.")
        value = source.read(maximum + 1)
    if len(value) > maximum:
        raise ValueError("Input grew beyond its size limit.")
    return value


def identity(metadata):
    return (metadata.get("producer_pid"), metadata.get("session_id"), metadata.get("frame_seq"),
            metadata.get("props", {}).get("layout_revision"))


def png_frame(directory, metadata):
    name = metadata.get("frame_file")
    if not isinstance(name, str) or Path(name).name != name or not name.endswith(".png"):
        raise ValueError("Only PNG files in the supplied frame directory may be read.")
    payload = owned_bytes(directory / name, MAX_PNG)
    if len(payload) != metadata.get("png_bytes") or zlib.crc32(payload) != metadata.get("png_crc32"):
        raise ValueError("PNG/metadata CRC mismatch.")
    with Image.open(io.BytesIO(payload)) as image:
        if image.format != "PNG" or image.size != (metadata.get("width"), metadata.get("height")) or "A" not in image.getbands():
            raise ValueError("Expected the producer's correctly sized alpha PNG.")
        size = image.size
    offset = metadata.get("desktop_offset_px", [0, 0])
    if (not isinstance(offset, list) or len(offset) != 2 or
            any(isinstance(v, bool) or not isinstance(v, (int, float)) or not math.isfinite(v) or abs(v) > 100000 for v in offset)):
        raise ValueError("Invalid projection offset.")
    return {"bytes": payload, "size": list(size), "offset_px": offset}


def snapshot(directory, expected_pid=None, require_running=True):
    raw = owned_bytes(directory / "runtime.json", 65536)
    metadata = json.loads(raw)
    if (metadata.get("producer") != "unreal-scene-capture" or
            not isinstance(metadata.get("session_id"), str) or not metadata["session_id"] or
            not isinstance(metadata.get("producer_pid"), int) or isinstance(metadata["producer_pid"], bool) or
            metadata["producer_pid"] <= 0 or
            (expected_pid is not None and metadata["producer_pid"] != expected_pid)):
        raise ValueError("Expected the owned Unreal producer.")
    if require_running and (metadata.get("running") is not True or metadata.get("ready") is not True or
            time.time() - (directory / "runtime.json").stat().st_mtime > 3):
        raise ValueError("Waiting for a fresh, ready Unreal frame.")
    layers = []
    props = metadata.get("props", {})
    if props.get("schema_version") == 2:
        if props.get("ready") is not True:
            raise ValueError("Habitat items are not ready.")
        for role in ("bed", "bowl"):
            item = props.get("items", {}).get(role, {})
            if item.get("ready") is not True:
                raise ValueError("Missing habitat item: " + role)
            if item.get("layout_revision") != props.get("layout_revision"):
                raise ValueError("Habitat item is not paired to the current layout.")
            layers.append(dict(png_frame(directory, item), role=role))
    elif props.get("ready") is True:
        layers.append(dict(png_frame(directory, props), role="habitat"))
    layers.append(dict(png_frame(directory, metadata), role="main"))
    mask = metadata.get("input_mask")
    if mask is not None:
        if mask.get("frame_seq") != metadata.get("frame_seq"):
            raise ValueError("Visible-cat mask is not paired to this main frame.")
        layers.append(dict(png_frame(directory, mask), role="input-mask"))
    after = json.loads(owned_bytes(directory / "runtime.json", 65536))
    if identity(after) != identity(metadata):
        raise ValueError("Frame or layout changed during capture; retrying.")
    return metadata, raw, layers


def save_snapshot(output, index, metadata, raw, layers, elapsed):
    row = {"elapsed_s": elapsed, "frame_seq": metadata["frame_seq"],
           "layout_revision": metadata.get("props", {}).get("layout_revision"),
           "activity": metadata.get("needs", {}).get("activity"),
           "appearance": metadata.get("appearance"),
           "visible_cat_pixels": metadata.get("input_mask", {}).get("visible_cat_pixels", 0),
           "occluded_cat_pixels": metadata.get("input_mask", {}).get("occluded_cat_pixels", 0),
           "actor_forward_world": metadata.get("motion", {}).get("actor_forward_world"),
           "rest_pose_alpha": metadata.get("props", {}).get("rest_pose_alpha", 0),
           "eating_pose_strength": metadata.get("props", {}).get("eating_pose_strength", 0), "layers": []}
    json_name = f"raw/runtime-{index:05d}.json"
    (output / json_name).write_bytes(raw)
    row["runtime_file"] = json_name
    for layer in layers:
        digest = hashlib.sha256(layer["bytes"]).hexdigest()
        name = "raw/" + digest + ".png"
        if not (output / name).exists():
            (output / name).write_bytes(layer["bytes"])
        row["layers"].append({key: layer[key] for key in ("role", "size", "offset_px")} |
                             {"file": name, "sha256": digest})
    return row


def record(directory, output, seconds, fps, expected_pid=None):
    output.mkdir(parents=True, exist_ok=False)
    (output / "raw").mkdir()
    started, deadline = time.monotonic(), time.monotonic() + seconds
    next_sample, session, rows, last, last_good = started, None, [], None, started
    while time.monotonic() < deadline:
        now = time.monotonic()
        if now < next_sample:
            time.sleep(min(.02, next_sample - now)); continue
        next_sample += 1 / fps
        try:
            metadata, raw, layers = snapshot(directory, expected_pid)
            producer = identity(metadata)[:2]
            if session is not None and producer != session:
                raise RuntimeError("Renderer session changed; recording stopped.")
            session, last, last_good = producer, (metadata, raw, layers), now
        except (OSError, ValueError):
            if last is None or now - last_good > 3:
                continue
            metadata, raw, layers = last
        rows.append(save_snapshot(output, len(rows), metadata, raw, layers, now - started))
    if not rows:
        raise RuntimeError("No matched Unreal frames were recorded.")
    manifest = {"schema_version": 1, "kind": "owned-unreal-rgba-preview", "desktop_screenshot": False,
                "audio_recorded": False, "neutral_background_added": True,
                "producer_pid": session[0], "session_id": session[1], "fps": fps,
                "duration_s": len(rows) / fps, "frames": rows,
                "raw_sources_modified": False, "caption": CAPTION}
    (output / "recording.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n")
    return manifest


def font(size):
    for name in ("/usr/share/fonts/TTF/DejaVuSans.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"):
        if Path(name).is_file():
            return ImageFont.truetype(name, size)
    return ImageFont.load_default(size=size)


def union_bounds(rows):
    rectangles = [(layer["offset_px"][0], layer["offset_px"][1],
                   layer["offset_px"][0] + layer["size"][0], layer["offset_px"][1] + layer["size"][1])
                  for row in rows for layer in row["layers"] if layer["role"] != "input-mask"]
    return [math.floor(min(x[0] for x in rectangles)), math.floor(min(x[1] for x in rectangles)),
            math.ceil(max(x[2] for x in rectangles)), math.ceil(max(x[3] for x in rectangles))]


def recorded_path(output, name):
    path = Path(name)
    if path.is_absolute() or len(path.parts) != 2 or path.parts[0] != "raw" or Path(path.parts[1]).name != path.parts[1]:
        raise ValueError("Recorded image path must stay inside raw/.")
    return output / path


def compose(output, manifest):
    rows = manifest["frames"]
    left, top, right, bottom = bounds = union_bounds(rows)
    width, height = right - left, bottom - top
    if width <= 0 or height <= 0 or width > 10000 or height > 10000:
        raise ValueError("Unreasonable recorded scene extent.")
    scale = min(912 / width, 454 / height)
    origin = ((960 - width * scale) / 2, 24 + (454 - height * scale) / 2)
    frames = output / "rendered"; frames.mkdir(exist_ok=True)
    for index, row in enumerate(rows):
        # Bed/bowl below the already depth-composited actual main scene.
        canvas = Image.new("RGBA", (960, 540), (24, 29, 37, 255))
        for layer in row["layers"]:
            if layer["role"] == "input-mask":
                continue
            payload = owned_bytes(recorded_path(output, layer["file"]), MAX_PNG)
            if hashlib.sha256(payload).hexdigest() != layer["sha256"]:
                raise ValueError("Recorded source PNG changed.")
            with Image.open(io.BytesIO(payload)) as image:
                size = (max(1, round(image.width * scale)), max(1, round(image.height * scale)))
                pixel = (round(origin[0] + (layer["offset_px"][0] - left) * scale),
                         round(origin[1] + (layer["offset_px"][1] - top) * scale))
                canvas.alpha_composite(image.convert("RGBA").resize(size, Image.Resampling.LANCZOS), pixel)
        ImageDraw.Draw(canvas).text((24, 506), CAPTION, font=font(19), fill=(215, 222, 232, 255))
        canvas.convert("RGB").save(frames / f"frame-{index:05d}.png")
    selected = []
    appearances = [name for name in ("van", "british", "tuxedo") if any(row.get("appearance") == name for row in rows)]
    def portrait_score(row):
        visible, hidden = row.get("visible_cat_pixels", 0), row.get("occluded_cat_pixels", 0)
        direction = row.get("actor_forward_world")
        # The current game camera's horizontal offset is (210,-250). This only
        # selects an actual frame; it never changes a recorded pose or camera.
        facing = direction[0] * .643 - direction[1] * .766 if isinstance(direction, list) and len(direction) >= 2 else 0
        return visible / max(1, visible + hidden) * 4 + facing
    if len(appearances) >= 3:
        for appearance in appearances[:3]:
            matches = [index for index, row in enumerate(rows) if row.get("appearance") == appearance]
            selected.append(max(matches, key=lambda index: portrait_score(rows[index])))
    for activity in (() if len(selected) == 3 else ("Play", "Eat", "Sleep", "Wander")):
        matches = [index for index, row in enumerate(rows) if row.get("activity") == activity and
                   (activity != "Sleep" or row.get("rest_pose_alpha", 0) > .75) and
                   (activity != "Eat" or row.get("eating_pose_strength", 0) > .25)]
        if matches:
            selected.append(matches[len(matches) // 2])
        if len(selected) == 3:
            break
    for index in (0, len(rows) // 2, len(rows) - 1):
        if len(selected) == 3: break
        if index not in selected: selected.append(index)
    stills = output / "stills"; stills.mkdir(exist_ok=True)
    for number, index in enumerate(selected, 1):
        shutil.copy2(frames / f"frame-{index:05d}.png", stills / f"preview-{number}.png")
        main = next(layer for layer in rows[index]["layers"] if layer["role"] == "main")
        shutil.copy2(recorded_path(output, main["file"]), stills / f"raw-main-{number}.png")
    shutil.copy2(frames / f"frame-{selected[0]:05d}.png", output / "poster.png")
    manifest["render"] = {"canvas": [960, 540], "union_rect_px": bounds, "constant_scale": scale,
                          "background_rgb": [24, 29, 37], "still_indices": selected}


def encode(output, manifest):
    ffmpeg = shutil.which("ffmpeg")
    if not ffmpeg: raise RuntimeError("ffmpeg is required to encode the preview.")
    def run(*arguments):
        subprocess.run([ffmpeg, "-hide_banner", "-loglevel", "error", "-filter_complex_threads", "2", "-y", *map(str, arguments)], check=True)
    run("-framerate", manifest["fps"], "-i", output / "rendered/frame-%05d.png", "-c:v", "libx264",
        "-threads", "2", "-crf", "23", "-pix_fmt", "yuv420p", "-movflags", "+faststart", output / "preview.mp4")
    for size, fps, colors in ((640, 8, 96), (560, 6, 64)):
        filters = (f"fps={fps},scale={size}:-1:flags=lanczos,split[a][b];"
                   f"[a]palettegen=max_colors={colors}[p];[b][p]paletteuse=dither=bayer:bayer_scale=3")
        run("-i", output / "preview.mp4", "-filter_complex", filters, "-loop", "0", output / "preview.gif")
        if (output / "preview.gif").stat().st_size <= 8 * 1024 * 1024: break
    manifest["media"] = {name: {"bytes": (output / name).stat().st_size,
        "sha256": hashlib.sha256((output / name).read_bytes()).hexdigest()}
        for name in ("preview.mp4", "preview.gif", "poster.png")}


def self_test():
    with tempfile.TemporaryDirectory(prefix="mirmir-media-test-") as temporary:
        root = Path(temporary); output = root / "recorded"; (output / "raw").mkdir(parents=True)
        image = Image.new("RGBA", (8, 8), (130, 150, 180, 255)); image.putpixel((0, 0), (0, 0, 0, 0))
        image.save(root / "frame.png"); png = (root / "frame.png").read_bytes()
        metadata = {"producer": "unreal-scene-capture", "producer_pid": 123, "session_id": "test-only",
            "frame_seq": 1, "running": True, "ready": True, "frame_file": "frame.png", "width": 8, "height": 8,
            "png_bytes": len(png), "png_crc32": zlib.crc32(png), "desktop_offset_px": [4, -2]}
        (root / "runtime.json").write_text(json.dumps(metadata))
        captured, raw, layers = snapshot(root, 123)
        row = save_snapshot(output, 0, captured, raw, layers, 0)
        assert union_bounds([row]) == [4, -2, 12, 6]
        assert (output / row["layers"][0]["file"]).read_bytes() == png
        bad = dict(metadata, png_crc32=0)
        try: png_frame(root, bad)
        except ValueError: pass
        else: raise AssertionError("Mismatched PNG accepted")
        try: png_frame(root, dict(metadata, frame_file="../outside.png"))
        except ValueError: pass
        else: raise AssertionError("Foreign path accepted")
        compose(output, {"frames": [row]})
        assert Image.open(output / "poster.png").size == (960, 540)
    print("PASS: owned-frame CRC/path gate, unchanged raw bytes, fixed projection union and preview composition; synthetic test only.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--frame-dir", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--seconds", type=float, default=20)
    parser.add_argument("--fps", type=int, default=10)
    parser.add_argument("--producer-pid", type=int)
    parser.add_argument("--render-only", action="store_true")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test: self_test(); return
    if args.output is None: parser.error("--output is required")
    if args.render_only:
        manifest = json.loads((args.output / "recording.json").read_text())
    else:
        if args.frame_dir is None: parser.error("--frame-dir is required")
        if not math.isfinite(args.seconds) or not 1 <= args.seconds <= 120 or not 1 <= args.fps <= 20:
            parser.error("Use 1–120 seconds and 1–20 fps.")
        manifest = record(args.frame_dir.resolve(), args.output.resolve(), args.seconds, args.fps, args.producer_pid)
    compose(args.output, manifest); encode(args.output, manifest)
    (args.output / "recording.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n")
    print(json.dumps({"output": str(args.output), "frames": len(manifest["frames"]),
        "duration_s": manifest["duration_s"], "media": manifest["media"], "desktop_screenshot": False}, indent=2))


if __name__ == "__main__": main()
