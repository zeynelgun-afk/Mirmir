# Native desktop shell

The shell displays **live Unreal Engine RGBA output** in transparent, non-focusing
Qt windows. There is no sprite, static-cat or alternate-engine fallback.
The application UI remains Turkish; this document describes the implementation
and verification boundaries.

## Running

```bash
./run.sh
./run.sh --stop
python native-shell/mirmir_shell.py --self-test
```

A completed Linux Shipping archive is preferred. The source checkout can fall
back to the configured Unreal Editor in game mode. Installed bundles include a
private CPython/PyQt runtime and the cooked game; they do not need the editor.
The manifest paths can be relative to the installed application directory.

Left-click or stroke the cat to pet it; its cursor changes to a hand. Right-click
opens the tray menu. Shift-drag the cat to move the shared desktop anchor. The
system tray remains available; the companion windows request no taskbar or pager
entries and do not take keyboard focus.

## Independent bed and feeding station

Protocol v2 uses separate real Unreal captures for the bed and bowl. Each station
has its own transparent window and alpha-derived region. A short left click on
the bed requests rest; a short click on the bowl requests feeding. Moving beyond
Qt's drag threshold starts a placement preview, and releasing submits one move
request. A drag never also triggers feeding or rest.

Only the station's opaque bounds constrain its preview at screen edges; its
transparent canvas may extend beyond the screen. The other station stays put.
Confirmed positions come directly from Unreal and are **not independently
clamped** by Qt. Initial scene fitting moves the shared anchor consistently;
layouts too wide for the available screen produce a clear placement message
rather than a false world/image alignment.

A move carries the station ID, current layout revision, matched producer/session,
a unique request ID, physical-pixel delta and five-second expiration. It is
written atomically to the private `layout-command.json`. Only one placement is
pending at a time. Preview remains until the matching `placement` acknowledgement
and corresponding station metadata arrive. Rejection or timeout restores the
last confirmed placement. Feed/Play acknowledgements cannot commit a station
move. Unreal owns world positions and their persistence.

The shared native anchor is saved separately as `home-anchor-v1.json` under the
application data directory. It contains only the application's logical desktop
position, not pointer history. A restored scene is fitted consistently to the
available screen when necessary. The legacy v0.4 compound habitat capture remains
readable but has no independent station placement API.

## Rendering and input routing

The main frame is a real composite so the bed can occlude the cat and receive
its shadows. A second, depth-tested `input_mask` identifies the **visible cat and
toy**. It must match the main frame's sequence, dimensions and CRC-paired bytes.
An entirely transparent input mask is valid when the cat is fully occluded.

On Linux/xcb, `window_platform.py` uses the system X11/Xext libraries to maintain
two different regions: the bounding/render region follows the composite alpha;
`ShapeInput` follows the visible-cat mask. Station pixels therefore pass input
to their separate windows without cutting them out of the rendered composite.
The input shape is reapplied after every visual-mask update. Only this process's
own window IDs are changed; no KWin, session, display or panel settings are edited.

The same own-window helper requests `_NET_WM_STATE_SKIP_TASKBAR` and
`_NET_WM_STATE_SKIP_PAGER`, preserving other EWMH states. Qt's non-focusing policy
is retained. These are application window hints, not changes to the user's taskbar.
An actual isolated X11 test checked the hints, `WM_HINTS.input=false`, and distinct
input versus bounding regions. It did not click other applications.

On Windows, the shell sets `WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE`, clears
`WS_EX_APPWINDOW` and keeps its own layered windows. `WM_NCHITTEST` routes pixels
outside the matched visible-cat mask through the main HWND. Whole-window
`WS_EX_TRANSPARENT` is avoided because it would discard petting too. These Windows
paths are source preparations: real Win64 rendering, installer, hit testing,
focus, mixed DPI and audio still need a Windows machine.

## Viewport and cursor data

Modern scenes are positioned from the same engine projection throughout. The
shell publishes a private `viewport.json` containing its available screen bounds
relative to the shared anchor, main-canvas size, matched producer/session and a
five-second TTL. It refreshes at 1 Hz or on home/screen changes. Unreal uses this
to constrain real movement; the shell does not hide excess actor motion with an
independently clamped cat window. Legacy v0.4 keeps its previous edge behavior.

Cursor interest is optional and sparse: a 1.5-second attention window sampled at
5 Hz, with a 12–20-second pause (8–14 seconds before the first window). Only the
latest bounded offset relative to the application's own frame is exchanged in
`cursor.json`; no absolute pointer history, desktop pixels, other-app files,
window titles, keyboard input or microphone is collected. The payload carries a
matched producer/session and a 750 ms TTL. Synthetic/offscreen tests never query
the real pointer. The tray toggle or `--no-cursor-interest` disables it.

## Files and frame validation

Persistent data uses `${XDG_DATA_HOME:-$HOME/.local/share}/mirmir/unreal` and logs
use `${XDG_STATE_HOME:-$HOME/.local/state}/mirmir/unreal`. Live frames, runtime,
window status and leases prefer the owned private tmpfs directory below
`XDG_RUNTIME_DIR`. Custom data/state roots receive distinct hashed namespaces.
`frame-location.json` in the state directory records the actual live directory.
Only the child environment receives `MIRMIR_FRAME_DIR` and the selected XDG roots.

Every displayed image must have a ready/running producer, matching child PID and
session, monotonic frame identity, correct PNG byte count/CRC and decoded size.
The main image and input mask are accepted together. Brief publication races may
retain the last matched frame for up to three seconds; an explicit error or
stopped producer hides all surfaces. Static station PNGs may be older, but their
metadata must belong to the current validated parent stream.

Commands use small atomic JSON files in the application's own queue. Stale known
commands are removed before starting a new engine, while the cat save is kept.
Normal shutdown sends one quit command, waits for its own child and preserves
saves. Fallback termination is limited to that child. Existing legacy leases are
recognized so an upgrade does not launch a second cat.

Windows defaults use `LOCALAPPDATA/Mirmir`; its runtime directory is **not tmpfs**.
Ownership/SID and reparse-point checks are present. No broad ACL audit or real
Windows runtime validation is implied.

## Verification

The offscreen self-test covers independent drag/click separation, release-only
placement, foreign/stale ACKs, rejection/timeout rollback, persisted home,
viewport timing, fully empty input masks, CRC/session/sequence gates, alpha/DPI,
legacy leases, cursor privacy, engine-confirmed interaction feedback and the
regression where edge clamping silently cancelled Play.

Synthetic tests do not establish natural animation, physical paw contact, real
user clicks or platform integration. Real Unreal captures, owned-window X11
inspection and installed-bundle checks are reported separately. A locked desktop
can prevent managed windows from mapping; testing never unlocks it or bypasses
the compositor. The standalone shell is licensed under [GPL-3.0-only](LICENSE);
Unreal and the assets retain their separate licenses.
