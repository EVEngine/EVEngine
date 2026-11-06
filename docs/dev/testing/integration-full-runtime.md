# 当前 138 个集成测试 CPP 的运行耗时

实测日期：2026-10-25。Windows x64，MSVC Debug、SHARED，原有逐用例进程隔离，CTest 4 并发。仅运行当前 `test/integration` 目录对应的全部默认桌面注册用例，不编译、不切换源文件、不改用 bundle。

覆盖 138 个 CPP、3458 个用例；完成 1 轮。

- 第 1 轮：墙钟 300.063 秒，CTest 退出码 8，结果 {'passed': 3419, 'skipped': 15, 'failed': 24}。

整批墙钟：300.063 秒；用例进程累计：1189.020 秒；全部注册用例均通过的文件 133/138。

**口径：下面每个文件的秒数，是该文件所有独立用例进程耗时的累计值（多轮时取中位数），包含进程启动和重复初始化。它不是 CPP 在单进程 bundle 内运行的时间，也不是独立串行运行该文件的实测墙钟；多个文件并发，所以这些值不能相加当作整批墙钟。min–max 是各轮文件累计值范围。单轮没有可用的波动估计，不能用于证明性能改进。失败文件的数字只是本次执行到失败的开销。**

环境固定为 `EVENGINE_VIEW_SECONDS=0.3`、`EVENGINE_PERF_FRAMES=30`。可选 BENCHMARK 和消息框开关未设置；包含此类分支的文件注明开关，进程成功不等于未启用分支的工作负载已经执行。编译期禁用/其他平台的用例不包含在当前桌面运行数字中。失败、超时、跳过原样保留，不能据此宣称全通过。ClassicScenes 的缺失资源跳过项不能当作已测得完整场景运行成本。

本轮没有恢复旧快照、修改用例或下载额外场景资源；运行前对当前 16 个测试程序逐一检查全部预期编译身份。遇到失败时仍执行完本轮全部用例，然后停止追加轮次，不用失败后的重复运行凑正常中位数。

完整机器数据见 [JSON](integration-full-runtime.json) 和 [CSV](integration-full-runtime.csv)。本机逐用例 JUnit、CTest 日志及冻结选择见 `.local-debug/integration-full-current/`（不提交）。

## 与上次完整运行比较

相同 Windows Debug SHARED 配置、逐用例进程隔离、4 并发、120 秒单例超时、VIEW_SECONDS=0.3 / PERF_FRAMES=30；两次均完整运行所有 138 个当前集成 CPP 对应的默认桌面用例。旧记录已完成最初的集成整理，本比较只反映后续六边形、体素及脚本契约精简，不是未优化原始测试集的全量对照。

- 总墙钟：**360.420 → 300.063 秒**，节省 **60.357 秒（16.7%）**，约 **6 分钟 → 5 分钟**。
- 用例进程累计：1431.291 → 1189.020 秒。累计值不是整批墙钟。
- 父用例：3591 → 3458；通过数 3552 → 3419，减少的 133 个注册与精简/子例合并一致。
- 仍为 24 个失败、15 个跳过；名称和状态逐项完全一致，没有新增失败/跳过。全部注册用例通过的文件仍为 133/138。本轮不是全绿验证。

整批运行中的三个重点文件，以下均为各自用例进程累计值：

- 六边形聚合文件：84.649 → 30.486 秒，71 → 37 个父用例。
- 体素聚合文件：91.664 → 44.151 秒，100 → 43 个父用例。
- 脚本场景文件：104.672 → 26.553 秒，150 → 108 个父用例。

失败及跳过都保留在完整日志和 JSON，没有从统计中删去。本轮 CTest 退出码为 8；完整执行全部 3458 个用例后停止追加轮次。两边各一轮，机器负载、缓存和 CTest 历史成本排序可能影响结果，因此 16.7% 是实测观察值，不是 ABBA 对照或全绿运行的统计结论。此前脚本单文件 88.8% 的墙钟降幅不可套用于整个集成套件。

基线见 [原完整快照](integration-file-runtime.md)，本次源码为 `44b465fd3`。运行前对当前 16 个程序逐一确认预期身份并记录哈希，未恢复旧二进制快照；运行后再次检查哈希保持不变。未修改用例、断言、等待参数或下载缺失资源以改变结果。

## 按累计耗时降序逐文件列出

### 1. [render_presentation_graphics_gpu_driven_tests.cpp](../../../test/integration/render-presentation/render_presentation_graphics_gpu_driven_tests.cpp)

- 路径：`test/integration/render-presentation/render_presentation_graphics_gpu_driven_tests.cpp`；52 个用例。
- 用例累计耗时中位数：**91.509 秒**；范围 91.509–91.509 秒。
- 各轮结果：{'passed': 41, 'failed': 11}。
- 最慢用例（跨轮中位数）：`graphics.clusteredLighting.reusesUnchangedStorageBindings` 19.187 秒；`graphics.deferredLighting.reusesCompletedFrameResources` 14.182 秒；`animation.spine.windowDemoRealModels` 4.120 秒。

### 2. [render_presentation_graphics_RenderSystem3D_tests.cpp](../../../test/integration/render-presentation/render_presentation_graphics_RenderSystem3D_tests.cpp)

- 路径：`test/integration/render-presentation/render_presentation_graphics_RenderSystem3D_tests.cpp`；76 个用例。
- 用例累计耗时中位数：**78.196 秒**；范围 78.196–78.196 秒。
- 各轮结果：{'passed': 73, 'failed': 3}。
- 未设置的可选开关：`EVENGINE_SKY_BENCHMARK`。
- 最慢用例（跨轮中位数）：`daynight.sky injected cloud time changes authored wisps and closes the morph cycle` 4.056 秒；`daynight.sky optical cycle survives midnight and restores the same rendered snapshot` 3.625 秒；`UI.smoke.panelGalleryPreview` 2.934 秒。

### 3. [render_presentation_graphics_RenderImageAudit_tests.cpp](../../../test/integration/render-presentation/render_presentation_graphics_RenderImageAudit_tests.cpp)

- 路径：`test/integration/render-presentation/render_presentation_graphics_RenderImageAudit_tests.cpp`；58 个用例。
- 用例累计耗时中位数：**67.217 秒**；范围 67.217–67.217 秒。
- 各轮结果：{'passed': 56, 'failed': 2}。
- 最慢用例（跨轮中位数）：`graphics.imageAudit.pipelineConfigs` 1.819 秒；`graphics.imageAudit.composite2d3d` 1.797 秒；`graphics.imageAudit.materialsAndCamera` 1.787 秒。

### 4. [physics_layered_cloth_gpu.cpp](../../../test/integration/layered-cloth/physics_layered_cloth_gpu.cpp)

- 路径：`test/integration/layered-cloth/physics_layered_cloth_gpu.cpp`；14 个用例。
- 用例累计耗时中位数：**64.236 秒**；范围 64.236–64.236 秒。
- 各轮结果：{'passed': 14}。
- 最慢用例（跨轮中位数）：`PhysicsLayeredClothGPU.fallingSheetsRemainSeparatedForOneSimulatedMinute` 29.535 秒；`PhysicsLayeredClothGPU.stabilityAndSynchronousTimingBaseline` 14.561 秒；`PhysicsLayeredClothGPU.rendersComputeGeneratedVertices` 2.346 秒。

### 5. [render_presentation_graphics_ClassicScenes_tests.cpp](../../../test/integration/render-presentation/render_presentation_graphics_ClassicScenes_tests.cpp)

- 路径：`test/integration/render-presentation/render_presentation_graphics_ClassicScenes_tests.cpp`；56 个用例。
- 用例累计耗时中位数：**52.393 秒**；范围 52.393–52.393 秒。
- 各轮结果：{'passed': 34, 'skipped': 15, 'failed': 7}。
- 最慢用例（跨轮中位数）：`ClassicScenes.perf.maxFps` 2.984 秒；`ClassicScenes.pbrChart.flythroughConfigs` 2.795 秒；`ClassicScenes.cornell.flythroughConfigs` 2.592 秒。

### 6. [render_pipeline_graphics_graphics_backend_parity_tests.cpp](../../../test/integration/render-pipeline/render_pipeline_graphics_graphics_backend_parity_tests.cpp)

- 路径：`test/integration/render-pipeline/render_pipeline_graphics_graphics_backend_parity_tests.cpp`；67 个用例。
- 用例累计耗时中位数：**44.752 秒**；范围 44.752–44.752 秒。
- 各轮结果：{'passed': 67}。
- 未设置的可选开关：`EVENGINE_ECS_MILLION_BENCHMARK`, `EVENGINE_PCG_MILLION_BENCHMARK`。
- 最慢用例（跨轮中位数）：`graphics.backendParity.pixelWorldMillionPixelAtlas` 3.099 秒；`fluids.ssf.sphericalDepthAndNormalParity` 1.476 秒；`gpgpu.procgen.transformParity` 1.278 秒。

### 7. [render_presentation_graphics_voxel_face_dir_tests.cpp](../../../test/integration/render-presentation/render_presentation_graphics_voxel_face_dir_tests.cpp)

- 路径：`test/integration/render-presentation/render_presentation_graphics_voxel_face_dir_tests.cpp`；43 个用例。
- 用例累计耗时中位数：**44.151 秒**；范围 44.151–44.151 秒。
- 各轮结果：{'passed': 43}。
- 最慢用例（跨轮中位数）：`softbody.cloth3d.collidesWithWorld3D` 3.963 秒；`softbody.cloth3d.momentumExchangeScalesWithMass` 3.013 秒；`softbody.gpu.clothLargeSelfCollision` 1.692 秒。

### 8. [asset_import_graphics_tests.cpp](../../../test/integration/asset-import/asset_import_graphics_tests.cpp)

- 路径：`test/integration/asset-import/asset_import_graphics_tests.cpp`；102 个用例。
- 用例累计耗时中位数：**42.723 秒**；范围 42.723–42.723 秒。
- 各轮结果：{'passed': 102}。
- 最慢用例（跨轮中位数）：`animation.mixamo.skeletonIdleRunJumpPreview` 3.810 秒；`model3d.render.rock1Fbx` 2.581 秒；`model3d.render.rock1ToonShader` 2.511 秒。

### 9. [card_rts_payment_card_tests.cpp](../../../test/integration/card-rts-payment/card_rts_payment_card_tests.cpp)

- 路径：`test/integration/card-rts-payment/card_rts_payment_card_tests.cpp`；11 个用例。
- 用例累计耗时中位数：**36.387 秒**；范围 36.387–36.387 秒。
- 各轮结果：{'passed': 11}。
- 最慢用例（跨轮中位数）：`card.rtsScriptScenarioCollectsBuildsProducesAndPlaysAtomicSkills` 34.926 秒；`card.genericBatchRetainsScriptOwnersAndReleasesPendingStagesAtVmShutdown` 0.269 秒；`card.rtsScriptGatheringUsesConfiguredResourceAndDropoff` 0.257 秒。

### 10. [render_presentation_graphics_procgen_tests.cpp](../../../test/integration/render-presentation/render_presentation_graphics_procgen_tests.cpp)

- 路径：`test/integration/render-presentation/render_presentation_graphics_procgen_tests.cpp`；30 个用例。
- 用例累计耗时中位数：**34.781 秒**；范围 34.781–34.781 秒。
- 各轮结果：{'passed': 29, 'failed': 1}。
- 最慢用例（跨轮中位数）：`particles.gpu.residentSimulationAndIndirectRendering` 3.706 秒；`particles.scalability.renderBudgetStressScene` 2.815 秒；`particles.playback.renderDistanceTrailAndLoopingBurst` 2.598 秒。

