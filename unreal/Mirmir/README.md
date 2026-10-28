# Mırmır — Unreal development

The main product is a C++ Unreal Engine **5.8.4** game rendered into RGBA
frames for the [native desktop shell](../../native-shell/README.md). Both
Editor game mode and a cooked Linux Shipping game have been run locally.
See [current status](../../docs/current-status.md) for versioned evidence.

## Run and build

From the repository root:

```bash
export MIRMIR_UE_ROOT=/path/to/UnrealEngine-5.8.4
./unreal/Mirmir/scripts/build.sh -MaxParallelActions=2
./run.sh
./run.sh --stop
./run.sh --preview
./run.sh --editor
```

With a valid `.local/package.json`, the launcher starts its Shipping binary.
Otherwise it uses the installed engine. The desktop shell uses Qt/XWayland
on the tested KDE Wayland session. It does not change the user's compositor
settings. There is no static-cat or Godot fallback.

```bash
./unreal/Mirmir/scripts/test-core.sh
./unreal/Mirmir/scripts/test-engine.sh
./unreal/Mirmir/scripts/package-game.sh
```

The engine path is resolved from `MIRMIR_UE_ROOT` or ignored local metadata.
Core tests exercise engine-independent behavior, locomotion and navigation
under ASan/UBSan. Unreal automation, RHI rendering and Shipping launch are
separate checks. Each package invocation creates a new archive and records
its project version; previous packages are preserved.

## Assets and animation

The cat uses LostBoyz2078's **Cat Walk**, licensed **CC BY-NC 4.0**. Four mesh
parts follow the same skeleton. An authored 0.8-second walk and retargeted
idle/tail clips are blended in a native animation instance. Source motion
suggests an initial walk-speed reference of approximately 68 cm/s; this is
not proof of zero foot sliding. Four-paw IK and speed-dependent playback
reduce sliding. Backward clearance steps play the walk in reverse.

Movement follows bounded curvature, acceleration and yaw rates. Candidate
routes and actual swept steps avoid furniture and respect the desktop
viewport. The bed has visible openings at both ends; resting enters from
one side and exits forward through the other. This is constrained kinematic
locomotion, not a full muscle or paw-force simulation. The toy ball uses
Chaos rigid-body physics and collides with the floor and furniture.

Van, British Shorthair and black-and-white profiles share the same geometry.
Short fur shells and float-HDR color conversion are implemented. Rest and
eating reactions are procedural. There are no accepted authored sleep,
grooming or running clips, closed-eye geometry or finished Groom fur.

Asset source links, hashes, modifications and all license conditions are
recorded in [ASSET-LICENSES.md](../../ASSET-LICENSES.md). `import-assets.py`
imports existing licensed GLB/audio sources; `bootstrap-preview.py` creates
the preview Blueprint and map with actual mesh, animation and sound
assignments. Inspect existing content before rerunning either step; do not
blindly overwrite unrelated assets. These tools do not generate a new walk,
idle animation or Groom.

## Desktop rendering and placement

The main capture renders the cat, toy and furniture together so depth ordering
is decided in Unreal. Separate bed and bowl captures keep stationary objects
visible while the cat travels. Smaller captures retain the same physical
pixels-per-centimeter scale and publish top-left offsets in the shared scene.
Two depth captures derive a visible-cat input mask, paired with the main
frame's sequence and PNG CRC. Hidden cat pixels do not intercept input.

The shell sends a session-bound `move_station` request after a drop. The engine
converts physical pixel displacement to the floor plane, validates the new
layout, saves accepted positions and returns the matching request ID. Invalid
overlap or occupied-bed moves are rejected. `viewport.json` bounds movement
using the shell's actual available desktop region.

See the [shell protocol](../../native-shell/README.md) and
[placement design](../../docs/superpowers/specs/2026-10-28-mirmir-layout-collision-design.md).

## Data and sound

On Linux, `${XDG_DATA_HOME:-~/.local/share}/mirmir/unreal/` contains
`cat-v1.json`, `habitat-v1.json`, the native home anchor and command files.
Logs use `${XDG_STATE_HOME:-~/.local/state}/mirmir/unreal/`. Frames, runtime
metadata and the shell lease use a private runtime directory where possible;
`frame-location.json` records it. Engine caches and `Saved/` are separate from
the cat's user data. Corrupt saves are not overwritten automatically.

Meow and purr use real SoundWave recordings. `FeedSound` may remain unassigned;
there is no invented eating recording. The child process defaults to
PulseAudio and the project allows unfocused audio. Global sound settings are
not changed. Earlier isolated output-stream measurements confirm a signal,
not physical speaker output or a user's listening experience.

`r.PSOPrecaching=0` is a project-specific workaround for a reproduced startup
crash on the local Radeon 890M/Mesa ACO stack. Keep it until a separately
verified replacement is available. A short successful run does not establish
long-term stability or compatibility with every GPU.
