# 当前 138 个集成测试 CPP 的运行耗时

这是六边形、体素及脚本场景删减前的实测快照（3591 个注册用例），保留作历史基线。后续变更与新测量见 [六边形契约缩减](hex-level-contract-reduction.md) 、[体素契约缩减](voxel-contract-reduction.md) 和 [脚本场景契约精简](script-scene-contract-reduction.md)；本报告中六边形文件的 71 个用例/84.649 秒、体素聚合文件的 100 个用例/91.664 秒不代表删减后的状态。

实测日期：2026-10-25。Windows x64，MSVC Debug、SHARED，原有逐用例进程隔离，CTest 4 并发。仅运行当前 `test/integration` 目录对应的全部默认桌面注册用例，不编译、不切换源文件、不改用 bundle。

覆盖 138 个 CPP、3591 个用例；完成 1 轮。

- 第 1 轮：墙钟 360.420 秒，CTest 退出码 8，结果 {'passed': 3552, 'skipped': 15, 'failed': 24}。

整批墙钟：360.420 秒；用例进程累计：1431.291 秒；全部注册用例均通过的文件 133/138。

**口径：下面每个文件的秒数，是该文件所有独立用例进程耗时的累计值（多轮时取中位数），包含进程启动和重复初始化。它不是 CPP 在单进程 bundle 内运行的时间，也不是独立串行运行该文件的实测墙钟；多个文件并发，所以这些值不能相加当作整批墙钟。min–max 是各轮文件累计值范围。单轮没有可用的波动估计，不能用于证明性能改进。失败文件的数字只是本次执行到失败的开销。**

环境固定为 `EVENGINE_VIEW_SECONDS=0.3`、`EVENGINE_PERF_FRAMES=30`。可选 BENCHMARK 和消息框开关未设置；包含此类分支的文件注明开关，进程成功不等于未启用分支的工作负载已经执行。编译期禁用/其他平台的用例不包含在当前桌面运行数字中。失败、超时、跳过原样保留，不能据此宣称全通过。ClassicScenes 的缺失资源跳过项不能当作已测得完整场景运行成本。

此前受限环境探测轮保存于 `.local-debug/integration-file-timing/`，出现 AppData 写入 permission denied，未作为正常环境耗时样本。随后以相同源文件/二进制、允许既有测试目录写入的环境执行本次测量；没有修改用例、断言或下载额外场景资源。两轮受 CTest 历史 cost 排序及缓存状态影响，不构成性能前后对比。

完整机器数据见 [JSON](integration-file-runtime.json) 和 [CSV](integration-file-runtime.csv)。本机逐用例 JUnit、CTest 日志及冻结选择见 `.local-debug/integration-file-timing-native/`（不提交）。

## 如何使用这次结果

前 10 个文件占本次用例进程累计耗时的 49.5%。优先复审这些文件比平均减少每个文件的用例更有针对性，但其中包含失败、跳过，比例仅代表本次实际开销。

- `script_api_graphics_scene_tests.cpp`：150 个用例累计 104.672 秒，全部通过。其中 `scriptCompiler.compilesRepositoryNutCompatibilityBaseline` 单独 63.454 秒；应先明确全仓脚本兼容性扫描应由哪条门禁负责，再判断是否需要每次集成回归都执行完整语料。
- `render_presentation_graphics_voxel_face_dir_tests.cpp`：100 个用例累计 91.664 秒，全部通过。适合结合边界与独有价值审查，区分必须观察真实 GPU 的路径和可以留在局部测试的方向/参数矩阵。
- `render_presentation_graphics_hex_level_simulation_tests.cpp`：71 个用例累计 84.649 秒，全部通过。应按场景契约核对重复初始化与完整流程，不能仅凭运行成本删除关卡行为。
- `physics_layered_cloth_gpu.cpp`：14 个用例累计 69.329 秒，全部通过。其中一分钟模拟稳定性 34.444 秒，同步计时基线 15.282 秒；前者具有独立长时稳定性价值，不能当作重复参数矩阵直接删除。
- `card_rts_payment_card_tests.cpp`：11 个用例累计 35.187 秒，其中真实 RTS 脚本任务 33.820 秒。单个端到端任务主导文件成本，应先检查其等待/推进策略，同时保留真实跨域支付与补偿语义。

五个含失败或跳过的文件是 `render_presentation_graphics_gpu_driven_tests.cpp`、`render_presentation_graphics_RenderSystem3D_tests.cpp`、`render_presentation_graphics_RenderImageAudit_tests.cpp`、`render_presentation_graphics_ClassicScenes_tests.cpp` 和 `render_presentation_graphics_procgen_tests.cpp`。它们的正常成功完成耗时尚未得到本次测量证明。失败原因和缺失资源信息保留在 JSON 的 `nonpassing_cases` 中。

本次只生成运行成本证据，没有修改测试、生产接口、ECS、持久格式、模块边界或生命周期，因此没有新增架构例外；数据条目数、3591 个注册身份、JUnit 完整性及 `git diff --check` 已核对。没有为纯测量修改运行策略，也没有运行额外全库构建或 `make check`。

## 按累计耗时降序逐文件列出

### 1. [script_api_graphics_scene_tests.cpp](../../../test/integration/script-api/script_api_graphics_scene_tests.cpp)

- 路径：`test/integration/script-api/script_api_graphics_scene_tests.cpp`；150 个用例。
- 用例累计耗时中位数：**104.672 秒**；范围 104.672–104.672 秒。
- 各轮结果：{'passed': 150}。
- 最慢用例（跨轮中位数）：`scriptCompiler.compilesRepositoryNutCompatibilityBaseline` 63.454 秒；`Scene.render.parentChildOrbitPreview` 2.322 秒；`scriptCompiler.servesLanguageToolingQueries` 1.840 秒。

### 2. [render_presentation_graphics_voxel_face_dir_tests.cpp](../../../test/integration/render-presentation/render_presentation_graphics_voxel_face_dir_tests.cpp)

- 路径：`test/integration/render-presentation/render_presentation_graphics_voxel_face_dir_tests.cpp`；100 个用例。
- 用例累计耗时中位数：**91.664 秒**；范围 91.664–91.664 秒。
- 各轮结果：{'passed': 100}。
- 最慢用例（跨轮中位数）：`softbody.render.cloth3DPreview` 5.562 秒；`softbody.cloth3d.collidesWithWorld3D` 3.880 秒；`softbody.cloth3d.momentumExchangeScalesWithMass` 3.075 秒。

### 3. [render_presentation_graphics_hex_level_simulation_tests.cpp](../../../test/integration/render-presentation/render_presentation_graphics_hex_level_simulation_tests.cpp)

- 路径：`test/integration/render-presentation/render_presentation_graphics_hex_level_simulation_tests.cpp`；71 个用例。
- 用例累计耗时中位数：**84.649 秒**；范围 84.649–84.649 秒。
- 各轮结果：{'passed': 71}。
- 最慢用例（跨轮中位数）：`ik.solver2d.armReachPreview` 2.659 秒；`inventory.bag.uiPreview` 2.651 秒；`map.path.astar.routePreview` 2.531 秒。

### 4. [render_presentation_graphics_gpu_driven_tests.cpp](../../../test/integration/render-presentation/render_presentation_graphics_gpu_driven_tests.cpp)

- 路径：`test/integration/render-presentation/render_presentation_graphics_gpu_driven_tests.cpp`；52 个用例。
- 用例累计耗时中位数：**84.601 秒**；范围 84.601–84.601 秒。
- 各轮结果：{'passed': 41, 'failed': 11}。
- 最慢用例（跨轮中位数）：`graphics.clusteredLighting.reusesUnchangedStorageBindings` 18.620 秒；`graphics.deferredLighting.reusesCompletedFrameResources` 14.065 秒；`animation.spine.windowDemoRealModels` 4.116 秒。

### 5. [render_presentation_graphics_RenderSystem3D_tests.cpp](../../../test/integration/render-presentation/render_presentation_graphics_RenderSystem3D_tests.cpp)

- 路径：`test/integration/render-presentation/render_presentation_graphics_RenderSystem3D_tests.cpp`；76 个用例。
- 用例累计耗时中位数：**76.064 秒**；范围 76.064–76.064 秒。
- 各轮结果：{'passed': 73, 'failed': 3}。
- 未设置的可选开关：`EVENGINE_SKY_BENCHMARK`。
- 最慢用例（跨轮中位数）：`daynight.sky injected cloud time changes authored wisps and closes the morph cycle` 5.262 秒；`daynight.sky optical cycle survives midnight and restores the same rendered snapshot` 5.025 秒；`graphics.sky authored wisps composite into the shared capture path` 2.743 秒。

### 6. [physics_layered_cloth_gpu.cpp](../../../test/integration/layered-cloth/physics_layered_cloth_gpu.cpp)

- 路径：`test/integration/layered-cloth/physics_layered_cloth_gpu.cpp`；14 个用例。
- 用例累计耗时中位数：**69.329 秒**；范围 69.329–69.329 秒。
- 各轮结果：{'passed': 14}。
- 最慢用例（跨轮中位数）：`PhysicsLayeredClothGPU.fallingSheetsRemainSeparatedForOneSimulatedMinute` 34.444 秒；`PhysicsLayeredClothGPU.stabilityAndSynchronousTimingBaseline` 15.281 秒；`PhysicsLayeredClothGPU.foldedSelfContactSeparatesWithoutCentreDrift` 2.671 秒。

### 7. [render_presentation_graphics_RenderImageAudit_tests.cpp](../../../test/integration/render-presentation/render_presentation_graphics_RenderImageAudit_tests.cpp)

- 路径：`test/integration/render-presentation/render_presentation_graphics_RenderImageAudit_tests.cpp`；58 个用例。
- 用例累计耗时中位数：**61.457 秒**；范围 61.457–61.457 秒。
- 各轮结果：{'passed': 56, 'failed': 2}。
- 最慢用例（跨轮中位数）：`graphics.imageAudit.postFx` 2.171 秒；`graphics.imageAudit.pipelineConfigs` 1.747 秒；`graphics.imageAudit.materialsAndCamera` 1.640 秒。

### 8. [render_pipeline_graphics_graphics_backend_parity_tests.cpp](../../../test/integration/render-pipeline/render_pipeline_graphics_graphics_backend_parity_tests.cpp)

- 路径：`test/integration/render-pipeline/render_pipeline_graphics_graphics_backend_parity_tests.cpp`；67 个用例。
- 用例累计耗时中位数：**45.945 秒**；范围 45.945–45.945 秒。
- 各轮结果：{'passed': 67}。
- 未设置的可选开关：`EVENGINE_ECS_MILLION_BENCHMARK`, `EVENGINE_PCG_MILLION_BENCHMARK`。
- 最慢用例（跨轮中位数）：`graphics.backendParity.pixelWorldMillionPixelAtlas` 3.339 秒；`fluids.gpu.surfaceFlow` 1.459 秒；`graphics.raytracing.reflectionHitVisibility` 1.454 秒。

### 9. [render_presentation_graphics_ClassicScenes_tests.cpp](../../../test/integration/render-presentation/render_presentation_graphics_ClassicScenes_tests.cpp)

- 路径：`test/integration/render-presentation/render_presentation_graphics_ClassicScenes_tests.cpp`；56 个用例。
- 用例累计耗时中位数：**45.843 秒**；范围 45.843–45.843 秒。
- 各轮结果：{'passed': 34, 'skipped': 15, 'failed': 7}。
- 最慢用例（跨轮中位数）：`ClassicScenes.pbrChart.flythroughConfigs` 2.806 秒；`ClassicScenes.perf.maxFps` 2.558 秒；`ClassicScenes.cornell.flythroughConfigs` 2.494 秒。

### 10. [asset_import_graphics_tests.cpp](../../../test/integration/asset-import/asset_import_graphics_tests.cpp)

