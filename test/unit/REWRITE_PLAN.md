# test/unit systematic rewrite plan

Scope: every `.cpp` directly or recursively under `test/unit` at baseline. Original tests are reference material only.

A checkbox is complete only when the test source has been reorganized from the API contract and implementation into a coherent behavior specification. Each case names an interface and its expected observable result for representative inputs, boundaries, invalid inputs, and state/lifecycle as applicable. Remove redundant checks and expensive demonstration cases. Group cheap variants of one interface with `SUB_CASE` so CTest launches fewer processes; keep expensive GPU/window cases isolated where sharing a process increases cost or couples state. Reusing old test organization, renaming tests, adding comments, or appending isolated assertions does not qualify. Record verification evidence and any runtime limitation. Confirmed or code-path defects go in `BUGS_FOUND.md` with reproducer, expected/actual behavior, and source location.

Include and origin audit: remove imports unused by the rewritten tests. `EVE_TEST_FILTER_ORIGINS` guards survive only where the source catalogue maps one merged translation unit to independently selected original sources for profile/standalone builds (`cmake/TestLevels.cmake`). Audit every surviving guard against that mapping; it is a build selection mechanism, not a way to organize behavior cases.

Final origin audit: 41 C++ files retain guards; all 41 map to multiple original sources in their `eve-test-policy` source headers, and every surviving origin macro matches a declared origin. The single-origin guard in `gpgpu_tests.cpp` was removed; the recipe/workshop cases in devtools share the `mcp_whitebox` origin. Includes were reviewed during each file rewrite.

Final structure on current `dev`: 133 source files (3528 `TEST_CASE` registrations) map to 119 unit sources (1376 registrations) after cohesive merges; 67 sources use `SUB_CASE` (1120 subcases total) to document cheap input/output variants within one process. This reduces process-level unit registrations by 61% while retaining separately registered expensive GPU and window cases for process isolation. The test catalogue records 1377 unit identities overall (including one source outside `test/unit`) and 3830 integration identities across 326 C++ test sources. The 133rd file, `graphics_damage_numbers.cpp`, arrived on `dev` after the initial 132-file inventory and was rewritten into the consolidated graphics contract source during PR preparation.

Verification status (2026-10-25): all 132 original entries have source/API review and static `test_levels.py` validation; the newly merged damage-number file was also reviewed and rewritten. Before rebasing, the Windows debug build linked all 28 domain test executables. `make check` passed, including 494 script tests (3 skipped), the source catalogue, and the assertion-level gate. In eight targeted domains, 1059 of 1061 independent cases passed after the corrected vehicle script call was rerun; the two remaining red cases reproduce the melee event and RPG current-HP defects in `BUGS_FOUND.md`. Rebased `dev` checks are run separately before this PR is pushed. This is targeted Windows verification, not a claim that the full several-thousand-case suite was run locally.

| Worker | Files | Baseline lines |
| --- | ---: | ---: |
| 1 | 26 | 22537 |
| 2 | 27 | 22559 |
| 3 | 27 | 22553 |
| 4 | 26 | 22546 |
| 5 | 26 | 22544 |

## Added on dev during PR preparation

- [x] `test/unit/graphics/graphics_damage_numbers.cpp` (merged into `test/unit/graphics/graphics_pipeline_state_tests.cpp`; three interface contracts reorganized into six input/output subcases)

## Worker 1