### 11. [render_presentation_graphics_window_cpp_tests.cpp](../../../test/integration/render-presentation/render_presentation_graphics_window_cpp_tests.cpp)

- 路径：`test/integration/render-presentation/render_presentation_graphics_window_cpp_tests.cpp`；21 个用例。
- 用例累计耗时中位数：**32.428 秒**；范围 32.428–32.428 秒。
- 各轮结果：{'passed': 21}。
- 未设置的可选开关：`EVENGINE_TEST_MSGBOX`。
- 最慢用例（跨轮中位数）：`graphics.lifecycle.minimizeRestoreStillPresents` 10.723 秒；`window.minimizeRestoreRoundTrip` 6.853 秒；`window.setWindowSettingsRoundTrip` 1.236 秒。

### 12. [file_io_graphics_tensor_llm_tests.cpp](../../../test/integration/file-io/file_io_graphics_tensor_llm_tests.cpp)

- 路径：`test/integration/file-io/file_io_graphics_tensor_llm_tests.cpp`；31 个用例。
- 用例累计耗时中位数：**31.953 秒**；范围 31.953–31.953 秒。
- 各轮结果：{'passed': 31}。
- 最慢用例（跨轮中位数）：`devtools.projectIndex.automaticWithoutManifestReflectsDomainChainAndFields` 6.802 秒；`devtools.languageServer.completesChainedAndConstructedNativeTypes` 3.960 秒；`devtools.projectIndex.automaticRejectsShadowedAndDynamicNativeLookalikes` 3.454 秒。

### 13. [render_pipeline_graphics_graphics_vegetation_extras_tests.cpp](../../../test/integration/render-pipeline/render_pipeline_graphics_graphics_vegetation_extras_tests.cpp)

- 路径：`test/integration/render-pipeline/render_pipeline_graphics_graphics_vegetation_extras_tests.cpp`；40 个用例。
- 用例累计耗时中位数：**30.743 秒**；范围 30.743–30.743 秒。
- 各轮结果：{'passed': 40}。
- 最慢用例（跨轮中位数）：`graphics.Grass.gpuRenderScreenshot` 2.430 秒；`tensor.onnx.gpu.parallelCompilation` 1.403 秒；`tensor.gpu.transformerBlockInference` 1.214 秒。

### 14. [render_presentation_graphics_hex_level_simulation_tests.cpp](../../../test/integration/render-presentation/render_presentation_graphics_hex_level_simulation_tests.cpp)

- 路径：`test/integration/render-presentation/render_presentation_graphics_hex_level_simulation_tests.cpp`；37 个用例。
- 用例累计耗时中位数：**30.486 秒**；范围 30.486–30.486 秒。
- 各轮结果：{'passed': 37}。
- 最慢用例（跨轮中位数）：`ik.solver2d.armReachPreview` 2.922 秒；`inventory.bag.uiPreview` 2.740 秒；`map.path.astar.routePreview` 2.527 秒。

### 15. [file_io_graphics_hex_level_data_tests.cpp](../../../test/integration/file-io/file_io_graphics_hex_level_data_tests.cpp)

- 路径：`test/integration/file-io/file_io_graphics_hex_level_data_tests.cpp`；91 个用例。
- 用例累计耗时中位数：**29.962 秒**；范围 29.962–29.962 秒。
- 各轮结果：{'passed': 91}。
- 最慢用例（跨轮中位数）：`animation.skinned.render.playerDriven` 2.538 秒；`filesystem.watch.coalescesOverlappingRegistrations` 2.510 秒；`animation.skinned.render.applyToMesh` 1.915 秒。

### 16. [asset_pipeline_graphics_import_workflows_tests.cpp](../../../test/integration/asset-pipeline/asset_pipeline_graphics_import_workflows_tests.cpp)

- 路径：`test/integration/asset-pipeline/asset_pipeline_graphics_import_workflows_tests.cpp`；139 个用例。
- 用例累计耗时中位数：**28.566 秒**；范围 28.566–28.566 秒。
- 各轮结果：{'passed': 139}。
- 最慢用例（跨轮中位数）：`asset.import.unityStaticPrefabRendersOwnedEcsInstances` 2.585 秒；`asset.import.unityFbxCharacterProducesSkinAndAnimation` 2.293 秒；`asset.import.unityVegetationNormalPreservesLinearSourceAndGeneratesHeight` 1.782 秒。

### 17. [script_api_graphics_scene_tests.cpp](../../../test/integration/script-api/script_api_graphics_scene_tests.cpp)

- 路径：`test/integration/script-api/script_api_graphics_scene_tests.cpp`；108 个用例。
- 用例累计耗时中位数：**26.553 秒**；范围 26.553–26.553 秒。
- 各轮结果：{'passed': 108}。
- 最慢用例（跨轮中位数）：`scriptCompiler.sourceMetadataDiagnosticsAndTooling` 1.873 秒；`scriptConfig.rejectsMissingRequiredModule` 1.208 秒；`scriptCompiler.nativeBindingExecution` 1.119 秒。

### 18. [script_api_graphics_mcp_tests.cpp](../../../test/integration/script-api/script_api_graphics_mcp_tests.cpp)

- 路径：`test/integration/script-api/script_api_graphics_mcp_tests.cpp`；87 个用例。
- 用例累计耗时中位数：**25.544 秒**；范围 25.544–25.544 秒。
- 各轮结果：{'passed': 87}。
- 最慢用例（跨轮中位数）：`devtools.mcp.stderrCaptureFeedsConsole` 4.767 秒；`moduleExpose.allBindingsPreserveProcgenRockSurface` 1.230 秒；`moduleExpose.allBindingsPreserveRenderable3DSurface` 1.098 秒。

### 19. [concurrency_graphics_tests.cpp](../../../test/integration/concurrency/concurrency_graphics_tests.cpp)

- 路径：`test/integration/concurrency/concurrency_graphics_tests.cpp`；113 个用例。
- 用例累计耗时中位数：**22.393 秒**；范围 22.393–22.393 秒。
- 各轮结果：{'passed': 113}。
- 未设置的可选开关：`EVENGINE_PIXELWORLD_ACTIVE_BENCHMARK`, `EVENGINE_PIXELWORLD_MILLION_BENCHMARK`。
- 最慢用例（跨轮中位数）：`devtools.architecture.mcpStdioListsAndQueriesGraph` 1.473 秒；`devtools.mcp.everyDeclaredToolIsRouted` 1.275 秒；`render_graph.jobSystemEngineFramePattern` 1.153 秒。

### 20. [gameplay_composition_graphics_procgen_core_boundary_tests.cpp](../../../test/integration/gameplay-composition/gameplay_composition_graphics_procgen_core_boundary_tests.cpp)

- 路径：`test/integration/gameplay-composition/gameplay_composition_graphics_procgen_core_boundary_tests.cpp`；118 个用例。
- 用例累计耗时中位数：**20.413 秒**；范围 20.413–20.413 秒。
- 各轮结果：{'passed': 118}。
- 最慢用例（跨轮中位数）：`procgen.e2e.snapshotRoundTripPreservesIdentityBuildKeyAndQueries` 1.298 秒；`ui.pcgPhotoModeApply.lightingSkyboxAuthorityDrivesProceduralSkyAndRestores` 1.235 秒；`ui.pcgPhotoModeApply.lightingSunAuthorityPersistsAcrossDayNightUpdates` 1.167 秒。

### 21. [gameplay_composition_graphics_editor_camera_target_tests.cpp](../../../test/integration/gameplay-composition/gameplay_composition_graphics_editor_camera_target_tests.cpp)

- 路径：`test/integration/gameplay-composition/gameplay_composition_graphics_editor_camera_target_tests.cpp`；127 个用例。
- 用例累计耗时中位数：**20.137 秒**；范围 20.137–20.137 秒。
- 各轮结果：{'passed': 127}。
- 最慢用例（跨轮中位数）：`editor.v2.asset_database_indexes_and_pages_100k_records` 4.367 秒；`editor.housegen.kit_supports_incremental_authoring_properties_and_snapshot` 0.291 秒；`editor.material.preview_uses_immutable_isolated_revisioned_request` 0.286 秒。

### 22. [gameplay_composition_graphics_graphics_vegetation_tests.cpp](../../../test/integration/gameplay-composition/gameplay_composition_graphics_graphics_vegetation_tests.cpp)

- 路径：`test/integration/gameplay-composition/gameplay_composition_graphics_graphics_vegetation_tests.cpp`；108 个用例。
- 用例累计耗时中位数：**18.981 秒**；范围 18.981–18.981 秒。
- 各轮结果：{'passed': 108}。
- 最慢用例（跨轮中位数）：`GraphicsPrimitives.sceneCacheOnlyRebuildsUpdatedSlots` 1.850 秒；`graphics.backendParity.vegetationFieldGizmoReadback` 0.647 秒；`graphics.vegetation.normal_gpu_matches_filtered_cpu_reference` 0.600 秒。

### 23. [gameplay_composition_graphics_rts_script_tests.cpp](../../../test/integration/gameplay-composition/gameplay_composition_graphics_rts_script_tests.cpp)

- 路径：`test/integration/gameplay-composition/gameplay_composition_graphics_rts_script_tests.cpp`；121 个用例。
- 用例累计耗时中位数：**18.856 秒**；范围 18.856–18.856 秒。
- 各轮结果：{'passed': 121}。
- 最慢用例（跨轮中位数）：`rts.sandboxInitializesStepsAndRebuildsAcrossReload` 1.874 秒；`softbody.volume.renderPreview` 1.616 秒；`softbody.volume.pinsAndWorldCollision` 0.744 秒。

### 24. [physics_simulation_graphics_box3d_tests.cpp](../../../test/integration/physics-simulation/physics_simulation_graphics_box3d_tests.cpp)

- 路径：`test/integration/physics-simulation/physics_simulation_graphics_box3d_tests.cpp`；127 个用例。
- 用例累计耗时中位数：**18.615 秒**；范围 18.615–18.615 秒。
- 各轮结果：{'passed': 127}。
- 最慢用例（跨轮中位数）：`box2d.render.fallingStackPreview` 2.939 秒；`box3d.query.orientedBoxCast` 0.313 秒；`climbing.selection.failedPrepareLeavesInputAndAuthorityUnchanged` 0.283 秒。

### 25. [gameplay_composition_graphics_animation_inertializer_tests.cpp](../../../test/integration/gameplay-composition/gameplay_composition_graphics_animation_inertializer_tests.cpp)

- 路径：`test/integration/gameplay-composition/gameplay_composition_graphics_animation_inertializer_tests.cpp`；129 个用例。
- 用例累计耗时中位数：**17.252 秒**；范围 17.252–17.252 秒。
- 各轮结果：{'passed': 129}。
- 最慢用例（跨轮中位数）：`audio.modplug.midi.play` 1.066 秒；`audio.streamSource.pump` 0.572 秒；`audio_editing.publication_failure_is_atomic_and_success_commits_candidate` 0.293 秒。

### 26. [script_api_graphics_ScriptECS_tests.cpp](../../../test/integration/script-api/script_api_graphics_ScriptECS_tests.cpp)

- 路径：`test/integration/script-api/script_api_graphics_ScriptECS_tests.cpp`；108 个用例。
- 用例累计耗时中位数：**14.826 秒**；范围 14.826–14.826 秒。
- 各轮结果：{'passed': 108}。
- 最慢用例（跨轮中位数）：`database.panelBuildsGridAndBindsCells` 0.533 秒；`climbing.playground.mainScriptCompilesThroughEveScriptFrontend` 0.446 秒；`ScriptECS.shaderSystemClassExists` 0.329 秒。

### 27. [gameplay_composition_graphics_editor_tests.cpp](../../../test/integration/gameplay-composition/gameplay_composition_graphics_editor_tests.cpp)

