# 单元测试修剪日志（逐文件）

> 状态：进行中；日期：2026-08-18
> 方法：逐文件通读每个 `TEST_CASE`，按“是否增加覆盖”决定保留/合并/删除；
> 合并时保留原测试的全部断言，删除仅限被其它用例完全覆盖或并入合并用例的断言。
> 目标：减少用例数（进程/CTest 开销）且不损失代码覆盖。

## 修剪准则

1. **删除**：断言已被另一用例（或合并后的用例）完全覆盖的用例。
2. **合并**：同一函数/同一行为族的参数变体测试，合并为一个带分块的 `TEST_CASE`，
   保留全部断言（如 PackedRect 打包、decode 平面/UV、Frustum 相交、单方块网格）。
3. **拆分**：一个用例内混杂多个无关行为时拆成独立用例（当前批次暂未发现需要拆分的）。
4. 资产依赖不同的用例（有 CesiumMan / 无资产）保留各自的覆盖路径，不强行合并。

## 已处理文件

### test/voxel.cpp：134 → 90（-44）

| 合并/删除的用例 | 去向 |
| --- | --- |
| `voxel.pack.bit_isolation`、`max_fields`、`width_height_min`、`tex_field_isolates_from_xyz`、`masks_oob_inputs`、`module.pack_clamps_via_mask`、`module.pack_unpack_roundtrip_api`、`roundtrip_randomish_grid` | 并入 `voxel.pack.roundtrip` |
| `greedy.single_voxel_exactly_six_1x1`、`greedy.max_tex_and_corner_voxel`、`mesher.meshChunk_single_voxel` | 并入 `voxel.greedy.single_voxel` |
| `greedy.meshFace_only_one_direction`、`greedy.meshFace_appends_not_clears`、`greedy.meshChunk_appends_cleared_per_face`、`module.meshChunk_multi_textures`、`module.meshChunk_keeps_distinct_textures` | 并入 `voxel.mesher.api_behavior` |
| `frustum.large_aabb_containing_camera`、`frustum.rejects_fully_left`、`frustum.rejects_above_and_far`、`frustum.tiny_aabb_on_axis` | 并入 `voxel.frustum.intersects` |
| `decode.winding_matches_outward_normal`、`all_face_planes`、`posX_plane_and_extent`、`negX_extent`、`posZ_and_negY_extents`、`negZ_extent_and_winding`、`both_triangles_match_normal`、`all_dirs_both_triangles`、`max_extent_rect` | 并入 `voxel.decode.planes_extents_winding` |
| `decode.atlas_uv_for_tex_index`、`uv_tilesPerRow_one`、`uv_corners_monotonic_in_tile`、`uv_grid_for_many_tex_indices`、`uv_tex127_tiles16`、`uv_tilesPerRow_negative_clamps` | 并入 `voxel.decode.uv_tiling` |
| `decode.chunk_origin_offsets_corners`、`decode.negative_chunk_origin` | 并入 `voxel.decode.chunk_origin_offsets` |
| `world.visible_batch_packed_nonnull`、`world.visible_rect_count_equals_sum_batches` | 并入 `voxel.world.visible_batches_decode_on_chunk_origin` |
| `chunk.ensureMeshed_idempotent`、`chunk.fill_dirty_clear_dirty` | 并入 `voxel.chunk.empty_and_dirty_flags` |
| `world.remeshDirty_zero_when_clean` | 并入 `voxel.world.remeshDirty_counts_only_dirty` |
| `world.removeChunk_then_get_air` | 并入 `voxel.world.negative_coords_and_remove` |
| `registry.faceTex_zero_is_valid_tile`、`world.empty_registry_keeps_id_as_tex` | 并入 `voxel.registry.add_and_lookup` |
| `world.eye_on_posX_keeps_posX_drops_negX`、`world.eye_on_posY_keeps_posY_drops_negY`、`world.face_cull_from_negZ` | 删除（`face_cull_all_six_axes` 已覆盖六个方向） |

保留：各类贪心合并形状（L 形/环/棋盘/隧道/台阶等）、cross-chunk/负坐标、visible 范围过滤、
注册表零纹理等各有独立断言的用例。

### test/particles_attach_skin.cpp：11 → 8（-3）

