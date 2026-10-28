# Current status — 2026-10-28

Mırmır uses **Unreal Engine 5.8.4**. The repository and current documentation
are in English; the application interface remains Turkish. The Godot
feasibility experiment is an archived, separate project.

## Presentation reset

The user rejected the current procedural feeding and seated-rest presentation
as visually unrealistic. Those captures remain historical engineering evidence
only; they are **not accepted product quality** and are not being released as a
new version. Development has pivoted to a zero-cost licensed rigged cat and an
original authored behavior set. [Blendkit Domestic Cat is the first integration
candidate](research/zero-cost-cat-asset-selection-2026-10-28.md), subject to
Blender structure/export, Unreal fur and raw-clip review gates before any
desktop integration.

The reviewed free Blendkit source is now present only in the ignored local
asset boundary. Blender 4.5.15 LTS verified its 32-bone rig and five particle
hair systems, exported a 1.2 MiB skeletal FBX, and Unreal 5.8.4 imported one
SkeletalMesh, one Skeleton and three material slots under the isolated
`/Game/Animals/BlendkitDomesticCat` path. A generated PhysicsAsset is present.
The candidate Groom now contains 79,854 curves, 724,166 curve vertices and
7,985 generated guides. Its Alembic-to-FBX axis conversion is explicitly
`X=-90°, Z=180°`; the resulting binding passed Unreal's asset, mesh and Groom
compatibility checks and a delayed Vulkan frame confirmed that it follows the
cat instead of appearing rotated or displaced.

The current short-coat profile reduces the dense body layer from about 1.65 cm
mean strand length to 0.58 cm, while preserving the shorter face/underfur and
whisker layers. A real black Hair-shaded render was inspected, but the free
source's particle coverage remains visibly regional and clumpy. It is therefore
still a candidate: visual acceptance is false, the product Blueprint was not
changed, and no release was produced. The source also contains no usable
authored feeding, lying, sleeping or play animation library.

## Version 0.5.0

The source adds independent bed/bowl dragging, saved furniture positions,
swept collision and curved routes around obstacles. Invalid overlap and
occupied-bed moves are rejected with engine acknowledgements. The bed has
openings for forward entry and exit. The feeding bowl now sits on a raised,
movable stand; its floor-root placement remains compatible with saved layouts.
Furniture and cat share Unreal depth ordering, and hidden cat pixels are
removed from the native input region. The system tray remains the control
surface; owned windows request no taskbar/pager entry or keyboard focus.

Feeding approaches the bowl, settles, lowers the head, cycles the existing jaw
and tongue rig, then raises the head before travel. The rest base uses the
credited authored **SitDown → IdleSit → StandUp** clips, with procedural
head, eyelid, breathing and tail layers. The internal `Sleeping` state denotes
seated rest; it is not an authored lying-down or sleep animation.

Play uses an evaluated front-paw position and velocity to request an impulse
on a Chaos rigid-body ball. Requested animation targets alone cannot hit the
ball. Counter resets and retained impulse records are separated when counting
new hits in each play session.

## Evidence and build identity

The final behavior proof uses Linux Shipping archive
`package-20261028T074553Z-1412355`, with binary SHA256
`648617a616c3f144cc60c5a8a77a40d293f8bd55257da5a72d44b7a19a20d52a`.
It ran without the editor and exited normally with code **0**. The
[public behavior record](media/behavior-provenance.json) identifies the
producer sessions, image hashes, frame/layout identities and measurements.

The full **Feed → Play → Rest → Feed → Play** run contains 2,220 metadata
samples. All 444 archived main PNGs passed their recorded byte-count,
dimensions and CRC checks. It observed 13 `SitDown`, 97 `Sitting` and
24 `StandUp` samples. A separate stationary-rest run of the same binary
exited with code 0; its front-profile frame shows closed eyelids in the
seated pose. Its 89 archived main frames also passed CRC checks. These are
Shipping observations, separate from the earlier Development/RHI calibration.

