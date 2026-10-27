# Distribution components

Mırmır application code is covered by the repository LICENSE. The native Qt
shell source and its PyQt6 integration are distributed under GPL-3.0; the
complete shell source and license accompany the bundle. The Unreal renderer
is a separate process, communicating with that shell through local IPC files.

PyQt6 is GPLv3, **not LGPL**. Its wheels include LGPL Qt libraries; retain
their supplied license directories. The bundle keeps shared Qt libraries
replaceable instead of embedding them in an opaque single executable.
See [Riverbank's licensing description](https://riverbankcomputing.com/software/pyqt/intro).

Exact Python/wheel versions, SHA256 values, binary download URLs and original
source URLs are recorded in `runtime-lock.json`. Rebuild the private runtime
with `python packaging/prepare-runtime.py`; this does not install into the OS.
The Qt source archive for the bundled Qt version is linked in that lock.
Python/NumPy and all other wheel-supplied license notices remain in the private
runtime's package directories. Do not remove these directories when repacking.

Unreal Engine runtime object code is supplied through the standard cooked UAT
output, subject to the [Unreal Engine EULA](https://www.unrealengine.com/eula/unreal).
Unreal® Engine, Copyright 1998–2026, Epic Games, Inc. All rights reserved.
Unreal® is a trademark or registered trademark of Epic Games, Inc. in the
United States of America and elsewhere. No editor, source, SDK or toolchain is
included. Preserve the UAT runtime notices and third-party runtime files.

Cat geometry and original walking motion: LostBoyz2078 / LostModels2025,
CC BY-NC4.0, personal noncommercial use. Additional motion: DreamNoms, CC BY4.0.
Full attribution, source links and changes appear in ASSET-LICENSES.md and the
included cat/additional-motion provenance files. Sound recordings retain their
CC0 attribution. The generated fine-fur texture is separately identified as
AI-generated; its prompt and origin record are included.

The GPL/LGPL component terms do not replace the separate noncommercial cat
asset condition or Unreal runtime terms. A public source repository does not
turn those assets into MIT-licensed content.
