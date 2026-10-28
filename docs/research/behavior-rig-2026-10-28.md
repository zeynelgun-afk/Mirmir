# Behavior animation and source-rig calibration

The included Cat Walk asset has more facial controls than its bone names
initially suggest. Mirmir uses its existing skeleton and skin weights for
procedural actions layered over the credited authored idle/walk clips. These
are runtime adaptations, not newly downloaded or authored animation clips.
The original GLB is unchanged; [asset licenses](../../ASSET-LICENSES.md) apply.

## Anatomical controls

| Control | Original rig name | Evidence |
| --- | --- | --- |
| Neck/head chain | `cat_bip-Neck_7`, `cat_bip-Neck1_8`, `cat_bip-Head_9` | Joint hierarchy and skin weights |
| Lower jaw | `fst-Xtra01_16` | Lower-lip vertices are fully weighted to this bone |
| Tongue | `tongue_bone01_17` through `tongue_bone12_28` | Skinned tongue chain under the jaw |
| Upper eyelids | `Bone03_12`, `Bone02(mirrored)_14` | Face skin weights and overlap with the eye mesh |
| Lower eyelids | `Bone04_13`, `Bone04(mirrored)_15` | Face skin weights and overlap with the eye mesh |
| Ears | `cat_bip-Ponytail1_10`, `cat_bip-Ponytail2_11` | Ear geometry skin weights |

The source is `assets/personal-use/cat-walk-loop.glb`, SHA256
`1385db01e9bb3346abe3f7f45b2a5644f70b8133447bbd66d52dab6fd16759d3`.
A source inverse-bind matrix maps an observed upper-lip vertex into head-bone
space. With the installed glTF import conversion and component scale, its
Unreal bone-local coordinate is approximately `(-34.39959, 3.01320, 729.48403)`.
These are imported bone-local units, not a 7-meter world-space offset.
Actual world-space mouth/food distance must be measured after pose evaluation.

## Implemented approach

Feeding uses a raised bowl on a movable stand, bounded neck/head aiming,
supported body lean, planted paws, a small jaw cycle and tongue movement.
The bowl root stays on the floor for layout persistence; its rim is about
15 cm above that floor in the game's world units. A reachable target on the
near side avoids forcing a deep crouch into a floor-level bowl. State
transitions approach, settle, lower, lick and raise before travel resumes.
Tongue motion is a procedural deformation of the existing skin. It is not a
resolved tongue-to-kibble collision or a fluid/food-consumption simulation.

The rest base uses the credited `Tuxedo_SitDown`, `Tuxedo_IdleSit` and
`Tuxedo_StandUp` sequences. Head lowering, eyelid closure, tail relaxation and
breathing are procedural layers. The internal `Sleeping` behavior label
therefore means seated rest with closed eyes, **not an authored lying-down or
sleep clip**. A source-geometry eye-coverage check supports upper/lower lid
limits of 20/10 degrees; larger rotations can reopen gaps. That geometric
check is separate from actual rendered appearance, fur-shell coverage and
different head-scale profiles. A cooked build must demonstrate the actual
`SitDown` → `Sitting` → `StandUp` playback states; source references alone do
not prove those assets survived cooking.

The hind chain is `Thigh` → `Calf` → `Foot` → `Toe0` → `Toe01`. In this rig
`Foot` is an elevated hock, while `Toe0` and `Toe01` carry the ground-level
paw skin. At the normalized base scale, the four links measure approximately
13.91, 10.42, 9.05 and 2.06 cm. Earlier deep-crouch candidates held distal
points steady but produced visibly raised hind legs. Those runs demonstrate
why a small toe-point error is insufficient evidence of a natural pose.
The seated authored base avoids treating a standing ankle orientation as a
complete sleep pose.

Play uses an evaluated front-paw trajectory. Only measured world-space paw
position and velocity can request a ball impulse; an animation target alone
cannot hit the ball. The ball uses Chaos rigid-body physics. The cat remains
kinematic, with swept body movement and skeletal control rather than a full
physical animal simulation. Physics checks cover mass-dependent impulse,
contact range, cooldown, furniture-aware initial spawn and bounded motion.
An isolated Chaos fixture uses synthetic paw inputs; actual animated-paw RHI
observations are recorded separately. Count new paw-sample frame identities
and paired before/after velocity samples per play session, since a counter
or a previous impulse record can persist through later behavior states.

## Verification boundaries

Runtime metadata exposes action phases, muzzle/food/paw targets, evaluated
bone positions, a source-calibrated tongue-surface landmark and ball
impulse/velocity/contact samples. The tongue surface landmark is an existing
mesh-cap point expressed in `tongue_bone12_28` space, not that bone's origin.
Its original vertices blend roughly 90.2% bone 12 and 9.8% bone 11; independent
tip curling would require skin-weight evaluation rather than a rigid proxy.
Review actual RGBA
sequences alongside these values. Small numerical target error does not by
itself establish a natural-looking pose, and a successful action command does
not establish that its travel or animation completed. Release verification
must identify the exact source/binary and retain failed candidate runs.

## Final Shipping observation

The final 0.5 behavior record is tied to archive
`package-20261028T074553Z-1412355`, binary SHA256
`648617a616c3f144cc60c5a8a77a40d293f8bd55257da5a72d44b7a19a20d52a`.
The full Feed → Play → Rest → Feed → Play run exited normally and observed
all three authored sit/stand playback states. Three distinct evaluated-paw
sample identities produced paired ball-velocity changes in the two Play
sessions. A separate front-profile Shipping rest capture shows closed
eyelids. All public media uses these final Shipping proofs; the earlier
Development Editor/RHI trials remain calibration history.

The [public provenance record](../media/behavior-provenance.json) retains
counts, hashes and selected source metadata. The [contact sheet and original
RGBA files](../media/README.md) show the raised bowl, measured paw impulse and
seated doze. Near-rim lapping remains a procedural animation rather than a
food-contact solver. Closed eyes over a seated clip remain different from an
authored lying-down sleep animation.
