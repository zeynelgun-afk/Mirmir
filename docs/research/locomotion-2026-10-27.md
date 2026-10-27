# Locomotion research and implementation decisions

Research date: 2026-10-27. These are primary sources and pinned repository
revisions. A reference is not an integrated dependency or a claim that its
results have been reproduced in Mirmir.

## Diagnosed failure

The existing actor reduces forward speed to zero for a target behind its
heading, while independently rotating at up to 90 degrees/second. The walk
rate is correctly tied to forward speed, so its feet freeze while the actor
still pivots. Smoothing that rotation alone cannot fix sliding contacts.

The next implementation couples angular velocity to forward speed and path
curvature, bounds angular and lateral acceleration, and uses a curved approach
for targets behind the cat. An additive, bounded contact correction is
evaluated against the actual four leg chains and authored walk. A purely
kinematic constraint does not establish force-driven muscle simulation.

## Engine and academic sources

| Source | Relevant finding | Decision for this project |
| --- | --- | --- |
| [Epic: Pose Warping](https://dev.epicgames.com/documentation/en-us/unreal-engine/pose-warping-in-unreal-engine) | Orientation, stride and slope warping depend on explicit motion and bone definitions; the graph workflow expects root motion and IK setup. | The current in-place feline clip cannot acquire correct contacts merely by enabling a humanoid orientation node. |
| [Epic: Procedural Animation with a Locomotor](https://dev.epicgames.com/community/learning/tutorials/EkxO/unreal-engine-procedural-animation-with-a-locomotor) | A quadruped can use explicit limb targets and whole-body corrections in Control Rig. | Reference for contact-aware motion, rather than replacing the cat with a robot rig. |
| [Martin and Neff: Interactive Quadruped Animation, MiG 2012](https://web.cs.ucdavis.edu/~neff/papers/CAT_MiG.pdf) | Cat motion uses body trajectories, footprint planning and leg IK, with head/tail controls layered separately. | Couple body path and paw support; keep small head attention independent. This paper is a kinematic animation system, not evidence of full dynamic cat physics. |

## Similar GitHub projects inspected

| Project and inspected revision | Useful idea | Reuse boundary |
| --- | --- | --- |
| [TempoQuadruped](https://github.com/tempo-sim/TempoQuadruped/tree/24637981480dbf4fe49f4e6b988cc4f570e97f3a) | Unreal quadruped example driven by Locomotor/Full Body IK and a movement target. | Apache-2.0 at that revision. Robot content and its model are not copied. |
| [AI4Animation](https://github.com/sebastianstarke/AI4Animation/tree/98aff6c87d73e4af0cbf50df2586f58532475662) | Mode-adaptive quadruped motion research and local contact/phase methods show why a single forward clip has limited turn coverage. | Its README restricts code redistribution; neither code nor mocap is imported. This project does not run its neural controller. |
| [SixOfFive/DesktopPet](https://github.com/SixOfFive/DesktopPet/tree/4eeb333e1866f52163af1ea2f9c02d499141ddce) | Offscreen 3D rendering into a per-pixel Windows desktop overlay; cursor curiosity and ball interaction. | GitHub reports no detected license at the inspected revision. Architecture reference only; no code or assets copied. |
| [virtual-pet](https://github.com/gabrielquilice/virtual-pet/tree/30590ee0d29a4f5df430b7d7062b2d3d9bcefed0) | Transparent PyQt shell, one-instance behavior, XWayland and Windows packaging separation. | GPL-3.0 reference. No copied implementation; this remains a real Unreal renderer, with no sprite fallback. |

Cursor interest is intermittent and bounded. Only the latest cursor offset is
exchanged locally; no desktop image, foreground window title or keyboard input
is needed. Platform packaging and documentation must distinguish built files
from OS installation/runtime validation.

## Acceptance evidence

Evaluate real Unreal output: heading continuity, stationary pivot count,
speed-to-animation coupling, curvature and yaw acceleration, four paw paths
through a turn, alpha/CRC frame integrity and stop/restart persistence. Keep
original unaltered render frames and report remaining contact errors. A green
unit test or a generated installer script is not visual or platform proof.

## 0.4 measured follow-up

The stationary-pivot failure was observed in 120 intervals of the old natural
90-second run (4.14 seconds). The new controlled 13.93-second turn run recorded
zero such intervals and a minimum measured radius of about 50 cm. These runs
have different paths/durations, so their peak yaw values are snapshots rather
than a general improvement percentage.

The first IK A/B exposed a real defect: full-stance proxy drift decreased, but
quickly returning up to 4 cm of correction during release caused large velocity
spikes. 0.4 bounds the correction velocity and releases it through swing. Its
298 context-matched intervals retained near-zero median full-stance proxy
drift; release speeds still exceeded the uncorrected source on some feet. The
observed release maxima were roughly 22/20/45/51 cm/s across the four feet.
This is partial correction, not zero-slip foot physics. Preserve this limit
when evaluating the result.

The habitat RHI probe separately demonstrated bed arrival, procedural resting,
feeding arrival and Play remaining active after Pet. The independent native
bug sending Rest when a playing window reached the screen edge was removed.
See [release verification](../release-0.4-verification.json) for unaltered image
hashes, measured results and platform/locked-session limits.
