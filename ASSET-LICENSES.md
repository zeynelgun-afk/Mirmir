# Third-party asset licenses

The prototype code and the third-party assets have separate licenses. Keep this
file and the source credits with any distribution that includes these assets.

## Frozen desktop-lab cat model

`desktop-lab/` is the frozen Godot feasibility prototype. Its Fripouille model
and procedural gait are not the Unreal product's authored walking asset.

**“3d modelling my cat: Fripouille” by guillaume bolis** is licensed under
[Creative Commons Attribution 4.0 International (CC BY 4.0)](https://creativecommons.org/licenses/by/4.0/).

- Original author: [guillaume bolis](https://sketchfab.com/guillaume.bolis).
- Original work: [Fripouille on Sketchfab](https://sketchfab.com/3d-models/3d-modelling-my-cat-fripouille-0ab14bf98e754f8d90fe1bf1c84ca66c).
- Included file: `desktop-lab/assets/cat/fripouille.glb`.
- Downloaded from the public [AllenAI Objaverse mirror](https://huggingface.co/datasets/allenai/objaverse/resolve/main/glbs/000-125/0ab14bf98e754f8d90fe1bf1c84ca66c.glb).
  AllenAI is the mirror operator; guillaume bolis is the model author.
- Original author license evidence: `assets/provenance/fripouille-source.json`.
- Changes in Mırmır: procedural skeletal animation, scene placement, lighting,
  and scale at runtime. The distributed GLB retains the downloaded source bytes.
  The original model contains a rig and textures, but no authored motion clips
  or strand-based fur. These additions do not imply endorsement by the author.

Suggested visible credit:

> Cat model: “3d modelling my cat: Fripouille” by guillaume bolis, CC BY 4.0.
> Runtime procedural animation and presentation adapted for Mırmır.

Link the author, original work, and license in the credits interface when possible.

## Optional personal-use Unreal walking cat

**“Cat Walk” by LostBoyz2078** (current profile: LostModels2025) is licensed under
[Creative Commons Attribution-NonCommercial 4.0 International (CC BY-NC 4.0)](https://creativecommons.org/licenses/by-nc/4.0/).
It is included only for the user's local personal, noncommercial prototype.

- Original author: [LostBoyz2078 / LostModels2025](https://sketchfab.com/LostModels2025).
- Original work: [Cat Walk on Sketchfab](https://sketchfab.com/3d-models/cat-walk-915680200c064815bba75e008ba9efb5).
- Included optional file: `assets/personal-use/cat-walk.glb`.
- Download mirror: [Jaaf4r/myGames, pinned source revision](https://github.com/Jaaf4r/myGames/tree/8e5537e20d104686a462917b7bdc53bb05cf0df6/Godot/hungryCat/assets/cat_walk).
  The repository's own code license does not replace the model's CC BY-NC license.
- Original source files and accompanying license are retained under
  `assets/provenance/cat-walk-personal/original/`.
- Changes: external binary/texture files packed into a self-contained GLB;
  fur materials explicitly set to nonmetallic (`metallicFactor=0`,
  `roughnessFactor=0.9`). Geometry, rig, and authored animation key values
  and timing are unchanged.
- The source has one authored in-place walking clip, not a full run/sit package.
  Its nine-phase visual inspection was performed in a temporary asset viewer;
  Unreal import and playback must be verified separately.

Required credit when sharing this optional asset or a derivative:

> This work is based on “Cat Walk” by LostBoyz2078, licensed under CC BY-NC 4.0.
> Adapted for Mırmır by GLB packaging and nonmetallic material settings.

Include links to the original work, author, and license. Attribution and this
derivative remain subject to the noncommercial condition. This optional asset's
provenance, original hashes, derived-file hash, and review limits are recorded
separately in `assets/personal-use/sources.json`; it is not part of the
commercial-use-compatible asset manifest in `assets/sources.json`.

The optional `cat-walk-loop.glb` is a further CC BY-NC 4.0 derivative: the
existing 24 authored key samples are preserved and the exact first pose is
appended at 0.8 seconds. It introduces no new leg poses. Keep the same author,
original-work and license credits; see `assets/personal-use/loop-variant.json`
and `scripts/close-walk-loop.py` for the change and its verification limits.

The Unreal appearance templates under `/Game/Mirmir/Appearance/` are further
personal-use adaptations of this same Cat Walk texture/geometry. They retain
the original author's credit and CC BY-NC 4.0 condition. `M_CatCoat` recolors the
existing atlas while retaining selected facial/skin and fur detail;
`M_CatEyes` recolors the original iris and otherwise follows coat coloring,
while retaining the audited pupil/sclera, mouth and bright feature UV islands.
The optional `M_CatFur` adds procedural masked shell coverage and displacement
when instantiated by the renderer. It is a layered short-fur approximation,
not a strand Groom or a new photographic fur texture. Original imported
textures/materials/meshes remain byte-identical. Separate provenance and actual
editor shader-compilation evidence are in `assets/provenance/appearance/`.
These are shader adaptations of the existing cat, not anatomically exact new
Van or British Shorthair models; rendered quality must be reviewed separately.

`assets/generated/fur-detail-v1.png` is a new AI-generated fur-detail bitmap,
created with the built-in image generator without reference images. Its exact
prompt, generation mode, unmodified-file SHA256 and visual/edge review limits
are recorded in `assets/generated/fur-detail-v1.json`. It is intended for
restrained albedo modulation and a height-derived shader normal; it is not a
measured PBR scan. The original generated bitmap is preserved. The owned Unreal
texture uses filtered mips and mirror addressing because the generated opposite
edges are not exactly periodic. This generated-detail record does not replace
the Cat Walk author's credit or its noncommercial condition.

## Optional Tuxedo motion retarget candidate

**“Tuxedo Cat Animated 2.0” by DreamNoms** is licensed under
[Creative Commons Attribution 4.0 International (CC BY 4.0)](https://creativecommons.org/licenses/by/4.0/).

- Original author: [DreamNoms](https://sketchfab.com/DreamNoms).
- Original work: [Tuxedo Cat Animated 2.0 on Sketchfab](https://sketchfab.com/3d-models/tuxedo-cat-animated-20-783fcb78b55b4394a212c2b6392e1113).
- Download mirror: [lhns/steam-frame-nix, pinned source revision](https://github.com/lhns/steam-frame-nix/tree/881c1de6a9e813b7c269be80a9bf91baf588f5f2/modules/pet/sources/tuxedo-cat).
  The mirror is not the animation's author. Original glTF/BIN, its accompanying
  attribution, current author-platform license metadata, URLs and SHA256 hashes
  are recorded under `assets/provenance/tuxedo-motion/`.
- Optional derivative: `assets/personal-use/cat-tuxedo-motion-candidate.glb`.
  DreamNoms's idle, tail and sitting motion is resampled and transferred by
  anatomical world-rotation alignment onto the existing Cat Walk rig. The target
  reference is derived from the mean of its 24 authored walk phases; geometry,
  skin, node hierarchy, materials and original walking animation are preserved.
- The combined derivative retains the **noncommercial condition** of the
  Cat Walk model by LostBoyz2078. Credit both authors and both original works.
- This file is an unaccepted review candidate. The sitting transitions fail
  the static contact checks and must not be presented as working product
  animations. Idle and tail clips require actual Unreal visual review. It
  contains no sleeping animation. The reproducible transformation and its
  limits are in `scripts/retarget-cat-motion.py` and
  `assets/personal-use/tuxedo-motion-candidate.json`.

Attribution for this optional derivative:

> Cat geometry and original walk: “Cat Walk” by LostBoyz2078, CC BY-NC 4.0.
> Additional motion: “Tuxedo Cat Animated 2.0” by DreamNoms, CC BY 4.0.
> Reference-pose derivation and motion retargeting adapted for Mırmır.

No endorsement by either author is implied. This optional candidate is separate
from the four-asset manifest in `assets/sources.json` and the original walking
asset record in `assets/personal-use/sources.json`.

The further optional `cat-tuxedo-motion-ik-candidate.glb` retains those same
credits and noncommercial scope. It applies a bounded two-bone contact
correction to the retargeted sitting clips, preserving target bone lengths and
full-contact wrist orientation. It remains unaccepted: residual floor errors,
unreachable target residuals and a roughly 3 cm transition endpoint gap are
documented in `assets/personal-use/tuxedo-motion-ik-candidate.json`.
`scripts/retarget-cat-contact-ik.py` reproduces this isolated review variant;
it does not overwrite the original walk or the first retarget candidate.

## Recorded cat sounds

The following recordings are by **Joseph SARDIN / BigSoundBank** and released
under [CC0 1.0 Universal](https://creativecommons.org/publicdomain/zero/1.0/).
The official [BigSoundBank license](https://bigsoundbank.com/licenses.html) permits
redistribution, editing, and commercial use. Attribution is appreciated but is not
a condition of CC0.

| Included file | Original title | Official source |
| --- | --- | --- |
| `desktop-lab/assets/audio/purr.ogg` | Cat Purr, sound 0436 | [Download page](https://bigsoundbank.com/cat-purr-s0436.html) |
| `desktop-lab/assets/audio/meow-1.ogg` | Little Meow of a Cat #1, sound 0494 | [Download page](https://bigsoundbank.com/little-meow-of-a-cat-s0494.html) |
| `desktop-lab/assets/audio/meow-2.ogg` | Little meow of a cat #9, sound 1479 | [Download page](https://bigsoundbank.com/little-meow-of-a-cat-9-s1479.html) |

The included Ogg recordings retain the downloaded source bytes. Playback volume,
pitch, spatial positioning, and looping may be adjusted at runtime.

Suggested credit: “Additional sounds: Joseph SARDIN — BigSoundBank.com (CC0).”

## Provenance and integrity

`assets/sources.json` records exact local paths, original source URLs, actual
download URLs, authors, licenses, verification timestamps, modification notes,
and SHA256 hashes. `assets/provenance/` contains minimal evidence obtained from
the original author/platform metadata or each official sound download page.

The evaluated Somali cat model is not included in this project.

Current owned Unreal appearance/toy material hashes, their recipe scripts and
the unchanged fourteen original imported cat assets are snapshotted separately
in `assets/provenance/appearance/current-assets.json`. Earlier appearance
records retain historical shader snapshots. `scripts/verify_assets.py` checks
the current snapshot, donor sources, retarget binary preservation and immutable
generated texture in addition to the four primary licensed assets. Asset
integrity does not establish rendered quality or runtime physics behavior.

## Desktop bed and feeding station

`AMirmirHabitat` builds the bed, cushion, bowl and kibble from the installed
Unreal Engine BasicShapes Sphere/Cylinder and BasicShapeMaterial. Their object
code/cooked content is part of the Unreal game, under Epic's separate Unreal
Engine terms; the editor, engine source and SDK are not included in application
packages. The arrangement, dimensions, colors and habitat behavior are original
Mirmir code. No downloaded furniture model or texture is used. The cat's
separate author credits and noncommercial condition still apply.

The optional native resting/eating adjustments are procedural modifications
layered on the credited cat rig and existing motion; they do not imply an
additional artist-authored sleeping/eating clip or fully force-driven animal
simulation. Keep the Cat Walk and DreamNoms credits with the rendered cat.