- 路径：`test/integration/gameplay-composition/gameplay_composition_graphics_editor_tests.cpp`；117 个用例。
- 用例累计耗时中位数：**13.877 秒**；范围 13.877–13.877 秒。
- 各轮结果：{'passed': 117}。
- 最慢用例（跨轮中位数）：`editor.scene.script_component_planner_refuses_invalid_edits` 0.303 秒；`editor.audio.waveform_builds_bounded_envelopes_and_loop_diagnostics` 0.290 秒；`editor.gizmo_manager.multi_mode` 0.282 秒。

### 28. [gameplay_composition_graphics_gameplay_action_tests.cpp](../../../test/integration/gameplay-composition/gameplay_composition_graphics_gameplay_action_tests.cpp)

- 路径：`test/integration/gameplay-composition/gameplay_composition_graphics_gameplay_action_tests.cpp`；117 个用例。
- 用例累计耗时中位数：**13.819 秒**；范围 13.819–13.819 秒。
- 各轮结果：{'passed': 117}。
- 最慢用例（跨轮中位数）：`editor.stylize.recipe_rejects_mixed_stages_and_loads_atomically` 0.318 秒；`editor.stylize.runtime_rejects_stale_generation_before_graphics_use` 0.284 秒；`editor.splinePath.snapshotGeneratesTubeWithoutSourceMesh` 0.276 秒。

### 29. [CrowdThread.cpp](../../../test/integration/crowd-runtime/CrowdThread.cpp)

- 路径：`test/integration/crowd-runtime/CrowdThread.cpp`；7 个用例。
- 用例累计耗时中位数：**13.555 秒**；范围 13.555–13.555 秒。
- 各轮结果：{'passed': 7}。
- 最慢用例（跨轮中位数）：`crowd.thread.predictiveAvoidanceMatchesSerialWithFourWorkers` 7.776 秒；`crowd.thread.partialVelocityFailureRollsBackClockAndMotion` 3.637 秒；`crowd.thread.serialParallelContactAndReportsAreBitExact` 0.924 秒。

### 30. [gameplay_composition_graphics_editor_material_target_tests.cpp](../../../test/integration/gameplay-composition/gameplay_composition_graphics_editor_material_target_tests.cpp)

- 路径：`test/integration/gameplay-composition/gameplay_composition_graphics_editor_material_target_tests.cpp`；102 个用例。
- 用例累计耗时中位数：**13.513 秒**；范围 13.513–13.513 秒。
- 各轮结果：{'passed': 102}。
- 最慢用例（跨轮中位数）：`editor.material.vegetation_motion_snapshot_reaches_bound_runtime_material` 0.592 秒；`editor.spritestack.failed_rebake_preserves_generation_and_stale_publish_fails_first` 0.544 秒；`editor.material.missing_vegetation_motion_texture_rejects_atomically` 0.400 秒。

### 31. [gameplay_composition_graphics_rts_combat_tests.cpp](../../../test/integration/gameplay-composition/gameplay_composition_graphics_rts_combat_tests.cpp)

- 路径：`test/integration/gameplay-composition/gameplay_composition_graphics_rts_combat_tests.cpp`；62 个用例。
- 用例累计耗时中位数：**13.136 秒**；范围 13.136–13.136 秒。
- 各轮结果：{'passed': 62}。
- 最慢用例（跨轮中位数）：`rts.largeExactlyOverlappingCrowdUnlocksDeterministically` 6.419 秒；`rts.completedCanonicalResearchAppliesToExistingAndFutureUnitsOnce` 0.274 秒；`rts.escortAndCombatGroupCoordinateTargets` 0.216 秒。

### 32. [script_api_graphics_dap_tests.cpp](../../../test/integration/script-api/script_api_graphics_dap_tests.cpp)

- 路径：`test/integration/script-api/script_api_graphics_dap_tests.cpp`；68 个用例。
- 用例累计耗时中位数：**13.011 秒**；范围 13.011–13.011 秒。
- 各轮结果：{'passed': 68}。
- 最慢用例（跨轮中位数）：`devtools.dap.initializeLaunchContinueStep` 1.215 秒；`devtools.dap.frameScopesAndVariableTree` 0.700 秒；`devtools.dap.runtimeExecuteReportsThroughDevToolOnce` 0.580 秒。

### 33. [runtime_lifecycle_graphics_tests.cpp](../../../test/integration/runtime-lifecycle/runtime_lifecycle_graphics_tests.cpp)

- 路径：`test/integration/runtime-lifecycle/runtime_lifecycle_graphics_tests.cpp`；86 个用例。
- 用例累计耗时中位数：**12.010 秒**；范围 12.010–12.010 秒。
- 各轮结果：{'passed': 86}。
- 最慢用例（跨轮中位数）：`plugins.load.nativeLibraryAndInstantiateCppModule` 1.086 秒；`gameplay.control.squirrelFacadeUsesTheSharedJsonRouter` 0.405 秒；`keyboard.textInput` 0.389 秒。

### 34. [gameplay_composition_graphics_buildingfx_tests.cpp](../../../test/integration/gameplay-composition/gameplay_composition_graphics_buildingfx_tests.cpp)

- 路径：`test/integration/gameplay-composition/gameplay_composition_graphics_buildingfx_tests.cpp`；94 个用例。
- 用例累计耗时中位数：**11.478 秒**；范围 11.478–11.478 秒。
- 各轮结果：{'passed': 94}。
- 最慢用例（跨轮中位数）：`climbing.motionMatchingDemoScriptCompilesAndDocumentsUeOnlyBoundary` 0.674 秒；`climbing.animation.motionMatchingYieldsToActionAndResumesByTick` 0.316 秒；`combatDamage.previewUsesCanonicalRuleWithoutMutatingTarget` 0.283 秒。

### 35. [rts_unit_lifecycle_tactics_rts_tests.cpp](../../../test/integration/rts-unit-lifecycle/rts_unit_lifecycle_tactics_rts_tests.cpp)

- 路径：`test/integration/rts-unit-lifecycle/rts_unit_lifecycle_tactics_rts_tests.cpp`；64 个用例。
- 用例累计耗时中位数：**11.324 秒**；范围 11.324–11.324 秒。
- 各轮结果：{'passed': 64}。
- 最慢用例（跨轮中位数）：`rts.groups.pacingWorksWithAndWithoutCrowd` 0.859 秒；`rts.groups.mixedSpeedSquadClearsSingleCellPassage` 0.574 秒；`rts.groups.opposingSquadsReserveWholeCorridor` 0.551 秒。

### 36. [physics_simulation_graphics_physics_rigid_backend_tests.cpp](../../../test/integration/physics-simulation/physics_simulation_graphics_physics_rigid_backend_tests.cpp)

- 路径：`test/integration/physics-simulation/physics_simulation_graphics_physics_rigid_backend_tests.cpp`；94 个用例。
- 用例累计耗时中位数：**11.041 秒**；范围 11.041–11.041 秒。
- 各轮结果：{'passed': 94}。
- 最慢用例（跨轮中位数）：`physics.mechanism.crankSliderConvertsRotationToStroke` 0.331 秒；`climbing.snapshot.futureVersionAndStaleLinkFailuresAreAtomic` 0.284 秒；`climbing.reload.jsonProfileAndRuntimeRoundTrip` 0.231 秒。

### 37. [script_api_graphics_terrain_probe_tests.cpp](../../../test/integration/script-api/script_api_graphics_terrain_probe_tests.cpp)

- 路径：`test/integration/script-api/script_api_graphics_terrain_probe_tests.cpp`；88 个用例。
- 用例累计耗时中位数：**10.752 秒**；范围 10.752–10.752 秒。
- 各轮结果：{'passed': 88}。
- 最慢用例（跨轮中位数）：`procgen.terrain.performance.real1kRoundTripAnd1k4k8kRandomAccess` 0.778 秒；`procgen.mask.scriptGlobalSpawnerMask` 0.394 秒；`TimerTest.basic` 0.248 秒。

### 38. [gameplay_composition_graphics_stylize_attack_vfx_layers_tests.cpp](../../../test/integration/gameplay-composition/gameplay_composition_graphics_stylize_attack_vfx_layers_tests.cpp)

- 路径：`test/integration/gameplay-composition/gameplay_composition_graphics_stylize_attack_vfx_layers_tests.cpp`；41 个用例。
- 用例累计耗时中位数：**8.401 秒**；范围 8.401–8.401 秒。
- 各轮结果：{'passed': 41}。
- 最慢用例（跨轮中位数）：`stylize.ysa.realGpuPipeline` 0.989 秒；`stylize.effects.gpu.meshShaders` 0.849 秒；`stylize.effects.gpu.postPasses` 0.779 秒。

### 39. [gameplay_composition_graphics_procgen_road_editing_tests.cpp](../../../test/integration/gameplay-composition/gameplay_composition_graphics_procgen_road_editing_tests.cpp)

- 路径：`test/integration/gameplay-composition/gameplay_composition_graphics_procgen_road_editing_tests.cpp`；45 个用例。
- 用例累计耗时中位数：**7.760 秒**；范围 7.760–7.760 秒。
- 各轮结果：{'passed': 45}。
- 最慢用例（跨轮中位数）：`procgen.simulation.crystalCave` 1.821 秒；`procgen.simulation.lootPropBake` 0.343 秒；`resource.gameplay.rtsBuildUsesSharedActionOrdersProductionPayment` 0.287 秒。

### 40. [frame_recording_tests.cpp](../../../test/integration/frame-recording/frame_recording_tests.cpp)

- 路径：`test/integration/frame-recording/frame_recording_tests.cpp`；2 个用例。
- 用例累计耗时中位数：**7.307 秒**；范围 7.307–7.307 秒。
- 各轮结果：{'passed': 2}。
- 最慢用例（跨轮中位数）：`graphics.frameRecording.fxaaSlotReuseResizeAndPersistentCanvas` 7.217 秒；`graphics.frameRecording.inlineBudgetAndPeriodicReprobe` 0.090 秒。

### 41. [vsync_config_tests.cpp](../../../test/integration/vsync-config/vsync_config_tests.cpp)

- 路径：`test/integration/vsync-config/vsync_config_tests.cpp`；5 个用例。
- 用例累计耗时中位数：**5.444 秒**；范围 5.444–5.444 秒。
- 各轮结果：{'passed': 5}。
- 最慢用例（跨轮中位数）：`scriptConfig.vsyncAppliedBeforeEditorWindow` 1.316 秒；`scriptConfig.vsyncExplicitTrue` 1.231 秒；`scriptConfig.vsyncFalseUsesUnsynchronizedPresent` 1.178 秒。

### 42. [asset_pipeline_graphics_file_admission_tests.cpp](../../../test/integration/asset-pipeline/asset_pipeline_graphics_file_admission_tests.cpp)

- 路径：`test/integration/asset-pipeline/asset_pipeline_graphics_file_admission_tests.cpp`；41 个用例。
- 用例累计耗时中位数：**5.017 秒**；范围 5.017–5.017 秒。
- 各轮结果：{'passed': 41}。
- 最慢用例（跨轮中位数）：`asset.mesh.unityV11AuthoringChannelsSurviveCookAndRuntimeLoad` 0.438 秒；`asset.import.gltfMaterialSelectsNonzeroUvWithoutTransform` 0.285 秒；`asset.migration.rejectsUnknownNewVersionWithoutChangingInput` 0.207 秒。

### 43. [ocean_gpu.cpp](../../../test/integration/ocean-voyage/ocean_gpu.cpp)

