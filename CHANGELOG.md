# Changelog

## 0.5.0 — 2026-10-28

- Move the bed and bowl independently by dragging them. Accepted positions and
  the desktop scene anchor persist across restarts.
- Reject overlapping furniture, placements on the cat, blocked bed entrances
  and moves while the cat is using the bed, with visible placement feedback.
- Add swept body collision, obstacle-aware curved routes and two visible bed
  openings for forward entry and exit.
- Render furniture and cat with shared Unreal depth ordering. The native
  input region follows only visible cat/toy pixels.
- Hide owned desktop and game windows from the taskbar and pager while
  retaining tray controls.
- Publish English repository guides and real Unreal appearance photos/video.

This remains a noncommercial preview. See the release verification record for
what was executed, and the current status page for remaining animation and
platform limitations.

## 0.4.0 — 2026-10-27

- Add a permanent bed and feeding bowl, procedural rest/eating reactions and
  engine-confirmed menu messages.
- Fix Play being replaced by Rest near screen edges and prevent petting from
  cancelling an ongoing meal or ball session.
- Refine curved turning, limited four-paw IK and cursor/petting responses.
- Publish Linux packages in Debian, RPM, Arch, AppImage, portable and direct
  Flatpak formats; retain Windows installer sources separately.
