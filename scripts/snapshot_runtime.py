#!/usr/bin/env python3
"""Record a short sample of this application's own process, not system activity."""
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import subprocess
import time

root = Path(__file__).resolve().parents[1]
state = Path(os.environ.get("XDG_STATE_HOME", Path.home() / ".local/state")) / "mirmir"
data = Path(os.environ.get("XDG_DATA_HOME", Path.home() / ".local/share")) / "mirmir"
pid = int((state / "engine.pid").read_text())
identity = (Path("/proc") / str(pid) / "cmdline").read_bytes().split(b"\0")
if str(root / "desktop-lab").encode() not in identity:
    raise SystemExit("PID does not belong to the expected Mırmır lab")
clock_ticks = os.sysconf("SC_CLK_TCK")
page_size = os.sysconf("SC_PAGE_SIZE")

def sample():
    content = (Path("/proc") / str(pid) / "stat").read_text().rsplit(") ", 1)[1].split()
    return (int(content[11]) + int(content[12])) / clock_ticks, int(content[21]) * page_size

start_cpu, _ = sample()
started = time.monotonic()
time.sleep(3)
end_cpu, rss = sample()
duration = time.monotonic() - started
status = json.loads((data / "runtime-status.json").read_text())
report = {
    "timestamp_utc": datetime.now(timezone.utc).isoformat(),
    "implementation": "godot-desktop-lab",
    "service_active": subprocess.run(["systemctl", "--user", "is-active", "--quiet", "mirmir-desktop.service"]).returncode == 0,
    "cpu_sample_seconds": round(duration, 3),
    "cpu_percent_one_core": round((end_cpu - start_cpu) / duration * 100, 2),
    "rss_mib": round(rss / 1024 / 1024, 2),
    "fps_latest_reported": status.get("fps"),
    "renderer": status.get("renderer"),
    "limits": [
        "One short process sample, not a continuous performance guarantee.",
        "RSS includes process memory; Godot static allocator memory alone is smaller.",
        "GPU load, energy, battery, temperature, fullscreen and multi-monitor behavior are unmeasured.",
        "This is not Unreal Engine performance evidence or human usability validation.",
    ],
}
target = root / "docs/runtime-sample.json"
target.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n")
print(json.dumps(report, ensure_ascii=False, indent=2))