- 路径：`test/integration/asset-import/asset_import_graphics_tests.cpp`；102 个用例。
- 用例累计耗时中位数：**43.903 秒**；范围 43.903–43.903 秒。
- 各轮结果：{'passed': 102}。
- 最慢用例（跨轮中位数）：`animation.mixamo.skeletonIdleRunJumpPreview` 3.524 秒；`model3d.render.rock1Fbx` 2.391 秒；`model3d.render.cesiumManGltf` 2.323 秒。

### 11. [render_presentation_graphics_window_cpp_tests.cpp](../../../test/integration/render-presentation/render_presentation_graphics_window_cpp_tests.cpp)

- 路径：`test/integration/render-presentation/render_presentation_graphics_window_cpp_tests.cpp`；21 个用例。
- 用例累计耗时中位数：**36.128 秒**；范围 36.128–36.128 秒。
- 各轮结果：{'passed': 21}。
- 未设置的可选开关：`EVENGINE_TEST_MSGBOX`。
- 最慢用例（跨轮中位数）：`window.minimizeRestoreRoundTrip` 12.952 秒；`graphics.lifecycle.minimizeRestoreStillPresents` 10.896 秒；`window.setFullscreenDesktopRoundTripUsesSettings` 1.025 秒。

### 12. [card_rts_payment_card_tests.cpp](../../../test/integration/card-rts-payment/card_rts_payment_card_tests.cpp)

- 路径：`test/integration/card-rts-payment/card_rts_payment_card_tests.cpp`；11 个用例。
- 用例累计耗时中位数：**35.187 秒**；范围 35.187–35.187 秒。
- 各轮结果：{'passed': 11}。
- 最慢用例（跨轮中位数）：`card.rtsScriptScenarioCollectsBuildsProducesAndPlaysAtomicSkills` 33.820 秒；`card.genericBatchRejectsAdvancedRtsContextBeforePayment` 0.279 秒；`card.rtsScriptGatheringUsesConfiguredResourceAndDropoff` 0.244 秒。

### 13. [asset_pipeline_graphics_import_workflows_tests.cpp](../../../test/integration/asset-pipeline/asset_pipeline_graphics_import_workflows_tests.cpp)

- 路径：`test/integration/asset-pipeline/asset_pipeline_graphics_import_workflows_tests.cpp`；139 个用例。
- 用例累计耗时中位数：**34.224 秒**；范围 34.224–34.224 秒。
- 各轮结果：{'passed': 139}。
- 最慢用例（跨轮中位数）：`asset.import.unityFbxCharacterProducesSkinAndAnimation` 2.221 秒；`asset.import.unityStaticPrefabRendersOwnedEcsInstances` 2.073 秒；`asset.import.unityVegetationNormalPreservesLinearSourceAndGeneratesHeight` 1.766 秒。

### 14. [render_presentation_graphics_procgen_tests.cpp](../../../test/integration/render-presentation/render_presentation_graphics_procgen_tests.cpp)

- 路径：`test/integration/render-presentation/render_presentation_graphics_procgen_tests.cpp`；30 个用例。
- 用例累计耗时中位数：**32.104 秒**；范围 32.104–32.104 秒。
- 各轮结果：{'passed': 29, 'failed': 1}。
- 最慢用例（跨轮中位数）：`particles.gpu.residentSimulationAndIndirectRendering` 3.216 秒；`particles.playback.renderDistanceTrailAndLoopingBurst` 2.713 秒；`particles.scalability.renderBudgetStressScene` 2.702 秒。

### 15. [render_pipeline_graphics_graphics_vegetation_extras_tests.cpp](../../../test/integration/render-pipeline/render_pipeline_graphics_graphics_vegetation_extras_tests.cpp)

- 路径：`test/integration/render-pipeline/render_pipeline_graphics_graphics_vegetation_extras_tests.cpp`；40 个用例。
- 用例累计耗时中位数：**32.082 秒**；范围 32.082–32.082 秒。
- 各轮结果：{'passed': 40}。
- 最慢用例（跨轮中位数）：`graphics.Grass.gpuRenderScreenshot` 2.330 秒；`tensor.gpu.kernelFamiliesMatchEager` 1.467 秒；`tensor.onnx.gpu.parallelCompilation` 1.444 秒。

### 16. [file_io_graphics_tensor_llm_tests.cpp](../../../test/integration/file-io/file_io_graphics_tensor_llm_tests.cpp)

- 路径：`test/integration/file-io/file_io_graphics_tensor_llm_tests.cpp`；31 个用例。
- 用例累计耗时中位数：**31.685 秒**；范围 31.685–31.685 秒。
- 各轮结果：{'passed': 31}。
- 最慢用例（跨轮中位数）：`devtools.projectIndex.automaticWithoutManifestReflectsDomainChainAndFields` 6.715 秒；`devtools.languageServer.completesChainedAndConstructedNativeTypes` 3.913 秒；`devtools.projectIndex.automaticRejectsShadowedAndDynamicNativeLookalikes` 3.446 秒。

### 17. [file_io_graphics_hex_level_data_tests.cpp](../../../test/integration/file-io/file_io_graphics_hex_level_data_tests.cpp)

- 路径：`test/integration/file-io/file_io_graphics_hex_level_data_tests.cpp`；91 个用例。
- 用例累计耗时中位数：**29.740 秒**；范围 29.740–29.740 秒。
- 各轮结果：{'passed': 91}。
- 最慢用例（跨轮中位数）：`animation.skinned.render.playerDriven` 2.741 秒；`filesystem.watch.coalescesOverlappingRegistrations` 2.500 秒；`animation.skinned.render.applyToMesh` 1.765 秒。

### 18. [script_api_graphics_mcp_tests.cpp](../../../test/integration/script-api/script_api_graphics_mcp_tests.cpp)

- 路径：`test/integration/script-api/script_api_graphics_mcp_tests.cpp`；87 个用例。
- 用例累计耗时中位数：**27.174 秒**；范围 27.174–27.174 秒。
- 各轮结果：{'passed': 87}。
- 最慢用例（跨轮中位数）：`devtools.mcp.stderrCaptureFeedsConsole` 4.747 秒；`moduleExpose.allBindingsPreserveProcgenRockSurface` 1.117 秒；`moduleExpose.allBindingsPreserveScriptEcsOverrides` 0.968 秒。

### 19. [concurrency_graphics_tests.cpp](../../../test/integration/concurrency/concurrency_graphics_tests.cpp)

- 路径：`test/integration/concurrency/concurrency_graphics_tests.cpp`；113 个用例。
- 用例累计耗时中位数：**25.905 秒**；范围 25.905–25.905 秒。
- 各轮结果：{'passed': 113}。
- 未设置的可选开关：`EVENGINE_PIXELWORLD_ACTIVE_BENCHMARK`, `EVENGINE_PIXELWORLD_MILLION_BENCHMARK`。
- 最慢用例（跨轮中位数）：`devtools.architecture.mcpStdioListsAndQueriesGraph` 1.493 秒；`devtools.mcp.everyDeclaredToolIsRouted` 1.252 秒；`render_graph.jobSystemEngineFramePattern` 1.119 秒。

### 20. [gameplay_composition_graphics_procgen_core_boundary_tests.cpp](../../../test/integration/gameplay-composition/gameplay_composition_graphics_procgen_core_boundary_tests.cpp)

- 路径：`test/integration/gameplay-composition/gameplay_composition_graphics_procgen_core_boundary_tests.cpp`；118 个用例。
- 用例累计耗时中位数：**23.882 秒**；范围 23.882–23.882 秒。
- 各轮结果：{'passed': 118}。
- 最慢用例（跨轮中位数）：`procgen.e2e.snapshotRoundTripPreservesIdentityBuildKeyAndQueries` 1.285 秒；`ui.pcgPhotoModeApply.lightingSkyboxAuthorityDrivesProceduralSkyAndRestores` 1.230 秒；`ui.pcgPhotoModeApply.lightingSunAuthorityPersistsAcrossDayNightUpdates` 1.059 秒。

### 21. [gameplay_composition_graphics_editor_camera_target_tests.cpp](../../../test/integration/gameplay-composition/gameplay_composition_graphics_editor_camera_target_tests.cpp)

- 路径：`test/integration/gameplay-composition/gameplay_composition_graphics_editor_camera_target_tests.cpp`；127 个用例。
- 用例累计耗时中位数：**23.153 秒**；范围 23.153–23.153 秒。
- 各轮结果：{'passed': 127}。
- 最慢用例（跨轮中位数）：`editor.v2.asset_database_indexes_and_pages_100k_records` 4.396 秒；`editor.camera.invalid_snapshot_and_runtime_generation_are_atomic` 0.405 秒；`editor.behavior.graph_compiles_deterministic_tree` 0.395 秒。

### 22. [gameplay_composition_graphics_rts_script_tests.cpp](../../../test/integration/gameplay-composition/gameplay_composition_graphics_rts_script_tests.cpp)

- 路径：`test/integration/gameplay-composition/gameplay_composition_graphics_rts_script_tests.cpp`；121 个用例。
- 用例累计耗时中位数：**22.238 秒**；范围 22.238–22.238 秒。
- 各轮结果：{'passed': 121}。
- 最慢用例（跨轮中位数）：`rts.sandboxInitializesStepsAndRebuildsAcrossReload` 1.892 秒；`softbody.volume.renderPreview` 1.467 秒；`softbody.volume.pinsAndWorldCollision` 0.731 秒。

### 23. [gameplay_composition_graphics_graphics_vegetation_tests.cpp](../../../test/integration/gameplay-composition/gameplay_composition_graphics_graphics_vegetation_tests.cpp)

- 路径：`test/integration/gameplay-composition/gameplay_composition_graphics_graphics_vegetation_tests.cpp`；108 个用例。
- 用例累计耗时中位数：**21.100 秒**；范围 21.100–21.100 秒。
- 各轮结果：{'passed': 108}。
- 最慢用例（跨轮中位数）：`GraphicsPrimitives.sceneCacheOnlyRebuildsUpdatedSlots` 1.812 秒；`graphics.vegetation.detail_layer_matches_tve_replace_formula` 0.595 秒；`graphics.vegetation.gpu_field_binding_is_coherent_stale_safe_and_atomic` 0.585 秒。

### 24. [gameplay_composition_graphics_gameplay_action_tests.cpp](../../../test/integration/gameplay-composition/gameplay_composition_graphics_gameplay_action_tests.cpp)

- 路径：`test/integration/gameplay-composition/gameplay_composition_graphics_gameplay_action_tests.cpp`；117 个用例。
- 用例累计耗时中位数：**20.399 秒**；范围 20.399–20.399 秒。
- 各轮结果：{'passed': 117}。
- 最慢用例（跨轮中位数）：`gameplay.control.routerDispatchesAmongTwoWeaponInstances` 1.054 秒；`editor.v2.validation_and_diagnostics_are_extension_owned` 0.410 秒；`editor.ui.snapshot_load_is_atomic_and_rejects_invalid_hierarchies` 0.406 秒。

### 25. [script_api_graphics_ScriptECS_tests.cpp](../../../test/integration/script-api/script_api_graphics_ScriptECS_tests.cpp)

- 路径：`test/integration/script-api/script_api_graphics_ScriptECS_tests.cpp`；108 个用例。
- 用例累计耗时中位数：**19.088 秒**；范围 19.088–19.088 秒。
- 各轮结果：{'passed': 108}。
- 最慢用例（跨轮中位数）：`ScriptECS.componentRetiredGenerationKeepsItsOwnFields` 0.504 秒；`ECS.script.cppCreatedEntitiesVisible` 0.490 秒；`climbing.playground.mainScriptCompilesThroughEveScriptFrontend` 0.426 秒。

### 26. [physics_simulation_graphics_box3d_tests.cpp](../../../test/integration/physics-simulation/physics_simulation_graphics_box3d_tests.cpp)

