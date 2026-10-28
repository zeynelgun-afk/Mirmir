#!/usr/bin/env python3
"""Fail if editable licensed third-party assets enter the public repository."""

from pathlib import Path
import subprocess


root = Path(__file__).resolve().parents[1]
forbidden_prefixes = ("unreal/Mirmir/Content/Animals/",)
tracked = subprocess.run(
    ["git", "ls-files", "-z"], cwd=root, check=True, capture_output=True
).stdout.decode("utf-8").split("\0")
violations = sorted(
    path for path in tracked if path and path.startswith(forbidden_prefixes)
)
if violations:
    raise SystemExit(
        "Licensed third-party source assets are tracked in the public repo:\n"
        + "\n".join(violations)
    )

ignore = (root / ".gitignore").read_text(encoding="utf-8")
for prefix in forbidden_prefixes:
    if prefix not in ignore:
        raise SystemExit(f"Missing licensed-asset ignore boundary: {prefix}")
print("MIRMIR_LICENSE_BOUNDARY no editable third-party cat assets tracked")
