# House workshop and village

Run `make run/macosx-debug GAME=examples/building_village` (or the corresponding host target).
The example uses the runtime Building protocol, Map A* and Physics capsule queries; no editor
module or `eve_eval` gameplay path is required. Original bundled OBJ models provide window
walls, posts, chairs and roofs; see [asset conventions](assets/README.md).

## Build, reuse and edit houses

1. **Workshop**: choose a tool and left-click to build; right-click removes a part. **Floor +**
   cycles the edited level through 0–3. **Stairs** places a 2x4 staircase rising one level.
   R rotates by 90 degrees. Mouse picking intersects the selected floor plane.
2. **Capture + save blueprint** publishes the current workshop as the placement recipe and
   saves it to `saves/house-blueprint.json` in the application write directory. Startup creates
   an in-memory default only and never overwrites that file. **Load blueprint** restores saved
   parts into the workshop and selects the loaded placement recipe. Continue editing and capture
   again to publish changes. Invalid JSON, schema, dimensions or unsupported levels leave both
   the workshop and active recipe unchanged.
3. **Village**: place independent copies of the recipe. Right-click a floor or use
   **Select next house** to select an assembly.
4. **Remodel selected house** edits only that instance. All seven part tools remain available,
   even when the loaded recipe does not contain that part. Choose **Floor +** before editing
   upper floors. Moving/removing a house, Z/Y undo/redo and Save/Load preserve all its levels.

Z/Y and **Undo / Redo** operate on the current workspace: workshop history is independent of
village history. Blueprint import is undoable in the workshop. Undo changes the editable draft;
it does not rewrite the published recipe or its disk file. Capture again when ready to publish.
Histories are session-local; loading a village still clears its village undo history.

**Roof 4x3** adds a roof one level above the edited floor. Its entire 4x3 footprint (3x4 when
rotated) needs tagged supporting floors. Select Roof and right-click any covered cell to
remove it; other tools ignore hidden roofs. Stairs can also be removed anywhere in their
footprint with the Stairs tool selected. Leave an opening in the floor above a staircase:
covering the stair headroom makes the route unavailable.

**3D / Plan** switches presentation. Plan shows the edited level. **Cutaway / Full walls**
hides higher floors and lowers walls on the focused floor; it never changes collision.
During walking only the occupied house is automatically cut away, following the visitor's
actual floor. Other houses keep their exterior.

## Gravity and walking between floors

**Walk mode** enables click destinations on the edited floor. **Visit house** visits the
selected house on that floor. **Go up / Go down** requests the adjacent floor relative to
the visitor's current floor; **Outside** returns to the ground-level spawn.

W/A/S/D move along village axes (W = -Z, S = +Z, A = -X, D = +X), unless UI captures the
keyboard. Direction buttons apply the same input for 30 fixed ticks. Manual input cancels
the automatic route. **Reset visitor** explicitly resets to the spawn if its grid cell is clear.

Both automatic and manual movement use a 0.16 m radius, 1.3 m high capsule. Position and
height change only by resolved Physics movement. Gravity is -9.8 m/s² with a 20 m/s terminal
fall speed. The controller sweeps up/across/down to climb steps up to 0.24 m and follows
small descending steps; larger drops fall under gravity. Staircases have fifteen 0.2 m steps
and a 4 m run. Floors are real colliders, including the 0.1 m ground-floor threshold.

**Open / close door** remodels all doors in the selected house. Closing an exit stops the
route; opening it resumes planning. Moving/removing a house or loading a save rebuilds both
navigation and collision. Removed stairs disconnect floors. **Remove stairs** and **Drop
floor** are undoable demonstrations: Drop floor removes the upper-floor tiles underneath
the visitor, who falls to the next remaining support. Undo reconstructs the floor without
teleporting the visitor back up.

A new collider overlapping the visitor stops movement and reports the obstruction; undo or
Reset resolves it. A clear position near a wall can reconnect to the conservative navigation
grid through a physically checked connector. Falling itself continues when a route is absent.

## Repeatable demonstrations