- 路径：`test/integration/physics-simulation/physics_simulation_graphics_box3d_tests.cpp`；127 个用例。
- 用例累计耗时中位数：**18.865 秒**；范围 18.865–18.865 秒。
- 各轮结果：{'passed': 127}。
- 最慢用例（跨轮中位数）：`box2d.render.fallingStackPreview` 2.803 秒；`climbing.selection.failedPrepareLeavesInputAndAuthorityUnchanged` 0.377 秒；`climbing.performance.reportsOrdinaryCandidateDenseAndActiveP50P95` 0.308 秒。

### 27. [gameplay_composition_graphics_editor_tests.cpp](../../../test/integration/gameplay-composition/gameplay_composition_graphics_editor_tests.cpp)

- 路径：`test/integration/gameplay-composition/gameplay_composition_graphics_editor_tests.cpp`；117 个用例。
- 用例累计耗时中位数：**18.600 秒**；范围 18.600–18.600 秒。
- 各轮结果：{'passed': 117}。
- 最慢用例（跨轮中位数）：`editor.scene.script_component_reset_restores_the_declared_default` 0.435 秒；`editor.audio.waveform_builds_bounded_envelopes_and_loop_diagnostics` 0.376 秒；`editor.animation.clip_editor_installs_workspace_and_reverses_mask` 0.374 秒。

### 28. [ocean_render.cpp](../../../test/integration/ocean-voyage/ocean_render.cpp)

- 路径：`test/integration/ocean-voyage/ocean_render.cpp`；3 个用例。
- 用例累计耗时中位数：**17.024 秒**；范围 17.024–17.024 秒。
- 各轮结果：{'passed': 3}。
- 最慢用例（跨轮中位数）：`fluids.ocean.residentRenderingMatchesCpuWithWakeAndFloatingOrigin` 6.445 秒；`fluids.ocean.scriptRenderBuildersOwnInputsAcrossGc` 5.793 秒；`fluids.ocean.rendererPublishesSharedSurfaceAndFoam` 4.786 秒。

### 29. [gameplay_composition_graphics_animation_inertializer_tests.cpp](../../../test/integration/gameplay-composition/gameplay_composition_graphics_animation_inertializer_tests.cpp)

- 路径：`test/integration/gameplay-composition/gameplay_composition_graphics_animation_inertializer_tests.cpp`；129 个用例。
- 用例累计耗时中位数：**16.354 秒**；范围 16.354–16.354 秒。
- 各轮结果：{'passed': 129}。
- 最慢用例（跨轮中位数）：`audio.modplug.midi.play` 1.134 秒；`audio.streamSource.pump` 0.583 秒；`audio.actionStateFadeOutUsesInjectedActionTimeAndReleasesSource` 0.343 秒。

### 30. [gameplay_composition_graphics_editor_material_target_tests.cpp](../../../test/integration/gameplay-composition/gameplay_composition_graphics_editor_material_target_tests.cpp)

- 路径：`test/integration/gameplay-composition/gameplay_composition_graphics_editor_material_target_tests.cpp`；102 个用例。
- 用例累计耗时中位数：**16.312 秒**；范围 16.312–16.312 秒。
- 各轮结果：{'passed': 102}。
- 最慢用例（跨轮中位数）：`editor.spritestack.failed_rebake_preserves_generation_and_stale_publish_fails_first` 0.560 秒；`editor.scene.material_component_publication_is_staged_live_and_reversible` 0.476 秒；`editor.material.vegetation_motion_snapshot_reaches_bound_runtime_material` 0.391 秒。

### 31. [gameplay_composition_graphics_rts_combat_tests.cpp](../../../test/integration/gameplay-composition/gameplay_composition_graphics_rts_combat_tests.cpp)

- 路径：`test/integration/gameplay-composition/gameplay_composition_graphics_rts_combat_tests.cpp`；62 个用例。
- 用例累计耗时中位数：**15.816 秒**；范围 15.816–15.816 秒。
- 各轮结果：{'passed': 62}。
- 最慢用例（跨轮中位数）：`rts.largeExactlyOverlappingCrowdUnlocksDeterministically` 6.418 秒；`rts.supplyNavigationUsesCanonicalThreatCostOverlay` 0.727 秒；`rts.combatGroupThreatSectorsPartitionTargetsAndFallBack` 0.373 秒。

### 32. [script_api_graphics_terrain_probe_tests.cpp](../../../test/integration/script-api/script_api_graphics_terrain_probe_tests.cpp)

- 路径：`test/integration/script-api/script_api_graphics_terrain_probe_tests.cpp`；88 个用例。
- 用例累计耗时中位数：**15.466 秒**；范围 15.466–15.466 秒。
- 各轮结果：{'passed': 88}。
- 最慢用例（跨轮中位数）：`procgen.session.scriptEffectCommands` 1.072 秒；`procgen.terrain.performance.real1kRoundTripAnd1k4k8kRandomAccess` 0.902 秒；`weapon.script.projectileProviderSeesEveryPellet` 0.718 秒。

### 33. [runtime_lifecycle_graphics_tests.cpp](../../../test/integration/runtime-lifecycle/runtime_lifecycle_graphics_tests.cpp)

- 路径：`test/integration/runtime-lifecycle/runtime_lifecycle_graphics_tests.cpp`；86 个用例。
- 用例累计耗时中位数：**13.904 秒**；范围 13.904–13.904 秒。
- 各轮结果：{'passed': 86}。
- 最慢用例（跨轮中位数）：`plugins.load.nativeLibraryAndInstantiateCppModule` 0.755 秒；`devtools.renderflow.formatReportTruncates` 0.479 秒；`scenePanel.treeSelectionAndNodeEditing` 0.465 秒。

### 34. [script_api_graphics_dap_tests.cpp](../../../test/integration/script-api/script_api_graphics_dap_tests.cpp)

- 路径：`test/integration/script-api/script_api_graphics_dap_tests.cpp`；68 个用例。
- 用例累计耗时中位数：**13.655 秒**；范围 13.655–13.655 秒。
- 各轮结果：{'passed': 68}。
- 最慢用例（跨轮中位数）：`devtools.dap.initializeLaunchContinueStep` 0.995 秒；`devtools.dap.frameScopesAndVariableTree` 0.539 秒；`devtools.dap.realScriptBreakpointAndCallStack` 0.483 秒。

### 35. [CrowdThread.cpp](../../../test/integration/crowd-runtime/CrowdThread.cpp)

- 路径：`test/integration/crowd-runtime/CrowdThread.cpp`；7 个用例。
- 用例累计耗时中位数：**13.477 秒**；范围 13.477–13.477 秒。
- 各轮结果：{'passed': 7}。
- 最慢用例（跨轮中位数）：`crowd.thread.predictiveAvoidanceMatchesSerialWithFourWorkers` 7.768 秒；`crowd.thread.partialVelocityFailureRollsBackClockAndMotion` 3.610 秒；`crowd.thread.serialParallelContactAndReportsAreBitExact` 0.885 秒。

### 36. [gameplay_composition_graphics_buildingfx_tests.cpp](../../../test/integration/gameplay-composition/gameplay_composition_graphics_buildingfx_tests.cpp)

- 路径：`test/integration/gameplay-composition/gameplay_composition_graphics_buildingfx_tests.cpp`；94 个用例。
- 用例累计耗时中位数：**13.082 秒**；范围 13.082–13.082 秒。
- 各轮结果：{'passed': 94}。
- 最慢用例（跨轮中位数）：`climbing.motionMatchingDemoScriptCompilesAndDocumentsUeOnlyBoundary` 0.679 秒；`dialogueProc.playLineAndMeta` 0.374 秒；`climbing.animation.bindingValidatesClipIdentityAndResolvesRootBone` 0.293 秒。

### 37. [physics_simulation_graphics_physics_rigid_backend_tests.cpp](../../../test/integration/physics-simulation/physics_simulation_graphics_physics_rigid_backend_tests.cpp)

- 路径：`test/integration/physics-simulation/physics_simulation_graphics_physics_rigid_backend_tests.cpp`；94 个用例。
- 用例累计耗时中位数：**12.179 秒**；范围 12.179–12.179 秒。
- 各轮结果：{'passed': 94}。
- 最慢用例（跨轮中位数）：`physics.mechanism.crankSliderConvertsRotationToStroke` 0.403 秒；`physics.contacts.rejectsForeignStaleAndInvalidInputs` 0.284 秒；`physics.joints.rotatedFramesSurviveSnapshotRestore` 0.283 秒。

### 38. [gameplay_composition_graphics_procgen_road_editing_tests.cpp](../../../test/integration/gameplay-composition/gameplay_composition_graphics_procgen_road_editing_tests.cpp)

- 路径：`test/integration/gameplay-composition/gameplay_composition_graphics_procgen_road_editing_tests.cpp`；45 个用例。
- 用例累计耗时中位数：**9.607 秒**；范围 9.607–9.607 秒。
- 各轮结果：{'passed': 45}。
- 最慢用例（跨轮中位数）：`procgen.simulation.crystalCave` 1.776 秒；`resource.gameplay.rtsProductionCancellationRefundsAllResourcesAtomically` 0.373 秒；`rpg.craftingRejectsInsufficientIngredientsWithoutQueueMutation` 0.371 秒。

### 39. [gameplay_composition_graphics_stylize_attack_vfx_layers_tests.cpp](../../../test/integration/gameplay-composition/gameplay_composition_graphics_stylize_attack_vfx_layers_tests.cpp)

- 路径：`test/integration/gameplay-composition/gameplay_composition_graphics_stylize_attack_vfx_layers_tests.cpp`；41 个用例。
- 用例累计耗时中位数：**9.079 秒**；范围 9.079–9.079 秒。
- 各轮结果：{'passed': 41}。
- 最慢用例（跨轮中位数）：`stylize.effects.gpu.postPasses` 1.061 秒；`stylize.effects.gpu.meshShaders` 0.984 秒；`stylize.ysa.realGpuPipeline` 0.851 秒。

### 40. [asset_pipeline_graphics_file_admission_tests.cpp](../../../test/integration/asset-pipeline/asset_pipeline_graphics_file_admission_tests.cpp)

- 路径：`test/integration/asset-pipeline/asset_pipeline_graphics_file_admission_tests.cpp`；41 个用例。
- 用例累计耗时中位数：**8.398 秒**；范围 8.398–8.398 秒。
- 各轮结果：{'passed': 41}。
- 最慢用例（跨轮中位数）：`asset.import.materialV14ColorParametersSurviveV15CookWithImageDependency` 0.582 秒；`asset.mesh.unityV11AuthoringChannelsSurviveCookAndRuntimeLoad` 0.472 秒；`asset.migration.materialV12RejectsUnversionedEmissionAtomically` 0.376 秒。

### 41. [rts_unit_lifecycle_tactics_rts_tests.cpp](../../../test/integration/rts-unit-lifecycle/rts_unit_lifecycle_tactics_rts_tests.cpp)

- 路径：`test/integration/rts-unit-lifecycle/rts_unit_lifecycle_tactics_rts_tests.cpp`；64 个用例。
- 用例累计耗时中位数：**8.387 秒**；范围 8.387–8.387 秒。
- 各轮结果：{'passed': 64}。
- 最慢用例（跨轮中位数）：`rts.groups.pacingWorksWithAndWithoutCrowd` 0.718 秒；`rts.groups.mixedSpeedSquadClearsSingleCellPassage` 0.671 秒；`rts.groups.opposingSquadsReserveWholeCorridor` 0.558 秒。

### 42. [frame_recording_tests.cpp](../../../test/integration/frame-recording/frame_recording_tests.cpp)

- 路径：`test/integration/frame-recording/frame_recording_tests.cpp`；2 个用例。
- 用例累计耗时中位数：**7.400 秒**；范围 7.400–7.400 秒。
- 各轮结果：{'passed': 2}。
- 最慢用例（跨轮中位数）：`graphics.frameRecording.fxaaSlotReuseResizeAndPersistentCanvas` 7.295 秒；`graphics.frameRecording.inlineBudgetAndPeriodicReprobe` 0.105 秒。

