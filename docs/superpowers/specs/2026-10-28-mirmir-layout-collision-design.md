# Mırmır 0.5: movable stations and solid furniture

The user wants independent bed/bowl placement, persistent positions, no cat
passing through furniture, and no taskbar entry while retaining the tray.
The existing authorization permits implementing and verifying these changes
without another approval step. Preserve cat saves and desktop/session settings.

The diagnosed causes are NoCollision furniture, direct actor translation, and
two independent RGBA layers which always paint the cat above the furniture.
Changing only collision flags cannot fix routing or visual depth.

- Engine habitat positions live in separate versioned user data. Finite,
  bounded positions are accepted atomically only when clear of the cat, the
  other station and required entry/exit space. Occupied-bed movement is
  rejected with an explicit ACK. Existing cat-v1.json remains compatible.
- Each station has a real Unreal RGBA stream and an independently draggable
  native surface. Drag preview is transient; release sends session/PID/revision
  and request ID. Only the matching accepted engine ACK commits a new position.
  The scene anchor is also persisted. Native display never silently clamps
  confirmed geometry apart from its world representation.
- The cat uses a swept body proxy and obstacle-aware curved navigation. Bed
  entry/exit is permitted only for rest through matching visible openings;
  ordinary movement and play avoid solid furniture. The bowl always blocks
  the body. Toy collision uses the same physical furniture.
- The main frame renders cat and furniture together for real depth ordering.
  A depth-tested visible-cat input mask routes covered bed/bowl pixels to their
  own windows rather than petting invisible parts of the cat. Per-station
  captures preserve one physical pixel/cm projection and explicit offsets.
- Only owned windows receive skip-taskbar/skip-pager/tool-window policies;
  the tray remains. No KWin, session, display or taskbar configuration changes.

Verification: geometry/placement rejection and persistence tests, real Unreal
build/automation, swept paths and front/behind rendered occlusion, separate
visible/input shapes, drag ACK/rollback/restart and own-window taskbar hints.
Use isolated saves for probes. Package/cut over the verified 0.5 Shipping build,
preserving the user's current appearance, needs and audio settings. Report
platform and physical simulation limits separately.
