#!/usr/bin/env python3
"""Install an already downloaded official Linux UE 5.8.4 ZIP, without sudo.

This checks the ZIP structure, not Epic's publisher signature. Obtain the ZIP
from the authenticated official Linux download page. The archive is preserved.
No setup scripts, editor, downloads, accounts, or license acceptance are run.
"""

from __future__ import annotations

import argparse
import ctypes
import errno
import json
import os
from pathlib import Path, PurePosixPath
import posixpath
import shutil
import stat
import sys
import tempfile
import time
import zipfile
from dataclasses import dataclass


VERSION = "5.8.4"
ARCHIVE_NAME = f"Linux_Unreal_Engine_{VERSION}.zip"
EDITOR_PARTS = ("Engine", "Binaries", "Linux", "UnrealEditor")
CHUNK_SIZE = 8 * 1024 * 1024
HEADROOM = 2 * 1024**3
PROJECT = Path(__file__).resolve().parents[1]


def event(name: str, **values: object) -> None:
    print(json.dumps({"event": name, **values}, ensure_ascii=False), flush=True)


@dataclass(frozen=True)
class Entry:
    info: zipfile.ZipInfo
    relative: PurePosixPath
    kind: str
    permissions: int
    link_target: str | None = None


@dataclass(frozen=True)
class Plan:
    entries: tuple[Entry, ...]
    unpacked_bytes: int
    prefix: tuple[str, ...]
    build_version: dict | None


def safe_parts(name: str) -> tuple[str, ...]:
    if not name or name.startswith(("/", "\\")) or "\\" in name:
        raise ValueError(f"Unsafe ZIP path: {name!r}")
    if any(ord(character) < 32 for character in name):
        raise ValueError("ZIP path contains control characters")
    parts = tuple(name.rstrip("/").split("/"))
    if any(part in ("", ".", "..") for part in parts) or ":" in parts[0]:
        raise ValueError(f"Unsafe ZIP path: {name!r}")
    return parts


def inspect_archive(archive: zipfile.ZipFile) -> Plan:
    infos = archive.infolist()
    if len(infos) > 1_000_000:
        raise ValueError("ZIP has an unreasonable number of entries")
    paths = [safe_parts(info.filename) for info in infos]
    candidates = [path[:-4] for path in paths if path[-4:] == EDITOR_PARTS]
    if len(candidates) != 1:
        raise ValueError("Expected exactly one Engine/Binaries/Linux/UnrealEditor")
    prefix = candidates[0]
    entries: list[Entry] = []
    occupied: dict[PurePosixPath, str] = {}
    unpacked_bytes = 0
    build_version = None
    for info, parts in zip(infos, paths):
        # ZIPs may include an enclosing version directory and its ancestors.
        if len(parts) <= len(prefix) and prefix[: len(parts)] == parts and info.is_dir():
            continue
        if parts[: len(prefix)] != prefix:
            raise ValueError(f"ZIP entry outside the detected engine root: {info.filename!r}")
        relative = PurePosixPath(*parts[len(prefix) :])
        if str(relative) == ".":
            raise ValueError("Invalid engine root ZIP entry")
        if relative in occupied:
            raise ValueError(f"Duplicate ZIP destination: {relative}")
        if info.flag_bits & 1:
            raise ValueError("Encrypted ZIP entries are not supported")
        mode = (info.external_attr >> 16) & 0xFFFF
        file_type = stat.S_IFMT(mode)
        if info.is_dir():
            kind = "directory"
            if file_type not in (0, stat.S_IFDIR):
                raise ValueError(f"Inconsistent directory metadata: {relative}")
        elif file_type == stat.S_IFLNK:
            kind = "symlink"
        elif file_type in (0, stat.S_IFREG):
            kind = "file"
        else:
            raise ValueError(f"Unsupported special ZIP entry: {relative}")

        link_target = None
        if kind == "symlink":
            if info.file_size > 4096:
                raise ValueError(f"Oversized symlink target: {relative}")
            link_target = archive.read(info).decode("utf-8")
            if (not link_target or "\\" in link_target or link_target.startswith("/")
                    or any(ord(character) < 32 for character in link_target)):
                raise ValueError(f"Unsafe symlink target: {relative}")
            resolved = posixpath.normpath(posixpath.join(str(relative.parent), link_target))
            if resolved == ".." or resolved.startswith("../"):
                raise ValueError(f"Symlink escapes engine root: {relative}")
        if kind == "file":
            unpacked_bytes += info.file_size
        occupied[relative] = kind
        entries.append(Entry(info, relative, kind, stat.S_IMODE(mode) & 0o777, link_target))
        if str(relative) == "Engine/Build/Build.version" and kind == "file":
            if info.file_size > 65536:
                raise ValueError("Unexpectedly large Build.version")
            build_version = json.loads(archive.read(info).decode("utf-8-sig"))
            if not isinstance(build_version, dict):
                raise ValueError("Invalid engine Build.version")
            actual = tuple(build_version.get(key) for key in ("MajorVersion", "MinorVersion", "PatchVersion"))
            if actual != (5, 8, 4):
                raise ValueError(f"Engine Build.version is {actual}, expected (5, 8, 4)")

    for entry in entries:
        for parent in entry.relative.parents:
            if parent in occupied and occupied[parent] != "directory":
                raise ValueError(f"ZIP destination is beneath a file/symlink: {entry.relative}")
    if occupied.get(PurePosixPath(*EDITOR_PARTS)) != "file":
        raise ValueError("UnrealEditor must be a regular file")
    if occupied.get(PurePosixPath("Engine/Build/BatchFiles/Linux/Build.sh")) != "file":
        raise ValueError("The archive does not contain the Linux engine Build.sh")
    return Plan(tuple(entries), unpacked_bytes, prefix, build_version)