### 43. [vsync_config_tests.cpp](../../../test/integration/vsync-config/vsync_config_tests.cpp)

- 路径：`test/integration/vsync-config/vsync_config_tests.cpp`；5 个用例。
- 用例累计耗时中位数：**5.143 秒**；范围 5.143–5.143 秒。
- 各轮结果：{'passed': 5}。
- 最慢用例（跨轮中位数）：`scriptConfig.vsyncDefaultsEnabled` 1.202 秒；`scriptConfig.vsyncFalseUsesUnsynchronizedPresent` 1.153 秒；`scriptConfig.vsyncAppliedBeforeEditorWindow` 1.125 秒。

### 44. [rts_crowd_flow_navigation.cpp](../../../test/integration/rts-crowd/rts_crowd_flow_navigation.cpp)

- 路径：`test/integration/rts-crowd/rts_crowd_flow_navigation.cpp`；23 个用例。
- 用例累计耗时中位数：**3.352 秒**；范围 3.352–3.352 秒。
- 各轮结果：{'passed': 23}。
- 最慢用例（跨轮中位数）：`rts.crowd.sharedFlowMissingProjectionRejectsWithoutConsumingTick` 0.378 秒；`rts.crowd.groupDisconnectedAssignedSlotStopsAfterNearestSlotInsteadOfCompleting` 0.310 秒；`rts.crowd.sharedFlowQueuesMoreThan64GoalsWithoutRejectingTick` 0.299 秒。

### 45. [rts_formation.cpp](../../../test/integration/rts-crowd/rts_formation.cpp)

- 路径：`test/integration/rts-crowd/rts_formation.cpp`；13 个用例。
- 用例累计耗时中位数：**3.128 秒**；范围 3.128–3.128 秒。
- 各轮结果：{'passed': 13}。
- 最慢用例（跨轮中位数）：`rts.formation.largeTranslatedGridDoesNotReverseMarchingRows` 1.157 秒；`rts.attackSlots.waitingMembersNeverProduceRealWeaponFire` 0.620 秒；`rts.groups.attackMoveArrivalRetiresMovingGroupAndKeepsCombatStance` 0.210 秒。

### 46. [CrowdMotion.cpp](../../../test/integration/rts-crowd/CrowdMotion.cpp)

- 路径：`test/integration/rts-crowd/CrowdMotion.cpp`；6 个用例。
- 用例累计耗时中位数：**2.886 秒**；范围 2.886–2.886 秒。
- 各轮结果：{'passed': 6}。
- 最慢用例（跨轮中位数）：`rts.crowd.parallelConsumerMatchesSerialMotionAndArrival` 1.314 秒；`rts.crowd.partialParallelGatherRollsBackBothConsumerPoses` 0.567 秒；`rts.crowd.parallelConsumerRejectsExpiredProviderWithoutPublishingMotion` 0.561 秒。

### 47. [procgen_road_profile_editing.cpp](../../../test/integration/gameplay-composition/procgen_road_profile_editing.cpp)

- 路径：`test/integration/gameplay-composition/procgen_road_profile_editing.cpp`；26 个用例。
- 用例累计耗时中位数：**2.880 秒**；范围 2.880–2.880 秒。
- 各轮结果：{'passed': 26}。
- 最慢用例（跨轮中位数）：`procgen.road.editing.pointProfilesRoundTripMigrateAndRejectAtomically` 0.386 秒；`procgen.road.editing.splitEdgeUndoRestoresOriginalIdentityAndLinks` 0.240 秒；`procgen.road.editing.junctionRadiusUsesExistingAuthorityUndo` 0.192 秒。

### 48. [ocean_gpu.cpp](../../../test/integration/ocean-voyage/ocean_gpu.cpp)

- 路径：`test/integration/ocean-voyage/ocean_gpu.cpp`；4 个用例。
- 用例累计耗时中位数：**2.775 秒**；范围 2.775–2.775 秒。
- 各轮结果：{'passed': 4}。
- 最慢用例（跨轮中位数）：`fluids.ocean.gpuSpectrumLargeBandParity` 0.989 秒；`fluids.ocean.gpuSpectrumMatchesCpuAndRejectsStaleFrames` 0.941 秒；`fluids.ocean.gpuSeaTransitionMatchesCpuFieldsAndLevel` 0.780 秒。

### 49. [save_restore_graphics_tests.cpp](../../../test/integration/save-restore/save_restore_graphics_tests.cpp)

- 路径：`test/integration/save-restore/save_restore_graphics_tests.cpp`；12 个用例。
- 用例累计耗时中位数：**2.760 秒**；范围 2.760–2.760 秒。
- 各轮结果：{'passed': 12}。
- 最慢用例（跨轮中位数）：`graphics.canvas.separateSubmissionsPreserveAndClearRGBA8` 0.390 秒；`gameSave.failedLaterPrepareLeavesEarlierParticipantUnchanged` 0.387 秒；`dialoguePersistence.rejectsLegacyUnversionedState` 0.382 秒。

### 50. [audio_playback_graphics_tests.cpp](../../../test/integration/audio-playback/audio_playback_graphics_tests.cpp)

- 路径：`test/integration/audio-playback/audio_playback_graphics_tests.cpp`；12 个用例。
- 用例累计耗时中位数：**2.515 秒**；范围 2.515–2.515 秒。
- 各轮结果：{'passed': 12}。
- 最慢用例（跨轮中位数）：`demo.newPlanetTexture` 0.843 秒；`sound.newSoundDataEmpty` 0.238 秒；`sound.decoder.clone` 0.201 秒。

### 51. [physics_vehicle_tires3d.cpp](../../../test/integration/physics-simulation/physics_vehicle_tires3d.cpp)

- 路径：`test/integration/physics-simulation/physics_vehicle_tires3d.cpp`；10 个用例。
- 用例累计耗时中位数：**2.413 秒**；范围 2.413–2.413 秒。
- 各轮结果：{'passed': 10}。
- 最慢用例（跨轮中位数）：`physics.vehicleTires3D.brakesHoldOnSlopeAndStopForwardMotion` 1.093 秒；`physics.vehicleTires3D.suspensionMobilityDrivesAndSteersByForces` 0.648 秒；`physics.vehicleTires3D.meshTriangleFrictionOverridesBaseMaterial` 0.163 秒。

### 52. [rts_crowd_registry.cpp](../../../test/integration/rts-crowd/rts_crowd_registry.cpp)

- 路径：`test/integration/rts-crowd/rts_crowd_registry.cpp`；12 个用例。
- 用例累计耗时中位数：**2.122 秒**；范围 2.122–2.122 秒。
- 各轮结果：{'passed': 12}。
- 最慢用例（跨轮中位数）：`rts.crowd.mapAdapterPreservesAdjacencyAndDestinationEntryCosts` 0.391 秒；`rts.crowd.rebindingAndUnlinkInvalidateOldProjection` 0.366 秒；`rts.crowd.surfaceTrackingRequiresProvidersAndRetainsSettingsAfterRejectedConfiguration` 0.282 秒。

### 53. [render_pipeline_graphics_frame_capture_tests.cpp](../../../test/integration/render-pipeline/render_pipeline_graphics_frame_capture_tests.cpp)

- 路径：`test/integration/render-pipeline/render_pipeline_graphics_frame_capture_tests.cpp`；1 个用例。
- 用例累计耗时中位数：**2.079 秒**；范围 2.079–2.079 秒。
- 各轮结果：{'passed': 1}。
- 最慢用例（跨轮中位数）：`graphics.frameCapture.sameFrameNativeAttachmentsAndFrozenSnapshots` 2.079 秒。

### 54. [fluids_shore_backend_tests.cpp](../../../test/integration/render-presentation/fluids_shore_backend_tests.cpp)

- 路径：`test/integration/render-presentation/fluids_shore_backend_tests.cpp`；2 个用例。
- 用例累计耗时中位数：**1.897 秒**；范围 1.897–1.897 秒。
- 各轮结果：{'passed': 2}。
- 最慢用例（跨轮中位数）：`fluids.shore.gpuMatchesCpuOnSlopingWetDryBed` 1.204 秒；`fluids.shore.scriptSurfaceSyncChecksInputsAndPublishesMesh` 0.693 秒。

### 55. [procgen_road_spacing_portals.cpp](../../../test/integration/gameplay-composition/procgen_road_spacing_portals.cpp)

- 路径：`test/integration/gameplay-composition/procgen_road_spacing_portals.cpp`；7 个用例。
- 用例累计耗时中位数：**1.887 秒**；范围 1.887–1.887 秒。
- 各轮结果：{'passed': 7}。
- 最慢用例（跨轮中位数）：`procgen.road.authoring.rigidDistributionMatchesMeasuredReferenceAndBudget` 0.782 秒；`procgen.road.authoring.tunnelPortalHolesPreserveRoofAcrossSplitSaveUndoAndNativeWriteback` 0.325 秒；`procgen.road.authoring.smoothingGroupsCrossUvSeamsButPreserveHardGroups` 0.205 秒。

### 56. [physics_snapshot3d_ownership.cpp](../../../test/integration/physics-simulation/physics_snapshot3d_ownership.cpp)

- 路径：`test/integration/physics-simulation/physics_snapshot3d_ownership.cpp`；13 个用例。
- 用例累计耗时中位数：**1.887 秒**；范围 1.887–1.887 秒。
- 各轮结果：{'passed': 13}。
- 最慢用例（跨轮中位数）：`physics.snapshot3d.motorTrajectorySurvivesRestore` 0.262 秒；`physics.snapshot3d.jointParametersRoundTripAllKinds` 0.246 秒；`physics.snapshot3d.transmissionPhaseTopologyAndTrajectory` 0.241 秒。

### 57. [ocean_boat.cpp](../../../test/integration/ocean-voyage/ocean_boat.cpp)

- 路径：`test/integration/ocean-voyage/ocean_boat.cpp`；6 个用例。
- 用例累计耗时中位数：**1.802 秒**；范围 1.802–1.802 秒。
- 各轮结果：{'passed': 6}。
- 最慢用例（跨轮中位数）：`fluids.ocean.boatWakeChangesSecondRigidBody` 0.905 秒；`fluids.ocean.boatRespondsToSpectralWaves` 0.311 秒；`fluids.ocean.boatFloatsAndPropelsRealRigidBody` 0.233 秒。

### 58. [ui_workflows_graphics_tests.cpp](../../../test/integration/ui-workflows/ui_workflows_graphics_tests.cpp)

- 路径：`test/integration/ui-workflows/ui_workflows_graphics_tests.cpp`；14 个用例。
- 用例累计耗时中位数：**1.756 秒**；范围 1.756–1.756 秒。
- 各轮结果：{'passed': 14}。
- 最慢用例（跨轮中位数）：`UI.p1.scrollListHeadlessRenderLarge` 0.375 秒；`UI.b.themeAppliesCompleteModernStyle` 0.194 秒；`UI.componentScheduling.failedBatchRetainsRemainingWork` 0.110 秒。

### 59. [physics_joint2d_snapshot.cpp](../../../test/integration/physics-simulation/physics_joint2d_snapshot.cpp)

- 路径：`test/integration/physics-simulation/physics_joint2d_snapshot.cpp`；9 个用例。
- 用例累计耗时中位数：**1.576 秒**；范围 1.576–1.576 秒。
- 各轮结果：{'passed': 9}。
- 最慢用例（跨轮中位数）：`physics.joint2d.snapshotRejectsInvalidTopologyAtomically` 0.370 秒；`physics.joint2d.snapshotGearPreservesOriginalPhaseAndDependencyTeardown` 0.329 秒；`physics.joint2d.snapshotScriptReferencesObserveRestoreAndWorldLifetime` 0.226 秒。

### 60. [procgen_road_presets.cpp](../../../test/integration/gameplay-composition/procgen_road_presets.cpp)