| 合并/删除的用例 | 去向 |
| --- | --- |
| `attach.followRotationOffKeepsDirection` | 并入 `attach.followRotation`（同旋转矩阵，开关两态） |
| `attach.yzPlaneAndDetachClearsKind` | yz 平面映射与 attach kind 断言并入 `attach.boneFollowsPose` |
| `skin.skinnedCacheReadable` | 缓存可读/isfinite 断言并入 `skin.surfaceEmitCesiumMan` |

保留：`byNameAndOffset`、`emitsAtBone`、`animPoseDynamicEmitAcrossFrames`（无资产路径）、
`clipSampleMovesEmitter`（真实动画资产路径）、`emptyBoneFilterFallsBack` 等。

### test/particles_attach_more.cpp：20 → 20（通读后无重复）

每个用例覆盖不同行为：发射器寿命、缓冲上限、重绑定骨骼、缩放 0/负、停止/启动保留附着、
发射区域、随骨旋转的发射速度、径向力随动原点、detach 后骨骼索引、姿态不变不漂移、
Spine 旋转父级偏移、IK2D 求解追踪、IK3D 偏移投影、按名重附着、预设保留附着、emit(0/负)、
皮肤平面 xz/yz、清皮肤回退骨骼原点、手动 syncAttach、followRotation 切换重同步。
这些不是同一断言的参数变体，删/并都会损失覆盖，保留。

## 待处理文件（按优先级）

1. `particles.cpp` + `particles_attach_more/extra/dynamic_bones`（109 用例，附着族）
2. `callgraph.cpp` / `renderflow.cpp` / `debugger.cpp` / `debugger_audit.cpp`（devtools 环缓冲族）
3. `animation.cpp` / `animation_mixamo` / `animation_skinned` / `animation_sprite_spine`
4. `map.cpp` / `map_path.cpp` / `map_fov.cpp`
5. `procgen.cpp` / `procgen_simulation.cpp` / `roguelike_generator.cpp`
6. `hex_level_data.cpp` / `hex_level_simulation.cpp`
7. `rx.cpp` / `rpg.cpp` / `data.cpp` / `image.cpp` / `event.cpp` / `math.cpp` / `thread.cpp` 等基础域
8. 图形域（`RenderImageAudit`、`ClassicScenes`、`RenderSceneEffects`、`voxel_render`、`spritestack` 等）
9. 其余小文件

## 本机编译与运行验证（2026-08-19）

- 本机（Windows + VS18/MSVC + Ninja）用原始仓库的预构建
  `build/third-party-binary/win32-debug` 配置并编译 `unit_test.exe` 成功；
  CTest 发现脚本注册 113 个 bundle + 1385 个用例条目。
- `voxel.` 全族 167 个用例（voxel.cpp 90 + voxel_render.cpp 77）：**154 通过、13 warning、
  0 失败**；13 个 warning 全部来自未改动的 `voxel_render.cpp`（GPU 像素容差软断言，既有行为）。
- `particles.attach.` 全族 62 个用例：**0 失败**（1 个既有软 warning 在未改动的
  `clipSampleMovesEmitter`）。
- 合并过程中发现并修复两处问题：
  1. `CHECK(a && b)` 与 zeroerr 的 `&&` 重载冲突（MSVC C2666）→ 拆成单独断言；
  2. `CHECK_EQ(world->remeshDirty(), n)` 因 zeroerr 对参数二次求值且顺序不定产生伪 warning
     → 改为先存局部变量再断言（顺带消除了原用例就存在的伪警告）。
- 环境性失败（与本次改动无关）：`xray.occludedSilhouette` 在本机访问冲突崩溃；
  部分 filesystem 用例在沙箱内 `Failed to initialize filesystem`，提权后可过；
  渲染软断言 warning 为本机 GPU 容差现象。

> 备注：远端 main（fbf9db0）的 scene-ecs 合并存在已知编译问题
> （`SceneLoader.cpp` 引用不存在的 `SceneNode::linkTarget/linkKind`），为让本 PR 可编译，
> 按仓库 `codex/fix-sceneloader-merge-ci` 分支的修复一并提交了这两处补丁
> （`src/modules/sceneloader/SceneLoader.cpp` + `test/sceneloader.cpp`）。
