#!/usr/bin/env python3
"""Run only the installed Mirmir Flatpak in an isolated app-owned test namespace."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import time
import uuid
import zlib

APP_ID = "io.github.zeynelgun_afk.Mirmir"
ROOT = Path(__file__).resolve().parents[2]


def read_json(path):
    try:
        return json.loads(path.read_text())
    except (OSError, ValueError):
        return {}


def wait_for(callback, seconds, process=None):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        result = callback()
        if result:
            return result
        if process is not None and process.poll() is not None:
            raise RuntimeError("Owned Flatpak exited: " + str(process.returncode))
        time.sleep(0.1)
    raise TimeoutError("Mirmir sandbox check timed out.")


def locked_session():
    session = os.environ.get("XDG_SESSION_ID")
    if not session:
        return None
    try:
        check = subprocess.run(["loginctl", "show-session", session, "--property=LockedHint", "--value"],
                               capture_output=True, text=True, timeout=3)
        value = check.stdout.strip()
        return value == "yes" if check.returncode == 0 and value in {"yes", "no"} else None
    except (OSError, subprocess.TimeoutExpired):
        return None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT / ".local/flatpak-verification")
    parser.add_argument("--allow-locked-session", action="store_true",
                        help="Run runtime/IPC checks while locked, explicitly leaving desktop visibility unverified.")
    parser.add_argument("--require-props", action="store_true", help="Require the real 0.4 bed/bowl frame and native habitat state.")
    args = parser.parse_args()
    token = uuid.uuid4().hex
    output = args.output.resolve() / token; output.mkdir(parents=True)
    private = Path.home() / ".var/app" / APP_ID
    data = private / "data/verification" / token
    state = private / ".local/state/verification" / token
    # Flatpak reserves/reinitializes the XDG base variables. The upstream
    # launcher applies this validated token after sandbox initialization.
    environment = ["--env=MIRMIR_VERIFY_NAMESPACE=" + token]
    pointer = state / "mirmir/unreal/frame-location.json"
    result = {"app_id": APP_ID, "isolated_data": str(data), "isolated_state": str(state), "test_passed": False,
              "actual_pointer_input_tested": False, "speaker_output_tested": False,
              "actual_desktop_overlay_verified": False, "runtime_and_ipc_passed": False,
              "desktop_session_locked": locked_session(), "unverified": []}
    process = None
    with (output / "flatpak.log").open("w") as log, (output / "instance-id").open("w+") as instance:
        try:
            process = subprocess.Popen(["flatpak", "run", "--user", "--die-with-parent", "--instance-id-fd=" + str(instance.fileno()),
                *environment, APP_ID, "--no-cursor-interest"], stdout=log, stderr=subprocess.STDOUT, pass_fds=(instance.fileno(),))
            location = wait_for(lambda: read_json(pointer), 75, process)
            frames = Path(location["frames_directory"]).resolve()
            allowed_runtime = Path(os.environ.get("XDG_RUNTIME_DIR", "/run/user/" + str(os.getuid()))) / "app" / APP_ID
            if (location.get("data_directory") != str(data / "mirmir/unreal") or location.get("state_directory") != str(state / "mirmir/unreal") or
                    not frames.is_relative_to(allowed_runtime.resolve())):
                raise RuntimeError("Flatpak output did not stay in its app-owned namespace.")
            result["frames"] = str(frames)

            def matched():
                runtime, shell = read_json(frames / "runtime.json"), read_json(frames / "shell-status.json")
                if not (runtime.get("running") is True and runtime.get("ready") is True and shell.get("ready") is True and shell.get("engine_kind") == "shipping" and
                        runtime.get("session_id") == shell.get("session_id") and isinstance(runtime.get("producer_pid"), int) and runtime["producer_pid"] > 0):
                    return None
                if args.require_props and not (runtime.get("props", {}).get("ready") is True and shell.get("props_ready") is True):
                    return None
                try:
                    name = runtime.get("frame_file", "frame.png")
                    if not isinstance(name, str) or Path(name).name != name:
                        raise RuntimeError("Unexpected frame path.")
                    png = (frames / name).read_bytes()
                except OSError:
                    return None
                if len(png) == runtime.get("png_bytes") and zlib.crc32(png) == runtime.get("png_crc32"):
                    return runtime, shell, png

            runtime, shell, png = wait_for(matched, 75, process)
            from PyQt6.QtGui import QImage
            image = QImage.fromData(png, "PNG").convertToFormat(QImage.Format.Format_RGBA8888)
            if image.isNull() or not image.hasAlphaChannel():
                raise RuntimeError("No real RGBA frame decoded.")
            raw = image.constBits().asstring(image.sizeInBytes())
            alpha = raw[3::4]
            if min(alpha) != 0 or max(alpha) != 255 or image.width() != runtime["width"] or image.height() != runtime["height"]:
                raise RuntimeError("Unexpected alpha or frame dimensions.")
            (output / "frame.png").write_bytes(png)  # Raw producer bytes, unchanged.
            (output / "runtime.json").write_text(json.dumps(runtime, indent=2))
            (output / "shell-status.json").write_text(json.dumps(shell, indent=2))
            result.update(session_id=runtime["session_id"], producer_pid_in_sandbox=runtime["producer_pid"],
                          raw_frame_sha256=hashlib.sha256(png).hexdigest(), alpha_range=[min(alpha), max(alpha)],
                          frame_size=[image.width(), image.height()], engine_kind=shell["engine_kind"], frame_storage=location.get("storage"))
            if args.require_props:
                props = runtime["props"]
                name = props.get("frame_file")
                if not isinstance(name, str) or Path(name).name != name:
                    raise RuntimeError("Unexpected habitat frame path.")
                props_png = (frames / name).read_bytes()
                if len(props_png) != props.get("png_bytes") or zlib.crc32(props_png) != props.get("png_crc32"):
                    raise RuntimeError("Habitat CRC did not match its published bytes.")
                props_image = QImage.fromData(props_png, "PNG").convertToFormat(QImage.Format.Format_RGBA8888)
                if props_image.isNull() or not props_image.hasAlphaChannel():
                    raise RuntimeError("Habitat is not RGBA.")
                props_alpha = props_image.constBits().asstring(props_image.sizeInBytes())[3::4]
                if min(props_alpha) != 0 or max(props_alpha) != 255:
                    raise RuntimeError("Habitat lacks transparent/opaque coverage.")
                (output / "props.png").write_bytes(props_png)
                result["habitat"] = {"crc_matched": True, "alpha_range": [min(props_alpha), max(props_alpha)],
                    "size": [props_image.width(), props_image.height()], "native_ready": shell["props_ready"],
                    "native_visible_intent": shell.get("props_visible"), "frame_seq": props["frame_seq"],
                    "raw_sha256": hashlib.sha256(props_png).hexdigest()}
            try:
                from Xlib import display
                from Xlib.ext import shape
                connection = display.Display()
                try:
                    own_window = connection.create_resource_object("window", int(shell["window_id"]))
                    def mapped_shape():
                        attributes = own_window.get_attributes()
                        bounds = own_window.shape_get_rectangles(shape.SK.Bounding).rectangles
                        # QWidget.show() and the initial status write can
                        # precede the compositor/X11 map acknowledgement.
                        return (attributes, bounds) if attributes.map_state == 2 and len(bounds) >= 2 else None
                    if result["desktop_session_locked"] is True and args.allow_locked_session:
                        attributes = own_window.get_attributes()
                        bounds = own_window.shape_get_rectangles(shape.SK.Bounding).rectangles
                    else:
                        attributes, bounds = wait_for(mapped_shape, 5, process)
                    result["x11_owned_window"] = {"mapped": attributes.map_state == 2,
                        "depth": own_window.get_geometry().depth, "bounding_shape_rectangles": len(bounds)}
                    if attributes.map_state != 2 and result["desktop_session_locked"] is True and args.allow_locked_session:
                        result["unverified"].append("Actual desktop mapping/visibility: user's session is locked; no unlock or compositor changes attempted.")
                    elif attributes.map_state != 2 or len(bounds) < 2:
                        raise RuntimeError("Owned desktop overlay is not mapped with a cat-shaped region.")
                    else:
                        result["actual_desktop_overlay_verified"] = True
                finally:
                    connection.close()
            except ImportError:
                result["x11_owned_window"] = {"verified": False, "reason": "Host Python Xlib unavailable."}

            commands = data / "mirmir/unreal/commands"; commands.mkdir(parents=True, exist_ok=True)
            def command(payload):
                name = uuid.uuid4().hex
                temporary = commands / (name + ".tmp")
                temporary.write_text(json.dumps(payload)); temporary.replace(commands / (name + ".json"))
            before_bond = runtime.get("needs", {}).get("bond", 0)
            command({"command": "pet", "stroke_normalized": [0.5, -0.2]})
            engaged = wait_for(lambda: (value if (value := read_json(frames / "runtime.json")).get("needs", {}).get("bond", 0) > before_bond else None), 8, process)
            result["pet_ipc_bond_increased"] = engaged["needs"]["bond"] > before_bond
            if args.require_props:
                # User regression: explicit Feed/Play must work immediately
                # after petting, and petting must not cancel the physical game.
                def accepted(action):
                    value = read_json(frames / "runtime.json")
                    acknowledgement = value.get("interaction", {})
                    return value if acknowledgement.get("action") == action and acknowledgement.get("accepted") is True else None
                command({"command": "feed"})
                fed = wait_for(lambda: accepted("feed"), 8, process)
                result["feed_after_pet_acknowledged"] = fed["interaction"]["accepted"]
                command({"command": "play"})
                played = wait_for(lambda: (value if (value := accepted("play")) and value.get("toy", {}).get("active") is True else None), 8, process)
                result["play_after_feed_acknowledged"] = played["interaction"]["accepted"]
                time.sleep(1.0)
                command({"command": "pet", "stroke_normalized": [-0.4, -0.2]})
                deadline = time.monotonic() + 2.0
                samples = 0
                while time.monotonic() < deadline:
                    value = read_json(frames / "runtime.json")
                    if value.get("needs", {}).get("activity") != "Play" or value.get("toy", {}).get("active") is not True:
                        raise RuntimeError("Play was interrupted during the post-pet observation window.")
                    samples += 1
                    time.sleep(.1)
                result["play_survived_petting"] = True
                result["post_pet_play_samples"] = samples
            command({"command": "rest"})
            wait_for(lambda: read_json(frames / "runtime.json").get("needs", {}).get("activity") == "Sleep", 8, process)
            result["rest_ipc_observed"] = True
            subprocess.run(["flatpak", "run", "--user", *environment, APP_ID, "--stop"], check=True, stdout=log, stderr=subprocess.STDOUT, timeout=20)
            process.wait(timeout=20)
            result["normal_stop_exit_code"] = process.returncode
            result["runtime_and_ipc_passed"] = process.returncode == 0
            result["test_scope"] = "sandbox runtime/IPC/normal stop; desktop visibility reported separately"
            result["test_passed"] = process.returncode == 0
        except Exception as failure:
            result["error"] = str(failure)
        finally:
            if process is not None and process.poll() is None:
                try:
                    subprocess.run(["flatpak", "run", "--user", *environment, APP_ID, "--stop"], stdout=log, stderr=subprocess.STDOUT, timeout=18)
                    process.wait(timeout=8)
                except (subprocess.TimeoutExpired, OSError) as failure:
                    instance.seek(0); owned_id = instance.read().strip()
                    try:
                        if owned_id.isdigit():
                            subprocess.run(["flatpak", "kill", owned_id], stdout=log, stderr=subprocess.STDOUT, timeout=10)
                        process.wait(timeout=10)
                    except (subprocess.TimeoutExpired, OSError) as cleanup_failure:
                        result["cleanup_error"] = str(cleanup_failure)
                        process.terminate()  # Only this Popen child; --die-with-parent is enabled.
                        try:
                            process.wait(timeout=5)
                        except subprocess.TimeoutExpired:
                            process.kill(); process.wait(timeout=5)
            (output / "result.json").write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n")
    print(json.dumps(result, ensure_ascii=False, indent=2))
    print("Artifacts: " + str(output))
    return 0 if result["test_passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
