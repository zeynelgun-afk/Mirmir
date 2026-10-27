#!/usr/bin/env python3
"""AI-assisted upstream-only Flatpak repackager. Not a Flathub submission manifest."""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]
APP_ID = "io.github.zeynelgun_afk.Mirmir"
RUNTIME = "org.freedesktop.Platform/x86_64/25.08"
PERMISSIONS = ["--socket=x11", "--share=ipc", "--device=dri", "--socket=pulseaudio",
               "--talk-name=org.kde.StatusNotifierWatcher"]


def validated_stage(stage):
    stage = stage.resolve(strict=True)
    for path in stage.rglob("*"):
        if path.is_symlink() and not path.resolve().is_relative_to(stage):
            raise ValueError("Bundle contains an external symlink: " + str(path.relative_to(stage)))
    for relative in ("mirmir", "python/bin/python3", "native-shell/mirmir_shell.py", ".local/package.json"):
        if not (stage / relative).is_file():
            raise ValueError("Missing staged file: " + relative)
    if not (stage / "licenses").is_dir():
        raise ValueError("Staged third-party licenses are required.")
    package = json.loads((stage / ".local/package.json").read_text(encoding="utf-8-sig"))
    if package.get("platform") != "Linux" or package.get("configuration") != "Shipping" or package.get("packaging_command_succeeded") is not True:
        raise ValueError("The stage must contain a completed Linux Shipping package.")
    for key in ("binary", "launcher", "archive_directory"):
        path = Path(package[key])
        if path.is_absolute() or not (stage / path).resolve().is_relative_to(stage):
            raise ValueError("Stage manifest paths must be relative and internal: " + key)
    binary = stage / package["binary"]
    if binary.name != "Mirmir-Linux-Shipping" or not binary.is_file():
        raise ValueError("Real Shipping binary missing.")
    with binary.open("rb") as source:
        if source.read(4) != b"\x7fELF":
            raise ValueError("Shipping binary is not ELF.")
    return stage, package


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--stage", required=True, type=Path, help="Completed portable Linux Mirmir directory.")
    parser.add_argument("--version", default=(ROOT / "packaging/VERSION").read_text().strip())
    parser.add_argument("--output", type=Path, default=ROOT / ".local/dist/flatpak")
    args = parser.parse_args()
    if not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+(?:[-.][A-Za-z0-9]+)*", args.version):
        parser.error("Version must be a filename-safe semantic version.")
    stage, package = validated_stage(args.stage)
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ")
    output = args.output.resolve() / (args.version + "-" + stamp)
    build = output / "build-dir"
    app = build / "files/lib/mirmir"
    app.parent.mkdir(parents=True)
    shutil.copytree(stage, app, symlinks=True)
    # Rewrite internal absolute symlinks to stay relocatable under /app.
    for original in stage.rglob("*"):
        if original.is_symlink() and Path(original.readlink()).is_absolute():
            copied = app / original.relative_to(stage)
            copied.unlink()
            import os
            copied.symlink_to(os.path.relpath(app / original.resolve().relative_to(stage), copied.parent))
    scripts = Path(__file__).resolve().parent
    (build / "metadata").write_text("[Application]\nname=" + APP_ID + "\nruntime=" + RUNTIME +
                                  "\nsdk=org.freedesktop.Sdk/x86_64/25.08\ncommand=mirmir\n", encoding="utf-8")
    for source, target in [("mirmir", "bin/mirmir"), ("io.github.zeynelgun_afk.Mirmir.desktop", "share/applications/" + APP_ID + ".desktop")]:
        destination = build / "files" / target
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(scripts / source, destination)
    (build / "files/bin/mirmir").chmod(0o755)
    icon = stage / "assets/icon.svg"
    if not icon.is_file():
        raise ValueError("Staged assets/icon.svg is required for desktop integration.")
    icon_dest = build / "files/share/icons/hicolor/scalable/apps" / (APP_ID + ".svg")
    icon_dest.parent.mkdir(parents=True); shutil.copy2(icon, icon_dest)
    disclosure = build / "files/share/doc" / APP_ID / "AI-DISCLOSURE.txt"
    disclosure.parent.mkdir(parents=True)
    disclosure.write_text("AI-assisted upstream direct-download packaging. NOT A FLATHUB SUBMISSION.\n"
        "The application code, documentation and this repackaging script substantially include AI-generated material.\n"
        "Unreal Engine and third-party assets retain their separate provenance and licenses.\n"
        "No Flathub submission, review request or reply is automated by this project.\n", encoding="utf-8")
    bundle = output / ("Mirmir-" + args.version + "-x86_64.flatpak")
    repo = output / "repo"
    commands = [["flatpak", "build-finish", "--command=mirmir", "--no-inherit-permissions", *PERMISSIONS, str(build)],
        ["flatpak", "build-export", "--arch=x86_64", "--subject=Mirmir " + args.version + " upstream AI-assisted bundle", str(repo), str(build), "stable"],
        ["flatpak", "build-bundle", "--arch=x86_64", "--runtime-repo=https://dl.flathub.org/repo/flathub.flatpakrepo", str(repo), str(bundle), APP_ID, "stable"]]
    with (output / "build.log").open("w", encoding="utf-8") as log:
        for command in commands:
            log.write(json.dumps(command) + "\n"); log.flush()
            subprocess.run(command, check=True, stdout=log, stderr=subprocess.STDOUT)
    with bundle.open("rb") as source:
        digest = hashlib.file_digest(source, "sha256").hexdigest()
    result = {"app_id": APP_ID, "version": args.version, "runtime": RUNTIME, "stage": str(stage), "bundle": str(bundle),
              "sha256": digest, "bytes": bundle.stat().st_size, "permissions": PERMISSIONS,
              "build_succeeded": True, "ai_assisted_upstream_package": True, "flathub_submission": False,
              "signed": False, "actual_sandbox_render_verified": False}
    (output / "result.json").write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    (output / "SHA256SUMS").write_text(digest + "  " + bundle.name + "\n", encoding="utf-8")
    print(json.dumps(result, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