Two new evaluated-paw impulses occurred in the first Play session and one
in the second. Their paired ball-velocity changes were approximately
27.61, 23.26 and 20.84 cm/s. Previous-session records were excluded by paw
sample identity. During active play the minimum sampled ball-center height
was about 2.50 cm. This demonstrates the controlled run's measured paw and
ball response; it does not prove every layout or every future strike.

The two feeding phases had median upper-lip-to-near-rim-target distances of
about 2.05 and 1.13 cm. Those distances describe a rig landmark and an aiming
target. They do **not** establish tongue-to-kibble collision or resolved
food contact. See the [actual frames and composition details](media/README.md).

An earlier 0.5 desktop integration check preserved the user's Van appearance
and save. In an unlocked session, all three owned windows mapped with
keyboard input disabled and taskbar/pager exclusion. Main, per-item furniture
and visible-cat input-mask PNGs passed PID/session/CRC checks. Native
placement tests moved each station independently, rejected invalid drops and
restored the layout after restart. These earlier integration checks do not
replace verification of a later behavior cook or package.

Core and native checks cover behavior/navigation, frame pairing, alpha/DPI,
placement acknowledgements, persistence, asset provenance and shutdown.
Unreal automation, actual RHI behavior, native window mapping and fresh
package installation are distinct checks. None establishes the user's
acceptance of visual naturalness.

## Available presentation

- Van, British Shorthair and black-and-white appearance profiles share one
  base model; they are not separate scanned breeds.
- Authored walk, idle, tail and sit/stand clips blend with limited skeletal
  control and paw-contact adjustment.
- Real meow/purr recordings, petting response, saved bond, needs and occasional
  cursor interest are available without a stored pointer history.
- Short fur shells and float-HDR color conversion are implemented. Moving and
  idle target rates are 30/8 frames per second; sustained performance depends
  on the machine.

## Distribution boundaries

The earlier [0.4 preview](https://github.com/zeynelgun-afk/Mirmir/releases/tag/v0.4.0)
provides Debian, RPM, Arch, AppImage, portable archive and direct Flatpak
bundles. Its [verification record](release-0.4-verification.json) retains that
version's exact scope. The final 0.4 session was locked, so its final desktop
mapping was not verified. That historical limit is not reused for a newer run.

The 0.4 Debian package installed in a clean Ubuntu 22.04 container and passed
Qt XCB linking and offscreen shell checks. This was not an independent GPU or
desktop test. Fresh Fedora/openSUSE/Arch installations remain unverified.
Windows build and Inno Setup sources exist; a Windows game or installer EXE
has not been built or tested on this Linux workstation. Direct Flatpak
bundles are not listed on Flathub. New 0.5 installers require fresh staging
and verification from the final behavior cook.

## Limits

There is no authored lying/sleep clip or finished photorealism. A locally
licensed Groom candidate now imports and binds correctly, but its coverage is
not accepted and it is not configured in the product Blueprint.
The cat uses constrained kinematic movement and skeletal control, not a full
muscle, paw-force or paw-pad contact simulation. Jaw/tongue animation and a
source-calibrated surface landmark do not constitute resolved tongue-to-food
collision or food-fluid simulation. A small target error or stable distal
bone point can coexist with an unnatural pose; actual frames must be reviewed.
Foot sliding, crowded layouts, repeated interactions and long sessions need
further observation. These controlled tests do not guarantee every layout.

[Installation](INSTALL.md) · [Unreal development](../unreal/Mirmir/README.md) ·
[Desktop shell](../native-shell/README.md) ·
[Behavior and rig calibration](research/behavior-rig-2026-10-28.md) ·
[Motion research](research/locomotion-2026-10-27.md) · [Flatpak status](FLATPAK.md)

Original code is MIT, the separate PyQt shell is GPLv3, Cat Walk derivatives
are CC BY-NC 4.0, additional motion is CC BY 4.0 and sounds are CC0.
[Sources and licenses](../ASSET-LICENSES.md) also apply to the media.