- 路径：`test/integration/gameplay-composition/procgen_road_presets.cpp`；7 个用例。
- 用例累计耗时中位数：**1.512 秒**；范围 1.512–1.512 秒。
- 各轮结果：{'passed': 7}。
- 最慢用例（跨轮中位数）：`procgen.road.authoring.presetRestoreRejectsLateHistoryFailureAndMigratesAbsentOverrides` 0.377 秒；`procgen.road.authoring.presetCombinedProfilesValidateFinalCustomPortsAndPreserveTraffic` 0.275 秒；`procgen.road.authoring.presetExplicitFalseAndEmptyArraysRoundTripAndUnbindingRestoresSource` 0.267 秒。

### 61. [performance_graphics_tests.cpp](../../../test/integration/performance/performance_graphics_tests.cpp)

- 路径：`test/integration/performance/performance_graphics_tests.cpp`；4 个用例。
- 用例累计耗时中位数：**1.426 秒**；范围 1.426–1.426 秒。
- 各轮结果：{'passed': 4}。
- 未设置的可选开关：`EVENGINE_MESH_VFX_BENCHMARK`, `EVENGINE_SETTLEMENT_BENCHMARK`。
- 最慢用例（跨轮中位数）：`stylize.meshVfx.sceneColorRefractionSamplesBoundTexture` 0.794 秒；`emergence.perf.tenThousandRulesSingleKeyWakeIsSublinear` 0.448 秒；`settlement.benchmark.traceAndIndependentBatchReportP50P95` 0.096 秒。

### 62. [procgen_road_side_terrain.cpp](../../../test/integration/gameplay-composition/procgen_road_side_terrain.cpp)

- 路径：`test/integration/gameplay-composition/procgen_road_side_terrain.cpp`；6 个用例。
- 用例累计耗时中位数：**1.426 秒**；范围 1.426–1.426 秒。
- 各轮结果：{'passed': 6}。
- 最慢用例（跨轮中位数）：`procgen.road.authoring.sideTerrainBoundsModesConsumeProviderAndStandardRecipe` 0.403 秒；`procgen.road.authoring.sideTerrainAbsenceMissFailureAndSharedBudgetPreserveAuthor` 0.353 秒；`procgen.road.authoring.sideAlignmentVerticalFramesAndSharedProjectionBudget` 0.292 秒。

### 63. [hud_tests.cpp](../../../test/integration/ui-hud/hud_tests.cpp)

- 路径：`test/integration/ui-hud/hud_tests.cpp`；13 个用例。
- 用例累计耗时中位数：**1.354 秒**；范围 1.354–1.354 秒。
- 各轮结果：{'passed': 13}。
- 最慢用例（跨轮中位数）：`UI.hud.nodeHandlesSurvivePropsButRejectReplacementAndDestroyedHosts` 0.260 秒；`editor.ui.hud_fill_snapshot_roundtrip_and_runtime_publication_are_atomic` 0.226 秒；`UI.hud.valueTweenRetargetDelayClampAndRebuildCancellation` 0.195 秒。

### 64. [physics_voronoi_fracture.cpp](../../../test/integration/house-collapse/physics_voronoi_fracture.cpp)

- 路径：`test/integration/house-collapse/physics_voronoi_fracture.cpp`；11 个用例。
- 用例累计耗时中位数：**1.336 秒**；范围 1.336–1.336 秒。
- 各轮结果：{'passed': 11}。
- 最慢用例（跨轮中位数）：`physics_voronoi.houseSeedsRemainClosed` 0.239 秒；`physics_voronoi.invalidInputAndMeshBudgets` 0.213 秒；`physics_voronoi.closedVolumePartitionAndDeterminism` 0.188 秒。

### 65. [procgen_road_extensions.cpp](../../../test/integration/gameplay-composition/procgen_road_extensions.cpp)

- 路径：`test/integration/gameplay-composition/procgen_road_extensions.cpp`；7 个用例。
- 用例累计耗时中位数：**1.137 秒**；范围 1.137–1.137 秒。
- 各轮结果：{'passed': 7}。
- 最慢用例（跨轮中位数）：`procgen.road.authoring.multiArmFacilityRoutesSaveUndoAndMigrateSplitIdentities` 0.207 秒；`procgen.road.authoring.terrainChannelsRollbackAllTilesAndRetainRedoOnLateFailure` 0.205 秒；`procgen.road.authoring.nativeTerrainWorldWritesEveryRoadChannelInOneHistoryFrame` 0.202 秒。

### 66. [ocean_spray_render.cpp](../../../test/integration/ocean-voyage/ocean_spray_render.cpp)

- 路径：`test/integration/ocean-voyage/ocean_spray_render.cpp`；3 个用例。
- 用例累计耗时中位数：**1.076 秒**；范围 1.076–1.076 秒。
- 各轮结果：{'passed': 3}。
- 最慢用例（跨轮中位数）：`fluids.ocean.sprayBatchRendersAndRejectsInvalidPublication` 0.693 秒；`fluids.ocean.sprayRendererObservesProviderRetirement` 0.318 秒；`fluids.ocean.sprayScriptBuildersSurviveGc` 0.066 秒。

### 67. [physics_force_application.cpp](../../../test/integration/physics-simulation/physics_force_application.cpp)

- 路径：`test/integration/physics-simulation/physics_force_application.cpp`；9 个用例。
- 用例累计耗时中位数：**1.042 秒**；范围 1.042–1.042 秒。
- 各轮结果：{'passed': 9}。
- 最慢用例（跨轮中位数）：`physics.forces.applicationRejectsStaticWithoutMutation` 0.284 秒；`physics.forces.batch2DAtomicityAndCoupleAreMeterIndependent` 0.259 秒；`physics.forces.batchMoveAndForeignWorldCannotDuplicateSubmission` 0.081 秒。

### 68. [render_presentation_impasto_tests.cpp](../../../test/integration/render-presentation/render_presentation_impasto_tests.cpp)

- 路径：`test/integration/render-presentation/render_presentation_impasto_tests.cpp`；2 个用例。
- 用例累计耗时中位数：**1.042 秒**；范围 1.042–1.042 秒。
- 各轮结果：{'passed': 2}。
- 最慢用例（跨轮中位数）：`stylize.impasto.realGpuPipeline` 0.949 秒；`stylize.impasto.independentMaterialContract` 0.093 秒。

### 69. [PcmStream.cpp](../../../test/integration/audio-playback/PcmStream.cpp)

- 路径：`test/integration/audio-playback/PcmStream.cpp`；4 个用例。
- 用例累计耗时中位数：**0.979 秒**；范围 0.979–0.979 秒。
- 各轮结果：{'passed': 4}。
- 最慢用例（跨轮中位数）：`audio.pcm.bothDestructionOrders` 0.295 秒；`audio.pcm.scriptResultAndOwnership` 0.285 秒；`audio.pcm.starvationPauseResumeAndDrain` 0.276 秒。

### 70. [procgen_road_reference.cpp](../../../test/integration/gameplay-composition/procgen_road_reference.cpp)

- 路径：`test/integration/gameplay-composition/procgen_road_reference.cpp`；6 个用例。
- 用例累计耗时中位数：**0.974 秒**；范围 0.974–0.974 秒。
- 各轮结果：{'passed': 6}。
- 最慢用例（跨轮中位数）：`procgen.road.network.coincidentEndpointPositionsDoNotMergeNodeIdentity` 0.370 秒；`procgen.road.authoring.referenceCubicWidthIsExplicitAndLegacySnapshotsRetainLinear` 0.292 秒；`procgen.road.terrain.elevatedOriginMapsNormalizedHeightsAndAllChannels` 0.097 秒。

### 71. [procgen_road_terrain_author_policy.cpp](../../../test/integration/gameplay-composition/procgen_road_terrain_author_policy.cpp)

- 路径：`test/integration/gameplay-composition/procgen_road_terrain_author_policy.cpp`；5 个用例。
- 用例累计耗时中位数：**0.953 秒**；范围 0.953–0.953 秒。
- 各轮结果：{'passed': 5}。
- 最慢用例（跨轮中位数）：`procgen.road.terrain.authorPolicyMatchesReferenceDisjointSelectionFixture` 0.282 秒；`procgen.road.terrain.authorPolicyNetworkSaveConsumesOneAtomicPlan` 0.262 秒；`procgen.road.terrain.authorPolicyDisabledPlanPreservesHistory` 0.200 秒。

### 72. [physics_joint2d.cpp](../../../test/integration/physics-simulation/physics_joint2d.cpp)

- 路径：`test/integration/physics-simulation/physics_joint2d.cpp`；9 个用例。
- 用例累计耗时中位数：**0.949 秒**；范围 0.949–0.949 秒。
- 各轮结果：{'passed': 9}。
- 最慢用例（跨轮中位数）：`physics.joint2d.revoluteMotorLimitsAndBodyTeardown` 0.183 秒；`physics.joint2d.bodyDestructionInvalidatesDependentGear` 0.178 秒；`physics.joint2d.prismaticDistanceWheelAndWeld` 0.165 秒。

### 73. [material_studio_scripts.cpp](../../../test/integration/material-studio/material_studio_scripts.cpp)

- 路径：`test/integration/material-studio/material_studio_scripts.cpp`；4 个用例。
- 用例累计耗时中位数：**0.936 秒**；范围 0.936–0.936 秒。
- 各轮结果：{'passed': 4}。
- 最慢用例（跨轮中位数）：`editor.material.facade_script_projects_owned_values_and_checked_failures` 0.305 秒；`editor.material.preview_geometry_has_recessed_rings_valid_normals_and_matching_sky_edges` 0.302 秒；`editor.material.studio_publication_clears_resources_and_preserves_preview_on_failure` 0.224 秒。

### 74. [climbing_pose_contacts.cpp](../../../test/integration/gameplay-composition/climbing_pose_contacts.cpp)

- 路径：`test/integration/gameplay-composition/climbing_pose_contacts.cpp`；7 个用例。
- 用例累计耗时中位数：**0.931 秒**；范围 0.931–0.931 秒。
- 各轮结果：{'passed': 7}。
- 最慢用例（跨轮中位数）：`climbing.poseContacts.nonHandContactsDoNotPinReleasedHands` 0.319 秒；`climbing.poseContacts.feetRequireTargetsAndRemainIndependent` 0.180 秒；`climbing.poseContacts.invalidFootRejectsBeforeHandMutation` 0.170 秒。

### 75. [physics_destruction_graphics_depth.cpp](../../../test/integration/house-collapse/physics_destruction_graphics_depth.cpp)

- 路径：`test/integration/house-collapse/physics_destruction_graphics_depth.cpp`；1 个用例。
- 用例累计耗时中位数：**0.848 秒**；范围 0.848–0.848 秒。
- 各轮结果：{'passed': 1}。
- 最慢用例（跨轮中位数）：`physics_destruction_graphics.depthTracksCurrentSubmissionAndExpiresWithRenderer` 0.848 秒。

### 76. [physics_coupling3d.cpp](../../../test/integration/physics-simulation/physics_coupling3d.cpp)

- 路径：`test/integration/physics-simulation/physics_coupling3d.cpp`；7 个用例。
- 用例累计耗时中位数：**0.812 秒**；范围 0.812–0.812 秒。
- 各轮结果：{'passed': 7}。
- 最慢用例（跨轮中位数）：`physics.coupling3d.gearTracksMultipleTurnsAndSourceDeath` 0.238 秒；`physics.coupling3d.mixedRackAndPinion` 0.189 秒；`physics.coupling3d.pulleyConservesWeightedLength` 0.102 秒。

### 77. [editor_control_rig.cpp](../../../test/integration/control-rig/editor_control_rig.cpp)

