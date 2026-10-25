# Short integration contracts and explicit long-running workloads

This pass separates bounded interaction contracts from long-running stability, timing, full-match and interactive-preview workloads. The four existing integration translation units remain ordinary merged CPPs; no Unity source, source-selection macro, shared test main or whole-file runtime bundle was added. Related alternatives share initialization only within a contract parent. Benchmark CPPs belong to the explicit `EVENGINE_BUILD_TEST_BENCHMARKS` lane and are outside the normal integration workload.

Short tests do not provide equivalent coverage of the removed long durations. This document records both retained behavior and the resulting limits. Earlier integration build/runtime measurements must not be presented as measurements of these new changes.

## GPU lighting resource lifetime and bounded visual consumers

Source: [render_presentation_graphics_gpu_driven_tests.cpp](../../../test/integration/render-presentation/render_presentation_graphics_gpu_driven_tests.cpp).

The following original workloads, with their original names and loop thresholds, moved to [graphics_lighting_resource_stability.cpp](../../../test/benchmark/graphics/graphics_lighting_resource_stability.cpp):

- `graphics.deferredLighting.reusesCompletedFrameResources`: 800 consecutive deferred-lighting submissions/presents. The workload deliberately avoids per-frame readback or idle waits; the sustained run targets VMA mapping-reference lifetime and completed-resource reuse.
- `graphics.clusteredLighting.reusesUnchangedStorageBindings`: 1,000 submissions of unchanged clustered-light data and a real mesh, targeting continued storage-binding reuse without exhausting the descriptor pool.

The replacement regular contract, `graphics.lighting.frameSlotResourceReuse`, creates one 64×48 real window/context and runs two named alternatives, each with eight presents. It exercises deferred completion/reuse and unchanged clustered storage submission across the current two/three-slot backends. Initialization assertions execute in the case context, window closure uses assertion-free RAII, and vsync is disabled. The short contract does not claim to cross the old mapping-reference overflow or descriptor-exhaustion thresholds. Existing G-buffer resize, attachment pixel readback and hybrid-lighting integration observations remain separate.

The same CPP contained interactive visual holds:

- `xray.occludedSilhouette`: the 60-frame paced preview becomes eight submissions after its existing warm-up. G-buffer validity/depth observations remain, with fatal assertions. This is a bounded pass smoke, not an automated proof that a human-visible silhouette has the intended appearance.
- `animation.spine.windowDemoRealModels`: 180 paced animation frames become eight fixed `1/60` updates, plus one `1/60` update for the retained readback frame: nine steps, approximately 0.15 simulated seconds. Every model must still emit draw items, exercise rotated attachments, produce non-background pixels near its target and remain within the original 40-pixel centering tolerance. Thresholds were not widened.
- `dialogue.stage.avatarTypewriterPreview`: 150 paced frames become eight `0.016` updates, 0.128 simulated seconds. The fixture raises its authored typewriter speed and selects the presented choice promptly instead of waiting until frame 110. The real say/advance/choice/idle, avatar sync and UI remount/render path remains; the case requires a choice to have been observed and dialogue to end idle. This does not retain a real-time slow-typewriter presentation demonstration.
- `graphics.drawText.rendersGlyphPixelsOnCanvas`: the optional 60-frame windowed viewing loop is removed. The real canvas glyph-pixel and font-state assertions remain fatal.

Artificial `SDL_Delay` pacing was removed from these preview paths and their warm-up helper. No large simulation dt substitutes for the deleted wall-clock waits. Other small domain updates in this CPP retain their distinct mesh/material/pose/readback observations.

### Spine false-green fixture correction

The old `measurePoseCenter` helper intentionally sets player position to `(0,0)` to measure local-space quads. The old final check called this helper and then read `getX/getY`, which consequently returned zero. It compared local pose bounds with the intended screen position. These checks used warning-level `CHECK`, so successful test status did not establish centering.

The case now saves the actual rendered translation before calling that helper, restores the translation afterward, and calculates screen center as saved translation plus local pose center. The original 40-pixel bounds, rotation and pixel oracles remain. This corrects fixture observation; it is not a production animation change or a justification to restore a long preview. Runtime confirmation of the corrected short case is pending in this document.

## Card/RTS script composition without a full match

Source: [card_rts_payment_card_tests.cpp](../../../test/integration/card-rts-payment/card_rts_payment_card_tests.cpp).

The original `card.rtsScriptScenarioCollectsBuildsProducesAndPlaysAtomicSkills` moved intact to [card_rts_script_scenario_benchmark.cpp](../../../test/benchmark/card-rts/card_rts_script_scenario_benchmark.cpp). Its original full-match work remains: collection up to 1,500 steps, construction up to 600, production 100, further collection up to 600, resource accumulation up to 900, reinforcement 100, combat/skill attempts up to 1,200, victory up to 3,000 and ten reset steps, all at `1/30` dt. Conditional stages can finish early. This lane retains sustained travel/economy/combat/victory and reset coverage.

The regular `card.rtsScriptShortCollectionConstructionAndAtomicPlayContract` still executes the actual example orchestration and real RTS/card/transaction bindings. Its in-memory fixture narrows population and travel, accelerates configured gathering/build/cast rates, and shortens configured production durations. Replacement helpers assert expected match counts rather than silently accepting a changed example. The repository's example resources are not rewritten.

