# Feeding, active play and resting animation polish

The user observed faulty feeding animation, incomplete ball play and incomplete
resting behavior in the actual desktop application. The 0.5 candidate is not
eligible for publication until these behaviors are improved and reviewed.
The existing authorization permits implementation without approval questions.
English repository documentation and actual preview media remain in scope.

## Feeding

Replace the fixed head-only rotation with a calibrated mouth target at the
food surface. Distribute the adjustment through the existing neck/head rig,
blend into a supported stance and use the actual tongue rig if its skin
weights and geometry permit a small licking motion. Stop the walking cycle
while eating. Approach, settle, eat and raise the head before moving away;
keep the body and paws outside the bowl. Do not invent an authored eating
clip or claim a jaw motion the asset cannot support.

## Play

The cat must act on the ball: approach, plant, reach a front paw, strike,
recover and follow. A physical impulse requires the actual evaluated paw
near the actual Chaos ball, a directed paw velocity and a cooldown. The ball
is never teleported after its initial safe spawn. Collisions, a bounded
impulse and maximum chase distance contain the session. Record contact,
impulse and before/after velocity independently of animation intent.

## Rest

Use a complete supported transition: enter the bed, settle the body and
front paws, lower the head, relax the tail and breathe, then rise before
walking away. Keep the existing authored idle/walk and layer procedural
poses on the calibrated skeleton. Do not label this as a new authored sleep
clip. Closed eyes require actual suitable geometry or rig support.

## Verification and delivery

Review actual Unreal RGBA sequences for every transition, not only activity
flags. Measure mouth/food distance, support/paw placement, paw/ball proximity
and physical velocity changes. Preserve furniture collision, desktop bounds,
frame/input pairing and normal shutdown. Rebuild Shipping and all installer
formats from the final source; retain earlier candidates as superseded.
Back up the latest cat, furniture and desktop anchor saves immediately before
cutover, because the user has moved the furniture during this session.
Publish the English README, real appearance photos and a clearly labelled
actual movement preview with source credits. No synthetic screenshots or
other apps' pixels are used.