- [x] `test/unit/cmdline/cmdline_unit.cpp` (366 baseline lines)
- [x] `test/unit/combat/combat_tests.cpp` (947 baseline lines)
- [x] `test/unit/data/data.cpp` (407 baseline lines)
- [x] `test/unit/decal/decal_unit.cpp` (473 baseline lines)
- [x] `test/unit/devtools/devtools_tests.cpp` (2626 baseline lines)
- [x] `test/unit/emergence/emergence_perf_unit.cpp` (296 baseline lines)
- [x] `test/unit/filesystem/filesystem_tests.cpp` (901 baseline lines)
- [x] `test/unit/fluids/fluids_emitters_coupling_tests.cpp` (3009 baseline lines) (merged into `test/unit/fluids/fluids_tests.cpp`)
- [x] `test/unit/fluids/ocean_codec.cpp` (128 baseline lines) (merged into `test/unit/fluids/fluids_tests.cpp`)
- [x] `test/unit/fluids/ocean_spray.cpp` (179 baseline lines) (merged into `test/unit/fluids/fluids_tests.cpp`)
- [x] `test/unit/fluids/ocean_surface.cpp` (85 baseline lines) (merged into `test/unit/fluids/fluids_tests.cpp`)
- [x] `test/unit/fluids/ocean_transition.cpp` (139 baseline lines) (merged into `test/unit/fluids/fluids_tests.cpp`)
- [x] `test/unit/gpgpu/gpgpu_tests.cpp` (55 baseline lines)
- [x] `test/unit/graphics/graphics_pipeline_state_tests.cpp` (4988 baseline lines)
- [x] `test/unit/image/image_tests.cpp` (523 baseline lines)
- [x] `test/unit/particles/particles_tests.cpp` (1824 baseline lines)
- [x] `test/unit/physics_backend/physics_core_boundary_unit.cpp` (135 baseline lines)
- [x] `test/unit/physics_destruction_cook/physics_destruction_cook.cpp` (174 baseline lines)
- [x] `test/unit/pixelworld_replay/pixelworld_replay.cpp` (234 baseline lines)
- [x] `test/unit/procgen/procgen_road_junction_seams.cpp` (276 baseline lines)
- [x] `test/unit/procgen/procgen_road_terrain_blend.cpp` (45 baseline lines)
- [x] `test/unit/procgen/procgen_road_world_origin.cpp` (214 baseline lines)
- [x] `test/unit/procgen/procgen_urban_tests.cpp` (2972 baseline lines)
- [x] `test/unit/procgen/terrain_object_collision.cpp` (90 baseline lines)
- [x] `test/unit/rx/rx_unit.cpp` (318 baseline lines)
- [x] `test/unit/settlement/settlement_simulation_rules.cpp` (1133 baseline lines)

## Worker 2

Checked entries were rewritten against their API and implementation and passed source inventory and `git diff --check` on 2026-10-25. Compiled runtime execution remains pending.

Origin guards remain in merged sources whose separate catalogue origins are selected by `cmake/TestLevels.cmake` for profile/standalone builds. The single-origin editing test's guard was removed. Repeated `PrivateTestScope.h` includes isolate Windows near/far macros between those selected sections.

- [x] `test/unit/action/action_tests.cpp` (1126 baseline lines)
- [x] `test/unit/archspace_hd2d/archspace_hd2d_projector.cpp` (35 baseline lines)
- [x] `test/unit/attributes/attributes.cpp` (85 baseline lines)
- [x] `test/unit/authority/authority_tests.cpp` (317 baseline lines)
- [x] `test/unit/avatar/avatar_tests.cpp` (843 baseline lines)
- [x] `test/unit/dnut_interpreter/dnut_interpreter.cpp` (387 baseline lines)
- [x] `test/unit/editing/editing_tests.cpp` (59 baseline lines)
- [x] `test/unit/fluids/ocean_mesh.cpp` (124 baseline lines) (merged into `test/unit/fluids/fluids_tests.cpp`)
- [x] `test/unit/fluids/ocean_spectrum.cpp` (171 baseline lines) (merged into `test/unit/fluids/fluids_tests.cpp`)
- [x] `test/unit/graphics_raytracing/graphics_raytracing_unit.cpp` (30 baseline lines)
- [x] `test/unit/inventory/inventory_tests.cpp` (942 baseline lines)
- [x] `test/unit/math/math_tests.cpp` (543 baseline lines)
- [x] `test/unit/model3d/model3d_normals.cpp` (134 baseline lines)
- [x] `test/unit/policyregistry/policyregistry_unit.cpp` (185 baseline lines)
- [x] `test/unit/procgen/procgen_arrow.cpp` (245 baseline lines)
  - [x] `test/unit/procgen/procgen_composition_tests.cpp` (2840 baseline lines)
- [x] `test/unit/procgen/procgen_spline_explicit_frames.cpp` (87 baseline lines)
- [x] `test/unit/procgen/procgen_spline_path.cpp` (293 baseline lines)
  - [x] `test/unit/procgen/procgen_terrain_tests.cpp` (4848 baseline lines)