The short contract uses `1/30` dt and a global maximum of 90 steps, at most three simulated seconds. It observes collected resources, completed construction, produced units, rejected play with no payment/card mutation, successful atomic payment/skill consumption, reinforcement and reset. It does not claim the original population, economy pacing, combat duration or victory coverage. A separate configured-resource/dropoff contract allows at most 24 `1/30` steps, 0.8 simulated seconds, and verifies the correct resource receives income without crediting the unrelated resource.

## Layered-cloth correctness versus stability and timing

Source: [physics_layered_cloth_gpu.cpp](../../../test/integration/layered-cloth/physics_layered_cloth_gpu.cpp).

Two original names moved to [physics_layered_cloth_benchmark.cpp](../../../test/benchmark/physics/physics_layered_cloth_benchmark.cpp):

- `PhysicsLayeredClothGPU.stabilityAndSynchronousTimingBaseline`: 124 updates at `1/60`, retaining four warm-up updates, 120 timed samples, CSV output, median/p95/max reporting, finite positions and pinned-particle observations.
- `PhysicsLayeredClothGPU.fallingSheetsRemainSeparatedForOneSimulatedMinute`: 1,800 updates at `1/30`, retaining the 60-second simulated duration, periodic finite/floor/geometric-layer-separation observations and post-reset update.

The integration suite retains bounded real GPU/reference comparisons, pinned/reset behavior, mass-dependent contacts and disabled-contact behavior, explicit hash-overflow fallback, renderable compute-generated vertices, swept point/face and edge CCD, friction/filtering, bounded dense corrections and folded-sheet separation with centre conservation. These have direct position, diagnostic, mass-balance, geometry or rendering observations rather than a timing printout alone.

For example, free-fall comparison runs eight `0.01` steps, 0.08 simulated seconds, and pinned/reset runs twelve `1/60` steps, 0.2 seconds. Contact probes directly arrange the critical crossing/folded geometry instead of waiting for a long simulation to reach it. The short contracts do not establish one-minute stability or the timing distribution. GPU initialization helpers receive the real `TestContext`, so setup failure is not detached from the test result.

## Window lifecycle instead of repeated presentation holds

Source: [render_presentation_graphics_window_cpp_tests.cpp](../../../test/integration/render-presentation/render_presentation_graphics_window_cpp_tests.cpp).

`window.lifecycle.desktopEventContract` shares one initialized window/context across named minimize/restore, hidden/shown and focus observations. Synthetic SDL events exercise the actual lifecycle event sink. It requires inactive presentation after minimize/hide, successful resume after restore/show, and continued activity when focus alone changes. Resume verification performs an actual clear/present/readback, checks image dimensions and expected channel ordering, rather than merely presenting several frames without checking output.

`graphics.lifecycle.appBackgroundForegroundResume` retains two complete background/foreground cycles with the same resumed-frame observations. Native maximize/restore remains a separate window-manager-dependent observation; each transition pumps events with a bounded 200 ms deadline and 1 ms polling delay. A window manager that ignores maximize retains the existing early-return behavior, so that branch is not proof that native maximize occurred.

Fixed 50 ms lifecycle sleeps and repeated unchecked clear-frame holds were removed. The synthetic event contract is deterministic but is not equivalent to exercising every host window manager's native iconification behavior. Window setup/event/readback helpers receive the case's `TestContext`. No long-duration benchmark was invented for removed viewing delays: those delays were not independently asserted stability workloads.

## Verification

Native Windows MSVC Debug SHARED builds passed with benchmark compilation both ON and OFF. The OFF configuration was restored and its actual CTest inventory excludes all five migrated identities. With benchmarks ON, all five original long cases passed when explicitly selected; their combined four-concurrency wall time was 36.23 seconds. This is opt-in workload time, not routine integration time.

The four changed integration sources contain 93 active cases: 82 passed and the same 11 pre-existing GPU cases failed. The Spine case passed after correcting its local-position measurement. No new failure was introduced. Runs used the new [background desktop launcher](integration-hidden-windows.md); visibility and actual pixel assertions remained enabled. The focused 23.08-second wall time overlapped source checks and is diagnostic, not a clean performance sample.

`make check CI_BASE=20394d720951b2aa13f035dd9b16104c3de3096e` passed, including 527 script tests (three skips), source formatting and architecture gates. The explicit base is the existing optimization branch baseline; newer `dev` has independent test-policy changes and the PR still needs conflict resolution. This pass changes test fixtures, classification and launch tooling, without a production API, ECS, persistent format or module boundary change. No architecture-rule exception was taken.

The [complete 138-source four-concurrency measurement](integration-short-runtime.md), run after source checks finished, took 282.118 seconds for 3,453 cases: 3,414 passed, 24 failed, 15 skipped. Nonpassing identities match the preceding complete sweep exactly. The observed 17.945-second reduction also includes the change from visible windows to a private desktop; it is not an isolated source-change speedup. Do not infer the old 800/1,000-submission or one-minute cloth outcomes from passing short tests, and do not count unavailable assets or early returns as exercised rendering coverage.