- 路径：`test/integration/ocean-voyage/ocean_gpu.cpp`；4 个用例。
- 用例累计耗时中位数：**3.425 秒**；范围 3.425–3.425 秒。
- 各轮结果：{'passed': 4}。
- 最慢用例（跨轮中位数）：`fluids.ocean.gpuSpectrumLargeBandParity` 1.440 秒；`fluids.ocean.gpuSpectrumMatchesCpuAndRejectsStaleFrames` 0.911 秒；`fluids.ocean.gpuSeaTransitionMatchesCpuFieldsAndLevel` 0.812 秒。

### 44. [CrowdMotion.cpp](../../../test/integration/rts-crowd/CrowdMotion.cpp)

- 路径：`test/integration/rts-crowd/CrowdMotion.cpp`；6 个用例。
- 用例累计耗时中位数：**2.974 秒**；范围 2.974–2.974 秒。
- 各轮结果：{'passed': 6}。
- 最慢用例（跨轮中位数）：`rts.crowd.parallelConsumerMatchesSerialMotionAndArrival` 1.331 秒；`rts.crowd.parallelConsumerRejectsExpiredProviderWithoutPublishingMotion` 0.573 秒；`rts.crowd.partialParallelGatherRollsBackBothConsumerPoses` 0.571 秒。

### 45. [procgen_road_profile_editing.cpp](../../../test/integration/gameplay-composition/procgen_road_profile_editing.cpp)

- 路径：`test/integration/gameplay-composition/procgen_road_profile_editing.cpp`；26 个用例。
- 用例累计耗时中位数：**2.857 秒**；范围 2.857–2.857 秒。
- 各轮结果：{'passed': 26}。
- 最慢用例（跨轮中位数）：`procgen.road.editing.snapshotRoundTripPreservesStableTopology` 0.265 秒；`procgen.road.editing.crossingEdgesBecomeOneUndoableJunction` 0.260 秒；`procgen.road.editing.junctionControlUsesExistingAuthorityUndo` 0.244 秒。

### 46. [rts_formation.cpp](../../../test/integration/rts-crowd/rts_formation.cpp)

- 路径：`test/integration/rts-crowd/rts_formation.cpp`；13 个用例。
- 用例累计耗时中位数：**2.818 秒**；范围 2.818–2.818 秒。
- 各轮结果：{'passed': 13}。
- 最慢用例（跨轮中位数）：`rts.formation.largeTranslatedGridDoesNotReverseMarchingRows` 1.152 秒；`rts.attackSlots.waitingMembersNeverProduceRealWeaponFire` 0.749 秒；`rts.groups.attackMoveArrivalRetiresMovingGroupAndKeepsCombatStance` 0.238 秒。

### 47. [physics_vehicle_tires3d.cpp](../../../test/integration/physics-simulation/physics_vehicle_tires3d.cpp)

- 路径：`test/integration/physics-simulation/physics_vehicle_tires3d.cpp`；10 个用例。
- 用例累计耗时中位数：**2.572 秒**；范围 2.572–2.572 秒。
- 各轮结果：{'passed': 10}。
- 最慢用例（跨轮中位数）：`physics.vehicleTires3D.brakesHoldOnSlopeAndStopForwardMotion` 1.202 秒；`physics.vehicleTires3D.suspensionMobilityDrivesAndSteersByForces` 0.553 秒；`physics.vehicleTires3D.rejectedStepAndInvalidWheelAreAtomic` 0.183 秒。

### 48. [ocean_render.cpp](../../../test/integration/ocean-voyage/ocean_render.cpp)

- 路径：`test/integration/ocean-voyage/ocean_render.cpp`；3 个用例。
- 用例累计耗时中位数：**2.519 秒**；范围 2.519–2.519 秒。
- 各轮结果：{'passed': 3}。
- 最慢用例（跨轮中位数）：`fluids.ocean.residentRenderingMatchesCpuWithWakeAndFloatingOrigin` 0.922 秒；`fluids.ocean.scriptRenderBuildersOwnInputsAcrossGc` 0.871 秒；`fluids.ocean.rendererPublishesSharedSurfaceAndFoam` 0.726 秒。

### 49. [rts_crowd_flow_navigation.cpp](../../../test/integration/rts-crowd/rts_crowd_flow_navigation.cpp)

- 路径：`test/integration/rts-crowd/rts_crowd_flow_navigation.cpp`；23 个用例。
- 用例累计耗时中位数：**2.259 秒**；范围 2.259–2.259 秒。
- 各轮结果：{'passed': 23}。
- 最慢用例（跨轮中位数）：`rts.crowd.failedPumpAndSameOwnerInputRepairDoNotRefundTickQuota` 0.251 秒；`rts.crowd.reconfigureAndClearReenableCannotRefundFailedTickWork` 0.249 秒；`rts.crowd.sharedFlowCheckpointRenewsOwnerAndRebindsCachedField` 0.176 秒。

### 50. [audio_playback_graphics_tests.cpp](../../../test/integration/audio-playback/audio_playback_graphics_tests.cpp)

- 路径：`test/integration/audio-playback/audio_playback_graphics_tests.cpp`；12 个用例。
- 用例累计耗时中位数：**2.057 秒**；范围 2.057–2.057 秒。
- 各轮结果：{'passed': 12}。
- 最慢用例（跨轮中位数）：`demo.newPlanetTexture` 0.805 秒；`sound.newSoundData.wav` 0.277 秒；`demo.newSound.music` 0.107 秒。

### 51. [render_pipeline_graphics_frame_capture_tests.cpp](../../../test/integration/render-pipeline/render_pipeline_graphics_frame_capture_tests.cpp)

- 路径：`test/integration/render-pipeline/render_pipeline_graphics_frame_capture_tests.cpp`；1 个用例。
- 用例累计耗时中位数：**1.990 秒**；范围 1.990–1.990 秒。
- 各轮结果：{'passed': 1}。
- 最慢用例（跨轮中位数）：`graphics.frameCapture.sameFrameNativeAttachmentsAndFrozenSnapshots` 1.990 秒。

### 52. [save_restore_graphics_tests.cpp](../../../test/integration/save-restore/save_restore_graphics_tests.cpp)

- 路径：`test/integration/save-restore/save_restore_graphics_tests.cpp`；12 个用例。
- 用例累计耗时中位数：**1.978 秒**；范围 1.978–1.978 秒。
- 各轮结果：{'passed': 12}。
- 最慢用例（跨轮中位数）：`graphics.canvas.separateSubmissionsPreserveAndClearRGBA8` 0.522 秒；`rpg.saveSession.explicitIdMigrationRewritesOwningPayloadBeforeValidation` 0.317 秒；`dialoguePersistence.jsonRoundtrip` 0.209 秒。

### 53. [ui_workflows_graphics_tests.cpp](../../../test/integration/ui-workflows/ui_workflows_graphics_tests.cpp)

- 路径：`test/integration/ui-workflows/ui_workflows_graphics_tests.cpp`；14 个用例。
- 用例累计耗时中位数：**1.897 秒**；范围 1.897–1.897 秒。
- 各轮结果：{'passed': 14}。
- 最慢用例（跨轮中位数）：`UI.p1.scrollListHeadlessRenderLarge` 0.483 秒；`UI.p0.scriptEventCallbacks` 0.200 秒；`UI.editorKit.desktopCompositionRenders` 0.122 秒。

### 54. [physics_snapshot3d_ownership.cpp](../../../test/integration/physics-simulation/physics_snapshot3d_ownership.cpp)

- 路径：`test/integration/physics-simulation/physics_snapshot3d_ownership.cpp`；13 个用例。
- 用例累计耗时中位数：**1.851 秒**；范围 1.851–1.851 秒。
- 各轮结果：{'passed': 13}。
- 最慢用例（跨轮中位数）：`physics.snapshot3d.transmissionPhaseTopologyAndTrajectory` 0.402 秒；`physics.snapshot3d.jointParametersRoundTripAllKinds` 0.243 秒；`physics.snapshot3d.candidateOwnershipCommitAndRollback` 0.188 秒。

### 55. [ocean_boat.cpp](../../../test/integration/ocean-voyage/ocean_boat.cpp)

- 路径：`test/integration/ocean-voyage/ocean_boat.cpp`；6 个用例。
- 用例累计耗时中位数：**1.752 秒**；范围 1.752–1.752 秒。
- 各轮结果：{'passed': 6}。
- 最慢用例（跨轮中位数）：`fluids.ocean.boatWakeChangesSecondRigidBody` 0.924 秒；`fluids.ocean.boatRespondsToSpectralWaves` 0.317 秒；`fluids.ocean.boatFloatsAndPropelsRealRigidBody` 0.183 秒。

### 56. [procgen_road_spacing_portals.cpp](../../../test/integration/gameplay-composition/procgen_road_spacing_portals.cpp)

- 路径：`test/integration/gameplay-composition/procgen_road_spacing_portals.cpp`；7 个用例。
- 用例累计耗时中位数：**1.697 秒**；范围 1.697–1.697 秒。
- 各轮结果：{'passed': 7}。
- 最慢用例（跨轮中位数）：`procgen.road.authoring.rigidDistributionMatchesMeasuredReferenceAndBudget` 0.623 秒；`procgen.road.authoring.tunnelPortalHolesPreserveRoofAcrossSplitSaveUndoAndNativeWriteback` 0.403 秒；`procgen.road.authoring.rigidDistributionPersistsPresetUndoAndLegacyMigration` 0.205 秒。

### 57. [physics_voronoi_fracture.cpp](../../../test/integration/house-collapse/physics_voronoi_fracture.cpp)

- 路径：`test/integration/house-collapse/physics_voronoi_fracture.cpp`；11 个用例。
- 用例累计耗时中位数：**1.663 秒**；范围 1.663–1.663 秒。
- 各轮结果：{'passed': 11}。
- 最慢用例（跨轮中位数）：`physics_voronoi.failedAdmissionDoesNotLeakBodies` 0.282 秒；`physics_voronoi.authoredAssemblyKeepsSupports` 0.274 秒；`physics_voronoi.weldedWorldFirstTeardown` 0.248 秒。

### 58. [fluids_shore_backend_tests.cpp](../../../test/integration/render-presentation/fluids_shore_backend_tests.cpp)

- 路径：`test/integration/render-presentation/fluids_shore_backend_tests.cpp`；2 个用例。
- 用例累计耗时中位数：**1.604 秒**；范围 1.604–1.604 秒。
- 各轮结果：{'passed': 2}。
- 最慢用例（跨轮中位数）：`fluids.shore.gpuMatchesCpuOnSlopingWetDryBed` 1.024 秒；`fluids.shore.scriptSurfaceSyncChecksInputsAndPublishesMesh` 0.580 秒。

### 59. [performance_graphics_tests.cpp](../../../test/integration/performance/performance_graphics_tests.cpp)

- 路径：`test/integration/performance/performance_graphics_tests.cpp`；4 个用例。
- 用例累计耗时中位数：**1.601 秒**；范围 1.601–1.601 秒。
- 各轮结果：{'passed': 4}。
- 未设置的可选开关：`EVENGINE_MESH_VFX_BENCHMARK`, `EVENGINE_SETTLEMENT_BENCHMARK`。
- 最慢用例（跨轮中位数）：`stylize.meshVfx.sceneColorRefractionSamplesBoundTexture` 0.941 秒；`emergence.perf.tenThousandRulesSingleKeyWakeIsSublinear` 0.466 秒；`settlement.benchmark.traceAndIndependentBatchReportP50P95` 0.101 秒。

### 60. [hud_tests.cpp](../../../test/integration/ui-hud/hud_tests.cpp)

- 路径：`test/integration/ui-hud/hud_tests.cpp`；13 个用例。
- 用例累计耗时中位数：**1.400 秒**；范围 1.400–1.400 秒。
- 各轮结果：{'passed': 13}。
- 最慢用例（跨轮中位数）：`UI.hud.componentsReconcileValuesAndKeepInteractionOnSkillButton` 0.189 秒；`UI.hud.texturedFillMatchesQueuedAndNativeDrawGeometry` 0.168 秒；`UI.hud.linearFillCropsUvsAndKeepsReverseEdge` 0.167 秒。

