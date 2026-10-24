# Recovery and verification

This is a reconstruction after the old workspace was reset. Original commits
a09bb50d8 and 73b585f5d were not found in the current object store or on GitHub.
Original raw logs and preview could not be recovered. No old measurement is
presented as evidence for this code.

The intended checks are:
1. A* and flow-field reachability on continuous terrain; 1 km CPU navigation case.
2. Physical uphill restriction, downhill/lateral movement and policy override.
3. A 120-second rendered scene run with diagnostic and memory monitoring.

Current runs and their exact limitations will be recorded below. A finite scene
does not establish infinite-world streaming or production reliability. Earlier
conversation records mention eight terrain-suite assertion mismatches; their raw
logs are unavailable, so their current status is unknown.

## CI startup repair, 2026-10-24

Reproduced the Linux example-smoke failure: Squirrel has no global min/max here,
and UI exposes mountBuildAs rather than endBuild. The example now uses local
numeric helpers and explicitly mounts its built UI. Reduced the downward
contact probe from 0.04 m to 0.01 m per fixed step: the former projected the
steep actor's intended uphill motion downhill, leaving it at the slope foot.
The maximum slope limits themselves remain 35 and 75 degrees.

Fresh validation with the local software-Vulkan engine:
- CI's smoke_examples.sh: alive for 15 seconds, zero error markers (PASS).
- verify.py: 120.09 seconds, both distinct actors arrived, ongoing simulation,
  no script diagnostics, process survived (all five checks PASS).
- Warmed process-group RSS grew 7580 KiB in that run; this is an observation,
  not a general memory bound.
- make check CI_BASE=origin/dev: PASS, 490 script tests, 3 skipped.

See evidence/runtime-summary.json, evidence/runtime.txt and evidence/smoke.txt.
The standalone verifier now catches eve_init failed/frame error/Run failed and
resolves its default engine path relative to the example directory.

This change only repairs scripts and their verifier. The PR's existing CI run
passed source quality and Linux native tests before failing example smoke;
the new commit still requires its own CI run. The local binary was reused;
no claim is made that every platform or unrelated example was rerun locally.
