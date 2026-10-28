# Current status — 2026-10-28

Mırmır uses **Unreal Engine 5.8.4**. The repository is public; its README,
installation guides, current development docs and release descriptions are in
English. The application interface remains Turkish. The Godot feasibility
experiment is an archived, separate project.

## Version 0.5.0

The new version adds independent bed/bowl dragging, saved furniture positions,
swept collision and obstacle-aware curved routes. Invalid overlap and occupied
bed moves are rejected with engine acknowledgements. The bed now has two
visible openings for forward entry and exit. Furniture and cat share real
Unreal depth ordering; hidden cat pixels are removed from the native input
region. Owned windows request no taskbar or pager entry and no keyboard focus;
the tray remains the control surface.

The final Linux Shipping game has been built and started with the existing
user save and Van appearance preserved. In an unlocked session, all three
owned windows mapped successfully with taskbar/pager exclusion and keyboard
input disabled. The main image, per-item furniture images and visible-cat
input mask passed PID/session/CRC checks. Actual manual clicks and the user's
assessment of visual naturalness remain separate from these checks.

Source checks pass: ASan/UBSan behavior, locomotion and navigation tests,
native placement/ACK/rollback/persistence tests, frame pairing, alpha/DPI,
asset provenance hashes and Python compilation. All three real Unreal
automation tests pass. An isolated actual Shipping test moved both stations,
verified the sibling stayed fixed, restarted to restore both positions and
reached the moved bed with full rest pose. That earlier test ran while the
session was locked; the final unlocked mapping check is recorded separately.

The final Editor/RHI probe reached the bed in 22.18 seconds, exited and reached
the bowl in 7.70 seconds, then observed 453.77 cm of movement during 35 seconds
of Play with no blocked samples. The sampled bowl/body separation remained
positive; normal bed movement stayed outside the obstacle proxy, with the
explicitly permitted bed corridor measured separately. Occluded cat pixels
were observed in 162 samples, and input-mask sequences matched the main
frames. These are controlled-run results, not guarantees for every layout.

Installer packaging and release verification are being completed separately.

## Available behavior

- Authored walk, idle and tail clips with blended transitions, speed-dependent
  playback and limited four-paw IK.
- Van, British Shorthair and black-and-white appearance profiles on the same
  base geometry, short fur shells and float-HDR color conversion.
- Real meow/purr recordings, petting head response, saved bond, needs and
  occasional cursor interest without a stored pointer history.
- A Chaos toy ball; walking to the bowl and lowering the head to eat; entering
  the bed and settling into a procedural crouched resting pose.
- Moving/still target rates of 30/8 frames per second. Actual performance
  depends on the machine; a target rate is not a measured sustained rate.

## Distribution and verification boundaries

The earlier [0.4 preview](https://github.com/zeynelgun-afk/Mirmir/releases/tag/v0.4.0)
contains Debian, RPM, Arch, AppImage, portable archive and direct Flatpak
bundles. Its [verification record](release-0.4-verification.json) separates real
Shipping rendering, sandbox IPC, package creation and desktop visibility. The
final 0.4 session was locked, so its final desktop mapping was not verified.
That historical limitation is not reused as a claim about a newer run.

The 0.4 Debian package installed in a clean Ubuntu 22.04 container and passed
Qt XCB linking and offscreen shell checks. That was not an independent GPU
or desktop test. Fresh Fedora/openSUSE/Arch installations remain unverified.
Windows build and Inno Setup sources exist; a Windows game or installer EXE
has not been built or tested on this Linux workstation. Direct Flatpak
bundles are not listed on Flathub.

## Remaining work

Rest is procedural; there is no authored closed-eye sleep clip or separate
closed-eye geometry. Groom fur and finished photorealism are not complete.
The cat uses constrained kinematic movement and IK, not a full muscle,
paw-force or pad-contact simulation. Remaining foot sliding, rapid toy
contacts, crowded layouts and long sessions need further observation. Automated
checks do not establish the user's acceptance of visual naturalness.

[Installation](INSTALL.md) · [Unreal development](../unreal/Mirmir/README.md) ·
[Desktop shell](../native-shell/README.md) · [Motion research](research/locomotion-2026-10-27.md) ·
[Flatpak status](FLATPAK.md)

Original code is MIT, the separate PyQt shell is GPLv3, Cat Walk derivatives
are CC BY-NC 4.0, additional motion is CC BY 4.0 and sounds are CC0.
[Sources and licenses](../ASSET-LICENSES.md) apply to the media as well.