- [x] `test/unit/rts/gameplay_control_rts.cpp` (199 baseline lines)
- [x] `test/unit/scene/scene_tests.cpp` (1725 baseline lines)
- [x] `test/unit/sensing/sensing_unit.cpp` (410 baseline lines)
- [x] `test/unit/social/social_unit.cpp` (147 baseline lines)
- [x] `test/unit/tactics/tactics_tests.cpp` (3438 baseline lines)
- [x] `test/unit/tags/tags_tests.cpp` (270 baseline lines)
  - [x] `test/unit/voxel/voxel_tests.cpp` (2618 baseline lines)
- [x] `test/unit/weather/weather_tests.cpp` (458 baseline lines)

## Worker 3

  Checked entries below were rewritten against their API and implementation and
  passed source inventory and whitespace checks on 2026-10-25. Windows compilation
  and runtime execution remain pending the aggregate build and test pass.

- [x] `test/unit/action_input/action_combo_windows.cpp` (82 baseline lines)
- [x] `test/unit/animation/animation_tests.cpp` (4668 baseline lines)
- [x] `test/unit/buildingfx/building_placement_regression_unit.cpp` (137 baseline lines)
- [x] `test/unit/card/card_tests.cpp` (913 baseline lines)
- [x] `test/unit/daynight/daynight_tests.cpp` (272 baseline lines)
- [x] `test/unit/definitions/definitions_unit.cpp` (174 baseline lines)
- [x] `test/unit/dialogue/dialogue_tests.cpp` (1464 baseline lines)
- [x] `test/unit/economy/economy_unit.cpp` (773 baseline lines)
- [x] `test/unit/effects/effects_unit.cpp` (293 baseline lines)
- [x] `test/unit/graphics/graphics_shader_parameters.cpp` (17 baseline lines) (merged into `test/unit/graphics/graphics_pipeline_state_tests.cpp`)
- [x] `test/unit/ik/ik_unit.cpp` (338 baseline lines)
- [x] `test/unit/map/map_tests.cpp` (2588 baseline lines)
- [x] `test/unit/pixelworld_streaming/pixelworld_script.cpp` (308 baseline lines)
- [x] `test/unit/platform_event/async_unit.cpp` (56 baseline lines)
- [x] `test/unit/procgen/procgen_auto_bridge.cpp` (225 baseline lines)
  - [x] `test/unit/procgen/procgen_foundation_tests.cpp` (3705 baseline lines)
- [x] `test/unit/procgen/procgen_generation_stages.cpp` (233 baseline lines)
- [x] `test/unit/procgen/procgen_road_geometry_artifact.cpp` (94 baseline lines)
- [x] `test/unit/procgen/procgen_road_terrain_opacity.cpp` (167 baseline lines)
- [x] `test/unit/scene_editor/scene_editor_gizmo.cpp` (44 baseline lines)
- [x] `test/unit/schema/schema_tests.cpp` (402 baseline lines)
- [x] `test/unit/statepatch/statepatch_unit.cpp` (140 baseline lines)
- [x] `test/unit/tensor/tensor_tests.cpp` (1573 baseline lines)
- [x] `test/unit/timer/service_mocks_unit.cpp` (125 baseline lines)
- [x] `test/unit/transaction/transaction_unit.cpp` (435 baseline lines)
- [x] `test/unit/ui/ui_tests.cpp` (2824 baseline lines)
- [x] `test/unit/vehicle/vehicle_unit.cpp` (503 baseline lines)

## Worker 4

- [x] `test/unit/agent/agent.cpp` (252 baseline lines)
- [x] `test/unit/archspace/archspace_space_query.cpp` (155 baseline lines)
- [x] `test/unit/archspace_map/archspace_map_projector.cpp` (63 baseline lines)
  - [x] `test/unit/asset/asset_tests.cpp` (1002 baseline lines)
- [x] `test/unit/climbing/climbing_tests.cpp` (1031 baseline lines)
- [x] `test/unit/fluids/fluids_shore_tests.cpp` (103 baseline lines) (merged into `test/unit/fluids/fluids_tests.cpp`)
- [x] `test/unit/fluids/fluids_solver_tests.cpp` (4158 baseline lines) (merged into `test/unit/fluids/fluids_tests.cpp`)
- [x] `test/unit/game_event/game_event_tests.cpp` (280 baseline lines)
  - [x] `test/unit/graphics/graphics_resources_tests.cpp` (835 baseline lines) (merged into `test/unit/graphics/graphics_pipeline_state_tests.cpp`)
