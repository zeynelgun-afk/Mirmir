#!/usr/bin/env python3
"""Wait for the completed official ZIP, install, build, import and bootstrap.

No account/license action or browser control. Uses only a completed local file
and the official extracted engine's tools; never runs the old clang archive.
"""
from __future__ import annotations
from datetime import datetime, timezone
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import time
import zipfile

ROOT = Path(__file__).resolve().parents[1]
STATE = Path(os.environ.get("XDG_STATE_HOME", Path.home() / ".local/state")) / "mirmir/unreal"
ARCHIVE = Path.home() / "İndirilenler/Linux_Unreal_Engine_5.8.4.zip"


def status(stage, **fields):
    STATE.mkdir(parents=True, exist_ok=True)
    data = {"stage": stage, "timestamp_utc": datetime.now(timezone.utc).isoformat(), **fields}
    temporary = STATE / "preparation.json.tmp"
    temporary.write_text(json.dumps(data, ensure_ascii=False, indent=2) + "\n")
    temporary.replace(STATE / "preparation.json")
    print(json.dumps(data, ensure_ascii=False), flush=True)


def run(stage, command, *, env=None):
    log_path = STATE / (stage + ".log")
    status(stage, log=str(log_path))
    with log_path.open("w") as log:
        result = subprocess.run(command, cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT)
    if result.returncode:
        status(stage + "_failed", exit_code=result.returncode, log=str(log_path))
        print(log_path.read_text(errors="replace")[-12000:], flush=True)
        raise SystemExit(result.returncode)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--archive", type=Path, default=ARCHIVE)
    parser.add_argument("--wait", action="store_true")
    args = parser.parse_args()
    archive = args.archive.expanduser()
    while not (archive.is_file() and zipfile.is_zipfile(archive)):
        if not args.wait:
            raise SystemExit("The completed official ZIP is not present yet")
        partials = []
        for path in archive.parent.glob("*.crdownload"):
            # File names/sizes only, never inspect unrelated file contents.
            info = path.stat()
            partials.append({"file": path.name, "bytes": info.st_size, "last_write_unix": info.st_mtime})
        status("waiting_download", archive=str(archive), partials=partials)
        time.sleep(30)

    config_path = ROOT / ".local/engine.json"
    if not config_path.exists():
        run("install", [sys.executable, str(ROOT / "scripts/install-engine.py"), str(archive), "--write-config"])
    config = json.loads(config_path.read_text())
    engine = Path(config["engine_root"])
    executable = engine / "Engine/Binaries/Linux/UnrealEditor"
    if not executable.is_file():
        raise SystemExit("Installed engine config has no UnrealEditor")
    env = os.environ.copy()
    env["MIRMIR_UE_ROOT"] = str(engine)
    env["SDL_VIDEODRIVER"] = "x11"
    sdk = engine / "Engine/Extras/ThirdPartyNotUE/SDKs/HostLinux/Linux_x64"
    bundled_compilers = list(sdk.glob("*/x86_64-unknown-linux-gnu/bin/clang++")) if sdk.is_dir() else []
    if not bundled_compilers:
        setup = engine / "Engine/Build/BatchFiles/Linux/SetupToolchain.sh"
        if setup.is_file():
            run("toolchain", [str(setup)], env=env)
    run("build", [str(ROOT / "unreal/Mirmir/scripts/build.sh"), "-MaxParallelActions=2"], env=env)
    project = ROOT / "unreal/Mirmir/Mirmir.uproject"
    for stage, name in [("import", "import-assets.py"), ("bootstrap", "bootstrap-preview.py")]:
        script = ROOT / "unreal/Mirmir/scripts" / name
        run(stage, [str(executable), str(project), "-run=pythonscript", f"-script={script}",
                    "-unattended", "-nop4", "-nosplash", "-NullRHI", "-stdout", "-FullStdOutLogOutput"], env=env)
    status("ready_for_graphics_validation", engine_root=str(engine),
           note="Build/import are preparation; real render, gait and desktop checks still required")


if __name__ == "__main__":
    main()
