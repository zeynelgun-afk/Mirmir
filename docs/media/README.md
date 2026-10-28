# Preview media

These images come from the application's own **Unreal Engine 5.8.4 renderer**.
They are not desktop screenshots. The preview images and clips place actual
RGBA game frames on a neutral background, with a fixed projection scale and
an English caption. No other application's pixels, pointer history, microphone
or audio were captured.

The [Van](van.png), [British](british.png) and [tuxedo](tuxedo.png) images show
three appearance profiles on the same base mesh. They are not three separately
scanned breed models. The corresponding `raw-main-*.png` files retain the
producer's original PNG bytes, including transparency. The larger preview
images combine the CRC-matched bed, bowl and depth-composited main render.

The [appearance GIF](appearance-preview.gif) and
[appearance MP4](appearance-preview.mp4) are a silent, 24.9-second capture of
those profile changes. **The actor did not translate during that capture.**
It is an appearance gallery, not a walking demonstration or proof of final
Shipping behavior. Its capture came from a pre-release editor/game session.

[appearance-provenance.json](appearance-provenance.json) records the producer
session, frame/layout identities, unchanged source hashes, display offsets,
composition and verification limits. The complete raw recording remains in
the local validation archive. `scripts/record-preview.py` records only the
explicitly supplied app frame directory and retains raw PNG/JSON separately
from the composed presentation.

Cat geometry and original walk: **Cat Walk by LostBoyz2078**, CC BY-NC 4.0.
Additional motion: **Tuxedo Cat Animated 2.0 by DreamNoms**, CC BY 4.0.
Full author/source links, modifications and generated-fur provenance are in
[ASSET-LICENSES.md](../../ASSET-LICENSES.md).