- [x] `test/unit/graphics/virtual_texture_streaming.cpp` (133 baseline lines) (merged into `test/unit/graphics/graphics_pipeline_state_tests.cpp`)
- [x] `test/unit/i18n/i18n_unit.cpp` (299 baseline lines)
- [x] `test/unit/material_editor/material_editor_tests.cpp` (112 baseline lines)
- [x] `test/unit/orders/orders_unit.cpp` (168 baseline lines)
- [x] `test/unit/physics/physics_tests.cpp` (2793 baseline lines)
  - [x] `test/unit/physics_cloth/physics_cloth_unit.cpp` (398 baseline lines)
- [x] `test/unit/physics_destruction/physics_destruction_fracture_rules.cpp` (198 baseline lines)
  - [x] `test/unit/physics_softbody/softbody_unit.cpp` (423 baseline lines)
- [x] `test/unit/procgen/procgen_cave_formations.cpp` (1947 baseline lines)
- [x] `test/unit/procgen/procgen_everyday_materials.cpp` (227 baseline lines)
- [x] `test/unit/procgen/procgen_mesh_tests.cpp` (2085 baseline lines)
- [x] `test/unit/procgen/procgen_road_circular_ghost.cpp` (69 baseline lines)
  - [x] `test/unit/production/production_tests.cpp` (715 baseline lines)
- [x] `test/unit/profiler/profiler_unit.cpp` (55 baseline lines)
- [x] `test/unit/rpg/rpg_tests.cpp` (4521 baseline lines)
- [x] `test/unit/spatial/spatial_unit.cpp` (187 baseline lines)
  - [x] `test/unit/virtualgeometry/virtualgeometry_builder_unit.cpp` (337 baseline lines)

## Worker 5

- [x] `test/unit/asset_graphics/asset_graphics_material_color.cpp` (203 baseline lines)
- [x] `test/unit/building/building.cpp` (2064 baseline lines)
- [x] `test/unit/camera/camera.cpp` (450 baseline lines)
- [x] `test/unit/common/common_tests.cpp` (4470 baseline lines)
- [x] `test/unit/crowd/crowd_tests.cpp` (1058 baseline lines)
- [x] `test/unit/editor/editor_tests.cpp` (404 baseline lines)
- [x] `test/unit/fluids/ocean_hull_mask.cpp` (68 baseline lines) (merged into `test/unit/fluids/fluids_tests.cpp`)
- [x] `test/unit/fluids/ocean_wake.cpp` (358 baseline lines) (merged into `test/unit/fluids/fluids_tests.cpp`)
- [x] `test/unit/graphics_fog/graphics_fog_unit.cpp` (261 baseline lines)
- [x] `test/unit/grid/grid.cpp` (160 baseline lines)
- [x] `test/unit/hexmap/hexmap_tests.cpp` (2718 baseline lines)
- [x] `test/unit/npc_ai/npc_ai_tests.cpp` (581 baseline lines)
- [x] `test/unit/physics_rope/physics_rope_unit.cpp` (246 baseline lines)
- [x] `test/unit/pixelworld/pixelworld_tests.cpp` (861 baseline lines)
- [x] `test/unit/procgen/procgen_points_tests.cpp` (4340 baseline lines)
- [x] `test/unit/procgen/procgen_road_bundled_circular.cpp` (116 baseline lines)
- [x] `test/unit/procgen/procgen_road_geometry_sources.cpp` (101 baseline lines)
- [x] `test/unit/procgen/procgen_road_placement_anchors.cpp` (332 baseline lines)
- [x] `test/unit/procgen/procgen_road_profiles.cpp` (129 baseline lines)
- [x] `test/unit/procgen/procgen_road_recipe_artifact.cpp` (195 baseline lines)
- [x] `test/unit/procgen/procgen_road_roll_policy.cpp` (64 baseline lines)
- [x] `test/unit/procgen/procgen_road_roundabout.cpp` (53 baseline lines)
- [x] `test/unit/procgen/procgen_road_terrain_batch.cpp` (158 baseline lines)
- [x] `test/unit/procgen_animation/pcg_biome_controller_unit.cpp` (291 baseline lines)
- [x] `test/unit/stylize/stylize_tests.cpp` (1899 baseline lines)
- [x] `test/unit/weapon/weapon_tests.cpp` (964 baseline lines)
