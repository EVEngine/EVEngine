# PR #625：CI 失败记录与本轮处理范围（2026-10-22）

## 本轮修复

检查运行 [CI 39701434735](https://github.com/EVEngine/EVEngine/actions/runs/39701434735)，
失败提交为 `9b7d759404f5c81595e21d0995d29c9c280c66ec`。

[Windows 构建](https://github.com/EVEngine/EVEngine/actions/runs/39701434735/job/119583853830)
在 `assertion_contract.cpp` 和 `async_unit.cpp` 的比较 CHECK 上报 C4003，随后出现 C3409 等解析错误。
原因是新增 `ZEROERR_CHECK_EQ(...) -> ZEROERR_REQUIRE_EQ(__VA_ARGS__)` 的转发层在 MSVC
传统预处理器下把参数包当作单个参数，破坏下一级具名比较宏的展开。六个比较 CHECK 改为
直接带具名 lhs/rhs 调用 `ZEROERR_ASSERT_CMP`，保持 ERROR 级别、单次求值及失败退出语义。
probe 的既有单次求值用例增加 NE/LT/LE/GT/GE 的编译覆盖，包含省略诊断消息的调用。

用户已明确选择本轮仅提交 Windows 宏兼容修复，并保留其余失败清单。
本轮没有修改其他模块、放宽断言、跳过用例或更改失败退出码。

## 日志范围与未处理检查

- [Linux](https://github.com/EVEngine/EVEngine/actions/runs/39701434735/job/119583853900)：
  [完整 CTest 工件](https://github.com/EVEngine/EVEngine/actions/runs/39701434735/artifacts/12399947768)
  的 `LastTestsFailed.log` 记录 318 个失败用例。
- [macOS](https://github.com/EVEngine/EVEngine/actions/runs/39701434735/job/119583853870)：
  [完整 CTest 工件](https://github.com/EVEngine/EVEngine/actions/runs/39701434735/artifacts/12400139475)
  记录 324 个失败用例。
- [Linux ASan+UBSan](https://github.com/EVEngine/EVEngine/actions/runs/39701434735/job/119583854016)：
  已下载的作业日志可见至少 82 个用例输出 ERROR assertion，尾部截断；未找到对应测试工件，
  因此不把该数字当作完整失败总数，也不推断未显示的 sanitizer 诊断。
- 普通运行日志及独立作业日志均存在尾部截断。下表以下载的普通 Linux/macOS CTest 工件为准：
  315 个用例在两平台都失败，合计 327 个不同用例。它们是待调查记录，不是允许失败列表，
  不接入 CTest 筛选或通过判定，也不代表每项根因已确定。

本机正常宿主入口的 `assertion_contract_probe` 已重新编译，CTest
`test.assertionExitContract` 通过。`make check CI_BASE=5151e1480cfe3909982c12ca1ce43ba594e3fca0`
通过，461 项脚本/契约测试通过，`git diff --check` 通过。
当前主机为 macOS，MSVC 实际编译结果需等待 Windows CI。
本页不声明四个平台 CI 已通过。

## 其余失败清单

| 用例 | Linux | macOS |
|---|---|---|
| `Camera.panAndZoomAffectOffscreenPixels` | 失败 | 失败 |
| `Canvas.offscreenGetPixelAndSampleOnScreen` | 失败 | 失败 |
| `Capability.leaseMovedFromIsEmpty` | 失败 | 失败 |
| `Database.SQLiteCrudOrmAndECSExport` | 失败 | 失败 |
| `GpuDriven.capsAvailable` | — | 失败 |
| `GpuDrivenParity.cameraFacingCardTurnsTowardSideCamera` | 失败 | — |
| `GpuDrivenParity.maskedAlphaCutoff` | 失败 | — |
| `PhysicsCloth.xpbdComplianceProducesTimeStepAwareMaterialResponse` | 失败 | 失败 |
| `RenderSystem3D.dynamicVertexRingTracksLatestUpdate` | 失败 | 失败 |
| `RenderSystem3D.entityIdMaskPixels` | 失败 | 失败 |
| `RenderSystem3D.gpuOpaqueCollectorRunsBeforeCullAndUnregisters` | 失败 | 失败 |
| `RenderSystem3D.readTextureBackPixels` | 失败 | 失败 |
| `Scene.SceneComponent.buildWhenAndSlot` | 失败 | 失败 |
| `Scene.api.transformGettersAndSpaceConversion` | 失败 | 失败 |
| `Scene.index.lazyRebuild` | 失败 | 失败 |
| `Scene.script.entityEnableAndDelayedDetach` | 失败 | 失败 |
| `Scene.serialize.roundTrip` | 失败 | 失败 |
| `SnowField.normalMapFromGradient` | 失败 | 失败 |
| `UI.componentScheduling.scriptCoalescingAndFailureRecovery` | 失败 | 失败 |
| `UI.editorKit.desktopCompositionRenders` | 失败 | 失败 |
| `UI.p0.scriptEventCallbacks` | 失败 | 失败 |
| `UI.smoke.RenderSystemSameFrameVulkanOverlay` | 失败 | 失败 |
| `UIHost.setPropsAndClickHandler` | 失败 | 失败 |
| `actionMontage.upperBodyCompensationObservesProviderMaskAndFullBodyPolicy` | 失败 | 失败 |
| `actionMontage.upperBodyCompensationRunsBeforeAdditiveLayersDeterministically` | 失败 | 失败 |
| `actionPrefabRuntime.spawnsRealRenderablePoolsAndAppliesThreeLifecycles` | 失败 | 失败 |
| `actionTimelineWidget.insertsRegisteredNotifyShapesAtCursor` | 失败 | 失败 |
| `animation.clip.notifyContractRejectsMissingSemanticEvents` | 失败 | 失败 |
| `animation.lattice.clampVsExtrapolate` | 失败 | 失败 |
| `animation.lattice.cornerScaleTrilinear` | 失败 | 失败 |
| `animation.lattice.offsetBulge` | 失败 | 失败 |
| `animation.mixamo.skeletonIdleRunJumpPreview` | 失败 | 失败 |
| `animation.motionMatching.anyStrictCostImprovementCanSwitch` | 失败 | 失败 |
| `animation.motionMatching.duplicateRangeRetainsCostPairFromWinningCandidate` | 失败 | 失败 |
| `animation.motionMatching.exhaustedOneShotHandsOffAndDoesNotReselect` | 失败 | 失败 |
| `animation.motionMatching.inertiaRunsThroughMatcherSearchAndAdvance` | 失败 | 失败 |
| `animation.motionSchema.normalizationRangeValidationIsAtomic` | 失败 | 失败 |
| `animation.motionSchema.velocitySelectionCopiesQueryAndRejectsInvalidReplacement` | 失败 | 失败 |
| `animation.orientationWarping.thresholdAndLowSpeedTargetZero` | 失败 | 失败 |
| `animation.physicalBalance.recoilDeflectsHitBone` | 失败 | 失败 |
| `animation.pose.twoBoneIkReachesTargetsWithoutIteration` | 失败 | 失败 |
| `animation.procedural.dynamicBoneColliderModesAreRuntimeConfigurable` | 失败 | 失败 |
| `animation.rootMotionPolicy.crossFadeBlendsPublishedDeltaAndBakesBind` | 失败 | 失败 |
| `animation.skinned.normalsFollowPose` | 失败 | 失败 |
| `animation.spine.windowDemoRealModels` | 失败 | 失败 |
| `animation.sprite.uniformFpsRangeReverseAndEvents` | 失败 | 失败 |
| `ao.overlayDarkensLitScene` | 失败 | 失败 |
| `asset.graphics.vegetationSceneLoadsAndProjectsAtomically` | — | 失败 |
| `asset.import.unityTerrainDetailSidecarImportsPublicApiTransformsAtomically` | 失败 | 失败 |
| `asset.procgen.terrainGpuGroupBindsSecondControlLayerSet` | 失败 | 失败 |
| `asset.runtimeWorldLoadersRejectCrossChunkMismatchAndSceneCycles` | 失败 | 失败 |
| `async.asyncSeq` | 失败 | 失败 |
| `async.eveScriptAwaitStateMachine` | 失败 | 失败 |
| `attributes.domainAdapters.projectSelectedStats` | 失败 | 失败 |
| `attributes.modifiersUsePriorityThenSequence` | 失败 | 失败 |
| `building.edgeCubicBezierSamplesDeterministicallyAndCommitsAtomically` | 失败 | 失败 |
| `building.structuralLinksRebuildAtomicallyAfterDefinitionReload` | 失败 | 失败 |
| `buildingfx.edgeCurveCommitsAndExtrudesCustomSurfaceFrames` | 失败 | 失败 |
| `buildingfx.projectsDiscreteFloorAlongGridNormal` | 失败 | 失败 |
| `combatCharacter.dodgeRelativeAndKnockbackWhileStunned` | 失败 | 失败 |
| `combatScript.runtimeOwnsMovementAndDamageState` | 失败 | 失败 |
| `common.zeroerr.preservesAssertionCounters` | 失败 | 失败 |
| `composableGameplay.cardAndRtsAdaptersShareDefinitionsTerrainAndIdentity` | 失败 | 失败 |
| `composableGameplay.scriptBuildsRebellionWithoutDomainCpp` | 失败 | 失败 |
| `container.adaptersShareParityAndCanonicalLifecycleEnvelope` | 失败 | 失败 |
| `daynight.nightBelowHorizon` | 失败 | 失败 |
| `daynight.sunColorWarmsNearHorizon` | 失败 | 失败 |
| `daynight.sunElevationPeaksAtNoon` | 失败 | 失败 |
| `decal.gpuHeadlessProjectionReadback` | 失败 | 失败 |
| `definitionRuntime.vehicleAdapterProjectsEntityAndRejectsStaleSnapshot` | 失败 | 失败 |
| `devtools.console.attachEvalPrintCapture` | 失败 | 失败 |
| `devtools.languageServer.completesChainedAndConstructedNativeTypes` | 失败 | 失败 |
| `devtools.languageServer.diagnosticsCompletionHoverAndSymbols` | 失败 | 失败 |
| `devtools.languageServer.formatFoldAndIncrementalEdit` | 失败 | 失败 |
| `devtools.languageServer.memberCompletionAndSignatureHelp` | 失败 | 失败 |
| `devtools.mcp.consoleToolsRoundTripScriptPrint` | 失败 | 失败 |
| `devtools.mcp.hostEditorBinding` | 失败 | 失败 |
| `devtools.mcp.hostResourceHotReload` | 失败 | 失败 |
| `devtools.mcp.initializeNegotiatesSupportedProtocol` | 失败 | 失败 |
| `devtools.mcp.initializeToolsStatus` | 失败 | 失败 |
| `devtools.mcp.profilerToolsReadTheLiveCore` | 失败 | 失败 |
| `devtools.mcp.spatialQueryToolsValidateAndReportMissingProviders` | 失败 | 失败 |
| `devtools.mcp.stderrCaptureFeedsConsole` | 失败 | 失败 |
| `devtools.projectIndex.incrementRenameDeleteAndRejectFailedPublication` | 失败 | 失败 |
| `dialogue.stage.avatarTypewriterPreview` | 失败 | 失败 |
| `dialoguePersistence.jsonRoundtrip` | 失败 | 失败 |
| `dialogueState.usesSharedConditionExplanation` | 失败 | 失败 |
| `dialogueState.valueRoundtripAndLegacyFacadeParity` | 失败 | 失败 |
| `economy.system.emitsEventsAndHooks` | 失败 | 失败 |
| `editor.actionTimeline.scriptUsesCanonicalTransactionsAndWorkspace` | 失败 | 失败 |
| `editor.animation.clip_editor_installs_workspace_and_reverses_mask` | 失败 | 失败 |
| `editor.animation.clip_editor_pointer_selects_bone_and_rejects_invalid_settings` | 失败 | 失败 |
| `editor.animation.clip_editor_script_composes_workspace_and_transactions` | 失败 | 失败 |
| `editor.animation.clip_snapshot_and_retarget_preview_are_non_destructive` | 失败 | 失败 |
| `editor.api.session_and_history_preserve_failure_status` | 失败 | 失败 |
| `editor.api.session_execute_checked_forwards_transaction_diagnostics` | 失败 | 失败 |
| `editor.audio.source_editor_installs_workspace_and_reverses_volume` | 失败 | 失败 |
| `editor.audio.source_editor_script_composes_workspace_and_transactions` | 失败 | 失败 |
| `editor.avatar.document_editor_installs_workspace_and_reverses_layer_z` | 失败 | 失败 |
| `editor.avatar.document_editor_script_composes_workspace_and_rejects_referenced_delete` | 失败 | 失败 |
| `editor.biome.rules_editor_installs_workspace_and_previews_deterministically` | 失败 | 失败 |
| `editor.biome.rules_editor_script_composes_workspace_and_exclusions` | 失败 | 失败 |
| `editor.building.edge_curve_is_one_atomic_undoable_gesture` | 失败 | 失败 |
| `editor.building.surface_attachment_survives_replace_and_undo` | 失败 | 失败 |
| `editor.procgen.script_editor_installs_workspace_and_commits_params` | 失败 | 失败 |
| `editor.procgen.script_editor_script_loads_schema_and_keeps_failed_preview` | 失败 | 失败 |
| `editor.targets.expose_capabilities_and_dirty_regions` | 失败 | 失败 |
| `editor.ui.theme_editor_script_composes_workspace_and_transactions` | 失败 | 失败 |
| `editor.v2.authority_compensation_validates_full_inverse_on_candidate` | 失败 | 失败 |
| `editor.v2.authority_rolls_back_partial_commit` | 失败 | 失败 |
| `editor.v2.automation_capability_discovers_executes_plans_and_cancels` | 失败 | 失败 |
| `editor.v2.automation_edits_material_through_the_same_transaction_path` | 失败 | 失败 |
| `editor.v2.disk_document_atomic_conflict_and_autosave` | 失败 | 失败 |
| `editor.v2.document_save_detects_external_revision_conflict` | 失败 | 失败 |
| `editor.v2.property_model_failed_commit_keeps_target_unchanged_and_is_discardable` | 失败 | 失败 |
| `editor.voxel.catalog_classifies_filled_and_partial_occupancy` | 失败 | 失败 |
| `editor.voxel.catalog_editor_script_composes_workspace_and_occupancy` | 失败 | 失败 |
| `event.clearDestroysOwnedPayloadExactlyOnce` | 失败 | 失败 |
| `event.ownedPushPollDestroysPayloadWithMessage` | 失败 | 失败 |
| `event_envelope.restoreRejectsDuplicateNonNilEventId` | 失败 | 失败 |
| `fluids.volume.colliderTriggersReportWithoutEnforcement` | 失败 | 失败 |
| `fluids.volume.collisionMaterialAdhesionUsesFluid3DCombinePriority` | 失败 | 失败 |
| `fluids.volume.particleInstancesFilterScaleColorAndInterpolate` | 失败 | 失败 |
| `fluids.volume.sdfSliceDebuggerSamplesAndTransformsOnDemand` | 失败 | 失败 |
| `gameplay.control.dialoguePlaysConversationThroughItsOwnOperations` | 失败 | 失败 |
| `gameplay.control.dialoguePublishesAtMostOneRunnerInstance` | 失败 | 失败 |
| `gameplay.control.dialoguePublishesNodeAndRouteVocabulary` | 失败 | 失败 |
| `gameplay.control.dialogueRoutesThroughTheSharedJsonFacade` | 失败 | 失败 |
| `gameplay.control.jsonFacadeRejectsUnknownSchemaFields` | 失败 | 失败 |
| `gameplay.control.rpgBattleUsesTheSameCheckedActionForPlayerAndAutomation` | 失败 | 失败 |
| `gameplayAction.phasePipelineUsesInjectedSimulationTime` | 失败 | 失败 |
| `gameplayTags.registryValidatesStableNamesAndIds` | 失败 | 失败 |
| `graphics.Grass.gpuRenderScreenshot` | 失败 | 失败 |
| `graphics.Shader.customSpvOnSprite` | 失败 | 失败 |
| `graphics.WaterUnderwater.transitionsFogCausticsAndSurfaceRestore` | 失败 | 失败 |
| `graphics.WaterUnderwaterFog.appliesAndRestoresVolumetricProvider` | 失败 | 失败 |
| `graphics.displayOutput.encodeHdr10PaperWhiteNearMidCode` | 失败 | 失败 |
| `graphics.gbufferPlaneDepthMatchesCameraRay` | 失败 | 失败 |
| `graphics.hair.guideSimulatorPinsRootsAndFalls` | 失败 | 失败 |
| `graphics.imageAudit.gbufferViews` | 失败 | 失败 |
| `graphics.imageAudit.postQuality` | 失败 | 失败 |
| `graphics.lightProbe.customProvidedChangesRenderedPixels` | 失败 | 失败 |
| `graphics.mapFog.unlockedCellsStayClear` | 失败 | 失败 |
| `graphics.render3d.toCanvas` | 失败 | 失败 |
| `graphics.resourceReuse.fileCacheDoesNotReloadResidentPixels` | 失败 | 失败 |
| `graphics.vegetation.gradient_uses_mesh_height_and_blended_blue_mask` | — | 失败 |
| `graphics.vegetation.motion_array_deforms_rest_stream_with_explicit_noise_and_time` | — | 失败 |
| `graphics.vegetation.normal_gpu_matches_filtered_cpu_reference` | — | 失败 |
| `graphics.vegetation.pbr_front_face_culling_matches_tve_render_cull` | 失败 | 失败 |
| `graphics.vegetation.translucency_gpu_backlight_and_ambient` | — | 失败 |
| `graphics.vegetation.vertex_occlusion_rgb_matches_tve_remap` | — | 失败 |
| `graphics.waterReflectionMasker.samplesPcgRoundedCoordinatesAndChannels` | 失败 | 失败 |
| `graphics_fog.interactorClearsInteriorAndBlocksThinAdvection` | 失败 | 失败 |
| `hd2d.tilemap.heightRaisesBoxes` | 失败 | 失败 |
| `hex.data.catalog.bootEachLevelDistinctConfig` | 失败 | 失败 |
| `hexmap.generate.isDeterministicAndProducesLandWaterAndPlants` | 失败 | 失败 |
| `hexmap.sphereMap.resetBuildsEveryCell` | 失败 | 失败 |
| `hexmap.sphereMesh.verticesStayOnTheirSurfaceRadius` | 失败 | 失败 |
| `housegen.footprintIncludesRotatedMultiCellComponents` | 失败 | 失败 |
| `identity.audit.logicalNamespaceIsCanonicalAndNamesRemainCaseSensitive` | 失败 | 失败 |
| `map.fov.perception.canDetect` | 失败 | 失败 |
| `map.fov.volume.verticalExtend` | 失败 | 失败 |
| `map.path.customGrid` | 失败 | 失败 |
| `map.path.flowField.matchesAstarReachability` | 失败 | 失败 |
| `map.path.layerMetadataIsNavigationAuthority` | 失败 | 失败 |
| `map.tileset.loadsExternalTsjRelativeToMap` | 失败 | 失败 |
| `orders.snapshot.roundTripAndMalformedRestorePreservesState` | 失败 | 失败 |
| `particles.attach.boneFollowsPose` | 失败 | 失败 |
| `particles.attach.clipSampleMovesEmitter` | 失败 | 失败 |
| `particles.attach.ik2dSolverStepTracksTip` | 失败 | 失败 |
| `particles.attach.ik3dSolverMovesEmitter` | 失败 | 失败 |
| `particles.attach.moveToOverwrittenBySync` | 失败 | 失败 |
| `particles.config.applyBlendFlipbookGradient` | 失败 | 失败 |
| `particles.config.applyJsonText` | 失败 | 失败 |
| `particles.effectAsset.naturalDurationUsesFiniteEmissionAndRejectsUnboundedAssets` | 失败 | 失败 |
| `particles.effectAsset.versionedMultiEmitterContract` | 失败 | 失败 |
| `particles.floatingOrigin.shiftsWorldParticlesWithoutChangingPlayback` | 失败 | 失败 |
| `particles.skin.filterByIndexAndClearFilter` | 失败 | 失败 |
| `physics.artifact.compositeColliderUsesPublicProcgenPublication` | 失败 | 失败 |
| `physics.backend.explicitWorldSelectionRejectsAbsentProvider` | 失败 | 失败 |
| `physics.forces.tireAirborneFreeRollingAndBrakeLock` | — | 失败 |
| `physics.joint2d.distanceRevoluteWeldMotorAndGearAreUsable` | 失败 | 失败 |
| `physics.jolt.meshRollingMaterialAndHitParity` | 失败 | 失败 |
| `physics.jolt.oneWayAndPairOverridesParity` | 失败 | 失败 |
| `physics.jolt.solvedContactCoverageLifetimeParity` | 失败 | 失败 |
| `physics.jolt.solvedImpulseMomentumParity` | 失败 | 失败 |
| `physics.jolt.worldControlsAndBoundsParity` | 失败 | 失败 |
| `physics.mechanism.ratchetBlocksReverseSpin` | 失败 | 失败 |
| `physics.rope.continuousCollisionStopsAtTriangleMesh` | 失败 | 失败 |
| `pixelworld.control_edit_snapshot_and_diagnostics_are_transactional` | 失败 | 失败 |
| `pixelworld.material_catalog_hot_reload_is_paused_compatible_and_transactional` | 失败 | 失败 |
| `pixelworld_editor_mounts_a_live_panel_and_reconciles_callback_edits` | 失败 | 失败 |
| `pixelworld_physics.terrain_cache_rebuilds_dirty_chunks_and_rolls_back_budget_failure` | 失败 | 失败 |
| `pixelworld_replay_chunk_digest_has_canonical_little_endian_encoding` | 失败 | 失败 |
| `pixelworld_streaming_disconnect_reconnect_applies_authoritative_chunk_delta` | 失败 | 失败 |
| `pixelworld_streaming_full_resync_atomically_clears_projection_and_accepts_rewind` | 失败 | 失败 |
| `pixelworld_streaming_interest_move_emits_entry_and_eviction_correction` | 失败 | 失败 |
| `pixelworld_transport_buffers_transfer_order_and_retains_tombstones_until_ACK` | 失败 | 失败 |
| `procgen.cableRope.textures.resultApiAndDefaults` | 失败 | 失败 |
| `procgen.cave.cellular.reproducible` | 失败 | 失败 |
| `procgen.cave.drunkard.reproducible` | 失败 | 失败 |
| `procgen.dungeon.bsp.reproducible` | 失败 | 失败 |
| `procgen.e2e.hexProvidersCommitQueryableObjects` | 失败 | 失败 |
| `procgen.gtsGeological.blendsHeightStripColorAndPackedNormal` | 失败 | 失败 |
| `procgen.gtsTerrainLod.rejectsInvalidProfileWithoutPartialResult` | 失败 | 失败 |
| `procgen.gtsTerrainObj.maskedMatchesPcgCellClassificationAndWinding` | 失败 | 失败 |
| `procgen.gtsWeather.bakesPackedPbrAndDisplacementAtomically` | 失败 | — |
| `procgen.heightmapSlopeQuery.threeSourceFormulasAndBinding` | 失败 | 失败 |
| `procgen.json.export` | 失败 | 失败 |
| `procgen.maze.backtrack.reproducible` | 失败 | 失败 |
| `procgen.mesh.bush.reproducibleAndStyles` | 失败 | 失败 |
| `procgen.mesh.marchingcubes.fillDensityAndErrors` | 失败 | 失败 |
| `procgen.mesh.marchingcubes.isolevelAffectsMesh` | 失败 | 失败 |
| `procgen.mesh.marchingcubes.moduleNullAndList` | 失败 | 失败 |
| `procgen.mesh.marchingcubes.noiseReproducibleAndVaries` | 失败 | 失败 |
| `procgen.mesh.marchingcubes.resolutionScales` | 失败 | 失败 |
| `procgen.mesh.marchingcubes.sphereVolumeSignStable` | 失败 | 失败 |
| `procgen.mesh.marchingcubes.torusAndAxisResolution` | 失败 | 失败 |
| `procgen.meshMerge.softSnapThenWeldSharesContactVerts` | 失败 | 失败 |
| `procgen.noise.terrain.reproducible` | 失败 | 失败 |
| `procgen.pbr.viaModuleAndErrors` | 失败 | 失败 |
| `procgen.pointGraph.meshSampleRequiresMeshSurface` | 失败 | 失败 |
| `procgen.pointGraph.reflectsOperationsForEditors` | 失败 | 失败 |
| `procgen.pointGraph.samplesLandscapeAndTextureOntoChannels` | 失败 | 失败 |
| `procgen.pointSet.largeIdentityUnionAvoidsQuadraticScan` | 失败 | 失败 |
| `procgen.recipeSchemas.meshDefaults` | 失败 | 失败 |
| `procgen.render.castlePng` | 失败 | 失败 |
| `procgen.roguelike.rulesChangeLayout` | 失败 | 失败 |
| `procgen.roguelike.structure` | 失败 | 失败 |
| `procgen.script.reflects_schema_and_generates_from_dynamic_values` | 失败 | 失败 |
| `procgen.system.cachedBuildReusesCommittedSnapshot` | 失败 | 失败 |
| `procgen.terrain.asset.chunkedRoundTripAndCorruptionDetection` | 失败 | 失败 |
| `procgen.terrain.materialShaderRendersPortableSixteenLayerContract` | 失败 | 失败 |
| `procgen.terrain.mesh.lodSkirtsStableSeamsAndMaterialWeights` | 失败 | 失败 |
| `procgen.terrain.pipeline.fluvialBreachesClosedBasinSpillSill` | 失败 | 失败 |
| `procgen.terrain.pipeline.fluvialResolvesValleyCrossSections` | 失败 | 失败 |
| `procgen.terrain.pipeline.multiSeedDrainageMorphology` | 失败 | 失败 |
| `procgen.terrainSplatmap.renormalizesTargetAndOtherLayers` | 失败 | 失败 |
| `procgen.terrainTextureAligner.bindsRealVm` | 失败 | 失败 |
| `procgen.wfc.simple.cavePreset` | 失败 | 失败 |
| `procgen.wfc.simple.dungeonObjectsAndMeta` | 失败 | 失败 |
| `procgen.wfc.simple.errors` | 失败 | 失败 |
| `procgen.wfc.simple.multiSeedBatch` | 失败 | 失败 |
| `procgen.wfc.simple.seedVaries` | 失败 | 失败 |
| `procgen.wfc.simple.terrainPreset` | 失败 | 失败 |
| `property.validation_editor_model_rejects_before_command_sink` | 失败 | 失败 |
| `reflectScriptExposesRuntimeApi` | 失败 | 失败 |
| `render_graph.engineTopology` | 失败 | 失败 |
| `render_graph.jobSystemExecutorParallel` | 失败 | 失败 |
| `rpg.battle.activeStatusLifestealRunsThroughSettlementChain` | 失败 | 失败 |
| `rpg.battle.activeStatusRuleProjectionTracksRemoval` | 失败 | 失败 |
| `rpg.battle.checkedActionRejectsIllegalTargetsAndDuplicatesAtomically` | 失败 | 失败 |
| `rpg.battle.criticalSettlementPreservesMetadataWithoutDoubleScaling` | 失败 | 失败 |
| `rpg.battle.damageUsesSharedSettlementRules` | 失败 | 失败 |
| `rpg.battle.defeatWhenPartyDies` | 失败 | 失败 |
| `rpg.battle.dynamicResourceDamage` | 失败 | 失败 |
| `rpg.battle.multiSideWinner` | 失败 | 失败 |
| `rpg.battle.playerSideConfigurable` | 失败 | 失败 |
| `rpg.battle.policyTargetsLowestHealthLegalParticipantDeterministically` | 失败 | 失败 |
| `rpg.battle.roundVictory` | 失败 | 失败 |
| `rpg.battle.skillDamageViaRegistry` | 失败 | 失败 |
| `rpg.battleTactics.strictReplacementAndConditionalAllyHealing` | 失败 | 失败 |
| `rpg.classic.mainScriptCompilesThroughEveScriptFrontend` | 失败 | 失败 |
| `rpg.classic.playthroughCompletesBothQuestsAndRestoresCheckpoint` | 失败 | 失败 |
| `rpg.state.captureRestoreCastingAndCooldown` | 失败 | 失败 |
| `rpg.status.periodicConfigurationBuildsCanonicalSettlementRequest` | 失败 | 失败 |
| `rts.abilitiesUseCanonicalDamageEconomyCooldownAndInterruptibleChannels` | 失败 | 失败 |
| `rts.actionAdapterReusesSharedActionRuntime` | 失败 | 失败 |
| `rts.coordinatedVolleyHoldsAndReleasesGroupWhileExplicitAttackBypassesSchedule` | 失败 | 失败 |
| `rts.largeExactlyOverlappingCrowdUnlocksDeterministically` | 失败 | 失败 |
| `rts.legacySandboxContentMaterializesEveryArchetype` | 失败 | 失败 |
| `rts.moduleResolvesStableSubjectsAndRejectsCrossRootDuplicates` | 失败 | 失败 |
| `rts.projectilesUseCanonicalTrajectorySweptImpactAndHostileSplashDamage` | 失败 | 失败 |
| `rts.scriptAIReplenishesWorkersBuildsArmyAndRestoresDeterministicIdentitySequence` | 失败 | 失败 |
| `rts.scriptConstructionAtomicallyPaysBuildsAndCompletesCanonicalOrder` | 失败 | 失败 |
| `rts.scriptGridBlocksHitscanAndNewObstaclesInterceptProjectiles` | 失败 | 失败 |
| `rts.scriptStatusEffectsDriveMovementDamageIntakeRegenerationAndExpiry` | 失败 | 失败 |
| `rts.scriptTerrainHeightDrivesFogFireLinesAndCheckpoints` | 失败 | 失败 |
| `rts.supplyRendezvousAvoidsVisibleHostileCoverage` | 失败 | 失败 |
| `rts.upstreamSupplyAutoDispatchesAndLeadsMovingRelay` | 失败 | 失败 |
| `scene.componentScheduling.automaticAndDestructionSafe` | 失败 | 失败 |
| `scene.editor.physics_placement_auto_hull_cache_is_explicit_and_session_owned` | 失败 | 失败 |
| `scene.pcgGrowth.appliesToAndDestroysRealNode` | 失败 | 失败 |
| `scene.physicsPlacement.rotate_and_point_are_distinct_tool_policies` | 失败 | 失败 |
| `scene.scriptLink.pullDrivesNode` | 失败 | 失败 |
| `schema.registry.supportsMultipleVersionsAndExactResolution` | 失败 | 失败 |
| `scriptCompiler.metadataIgnoresCommentedLanguageForms` | 失败 | 失败 |
| `scriptCompiler.servesLanguageToolingQueries` | 失败 | 失败 |
| `scriptConfig.ensureModuleLoadsOmittedSlot` | 失败 | 失败 |
| `scriptConfig.instantiatesOnlyConfiguredModules` | 失败 | 失败 |
| `scriptOwnership.procgenAndStateModulesOwnedBindings` | 失败 | 失败 |
| `sensing.ensureRouterRegistersTheCapabilityOnceAndKeepsForeignProviders` | 失败 | 失败 |
| `sensing.routerReportsUnclaimedSpacesInsteadOfAnsweringForThem` | 失败 | 失败 |
| `settlement.replay.rpgLifestealChainReplaysThroughDomainAdapter` | 失败 | 失败 |
| `softbody.cloth.grabMovesParticle` | 失败 | 失败 |
| `sparseVolumeTexture.allocatesOnlyOccupiedBricksAndInjects` | 失败 | 失败 |
| `stylize.action.attack_vfx camera executor routes to optional sink` | 失败 | 失败 |
| `tactics.gridCoverSeparatesInTheWayFromBeside` | 失败 | 失败 |
| `tactics.gridLineOfSightUsesTheBoardsOwnTopology` | — | 失败 |
| `tactics.occupyAndSurviveObjectivesUseBoardAndRoundAuthority` | 失败 | 失败 |
| `tactics.scriptEdgesAreDirectedAndAffectReachability` | 失败 | 失败 |
| `tactics.scriptReachesPresentationAndSightQueries` | 失败 | 失败 |
| `tactics.snapshotJsonRoundTripRefusesTamperingAndForeignDocuments` | 失败 | 失败 |
| `tactics.snapshotRoundTripsEdgesAtTheCurrentVersion` | 失败 | 失败 |
| `tactics.snapshotScriptSurfaceRoundTripsThroughSquirrel` | 失败 | 失败 |
| `tactics.visibleCellsInRangeComposesRangeWithSight` | 失败 | 失败 |
| `thread.postMain.fromWorkerThread` | 失败 | 失败 |
| `ui.control.policies_survive_flatten_and_reconcile` | 失败 | 失败 |
| `ui.dragDrop.retainedPayloadSurvivesFlattenAndReconcile` | 失败 | 失败 |
| `ui.pcgPhotoModeApply.cameraAuthorityProjectsAllPostFxFieldsToFinalComposite` | 失败 | 失败 |
| `ui.pcgPhotoModeApply.compilesStableTypedDomainCommands` | 失败 | 失败 |
| `ui.pcgPhotoModeApply.framePacingAuthorityAppliesVSyncAndRealFrameCap` | 失败 | 失败 |
| `ui.pcgPhotoModeApply.rollsBackInReverseAfterFailure` | 失败 | 失败 |
| `urban.grid.badParams` | 失败 | 失败 |
| `vehicle.orders.attackAimsMount` | 失败 | 失败 |
| `vehicle.orders.moveAndArrive` | 失败 | 失败 |
| `vehicle.orders.turnsTowardTarget` | 失败 | 失败 |
| `virtualgeometry.gpu.lodTransitionSweep` | 失败 | 失败 |
| `virtualgeometry.lod.coverHasNoGapsOrOverlaps` | 失败 | 失败 |
| `virtualgeometry.lod.errorThresholdSelectsMoreDetailCloser` | 失败 | 失败 |
| `virtualgeometry.lod.monotonicCoarsening` | 失败 | 失败 |
| `virtualgeometry.lod.selectionFineNearCoarseFar` | 失败 | 失败 |
| `volumetric.froxelIntegratesEmissiveProxyAndSceneLights` | 失败 | 失败 |
| `volumetric.occlusionFromSceneLights2D` | 失败 | 失败 |
| `volumetric.spotConeScatter2D` | 失败 | 失败 |
| `voxel.greedy.hollow_shell_counts` | 失败 | 失败 |
| `weapon.melee.stageMachine` | 失败 | 失败 |