### 61. [physics_joint2d.cpp](../../../test/integration/physics-simulation/physics_joint2d.cpp)

- 路径：`test/integration/physics-simulation/physics_joint2d.cpp`；9 个用例。
- 用例累计耗时中位数：**1.288 秒**；范围 1.288–1.288 秒。
- 各轮结果：{'passed': 9}。
- 最慢用例（跨轮中位数）：`physics.joint2d.linkRejectsForeignWorldAndDestroyedJoint` 0.385 秒；`physics.joint2d.invalidInputDoesNotCreateConstraints` 0.192 秒；`physics.joint2d.lockedWorldRejectsMutation` 0.190 秒。

### 62. [climbing_foot_contacts.cpp](../../../test/integration/physics-simulation/climbing_foot_contacts.cpp)

- 路径：`test/integration/physics-simulation/climbing_foot_contacts.cpp`；8 个用例。
- 用例累计耗时中位数：**1.288 秒**；范围 1.288–1.288 秒。
- 各轮结果：{'passed': 8}。
- 最慢用例（跨轮中位数）：`climbing.corner.outerRightAngleKeepsCapsuleOutsideWall` 0.284 秒；`climbing.corner.innerRightAngleKeepsCapsuleOutsideBothWalls` 0.271 秒；`climbing.footContacts.ladderSocketsFollowMovingBody` 0.252 秒。

### 63. [procgen_road_side_terrain.cpp](../../../test/integration/gameplay-composition/procgen_road_side_terrain.cpp)

- 路径：`test/integration/gameplay-composition/procgen_road_side_terrain.cpp`；6 个用例。
- 用例累计耗时中位数：**1.251 秒**；范围 1.251–1.251 秒。
- 各轮结果：{'passed': 6}。
- 最慢用例（跨轮中位数）：`procgen.road.authoring.sideTerrainBoundsModesConsumeProviderAndStandardRecipe` 0.298 秒；`procgen.road.authoring.sideAlignmentProducesOwnedEulerPoseAndRejectsInvalidNormals` 0.261 秒；`procgen.road.authoring.assetBoundsDriveFootprintAndRetainAdmittedIdentity` 0.234 秒。

### 64. [procgen_road_terrain_author_policy.cpp](../../../test/integration/gameplay-composition/procgen_road_terrain_author_policy.cpp)

- 路径：`test/integration/gameplay-composition/procgen_road_terrain_author_policy.cpp`；5 个用例。
- 用例累计耗时中位数：**1.239 秒**；范围 1.239–1.239 秒。
- 各轮结果：{'passed': 5}。
- 最慢用例（跨轮中位数）：`procgen.road.terrain.authorPolicyNetworkSaveConsumesOneAtomicPlan` 0.465 秒；`procgen.road.terrain.authorPolicyMatchesReferenceDisjointSelectionFixture` 0.291 秒；`procgen.road.terrain.authorPolicyDisabledPlanPreservesHistory` 0.264 秒。

### 65. [procgen_road_extensions.cpp](../../../test/integration/gameplay-composition/procgen_road_extensions.cpp)

- 路径：`test/integration/gameplay-composition/procgen_road_extensions.cpp`；7 个用例。
- 用例累计耗时中位数：**1.176 秒**；范围 1.176–1.176 秒。
- 各轮结果：{'passed': 7}。
- 最慢用例（跨轮中位数）：`procgen.road.authoring.prefabContinuationRejectsSameLabelWithDifferentOwnedGeometry` 0.297 秒；`procgen.road.authoring.prefabStartMiddleEndRepeatsWeldsAndReversesWithoutLosingAssets` 0.251 秒；`procgen.road.authoring.nativeTerrainWorldWritesEveryRoadChannelInOneHistoryFrame` 0.187 秒。

### 66. [ocean_spray_render.cpp](../../../test/integration/ocean-voyage/ocean_spray_render.cpp)

- 路径：`test/integration/ocean-voyage/ocean_spray_render.cpp`；3 个用例。
- 用例累计耗时中位数：**1.154 秒**；范围 1.154–1.154 秒。
- 各轮结果：{'passed': 3}。
- 最慢用例（跨轮中位数）：`fluids.ocean.sprayBatchRendersAndRejectsInvalidPublication` 0.709 秒；`fluids.ocean.sprayRendererObservesProviderRetirement` 0.373 秒；`fluids.ocean.sprayScriptBuildersSurviveGc` 0.072 秒。

### 67. [physics_force_application.cpp](../../../test/integration/physics-simulation/physics_force_application.cpp)

- 路径：`test/integration/physics-simulation/physics_force_application.cpp`；9 个用例。
- 用例累计耗时中位数：**1.152 秒**；范围 1.152–1.152 秒。
- 各轮结果：{'passed': 9}。
- 最慢用例（跨轮中位数）：`physics.forces.batchRejectsInvalidTailBeforeWaking` 0.300 秒；`physics.forces.batchMoveAndForeignWorldCannotDuplicateSubmission` 0.236 秒；`physics.forces.applicationRejectsStaticWithoutMutation` 0.168 秒。

### 68. [weather_lightning_runtime.cpp](../../../test/integration/weather-lightning/weather_lightning_runtime.cpp)

- 路径：`test/integration/weather-lightning/weather_lightning_runtime.cpp`；1 个用例。
- 用例累计耗时中位数：**1.050 秒**；范围 1.050–1.050 秒。
- 各轮结果：{'passed': 1}。
- 最慢用例（跨轮中位数）：`weather.lightning weapon endpoints pulse and stale entity` 1.050 秒。

### 69. [physics_destruction_graphics_depth.cpp](../../../test/integration/house-collapse/physics_destruction_graphics_depth.cpp)

- 路径：`test/integration/house-collapse/physics_destruction_graphics_depth.cpp`；1 个用例。
- 用例累计耗时中位数：**1.048 秒**；范围 1.048–1.048 秒。
- 各轮结果：{'passed': 1}。
- 最慢用例（跨轮中位数）：`physics_destruction_graphics.depthTracksCurrentSubmissionAndExpiresWithRenderer` 1.048 秒。

### 70. [procgen_road_presets.cpp](../../../test/integration/gameplay-composition/procgen_road_presets.cpp)

- 路径：`test/integration/gameplay-composition/procgen_road_presets.cpp`；7 个用例。
- 用例累计耗时中位数：**1.029 秒**；范围 1.029–1.029 秒。
- 各轮结果：{'passed': 7}。
- 最慢用例（跨轮中位数）：`procgen.road.authoring.presetV1RollDefaultsMigrateWithoutChangingSavedBehavior` 0.244 秒；`procgen.road.authoring.presetCandidatesRejectCyclesStaleOwnersAndLateFailuresAtomically` 0.205 秒；`procgen.road.authoring.sharedPresetsPropagateWithExplicitOverridesAndBake` 0.193 秒。

### 71. [physics_joint2d_snapshot.cpp](../../../test/integration/physics-simulation/physics_joint2d_snapshot.cpp)

- 路径：`test/integration/physics-simulation/physics_joint2d_snapshot.cpp`；9 个用例。
- 用例累计耗时中位数：**1.016 秒**；范围 1.016–1.016 秒。
- 各轮结果：{'passed': 9}。
- 最慢用例（跨轮中位数）：`physics.snapshot2d.materialAndDynamicsPreserveMassAndTrajectory` 0.287 秒；`physics.snapshot2d.invalidMaterialAndDynamicsPreserveLiveWorld` 0.211 秒；`physics.joint2d.snapshotRejectsInvalidTopologyAtomically` 0.108 秒。

### 72. [rts_crowd_registry.cpp](../../../test/integration/rts-crowd/rts_crowd_registry.cpp)

- 路径：`test/integration/rts-crowd/rts_crowd_registry.cpp`；12 个用例。
- 用例累计耗时中位数：**0.864 秒**；范围 0.864–0.864 秒。
- 各轮结果：{'passed': 12}。
- 最慢用例（跨轮中位数）：`rts.crowd.mapAdapterPreservesAdjacencyAndDestinationEntryCosts` 0.154 秒；`rts.crowd.duplicateBindingNamesRejectBeforeCreation` 0.077 秒；`rts.crowd.providerDestructionIsObservableAndSafeInBothOrders` 0.076 秒。

### 73. [editor_control_rig.cpp](../../../test/integration/control-rig/editor_control_rig.cpp)

- 路径：`test/integration/control-rig/editor_control_rig.cpp`；7 个用例。
- 用例累计耗时中位数：**0.863 秒**；范围 0.863–0.863 秒。
- 各轮结果：{'passed': 7}。
- 最慢用例（跨轮中位数）：`editor.controlRig.matchPoseIsOneUndoableEdit` 0.189 秒；`editor.controlRig.transactionHistoryAndRoundTrip` 0.185 秒；`editor.controlRig.dragPreviewDoesNotMutateHistory` 0.180 秒。

### 74. [editor_control_rig_automation.cpp](../../../test/integration/control-rig/editor_control_rig_automation.cpp)

- 路径：`test/integration/control-rig/editor_control_rig_automation.cpp`；6 个用例。
- 用例累计耗时中位数：**0.855 秒**；范围 0.855–0.855 秒。
- 各轮结果：{'passed': 6}。
- 最慢用例（跨轮中位数）：`editor.controlRig.automation.sharedDocumentHistoryAndSchema` 0.311 秒；`editor.controlRig.automation.nestedSchemaUsesSharedProjection` 0.213 秒；`editor.controlRig.automation.plansRejectStaleAndInvalidAtomically` 0.089 秒。

### 75. [render_presentation_impasto_tests.cpp](../../../test/integration/render-presentation/render_presentation_impasto_tests.cpp)

- 路径：`test/integration/render-presentation/render_presentation_impasto_tests.cpp`；2 个用例。
- 用例累计耗时中位数：**0.834 秒**；范围 0.834–0.834 秒。
- 各轮结果：{'passed': 2}。
- 最慢用例（跨轮中位数）：`stylize.impasto.realGpuPipeline` 0.728 秒；`stylize.impasto.independentMaterialContract` 0.106 秒。

### 76. [physics_coupling3d.cpp](../../../test/integration/physics-simulation/physics_coupling3d.cpp)

- 路径：`test/integration/physics-simulation/physics_coupling3d.cpp`；7 个用例。
- 用例累计耗时中位数：**0.821 秒**；范围 0.821–0.821 秒。
- 各轮结果：{'passed': 7}。
- 最慢用例（跨轮中位数）：`physics.coupling3d.gearTracksMultipleTurnsAndSourceDeath` 0.340 秒；`physics.coupling3d.mixedRackAndPinion` 0.099 秒；`physics.coupling3d.slackRopeDoesNotPush` 0.083 秒。

### 77. [editor_authority_atomic.cpp](../../../test/integration/transaction-authority/editor_authority_atomic.cpp)

- 路径：`test/integration/transaction-authority/editor_authority_atomic.cpp`；5 个用例。
- 用例累计耗时中位数：**0.819 秒**；范围 0.819–0.819 秒。
- 各轮结果：{'passed': 5}。
- 最慢用例（跨轮中位数）：`editor.atomic.legacy_rollback_exception_is_reported_and_cleanup_continues` 0.263 秒；`editor.atomic.two_agents_same_revision_only_one_commits` 0.237 秒；`editor.atomic.publish_failure_has_no_effect_and_can_retry` 0.195 秒。

### 78. [material_studio_scripts.cpp](../../../test/integration/material-studio/material_studio_scripts.cpp)

