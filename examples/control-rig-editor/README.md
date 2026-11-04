# Control Rig Editor

Native C++ rig authoring with a CC0 KayKit knight and real AnimSkin deformation.

Run `make run/win32-debug GAME=examples/control-rig-editor` (Linux: `run/linux-debug`).

- Select hierarchy entries or drag cyan controls in the front viewport. Drag previews solve live; release creates one undoable edit.
- Inspector edits parent-local XYZ, local XYZ rotation deltas, and uniform scale. Auto key records at the playhead.
- Scrub/play the timeline; Set Key/Delete Key, Undo/Redo use native document transactions.
- Add a model-space target, then create an `aim` or `twoBoneIK` solve node. For the knight left arm use `upperarm.l`, `lowerarm.l`, `wrist.l`. Run it after `fk:wrist.l`. Nodes accept one prerequisite through this compact UI; the native document supports multiple dependencies. Graph cycles and invalid references are rejected.
- Reparent preserves initial model-space pose for an unanimated rig; animated space switching is explicitly rejected.
- Save/Load uses `knight.rig.json` in the native `fs.getSaveDirectory()` directory (shown in the status bar) and the `eve.control-rig` v2 codec (v1 migrates automatically). Bake + replay evaluates every frame into a native AnimClip and plays it through AnimPlayer; Edit rig returns to authoring. Key from baked pose reverse-matches the current playback pose into one undoable FK key batch.

The rig is initialized as FK controls from the imported skeleton. A three-key chest turn is included. Ordinary hot reload preserves document JSON and rebuilds the preview; undo history is session-local. The GUI is an example assembly; `ControlRigEditor` is UI-independent and reusable in games and custom tools.

Current transform contract: normalized quaternion, positive uniform scale; front-view dragging edits model XY. IK clamps unreachable targets to limb reach. Reverse matching is defined only for unique full-weight FK bindings. No UE asset or RigVM import is implied.

Script hot reload reuses the ECS-owned model parts and camera; dropping script
references does not remove those entities. `CONTROL_RIG_PREVIEW_ENTITIES` logs
their ID/generation pairs, which must remain unchanged across script reloads.
Restart the example when changing the model asset itself.

Character and texture: KayKit Adventurers, CC0; see `assets/LICENSE-KAYKIT-ADVENTURERS.txt`.

Set `EVE_CONTROL_RIG_PROOF=1` before running to capture `evidence/pose-0.png`, `pose-1.png`, `baked-1.png`, and `solve-graph.png` through the engine readback path. The mode checks save/restore, bake, backward pose matching, and undo through the same native SDK.


## Agent automation verification

Run `eve run --mcp-port=7549`, then `python agent_demo.py --port 7549`.
The example registers its live Rig; the client discovers its ID and command schema,
plans and commits a control/keyframe edit, tests UI undo and Agent redo, saves and
loads through the UI, tests Agent undo of that UI import, and rejects a missing bone.
It writes engine screenshots and `evidence/agent-result.json`. This deliberately
edits the open example and saves `knight.rig.json` in its configured save directory.
Native regression tests additionally cover solver-node changes, stale plans,
registration generations and both host/target destruction orders.

## MCP animation sequence demo

From this example directory, start `eve run --mcp-port=7549`. In another terminal,
run `python agent_sequence_demo.py --port 7549 --output ../../build/control-rig-sequence`.
Install Pillow to produce the GIF (`python -m pip install pillow`).

The client discovers the existing command schema, generates a four-second knight
arm gesture with chest/head motion (four tracks, 36 keys), and submits it through
`eve_editor_plan` / `eve_editor_commit`. It verifies that planning leaves the live
document unchanged and that one undo/redo restores the entire sequence, then
invokes the existing Bake + replay button through `eve_ui_click`. Native AnimClip
sampling and AnimPlayer playback both loop. No script evaluation is used.

Outputs include `greeting.rig.json`, the pre-edit `original.rig.json`, engine-owned
PNG frames, `greeting.gif`, and `sequence-result.json`. The client verifies changing
frame hashes and viewport pixels. The baked AnimClip is in memory; the reusable
saved asset is the Rig JSON, not an exported animation clip. The demo replaces
the open Rig's keys and duration and leaves the generated animation playing.

## Joint degrees of freedom and limits

Open **Control degrees of freedom / limits** in Solve. Each translation/rotation
axis and uniform scale supports free, locked, or limited. Scroll for all fields;
Apply commits one undo step. Hinge X locks translation, scale and the other two
rotation axes. Reference-relative rotation uses degrees; translation uses local
units. Extension projects onto the reference bone direction with a length ratio.
Invalid ranges reject atomically; valid out-of-range keys are projected.

Run `python agent_limits_demo.py --output ../../build/control-rig-limits` against
this example's MCP port to verify limits, UI/Agent shared history and baking.
See `docs/usr/modules/animation-control-rig.md` for the full coordinate contract.