- 路径：`test/integration/control-rig/editor_control_rig.cpp`；7 个用例。
- 用例累计耗时中位数：**0.774 秒**；范围 0.774–0.774 秒。
- 各轮结果：{'passed': 7}。
- 最慢用例（跨轮中位数）：`editor.controlRig.documentRejectsForeignPublication` 0.252 秒；`editor.controlRig.scriptFacadeProjectsNumericTimeAndOwnedResults` 0.102 秒；`editor.controlRig.failedHistoryPreviewRestoresDocument` 0.098 秒。

### 78. [weather_lightning_runtime.cpp](../../../test/integration/weather-lightning/weather_lightning_runtime.cpp)

- 路径：`test/integration/weather-lightning/weather_lightning_runtime.cpp`；1 个用例。
- 用例累计耗时中位数：**0.766 秒**；范围 0.766–0.766 秒。
- 各轮结果：{'passed': 1}。
- 最慢用例（跨轮中位数）：`weather.lightning weapon endpoints pulse and stale entity` 0.766 秒。

### 79. [climbing_foot_contacts.cpp](../../../test/integration/physics-simulation/climbing_foot_contacts.cpp)

- 路径：`test/integration/physics-simulation/climbing_foot_contacts.cpp`；8 个用例。
- 用例累计耗时中位数：**0.757 秒**；范围 0.757–0.757 秒。
- 各轮结果：{'passed': 8}。
- 最慢用例（跨轮中位数）：`climbing.footContacts.ladderSocketsFollowMovingBody` 0.245 秒；`climbing.footContacts.narrowWallSupportsOnlyOneFoot` 0.079 秒；`climbing.corner.outerRightAngleKeepsCapsuleOutsideWall` 0.078 秒。

### 80. [physics_collision_regression.cpp](../../../test/integration/physics-simulation/physics_collision_regression.cpp)

- 路径：`test/integration/physics-simulation/physics_collision_regression.cpp`；4 个用例。
- 用例累计耗时中位数：**0.748 秒**；范围 0.748–0.748 秒。
- 各轮结果：{'passed': 4}。
- 最慢用例（跨轮中位数）：`physics.collision.stack.remainsSupportedAfterTenSeconds` 0.360 秒；`physics.collision.ccd.dynamicPairMaintainsOrderWithFourSubsteps` 0.230 秒；`physics.collision.ccd.sphereStopsAtThinWall` 0.080 秒。

### 81. [script_rendering_graphics_tests.cpp](../../../test/integration/script-rendering/script_rendering_graphics_tests.cpp)

- 路径：`test/integration/script-rendering/script_rendering_graphics_tests.cpp`；3 个用例。
- 用例累计耗时中位数：**0.692 秒**；范围 0.692–0.692 秒。
- 各轮结果：{'passed': 3}。
- 最慢用例（跨轮中位数）：`WindowTest.settingsFields` 0.330 秒；`WindowTest.basicWindow` 0.249 秒；`GraphicTest.bindings` 0.113 秒。

### 82. [procgen_road_noop.cpp](../../../test/integration/gameplay-composition/procgen_road_noop.cpp)

- 路径：`test/integration/gameplay-composition/procgen_road_noop.cpp`；5 个用例。
- 用例累计耗时中位数：**0.665 秒**；范围 0.665–0.665 秒。
- 各轮结果：{'passed': 5}。
- 最慢用例（跨轮中位数）：`procgen.road.editing.connectAllTurnsIsAtomicAndUndoable` 0.249 秒；`procgen.road.editingNoOp.commandAdaptersOmitNoChangeOperations` 0.197 秒；`procgen.road.editingNoOp.buildersReturnSuccessAndPreserveAuthor` 0.078 秒。

### 83. [procgen_road_side_placement.cpp](../../../test/integration/gameplay-composition/procgen_road_side_placement.cpp)

- 路径：`test/integration/gameplay-composition/procgen_road_side_placement.cpp`；2 个用例。
- 用例累计耗时中位数：**0.616 秒**；范围 0.616–0.616 秒。
- 各轮结果：{'passed': 2}。
- 最慢用例（跨轮中位数）：`procgen.road.authoring.sidePlacementPresetHistoryMigrationAndAtomicFailure` 0.374 秒；`procgen.road.authoring.sidePlacementRotatesTerrainFootprintBeforeWorldUpOffset` 0.242 秒。

### 84. [animation_atlas_rotation.cpp](../../../test/integration/sprite-atlas/animation_atlas_rotation.cpp)

- 路径：`test/integration/sprite-atlas/animation_atlas_rotation.cpp`；1 个用例。
- 用例累计耗时中位数：**0.598 秒**；范围 0.598–0.598 秒。
- 各轮结果：{'passed': 1}。
- 最慢用例（跨轮中位数）：`animation.sprite.rotatedAtlasPixelsAndLayout` 0.598 秒。

### 85. [procgen_road_authoring.cpp](../../../test/integration/gameplay-composition/procgen_road_authoring.cpp)

- 路径：`test/integration/gameplay-composition/procgen_road_authoring.cpp`；2 个用例。
- 用例累计耗时中位数：**0.592 秒**；范围 0.592–0.592 秒。
- 各轮结果：{'passed': 2}。
- 最慢用例（跨轮中位数）：`procgen.road.authoring.intervalsSplitAndSnapshotsUndoWithoutDuplication` 0.367 秒；`procgen.road.authoring.bridgeTunnelIntervalsKeepTerrainEvenNearItsSurface` 0.225 秒。

### 86. [ocean_script.cpp](../../../test/integration/ocean-voyage/ocean_script.cpp)

- 路径：`test/integration/ocean-voyage/ocean_script.cpp`；2 个用例。
- 用例累计耗时中位数：**0.576 秒**；范围 0.576–0.576 秒。
- 各轮结果：{'passed': 2}。
- 最慢用例（跨轮中位数）：`fluids.ocean.scriptOwnsSpectrumAndFrameBuilders` 0.341 秒；`fluids.ocean.scriptBoatWakeUsesOneFixedClock` 0.236 秒。

### 87. [physics_vehicle_tires.cpp](../../../test/integration/physics-simulation/physics_vehicle_tires.cpp)

- 路径：`test/integration/physics-simulation/physics_vehicle_tires.cpp`；6 个用例。
- 用例累计耗时中位数：**0.566 秒**；范围 0.566–0.566 秒。
- 各轮结果：{'passed': 6}。
- 最慢用例（跨轮中位数）：`physics.vehicleTires2D.pendingRetryCannotPublishOrDoubleApply` 0.211 秒；`physics.vehicleTires2D.offCenterCombinedSlipDissipatesAtMultipleScales` 0.087 秒；`physics.vehicleTires2D.invalidTailAndDestroyedBodyKeepCompletedState` 0.074 秒。

### 88. [material_preview_shadows.cpp](../../../test/integration/material-studio/material_preview_shadows.cpp)

- 路径：`test/integration/material-studio/material_preview_shadows.cpp`；1 个用例。
- 用例累计耗时中位数：**0.533 秒**；范围 0.533–0.533 秒。
- 各轮结果：{'passed': 1}。
- 最慢用例（跨轮中位数）：`editor.material.preview_shadows_affect_pixels_and_capture_path_stays_unshadowed` 0.533 秒。

### 89. [procgen_road_connections.cpp](../../../test/integration/gameplay-composition/procgen_road_connections.cpp)

- 路径：`test/integration/gameplay-composition/procgen_road_connections.cpp`；2 个用例。
- 用例累计耗时中位数：**0.520 秒**；范围 0.520–0.520 秒。
- 各轮结果：{'passed': 2}。
- 最慢用例（跨轮中位数）：`procgen.road.authoring.customConnectionSnapshotUndoAndSplitRebindStableIds` 0.346 秒；`procgen.road.authoring.closedBeamConnectionValidatesTheEntireSeamLoop` 0.174 秒。

### 90. [editor_control_rig_automation.cpp](../../../test/integration/control-rig/editor_control_rig_automation.cpp)

- 路径：`test/integration/control-rig/editor_control_rig_automation.cpp`；6 个用例。
- 用例累计耗时中位数：**0.480 秒**；范围 0.480–0.480 秒。
- 各轮结果：{'passed': 6}。
- 最慢用例（跨轮中位数）：`editor.controlRig.automation.sharedDocumentHistoryAndSchema` 0.092 秒；`editor.controlRig.automation.keysNodesAndGeneration` 0.084 秒；`editor.controlRig.automation.oldOwnerCannotRemoveReplacement` 0.082 秒。

### 91. [physics_force_script.cpp](../../../test/integration/physics-simulation/physics_force_script.cpp)

- 路径：`test/integration/physics-simulation/physics_force_script.cpp`；3 个用例。
- 用例累计耗时中位数：**0.471 秒**；范围 0.471–0.471 秒。
- 各轮结果：{'passed': 3}。
- 最慢用例（跨轮中位数）：`physics.forces.scriptModelsProjectResults` 0.336 秒；`physics.forces.scriptFieldSetsOwnValuesAndBatchRights` 0.072 秒；`physics.forces.scriptWingAssembliesControlBothDimensions` 0.062 秒。

### 92. [physics_wing_assembly.cpp](../../../test/integration/physics-simulation/physics_wing_assembly.cpp)

- 路径：`test/integration/physics-simulation/physics_wing_assembly.cpp`；4 个用例。
- 用例累计耗时中位数：**0.458 秒**；范围 0.458–0.458 秒。
- 各轮结果：{'passed': 4}。
- 最慢用例（跨轮中位数）：`physics.forces.wingAssembly2DUsesRotatedPointVelocity` 0.171 秒；`physics.forces.wingAssemblyFailureIsAtomic` 0.162 秒；`physics.forces.wingReverseFlowAndStallContinuity` 0.063 秒。

### 93. [editor_authority_atomic.cpp](../../../test/integration/transaction-authority/editor_authority_atomic.cpp)

- 路径：`test/integration/transaction-authority/editor_authority_atomic.cpp`；5 个用例。
- 用例累计耗时中位数：**0.456 秒**；范围 0.456–0.456 秒。
- 各轮结果：{'passed': 5}。
- 最慢用例（跨轮中位数）：`editor.atomic.legacy_rollback_exception_is_reported_and_cleanup_continues` 0.181 秒；`editor.atomic.publish_failure_has_no_effect_and_can_retry` 0.084 秒；`editor.atomic.forged_plan_target_rejected_before_mutation` 0.069 秒。

### 94. [profile_capture_tests.cpp](../../../test/integration/profiler-capture/profile_capture_tests.cpp)

- 路径：`test/integration/profiler-capture/profile_capture_tests.cpp`；3 个用例。
- 用例累计耗时中位数：**0.452 秒**；范围 0.452–0.452 秒。
- 各轮结果：{'passed': 3}。
- 最慢用例（跨轮中位数）：`profiler.core.reportMeasuresCapturedSpan` 0.288 秒；`profiler.capture.automaticFrameBeginIncludesEarlier3DWork` 0.095 秒；`profiler.core.sameNameDifferentStorageAggregates` 0.069 秒。

### 95. [physics_collapse_dust.cpp](../../../test/integration/house-collapse/physics_collapse_dust.cpp)

- 路径：`test/integration/house-collapse/physics_collapse_dust.cpp`；3 个用例。
- 用例累计耗时中位数：**0.451 秒**；范围 0.451–0.451 秒。
- 各轮结果：{'passed': 3}。
- 最慢用例（跨轮中位数）：`physics_collapse_dust.ownedWorkerIsBoundedNonblockingAndOutlivesSources` 0.255 秒；`physics_collapse_dust.savedGasInjectsExistingFogWithBoundedFootprints` 0.113 秒；`physics_collapse_dust.savedGasOutlivesSolverAndRejectsPartialLoad` 0.083 秒。

### 96. [physics_buoyancy_geometry.cpp](../../../test/integration/physics-simulation/physics_buoyancy_geometry.cpp)