- **Test resume** needs a fresh example with an empty village and the default workshop. It
  edits a part, verifies workshop undo/redo, saves to a dedicated test file, imports it, rejects
  malformed/version/bounds/level failures without mutation, edits again, places independent
  copies, adds a part absent from the recipe, restores the village file and walks inside.
  Success prints `building_village.workshop resume checks PASS` and
  `building_village.workshop journey PASS`. It uses `saves/workshop-check*.json` and does not
  overwrite the user's blueprint or village save.

- **Run complete workflow demo** needs an empty village. It checks blueprint disk round-trip,
  two independent houses, remodel isolation, roof support rejection, roof removal/undo,
  world disk round-trip and undo/redo. Success prints `building_village.workflow PASS`.
- **Test navigation** uses the bundled open-door single-floor recipe and checks four rotations,
  furniture clearance, removed targets, closed doors and restored door state, then actually
  walks inside. Success prints `building_village.navigation PASS`.
- **Test collision** uses that same fixture and checks real sweeps against walls, doors,
  rotated furniture, sliding, a 20 m boundary sweep and invalid-candidate cleanup, then walks.
- **Test floors** builds a three-storey recipe at (12,11), which must be unoccupied. Before
  publishing it, a separate Building world checks four rotations, missing stairs, low ceiling
  rejection and restore. The visitor then climbs two staircases to level 2 (about 6.1 m) and
  walks back outside. Success prints `building_village.multilevel topology PASS`,
  `building_village.multilevel ascent PASS` and `building_village.multilevel PASS`.
  The recipe is saved through the same blueprint codec and can be copied into the village.

Tests use the bundled fixture shapes; arbitrary remodels may intentionally make routes
unreachable. Existing houses are not deleted to make room for a demonstration.

## Ownership, scope and automation

Building is the only owner of building geometry, membership, level and door state.
`multilevel.nut` derives per-floor 0.25 m four-neighbor grids and stair portals. Same-floor
paths use native Map A*; Dijkstra combines them across stairs. Upper grids require floor
support and conservative clearance. Portal checks require clear landings and headroom.
`collision.nut` builds a candidate static world from the same geometry; successful publication
replaces both projections and destroys the old world. Failed projection stops movement.
`gravity.nut` owns transient visitor motion at 60 Hz with at most 0.1 s catch-up per frame.
Bodies/shapes are not cached across rebuilds. Render meshes never supply navigation facts.
`workshop.nut` reuses one builder-owned staging world for blueprint admission. Native placement
validates it, assembly membership is detached in an owning snapshot, then native atomic restore
publishes loose editable parts. No new save schema or per-load native worlds are introduced.
The part-tool cache is initialized from native instances and remains a read-only template.

Supported scope: this 26x20 XZ village, 1 m cells, 3 m floor spacing, levels 0–3, quarter-turn
stairs and zero elevation offsets. The `map` and `physics` modules are required. Window walls,
furniture and roof undersides use conservative colliders. There is no jumping, moving-body
interaction, load-bearing simulation, crowd avoidance or skeletal animation. Physics uses
Box3D's 0.005 m linear slop; tests allow additional floating-point tolerance. Cross-backend
bitwise determinism is not claimed. Visitor state is transient and excluded from village saves.

World saves retain door state; blueprint capture omits per-instance properties, so new houses
start with open doors. The existing versioned Building schema stores floor levels and stairs;
no separate navigation or collision save format is introduced.

`game.agent.json` declares UI-equivalent actions and the read-only `visitor` observation,
including x/y/z, level, grounded, velocityY, verticalTravel, stairSteps and multiTest. Start
with `--debug --mcp-port=17540`. Play Host requests require
`schemaId: "evengine.play-request"`, `schemaVersion: 1`, `op: "observe"`,
`observation: "visitor"`. Observations are never read back into simulation; Play Host
checkpoints do not replace the Building Save/Load commands.

Remaining workflow limits: Move still uses a reserved demo destination rather than mouse-driven
relocation; there is one saved blueprint slot, no blueprint library; unsaved drafts and undo
histories do not survive process exit. These are separate from the validated save/import/play loop.
