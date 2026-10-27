#!/usr/bin/env python3
"""Verify the real Godot desktop lab against an owned X11 underlay.

Runs in the user's current graphical session through XWayland, without changing
KWin configuration. Sends exactly two pointer clicks, after checking their live
recipient: one to our underlay and one to our Godot child. User data/state are
isolated in a temporary XDG tree; only the app's own viewport is captured.
Dependencies: python-xlib and Pillow. No screenshot of other applications.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time
from datetime import datetime, timezone

from PIL import Image
from Xlib import X, display
from Xlib.ext import shape, xtest


PROJECT = Path(__file__).resolve().parents[1]
BUTTON_MASK = X.Button1Mask | X.Button2Mask | X.Button3Mask | X.Button4Mask | X.Button5Mask


def wait_for(predicate, description: str, timeout: float = 12):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        value = predicate()
        if value:
            return value
        time.sleep(0.05)
    raise RuntimeError(f"Timed out: {description}")


def read_json(path: Path):
    try:
        return json.loads(path.read_text())
    except (OSError, json.JSONDecodeError):
        return None


def find_window(connection, process_id: int):
    atom = connection.intern_atom("_NET_WM_PID")

    def visit(window, depth=0):
        if depth > 5:
            return None
        try:
            prop = window.get_full_property(atom, X.AnyPropertyType)
            if prop is not None and int(prop.value[0]) == process_id:
                if window.get_attributes().map_state == X.IsViewable:
                    return window
            for child in window.query_tree().children:
                found = visit(child, depth + 1)
                if found is not None:
                    return found
        except Exception:
            return None
        return None

    return visit(connection.screen().root)


def window_id(value):
    return value.id if hasattr(value, "id") else int(value)


def run_verification(output_dir: Path, attempt_pointer: bool = False):
    output_dir.mkdir(parents=True, exist_ok=True)
    result = {
        "timestamp_utc": datetime.now(timezone.utc).isoformat(),
        "implementation": "godot-desktop-lab",
        "verification_kind": "automated-live-XWayland-with-owned-underlay",
        "manual_user_validation": False,
        "unreal_validation": False,
        "checks": {},
        "errors": [],
        "unverified": [],
    }
    for relative in ["desktop-lab/project.godot", "desktop-lab/scripts/main.gd", "desktop-lab/scripts/cat_visual.gd"]:
        result.setdefault("source_sha256", {})[relative] = hashlib.sha256((PROJECT / relative).read_bytes()).hexdigest()

    connection = display.Display()
    root = connection.screen().root
    original_pointer = root.query_pointer()
    original_focus = connection.get_input_focus().focus
    original_focus_id = window_id(original_focus)
    result["pointer_before"] = [original_pointer.root_x, original_pointer.root_y]
    result["environment"] = {
        "session": os.environ.get("XDG_SESSION_TYPE"),
        "desktop": os.environ.get("XDG_CURRENT_DESKTOP"),
        "x11_root_pixels": [root.get_geometry().width, root.get_geometry().height],
    }
    process = None
    underlay = None
    button_down = False
    pointer_touched = False
    log_file = None
    temporary = tempfile.TemporaryDirectory(prefix="mirmir-desktop-verification-")
    temporary_root = Path(temporary.name)
    command_index = 0
    underlay_clicks = 0
    app_data = temporary_root / "data" / "mirmir"
    command_dir = app_data / "commands"
    status_path = app_data / "runtime-status.json"

    def command(name: str):
        nonlocal command_index
        command_index += 1
        command_dir.mkdir(parents=True, exist_ok=True)
        pending = command_dir / f"verify-{command_index:04d}.json"
        staging = pending.with_suffix(".tmp")
        staging.write_text(json.dumps({"command": name}))
        staging.replace(pending)
        wait_for(lambda: not pending.exists(), f"IPC command {name}", 5)

    def collect_underlay_clicks():
        nonlocal underlay_clicks
        connection.sync()
        while connection.pending_events():
            event = connection.next_event()
            if event.type == X.ButtonPress and event.window.id == underlay.id:
                underlay_clicks += 1
        return underlay_clicks

    try:
        if original_pointer.mask & BUTTON_MASK:
            raise RuntimeError("A pointer button is held; verification will not interfere with a drag.")
        if not connection.has_extension("SHAPE") or not connection.has_extension("XTEST"):
            raise RuntimeError("X11 SHAPE and XTEST extensions are required.")
        executable = shutil.which("godot") or shutil.which("godot4")
        if not executable:
            raise RuntimeError("Godot executable not found.")
        capture_path = output_dir / "viewport.png"
        capture_path.unlink(missing_ok=True)
        env = os.environ.copy()
        env["XDG_DATA_HOME"] = str(temporary_root / "data")
        env["XDG_STATE_HOME"] = str(temporary_root / "state")
        # A dedicated dummy audio backend keeps this UI verification quiet.
        invocation = [
            executable, "--path", str(PROJECT / "desktop-lab"),
            "--display-driver", "x11", "--rendering-method", "gl_compatibility",
            "--audio-driver", "Dummy", "--", "--test-mode", "--capture", str(capture_path),
        ]
        result["invocation"] = invocation
        log_file = (output_dir / "godot.log").open("w")
        process = subprocess.Popen(invocation, env=env, stdout=log_file, stderr=subprocess.STDOUT)
        window = wait_for(lambda: find_window(connection, process.pid), "mapped Godot child window", 20)
        wait_for(lambda: capture_path.exists() and read_json(status_path), "own viewport and runtime status", 20)
        status = read_json(status_path)
        result["initial_status"] = status
        save_path = app_data / "cat-state.json"
        result["checks"]["isolated_state"] = status_path.exists() and not save_path.exists()
        command("mute")
        command("rest")  # Prevent autonomous movement while pointer coordinates are checked.
        # State actions install a conservative fallback before the next alpha
        # sample. Allow that scheduled refresh to finish before inspecting it.
        time.sleep(1.6)

        geometry = window.get_geometry()
        # python-xlib translate_coords is invoked on the destination window.
        translated = root.translate_coords(window, 0, 0)
        x, y = translated.x, translated.y
        width, height = geometry.width, geometry.height
        attributes = window.get_attributes()
        rectangles = [dict(x=r.x, y=r.y, width=r.width, height=r.height) for r in window.shape_get_rectangles(shape.SK.Input).rectangles]
        result["window"] = {
            "id": window.id, "physical_position": [x, y], "size": [width, height],
            "depth": geometry.depth, "override_redirect": bool(attributes.override_redirect),
            "input_rectangles": rectangles,
        }
        result["checks"]["argb_window"] = geometry.depth == 32
        result["checks"]["no_focus_overlay"] = bool(attributes.override_redirect)
        with Image.open(capture_path) as image:
            rgba = image.convert("RGBA")
            alpha = rgba.getchannel("A")
            corners = [rgba.getpixel((a, b))[3] for a, b in [(0, 0), (width - 1, 0), (0, height - 1), (width - 1, height - 1)]]
            transparent_count = alpha.histogram()[0]
            result["viewport"] = {
                "size": list(image.size), "corner_alpha": corners,
                "fully_transparent_fraction": transparent_count / (width * height),
                "sha256": hashlib.sha256(capture_path.read_bytes()).hexdigest(),
            }
            result["checks"]["transparent_corners"] = corners == [0, 0, 0, 0]
            result["checks"]["visible_render_content"] = alpha.getextrema() == (0, 255)

        def in_shape(point):
            px, py = point
            return any(r["x"] <= px < r["x"] + r["width"] and r["y"] <= py < r["y"] + r["height"] for r in rectangles)

        outside, inside = (25, 25), (440, 350)
        result["checks"]["shape_excludes_blank_corner"] = not in_shape(outside)
        result["checks"]["shape_includes_cat_body"] = in_shape(inside)
        if in_shape(outside) or not in_shape(inside):
            raise RuntimeError("Expected outside/inside test points do not match the live input shape.")
        if not (0 <= x and 0 <= y and x + width <= root.get_geometry().width and y + height <= root.get_geometry().height):
            raise RuntimeError("Overlay is outside the screen; refusing pointer injection.")
        underlay = root.create_window(
            x, y, width, height, 0, connection.screen().root_depth,
            X.InputOutput, X.CopyFromParent, background_pixel=0x27343B,
            event_mask=X.ButtonPressMask | X.ButtonReleaseMask | X.ExposureMask,
            override_redirect=True,
        )
        underlay.set_wm_name("Mirmir owned verification underlay")
        underlay.set_wm_class("mirmir-verification", "MirmirVerification")
        underlay.map()
        underlay.configure(sibling=window, stack_mode=X.Below)
        connection.sync()
        time.sleep(0.10)
        ug = underlay.get_geometry()
        ut = root.translate_coords(underlay, 0, 0)
        result["underlay"] = {
            "id": underlay.id, "physical_position": [ut.x, ut.y],
            "size": [ug.width, ug.height], "map_state": underlay.get_attributes().map_state,
            "kind": "raw-X11-physical-pixels-no-Qt-scaling",
        }

        def checked_click(point, expected_recipient):
            nonlocal button_down, pointer_touched
            px, py = x + point[0], y + point[1]
            pointer_touched = True
            xtest.fake_input(connection, X.MotionNotify, x=px, y=py)
            connection.sync()
            time.sleep(0.08)
            pointer = root.query_pointer()
            actual = window_id(pointer.child)
            result.setdefault("pointer_diagnostics", []).append({
                "requested_root": [px, py], "reported_root": [pointer.root_x, pointer.root_y],
                "expected_recipient": expected_recipient, "reported_recipient": actual,
            })
            if actual != expected_recipient or (pointer.root_x, pointer.root_y) != (px, py):
                raise RuntimeError(f"Click safety check failed: intended owned window {expected_recipient}, live recipient {actual}.")
            xtest.fake_input(connection, X.ButtonPress, detail=1)
            button_down = True
            xtest.fake_input(connection, X.ButtonRelease, detail=1)
            button_down = False
            connection.sync()
            time.sleep(0.18)
            return collect_underlay_clicks()

        try:
            if not attempt_pointer:
                raise RuntimeError("Pointer injection disabled by --no-input; compositor click routing remains unverified.")
            outside_count = checked_click(outside, underlay.id)
            result["checks"]["outside_click_reaches_underlay"] = outside_count == 1
            command("menu")
            outside_status = read_json(status_path)
            command("menu")
            result["checks"]["outside_click_does_not_pet"] = outside_status.get("total_pets") == 0
            inside_count = checked_click(inside, window.id)
            result["checks"]["inside_click_blocked_from_underlay"] = inside_count == outside_count
            command("menu")
            inside_status = read_json(status_path)
            result["checks"]["inside_click_pets_cat"] = inside_status.get("total_pets") == 1 and inside_status.get("activity") == "pet"
            command("menu")
            result["click_evidence"] = {
                "outside_local": list(outside), "inside_local": list(inside),
                "underlay_after_outside": outside_count, "underlay_after_inside": inside_count,
                "pets_after_outside": outside_status.get("total_pets"),
                "pets_after_inside": inside_status.get("total_pets"),
                "activity_after_inside": inside_status.get("activity"),
            }
            result["checks"]["actual_pointer_routing"] = all(result["checks"].get(key, False) for key in ["outside_click_reaches_underlay", "inside_click_blocked_from_underlay", "inside_click_pets_cat"])
        except RuntimeError as error:
            # Never substitute SendEvent: it would bypass compositor hit testing.
            if attempt_pointer:
                result["checks"]["actual_pointer_routing"] = False
            result["unverified"].append(str(error))
        underlay.destroy()
        underlay = None
        connection.sync()

        # These prove independent state transitions, not actual pointer routing.
        previous_pets = read_json(status_path).get("total_pets", 0)
        command("pet")
        pet_status = wait_for(lambda: (data if (data := read_json(status_path)) and data.get("total_pets") == previous_pets + 1 else None), "IPC pet status", 3)
        result["checks"]["ipc_pet_updates_state"] = pet_status["activity"] == "pet"
        command("feed")
        feed_status = read_json(status_path)
        result["checks"]["ipc_feed_updates_state"] = feed_status["activity"] == "eat" and feed_status["satiety"] > pet_status["satiety"]
        command("play")
        play_status = read_json(status_path)
        result["checks"]["ipc_play_updates_state"] = play_status["activity"] == "play" and play_status["total_games"] == 1
        command("rest")
        rest_status = read_json(status_path)
        result["checks"]["ipc_rest_updates_state"] = rest_status["activity"] == "sleep"
        result["ipc_evidence"] = {"pet": pet_status, "feed": feed_status, "play": play_status, "rest": rest_status}
        result["checks"]["focus_unchanged"] = window_id(connection.get_input_focus().focus) == original_focus_id
        command("quit")
        process.wait(timeout=5)
        result["checks"]["graceful_quit"] = process.returncode == 0
        result["checks"]["test_mode_did_not_save_state"] = not save_path.exists()

        # Exercise real disk persistence in the same isolated XDG tree, then
        # reopen in test mode. This never touches the user's actual save.
        base_invocation = invocation[:invocation.index("--")]
        status_path.unlink(missing_ok=True)
        process = subprocess.Popen(base_invocation, env=env, stdout=log_file, stderr=subprocess.STDOUT)
        wait_for(lambda: find_window(connection, process.pid), "persistence-test Godot child", 20)
        command("pet")
        saved = wait_for(lambda: read_json(save_path), "isolated persisted cat state", 5)
        result["checks"]["isolated_state_saved"] = saved.get("total_pets") == 1
        command("quit")
        process.wait(timeout=5)
        saved = read_json(save_path)
        result["saved_state"] = saved
        reload_capture = output_dir / "reloaded-viewport.png"
        reload_capture.unlink(missing_ok=True)
        status_path.unlink(missing_ok=True)
        process = subprocess.Popen(base_invocation + ["--", "--test-mode", "--capture", str(reload_capture)], env=env, stdout=log_file, stderr=subprocess.STDOUT)
        wait_for(lambda: find_window(connection, process.pid), "reload-test Godot child", 20)
        loaded = wait_for(lambda: (read_json(status_path) if reload_capture.exists() else None), "reloaded runtime status", 20)
        result["checks"]["saved_state_reloaded"] = loaded.get("total_pets") == saved.get("total_pets") == 1 and abs(loaded.get("bond", 0) - saved.get("bond", 0)) < 0.01
        result["reloaded_status"] = loaded
        command("quit")
        process.wait(timeout=5)
    except Exception as error:
        result["errors"].append(f"{type(error).__name__}: {error}")
    finally:
        if button_down:
            xtest.fake_input(connection, X.ButtonRelease, detail=1)
        if process is not None and process.poll() is None:
            try:
                command("quit")
                process.wait(timeout=3)
            except Exception:
                process.terminate()
                try:
                    process.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=3)
        if underlay is not None:
            underlay.destroy()
        try:
            if pointer_touched:
                xtest.fake_input(connection, X.MotionNotify, x=original_pointer.root_x, y=original_pointer.root_y)
                connection.sync()
            if window_id(connection.get_input_focus().focus) != original_focus_id:
                if hasattr(original_focus, "set_input_focus"):
                    original_focus.set_input_focus(X.RevertToParent, X.CurrentTime)
                    connection.sync()
            restored = root.query_pointer()
            result["pointer_after"] = [restored.root_x, restored.root_y]
            if pointer_touched:
                result["checks"]["pointer_restored"] = (restored.root_x, restored.root_y) == (original_pointer.root_x, original_pointer.root_y)
            else:
                result["checks"]["pointer_not_moved_by_test"] = True
            result["checks"]["focus_restored"] = window_id(connection.get_input_focus().focus) == original_focus_id
        except Exception as error:
            result["errors"].append(f"Restore: {type(error).__name__}: {error}")
        if log_file is not None:
            log_file.close()
            log_text = (output_dir / "godot.log").read_text()
            result["engine_log_messages"] = [line for line in log_text.splitlines() if line.startswith(("WARNING:", "ERROR:", "SCRIPT ERROR:"))]
            result["checks"]["no_script_runtime_errors"] = "SCRIPT ERROR:" not in log_text
        if status_path.exists():
            result["status_at_exit"] = read_json(status_path)
        temporary.cleanup()
        connection.close()
        result["checks"]["temporary_state_removed"] = not temporary_root.exists()
        result["verified_scope_passed"] = bool(result["checks"]) and all(result["checks"].values()) and not result["errors"]
        result["passed"] = result["verified_scope_passed"] and not result["unverified"]
        (output_dir / "result.json").write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n")
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", type=Path, default=PROJECT / "artifacts" / "desktop-verification")
    input_options = parser.add_mutually_exclusive_group()
    input_options.add_argument("--no-input", dest="attempt_input", action="store_false", help="Default: do not move/click the pointer; compositor routing remains unverified.")
    input_options.add_argument("--attempt-input", dest="attempt_input", action="store_true", help="Opt in to bounded XTest routing checks; unavailable in the verified KDE session.")
    parser.set_defaults(attempt_input=False)
    args = parser.parse_args()
    result = run_verification(args.output_dir.resolve(), attempt_pointer=args.attempt_input)
    print(json.dumps({"passed": result["passed"], "verified_scope_passed": result["verified_scope_passed"], "checks": result["checks"], "unverified": result["unverified"], "errors": result["errors"], "artifacts": str(args.output_dir.resolve())}, ensure_ascii=False, indent=2))
    # A skipped compositor routing check is visible in JSON, not a failure of
    # the independently verified rendering/state/persistence scope.
    raise SystemExit(0 if result["verified_scope_passed"] else 1)


if __name__ == "__main__":
    main()
