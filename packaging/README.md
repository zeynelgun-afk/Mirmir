# Installer recipes

`runtime-lock.json` pins portable CPython, every Linux wheel and the AppImage
runtime by SHA256 and source URL. `requirements-linux.lock` is generated from
those exact wheel records. The private runtime avoids copying the build host's
newer system Qt/glibc into an otherwise older-compatible application.

```bash
python packaging/prepare-runtime.py
python packaging/build-linux.py
```

The second command requires a successful `.local/package.json` Linux Shipping
cook and Docker. It stages only the UAT runtime, private Python/Qt, native shell
source, notices and recipes. It excludes `.debug/.sym` files, the old Godot lab,
Engine editor/source/toolchain, user records and secrets. Docker supplies build
tools without modifying the host OS. Outputs stay under ignored `.local/dist/`.

One staging tree produces `.deb`, `.rpm`, `.pkg.tar.zst`, portable `.tar.zst`
and a type2 AppImage. Package metadata is inspected; extracted-stage runtime
verification is recorded separately. `SHA256SUMS` and `build-manifest.json`
accompany outputs. Repacking is not a new Unreal cook or proof of broad distro
compatibility. Final release staging must match the final verified UE build.

Windows uses `windows/build-all.ps1` and `Mirmir.iss`. It requires an actual
licensed Windows UE build host and Inno Setup; there is no fake cross-platform
EXE generation. The self-hosted workflow is manual/trusted-main only. PR CI
uses hosted runners and never exposes the UE build machine to untrusted PRs.

See THIRD-PARTY-NOTICES.md and the bundled licenses. PyQt6 is GPLv3; Qt shared
libraries are LGPL. Source and rebuild instructions remain available with the
bundle. The separate UE runtime and cat-asset conditions remain applicable.