def rename_without_replacement(source: Path, target: Path) -> None:
    # Linux renameat2 avoids the check-then-rename race which could otherwise
    # replace a concurrently created empty destination directory.
    libc = ctypes.CDLL(None, use_errno=True)
    rename = getattr(libc, "renameat2", None)
    if rename is None:
        raise RuntimeError("renameat2 unavailable; refusing a potentially overwriting final rename")
    rename.argtypes = (ctypes.c_int, ctypes.c_char_p, ctypes.c_int, ctypes.c_char_p, ctypes.c_uint)
    rename.restype = ctypes.c_int
    if rename(-100, os.fsencode(source), -100, os.fsencode(target), 1) != 0:
        number = ctypes.get_errno()
        raise OSError(number, os.strerror(number), str(target))


def extract_archive(archive: zipfile.ZipFile, plan: Plan, stage: Path) -> None:
    completed = 0
    last_update = 0.0
    files = 0
    for entry in plan.entries:
        destination = stage.joinpath(*entry.relative.parts)
        if entry.kind == "directory":
            destination.mkdir(parents=True, exist_ok=True)
            continue
        if entry.kind == "symlink":
            continue  # Create links last, so writes never traverse archive links.
        destination.parent.mkdir(parents=True, exist_ok=True)
        first_chunk = b""
        with archive.open(entry.info, "r") as source, destination.open("xb") as output:
            while chunk := source.read(CHUNK_SIZE):
                if not first_chunk:
                    first_chunk = chunk[:4]
                output.write(chunk)
                completed += len(chunk)
                now = time.monotonic()
                if now - last_update >= 2.0:
                    event("extracting", bytes=completed, total_bytes=plan.unpacked_bytes,
                          percent=round(completed * 100 / max(1, plan.unpacked_bytes), 1),
                          files=files, current=str(entry.relative))
                    last_update = now
        # ZipFile checks CRC during this single decompression pass. Do not run
        # testzip() first: that would decompress the complete archive twice.
        permissions = entry.permissions or 0o644
        executable = (entry.relative.suffix == ".sh" or first_chunk == b"\x7fELF"
                      or first_chunk.startswith(b"#!") or entry.relative.parts == EDITOR_PARTS)
        if executable:
            permissions |= 0o111
        destination.chmod(permissions & 0o777)  # Never retain setuid/setgid bits.
        files += 1
    for entry in plan.entries:
        if entry.kind == "symlink":
            destination = stage.joinpath(*entry.relative.parts)
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.symlink_to(entry.link_target)
    for entry in plan.entries:
        if entry.kind == "symlink":
            # Also check composed links: lexical checks alone cannot detect a
            # target such as other_link/../../outside after other_link resolves.
            resolved = stage.joinpath(*entry.relative.parts).resolve(strict=False)
            if not resolved.is_relative_to(stage.resolve()):
                raise ValueError(f"Resolved symlink escapes staging root: {entry.relative}")
    for entry in sorted(plan.entries, key=lambda item: len(item.relative.parts), reverse=True):
        if entry.kind == "directory":
            stage.joinpath(*entry.relative.parts).chmod((entry.permissions or 0o755) | 0o700)
    event("extraction-complete", bytes=completed, files=files)