- 路径：`test/integration/material-studio/material_studio_scripts.cpp`；4 个用例。
- 用例累计耗时中位数：**0.794 秒**；范围 0.794–0.794 秒。
- 各轮结果：{'passed': 4}。
- 最慢用例（跨轮中位数）：`editor.material.facade_script_projects_owned_values_and_checked_failures` 0.317 秒；`editor.material.studio_publication_clears_resources_and_preserves_preview_on_failure` 0.227 秒；`editor.material.preview_geometry_has_recessed_rings_valid_normals_and_matching_sky_edges` 0.145 秒。

### 79. [animation_atlas_rotation.cpp](../../../test/integration/sprite-atlas/animation_atlas_rotation.cpp)

- 路径：`test/integration/sprite-atlas/animation_atlas_rotation.cpp`；1 个用例。
- 用例累计耗时中位数：**0.711 秒**；范围 0.711–0.711 秒。
- 各轮结果：{'passed': 1}。
- 最慢用例（跨轮中位数）：`animation.sprite.rotatedAtlasPixelsAndLayout` 0.711 秒。

### 80. [PcmStream.cpp](../../../test/integration/audio-playback/PcmStream.cpp)

- 路径：`test/integration/audio-playback/PcmStream.cpp`；4 个用例。
- 用例累计耗时中位数：**0.702 秒**；范围 0.702–0.702 秒。
- 各轮结果：{'passed': 4}。
- 最慢用例（跨轮中位数）：`audio.pcm.starvationPauseResumeAndDrain` 0.288 秒；`audio.pcm.bothDestructionOrders` 0.156 秒；`audio.pcm.validationAndBackpressureAreAtomic` 0.137 秒。

### 81. [climbing_pose_contacts.cpp](../../../test/integration/gameplay-composition/climbing_pose_contacts.cpp)

- 路径：`test/integration/gameplay-composition/climbing_pose_contacts.cpp`；7 个用例。
- 用例累计耗时中位数：**0.670 秒**；范围 0.670–0.670 秒。
- 各轮结果：{'passed': 7}。
- 最慢用例（跨轮中位数）：`climbing.poseContacts.feetRequireTargetsAndRemainIndependent` 0.169 秒；`climbing.poseContacts.aggregateOnlyContactRespectsModelPlacement` 0.159 秒；`climbing.poseContacts.rejectsNonfiniteInputBeforeChangingEitherHand` 0.084 秒。

### 82. [script_rendering_graphics_tests.cpp](../../../test/integration/script-rendering/script_rendering_graphics_tests.cpp)

- 路径：`test/integration/script-rendering/script_rendering_graphics_tests.cpp`；3 个用例。
- 用例累计耗时中位数：**0.606 秒**；范围 0.606–0.606 秒。
- 各轮结果：{'passed': 3}。
- 最慢用例（跨轮中位数）：`WindowTest.basicWindow` 0.305 秒；`WindowTest.settingsFields` 0.200 秒；`GraphicTest.bindings` 0.102 秒。

### 83. [physics_vehicle_tires.cpp](../../../test/integration/physics-simulation/physics_vehicle_tires.cpp)

- 路径：`test/integration/physics-simulation/physics_vehicle_tires.cpp`；6 个用例。
- 用例累计耗时中位数：**0.591 秒**；范围 0.591–0.591 秒。
- 各轮结果：{'passed': 6}。
- 最慢用例（跨轮中位数）：`physics.vehicleTires2D.offCenterCombinedSlipDissipatesAtMultipleScales` 0.209 秒；`physics.vehicleTires2D.invalidTailAndDestroyedBodyKeepCompletedState` 0.087 秒；`physics.vehicleTires2D.brakeAndAirborneRespectContactCapacity` 0.086 秒。

### 84. [physics_wing_assembly.cpp](../../../test/integration/physics-simulation/physics_wing_assembly.cpp)

- 路径：`test/integration/physics-simulation/physics_wing_assembly.cpp`；4 个用例。
- 用例累计耗时中位数：**0.582 秒**；范围 0.582–0.582 秒。
- 各轮结果：{'passed': 4}。
- 最慢用例（跨轮中位数）：`physics.forces.wingReverseFlowAndStallContinuity` 0.248 秒；`physics.forces.wingAssemblyFailureIsAtomic` 0.170 秒；`physics.forces.wingAssembly3DControlSurfacesAndWind` 0.089 秒。

### 85. [gameplay_composition_action_upper_body_orientation_tests.cpp](../../../test/integration/gameplay-composition/gameplay_composition_action_upper_body_orientation_tests.cpp)

- 路径：`test/integration/gameplay-composition/gameplay_composition_action_upper_body_orientation_tests.cpp`；3 个用例。
- 用例累计耗时中位数：**0.565 秒**；范围 0.565–0.565 秒。
- 各轮结果：{'passed': 3}。
- 最慢用例（跨轮中位数）：`actionMontage.upperBodyCompensationObservesProviderMaskAndFullBodyPolicy` 0.286 秒；`actionMontage.upperBodyCompensationRunsBeforeAdditiveLayersDeterministically` 0.171 秒；`actionMontage.upperBodyProfileReplacementIsTransactional` 0.108 秒。

### 86. [procgen_road_reference.cpp](../../../test/integration/gameplay-composition/procgen_road_reference.cpp)

- 路径：`test/integration/gameplay-composition/procgen_road_reference.cpp`；6 个用例。
- 用例累计耗时中位数：**0.548 秒**；范围 0.548–0.548 秒。
- 各轮结果：{'passed': 6}。
- 最慢用例（跨轮中位数）：`procgen.road.network.coincidentEndpointPositionsDoNotMergeNodeIdentity` 0.182 秒；`procgen.road.terrain.originYLegacyMigrationAndDirectHeightmapReceipt` 0.080 秒；`procgen.road.network.nodeMoveRejectsDegenerateIncidentEdgeAtomically` 0.074 秒。

### 87. [material_preview_shadows.cpp](../../../test/integration/material-studio/material_preview_shadows.cpp)

- 路径：`test/integration/material-studio/material_preview_shadows.cpp`；1 个用例。
- 用例累计耗时中位数：**0.526 秒**；范围 0.526–0.526 秒。
- 各轮结果：{'passed': 1}。
- 最慢用例（跨轮中位数）：`editor.material.preview_shadows_affect_pixels_and_capture_path_stays_unshadowed` 0.526 秒。

### 88. [world_sweep.cpp](../../../test/integration/projectile-behavior/world_sweep.cpp)

- 路径：`test/integration/projectile-behavior/world_sweep.cpp`；3 个用例。
- 用例累计耗时中位数：**0.523 秒**；范围 0.523–0.523 秒。
- 各轮结果：{'passed': 3}。
- 最慢用例（跨轮中位数）：`carrierWorldSweep.carrierPierceSnapshotAndFailure` 0.254 秒；`carrierWorldSweep.bodyDestructionAndBudget` 0.193 秒；`carrierWorldSweep.box3dContractAndRestore` 0.076 秒。

### 89. [physics_force_receipt.cpp](../../../test/integration/physics-simulation/physics_force_receipt.cpp)

- 路径：`test/integration/physics-simulation/physics_force_receipt.cpp`；3 个用例。
- 用例累计耗时中位数：**0.448 秒**；范围 0.448–0.448 秒。
- 各轮结果：{'passed': 3}。
- 最慢用例（跨轮中位数）：`physics.forces.receiptRejectsWrongStepAndRetriesBackendOnce` 0.216 秒；`physics.forces.receiptRestoreAndDestroyCancelPendingOnly` 0.168 秒；`physics.forces.receiptRequiresAutomaticForceClearing2D` 0.064 秒。

### 90. [physics_collapse_animation.cpp](../../../test/integration/house-collapse/physics_collapse_animation.cpp)

- 路径：`test/integration/house-collapse/physics_collapse_animation.cpp`；3 个用例。
- 用例累计耗时中位数：**0.440 秒**；范围 0.440–0.440 秒。
- 各轮结果：{'passed': 3}。
- 最慢用例（跨轮中位数）：`physics_collapse_animation.emptyAndStaleRecording` 0.187 秒；`physics_collapse_animation.savedPlaybackOutlivesWorld` 0.182 秒；`physics_collapse_animation.contentKeyIncludesEveryByte` 0.071 秒。

### 91. [procgen_road_recipe_snapshot.cpp](../../../test/integration/gameplay-composition/procgen_road_recipe_snapshot.cpp)

- 路径：`test/integration/gameplay-composition/procgen_road_recipe_snapshot.cpp`；1 个用例。
- 用例累计耗时中位数：**0.429 秒**；范围 0.429–0.429 秒。
- 各轮结果：{'passed': 1}。
- 最慢用例（跨轮中位数）：`procgen.road.authoring.recipeSnapshotConsumesEditorAuthorDataAndLOD` 0.429 秒。

### 92. [physics_collision_regression.cpp](../../../test/integration/physics-simulation/physics_collision_regression.cpp)

- 路径：`test/integration/physics-simulation/physics_collision_regression.cpp`；4 个用例。
- 用例累计耗时中位数：**0.416 秒**；范围 0.416–0.416 秒。
- 各轮结果：{'passed': 4}。
- 最慢用例（跨轮中位数）：`physics.collision.stack.remainsSupportedAfterTenSeconds` 0.175 秒；`physics.collision.query.primitiveHitsHaveNoSourceTriangle` 0.094 秒；`physics.collision.ccd.sphereStopsAtThinWall` 0.083 秒。

### 93. [physics_force_script.cpp](../../../test/integration/physics-simulation/physics_force_script.cpp)

- 路径：`test/integration/physics-simulation/physics_force_script.cpp`；3 个用例。
- 用例累计耗时中位数：**0.412 秒**；范围 0.412–0.412 秒。
- 各轮结果：{'passed': 3}。
- 最慢用例（跨轮中位数）：`physics.forces.scriptWingAssembliesControlBothDimensions` 0.284 秒；`physics.forces.scriptModelsProjectResults` 0.066 秒；`physics.forces.scriptFieldSetsOwnValuesAndBatchRights` 0.062 秒。

### 94. [physics_collapse_dust.cpp](../../../test/integration/house-collapse/physics_collapse_dust.cpp)

- 路径：`test/integration/house-collapse/physics_collapse_dust.cpp`；3 个用例。
- 用例累计耗时中位数：**0.405 秒**；范围 0.405–0.405 秒。
- 各轮结果：{'passed': 3}。
- 最慢用例（跨轮中位数）：`physics_collapse_dust.ownedWorkerIsBoundedNonblockingAndOutlivesSources` 0.231 秒；`physics_collapse_dust.savedGasInjectsExistingFogWithBoundedFootprints` 0.107 秒；`physics_collapse_dust.savedGasOutlivesSolverAndRejectsPartialLoad` 0.067 秒。

### 95. [procgen_road_noop.cpp](../../../test/integration/gameplay-composition/procgen_road_noop.cpp)

- 路径：`test/integration/gameplay-composition/procgen_road_noop.cpp`；5 个用例。
- 用例累计耗时中位数：**0.374 秒**；范围 0.374–0.374 秒。
- 各轮结果：{'passed': 5}。
- 最慢用例（跨轮中位数）：`procgen.road.editingNoOp.buildersReturnSuccessAndPreserveAuthor` 0.085 秒；`procgen.road.editingNoOp.commandAdaptersOmitNoChangeOperations` 0.081 秒；`procgen.road.editingNoOp.sameEndpointStillPlansMissingTurnsAndUndo` 0.071 秒。

### 96. [procgen_road_connections.cpp](../../../test/integration/gameplay-composition/procgen_road_connections.cpp)

