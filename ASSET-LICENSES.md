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
