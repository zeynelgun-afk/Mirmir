# Contributing to Mirmir

Contributions to motion quality, accessibility, packaging and platform testing
are welcome. Start with [current status](docs/current-status.md) and
[installation](docs/INSTALL.md); distinguish a built artifact from a tested
desktop experience.

## Report a problem

Include the release/version, OS, desktop session, GPU/driver and steps to
reproduce. A short capture of Mirmir's own render is useful for gait problems.
Do not attach private desktop screenshots, user save files or unredacted logs
containing unrelated information. Describe the observed behavior and expected
result; do not replace a user's visual report with a passing automated test.

## Changes

- Keep the real Unreal renderer as the main product. The Godot lab is an archive.
- Preserve user saves, appearance and audio settings. Keep local runtime data
  out of Git; do not add session/window-manager configuration changes.
- Run `./scripts/check.sh`. Unreal C++ changes also need the matching engine
  build and relevant automation/runtime verification.
- Give any added model, clip, texture or sound an author, license, source URL,
  modification record and hash. Do not assume a repository's code license
  covers its bundled assets.
- Keep improvements bounded and measurable. Report remaining paw contact or
  visual artifacts; do not label kinematic IK as force-driven locomotion.

Original project code is MIT; the separate PyQt shell is GPL-3.0-only. Existing
assets have their own terms, including the Cat Walk model's CC BY-NC 4.0
condition. Read [LICENSE](LICENSE) and [asset terms](ASSET-LICENSES.md).

## Distribution and Flathub

Direct Linux packages and Flatpak bundles are built from a cooked game without
shipping the editor SDK. Store inclusion and distribution testing are separate
tasks. [Flatpak documentation](docs/FLATPAK.md) records the current Flathub
submission restrictions, including its rules concerning AI-generated manifests
and AI agents. Do not submit the generated direct-distribution packaging as a
human-authored Flathub manifest.