- 路径：`test/integration/gameplay-composition/procgen_road_connections.cpp`；2 个用例。
- 用例累计耗时中位数：**0.364 秒**；范围 0.364–0.364 秒。
- 各轮结果：{'passed': 2}。
- 最慢用例（跨轮中位数）：`procgen.road.authoring.customConnectionSnapshotUndoAndSplitRebindStableIds` 0.184 秒；`procgen.road.authoring.closedBeamConnectionValidatesTheEntireSeamLoop` 0.180 秒。

### 97. [climbing_grounding.cpp](../../../test/integration/physics-simulation/climbing_grounding.cpp)

- 路径：`test/integration/physics-simulation/climbing_grounding.cpp`；3 个用例。
- 用例累计耗时中位数：**0.331 秒**；范围 0.331–0.331 秒。
- 各轮结果：{'passed': 3}。
- 最慢用例（跨轮中位数）：`climbing.grounding.tallStepRemainsBlocked` 0.176 秒；`climbing.grounding.lowStepUsesProductionMotor` 0.079 秒；`climbing.grounding.lowCeilingRejectsStepWithoutPublishingRaisedPose` 0.076 秒。

### 98. [procgen_road_side_placement.cpp](../../../test/integration/gameplay-composition/procgen_road_side_placement.cpp)

- 路径：`test/integration/gameplay-composition/procgen_road_side_placement.cpp`；2 个用例。
- 用例累计耗时中位数：**0.324 秒**；范围 0.324–0.324 秒。
- 各轮结果：{'passed': 2}。
- 最慢用例（跨轮中位数）：`procgen.road.authoring.sidePlacementPresetHistoryMigrationAndAtomicFailure` 0.178 秒；`procgen.road.authoring.sidePlacementRotatesTerrainFootprintBeforeWorldUpOffset` 0.146 秒。

### 99. [terrain_road_batch_vm.cpp](../../../test/integration/script-api/terrain_road_batch_vm.cpp)

- 路径：`test/integration/script-api/terrain_road_batch_vm.cpp`；1 个用例。
- 用例累计耗时中位数：**0.324 秒**；范围 0.324–0.324 秒。
- 各轮结果：{'passed': 1}。
- 最慢用例（跨轮中位数）：`procgen.road.terrain.squirrelBatchProductionPoliciesAndAtomicFailure` 0.324 秒。

### 100. [scene_procgen_rotation.cpp](../../../test/integration/gameplay-composition/scene_procgen_rotation.cpp)

- 路径：`test/integration/gameplay-composition/scene_procgen_rotation.cpp`；3 个用例。
- 用例累计耗时中位数：**0.310 秒**；范围 0.310–0.310 秒。
- 各轮结果：{'passed': 3}。
- 最慢用例（跨轮中位数）：`Scene.procgenSink.coordinatesReplacementWithoutLeakingCandidateHosts` 0.170 秒；`Scene.procgenSink.preservesDegreeRotationAcrossSnapshotsAndDeltas` 0.073 秒；`Scene.procgenSink.deltaSnapshotBuilderPreservesIdentityAndRejectsMalformedOrder` 0.067 秒。

### 101. [ocean_script.cpp](../../../test/integration/ocean-voyage/ocean_script.cpp)

- 路径：`test/integration/ocean-voyage/ocean_script.cpp`；2 个用例。
- 用例累计耗时中位数：**0.294 秒**；范围 0.294–0.294 秒。
- 各轮结果：{'passed': 2}。
- 最慢用例（跨轮中位数）：`fluids.ocean.scriptBoatWakeUsesOneFixedClock` 0.229 秒；`fluids.ocean.scriptOwnsSpectrumAndFrameBuilders` 0.065 秒。

### 102. [climbing_contact_presentation.cpp](../../../test/integration/gameplay-composition/climbing_contact_presentation.cpp)

- 路径：`test/integration/gameplay-composition/climbing_contact_presentation.cpp`；1 个用例。
- 用例累计耗时中位数：**0.285 秒**；范围 0.285–0.285 秒。
- 各轮结果：{'passed': 1}。
- 最慢用例（跨轮中位数）：`climbing.contactPresentation.fixedStepBlendReleasesAndRejectsAtomically` 0.285 秒。

### 103. [script_api_procgen_cave_generation_stages_tests.cpp](../../../test/integration/script-api/script_api_procgen_cave_generation_stages_tests.cpp)

- 路径：`test/integration/script-api/script_api_procgen_cave_generation_stages_tests.cpp`；1 个用例。
- 用例累计耗时中位数：**0.249 秒**；范围 0.249–0.249 秒。
- 各轮结果：{'passed': 1}。
- 最慢用例（跨轮中位数）：`procgen.stages.scriptOwnsFieldsAndComposesCaveWithoutGraphics` 0.249 秒。

### 104. [terrain_road_write_vm.cpp](../../../test/integration/script-api/terrain_road_write_vm.cpp)

- 路径：`test/integration/script-api/terrain_road_write_vm.cpp`；1 个用例。
- 用例累计耗时中位数：**0.245 秒**；范围 0.245–0.245 秒。
- 各轮结果：{'passed': 1}。
- 最慢用例（跨轮中位数）：`procgen.road.terrain.squirrelProductionMeshWriteAndUndo` 0.245 秒。

### 105. [physics_coupling3d_script.cpp](../../../test/integration/physics-simulation/physics_coupling3d_script.cpp)

- 路径：`test/integration/physics-simulation/physics_coupling3d_script.cpp`；2 个用例。
- 用例累计耗时中位数：**0.241 秒**；范围 0.241–0.241 秒。
- 各轮结果：{'passed': 2}。
- 最慢用例（跨轮中位数）：`physics.coupling3d.scriptWorldFirstInvalidation` 0.164 秒；`physics.coupling3d.scriptOwnershipAndTypeSafety` 0.076 秒。

### 106. [physics_buoyancy_geometry.cpp](../../../test/integration/physics-simulation/physics_buoyancy_geometry.cpp)

- 路径：`test/integration/physics-simulation/physics_buoyancy_geometry.cpp`；3 个用例。
- 用例累计耗时中位数：**0.234 秒**；范围 0.234–0.234 秒。
- 各轮结果：{'passed': 3}。
- 最慢用例（跨轮中位数）：`physics.forces.immersionMatchesIndependentSliceIntegration` 0.081 秒；`physics.forces.geometricBuoyancyBalancesGravity2DAnd3D` 0.078 秒；`physics.forces.immersionHalfDryFullAndThickness` 0.075 秒。

### 107. [rts_terrain_combat.cpp](../../../test/integration/gameplay-composition/rts_terrain_combat.cpp)

- 路径：`test/integration/gameplay-composition/rts_terrain_combat.cpp`；1 个用例。
- 用例累计耗时中位数：**0.214 秒**；范围 0.214–0.214 秒。
- 各轮结果：{'passed': 1}。
- 最慢用例（跨轮中位数）：`rts.terrainRidgeStopsFireAndTerrainEditsReopenFireLine` 0.214 秒。

### 108. [offscreen_authoring_graphics_tests.cpp](../../../test/integration/offscreen-authoring/offscreen_authoring_graphics_tests.cpp)

- 路径：`test/integration/offscreen-authoring/offscreen_authoring_graphics_tests.cpp`；2 个用例。
- 用例累计耗时中位数：**0.210 秒**；范围 0.210–0.210 秒。
- 各轮结果：{'passed': 2}。
- 最慢用例（跨轮中位数）：`authoringPreview.heightmapMissingProviderPreservesSource` 0.107 秒；`authoringPreview.stylizeMissingProviderPreservesDocument` 0.104 秒。

### 109. [CrowdScript.cpp](../../../test/integration/crowd-runtime/CrowdScript.cpp)

- 路径：`test/integration/crowd-runtime/CrowdScript.cpp`；1 个用例。
- 用例累计耗时中位数：**0.207 秒**；范围 0.207–0.207 秒。
- 各轮结果：{'passed': 1}。
- 最慢用例（跨轮中位数）：`crowd.script.runtimeBindingContracts` 0.207 秒。

### 110. [profile_capture_tests.cpp](../../../test/integration/profiler-capture/profile_capture_tests.cpp)

- 路径：`test/integration/profiler-capture/profile_capture_tests.cpp`；3 个用例。
- 用例累计耗时中位数：**0.204 秒**；范围 0.204–0.204 秒。
- 各轮结果：{'passed': 3}。
- 最慢用例（跨轮中位数）：`profiler.core.reportMeasuresCapturedSpan` 0.078 秒；`profiler.capture.automaticFrameBeginIncludesEarlier3DWork` 0.065 秒；`profiler.core.sameNameDifferentStorageAggregates` 0.061 秒。

### 111. [performance_action_montage_tests.cpp](../../../test/integration/performance/performance_action_montage_tests.cpp)

- 路径：`test/integration/performance/performance_action_montage_tests.cpp`；3 个用例。
- 用例累计耗时中位数：**0.202 秒**；范围 0.202–0.202 秒。
- 各轮结果：{'passed': 3}。
- 未设置的可选开关：`EVENGINE_MONTAGE_BENCHMARK`。
- 最慢用例（跨轮中位数）：`actionMontage.preparedTopologyHandlesDenseSectionSweepsAndJumps` 0.071 秒；`actionMontage.benchmarkPreparedPresentReportsP50P95AndMeasuredAllocationBoundary` 0.069 秒；`actionMontage.rvaluePresentTransfersOwningTimelineProjectionStorage` 0.061 秒。

### 112. [procgen_road_authoring.cpp](../../../test/integration/gameplay-composition/procgen_road_authoring.cpp)

- 路径：`test/integration/gameplay-composition/procgen_road_authoring.cpp`；2 个用例。
- 用例累计耗时中位数：**0.192 秒**；范围 0.192–0.192 秒。
- 各轮结果：{'passed': 2}。
- 最慢用例（跨轮中位数）：`procgen.road.authoring.bridgeTunnelIntervalsKeepTerrainEvenNearItsSurface` 0.108 秒；`procgen.road.authoring.intervalsSplitAndSnapshotsUndoWithoutDuplication` 0.084 秒。

### 113. [procgen_everyday_material_optics.cpp](../../../test/integration/script-api/procgen_everyday_material_optics.cpp)

- 路径：`test/integration/script-api/procgen_everyday_material_optics.cpp`；1 个用例。
- 用例累计耗时中位数：**0.184 秒**；范围 0.184–0.184 秒。
- 各轮结果：{'passed': 1}。
- 最慢用例（跨轮中位数）：`procgen.everyday.scriptOpticsMatchNative` 0.184 秒。

### 114. [environment_query_tests.cpp](../../../test/integration/sensing/environment_query_tests.cpp)

- 路径：`test/integration/sensing/environment_query_tests.cpp`；1 个用例。
- 用例累计耗时中位数：**0.173 秒**；范围 0.173–0.173 秒。
- 各轮结果：{'passed': 1}。
- 最慢用例（跨轮中位数）：`sensing.environment.scriptPositionsUseCanonicalExecutor` 0.173 秒。

### 115. [procgen_road_controllers.cpp](../../../test/integration/gameplay-composition/procgen_road_controllers.cpp)

- 路径：`test/integration/gameplay-composition/procgen_road_controllers.cpp`；1 个用例。
- 用例累计耗时中位数：**0.170 秒**；范围 0.170–0.170 秒。
- 各轮结果：{'passed': 1}。
- 最慢用例（跨轮中位数）：`procgen.road.authoring.outgoingControllerPresetSaveAndUndo` 0.170 秒。

### 116. [procgen_road_sampling_defaults.cpp](../../../test/integration/gameplay-composition/procgen_road_sampling_defaults.cpp)