- 路径：`test/integration/physics-simulation/physics_buoyancy_geometry.cpp`；3 个用例。
- 用例累计耗时中位数：**0.388 秒**；范围 0.388–0.388 秒。
- 各轮结果：{'passed': 3}。
- 最慢用例（跨轮中位数）：`physics.forces.immersionHalfDryFullAndThickness` 0.240 秒；`physics.forces.immersionMatchesIndependentSliceIntegration` 0.078 秒；`physics.forces.geometricBuoyancyBalancesGravity2DAnd3D` 0.070 秒。

### 97. [climbing_contact_presentation.cpp](../../../test/integration/gameplay-composition/climbing_contact_presentation.cpp)

- 路径：`test/integration/gameplay-composition/climbing_contact_presentation.cpp`；1 个用例。
- 用例累计耗时中位数：**0.384 秒**；范围 0.384–0.384 秒。
- 各轮结果：{'passed': 1}。
- 最慢用例（跨轮中位数）：`climbing.contactPresentation.fixedStepBlendReleasesAndRejectsAtomically` 0.384 秒。

### 98. [performance_action_montage_tests.cpp](../../../test/integration/performance/performance_action_montage_tests.cpp)

- 路径：`test/integration/performance/performance_action_montage_tests.cpp`；3 个用例。
- 用例累计耗时中位数：**0.364 秒**；范围 0.364–0.364 秒。
- 各轮结果：{'passed': 3}。
- 未设置的可选开关：`EVENGINE_MONTAGE_BENCHMARK`。
- 最慢用例（跨轮中位数）：`actionMontage.rvaluePresentTransfersOwningTimelineProjectionStorage` 0.220 秒；`actionMontage.preparedTopologyHandlesDenseSectionSweepsAndJumps` 0.074 秒；`actionMontage.benchmarkPreparedPresentReportsP50P95AndMeasuredAllocationBoundary` 0.071 秒。

### 99. [procgen_road_parameter_sampling.cpp](../../../test/integration/gameplay-composition/procgen_road_parameter_sampling.cpp)

- 路径：`test/integration/gameplay-composition/procgen_road_parameter_sampling.cpp`；1 个用例。
- 用例累计耗时中位数：**0.346 秒**；范围 0.346–0.346 秒。
- 各轮结果：{'passed': 1}。
- 最慢用例（跨轮中位数）：`procgen.road.authoring.parameterScanPolicySaveUndoAndLegacyMigration` 0.346 秒。

### 100. [world_sweep.cpp](../../../test/integration/projectile-behavior/world_sweep.cpp)

- 路径：`test/integration/projectile-behavior/world_sweep.cpp`；3 个用例。
- 用例累计耗时中位数：**0.336 秒**；范围 0.336–0.336 秒。
- 各轮结果：{'passed': 3}。
- 最慢用例（跨轮中位数）：`carrierWorldSweep.bodyDestructionAndBudget` 0.184 秒；`carrierWorldSweep.box3dContractAndRestore` 0.085 秒；`carrierWorldSweep.carrierPierceSnapshotAndFailure` 0.067 秒。

### 101. [procgen_road_recipe_snapshot.cpp](../../../test/integration/gameplay-composition/procgen_road_recipe_snapshot.cpp)

- 路径：`test/integration/gameplay-composition/procgen_road_recipe_snapshot.cpp`；1 个用例。
- 用例累计耗时中位数：**0.327 秒**；范围 0.327–0.327 秒。
- 各轮结果：{'passed': 1}。
- 最慢用例（跨轮中位数）：`procgen.road.authoring.recipeSnapshotConsumesEditorAuthorDataAndLOD` 0.327 秒。

### 102. [physics_collapse_animation.cpp](../../../test/integration/house-collapse/physics_collapse_animation.cpp)

- 路径：`test/integration/house-collapse/physics_collapse_animation.cpp`；3 个用例。
- 用例累计耗时中位数：**0.321 秒**；范围 0.321–0.321 秒。
- 各轮结果：{'passed': 3}。
- 最慢用例（跨轮中位数）：`physics_collapse_animation.contentKeyIncludesEveryByte` 0.174 秒；`physics_collapse_animation.emptyAndStaleRecording` 0.076 秒；`physics_collapse_animation.savedPlaybackOutlivesWorld` 0.071 秒。

### 103. [physics_coupling3d_script.cpp](../../../test/integration/physics-simulation/physics_coupling3d_script.cpp)

- 路径：`test/integration/physics-simulation/physics_coupling3d_script.cpp`；2 个用例。
- 用例累计耗时中位数：**0.301 秒**；范围 0.301–0.301 秒。
- 各轮结果：{'passed': 2}。
- 最慢用例（跨轮中位数）：`physics.coupling3d.scriptWorldFirstInvalidation` 0.223 秒；`physics.coupling3d.scriptOwnershipAndTypeSafety` 0.078 秒。

### 104. [climbing_grounding.cpp](../../../test/integration/physics-simulation/climbing_grounding.cpp)

- 路径：`test/integration/physics-simulation/climbing_grounding.cpp`；3 个用例。
- 用例累计耗时中位数：**0.300 秒**；范围 0.300–0.300 秒。
- 各轮结果：{'passed': 3}。
- 最慢用例（跨轮中位数）：`climbing.grounding.tallStepRemainsBlocked` 0.159 秒；`climbing.grounding.lowCeilingRejectsStepWithoutPublishingRaisedPose` 0.080 秒；`climbing.grounding.lowStepUsesProductionMotor` 0.061 秒。

### 105. [physics_force_receipt.cpp](../../../test/integration/physics-simulation/physics_force_receipt.cpp)

- 路径：`test/integration/physics-simulation/physics_force_receipt.cpp`；3 个用例。
- 用例累计耗时中位数：**0.292 秒**；范围 0.292–0.292 秒。
- 各轮结果：{'passed': 3}。
- 最慢用例（跨轮中位数）：`physics.forces.receiptRequiresAutomaticForceClearing2D` 0.161 秒；`physics.forces.receiptRestoreAndDestroyCancelPendingOnly` 0.068 秒；`physics.forces.receiptRejectsWrongStepAndRetriesBackendOnce` 0.063 秒。

### 106. [procgen_road_position_split.cpp](../../../test/integration/gameplay-composition/procgen_road_position_split.cpp)

- 路径：`test/integration/gameplay-composition/procgen_road_position_split.cpp`；2 个用例。
- 用例累计耗时中位数：**0.271 秒**；范围 0.271–0.271 秒。
- 各轮结果：{'passed': 2}。
- 最慢用例（跨轮中位数）：`procgen.road.editing.positionSplitUndoRedoKeepsProjectedPointAndIds` 0.198 秒；`procgen.road.editing.connectNodeAtPositionSnapsToVisibleCurve` 0.073 秒。

### 107. [procgen_road_sampling_defaults.cpp](../../../test/integration/gameplay-composition/procgen_road_sampling_defaults.cpp)

- 路径：`test/integration/gameplay-composition/procgen_road_sampling_defaults.cpp`；2 个用例。
- 用例累计耗时中位数：**0.269 秒**；范围 0.269–0.269 秒。
- 各轮结果：{'passed': 2}。
- 最慢用例（跨轮中位数）：`procgen.road.authoring.presetV2ExplicitSamplingOverridesRetainPresence` 0.181 秒；`procgen.road.authoring.presetV2MigratesEveryHistoryRootWithoutChangingChildInheritance` 0.088 秒。

### 108. [procgen_native_proxy_reuse.cpp](../../../test/integration/script-api/procgen_native_proxy_reuse.cpp)

- 路径：`test/integration/script-api/procgen_native_proxy_reuse.cpp`；1 个用例。
- 用例累计耗时中位数：**0.267 秒**；范围 0.267–0.267 秒。
- 各轮结果：{'passed': 1}。
- 最慢用例（跨轮中位数）：`procgen.script.nativeProxyAddressReuseInvalidatesOldInstance` 0.267 秒。

### 109. [terrain_road_write_vm.cpp](../../../test/integration/script-api/terrain_road_write_vm.cpp)

- 路径：`test/integration/script-api/terrain_road_write_vm.cpp`；1 个用例。
- 用例累计耗时中位数：**0.252 秒**；范围 0.252–0.252 秒。
- 各轮结果：{'passed': 1}。
- 最慢用例（跨轮中位数）：`procgen.road.terrain.squirrelProductionMeshWriteAndUndo` 0.252 秒。

### 110. [scene_procgen_rotation.cpp](../../../test/integration/gameplay-composition/scene_procgen_rotation.cpp)

- 路径：`test/integration/gameplay-composition/scene_procgen_rotation.cpp`；3 个用例。
- 用例累计耗时中位数：**0.248 秒**；范围 0.248–0.248 秒。
- 各轮结果：{'passed': 3}。
- 最慢用例（跨轮中位数）：`Scene.procgenSink.preservesDegreeRotationAcrossSnapshotsAndDeltas` 0.096 秒；`Scene.procgenSink.coordinatesReplacementWithoutLeakingCandidateHosts` 0.084 秒；`Scene.procgenSink.deltaSnapshotBuilderPreservesIdentityAndRejectsMalformedOrder` 0.067 秒。

### 111. [procgen_road_junction_style.cpp](../../../test/integration/gameplay-composition/procgen_road_junction_style.cpp)

- 路径：`test/integration/gameplay-composition/procgen_road_junction_style.cpp`；1 个用例。
- 用例累计耗时中位数：**0.233 秒**；范围 0.233–0.233 秒。
- 各轮结果：{'passed': 1}。
- 最慢用例（跨轮中位数）：`procgen.road.junctionStyle.saveRestoreAndUndoReproduceGeometry` 0.233 秒。

### 112. [procgen_road_full_roll.cpp](../../../test/integration/gameplay-composition/procgen_road_full_roll.cpp)

- 路径：`test/integration/gameplay-composition/procgen_road_full_roll.cpp`；1 个用例。
- 用例累计耗时中位数：**0.225 秒**；范围 0.225–0.225 秒。
- 各轮结果：{'passed': 1}。
- 最慢用例（跨轮中位数）：`procgen.road.authoring.fullRollPreservesReferenceRotationAndAtomicHistory` 0.225 秒。

### 113. [terrain_road_batch_vm.cpp](../../../test/integration/script-api/terrain_road_batch_vm.cpp)

- 路径：`test/integration/script-api/terrain_road_batch_vm.cpp`；1 个用例。
- 用例累计耗时中位数：**0.214 秒**；范围 0.214–0.214 秒。
- 各轮结果：{'passed': 1}。
- 最慢用例（跨轮中位数）：`procgen.road.terrain.squirrelBatchProductionPoliciesAndAtomicFailure` 0.214 秒。

### 114. [physics_mover_grounding.cpp](../../../test/integration/physics-simulation/physics_mover_grounding.cpp)

- 路径：`test/integration/physics-simulation/physics_mover_grounding.cpp`；1 个用例。
- 用例累计耗时中位数：**0.211 秒**；范围 0.211–0.211 秒。
- 各轮结果：{'passed': 1}。
- 最慢用例（跨轮中位数）：`physics.mover.grounding.supportAndFilteringContract` 0.211 秒。

### 115. [procgen_road_roll_pivot.cpp](../../../test/integration/gameplay-composition/procgen_road_roll_pivot.cpp)

- 路径：`test/integration/gameplay-composition/procgen_road_roll_pivot.cpp`；1 个用例。
- 用例累计耗时中位数：**0.201 秒**；范围 0.201–0.201 秒。
- 各轮结果：{'passed': 1}。
- 最慢用例（跨轮中位数）：`procgen.road.authoring.rollPivotRecipeSnapshotReverseSplitAndUndo` 0.201 秒。

### 116. [gameplay_composition_action_upper_body_orientation_tests.cpp](../../../test/integration/gameplay-composition/gameplay_composition_action_upper_body_orientation_tests.cpp)