def nearest_existing_directory(path: Path) -> Path:
    while not path.exists():
        if path == path.parent:
            raise ValueError("Cannot find target filesystem")
        path = path.parent
    if not path.is_dir():
        raise ValueError(f"Target parent is not a directory: {path}")
    return path


def write_config(target: Path) -> None:
    directory = PROJECT / ".local"
    path = directory / "engine.json"
    desired = {"schema_version": 1, "engine_version": VERSION, "engine_root": str(target)}
    if os.path.lexists(path):
        event("config-unchanged", path=str(path), reason="Existing config preserved; integrate deliberately")
        return
    directory.mkdir(parents=True, exist_ok=True)
    # O_EXCL protects an existing config including a concurrently created link.
    descriptor = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(descriptor, "w", encoding="utf-8") as output:
        json.dump(desired, output, indent=2)
        output.write("\n")
    event("config-written", path=str(path))


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path, help=f"Completed official {ARCHIVE_NAME}")
    parser.add_argument("--target", type=Path, default=Path.home() / "Applications" / f"UnrealEngine-{VERSION}")
    parser.add_argument("--dry-run", action="store_true", help="Inspect and check space only; write nothing")
    parser.add_argument("--write-config", action="store_true", help="After success, create project .local/engine.json if absent")
    args = parser.parse_args(argv)
    stage = None
    try:
        archive_path = args.archive.expanduser().resolve(strict=True)
        if archive_path.name != ARCHIVE_NAME or not archive_path.is_file():
            raise ValueError(f"Expected completed archive named exactly {ARCHIVE_NAME}")
        target = args.target.expanduser().absolute()
        if os.path.lexists(target):
            raise FileExistsError(f"Target already exists and will not be changed: {target}")
        original_stat = archive_path.stat()
        with zipfile.ZipFile(archive_path, "r") as archive:
            plan = inspect_archive(archive)
            filesystem = nearest_existing_directory(target.parent)
            free = shutil.disk_usage(filesystem).free
            reserve = 2 * plan.unpacked_bytes + HEADROOM
            event("inspection-complete", archive=str(archive_path), target=str(target),
                  entries=len(plan.entries), archive_bytes=original_stat.st_size,
                  unpacked_bytes=plan.unpacked_bytes, required_free_bytes=reserve,
                  free_bytes=free, enclosing_prefix="/".join(plan.prefix),
                  build_version_present=plan.build_version is not None,
                  note="Reserve is twice unpacked size plus 2 GiB; existing archive already occupies disk")
            if free < reserve:
                raise OSError(errno.ENOSPC, "Insufficient space for unpacked engine and conservative overhead")
            if args.dry_run:
                event("dry-run-complete", writes_performed=False)
                return 0
            target.parent.mkdir(parents=True, exist_ok=True)
            stage = Path(tempfile.mkdtemp(prefix=f".{target.name}.extract-", dir=target.parent))
            event("staging-created", path=str(stage))
            extract_archive(archive, plan, stage)
        latest_stat = archive_path.stat()
        if (latest_stat.st_size, latest_stat.st_mtime_ns) != (original_stat.st_size, original_stat.st_mtime_ns):
            raise RuntimeError("Archive changed during extraction; final install not published")
        editor = stage.joinpath(*EDITOR_PARTS)
        if not editor.is_file() or not os.access(editor, os.X_OK):
            raise RuntimeError("Extracted UnrealEditor is missing or not executable")
        rename_without_replacement(stage, target)
        stage = None
        event("installed", engine_root=str(target), archive_preserved=True,
              editor=str(target.joinpath(*EDITOR_PARTS)), setup_executed=False)
        toolchain = target / "Engine/Build/BatchFiles/Linux/SetupToolchain.sh"
        sdk_root = target / "Engine/Extras/ThirdPartyNotUE/SDKs/HostLinux/Linux_x64"
        event("post-install-check", setup_toolchain=str(toolchain) if toolchain.is_file() else None,
              bundled_sdk_directories=sorted(p.name for p in sdk_root.iterdir() if p.is_dir()) if sdk_root.is_dir() else [],
              note="Inspect bundled SDK before running official toolchain setup; unrelated clang tar is not used")
        if args.write_config:
            write_config(target)
        return 0
    except (OSError, ValueError, RuntimeError, zipfile.BadZipFile, NotImplementedError) as error:
        event("error", message=str(error), staging_preserved=str(stage) if stage else None,
              archive_preserved=True)
        return 1


if __name__ == "__main__":
    sys.exit(main())
