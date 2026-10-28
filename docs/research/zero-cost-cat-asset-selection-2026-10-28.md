# Zero-cost cat asset selection — 2026-10-28

## Decision

Mirmir will not purchase a model or animation pack. The paid GiM Animalia
candidate has been removed from the active plan.

The first zero-cost integration candidate is
[Domestic Cat, Rigged on Blendkit](https://www.blendkit.com/asset-gallery-detail/6b3bbcc7-db99-4424-9436-8e6678ee9354/),
created by Paweł Wałasiewicz. The official listing marks it as a free,
11,056-polygon model with an optional fur setup and a basic reverse-IK rig.
Blendkit's Royalty Free license permits commercial use and higher-level
derivative works, but prohibits reselling the model in the same form.

The source was downloaded without an account or payment through Blendkit's
official public API. It remains local and ignored below
`Content/Animals/BlendkitDomesticCat/`; it is not part of the public Git
repository. The exact reviewed source has SHA-256:

```text
5559921e22fee4af399e2ae640d7b77269c794e2a5aa14ab9ad5bf05b743d709
```

The source was opened read-only in Blender 4.5.15 LTS. The audit found one
32-bone armature, a 9,024-vertex body mesh, a separate eye mesh, and five hair
particle systems (`BODY`, `HEAD/PAWS`, `whiskers`, `FACE`, and `UNDERFUR`). It
contains a two-frame pose library, not a usable behavior animation set. This is
therefore a **rigged model candidate**, not a finished replacement and not yet
visual acceptance in Unreal.

Official sources:

- [Blendkit model listing](https://www.blendkit.com/asset-gallery-detail/6b3bbcc7-db99-4424-9436-8e6678ee9354/)
- [Blendkit license summary](https://www.blendkit.com/docs/licenses/)
- [Blender 4.5 LTS downloads](https://download.blender.org/release/Blender4.5/)

## Why the other free options were rejected

| Candidate | Verified result | Decision |
| --- | --- | --- |
| [Animal - Cat \| Free Bundle](https://www.fab.com/listings/bb864687-9852-4c54-a160-4cb28ce3e2ea) | Ten clips: idle, walk, run, jump, three attacks, hit, die and dead; no eating, lying, sleeping or waking. Official media is below the photoreal target and the listing says it was AI-generated. | Rejected as the product model and behavior set. |
| [Animal Animator](https://extensions.blender.org/add-ons/animal-animator/) | Excellent behavior coverage including eating, lying, sleep, grooming and pounce, but distributed under GPL-3.0-or-later. | Not imported or copied. Unreal's current EULA lists GPL-covered code or content as a non-compatible license. |
| [Bicolor Cat](https://sketchfab.com/3d-models/bicolor-cat-e623a618ca344a8393d7ba4d63ec23cf) | CC BY, one skin and one nominal animation, but the animation data does not form a usable action library and the surface quality misses the target. | Rejected. |
| [Standing Cat](https://www.innerscene.com/tools/library/3d-parts/standing-cat-cda42538) | Clean CC0 provenance and direct download, but static, unrigged and visibly stylized. | Rejected. |

## Zero-cost animation route

We will author and bake Mirmir's own clips on the Blendkit rig. The required
library is:

1. neutral idle, breathing, look-around and tail variations;
2. walk, trot, start, stop, left/right arcs and 90-degree turns;
3. approach bowl, lower head, eating loop, swallow/lick and exit;
4. stand-to-sit, sit-to-lie, curl, sleep loop, wake and stand;
5. stalk, pounce, bilateral paw taps, recoil and ball-follow turns;
6. petting reactions: head press, cheek rub, back arch and relaxed settle.

These will be authored from feline motion reference, with planted feet, pelvis
and spine timing, and explicit contact markers. They will be exported as baked
animation data rather than produced by the rejected runtime limb deformation.
Animal Animator may be used only as a list of behavior names; none of its GPL
motion data or code may enter the project.

## Acceptance gates

The model is not accepted merely because it is free or rigged. Before replacing
the current desktop cat it must pass all of these gates:

- deterministic source hash and Blender structure audit;
- skeletal-mesh export/import without missing weights, eye deformation or
  altered proportions;
- an Unreal fur solution that preserves silhouette quality at desktop size
  without excessive GPU cost;
- isolated unedited captures for eating, lie/sleep/wake, two turns and both paw
  actions;
- feet remain planted, the body does not cross the bowl or bed, and every
  contact event agrees with the prop collision;
- a packaged desktop run is visually reviewed before any public release.

The Blender audit/export tool is
`unreal/Mirmir/scripts/blendkit_cat_pipeline.py`; the shell entry point is
`unreal/Mirmir/scripts/process-blendkit-cat.sh`. Generated FBX, audit reports and
licensed source files stay under ignored local directories. The isolated Unreal
import is performed by `unreal/Mirmir/scripts/import-blendkit-cat.sh`; it does
not replace the product Blueprint or claim visual acceptance.