- 路径：`test/integration/gameplay-composition/gameplay_composition_action_upper_body_orientation_tests.cpp`；3 个用例。
- 用例累计耗时中位数：**0.198 秒**；范围 0.198–0.198 秒。
- 各轮结果：{'passed': 3}。
- 最慢用例（跨轮中位数）：`actionMontage.upperBodyCompensationObservesProviderMaskAndFullBodyPolicy` 0.071 秒；`actionMontage.upperBodyProfileReplacementIsTransactional` 0.067 秒；`actionMontage.upperBodyCompensationRunsBeforeAdditiveLayersDeterministically` 0.060 秒。

### 117. [offscreen_authoring_graphics_tests.cpp](../../../test/integration/offscreen-authoring/offscreen_authoring_graphics_tests.cpp)

- 路径：`test/integration/offscreen-authoring/offscreen_authoring_graphics_tests.cpp`；2 个用例。
- 用例累计耗时中位数：**0.198 秒**；范围 0.198–0.198 秒。
- 各轮结果：{'passed': 2}。
- 最慢用例（跨轮中位数）：`authoringPreview.stylizeMissingProviderPreservesDocument` 0.100 秒；`authoringPreview.heightmapMissingProviderPreservesSource` 0.098 秒。

### 118. [procgen_road_sampling.cpp](../../../test/integration/gameplay-composition/procgen_road_sampling.cpp)

- 路径：`test/integration/gameplay-composition/procgen_road_sampling.cpp`；1 个用例。
- 用例累计耗时中位数：**0.197 秒**；范围 0.197–0.197 秒。
- 各轮结果：{'passed': 1}。
- 最慢用例（跨轮中位数）：`procgen.road.authoring.samplingSettingsSaveUndoMigrateAndRejectBudgets` 0.197 秒。

### 119. [physics_layered_cloth_script.cpp](../../../test/integration/layered-cloth/physics_layered_cloth_script.cpp)

- 路径：`test/integration/layered-cloth/physics_layered_cloth_script.cpp`；1 个用例。
- 用例累计耗时中位数：**0.197 秒**；范围 0.197–0.197 秒。
- 各轮结果：{'passed': 1}。
- 最慢用例（跨轮中位数）：`PhysicsLayeredCloth.scriptBindings` 0.197 秒。

### 120. [file_io_devtools_frame_capture_tests.cpp](../../../test/integration/file-io/file_io_devtools_frame_capture_tests.cpp)

- 路径：`test/integration/file-io/file_io_devtools_frame_capture_tests.cpp`；1 个用例。
- 用例累计耗时中位数：**0.195 秒**；范围 0.195–0.195 秒。
- 各轮结果：{'passed': 1}。
- 最慢用例（跨轮中位数）：`mcp.frameCapture.protocolPreviewAndAtomicNativeExport` 0.195 秒。

### 121. [procgen_road_strength.cpp](../../../test/integration/gameplay-composition/procgen_road_strength.cpp)

- 路径：`test/integration/gameplay-composition/procgen_road_strength.cpp`；1 个用例。
- 用例累计耗时中位数：**0.181 秒**；范围 0.181–0.181 秒。
- 各轮结果：{'passed': 1}。
- 最慢用例（跨轮中位数）：`procgen.road.authoring.splineStrengthPresetOverrideSaveAndUndo` 0.181 秒。

### 122. [environment_query_tests.cpp](../../../test/integration/sensing/environment_query_tests.cpp)

- 路径：`test/integration/sensing/environment_query_tests.cpp`；1 个用例。
- 用例累计耗时中位数：**0.177 秒**；范围 0.177–0.177 秒。
- 各轮结果：{'passed': 1}。
- 最慢用例（跨轮中位数）：`sensing.environment.scriptPositionsUseCanonicalExecutor` 0.177 秒。

### 123. [scene_transaction_observer.cpp](../../../test/integration/scene-editing/scene_transaction_observer.cpp)

- 路径：`test/integration/scene-editing/scene_transaction_observer.cpp`；1 个用例。
- 用例累计耗时中位数：**0.176 秒**；范围 0.176–0.176 秒。
- 各轮结果：{'passed': 1}。
- 最慢用例（跨轮中位数）：`scene.transaction.observer_exception_does_not_reject_published_state` 0.176 秒。

### 124. [procgen_road_circular.cpp](../../../test/integration/gameplay-composition/procgen_road_circular.cpp)

- 路径：`test/integration/gameplay-composition/procgen_road_circular.cpp`；1 个用例。
- 用例累计耗时中位数：**0.167 秒**；范围 0.167–0.167 秒。
- 各轮结果：{'passed': 1}。
- 最慢用例（跨轮中位数）：`procgen.road.authoring.circularArcLimitsAndPresetUndo` 0.167 秒。

### 125. [physics_merge_contract.cpp](../../../test/integration/physics-simulation/physics_merge_contract.cpp)

- 路径：`test/integration/physics-simulation/physics_merge_contract.cpp`；2 个用例。
- 用例累计耗时中位数：**0.148 秒**；范围 0.148–0.148 秒。
- 各轮结果：{'passed': 2}。
- 最慢用例（跨轮中位数）：`physics.merge.spatialAddedJointKindsRetainParameters` 0.075 秒；`physics.merge.planarFactoriesShareIdentityAndSnapshot` 0.073 秒。

### 126. [script_api_procgen_cave_generation_stages_tests.cpp](../../../test/integration/script-api/script_api_procgen_cave_generation_stages_tests.cpp)

- 路径：`test/integration/script-api/script_api_procgen_cave_generation_stages_tests.cpp`；1 个用例。
- 用例累计耗时中位数：**0.143 秒**；范围 0.143–0.143 秒。
- 各轮结果：{'passed': 1}。
- 最慢用例（跨轮中位数）：`procgen.stages.scriptOwnsFieldsAndComposesCaveWithoutGraphics` 0.143 秒。

### 127. [physics_field_set.cpp](../../../test/integration/physics-simulation/physics_field_set.cpp)

- 路径：`test/integration/physics-simulation/physics_field_set.cpp`；2 个用例。
- 用例累计耗时中位数：**0.135 秒**；范围 0.135–0.135 秒。
- 各轮结果：{'passed': 2}。
- 最慢用例（跨轮中位数）：`physics.forces.fieldSetFiltersAndSuperposes2D` 0.072 秒；`physics.forces.fieldSet3DOrderAndAtomicFailures` 0.063 秒。

### 128. [CrowdScript.cpp](../../../test/integration/crowd-runtime/CrowdScript.cpp)

- 路径：`test/integration/crowd-runtime/CrowdScript.cpp`；1 个用例。
- 用例累计耗时中位数：**0.103 秒**；范围 0.103–0.103 秒。
- 各轮结果：{'passed': 1}。
- 最慢用例（跨轮中位数）：`crowd.script.runtimeBindingContracts` 0.103 秒。

### 129. [gameplay_composition_action_preview_profile_tests.cpp](../../../test/integration/gameplay-composition/gameplay_composition_action_preview_profile_tests.cpp)

- 路径：`test/integration/gameplay-composition/gameplay_composition_action_preview_profile_tests.cpp`；1 个用例。
- 用例累计耗时中位数：**0.086 秒**；范围 0.086–0.086 秒。
- 各轮结果：{'passed': 1}。
- 最慢用例（跨轮中位数）：`actionPreviewProfile.sidecarStoreRoundTripsAndRejectsIdentityMismatch` 0.086 秒。

### 130. [terrain_world_origin_vm.cpp](../../../test/integration/script-api/terrain_world_origin_vm.cpp)

- 路径：`test/integration/script-api/terrain_world_origin_vm.cpp`；1 个用例。
- 用例累计耗时中位数：**0.086 秒**；范围 0.086–0.086 秒。
- 各轮结果：{'passed': 1}。
- 最慢用例（跨轮中位数）：`procgen.road.worldOrigin.squirrelCreationAndWorldHeightUseBaseline` 0.086 秒。

### 131. [editor_control_rig_limits.cpp](../../../test/integration/control-rig/editor_control_rig_limits.cpp)

- 路径：`test/integration/control-rig/editor_control_rig_limits.cpp`；1 个用例。
- 用例累计耗时中位数：**0.079 秒**；范围 0.079–0.079 秒。
- 各轮结果：{'passed': 1}。
- 最慢用例（跨轮中位数）：`editor.controlRig.limits.sharedHistoryAndAtomicRejection` 0.079 秒。

### 132. [module_composition_devtools_frame_capture_tests.cpp](../../../test/integration/module-composition/module_composition_devtools_frame_capture_tests.cpp)

- 路径：`test/integration/module-composition/module_composition_devtools_frame_capture_tests.cpp`；1 个用例。
- 用例累计耗时中位数：**0.076 秒**；范围 0.076–0.076 秒。
- 各轮结果：{'passed': 1}。
- 最慢用例（跨轮中位数）：`mcp.frameCapture.providerAbsentAndSchemaRoutes` 0.076 秒。

### 133. [avatar_appearance_script.cpp](../../../test/integration/avatar-appearance/avatar_appearance_script.cpp)

- 路径：`test/integration/avatar-appearance/avatar_appearance_script.cpp`；1 个用例。
- 用例累计耗时中位数：**0.075 秒**；范围 0.075–0.075 秒。
- 各轮结果：{'passed': 1}。
- 最慢用例（跨轮中位数）：`avatar.appearance.scriptBoundaryUsesCheckedResults` 0.075 秒。

### 134. [rts_terrain_combat.cpp](../../../test/integration/gameplay-composition/rts_terrain_combat.cpp)

- 路径：`test/integration/gameplay-composition/rts_terrain_combat.cpp`；1 个用例。
- 用例累计耗时中位数：**0.075 秒**；范围 0.075–0.075 秒。
- 各轮结果：{'passed': 1}。
- 最慢用例（跨轮中位数）：`rts.terrainRidgeStopsFireAndTerrainEditsReopenFireLine` 0.075 秒。

### 135. [procgen_road_surface_path.cpp](../../../test/integration/gameplay-composition/procgen_road_surface_path.cpp)

- 路径：`test/integration/gameplay-composition/procgen_road_surface_path.cpp`；1 个用例。
- 用例累计耗时中位数：**0.072 秒**；范围 0.072–0.072 秒。
- 各轮结果：{'passed': 1}。
- 最慢用例（跨轮中位数）：`procgen.road.surfacePath.retainsPresetHistoryAndMigratesMissingPolicy` 0.072 秒。

### 136. [procgen_road_controllers.cpp](../../../test/integration/gameplay-composition/procgen_road_controllers.cpp)

- 路径：`test/integration/gameplay-composition/procgen_road_controllers.cpp`；1 个用例。
- 用例累计耗时中位数：**0.068 秒**；范围 0.068–0.068 秒。
- 各轮结果：{'passed': 1}。
- 最慢用例（跨轮中位数）：`procgen.road.authoring.outgoingControllerPresetSaveAndUndo` 0.068 秒。

### 137. [procgen_everyday_material_optics.cpp](../../../test/integration/script-api/procgen_everyday_material_optics.cpp)

- 路径：`test/integration/script-api/procgen_everyday_material_optics.cpp`；1 个用例。
- 用例累计耗时中位数：**0.065 秒**；范围 0.065–0.065 秒。
- 各轮结果：{'passed': 1}。
- 最慢用例（跨轮中位数）：`procgen.everyday.scriptOpticsMatchNative` 0.065 秒。

### 138. [physics_joint2d_script.cpp](../../../test/integration/physics-simulation/physics_joint2d_script.cpp)

- 路径：`test/integration/physics-simulation/physics_joint2d_script.cpp`；1 个用例。
- 用例累计耗时中位数：**0.058 秒**；范围 0.058–0.058 秒。
- 各轮结果：{'passed': 1}。
- 最慢用例（跨轮中位数）：`physics.joint2d.scriptResultAndOwningRelease` 0.058 秒。

精简后的完整 138 CPP / 4 并发实测及旧基线比较见 [全量最新测量](integration-full-runtime.md)。
