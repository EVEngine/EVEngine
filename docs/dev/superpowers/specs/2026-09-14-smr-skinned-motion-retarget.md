# Spec: Classical SMR-inspired skinned motion retarget

Date: 2026-09-14  
Status: Implemented (classical engine port; neural MeshRet/SMRNet deferred)

## Research summary

Public SMR / MeshRet work (IEEE TVCG SMRNet; NeurIPS 2024 MeshRet,
https://arxiv.org/abs/2410.20986) transfers motion while preserving **dense
geometric interactions** between body parts (contact + non-contact), not only
joint rotations.

Shared ideas that are portable without a Python/PyTorch runtime:

1. **Semantically consistent sensors (SCS)** — dense samples attached to bones
   (MeshRet casts rays from bone medial axes; we attach bind-local offsets to
   bones and tag them by body part).
2. **Dense mesh / sensor interaction field (DMI)** — pairwise relative positions
   between sensors on interacting parts (arm–torso, arm–arm, leg–leg, …).
3. **Interaction alignment** — retarget so the target’s interaction field stays
   close to the source’s (MeshRet uses a learned decoder + DMI loss; we use a
   classical two-bone IK correction after FK Avatar retarget).

What we deliberately do **not** ship in-engine yet:

- Transformer / PointNet weights, adversarial training, Mixamo/ScanRet datasets.
- Full mesh ray-cast SCS and LBS sensor skinning (optional follow-up via `AnimSkin`).

Those remain offline research tooling. This port keeps the runtime deterministic,
dependency-free, and scriptable.

## Engine integration

Extend existing offline retarget (`AnimRetargetProfile` +
`AnimClip::retargetWithProfile`):

1. FK Avatar retarget (unchanged).
2. Optional **SMR interaction pass** when
   `profile.setSkinnedInteractionPreserve(true)`:
   - Build sensor clouds for source/target skeletons.
   - Per baked frame: sample source + FK-retargeted target poses.
   - Capture source interaction pairs under a contact threshold.
   - For each pair whose tip bone has a configured (or auto-detected) IK chain,
     solve two-bone IK so the target tip approaches the scaled source-relative
     goal.
   - Rewrite rotation keys for chain bones; leave unrelated tracks alone.

## Public API

- `AnimRetargetProfile`
  - `setSkinnedInteractionPreserve(bool)` / getter
  - `setInteractionContactThreshold(float)` — meters in source bind space
  - `setInteractionCorrectionWeight(float)` — `[0,1]` IK blend
  - `addInteractionIkChain(root, mid, tip)` / `clearInteractionIkChains()`
  - `getInteractionCorrectionCount()` — diagnostics after retarget
- `AnimSmrSensorCloud::fromSkeleton(skeleton)` — testable sensor builder
- `AnimSmr::refineRetargetedClip(...)` — C++ entry used by `retargetWithProfile`

## Tests

- Sensor cloud tags limb vs torso bones by name heuristics.
- Preserve hand–torso proximity across a short/tall retarget when SMR is on;
  FK-only retarget leaves a larger gap on the taller target.
- Profile diagnostics increment when corrections run.

## Architecture checklist

- No new module; stays inside `animation`.
- No upward includes; no neural dependency.
- Fallible inputs keep existing `Exception` style of retarget APIs (same call
  path). Standalone cloud build returns empty rather than ambiguous `bool`.
- Deterministic: same skeletons/clip/profile → same baked keys.
- Docs: `docs/usr/modules/animation.md` + skeletal gap matrix updated.