- 路径：`test/integration/gameplay-composition/procgen_road_sampling_defaults.cpp`；2 个用例。
- 用例累计耗时中位数：**0.155 秒**；范围 0.155–0.155 秒。
- 各轮结果：{'passed': 2}。
- 最慢用例（跨轮中位数）：`procgen.road.authoring.presetV2MigratesEveryHistoryRootWithoutChangingChildInheritance` 0.089 秒；`procgen.road.authoring.presetV2ExplicitSamplingOverridesRetainPresence` 0.066 秒。

### 117. [physics_merge_contract.cpp](../../../test/integration/physics-simulation/physics_merge_contract.cpp)

- 路径：`test/integration/physics-simulation/physics_merge_contract.cpp`；2 个用例。
- 用例累计耗时中位数：**0.147 秒**；范围 0.147–0.147 秒。
- 各轮结果：{'passed': 2}。
- 最慢用例（跨轮中位数）：`physics.merge.planarFactoriesShareIdentityAndSnapshot` 0.080 秒；`physics.merge.spatialAddedJointKindsRetainParameters` 0.068 秒。

### 118. [physics_field_set.cpp](../../../test/integration/physics-simulation/physics_field_set.cpp)

- 路径：`test/integration/physics-simulation/physics_field_set.cpp`；2 个用例。
- 用例累计耗时中位数：**0.147 秒**；范围 0.147–0.147 秒。
- 各轮结果：{'passed': 2}。
- 最慢用例（跨轮中位数）：`physics.forces.fieldSet3DOrderAndAtomicFailures` 0.083 秒；`physics.forces.fieldSetFiltersAndSuperposes2D` 0.064 秒。

### 119. [procgen_road_position_split.cpp](../../../test/integration/gameplay-composition/procgen_road_position_split.cpp)

- 路径：`test/integration/gameplay-composition/procgen_road_position_split.cpp`；2 个用例。
- 用例累计耗时中位数：**0.140 秒**；范围 0.140–0.140 秒。
- 各轮结果：{'passed': 2}。
- 最慢用例（跨轮中位数）：`procgen.road.editing.connectNodeAtPositionSnapsToVisibleCurve` 0.073 秒；`procgen.road.editing.positionSplitUndoRedoKeepsProjectedPointAndIds` 0.067 秒。

### 120. [procgen_road_junction_style.cpp](../../../test/integration/gameplay-composition/procgen_road_junction_style.cpp)

- 路径：`test/integration/gameplay-composition/procgen_road_junction_style.cpp`；1 个用例。
- 用例累计耗时中位数：**0.137 秒**；范围 0.137–0.137 秒。
- 各轮结果：{'passed': 1}。
- 最慢用例（跨轮中位数）：`procgen.road.junctionStyle.saveRestoreAndUndoReproduceGeometry` 0.137 秒。

### 121. [procgen_road_roll_pivot.cpp](../../../test/integration/gameplay-composition/procgen_road_roll_pivot.cpp)

- 路径：`test/integration/gameplay-composition/procgen_road_roll_pivot.cpp`；1 个用例。
- 用例累计耗时中位数：**0.132 秒**；范围 0.132–0.132 秒。
- 各轮结果：{'passed': 1}。
- 最慢用例（跨轮中位数）：`procgen.road.authoring.rollPivotRecipeSnapshotReverseSplitAndUndo` 0.132 秒。

### 122. [procgen_road_full_roll.cpp](../../../test/integration/gameplay-composition/procgen_road_full_roll.cpp)

- 路径：`test/integration/gameplay-composition/procgen_road_full_roll.cpp`；1 个用例。
- 用例累计耗时中位数：**0.117 秒**；范围 0.117–0.117 秒。
- 各轮结果：{'passed': 1}。
- 最慢用例（跨轮中位数）：`procgen.road.authoring.fullRollPreservesReferenceRotationAndAtomicHistory` 0.117 秒。

### 123. [file_io_devtools_frame_capture_tests.cpp](../../../test/integration/file-io/file_io_devtools_frame_capture_tests.cpp)

- 路径：`test/integration/file-io/file_io_devtools_frame_capture_tests.cpp`；1 个用例。
- 用例累计耗时中位数：**0.095 秒**；范围 0.095–0.095 秒。
- 各轮结果：{'passed': 1}。
- 最慢用例（跨轮中位数）：`mcp.frameCapture.protocolPreviewAndAtomicNativeExport` 0.095 秒。

### 124. [procgen_road_parameter_sampling.cpp](../../../test/integration/gameplay-composition/procgen_road_parameter_sampling.cpp)

- 路径：`test/integration/gameplay-composition/procgen_road_parameter_sampling.cpp`；1 个用例。
- 用例累计耗时中位数：**0.087 秒**；范围 0.087–0.087 秒。
- 各轮结果：{'passed': 1}。
- 最慢用例（跨轮中位数）：`procgen.road.authoring.parameterScanPolicySaveUndoAndLegacyMigration` 0.087 秒。

### 125. [editor_control_rig_limits.cpp](../../../test/integration/control-rig/editor_control_rig_limits.cpp)

- 路径：`test/integration/control-rig/editor_control_rig_limits.cpp`；1 个用例。
- 用例累计耗时中位数：**0.083 秒**；范围 0.083–0.083 秒。
- 各轮结果：{'passed': 1}。
- 最慢用例（跨轮中位数）：`editor.controlRig.limits.sharedHistoryAndAtomicRejection` 0.083 秒。

### 126. [physics_layered_cloth_script.cpp](../../../test/integration/layered-cloth/physics_layered_cloth_script.cpp)

- 路径：`test/integration/layered-cloth/physics_layered_cloth_script.cpp`；1 个用例。
- 用例累计耗时中位数：**0.081 秒**；范围 0.081–0.081 秒。
- 各轮结果：{'passed': 1}。
- 最慢用例（跨轮中位数）：`PhysicsLayeredCloth.scriptBindings` 0.081 秒。

### 127. [gameplay_composition_action_preview_profile_tests.cpp](../../../test/integration/gameplay-composition/gameplay_composition_action_preview_profile_tests.cpp)

- 路径：`test/integration/gameplay-composition/gameplay_composition_action_preview_profile_tests.cpp`；1 个用例。
- 用例累计耗时中位数：**0.078 秒**；范围 0.078–0.078 秒。
- 各轮结果：{'passed': 1}。
- 最慢用例（跨轮中位数）：`actionPreviewProfile.sidecarStoreRoundTripsAndRejectsIdentityMismatch` 0.078 秒。

### 128. [terrain_world_origin_vm.cpp](../../../test/integration/script-api/terrain_world_origin_vm.cpp)

- 路径：`test/integration/script-api/terrain_world_origin_vm.cpp`；1 个用例。
- 用例累计耗时中位数：**0.077 秒**；范围 0.077–0.077 秒。
- 各轮结果：{'passed': 1}。
- 最慢用例（跨轮中位数）：`procgen.road.worldOrigin.squirrelCreationAndWorldHeightUseBaseline` 0.077 秒。

### 129. [physics_mover_grounding.cpp](../../../test/integration/physics-simulation/physics_mover_grounding.cpp)

- 路径：`test/integration/physics-simulation/physics_mover_grounding.cpp`；1 个用例。
- 用例累计耗时中位数：**0.075 秒**；范围 0.075–0.075 秒。
- 各轮结果：{'passed': 1}。
- 最慢用例（跨轮中位数）：`physics.mover.grounding.supportAndFilteringContract` 0.075 秒。

### 130. [procgen_road_sampling.cpp](../../../test/integration/gameplay-composition/procgen_road_sampling.cpp)

- 路径：`test/integration/gameplay-composition/procgen_road_sampling.cpp`；1 个用例。
- 用例累计耗时中位数：**0.075 秒**；范围 0.075–0.075 秒。
- 各轮结果：{'passed': 1}。
- 最慢用例（跨轮中位数）：`procgen.road.authoring.samplingSettingsSaveUndoMigrateAndRejectBudgets` 0.075 秒。

### 131. [procgen_road_circular.cpp](../../../test/integration/gameplay-composition/procgen_road_circular.cpp)

- 路径：`test/integration/gameplay-composition/procgen_road_circular.cpp`；1 个用例。
- 用例累计耗时中位数：**0.073 秒**；范围 0.073–0.073 秒。
- 各轮结果：{'passed': 1}。
- 最慢用例（跨轮中位数）：`procgen.road.authoring.circularArcLimitsAndPresetUndo` 0.073 秒。

### 132. [avatar_appearance_script.cpp](../../../test/integration/avatar-appearance/avatar_appearance_script.cpp)

- 路径：`test/integration/avatar-appearance/avatar_appearance_script.cpp`；1 个用例。
- 用例累计耗时中位数：**0.071 秒**；范围 0.071–0.071 秒。
- 各轮结果：{'passed': 1}。
- 最慢用例（跨轮中位数）：`avatar.appearance.scriptBoundaryUsesCheckedResults` 0.071 秒。

### 133. [module_composition_devtools_frame_capture_tests.cpp](../../../test/integration/module-composition/module_composition_devtools_frame_capture_tests.cpp)

- 路径：`test/integration/module-composition/module_composition_devtools_frame_capture_tests.cpp`；1 个用例。
- 用例累计耗时中位数：**0.071 秒**；范围 0.071–0.071 秒。
- 各轮结果：{'passed': 1}。
- 最慢用例（跨轮中位数）：`mcp.frameCapture.providerAbsentAndSchemaRoutes` 0.071 秒。

### 134. [procgen_road_strength.cpp](../../../test/integration/gameplay-composition/procgen_road_strength.cpp)

- 路径：`test/integration/gameplay-composition/procgen_road_strength.cpp`；1 个用例。
- 用例累计耗时中位数：**0.069 秒**；范围 0.069–0.069 秒。
- 各轮结果：{'passed': 1}。
- 最慢用例（跨轮中位数）：`procgen.road.authoring.splineStrengthPresetOverrideSaveAndUndo` 0.069 秒。

### 135. [procgen_road_surface_path.cpp](../../../test/integration/gameplay-composition/procgen_road_surface_path.cpp)

- 路径：`test/integration/gameplay-composition/procgen_road_surface_path.cpp`；1 个用例。
- 用例累计耗时中位数：**0.068 秒**；范围 0.068–0.068 秒。
- 各轮结果：{'passed': 1}。
- 最慢用例（跨轮中位数）：`procgen.road.surfacePath.retainsPresetHistoryAndMigratesMissingPolicy` 0.068 秒。

### 136. [physics_joint2d_script.cpp](../../../test/integration/physics-simulation/physics_joint2d_script.cpp)

- 路径：`test/integration/physics-simulation/physics_joint2d_script.cpp`；1 个用例。
- 用例累计耗时中位数：**0.066 秒**；范围 0.066–0.066 秒。
- 各轮结果：{'passed': 1}。
- 最慢用例（跨轮中位数）：`physics.joint2d.scriptResultAndOwningRelease` 0.066 秒。

### 137. [procgen_native_proxy_reuse.cpp](../../../test/integration/script-api/procgen_native_proxy_reuse.cpp)

- 路径：`test/integration/script-api/procgen_native_proxy_reuse.cpp`；1 个用例。
- 用例累计耗时中位数：**0.064 秒**；范围 0.064–0.064 秒。
- 各轮结果：{'passed': 1}。
- 最慢用例（跨轮中位数）：`procgen.script.nativeProxyAddressReuseInvalidatesOldInstance` 0.064 秒。

### 138. [scene_transaction_observer.cpp](../../../test/integration/scene-editing/scene_transaction_observer.cpp)

- 路径：`test/integration/scene-editing/scene_transaction_observer.cpp`；1 个用例。
- 用例累计耗时中位数：**0.059 秒**；范围 0.059–0.059 秒。
- 各轮结果：{'passed': 1}。
- 最慢用例（跨轮中位数）：`scene.transaction.observer_exception_does_not_reject_published_state` 0.059 秒。
