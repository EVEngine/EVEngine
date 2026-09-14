# Spec: Classical SMR-inspired skinned motion retarget

Date: 2026-09-14  
Status: Implemented (classical IK + optional neural MeshRet via animation_tensor)

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

Neural MeshRet path (optional L6 `animation_tensor`):

- Capability `ISmrNeuralRetarget` kept out of the L4 animation module; satellite
  registers it at boot and revokes on teardown.
- Builtin tensor MeshRet-style PointNet + Transformer graph (`SmrMeshRetNet`) for
  zero-weight bring-up; residual blend keeps untrained weights from destroying FK.
- ONNX runner (`SmrOnnxRunner`) accepts MeshRet-contract models exported by
  `scripts/export_meshret_onnx.py` (identity stub or trained weights).
- Profile: `setNeuralRetargetEnabled` / `setNeuralBackend("auto"|"onnx"|"tensor")` /
  `setNeuralModelPath`. Classical two-bone IK remains the fallback.

Still offline / research-only:

- Prefab MeshRet research weights and Mixamo/ScanRet training pipelines.
- Full mesh ray-cast SCS and LBS sensor skinning (optional follow-up via `AnimSkin`).

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
  - `setNeuralRetargetEnabled(bool)` — default `false`; requires `animation_tensor`
  - `setNeuralBackend("auto"|"onnx"|"tensor")` / `setNeuralModelPath(path)`
  - `getNeuralInferenceCount()` — diagnostics after a neural pass
- `AnimSmrSensorCloud::fromSkeleton(skeleton)` / `fromSkeletonDense(...)`
- `smrRefineRetargetedClip(...)` — C++ entry used by `retargetWithProfile`
- `ISmrNeuralRetarget` capability (`animation.ISmrNeuralRetarget`) — L6 provider
- `scripts/export_meshret_onnx.py` — MeshRet I/O contract identity stub exporter

## Tests

- Sensor cloud tags limb vs torso bones by name heuristics.
- Preserve hand–torso proximity across a short/tall retarget when SMR is on;
  FK-only retarget leaves a larger gap on the taller target.
- Profile diagnostics increment when corrections run.
- Neural: rot6d round-trip, feature batch shapes, tensor backend rewrites clip,
  classical fallback when neural is disabled.

## Architecture checklist

- Classical path stays inside `animation` (L4).
- Neural path is L6 `animation_tensor` (deps: animation + tensor) via capability.
- Fallible inputs keep existing `Exception` style of retarget APIs (same call
  path). Standalone cloud build returns empty rather than ambiguous `bool`.
- Deterministic: same skeletons/clip/profile → same baked keys.
- Docs: `docs/usr/modules/animation.md` + skeletal gap matrix updated.
