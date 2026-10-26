// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "default": "runtime-lifecycle",
  "cases": {
    "script-binding": [
      "runtime.reflectsAndUnloadsScriptClasses",
      "runtimeWrapsCompileErrorsAndRestoresStack",
      "runtimeRuntimeErrorCarriesLocationAndStack",
      "runtimeCaughtErrorExposesThrowSiteViaLastScriptError",
      "runtimeRuntimeErrorMarksScriptFailedAndRestoresStack",
      "runtimeCompileErrorCarriesLineColumnAndSnippet",
      "devtools.runtimeErrorSink.reportsCompileError",
      "devtools.runtimeErrorSink.noDoubleReportOnRuntimeError",
      "rx.script.subject.onNext",
      "rx.script.subject.operators",
      "rx.script.map",
      "rx.script.behaviorSubject",
      "rx.script.take",
      "rx.script.replaySubject",
      "rx.script.fromEvent.pump",
      "devtools.scenario.recordPersistLoadRoundTrip",
      "devtools.scenario.replayReproducesStateDrivenError",
      "Scene.script.entityLifecycle",
      "Scene.script.entityViewCacheAfterDetach",
      "Scene.script.entityEnableAndDelayedDetach",
      "Scene.script.linkPhysics2D",
      "Scene.script.apiCompleteness",
      "Scene.script.nodeEvents",
      "Scene.script.componentOnMount",
      "Scene.script.errorSemantics",
      "Scene.script.serializeAndPick",
      "scene.authoring.script_owns_document_and_rejects_bad_bounds",
      "scene.authoring.director_compiles_and_missing_native_provider_is_observable",
      "Scene.script.visibilityArchitectureMapping",
      "scene.editor.script_session_owns_target_and_projects_transaction_results",
      "scene.editor.script_physics_placement_previews_and_commits",
      "Scene.script.firstObjectLookupAndDelayedDetach",
      "schema.module.scriptBinding",
      "schema.module.reflectFromClass",
      "scriptCompiler.recordsErasedLanguageMetadata",
      "scriptCompiler.mapsDebuggerLocationsBidirectionally",
      "scriptCompiler.generatedModuleContractIsExecutable",
      "scriptCompiler.bindingContractEnablesNativeNamedArguments",
      "scriptCompiler.nestedBindingCallsKeepOuterSignatureStable",
      "scriptCompiler.generatedContractsDriveSimpleSquirrelNamedArguments",
      "scriptCompiler.bindingContractConvertsUnitLiterals",
      "scriptCompiler.bindingContractChecksStringChoices",
      "scriptCompiler.retainsStructuredDiagnosticsAfterFailure",
      "scriptCompiler.requiresPluginAnnotationsToBeRegistered",
      "scriptCompiler.servesLanguageToolingQueries",
      "scriptCompiler.bindingContractChecksLiteralTypes",
      "scriptCompiler.bindingContractScopesLiteralChecksToWholeArgument",
      "scriptCompiler.compilesRepositoryNutCompatibilityBaseline",
      "scriptConfig.acceptsAbsentModuleRequirements",
      "scriptConfig.rejectsMissingRequiredModule",
      "scriptConfig.validatesOptionalModuleDeclarationShape",
      "scriptConfig.instantiatesOnlyConfiguredModules",
      "scriptConfig.ensureModuleLoadsOmittedSlot",
      "scriptModule.defaultProviderUsesPlatformVirtualFileSystem",
      "scriptModule.resolvesCachesAndInstantiatesDependencyGraph",
      "scriptModule.rejectsCyclesBeforeExecution",
      "scriptModule.reloadReplacesGenerationAtomically",
      "scriptModule.reloadAffectedRebuildsReverseDependencyGenerations",
      "scriptModule.reloadAffectedRollsBackWholeGenerationGroup",
      "scriptModule.dofileInstantiatesImports",
      "property_access.reflection_builds_shared_schema_and_structured_values",
      "property_access.writes_and_refreshes_through_shared_mvvm_contract",
      "property_access.script_validation_matches_shared_contract",
      "scriptOwnership.ordersAndEffectsOwnedBinding",
      "scriptOwnership.procgenAndStateModulesOwnedBindings",
      "scriptOwnership.procgenOwnedGridExposesAssetPlacement",
      "userDocs.economy",
      "userDocs.attributes",
      "userDocs.decision",
      "userDocs.definitions",
      "userDocs.tags",
      "userDocs.gameEvent",
      "userDocs.policyRegistry",
      "userDocs.transaction",
      "userDocs.profiler",
      "userDocs.rope",
      "userDocs.ecsLifecycle",
      "userDocs.tutorialRules",
      "userDocs.tutorialLifecycleAndSaveFailure",
      "sensing.worldHandleAndScriptResultContract",
      "settlementScript.runtimeOwnsRulesAndLedger",
      "settlementScript.rejectsUnknownKindAndNormalizesSubjectCase",
      "SimpleSquirrelTest.ExportClass",
      "SimpleSquirrelTest.RefTest",
      "SimpleSquirrelTest.TestDefClass",
      "SimpleSquirrelTest.PerformInteration",
      "SimpleSquirrelTest.GetterTest",
      "SimpleSquirrelTest.GetAttr",
      "SimpleSquirrel.DirectMemberConversionsAndLifetime",
      "social.script.bindings",
      "squirrel_binding.value_round_trip_preserves_nested_value",
      "squirrel_binding.value_rejects_non_finite_numbers_with_path_and_source",
      "squirrel_binding.value_enforces_depth_limit",
      "squirrel_binding.result_projection_and_explicit_ignore_are_uniform",
      "statepatch.script.batchConflictQueryAndSnapshot",
      "tactics.graphScriptMovementSnapshotAndStaleRelease",
      "tactics.scriptInteractionSessionTurnsClicksIntoIntents",
      "tactics.scriptReachesPresentationAndSightQueries",
      "tactics.scriptOwnedBattleRunsDeterministicTurn",
      "tactics.scriptReleaseMakesProxyStale",
      "tactics.scriptReactionWindowOpensAcceptsAndDeclines",
      "tactics.scriptEndTurnAndFinishAreReachableWithoutWait",
      "tactics.scriptPreviewReachabilityAndResourcesMatchCommit",
      "tactics.scriptEdgesAreDirectedAndAffectReachability",
      "tactics.scriptAbilityDeclarationIsCostedAndLogged",
      "tactics.scriptCommandLogIsReadableAndRevisionScoped",
      "tactics.snapshotScriptSurfaceRoundTripsThroughSquirrel",
      "tags.script.bindings",
      "rx.script.throwingSubscriberIsReported",
      "Scene.script.attachRejectsPlainEntity",
      "Scene.script.callbackFailureIsNotSilent",
      "Scene.script.entityIsTheFirstEcsName",
      "Scene.script.nativeAccessReadsRegisteredComponentState",
      "Scene.script.nativeAccessSkipsUnregisteredComponents",
      "Scene.script.updateFailureIsNotSilent",
      "scriptModule.instantiationFailureCarriesTheScriptError",
      "map.collision.scriptOwnsTheWholeCollisionLoop",
      "map.collision.scriptSinkCanTellLayersApart",
      "map.collision.scriptSinkFailureIsCountedNotSwallowed",
      "map.collision.scriptSinkReceivesLayerIdentityAndGeometry",
      "map.collision.scriptSinkSeamIsAbsentWithoutTheModule",
      "animation.motion.badValueTablesAreRejectedNotZeroFilled",
      "animation.motion.nonFloatShapesReachScriptSinksToo",
      "animation.motion.scriptSinkFailureCancelsInsteadOfBeingSwallowed",
      "animation.motion.scriptSinkReceivesEveryEvaluatedValue",
      "animation.motion.sinkIsOptionalAndOnlyBindsWhenAsked",
      "scene.scriptLink.absentInputsAreReported",
      "scene.scriptLink.brokenTargetDoesNotStallSiblings",
      "scene.scriptLink.pullDrivesNode",
      "scene.scriptLink.pushDrivesFollower",
      "scene.scriptLink.rejectedPullKeepsNodeIntact",
      "scene.scriptLink.retiredProviderIsDropped",
      "scene.scriptLink.scriptBindingReportsResult",
      "sensing.script.candidateAnswerMustStayInTheQuerySpace",
      "sensing.script.candidateProviderAnswersTheResolver",
      "sensing.script.candidateProviderFailureIsReportedNotSwallowed",
      "sensing.script.candidateProviderSeamIsAbsentWithoutTheModule",
      "sensing.script.coordinateSpaceSpellingMatchesTheModuleName",
      "sensing.script.malformedCandidateAnswersAreRejected"
    ],
    "render-window": [
      "Scene.render.parentChildOrbitPreview"
    ],
    "physics-backend": [
      "Scene.link.genericMultiLinkAndSync",
      "Scene.link.physics2D",
      "Scene.link.body3DPullRoundTripsEuler"
    ],
    "module-composition": [
      "Scene.link.syncRenderable3DWorldRotation"
    ]
  },
  "module": "common",
  "modules": [
    "animation",
    "authority",
    "common",
    "devtools",
    "map",
    "property_access",
    "rx",
    "scene",
    "scene_editor",
    "schema",
    "sensing",
    "settlement",
    "social",
    "statepatch",
    "tactics",
    "tags"
  ],
  "origins": [
    "runtime.cpp",
    "runtime_error_sink.cpp",
    "rx.cpp",
    "scenario_recorder.cpp",
    "scene.cpp",
    "scene_authoring_script.cpp",
    "scene_binding_architecture.cpp",
    "scene_editor_script.cpp",
    "scene_object_lifetime.cpp",
    "schema.cpp",
    "script_compiler.cpp",
    "script_config.cpp",
    "script_module.cpp",
    "script_ownership.cpp",
    "script_user_doc_examples.cpp",
    "sensing.cpp",
    "settlement_script.cpp",
    "simplesquirrel.cpp",
    "simplesquirrel_direct_member.cpp",
    "social.cpp",
    "squirrel_binding.cpp",
    "statepatch.cpp",
    "tactics_graph.cpp",
    "tactics_presentation_script.cpp",
    "tactics_script.cpp",
    "tactics_snapshot_script.cpp",
    "tags.cpp",
    "map_tile_collision_sink.cpp",
    "motion_script_sink.cpp",
    "scene_script_link.cpp",
    "sensing_script_provider.cpp"
  ]
}
*/
// clang-format on

#include "zeroerr/assert.h"

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_runtime)
#include "PrivateTestScope.h"
#include <algorithm>
#include "common/Runtime.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// runtime.cpp
namespace test_runtime {
using namespace eve;

TEST_CASE("runtime.scopeAndNativeStackAreRestored") {
    Runtime runtime(256, ssq::Libs::ALL);
    CHECK(Runtime::current() == nullptr);
    const SQInteger top = runtime.vm().getTop();
    {
        auto scope = runtime.enter();
        CHECK(Runtime::current() == &runtime);
        CHECK_EQ(Runtime::stackDepth(), size_t(1));
        {
            auto guard = runtime.guard();
            sq_pushinteger(runtime.handle(), 42);
            CHECK_EQ(runtime.vm().getTop(), top + 1);
        }
        CHECK_EQ(runtime.vm().getTop(), top);
    }
    CHECK(Runtime::current() == nullptr);
}

TEST_CASE("runtime.reflectsAndUnloadsScriptClasses") {
    Runtime     runtime(512, ssq::Libs::ALL);
    const char* source = R"SQ(
class RuntimeProbe {
    </ editor = "slider", min = 0 />
    speed = 3
    function update(dt) { speed += dt }
}
)SQ";

    const Runtime::ScriptId id     = runtime.runSource(source, "runtime-probe.nut");
    const ScriptInfo*       script = runtime.script(id);
    CHECK(script != nullptr);
    // zeroerr's CHECK cannot print the ScriptState enum; compare via int.
    CHECK(static_cast<int>(script->state) == static_cast<int>(ScriptState::Loaded));
    CHECK(std::find(script->classes.begin(), script->classes.end(), "RuntimeProbe") != script->classes.end());

    const ReflectedClass* cls = runtime.reflectedClass("RuntimeProbe");
    CHECK(cls != nullptr);
    CHECK_EQ(cls->source, std::string("runtime-probe.nut"));
    auto speed = std::find_if(cls->members.begin(), cls->members.end(),
                              [](const ReflectedMember& member) { return member.name == "speed"; });
    CHECK(speed != cls->members.end());
    CHECK(!speed->method);
    CHECK(std::find_if(speed->attributes.begin(), speed->attributes.end(), [](const ReflectedAttribute& attribute) {
              return attribute.name == "editor" && attribute.value == "slider";
          }) != speed->attributes.end());

    CHECK(runtime.unload(id));
    CHECK(runtime.reflectedClass("RuntimeProbe") == nullptr);
    bool removed = false;
    try {
        runtime.findClass("RuntimeProbe");
    } catch (const ssq::NotFoundException&) {
        removed = true;
    }
    CHECK(removed);
}

TEST_CASE("runtimeWrapsCompileErrorsAndRestoresStack") {
    Runtime         runtime(256, ssq::Libs::ALL);
    const SQInteger top    = runtime.vm().getTop();
    bool            caught = false;
    try {
        runtime.compileSource("class Broken {", "broken.nut");
    } catch (const ScriptException& error) {
        caught = true;
        // zeroerr's CHECK_EQ cannot print the ScriptStage enum; compare via int.
        CHECK_EQ(static_cast<int>(error.stage()), static_cast<int>(ScriptStage::Compile));
        CHECK_EQ(error.source(), std::string("broken.nut"));
    }
    CHECK(caught);
    CHECK_EQ(runtime.vm().getTop(), top);
}

TEST_CASE("runtimeRuntimeErrorCarriesLocationAndStack") {
    Runtime runtime(256, ssq::Libs::ALL);
    bool    caught = false;
    try {
        runtime.runSource(R"SQ(
function boom() { throw "kaboom" }
function outer() { boom() }
outer();
)SQ",
                          "boom.nut");
    } catch (const ScriptException& error) {
        caught = true;
        CHECK_EQ(static_cast<int>(error.stage()), static_cast<int>(ScriptStage::Execute));
        CHECK_EQ(error.source(), std::string("boom.nut"));
        CHECK(error.hasLocation());
        CHECK(error.line() > 0);
        CHECK(!error.function().empty());
        const std::string message = error.what();
        CHECK(message.find("kaboom") != std::string::npos);
        CHECK(message.find("boom.nut:") != std::string::npos);
        CHECK(message.find("Stack:") != std::string::npos);
        CHECK(!error.stackTrace().empty());
        CHECK(error.stackTrace().find("boom") != std::string::npos);
        CHECK(error.stackTrace().find("outer") != std::string::npos);
    }
    CHECK(caught);
}

TEST_CASE("runtimeCaughtErrorExposesThrowSiteViaLastScriptError") {
    Runtime runtime(256, ssq::Libs::ALL);
    runtime.initialize();
    runtime.runSource(R"SQ(
function boom() { throw "kaboom" }
function outer() { boom() }
try { outer() } catch (e) {
    caught_stack <- eve.lastScriptError()
}
)SQ",
                      "caught.nut");
    const std::string report = runtime.vm().find("caught_stack").toString();
    CHECK(report.find("kaboom") != std::string::npos);
    CHECK(report.find("caught.nut") != std::string::npos);
    CHECK(report.find("Stack:") != std::string::npos);
    CHECK(report.find("boom") != std::string::npos);
}

TEST_CASE("runtimeRuntimeErrorMarksScriptFailedAndRestoresStack") {
    Runtime                 runtime(256, ssq::Libs::ALL);
    const SQInteger         top    = runtime.vm().getTop();
    const Runtime::ScriptId id     = runtime.compileSource("function bad() { throw \"nope\" }\nbad();\n", "fail.nut");
    bool                    caught = false;
    try {
        runtime.execute(id);
    } catch (const ScriptException&) {
        caught = true;
    }
    CHECK(caught);
    const ScriptInfo* script = runtime.script(id);
    CHECK(script != nullptr);
    CHECK(static_cast<int>(script->state) == static_cast<int>(ScriptState::Failed));
    CHECK(!script->error.empty());
    CHECK(script->error.find("fail.nut") != std::string::npos);
    CHECK_EQ(runtime.vm().getTop(), top);
}

TEST_CASE("runtimeCompileErrorCarriesLineColumnAndSnippet") {
    Runtime runtime(256, ssq::Libs::ALL);
    bool    caught = false;
    try {
        runtime.compileSource("local x = ;\nlocal y = 1;\n", "broken.nut");
    } catch (const ScriptException& error) {
        caught = true;
        CHECK_EQ(static_cast<int>(error.stage()), static_cast<int>(ScriptStage::Compile));
        CHECK(error.hasLocation());
        CHECK(error.line() >= 1);
        const std::string message = error.what();
        CHECK(message.find("local x = ;") != std::string::npos);
        CHECK(message.find("broken.nut:") != std::string::npos);
    }
    CHECK(caught);
}
}  // namespace test_runtime
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_runtime_error_sink)
#include "PrivateTestScope.h"
#include "common/Runtime.h"
#include "devtools/DevTool.hpp"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// runtime_error_sink.cpp
namespace test_runtime_error_sink {
using namespace eve;

// The Runtime error-handler sink wired by DevTool::attach(Runtime&) routes
// errors that the uncaught-error VM hook never sees (compile / reflect /
// unload) into the DevTool slicer/report, without double-reporting the
// uncaught runtime errors the hook already handled.

TEST_CASE("devtools.runtimeErrorSink.reportsCompileError") {
    dev::DevTool::instance().detach();
    Runtime runtime(256, ssq::Libs::ALL);
    runtime.initialize();
    dev::DevTool::instance().attach(runtime, /*sampleLocals=*/true);

    bool caught = false;
    try {
        runtime.runSource("class Broken {", "broken.nut");
    } catch (const ScriptException& error) {
        caught = true;
        CHECK_EQ(static_cast<int>(error.stage()), static_cast<int>(ScriptStage::Compile));
        CHECK_EQ(error.source(), std::string("broken.nut"));
    }
    CHECK(caught);

    // The Runtime error-handler sink must have routed the compile error into the
    // DevTool report even though the VM error hook never fires for a compile
    // failure.
    const std::string report = dev::DevTool::instance().lastReport();
    CHECK(!report.empty());
    CHECK(report.find("broken.nut") != std::string::npos);

    dev::DevTool::instance().detach();
}

TEST_CASE("devtools.runtimeErrorSink.noDoubleReportOnRuntimeError") {
    dev::DevTool::instance().detach();
    Runtime runtime(256, ssq::Libs::ALL);
    runtime.initialize();
    dev::DevTool::instance().attach(runtime, /*sampleLocals=*/true);

    bool caught = false;
    try {
        runtime.runSource(R"SQ(
function boom() { throw "kaboom" }
boom();
)SQ",
                          "boom.nut");
    } catch (const ScriptException& error) {
        caught = true;
        // The VM hook only captures the live stack when Runtime is bound;
        // the Runtime error sink reports it once after unwind.
        CHECK(!error.reported());
        CHECK(error.hasLocation());
        CHECK(!error.stackTrace().empty());
    }
    CHECK(caught);

    // Reported exactly once through the DevTool report pipeline.
    const std::string report = dev::DevTool::instance().lastReport();
    CHECK(!report.empty());
    CHECK(report.find("kaboom") != std::string::npos);

    dev::DevTool::instance().detach();
}
}  // namespace test_runtime_error_sink
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_rx)
#include <memory>
#include <simplesquirrel/simplesquirrel.hpp>
#include <string>
#include <vector>
#include "PrivateTestScope.h"
#include "common/Module.h"
#include "common/SquirrelBinding.h"
#include "platform_event/PlatformEvent.h"
#include "rx/Rx.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// rx.cpp
namespace test_rx {
using eve::rx::AnonymousObservable;
using eve::rx::BehaviorSubject;
using eve::rx::Observable;
using eve::rx::Observer;
using eve::rx::ReactiveProperty;
using eve::rx::ReplaySubject;
using eve::rx::Subject;
using eve::rx::Subscription;
using eve::rx::Value;

// ---------------------------------------------------------------------------
// Value basics
// ---------------------------------------------------------------------------


// ---------------------------------------------------------------------------
// Subject core
// ---------------------------------------------------------------------------


// ---------------------------------------------------------------------------
// Operators
// ---------------------------------------------------------------------------


// ---------------------------------------------------------------------------
// BehaviorSubject / ReplaySubject / ReactiveProperty
// ---------------------------------------------------------------------------


// ---------------------------------------------------------------------------
// Script binding (Squirrel)
// ---------------------------------------------------------------------------
namespace {

[[maybe_unused]] std::string runRxSnippet(const std::string& body) {
    ssq::VM vm(1024, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    std::string prelude = R"(
        rx <- eve.Rx();
    )";
    vm.run(vm.compileSource((prelude + "\n" + body).c_str()));
    try {
        return vm.find("result").toString();
    } catch (...) {
        return {};
    }
}

}  // namespace

TEST_CASE("rx.script.subject.onNext") {
    std::string out = runRxSnippet(R"(
        result <- "none";
        local s = rx.newSubject();
        s.subscribe(function(v) { result = "got:" + v; });
        s.onNext(42);
    )");
    CHECK_EQ(out, std::string("got:42"));
}

TEST_CASE("rx.script.subject.operators") {
    std::string out = runRxSnippet(R"(
        result <- "none";
        local s = rx.newSubject();
        local even = s.filter(function(v) { return v % 2 == 0; });
        even.subscribe(function(v) { result = "even:" + v; });
        s.onNext(1);
        s.onNext(2);
        s.onNext(4);
    )");
    CHECK_EQ(out, std::string("even:4"));
}

TEST_CASE("rx.script.map") {
    std::string out = runRxSnippet(R"(
        result <- "none";
        local s = rx.newSubject();
        local doubled = s.map(function(v) { return v * 2; });
        doubled.subscribe(function(v) { result = "d:" + v; });
        s.onNext(21);
    )");
    CHECK_EQ(out, std::string("d:42"));
}

TEST_CASE("rx.script.behaviorSubject") {
    std::string out = runRxSnippet(R"(
        result <- "none";
        local bs = rx.newBehaviorSubject(5);
        bs.subscribe(function(v) { result = "v:" + v; });
        bs.setValue(9);
    )");
    CHECK_EQ(out, std::string("v:9"));
}

TEST_CASE("rx.script.take") {
    std::string out = runRxSnippet(R"(
        result <- "";
        local s = rx.newSubject();
        s.take(2).subscribe(function(v) { result += v; });
        s.onNext(1);
        s.onNext(2);
        s.onNext(3);
    )");
    CHECK_EQ(out, std::string("12"));
}

TEST_CASE("rx.script.replaySubject") {
    std::string out = runRxSnippet(R"(
        result <- "";
        local rs = rx.newReplaySubject(2);
        rs.onNext(1);
        rs.onNext(2);
        rs.onNext(3);
        rs.subscribe(function(v) { result += v; });
    )");
    CHECK_EQ(out, std::string("23"));
}

TEST_CASE("rx.script.fromEvent.pump") {
    std::string out = runRxSnippet(R"(
        result <- "";
        local ev = eve.PlatformEvent();
        local ev2 = eve.PlatformEvent();
        local stream = rx.fromEvent("quest");
        stream.subscribe(function(v) { result += v; });
        ev.pushData("quest", "A");
        ev.pushData("other", "X");
        ev2.pushData("quest", "B");
        rx.pump(ev);
        rx.pump(ev2);
    )");
    CHECK_EQ(out, std::string("AB"));
}

TEST_CASE("rx.script.throwingSubscriberIsReported") {
    eve::script::resetReportedScriptCallFailures();
    runRxSnippet(R"(
        local s = rx.newSubject();
        s.subscribe(function(v) { throw "subscriber exploded"; });
        s.onNext(1);
    )");
    // The subscriber runs from native code. Before rx routed it through the shared
    // reporter, a subscriber that threw was indistinguishable from one that returned
    // nothing: the stream just stopped producing, with nothing on stderr.
    REQUIRE(eve::script::reportedScriptCallFailures() >= 1);
}
}  // namespace test_rx
#endif
#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_scenario_recorder)
#include <filesystem>
#include <string>
#include "PrivateTestScope.h"
#include "common/Module.h"
#include "common/Runtime.h"
#include "devtools/DevTool.hpp"
#include "devtools/ScenarioRecorder.h"
#include "platform_event/PlatformEvent.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// scenario_recorder.cpp
namespace test_scenario_recorder {
using namespace eve;

namespace {

const char* kTapScript = R"SQ(
hits <- 0;
function consumeTap(name) {
    if (name == "tap") {
        hits++;
        if (hits >= 3) throw "reproduced";
    }
    return hits;
}
)SQ";

// Polls every pending event and dispatches "tap" into consumeTap, catching the
// thrown error. Mirrors how the game loop consumes the event queue per frame.
bool drainTaps(ssq::VM& vm, bool& threw) {
    auto* ev = eve::ModuleManager::requireInstance<eve::platform_event::PlatformEvent>("PlatformEvent");
    if (!ev) return false;
    while (auto msg = ev->pollOwned()) {
        if (msg->name == "tap") {
            try {
                vm.callFunc(vm.findFunc("consumeTap"), vm, std::string("tap"));
            } catch (const std::exception&) {
                threw = true;
            }
        }
    }
    return true;
}

}  // namespace

TEST_CASE("devtools.scenario.recordPersistLoadRoundTrip") {
    dev::DevTool::instance().detach();
    dev::ScenarioRecorder::instance().cancel();

    Runtime rt(256, ssq::Libs::ALL);
    rt.runSource(kTapScript, "taps.nut");
    auto* ev = eve::ModuleManager::requireInstance<eve::platform_event::PlatformEvent>("PlatformEvent");
    ev->clear();

    auto& rec = dev::ScenarioRecorder::instance();
    CHECK(rec.begin(rt.handle()));
    CHECK(rec.recording());
    CHECK(!rec.baseline().empty());

    // Two frames; three "tap" messages consumed across them.
    rec.markFrame();
    ev->pushData("tap", "a");
    ev->pollOwned();
    rec.markFrame();
    ev->pushData("tap", "b");
    ev->pollOwned();
    ev->pushData("tap", "c");
    ev->pollOwned();

    const std::string path = "scenario_roundtrip_test.json";
    REQUIRE(rec.end(path));
    CHECK(!rec.recording());

    // Reload the persisted scenario independently of the recorder's memory.
    std::string                     baseline;
    std::vector<dev::ScenarioFrame> frames;
    std::string                     er, es;
    CHECK(dev::ScenarioRecorder::load(path, &baseline, &frames, &er, &es));
    CHECK(!baseline.empty());
    int taps = 0;
    for (const auto& f : frames)
        for (const auto& e : f.events)
            if (e.name == "tap") ++taps;
    CHECK_EQ(taps, 3);
    CHECK_EQ(static_cast<int>(frames.size()), 2);

    dev::ScenarioRecorder::instance().cancel();
    dev::DevTool::instance().detach();
}

TEST_CASE("devtools.scenario.replayReproducesStateDrivenError") {
    dev::DevTool::instance().detach();
    dev::ScenarioRecorder::instance().cancel();

    // --- Record the failing run ---
    Runtime rt(256, ssq::Libs::ALL);
    rt.runSource(kTapScript, "taps.nut");
    auto* ev = eve::ModuleManager::requireInstance<eve::platform_event::PlatformEvent>("PlatformEvent");
    ev->clear();

    auto& rec = dev::ScenarioRecorder::instance();
    CHECK(rec.begin(rt.handle()));  // baseline: hits = 0
    rec.markFrame();
    ev->pushData("tap", "a");
    ev->pollOwned();
    rec.markFrame();
    ev->pushData("tap", "b");
    ev->pollOwned();
    ev->pushData("tap", "c");
    ev->pollOwned();

    const std::string path = "scenario_repro_test.json";
    REQUIRE(rec.end(path));
    dev::DevTool::instance().detach();

    // --- Replay on a fresh VM: restore baseline, re-stage the recorded taps ---
    Runtime rt2(256, ssq::Libs::ALL);
    rt2.runSource(kTapScript, "taps.nut");
    REQUIRE(rec.beginReplay(rt2.handle(), path));

    // Baseline restored: hits is back to 0 before any staged input.
    const int64_t before = rt2.vm().find("hits").toInt();
    REQUIRE_EQ(before, 0);

    bool reproduced = false;
    while (rec.framesRemaining() > 0) {
        REQUIRE(rec.stageFrame());
        REQUIRE(drainTaps(rt2.vm(), reproduced));
    }
    // All 3 taps replayed from the clean baseline -> hits reaches 3 -> throws.
    REQUIRE(reproduced);

    dev::ScenarioRecorder::instance().cancel();
    dev::DevTool::instance().detach();
    // The repro file is written next to the test process on purpose (a developer can open
    // it), but the case must not leave it in the checkout: it used to show up in
    // `git status` after any local test run.
    std::error_code removed;
    std::filesystem::remove(path, removed);
}
}  // namespace test_scenario_recorder
#endif
#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_scene)
#include <SDL2/SDL.h>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <simplesquirrel/simplesquirrel.hpp>
#include <string>
#include <utility>
#include "PrivateTestScope.h"
#include "common/Capability.h"
#include "common/ECS.h"
#include "common/EntitySpatialResolver.h"
#include "common/Module.h"
#include "common/ProcgenSceneSink.h"
#include "common/ScriptComponentSnapshot.h"
#include "common/SquirrelBinding.h"
#include "graphics/AmbientOcclusion.h"
#include "graphics/AntiAliasing.h"
#include "graphics/Canvas.h"
#include "graphics/DrawItem2D.h"
#include "graphics/Font.h"
#include "graphics/GBuffer.h"
#include "graphics/GlobalIllumination.h"
#include "graphics/Graphics.h"
#include "graphics/Grass.h"
#include "graphics/Light.h"
#include "graphics/Material.h"
#include "graphics/Mesh.h"
#include "graphics/Outline.h"
#include "graphics/Quad.h"
#include "graphics/RenderControl.h"
#include "graphics/RenderSystem.h"
#include "graphics/RenderSystem3D.h"
#include "graphics/ScreenSpaceReflection.h"
#include "graphics/Shader.h"
#include "graphics/Texture.h"
#include "graphics/Volumetric.h"
#include "graphics/Water.h"
#include "graphics/Waterfall.h"
#include "physics/Body.h"
#include "physics/Body3D.h"
#include "physics/World.h"
#include "physics/World3D.h"
#include "scene/NodeDesc.h"
#include "scene/Scene.h"
#include "scene/SceneCameraProjection.h"
#include "scene/SceneComponent.h"
#include "scene/SceneHost.h"
#include "scene/SceneNodeRef.h"
#include "scene/SceneObject.h"
#include "scene/TransformSystem.h"
#include "spatial/Octree.h"
#include "window/Window.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// scene.cpp
namespace test_scene {
using namespace eve::scene;
using namespace eve::graphics;

namespace {

[[maybe_unused]] bool approxEq(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) < eps; }

[[maybe_unused]] bool matApproxEq(const glm::mat4& a, const glm::mat4& b, float eps = 1e-4f) {
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) {
            if (!approxEq(a[c][r], b[c][r], eps)) return false;
        }
    }
    return true;
}

template <class T>
[[maybe_unused]] T* sceneValue(eve::Result<T*> result) {
    CHECK(result.ok());
    if (!result.ok()) return nullptr;
    return std::move(result).takeValue();
}

class BattleScene : public SceneComponent {
public:
    bool     showAlly = true;
    NodeDesc slot;

    NodeDesc build() override {
        std::vector<NodeDesc> kids = {
            node("map").withPosition(0.f, 0.f, 0.f),
            node("player").withPosition(1.f, 0.f, 0.f),
            when(showAlly, node("ally").withPosition(2.f, 0.f, 0.f)),
        };
        if (slot.id.empty())
            kids.push_back(group({}, "__slot_empty"));
        else
            kids.push_back(slot);
        return node("battle", std::move(kids), "battle");
    }
};

}  // namespace


TEST_CASE("Scene.link.syncRenderable3DWorldRotation") {
    // The node sync has to decompose the world matrix with the inverse of the order
    // the renderer recomposes it (RenderSystem3D's modelFromTransform is
    // T*Ry(yaw)*Rx(pitch)*Rz(roll)*S). Extracting with glm::eulerAngles() instead
    // returns a different Euler family, so a multi-axis node rendered at a wrong
    // orientation. A single-axis rotation happens to survive either way, hence the
    // three-axis angles here.
    SceneHost *h = sceneValue(SceneHost::createHost("link3d"));
    h->setTree(
        node("root",
             {node("mesh").withPosition(1.f, 2.f, 3.f).withRotation(0.5f, 0.25f, 0.1f).withScaleXYZ(2.f, 3.f, 4.f)}));

    auto *r = eve::graphics::Renderable3D::create();
    REQUIRE(r != nullptr);
    CHECK(h->linkRenderable3D("mesh", r));
    TransformSystem::updateHost(h);

    const auto t = r->transform();
    REQUIRE(approxEq(t->x, 1.f));
    REQUIRE(approxEq(t->y, 2.f));
    REQUIRE(approxEq(t->z, 3.f));
    REQUIRE(approxEq(t->sx, 2.f));
    REQUIRE(approxEq(t->sy, 3.f));
    REQUIRE(approxEq(t->sz, 4.f));
    REQUIRE(approxEq(t->yaw, 0.5f));
    REQUIRE(approxEq(t->pitch, 0.25f));
    REQUIRE(approxEq(t->roll, 0.1f));
}


TEST_CASE("Scene.render.parentChildOrbitPreview") {
    if (ecs::current()->getManager<Renderable2D>() != nullptr) {
        auto view = ecs::View<Renderable2D, Renderable2D::Sprite>();
        for (auto it = view.begin(); it != view.end(); ++it) {
            auto [sp]   = *it;
            sp->visible = false;
        }
    }

    auto* win = eve::window::Window::create();
    auto* gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    eve::window::WindowSettings s;
    s.width    = 640;
    s.height   = 420;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));

    SceneHost* h = sceneValue(SceneHost::createHost("orbit"));
    h->setTree(node("root",
                    {
                        node("hub").withSpace("2d").withPosition(320.f, 210.f, 0.f).withRotation(0.f, 0.f, 0.f),
                        node("arm",
                             {
                                 node("hand").withSpace("2d").withPosition(90.f, 0.f, 0.f),
                             })
                            .withSpace("2d")
                            .withPosition(320.f, 210.f, 0.f),
                        node("moon").withSpace("2d").withPosition(420.f, 210.f, 0.f),
                    })
                   .withSpace("2d"));

    auto* hub              = Renderable2D::create();
    hub->sprite()->width   = 48;
    hub->sprite()->height  = 48;
    hub->sprite()->r       = 0.95f;
    hub->sprite()->g       = 0.75f;
    hub->sprite()->b       = 0.25f;
    hub->sprite()->visible = true;

    auto* hand              = Renderable2D::create();
    hand->sprite()->width   = 28;
    hand->sprite()->height  = 28;
    hand->sprite()->r       = 0.35f;
    hand->sprite()->g       = 0.85f;
    hand->sprite()->b       = 1.f;
    hand->sprite()->visible = true;

    auto* moon              = Renderable2D::create();
    moon->sprite()->width   = 22;
    moon->sprite()->height  = 22;
    moon->sprite()->r       = 0.85f;
    moon->sprite()->g       = 0.45f;
    moon->sprite()->b       = 0.95f;
    moon->sprite()->visible = true;

    REQUIRE(h->linkRenderable2D("hub", hub));
    REQUIRE(h->linkRenderable2D("hand", hand));
    REQUIRE(h->linkRenderable2D("moon", moon));

    auto* cam      = Camera2D::createCamera();
    cam->data()->r = 0.08f;
    cam->data()->g = 0.09f;
    cam->data()->b = 0.12f;

    float handTravel = 0.f;
    float prevHX     = hand->transform()->x;
    for (int frame = 0; frame < 90; ++frame) {
        const float t     = float(frame) * 0.06f;
        SceneNode*  arm   = sceneValue(h->findById("arm"));
        SceneNode*  moonN = sceneValue(h->findById("moon"));
        REQUIRE(arm != nullptr);
        REQUIRE(moonN != nullptr);
        arm->roll         = t;
        arm->localDirty   = true;
        moonN->x          = 320.f + std::cos(t * 1.4f) * 120.f;
        moonN->y          = 210.f + std::sin(t * 1.4f) * 70.f;
        moonN->localDirty = true;

        TransformSystem::updateHost(h);
        RenderSystem::render(*gfx);

        handTravel += std::fabs(hand->transform()->x - prevHX);
        prevHX = hand->transform()->x;

        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) break;
        }
        SDL_Delay(16);
    }

    CHECK_GT(handTravel, 50.f);
    hub->sprite()->visible  = false;
    hand->sprite()->visible = false;
    moon->sprite()->visible = false;
    win->close();
}


TEST_CASE("Scene.link.genericMultiLinkAndSync") {
    SceneHost* h = sceneValue(SceneHost::createHost("genlink"));
    h->setTree(node("root", {node("soldier")}));

    auto*                 r3 = eve::graphics::Renderable3D::create();
    eve::physics::World3D w3(0.f, -9.8f, 0.f, true);
    eve::physics::Body3D* b3  = w3.newBody("kinematic", 0.f, 0.f, 0.f);
    auto*                 cam = eve::graphics::Camera3D::createCamera();

    CHECK(h->linkRenderable3D("soldier", r3));
    CHECK(h->linkPhysics3D("soldier", b3, 0));
    CHECK(h->linkCamera3D("soldier", cam));
    CHECK_EQ(h->linkCount("soldier"), 3);

    // node → body + camera (syncMode 0)
    SceneNode* n = sceneValue(h->findById("soldier"));
    REQUIRE(n != nullptr);
    n->x          = 3.f;
    n->y          = 4.f;
    n->z          = 5.f;
    n->localDirty = true;
    TransformSystem::updateHost(h);
    CHECK(approxEq(b3->getX(), 3.f));
    CHECK(approxEq(b3->getY(), 4.f));
    CHECK(approxEq(b3->getZ(), 5.f));
    CHECK(approxEq(cam->data()->eyeX, 3.f));
    CHECK(approxEq(cam->data()->eyeY, 4.f));
    CHECK(approxEq(cam->data()->eyeZ, 5.f));

    // body → node (syncMode 1); re-linking same kind replaces target/mode
    CHECK(h->linkPhysics3D("soldier", b3, 1));
    b3->setPosition(10.f, 20.f, 30.f);
    TransformSystem::updateHost(h);
    CHECK(approxEq(n->x, 10.f));
    CHECK(approxEq(n->y, 20.f));
    CHECK(approxEq(n->z, 30.f));

    // unlink by kind keeps the other links
    CHECK(h->unlink("soldier", findLinkKind("physics3d")));
    CHECK_EQ(h->linkCount("soldier"), 2);
    CHECK(!h->unlink("soldier", findLinkKind("physics3d")));

    // full rebuild preserves remaining links by id
    h->setTree(node("root", {node("soldier")}));
    SceneNode* n2 = sceneValue(h->findById("soldier"));
    REQUIRE(n2 != nullptr);
    REQUIRE_EQ(n2->links.size(), 2u);
    bool hasMesh = false, hasCam = false;
    for (const auto& l : n2->links) {
        if (l.target == r3) hasMesh = true;
        if (l.target == cam) hasCam = true;
    }
    CHECK(hasMesh);
    CHECK(hasCam);

    ecs::DestroyEntity(r3);
    ecs::DestroyEntity(cam);
    w3.destroy();
}

TEST_CASE("Scene.link.physics2D") {
    SceneHost* h = sceneValue(SceneHost::createHost("link2dphys"));
    h->setTree(node("root", {node("p").withSpace("2d").withPosition(10.f, 20.f, 0.f)}));

    eve::physics::World w(0.f, 0.f, true, 1.f);
    eve::physics::Body* b = w.newBody("kinematic", 0.f, 0.f);
    CHECK(h->linkPhysics2D("p", b, 0));
    TransformSystem::updateHost(h);
    CHECK(approxEq(b->getX(), 10.f));
    CHECK(approxEq(b->getY(), 20.f));

    // body → node: angle syncs into 2D roll
    CHECK(h->linkPhysics2D("p", b, 1));
    b->setPosition(30.f, 40.f);
    b->setAngle(0.5f);
    TransformSystem::updateHost(h);
    SceneNode* n = sceneValue(h->findById("p"));
    REQUIRE(n != nullptr);
    CHECK(approxEq(n->x, 30.f));
    CHECK(approxEq(n->y, 40.f));
    CHECK(approxEq(n->roll, 0.5f));

    CHECK(h->unlink("p", findLinkKind("physics2d")));
    CHECK_EQ(h->linkCount("p"), 0);
    w.destroy();
}

TEST_CASE("Scene.link.body3DPullRoundTripsEuler") {
    // pushBody3D composes Ry(yaw)*Rx(pitch)*Rz(roll) onto the body, so pullBody3D has
    // to invert exactly that order. glm::eulerAngles() returns a different Euler
    // family, which handed a body-authoritative node the wrong angles for any
    // multi-axis rotation; localMatrix() then rebuilt a different orientation than
    // the body actually has. Single-axis values agree either way, hence three axes.
    SceneHost *h = sceneValue(SceneHost::createHost("link3drot"));
    h->setTree(node("root", {node("rig")}));

    eve::physics::World3D w(0.f, -9.8f, 0.f, true);
    eve::physics::Body3D *b = w.newBody("kinematic", 0.f, 0.f, 0.f);
    REQUIRE(b != nullptr);

    const float     yaw = 0.5f, pitch = 0.25f, roll = 0.1f;
    const glm::quat q = glm::angleAxis(yaw, glm::vec3(0.f, 1.f, 0.f)) *
                        glm::angleAxis(pitch, glm::vec3(1.f, 0.f, 0.f)) *
                        glm::angleAxis(roll, glm::vec3(0.f, 0.f, 1.f));
    b->setRotation(q.x, q.y, q.z, q.w);

    CHECK(h->linkPhysics3D("rig", b, 1));  // body-authoritative: pull into the node
    TransformSystem::updateHost(h);

    SceneNode *n = sceneValue(h->findById("rig"));
    REQUIRE(n != nullptr);
    REQUIRE(approxEq(n->yaw, yaw));
    REQUIRE(approxEq(n->pitch, pitch));
    REQUIRE(approxEq(n->roll, roll));
    w.destroy();
}


// The camera projection the picking entry points need is an optional
// capability: without a provider they must fail explicitly instead of silently
// returning a result, and with it restored they must work again.


// ---------------------------------------------------------------------------
// Script side: eve.SceneEntity attach / detach / update / view integration
// ---------------------------------------------------------------------------

static const char* kSceneEcsScriptContent = R"SQ(
function testSceneEntityLifecycle() {
    class MoveComp extends eve.Component { speed = 2.0 }
    class EnemyAI extends eve.SceneEntity {
        move = MoveComp
        ticks = 0
        detached = false
        function onAttach() { node().setPosition(1.0, 2.0, 3.0) }
        function update(dt) {
            ticks += 1
            local p = node().getPosition()
            node().setPosition(p[0] + move.speed * dt, p[1], p[2])
        }
        function onDetach() { detached = true }
    }

    local scene = eve.Scene()
    scene.beginBuild()
    scene.beginNode("root", "")
    scene.addNode("enemy", "")
    scene.end()
    scene.mountBuildAs("battle")

    local e = scene.attachEntity("enemy", EnemyAI)
    if (e == null) return false
    if (!e.isAlive()) return false
    if (eve.view(EnemyAI).len() != 1) return false
    if (!scene.hasEntity("enemy", EnemyAI)) return false
    if (scene.getEntity("enemy", EnemyAI) != e) return false
    if (scene.entitiesOf("enemy").len() != 1) return false

    local ref = scene.getNodeRef("enemy")
    if (ref == null || !ref.isValid()) return false
    if (ref.getParentId() != "root") return false
    if (ref.getPath() != "root/enemy") return false
    if (!ref.hasEntity(EnemyAI)) return false

    local p = ref.getPosition()
    if (p.len() != 3 || p[0] != 1.0 || p[1] != 2.0 || p[2] != 3.0) return false

    // scene.update(dt) drives updateTransforms + script update(dt)
    scene.update(0.5)
    if (e.ticks != 1) return false
    local p2 = ref.getPosition()
    if (p2[0] < 1.9 || p2[0] > 2.1) return false
    local wp = ref.getWorldPosition()
    if (wp[0] < 1.9 || wp[0] > 2.1) return false

    // reconcile keeps the binding alive (same node id)
    scene.beginBuild()
    scene.beginNode("root", "")
    scene.addNode("enemy", "")
    scene.end()
    scene.remountBuildAs("battle")
    if (!scene.hasEntity("enemy", EnemyAI)) return false
    if (eve.view(EnemyAI).len() != 1) return false
    if (!e.isAlive()) return false

    // full rebuild removing the node tears the binding down
    scene.beginBuild()
    scene.beginNode("root", "")
    scene.end()
    scene.mountBuildAs("battle")
    if (scene.hasEntity("enemy", EnemyAI)) return false
    if (eve.view(EnemyAI).len() != 0) return false
    if (!e.detached) return false
    if (e.isAlive()) return false
    return true
}

function testSceneEntityViewCacheAfterDetach() {
    class Marker extends eve.Component { v = 1 }
    class Unit extends eve.SceneEntity { mark = Marker }

    local scene = eve.Scene()
    scene.beginBuild()
    scene.beginNode("root", "")
    scene.addNode("a", "")
    scene.addNode("b", "")
    scene.end()
    scene.mountBuildAs("units")

    local a = scene.attachEntity("a", Unit)
    local b = scene.attachEntity("b", Unit)
    if (eve.view(Unit).len() != 2) return false

    scene.detachEntity("a", a)
    if (eve.view(Unit).len() != 1) return false
    local remaining = eve.view(Unit)
    if (remaining.len() != 1 || remaining[0] != b) return false

    scene.detachEntity("b", b)
    if (eve.view(Unit).len() != 0) return false
    return true
}

function testSceneEntityEnableAndDelayedDetach() {
    class ToggleAI extends eve.SceneEntity {
        ticks = 0
        enables = 0
        disables = 0
        detached = false
        function onEnable() { enables += 1 }
        function onDisable() { disables += 1 }
        function update(dt) {
            ticks += 1
            if (ticks == 1) scene().scheduleDetachEntity(nodeId, this)
        }
        function onDetach() { detached = true }
    }

    local scene = eve.Scene()
    scene.beginBuild()
    scene.beginNode("root")
    scene.addNode("npc")
    scene.addNode("prop")
    scene.end()
    scene.mountBuildAs("life")

    local e = scene.attachEntity("npc", ToggleAI)
    if (e == null) return false
    local other = scene.attachEntity("prop", ToggleAI)
    if (other == null) return false

    e.disable()
    if (e.disables != 1 || e.isEnabled()) return false
    scene.update(0.1)
    if (e.ticks != 0) return false  // disabled skips update
    if (other.ticks != 1) return false
    if (!other.detached) return false  // delayed detach flushed at end of update
    if (eve.view(ToggleAI).len() != 1) return false

    e.enable()
    if (e.enables != 1 || !e.isEnabled()) return false
    scene.update(0.1)
    if (e.ticks != 1) return false
    if (!e.detached) return false
    if (eve.view(ToggleAI).len() != 0) return false

    local ids = scene.collectIdsWith(ToggleAI)
    if (ids.len() != 0) return false
    return true
}

function testSceneLinkPhysics2D() {
    local scene = eve.Scene()
    scene.beginBuild()
    scene.beginNode("root", "")
    scene.addNode("ball", "")
    scene.setBuildPosition(50.0, 60.0, 0.0)
    scene.end()
    scene.mountBuildAs("links")

    local physics = eve.Physics()
    local world = physics.newWorld(0.0, 0.0, false)
    local body = world.newBody("kinematic", 0.0, 0.0)

    if (!scene.linkPhysics2D("ball", body)) return false
    if (scene.linkCount("ball") != 1) return false
    scene.updateTransforms()
    if (body.getX() != 50.0 || body.getY() != 60.0) return false

    // NodeRef forwarding + body-authoritative mode
    local ref = scene.getNodeRef("ball")
    if (ref == null) return false
    body.setPosition(99.0, 88.0)
    if (!ref.linkPhysics2D(body, "body")) return false
    scene.updateTransforms()
    local p = ref.getPosition()
    if (p[0] != 99.0 || p[1] != 88.0) return false

    // unlink by kind via NodeRef
    if (!ref.unlinkNodeKind("physics2d")) return false
    if (ref.linkCount() != 0) return false
    world.destroy()
    return true
}

function testSceneApiCompleteness() {
    local scene = eve.Scene()
    scene.beginBuild()
    scene.beginNode("root", "")
    scene.addNode("pivot", "")
    scene.setBuildPosition(10.0, 0.0, 0.0)
    scene.addNode("ball", "")
    scene.setBuildPosition(1.0, 2.0, 3.0)
    scene.end()
    scene.mountBuildAs("api")

    // getters
    local p = scene.getNodePosition("ball")
    if (p.len() != 3 || p[0] != 1.0 || p[1] != 2.0 || p[2] != 3.0) return false
    scene.setNodeScale("ball", 2.0, 3.0, 4.0)
    local s = scene.getNodeScale("ball")
    if (s[0] != 2.0 || s[2] != 4.0) return false
    scene.setNodeVisible("ball", false)
    if (scene.getNodeVisible("ball")) return false

    // world + space conversion. addNode() adds a *sibling* of the open node (the
    // documented order used by examples/scene-ecs/main.nut), so ball hangs off root,
    // not off pivot: its world position is its own local position here.
    scene.updateTransforms()
    local wpi = scene.getNodeWorldPosition("pivot")
    if (wpi[0] != 10.0 || wpi[1] != 0.0 || wpi[2] != 0.0) return false
    local wp = scene.getNodeWorldPosition("ball")
    if (wp[0] != 1.0 || wp[1] != 2.0 || wp[2] != 3.0) return false
    local lw = scene.localToWorld("ball", 0.0, 0.0, 0.0)
    if (lw[0] != 1.0) return false
    local wl = scene.worldToLocal("ball", 1.0, 2.0, 3.0)
    if (wl[0] != 0.0 || wl[1] != 0.0) return false

    // hierarchy ops
    if (!scene.setNodeParent("ball", "pivot")) return false
    if (scene.getParentId("ball") != "pivot") return false

    // Reparenting keeps the local transform (SceneHost::setParent only relinks), so
    // the parent's offset now shows up: pivot(10) + ball local(1) == 11.
    scene.updateTransforms()
    local wp2 = scene.getNodeWorldPosition("ball")
    if (wp2[0] != 11.0 || wp2[1] != 2.0 || wp2[2] != 3.0) return false
    local lw2 = scene.localToWorld("ball", 0.0, 0.0, 0.0)
    if (lw2[0] != 11.0) return false
    local wl2 = scene.worldToLocal("ball", 11.0, 2.0, 3.0)
    if (wl2[0] != 0.0 || wl2[1] != 0.0) return false

    if (!scene.removeNode("ball")) return false
    if (scene.getParentId("ball") != "") return false
    if (!scene.addNodeChild("root", "ball")) return false

    // quaternion round trip
    scene.setNodeRotation("ball", 0.5, 0.25, 0.1)
    local q = scene.getNodeQuaternion("ball")
    if (!scene.setNodeQuaternion("ball", q[0], q[1], q[2], q[3])) return false
    local r = scene.getNodeRotation("ball")
    local d0 = r[0] - 0.5
    local d1 = r[1] - 0.25
    local d2 = r[2] - 0.1
    if (d0 * d0 > 1e-8 || d1 * d1 > 1e-8 || d2 * d2 > 1e-8) return false

    // lookAt: +Z toward target
    scene.setNodePosition("ball", 0.0, 0.0, 0.0)
    if (!scene.setNodeLookAt("ball", 0.0, 0.0, 5.0)) return false
    scene.updateTransforms()
    local f = scene.getNodeRef("ball").getForward()
    if (f[0] * f[0] > 1e-8 || f[1] * f[1] > 1e-8 || (f[2] - 1.0) * (f[2] - 1.0) > 1e-8) return false

    // tags / layer
    if (!scene.addNodeTag("ball", "unit")) return false
    if (!scene.hasNodeTag("ball", "unit")) return false
    if (scene.collectIdsByTag("unit").len() != 1) return false
    if (!scene.removeNodeTag("ball", "unit")) return false
    scene.setNodeLayer("ball", 3)
    if (scene.getNodeLayer("ball") != 3) return false
    return true
}

function testSceneNodeEvents() {
    local scene = eve.Scene()
    scene.beginBuild()
    scene.beginNode("root", "")
    scene.addNode("a", "")
    scene.end()
    scene.mountBuildAs("evt")

    // handler registers against the selected host
    local events = []
    scene.setNodeEventHandler(function(action, nodeId, parentId) {
        events.push(action + ":" + nodeId)
    })

    // reconcile patch fires node_changed per affected node
    events.clear()
    scene.beginBuild()
    scene.beginNode("root", "")
    scene.addNode("a", "")
    scene.setBuildPosition(3.0, 0.0, 0.0)
    scene.end()
    scene.remountBuildAs("evt")
    if (events.len() != 2) return false
    if (events[0] != "node_changed:root") return false

    // full rebuild removing a node fires node_removed
    events.clear()
    scene.beginBuild()
    scene.beginNode("root", "")
    scene.end()
    scene.mountBuildAs("evt")
    if (events.len() < 1 || events[0] != "node_removed:a") return false

    // reparent fires node_moved
    scene.beginBuild()
    scene.beginNode("root", "")
    scene.addNode("x", "")
    scene.addNode("y", "")
    scene.end()
    scene.mountBuildAs("evt")
    events.clear()
    scene.setNodeParent("y", "x")
    if (events.len() < 1 || events[0] != "node_moved:y") return false
    return true
}

function testSceneComponentOnMount() {
    local mounted = 0
    scene <- eve.Scene()
    class MyComp extends eve.SceneComponent {
        function onMount() { mounted += 1 }
        function build() {
            // `scene` bare would resolve to the inherited SceneComponent.scene()
            // accessor method, so the documented pattern is to call the accessor
            // (see examples/basic/ui_component.nut: `local u = ui()`). updateIfDirty()
            // has already called beginBuild() for this pass.
            local s = scene()
            s.beginNode("root", "")
            s.end()
        }
    }
    local c = MyComp()
    c.mountAs("mc")
    if (mounted != 1) return false
    c.markDirty()
    c.updateIfDirty()
    if (mounted != 1) return false  // only the first mount
    return true
}

function testSceneErrorSemantics() {
    local scene = eve.Scene()
    scene.beginBuild()
    scene.beginNode("root", "")
    scene.addNode("a", "")
    scene.end()
    scene.mountBuildAs("err")
    local threw = false
    try {
        scene.getNodePosition("nope")
    } catch (e) {
        threw = true
    }
    if (!threw) return false
    return true
}

function testSceneSerializeAndPick() {
    local scene = eve.Scene()
    scene.beginBuild()
    scene.beginNode("root", "")
    scene.addNode("target", "")
    scene.end()
    scene.mountBuildAs("spk")
    scene.setNodeBounds("target", -1.0, -1.0, -1.0, 1.0, 1.0, 1.0)
    scene.updateTransforms()

    local json = scene.serializeHost()
    if (json.len() == 0) return false
    if (!scene.deserializeHostAt("spk2", json)) return false
    local p = scene.getNodePositionAt("spk2", "target")
    if (p[0] != 0.0 || p[1] != 0.0) return false
    if (scene.pickRay(0.0, 0.0, 5.0, 0.0, 0.0, -1.0) != "target") return false
    return true
}
function testAttachRejectsPlainEntity() {
    // This used to have to touch eve.Component first, because eve.SceneEntity is
    // installed by a post-ECS hook and a script whose first ECS name was SceneEntity
    // did not find it. The lazy resolver now injects the script ECS base itself, so
    // the ordering no longer matters (see testSceneEntityIsTheFirstEcsName).
    class Marker extends eve.Component { v = 0 }
    class PlainEntity extends eve.Entity { mark = Marker }

    local scene = eve.Scene()
    scene.beginBuild()
    scene.beginNode("root", "")
    scene.addNode("n", "")
    scene.end()
    scene.mountBuildAs("guard")

    // A plain Entity is not a script scene entity. This used to die mid-call with
    // "the index '_scene' does not exist" because the body stores the host binding in
    // members only eve.SceneEntity declares.
    if (scene.attachEntity("n", PlainEntity) != null) return false
    return scene.entitiesOf("n").len() == 0
}

function testSceneEntityIsTheFirstEcsName() {
    // `class MyThing extends eve.SceneEntity` is the natural opening line of a user
    // script, so SceneEntity being the *first* eve.* name touched has to work.
    class OnlyEntity extends eve.SceneEntity { }

    local scene = eve.Scene()
    scene.beginBuild()
    scene.beginNode("root", "")
    scene.addNode("n", "")
    scene.end()
    scene.mountBuildAs("first")

    local e = scene.attachEntity("n", OnlyEntity)
    if (e == null) return false
    if (!e.isAlive()) return false
    if (!scene.hasEntity("n", OnlyEntity)) return false
    if (!eve.isSubclass(OnlyEntity, eve.SceneEntity)) return false
    return eve.view(OnlyEntity).len() == 1
}

function testCallbackFailureIsReported() {
    class Marker extends eve.Component { v = 1 }
    class Unit extends eve.SceneEntity { mark = Marker }

    local scene = eve.Scene()
    scene.beginBuild()
    scene.beginNode("root", "")
    scene.addNode("a", "")
    scene.end()
    scene.mountBuildAs("failreport")
    if (scene.attachEntity("a", Unit) == null) return false

    // The callback runs and throws. `visited == 1` is what makes this a *throw* rather
    // than a callback that never ran, so the failure the engine has to report is real.
    local visited = 0
    scene.forEachEntity("failreport", "a", function(entity, index) {
        visited += 1
        throw "callback exploded"
    })
    return visited == 1
}

function testUpdateFailureIsReported() {
    class Boom extends eve.SceneEntity {
        function update(dt) { throw "update exploded" }
    }

    local scene = eve.Scene()
    scene.beginBuild()
    scene.beginNode("root", "")
    scene.addNode("n", "")
    scene.end()
    scene.mountBuildAs("updfail")
    if (scene.attachEntity("n", Boom) == null) return false

    // scene.update(dt) drives every rooted instance's update(dt); this one throws. The
    // engine has to report that rather than treat it like an instance that returned early.
    scene.update(0.016)
    return true
}

// 原生工具（编辑器 inspector / MCP / 诊断）要怎么读到"这个节点上的脚本组件状态"：
// `Scene::scriptEntitiesAt` 给实例，`scriptComponentInstancesOf` 给已登记组件的字段。
function buildSceneWithRegisteredComponent() {
    class ToolProbe extends eve.Component { x = 1.5; y = 2.5 }
    class ToolEntity extends eve.SceneEntity { probe = ToolProbe }

    local registered = eve.component("ToolProbe", 1, ToolProbe).ok
    local scene = eve.Scene()
    scene.beginBuild()
    scene.beginNode("root", "")
    scene.addNode("probe", "")
    scene.end()
    scene.mountBuildAs("tools")
    local e = scene.attachEntity("probe", ToolEntity)
    if (e == null) return false
    e.probe.x = 42.0
    return registered
}

// 未登记组件（纯槽位语义）的对照片：引擎工具**不该**看到它——它没有名字与版本，无从寻址。
function buildSceneWithUnregisteredComponent() {
    class LooseProbe extends eve.Component { v = 7.0 }
    class LooseEntity extends eve.SceneEntity { loose = LooseProbe }

    local scene = eve.Scene()
    scene.beginBuild()
    scene.beginNode("root", "")
    scene.addNode("loose", "")
    scene.end()
    scene.mountBuildAs("loosehost")
    return scene.attachEntity("loose", LooseEntity) != null
}

)SQ";

class SceneEcsBridgeFixture {
public:
    SceneEcsBridgeFixture() : vm(2048, ssq::Libs::ALL) {
        eve::ModuleManager::expose(vm);
        ssq::Script s = vm.compileSource(kSceneEcsScriptContent);
        vm.run(s);
    }

    ssq::VM vm;
};

TEST_CASE_FIXTURE(SceneEcsBridgeFixture, "Scene.script.entityLifecycle") {
    REQUIRE(vm.callFunc(vm.findFunc("testSceneEntityLifecycle"), vm).toBool());
}

// J-3 的"引擎工具读到脚本组件"读侧：节点 → 脚本实体（原生访问）→ 已登记组件 → 字段值。
// 这是编辑器 inspector / MCP 那一格的地基，所以在这里钉住它（不依赖编辑器 UI）。
TEST_CASE_FIXTURE(SceneEcsBridgeFixture, "Scene.script.nativeAccessReadsRegisteredComponentState") {
    REQUIRE(vm.callFunc(vm.findFunc("buildSceneWithRegisteredComponent"), vm).toBool());

    auto *scene = eve::scene::Scene::create();
    REQUIRE(scene != nullptr);

    const auto entities = scene->scriptEntitiesAt("tools", "probe");
    REQUIRE_EQ(entities.size(), std::size_t{1});

    auto states = eve::scriptComponentInstancesOf(entities.front());
    REQUIRE(states.ok());
    REQUIRE_EQ(states.value().size(), std::size_t{1});
    CHECK_EQ(states.value().front().name, std::string("ToolProbe"));
    CHECK_EQ(states.value().front().version.value(), std::uint64_t{1});
    CHECK(states.value().front().generation.value() >= 1);
    // 字段按声明顺序收集，值来自引擎侧那一个权威存储（脚本写 42.0 的那次也是写它）。
    const auto &fields = states.value().front().fields;
    const auto  x      = fields.find("x");
    const auto  y      = fields.find("y");
    REQUIRE(x != fields.end());
    REQUIRE(y != fields.end());
    CHECK_EQ(x->second.asDouble(), 42.0);
    CHECK_EQ(y->second.asDouble(), 2.5);

    // 负例：没有挂脚本的节点、未知宿主/节点都返回空数组（不是错误），调用方据此报"这里没有脚本"。
    CHECK(scene->scriptEntitiesAt("tools", "root").empty());
    CHECK(scene->scriptEntitiesAt("nope", "probe").empty());
}

// 未登记的组件（纯槽位语义）**不该**出现在原生工具里：它没有名字与版本，引擎无从寻址。
TEST_CASE_FIXTURE(SceneEcsBridgeFixture, "Scene.script.nativeAccessSkipsUnregisteredComponents") {
    REQUIRE(vm.callFunc(vm.findFunc("buildSceneWithUnregisteredComponent"), vm).toBool());

    auto      *scene    = eve::scene::Scene::create();
    const auto entities = scene->scriptEntitiesAt("loosehost", "loose");
    REQUIRE_EQ(entities.size(), std::size_t{1});

    auto states = eve::scriptComponentInstancesOf(entities.front());
    REQUIRE(states.ok());
    CHECK(states.value().empty());
}

TEST_CASE_FIXTURE(SceneEcsBridgeFixture, "Scene.script.entityViewCacheAfterDetach") {
    REQUIRE(vm.callFunc(vm.findFunc("testSceneEntityViewCacheAfterDetach"), vm).toBool());
}

TEST_CASE_FIXTURE(SceneEcsBridgeFixture, "Scene.script.entityEnableAndDelayedDetach") {
    CHECK(vm.callFunc(vm.findFunc("testSceneEntityEnableAndDelayedDetach"), vm).toBool());
}

TEST_CASE_FIXTURE(SceneEcsBridgeFixture, "Scene.script.linkPhysics2D") {
    REQUIRE(vm.callFunc(vm.findFunc("testSceneLinkPhysics2D"), vm).toBool());
}

TEST_CASE_FIXTURE(SceneEcsBridgeFixture, "Scene.script.apiCompleteness") {
    REQUIRE(vm.callFunc(vm.findFunc("testSceneApiCompleteness"), vm).toBool());
}

TEST_CASE_FIXTURE(SceneEcsBridgeFixture, "Scene.script.nodeEvents") {
    REQUIRE(vm.callFunc(vm.findFunc("testSceneNodeEvents"), vm).toBool());
}

TEST_CASE_FIXTURE(SceneEcsBridgeFixture, "Scene.script.componentOnMount") {
    REQUIRE(vm.callFunc(vm.findFunc("testSceneComponentOnMount"), vm).toBool());
}

TEST_CASE_FIXTURE(SceneEcsBridgeFixture, "Scene.script.errorSemantics") {
    REQUIRE(vm.callFunc(vm.findFunc("testSceneErrorSemantics"), vm).toBool());
}

TEST_CASE_FIXTURE(SceneEcsBridgeFixture, "Scene.script.attachRejectsPlainEntity") {
    REQUIRE(vm.callFunc(vm.findFunc("testAttachRejectsPlainEntity"), vm).toBool());
}

TEST_CASE_FIXTURE(SceneEcsBridgeFixture, "Scene.script.entityIsTheFirstEcsName") {
    REQUIRE(vm.callFunc(vm.findFunc("testSceneEntityIsTheFirstEcsName"), vm).toBool());
}

TEST_CASE_FIXTURE(SceneEcsBridgeFixture, "Scene.script.callbackFailureIsNotSilent") {
    // A callback that throws used to look exactly like an empty node: the failed sq_call
    // was turned into a false return and the script error was dropped, so a broken
    // handler produced no signal at all (plan doc 10.8).
    eve::script::resetReportedScriptCallFailures();
    REQUIRE(vm.callFunc(vm.findFunc("testCallbackFailureIsReported"), vm).toBool());
    REQUIRE(eve::script::reportedScriptCallFailures() >= 1);
}

TEST_CASE_FIXTURE(SceneEcsBridgeFixture, "Scene.script.updateFailureIsNotSilent") {
    // The per-instance update(dt) path (Scene::callMethod) had the same gap as the
    // forEachEntity callback: a throwing update() returned false and said nothing.
    eve::script::resetReportedScriptCallFailures();
    REQUIRE(vm.callFunc(vm.findFunc("testUpdateFailureIsReported"), vm).toBool());
    REQUIRE(eve::script::reportedScriptCallFailures() >= 1);
}

TEST_CASE_FIXTURE(SceneEcsBridgeFixture, "Scene.script.serializeAndPick") {
    REQUIRE(vm.callFunc(vm.findFunc("testSceneSerializeAndPick"), vm).toBool());
}
}  // namespace test_scene
#endif
#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_scene_authoring_script)
#include "PrivateTestScope.h"
#include <simplesquirrel/simplesquirrel.hpp>
#include "common/Module.h"
#include "scene/editor/SceneEditorModule.h"
#include "scripts.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// scene_authoring_script.cpp
namespace test_scene_authoring_script {
TEST_CASE("scene.authoring.script_owns_document_and_rejects_bad_bounds") {
    ssq::VM vm(2048, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        editor <- eve.Editor();
        module <- eve.SceneEditorModule();
        function checked(r) {if(!r.ok) throw r.status.summary;return r.value;}
        session <- checked(module.createAuthoringSession("script-authoring"));
        spawned <- session.execute("scene.authoring.spawn.v1",{object="box",source={kind="box"}});
        if(!spawned.ok) throw spawned.status.summary;
        saved <- session.saveJson();
        snapshot <- checked(session.snapshot());
        queried <- module.inspectAuthoringSnapshot(snapshot,"box",[-0.5,-0.5,-0.5,0.5,0.5,0.5]);
        if(!queried.ok || !queried.hasValue || queried.value.worldCorners.len()!=8) throw "query failed";
        bad <- module.inspectAuthoringSnapshot(snapshot,"box",["bad",0,0,1,1,1]);
        if(bad.ok || saved!=session.saveJson()) throw "invalid bounds mutated state";
    )"));
    REQUIRE(vm.find("spawned").toTable().get<bool>("ok"));
    REQUIRE(!vm.find("bad").toTable().get<bool>("ok"));
}

TEST_CASE("scene.authoring.director_compiles_and_missing_native_provider_is_observable") {
    ssq::VM vm(2048, ssq::Libs::ALL);
    bool    refused = false;
    auto    script  = vm.compileSource(eve::scene_director_content);
    try {
        vm.run(script);
    } catch (const std::exception&) {
        refused = true;
    }
    REQUIRE(refused);
}
}  // namespace test_scene_authoring_script
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_scene_binding_architecture)
#include "PrivateTestScope.h"
#include <simplesquirrel/vm.hpp>
#include "common/BindingContracts.h"
#include "common/Module.h"
#include "common/ScriptCompiler.h"
#include "scene/NodeDesc.h"
#include "scene/Scene.h"
#include "scene/SceneHost.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// scene_binding_architecture.cpp
namespace test_scene_binding_architecture {
TEST_CASE("Scene.script.visibilityArchitectureMapping") {
    const auto* contract = eve::script::generatedBindingContracts().find("scene/SceneNodeRef.setVisible");
    REQUIRE(contract != nullptr);
    REQUIRE(contract->architecture.has_value());
    CHECK_EQ(contract->cppSymbol, std::string("SceneNodeRef::setVisible"));
    CHECK_EQ(contract->architecture->stateOwner, std::string("eve::scene::SceneHost"));
    CHECK_EQ(contract->architecture->writes.at(0), std::string("eve::scene::SceneNode.visible"));
    CHECK_EQ(contract->architecture->inputHash.size(), size_t(64));
    CHECK(contract->toJson().find("Scene.script.visibilityArchitectureMapping") != std::string::npos);

    ssq::VM vm(2048, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    auto* scene   = eve::scene::Scene::create();
    auto  mounted = scene->mountAs("mapping-proof", eve::scene::node("root", {eve::scene::node("target")}));
    REQUIRE(mounted.ok());
    auto script = vm.compileSource(R"SQ(
        local scene = eve.Scene()
        local ref = scene.getNodeRefAt("mapping-proof", "target")
        mapping_applied <- ref.setVisible(false)
        mapping_visible <- ref.isVisible()
        local missing = scene.getNodeRefAt("mapping-proof", "absent")
        mapping_missing <- missing.setVisible(false)
    )SQ");
    vm.run(script);
    CHECK(vm.get<bool>("mapping_applied"));
    CHECK(!vm.get<bool>("mapping_visible"));
    CHECK(!vm.get<bool>("mapping_missing"));
    auto host = scene->findHost("mapping-proof");
    REQUIRE(host.ok());
    auto target = host.value()->findById("target");
    REQUIRE(target.ok());
    CHECK(!target.value()->visible);
}
}  // namespace test_scene_binding_architecture
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_scene_editor_script)
#include "PrivateTestScope.h"
#include <simplesquirrel/simplesquirrel.hpp>
#include "common/Module.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// scene_editor_script.cpp
namespace test_scene_editor_script {
TEST_CASE("scene.editor.script_session_owns_target_and_projects_transaction_results") {
    ssq::VM vm(2048, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        function checked(result) { if (!result.ok) throw result.status.summary; return result; }
        editor <- eve.Editor();
        module <- eve.SceneEditorModule();
        created <- checked(module.createSession("test.scene"));
        session <- created.value;
        checked(session.execute("scene.object.create.v1",{object="box"}));
        saved <- session.saveJson();
        checked(session.execute("scene.object.update.v1",{object="box",name="Crate",position=[1.0,2.0,3.0]}));
        checked(session.undo());
        if (saved != session.saveJson()) throw "undo failed";
        checked(session.restrictCommands(["scene.transform.set.v1"]));
        refused <- session.execute("scene.object.delete.v1",{object="box"});
        checked(session.execute("scene.transform.set.v1",{object="box",position=[2.0,0.0,0.0]}));
        state <- checked(session.snapshot()).value;
    )"));
    REQUIRE(vm.find("created").toTable().get<bool>("ok"));
    REQUIRE(!vm.find("refused").toTable().get<bool>("ok"));
    REQUIRE_EQ(vm.find("state").toTable().get<int>("schemaVersion"), 1);
}

TEST_CASE("scene.editor.script_physics_placement_previews_and_commits") {
    ssq::VM vm(2048, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        function checked(result) { if (!result.ok) throw result.status.summary; return result; }
        editor <- eve.Editor();
        module <- eve.SceneEditorModule();
        session <- checked(module.createSession("test.physics-placement")).value;
        checked(session.execute("scene.object.create.v1",{object="wall",position=[2.0,0.5,0.0]}));
        checked(session.execute("scene.object.create.v1",{object="crate",position=[0.0,0.5,0.0]}));
        checked(session.cachePhysicsPlacementCompound("crate","crate-mesh-v1",[
            {shape="box",source="generated",halfExtents=[0.25,0.5,0.5],localPosition=[-0.25,0.0,0.0]},
            {shape="box",source="generated",halfExtents=[0.25,0.5,0.5],localPosition=[0.25,0.0,0.0]}
        ]));
        colliderCache <- session.savePhysicsPlacementColliderCacheJson();
        checked(session.restorePhysicsPlacementColliderCacheJson(colliderCache));
        before <- session.saveJson();
        checked(session.beginPhysicsPlacement({objects=[
            {object="wall",selected=false,halfExtents=[0.5,2.0,2.0]},
            {object="crate",selected=true,shape="auto",colliderResourceKey="crate-mesh-v1",
             halfExtents=[0.5,0.5,0.5],layerBits=1,tags=["placeable"]}
        ],settings={colliderPolicy="generatedWhenMissing",maximumAngularSpeed=20.0,
                    scaleSpeedByBounds=true,teleportDistance=0.0},
           admission={includedLayerBits=1,excludedLayerBits=0,requiredTags=[]}}));
        for(local i=0;i<90;i++) frame <- checked(session.updatePhysicsPlacement(4.0,0.5,0.0,1.0/60.0)).value;
        if (!session.isPhysicsPlacementActive() || before != session.saveJson()) throw "preview leaked";
        checked(session.commitPhysicsPlacement());
        if (session.isPhysicsPlacementActive() || before == session.saveJson()) throw "commit failed";
        checked(session.undo());
        if (before != session.saveJson()) throw "undo failed";
        checked(session.beginPhysicsPlacement({primary="crate",objects=[
            {object="wall",selected=false,halfExtents=[0.5,2.0,2.0]},
            {object="crate",selected=true,halfExtents=[0.5,0.5,0.5]}
        ]}));
        scaled <- checked(session.updatePhysicsPlacementTransform(
            [0.0,0.5,0.0],[0.0,0.0,0.5],[2.0,1.5,0.75],1.0/60.0)).value;
        if (scaled.objects[1].scale[0] < 1.99) throw "scale preview failed";
        checked(session.cancelPhysicsPlacement());
        checked(session.beginPhysicsPlacement({mode="point",settings={freezePositionX=true,freezePositionY=true,
            freezePositionZ=true,freezeRotationX=false,freezeRotationY=false,freezeRotationZ=false},objects=[
            {object="crate",selected=true,halfExtents=[0.5,0.5,0.5]}
        ]}));
        pointed <- checked(session.updatePhysicsPlacement(5.0,0.5,0.0,1.0/60.0)).value;
        if (pointed.objects[0].position[0] > 0.01) throw "point moved locked object";
        checked(session.cancelPhysicsPlacement());
        checked(session.beginPhysicsPlacement({objects=[
            {object="wall",selected=false,halfExtents=[0.5,2.0,2.0]},
            {object="crate",selected=true,halfExtents=[0.5,0.5,0.5]}
        ]}));
        surface <- checked(session.alignPhysicsPlacementToSurface(
            [5.0,0.5,0.0],[-5.0,0.5,0.0],0.02,1.0/60.0)).value;
        if (!surface.surfaceAligned || surface.surfaceNormal[0] < 0.99) throw "surface align failed";
        checked(session.cancelPhysicsPlacement());
        checked(session.execute("scene.transform.set.v1",{object="crate",position=[0.0,3.0,0.0]}));
        checked(session.beginPhysicsPlacement({mode="drop",settings={gravityY=-9.8,subStepCount=4,
            preserveSelectionLayout=true},objects=[
            {object="wall",selected=false,halfExtents=[5.0,0.5,5.0]},
            {object="crate",selected=true,halfExtents=[0.5,0.5,0.5]}
        ]}));
        for(local i=0;i<240 && session.isPhysicsPlacementActive();i++) {
            dropped <- checked(session.updatePhysicsPlacement(0.0,0.0,0.0,1.0/60.0)).value;
            if (dropped.settled) checked(session.commitPhysicsPlacement());
        }
        if (session.isPhysicsPlacementActive() || !dropped.settled) throw "drop did not settle";
    )"));
}
}  // namespace test_scene_editor_script
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_scene_object_lifetime)
#include "PrivateTestScope.h"
#include <simplesquirrel/simplesquirrel.hpp>
#include "common/ECS.h"
#include "common/Module.h"
#include "common/Runtime.h"
#include "scene/SceneObject.h"
#include "zeroerr/unittest.h"
// scene_object_lifetime.cpp
namespace test_scene_object_lifetime {
TEST_CASE("Scene.script.firstObjectLookupAndDelayedDetach") {
    ecs::Table       table;
    ecs::ScopedTable current(table);
    eve::Runtime     runtime;
    eve::ModuleManager::expose(runtime);
    const auto script = runtime.runSource(R"SQ(
        if (eve._ensureScriptECS() < 0) throw "script ECS initialization failed";
        local host = eve.Scene();
        class Pulse extends eve.SceneEntity {
            ticks = 0;
            detached = false;
            function update(dt) {
                ticks += 1;
                if (!scene().scheduleDetachEntity(nodeId, this)) throw "detach failed";
            }
            function onDetach() { detached = true; }
        }
        host.beginBuild();
        host.beginNode("root", "Root");
        host.addNode("marker", "Marker");
        host.end();
        if (!host.mountBuildAs("first-slot")) throw "mount failed";
        local entity = host.attachEntity("marker", Pulse);
        if (entity == null || host.getEntity("marker", Pulse) != entity) throw "lookup failed";
        local callbacks = 0;
        host.forEachEntity("first-slot", "marker", function(instance, index) {
            if (instance != entity || index != 0) throw "callback arguments failed";
            callbacks += 1;
        });
        if (callbacks != 1) throw "callback was not invoked";
        if (host.getEntityAt("first-slot", "marker", Pulse) != entity) throw "host lookup failed";
        local entities = host.entitiesOfAt("first-slot", "marker");
        if (entities.len() != 1 || entities[0] != entity) throw "host enumeration failed";
        entity.disable();
        host.update(0.016);
        if (entity.ticks != 0) throw "disabled entity updated";
        entity.enable();
        host.update(0.016);
        host.update(0.016);
        if (entity.ticks != 1 || !entity.detached) throw "delayed detach failed";
        if (host.getEntity("marker", Pulse) != null) throw "detached entity still rooted";
        host.beginBuild();
        host.beginNode("root", "Root");
        host.addNode("marker", "Marker");
        host.end();
        if (!host.remountBuildAs("first-slot")) throw "remount failed";
        local replacement = host.attachEntity("marker", Pulse);
        if (replacement == null || host.getEntity("marker", Pulse) != replacement) throw "replacement lookup failed";
        host.update(0.016);
        if (!replacement.detached) throw "replacement detach failed";
        local orphan = host.attachEntity("marker", Pulse);
        if (orphan == null) throw "orphan attach failed";
        host.beginBuild();
        host.beginNode("root", "Root");
        host.end();
        if (!host.remountBuildAs("first-slot")) throw "host replacement failed";
        if (!orphan.detached) throw "host-first teardown did not detach its entity";
    )SQ");
    CHECK(script != 0);
    CHECK(table.getManager<eve::scene::SceneObject>() != nullptr);
}
}  // namespace test_scene_object_lifetime
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_schema)
#include <simplesquirrel/simplesquirrel.hpp>
#include "PrivateTestScope.h"
#include "schema/Schema.h"
#include "schema/SchemaRegistry.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// schema.cpp
namespace test_schema {
using namespace eve::schema;

namespace {

const char* unitSchema = R"({
  "id":"unit", "version":2, "title":"Unit", "additionalProperties":false,
  "fields":[
    {"name":"id", "type":"string", "required":true, "minLength":1},
    {"name":"speed", "type":"number", "minimum":0, "maximum":100},
    {"name":"role", "type":"string", "enum":["tank","infantry"]},
    {"name":"weapons", "type":"array", "elementType":"string", "reference":"weapon"}
  ]
})";

[[maybe_unused]] bool registrationSucceeded(eve::Result<SchemaRegistrationStatus> result) {
    return result.ok() && result.value() == SchemaRegistrationStatus::Registered;
}

}  // namespace


TEST_CASE("schema.module.scriptBinding") {
    SchemaRegistry::clear();
    ssq::VM vm(1024, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    auto script = vm.compileSource(R"(
        function schemaBindingTest() {
            local s = eve.Schema();
            s.clear();
            local registered = s.registerJson(@"{""id"":""rank"",""version"":3,""fields"":[{""name"":""level"",""type"":""integer"",""required"":true}]}");
            if (!registered.ok || !eve.result.isChecked(registered)) return false;
            if (s.getSchemaId(0) != "rank" || s.getSchemaVersion("rank") != 3) return false;
            if (!s.hasVersion("rank", 3) || s.getSchemaVersionCount("rank") != 1 ||
                s.getSchemaVersionAt("rank", 0) != 3) return false;
            if (s.getFieldName("rank", 0) != "level" || !s.getFieldRequired("rank", 0)) return false;
            local valid = s.validateJson("rank", @"{""level"":2}");
            if (!valid.ok || !eve.result.isChecked(valid)) return false;
            local invalid = s.validateJson("rank", @"{""level"":2.5}");
            if (invalid.ok || !eve.result.isChecked(invalid)) return false;
            if (s.getValidationErrorCode(0) != "type") return false;
            local accepted = s.validateJsonVersioned("rank", 3, @"{""level"":2}");
            if (!accepted.ok || accepted.code != "applied" || !eve.result.isChecked(accepted)) return false;
            local rejected = s.validateJsonVersioned("rank", 3, @"{""level"":2.5}");
            return !rejected.ok && rejected.diagnostics.len() == 1 &&
                   rejected.diagnostics[0].code == "invalid_argument" &&
                   rejected.diagnostics[0].path == "/level" && eve.result.isChecked(rejected);
        }
    )");
    vm.run(script);
    REQUIRE(vm.callFunc(vm.findFunc("schemaBindingTest"), vm).toBool());
}

TEST_CASE("schema.module.reflectFromClass") {
    SchemaRegistry::clear();
    ssq::VM vm(1024, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    auto script = vm.compileSource(R"(
        class CardDefinition </ id="card.definition", version=1, title="Card Definition", additionalProperties=false /> {
            </ required=true />
            id = ""
            </ required=true />
            name = ""
            </ values=["creature","spell"] />
            kind = "creature"
            </ min=0, max=20 />
            cost = 0
            </ min=0, max=99 />
            attack = 0
            </ min=0, max=99 />
            health = 0
        }

        function schemaReflectTest() {
            local s = eve.Schema();
            s.clear();
            if (!s.registerFromClass(CardDefinition)) return s.getLastError();
            if (s.getSchemaVersion("card.definition") != 1) return "version";
            if (s.getSchemaTitle("card.definition") != "Card Definition") return "title";
            if (s.getSchemaAdditionalProperties("card.definition")) return "additionalProperties";
            if (s.getFieldCount("card.definition") != 6) return "count";
            // Resolve by name, not by position. registerFromClass walks the class's
            // SQTable of members, so the enumeration order is the table's order, not the
            // declaration order (measured: attack, health, name, id, kind, cost). The API
            // promises getFieldName is stable, not that index 3 is `cost`, so index-based
            // expectations were the unfounded part. Looking names up also asserts that all
            // six declared fields are reflected at all.
            local function indexOf(field) {
                for (local i = 0; i < s.getFieldCount("card.definition"); ++i)
                    if (s.getFieldName("card.definition", i) == field) return i;
                return -1;
            }
            local idIdx = indexOf("id");
            local kindIdx = indexOf("kind");
            local costIdx = indexOf("cost");
            if (idIdx < 0 || kindIdx < 0 || costIdx < 0) return "names";
            if (s.getFieldType("card.definition", costIdx) != "integer") return "type";
            if (s.getFieldType("card.definition", idIdx) != "string") return "stringType";
            if (!s.getFieldRequired("card.definition", idIdx)) return "required";
            if (s.getFieldEnumCount("card.definition", kindIdx) != 2) return "enum";
            if (!s.getFieldHasMinimum("card.definition", costIdx)) return "min";
            if (s.getFieldMaximum("card.definition", costIdx) != 20) return "max";
            local ok = @"{""id"":""card.scout"",""name"":""Scout"",""kind"":""creature"",""cost"":2,""attack"":3,""health"":2}";
            // validateJson returns the projected Result table {ok, code, status, ...}, not a
            // bool -- the Result convention forbids collapsing a Result to bool. Testing the
            // table itself for truthiness always said "accepted", because a Squirrel table is
            // truthy; that is what made this case look like a validation bug.
            local okResult = s.validateJson("card.definition", ok);
            if (!okResult.ok) return "valid";
            local bad = @"{""id"":""x"",""name"":""y"",""kind"":""air"",""cost"":25}";
            local badResult = s.validateJson("card.definition", bad);
            if (badResult.ok) return "invalid";
            // Assert the constraint classes that must reject, not the first error code:
            // field iteration order is the class table's order, so "error 0" is not stable.
            local sawEnum = false;
            local sawMaximum = false;
            for (local i = 0; i < s.getValidationErrorCount(); ++i) {
                local code = s.getValidationErrorCode(i);
                if (code == "enum") sawEnum = true;
                if (code == "maximum") sawMaximum = true;
            }
            if (!sawEnum) return "enumCode";
            return sawMaximum;
        }
    )");
    vm.run(script);
    // The script returns `true` on success and a reason string on failure, so a plain
    // .toBool() would throw a cast error and hide the reason.
    const ssq::Object reflected = vm.callFunc(vm.findFunc("schemaReflectTest"), vm);
    if (reflected.getType() == ssq::Type::STRING) {
        std::cerr << "[script] schemaReflectTest failed: " << reflected.toString() << std::endl;
    }
    REQUIRE(reflected.getType() == ssq::Type::BOOL);
    REQUIRE(reflected.toBool());
}
}  // namespace test_schema
#endif
#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_script_compiler)
#include "PrivateTestScope.h"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include "common/BindingContracts.h"
#include "common/Runtime.h"
#include "common/ScriptCompiler.h"
#include "common/ScriptModule.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// script_compiler.cpp
namespace test_script_compiler {
using namespace eve;

TEST_CASE("scriptCompiler.recordsErasedLanguageMetadata") {
    Runtime runtime(512, ssq::Libs::ALL);
    runtime.compileSource(
        "persist score: int = 0\n"
        "export function answer(value: int) -> int { return value + score }\n",
        "game:/metadata.nut");

    const auto metadata = runtime.scriptCompiler().metadata("game:/metadata.nut");
    REQUIRE(metadata.has_value());
    CHECK(!metadata->get().sourceHash.empty());
    CHECK_EQ(metadata->get().persistRoots.size(), size_t(1));
    CHECK_EQ(metadata->get().persistRoots[0], std::string("score"));
    CHECK_EQ(metadata->get().exports.size(), size_t(1));
    CHECK_EQ(metadata->get().exports[0], std::string("answer"));
    CHECK_EQ(metadata->get().sourceMap.originalPosition({2, 9}).line, uint32_t(2));
}


TEST_CASE("scriptCompiler.mapsDebuggerLocationsBidirectionally") {
    Runtime runtime(512, ssq::Libs::ALL);
    runtime.compileSource("local value = 1\n", "game:/scripts/mapped.nut");

    script::ScriptSourceMap map;
    map.entries.push_back({{1, 1}, {1, 1}});
    map.entries.push_back({{10, 1}, {3, 1}});
    REQUIRE(runtime.scriptCompiler().setSourceMap("game:/scripts/mapped.nut", std::move(map)));

    const auto original = script::ScriptCompiler::toOriginalPosition("C:/games/demo/scripts/mapped.nut", {12, 7});
    CHECK_EQ(original.line, uint32_t(5));
    CHECK_EQ(original.column, uint32_t(7));

    const auto generated = script::ScriptCompiler::toGeneratedPosition("game:/scripts/mapped.nut", {5, 7});
    CHECK_EQ(generated.line, uint32_t(12));
    CHECK_EQ(generated.column, uint32_t(7));
}


TEST_CASE("scriptCompiler.generatedModuleContractIsExecutable") {
    const std::filesystem::path generated =
        std::filesystem::path(EVENGINE_TEST_BINARY_DIR).parent_path() / "src" / "scripts" / "module_list.nut";
    std::ifstream     input(generated, std::ios::binary);
    const std::string source(std::istreambuf_iterator<char>(input), {});
    CHECK(!source.empty());

    Runtime runtime(1024, ssq::Libs::ALL);
    runtime.runSource(source, "module_list.nut");
    ssq::Array contract(runtime.root().find("eve_module_contract"));
    CHECK(contract.size() > 0);
}

TEST_CASE("scriptCompiler.bindingContractEnablesNativeNamedArguments") {
    Runtime runtime(512, ssq::Libs::ALL);
    runtime.vm().addFunc("nativeCompose",
                         [](int first, int second, int third) { return first * 100 + second * 10 + third; });
    script::BindingContract contract;
    contract.module     = "test";
    contract.method     = "nativeCompose";
    contract.parameters = {{"first", "int"}, {"second", "int"}, {"third", "int"}};
    contract.returnType = "int";
    runtime.scriptCompiler().bindings().registerContract(std::move(contract));

    runtime.runSource("native_named_result <- nativeCompose(third: 3, first: 1, second: 2)\n", "native-named-test.nut");
    const auto result = runtime.vm().get<int64_t>("native_named_result");
    CHECK_EQ(result, int64_t(123));
}

TEST_CASE("scriptCompiler.nestedBindingCallsKeepOuterSignatureStable") {
    Runtime runtime(512, ssq::Libs::ALL);
    runtime.vm().addFunc("nativeInner", [](int value) { return value + 1; });
    runtime.vm().addFunc("nativeOuter", [](int first, int second) { return first * 10 + second; });

    script::BindingContract inner;
    inner.module     = "test";
    inner.method     = "nativeInner";
    inner.parameters = {{"value", "int"}};
    inner.returnType = "int";
    runtime.scriptCompiler().bindings().registerContract(std::move(inner));

    script::BindingContract outer;
    outer.module     = "test";
    outer.method     = "nativeOuter";
    outer.parameters = {{"first", "int"}, {"second", "int"}};
    outer.returnType = "int";
    runtime.scriptCompiler().bindings().registerContract(std::move(outer));

    runtime.runSource("nested_binding_result <- nativeOuter(nativeInner(4), 7)\n", "nested-binding-signature-test.nut");
    CHECK_EQ(runtime.vm().get<int64_t>("nested_binding_result"), int64_t(57));
}

TEST_CASE("scriptCompiler.generatedContractsDriveSimpleSquirrelNamedArguments") {
    Runtime    runtime(512, ssq::Libs::ALL);
    const auto contracts = runtime.scriptCompiler().bindings().snapshot();
    CHECK(contracts.size() > size_t(100));

    const script::BindingContract* generated =
        runtime.scriptCompiler().bindings().find("animation/AnimRetargetProfile.setNormalizedNameMatching");
    REQUIRE(generated != nullptr);
    REQUIRE_EQ(generated->parameters.size(), size_t(1));
    CHECK_EQ(generated->parameters[0].name, std::string("enabled"));
    CHECK_EQ(generated->parameters[0].type, std::string("bool"));

    runtime.vm().addFunc("setNormalizedNameMatching", [](bool enabled) { return enabled; });
    runtime.runSource("generated_contract_result <- setNormalizedNameMatching(enabled: true)\n",
                      "generated-contract-test.nut");
    CHECK(runtime.vm().get<bool>("generated_contract_result"));
}

TEST_CASE("scriptCompiler.bindingContractConvertsUnitLiterals") {
    Runtime runtime(512, ssq::Libs::ALL);
    runtime.vm().addFunc("nativeAngle", [](float angle) { return angle; });
    runtime.vm().addFunc("nativeDelay", [](float delay) { return delay; });

    script::BindingContract angle;
    angle.module     = "test";
    angle.method     = "nativeAngle";
    angle.parameters = {{"angle", "float", false, std::nullopt, script::ScriptUnit::Radians}};
    runtime.scriptCompiler().bindings().registerContract(std::move(angle));
    script::BindingContract delay;
    delay.module     = "test";
    delay.method     = "nativeDelay";
    delay.parameters = {{"delay", "float", false, std::nullopt, script::ScriptUnit::Milliseconds}};
    runtime.scriptCompiler().bindings().registerContract(std::move(delay));

    runtime.runSource(
        "native_angle_result <- nativeAngle(90deg)\n"
        "native_delay_result <- nativeDelay(250ms)\n",
        "native-units-test.nut");
    const float angleResult    = runtime.vm().get<float>("native_angle_result");
    const float delayResult    = runtime.vm().get<float>("native_delay_result");
    const bool  angleConverted = angleResult > 1.5707f && angleResult < 1.5709f;
    CHECK(angleConverted);
    CHECK_EQ(delayResult, 250.0f);
}

TEST_CASE("scriptCompiler.bindingContractChecksStringChoices") {
    Runtime runtime(512, ssq::Libs::ALL);
    runtime.vm().addFunc("nativeMode", [](std::string mode) { return mode; });
    script::BindingContract mode;
    mode.module     = "test";
    mode.method     = "nativeMode";
    mode.parameters = {{"mode", "string", false, std::nullopt, script::ScriptUnit::None, {"idle", "run"}}};
    runtime.scriptCompiler().bindings().registerContract(std::move(mode));
    runtime.runSource("native_mode_result <- nativeMode(\"run\")\n", "native-choice-test.nut");
    const std::string modeResult = runtime.vm().get<std::string>("native_mode_result");
    CHECK_EQ(modeResult, std::string("run"));

    bool rejected = false;
    try {
        runtime.compileSource("nativeMode(\"broken\")\n", "native-choice-error.nut");
    } catch (const ScriptException& error) {
        rejected = std::string(error.what()).find("outside the allowed choices") != std::string::npos;
    }
    CHECK(rejected);
}


TEST_CASE("scriptCompiler.retainsStructuredDiagnosticsAfterFailure") {
    Runtime runtime(512, ssq::Libs::ALL);
    bool    rejected = false;
    try {
        runtime.compileSource("function invalid() { return await 1 }\n", "game:/invalid.nut");
    } catch (const ScriptException&) {
        rejected = true;
    }
    CHECK(rejected);
    const auto metadata = runtime.scriptCompiler().metadata("game:/invalid.nut");
    REQUIRE(metadata.has_value());
    CHECK_EQ(metadata->get().diagnostics.size(), size_t(1));
    CHECK_EQ(metadata->get().diagnostics[0].code, std::string("EVE2601"));
    CHECK_EQ(metadata->get().diagnostics[0].position.line, uint32_t(1));
    CHECK(!metadata->get().diagnostics[0].fix.empty());
}

TEST_CASE("scriptCompiler.requiresPluginAnnotationsToBeRegistered") {
    Runtime runtime(512, ssq::Libs::ALL);
    bool    rejected = false;
    try {
        runtime.compileSource("class Asset { @plugin_asset(\"texture\") path = \"\" }\n", "game:/asset-error.nut");
    } catch (const ScriptException&) {
        rejected = true;
    }
    CHECK(rejected);

    runtime.scriptCompiler().registerAnnotation("plugin_asset");
    runtime.runSource("class Asset { @plugin_asset(\"texture\") path = \"\" }\n", "game:/asset.nut");
    CHECK(runtime.scriptCompiler().unregisterAnnotation("plugin_asset"));
}

TEST_CASE("scriptCompiler.servesLanguageToolingQueries") {
    Runtime runtime(512, ssq::Libs::ALL);
    runtime.compileSource("local playerSpeed: float = 2.0\n", "game:/tooling.nut");
    script::BindingContract contract;
    contract.module          = "physics";
    contract.method          = "toolingNewWorld";
    contract.returnType      = "World";
    contract.documentationId = "physics.toolingNewWorld";
    contract.parameters      = {{"gravity", "float"}, {"sleep", "bool"}};
    runtime.scriptCompiler().bindings().registerContract(std::move(contract));

    const auto scriptItems = runtime.scriptCompiler().completions("game:/tooling.nut", "player");
    CHECK_EQ(scriptItems.size(), size_t(1));
    CHECK_EQ(scriptItems[0].label, std::string("playerSpeed"));
    const auto nativeItems = runtime.scriptCompiler().completions("game:/tooling.nut", "toolingNew");
    CHECK_EQ(nativeItems.size(), size_t(1));
    CHECK(nativeItems[0].insertText.find("gravity:") != std::string::npos);

    const auto hover = runtime.scriptCompiler().hover("game:/tooling.nut", "toolingNewWorld");
    REQUIRE(hover.has_value());
    CHECK(hover->markdown.find("physics.toolingNewWorld") != std::string::npos);
    CHECK(runtime.scriptCompiler().diagnostics("game:/tooling.nut").empty());
}


TEST_CASE("scriptCompiler.bindingContractChecksLiteralTypes") {
    Runtime runtime(512, ssq::Libs::ALL);
    runtime.vm().addFunc("nativeCount", [](int count) { return count; });
    script::BindingContract contract;
    contract.module     = "test";
    contract.method     = "nativeCount";
    contract.returnType = "int";
    contract.parameters = {{"count", "int", false}};
    runtime.scriptCompiler().bindings().registerContract(std::move(contract));

    bool rejected = false;
    try {
        runtime.compileSource("nativeCount(\"many\")\n", "game:/type-error.nut");
    } catch (const ScriptException& error) {
        rejected = std::string(error.what()).find("not assignable") != std::string::npos;
    }
    CHECK(rejected);
}

TEST_CASE("scriptCompiler.bindingContractScopesLiteralChecksToWholeArgument") {
    Runtime runtime(512, ssq::Libs::ALL);
    runtime.vm().addFunc("nativeText", [](std::string text) { return text; });
    script::BindingContract contract;
    contract.module     = "test";
    contract.method     = "nativeText";
    contract.returnType = "string";
    contract.parameters = {{"text", "string", false}};
    runtime.scriptCompiler().bindings().registerContract(std::move(contract));

    runtime.runSource("compound_contract_result <- nativeText(\"count \" + (1 + 2))\n", "game:/compound-contract.nut");
    CHECK_EQ(runtime.vm().get<std::string>("compound_contract_result"), std::string("count 3"));
}


TEST_CASE("scriptCompiler.compilesRepositoryNutCompatibilityBaseline") {
#if !defined(EVENGINE_ANDROID) && !defined(EVENGINE_IOS)
    // Keep corpus identities virtual while resolving imports against the same repository tree.
    class CorpusModules final : public script::IScriptModuleProvider {
    public:
        script::ScriptModuleStatus resolve(const script::ScriptModuleRequest& request, std::string& canonicalUri,
                                           std::string& error) override {
            if (!script::ScriptModuleResolver::canonicalize(request, canonicalUri, error))
                return script::ScriptModuleStatus::Error;
            return canonicalUri.rfind("baseline:/", 0) == 0 ? script::ScriptModuleStatus::Found
                                                            : script::ScriptModuleStatus::NotHandled;
        }
        script::ScriptModuleStatus load(std::string_view canonicalUri, script::ScriptModuleSource& source,
                                        std::string& error) override {
            if (canonicalUri.rfind("baseline:/", 0) != 0) return script::ScriptModuleStatus::NotHandled;
            const auto    path = std::filesystem::path(EVENGINE_SOURCE_DIR) / std::string(canonicalUri.substr(10));
            std::ifstream input(path, std::ios::binary);
            if (!input) {
                error = "missing corpus module: " + path.string();
                return script::ScriptModuleStatus::Error;
            }
            source.canonicalUri = std::string(canonicalUri);
            source.utf8Source.assign(std::istreambuf_iterator<char>(input), {});
            source.debugOrigin = path.string();
            return script::ScriptModuleStatus::Found;
        }
    };
    const std::filesystem::path root(EVENGINE_SOURCE_DIR);
    size_t                      compiled = 0;
    using Clock                          = std::chrono::steady_clock;
    const auto start                     = Clock::now();
    double     readMs = 0, vmMs = 0, compileMs = 0, destroyMs = 0;
    const auto milliseconds = [](auto duration) { return std::chrono::duration<double, std::milli>(duration).count(); };
    for (std::filesystem::recursive_directory_iterator it(root), end; it != end; ++it) {
        if (it->is_directory()) {
            const std::string name = it->path().filename().string();
            if (name == ".git" || name == "build" || name == "third-party") it.disable_recursion_pending();
            continue;
        }
        if (it->path().extension() != ".nut") continue;
        const auto    readStart = Clock::now();
        std::ifstream input(it->path(), std::ios::binary);
        REQUIRE(input.good());
        const std::string source(std::istreambuf_iterator<char>(input), {});
        const std::string relative = std::filesystem::relative(it->path(), root).generic_string();
        readMs += milliseconds(Clock::now() - readStart);
        try {
            auto phaseStart = Clock::now();
            {
                Runtime runtime(1024, ssq::Libs::ALL);
                REQUIRE(runtime.scriptModules().registerProvider(std::make_shared<CorpusModules>()) != 0);
                const auto ready = Clock::now();
                vmMs += milliseconds(ready - phaseStart);
                runtime.compileSource(source, "baseline:/" + relative);
                phaseStart = Clock::now();
                compileMs += milliseconds(phaseStart - ready);
            }
            destroyMs += milliseconds(Clock::now() - phaseStart);
        } catch (const std::exception& error) {
            throw std::runtime_error(relative + ": " + error.what());
        }
        ++compiled;
    }
    REQUIRE(compiled >= size_t(150));
    const double totalMs = milliseconds(Clock::now() - start);
    std::fprintf(
        stderr,
        "Script corpus: %zu scripts; walk %.1f ms, read %.1f ms, VM %.1f ms, compile %.1f ms, destroy %.1f ms\n",
        compiled, totalMs - readMs - vmMs - compileMs - destroyMs, readMs, vmMs, compileMs, destroyMs);
#endif
}
}  // namespace test_script_compiler
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_script_config)
#include "PrivateTestScope.h"
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include "common/Module.h"
#include "common/Runtime.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// script_config.cpp
namespace test_script_config {
using namespace eve;

namespace {

std::string loadStartupThroughModuleValidation() {
    const std::filesystem::path path = std::filesystem::path(EVENGINE_SOURCE_DIR) / "src" / "scripts" / "load.nut";
    std::ifstream               input(path, std::ios::binary);
    std::string                 source(std::istreambuf_iterator<char>(input), {});
    const std::string           marker = "validate_project_modules();";
    const size_t                end    = source.find(marker);
    if (end == std::string::npos) return {};
    source.resize(end + marker.size());
    return source;
}

void configureRuntime(Runtime& runtime) {
    ModuleManager::expose(runtime);
    runtime.runSource("eve.moduleList <- []\n", "config-test-bootstrap.nut");
    runtime.runSource(loadStartupThroughModuleValidation(), "load.nut");
}

}  // namespace

TEST_CASE("scriptConfig.acceptsAbsentModuleRequirements") {
    Runtime runtime(1024, ssq::Libs::ALL);
    configureRuntime(runtime);
    CHECK(runtime.handle() != nullptr);
}

TEST_CASE("scriptConfig.rejectsMissingRequiredModule") {
    Runtime runtime(1024, ssq::Libs::ALL);
    configureRuntime(runtime);
    bool rejected = false;
    try {
        runtime.runSource("config.modules <- [\"missing_system\"]\nvalidate_project_modules()\n",
                          "config-validation-test.nut");
    } catch (const ScriptException& error) {
        rejected = std::string(error.what()).find("required module is missing: missing_system") != std::string::npos;
    }
    CHECK(rejected);
}

TEST_CASE("scriptConfig.validatesOptionalModuleDeclarationShape") {
    Runtime runtime(1024, ssq::Libs::ALL);
    configureRuntime(runtime);
    bool rejected = false;
    try {
        runtime.runSource("config.optionalModules <- \"audio\"\nvalidate_project_modules()\n",
                          "config-validation-test.nut");
    } catch (const ScriptException& error) {
        rejected = std::string(error.what()).find("config.optionalModules must be an array") != std::string::npos;
    }
    CHECK(rejected);
}

TEST_CASE("scriptConfig.instantiatesOnlyConfiguredModules") {
    Runtime runtime(1024, ssq::Libs::ALL);
    configureRuntime(runtime);
    runtime.runSource(
        "eve.Foo <- class { n = 1 }\n"
        "eve.Bar <- class { n = 2 }\n"
        "eve.moduleList = [{ slot = \"foo\", cls = \"Foo\" }, { slot = \"bar\", cls = \"Bar\" }]\n"
        "config.modules <- [\"foo\"]\n"
        "instantiate_configured_modules()\n"
        "_filtered_ok <- has_module(\"foo\") && !has_module(\"bar\")\n",
        "config-filter-test.nut");
    CHECK(runtime.vm().get<bool>("_filtered_ok"));
}

TEST_CASE("scriptConfig.ensureModuleLoadsOmittedSlot") {
    Runtime runtime(1024, ssq::Libs::ALL);
    configureRuntime(runtime);
    runtime.runSource(
        "eve.Foo <- class { n = 1 }\n"
        "eve.Bar <- class { n = 2 }\n"
        "eve.moduleList = [{ slot = \"foo\", cls = \"Foo\" }, { slot = \"bar\", cls = \"Bar\" }]\n"
        "config.modules <- [\"foo\"]\n"
        "instantiate_configured_modules()\n"
        "_ensured <- ensure_module(\"bar\") && has_module(\"bar\")\n",
        "config-ensure-test.nut");
    CHECK(runtime.vm().get<bool>("_ensured"));
}
}  // namespace test_script_config
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_script_module)
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include "PrivateTestScope.h"
#include "common/Capability.h"
#include "common/Runtime.h"
#include "common/ScriptCompiler.h"
#include "common/ScriptModule.h"
#include "common/ServiceInterfaces.h"
#include "filesystem/physfs/FileApi.h"
#include "property_access/PropertyAccess.h"
#include "property_access/squirrel/ReflectedPropertyModel.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// script_module.cpp
namespace test_script_module {
// script_module.cpp
using namespace eve;

namespace {

class MemoryModuleProvider final : public script::IScriptModuleProvider {
public:
    std::unordered_map<std::string, std::string> sources;
    std::unordered_map<std::string, int>         loads;

    script::ScriptModuleStatus resolve(const script::ScriptModuleRequest& request, std::string& canonicalUri,
                                       std::string& error) override {
        if (!script::ScriptModuleResolver::canonicalize(request, canonicalUri, error))
            return script::ScriptModuleStatus::Error;
        return canonicalUri.rfind("mem:/", 0) == 0 ? script::ScriptModuleStatus::Found
                                                   : script::ScriptModuleStatus::NotHandled;
    }

    script::ScriptModuleStatus load(std::string_view canonicalUri, script::ScriptModuleSource& source,
                                    std::string& error) override {
        if (canonicalUri.rfind("mem:/", 0) != 0) return script::ScriptModuleStatus::NotHandled;
        const auto found = sources.find(std::string(canonicalUri));
        if (found == sources.end()) {
            error = "missing memory module: " + std::string(canonicalUri);
            return script::ScriptModuleStatus::Error;
        }
        ++loads[found->first];
        source.canonicalUri = found->first;
        source.utf8Source   = found->second;
        source.contentHash  = std::to_string(std::hash<std::string>{}(found->second));
        source.debugOrigin  = "memory-test";
        return script::ScriptModuleStatus::Found;
    }
};

class VirtualFileSystem final : public service::IFileSystem {
public:
    std::unordered_map<std::string, std::string> files;

    bool readFile(const std::string& path, std::vector<uint8_t>& output) override {
        const auto found = files.find(path);
        if (found == files.end()) return false;
        output.assign(found->second.begin(), found->second.end());
        return true;
    }
    bool writeFile(const std::string&, const void*, size_t) override { return false; }
    bool fileExists(const std::string& path) override { return files.find(path) != files.end(); }
};

}  // namespace

TEST_CASE("scriptModule.defaultProviderUsesPlatformVirtualFileSystem") {
    cap::detail::clearAllRaw();
    VirtualFileSystem filesystem;
    filesystem.files["scripts/value.nut"] = "export const VALUE = 42\n";
    cap::provide<service::IFileSystem>(&filesystem);
    {
        Runtime runtime(512, ssq::Libs::ALL);
        runtime.runSource(
            "import { VALUE } from \"game:/scripts/value.nut\"\n"
            "vfs_module_result <- VALUE\n",
            "game:/main.nut");
        CHECK_EQ(runtime.vm().get<int64_t>("vfs_module_result"), int64_t(42));
    }
    cap::detail::clearAllRaw();
}

TEST_CASE("scriptModule.resolvesCachesAndInstantiatesDependencyGraph") {
    Runtime runtime(512, ssq::Libs::ALL);
    auto    provider                     = std::make_shared<MemoryModuleProvider>();
    provider->sources["mem:/values.nut"] = "export const VALUE = 40\n";
    provider->sources["mem:/answer.nut"] =
        "import { VALUE } from \"mem:/values.nut\"\n"
        "export function answer() -> int { return VALUE + 2 }\n";
    runtime.scriptModules().registerProvider(provider, 100);

    runtime.runSource(
        "import { answer } from \"mem:/answer.nut\"\n"
        "import { answer as answer_again } from \"mem:/answer.nut\"\n"
        "module_result <- answer() + answer_again()\n",
        "game:/main.nut");

    const auto moduleResult = runtime.vm().get<int64_t>("module_result");
    CHECK_EQ(moduleResult, int64_t(84));
    CHECK_EQ(provider->loads["mem:/answer.nut"], 1);
    CHECK_EQ(provider->loads["mem:/values.nut"], 1);
    const auto dependencies = runtime.scriptModules().dependencies("game:/main.nut");
    CHECK_EQ(dependencies.size(), size_t(1));
    CHECK_EQ(dependencies[0], std::string("mem:/answer.nut"));
    const auto reverse = runtime.scriptModules().reverseDependencies("mem:/answer.nut");
    CHECK_EQ(reverse.size(), size_t(1));
    CHECK_EQ(reverse[0], std::string("game:/main.nut"));
    const auto metadata = runtime.scriptCompiler().metadata("game:/main.nut");
    REQUIRE(metadata.has_value());
    CHECK_EQ(metadata->get().dependencies, dependencies);
}

TEST_CASE("scriptModule.rejectsCyclesBeforeExecution") {
    Runtime runtime(512, ssq::Libs::ALL);
    auto    provider                = std::make_shared<MemoryModuleProvider>();
    provider->sources["mem:/a.nut"] = "import { b } from \"mem:/b.nut\"\nexport function a() { return b() }\n";
    provider->sources["mem:/b.nut"] = "import { a } from \"mem:/a.nut\"\nexport function b() { return a() }\n";
    runtime.scriptModules().registerProvider(provider, 100);

    bool rejected = false;
    try {
        runtime.compileSource("import { a } from \"mem:/a.nut\"\n", "game:/main.nut");
    } catch (const ScriptException& error) {
        rejected = std::string(error.what()).find("cyclic script import") != std::string::npos;
    }
    REQUIRE(rejected);
}

TEST_CASE("scriptModule.reloadReplacesGenerationAtomically") {
    Runtime runtime(512, ssq::Libs::ALL);
    auto    provider                    = std::make_shared<MemoryModuleProvider>();
    provider->sources["mem:/value.nut"] = "export function value() { return 1 }\n";
    runtime.scriptModules().registerProvider(provider, 100);
    runtime.runSource("import { value } from \"mem:/value.nut\"\nfirst <- value()\n", "game:/first.nut");
    CHECK_EQ(runtime.vm().get<int64_t>("first"), int64_t(1));

    provider->sources["mem:/value.nut"] = "export function value( {\n";
    bool failed                         = false;
    try {
        runtime.scriptModules().reload("mem:/value.nut");
    } catch (const std::exception&) {
        failed = true;
    }
    CHECK(failed);
    runtime.runSource("import { value } from \"mem:/value.nut\"\nold_generation <- value()\n",
                      "game:/old-generation.nut");
    CHECK_EQ(runtime.vm().get<int64_t>("old_generation"), int64_t(1));

    provider->sources["mem:/value.nut"] = "export function value() { return 2 }\n";
    runtime.scriptModules().reload("mem:/value.nut");
    runtime.runSource("import { value } from \"mem:/value.nut\"\nnew_generation <- value()\n",
                      "game:/new-generation.nut");
    CHECK_EQ(runtime.vm().get<int64_t>("new_generation"), int64_t(2));
}

TEST_CASE("scriptModule.reloadAffectedRebuildsReverseDependencyGenerations") {
    Runtime runtime(512, ssq::Libs::ALL);
    auto    provider                    = std::make_shared<MemoryModuleProvider>();
    provider->sources["mem:/value.nut"] = "export function value() { return 1 }\n";
    provider->sources["mem:/user.nut"] =
        "import { value } from \"mem:/value.nut\"\nexport function answer() { return value() }\n";
    runtime.scriptModules().registerProvider(provider, 100);
    runtime.runSource("import { answer } from \"mem:/user.nut\"\nfirst_answer <- answer()\n", "game:/first.nut");
    CHECK_EQ(runtime.vm().get<int64_t>("first_answer"), int64_t(1));

    provider->sources["mem:/value.nut"] = "export function value() { return 2 }\n";
    const auto affected                 = runtime.scriptModules().reloadAffected("mem:/value.nut");
    CHECK_EQ(affected.size(), size_t(2));
    runtime.runSource("import { answer } from \"mem:/user.nut\"\nsecond_answer <- answer()\n", "game:/second.nut");
    CHECK_EQ(runtime.vm().get<int64_t>("second_answer"), int64_t(2));
}

TEST_CASE("scriptModule.reloadAffectedRollsBackWholeGenerationGroup") {
    Runtime runtime(512, ssq::Libs::ALL);
    auto    provider                    = std::make_shared<MemoryModuleProvider>();
    provider->sources["mem:/value.nut"] = "export function value() { return 3 }\n";
    provider->sources["mem:/user.nut"] =
        "import { value } from \"mem:/value.nut\"\nexport function answer() { return value() }\n";
    runtime.scriptModules().registerProvider(provider, 100);
    runtime.runSource("import { answer } from \"mem:/user.nut\"\nbefore_failure <- answer()\n", "game:/before.nut");

    provider->sources["mem:/value.nut"] = "export function value() { return 4 }\n";
    provider->sources["mem:/user.nut"]  = "export function answer( {\n";
    bool failed                         = false;
    try {
        runtime.scriptModules().reloadAffected("mem:/value.nut");
    } catch (const std::exception&) {
        failed = true;
    }
    REQUIRE(failed);
    runtime.runSource("import { answer } from \"mem:/user.nut\"\nafter_failure <- answer()\n", "game:/after.nut");
    CHECK_EQ(runtime.vm().get<int64_t>("after_failure"), int64_t(3));
}

TEST_CASE("scriptModule.instantiationFailureCarriesTheScriptError") {
    Runtime runtime(512, ssq::Libs::ALL);
    auto    provider                     = std::make_shared<MemoryModuleProvider>();
    provider->sources["mem:/broken.nut"] = "export const VALUE = 1\nthrow \"module body exploded\"\n";
    runtime.scriptModules().registerProvider(provider, 100);

    std::string message;
    try {
        runtime.runSource("import { VALUE } from \"mem:/broken.nut\"\n", "game:/main.nut");
    } catch (const std::exception &error) {
        message = error.what();
    }

    // The module body executes from native code, and its failure used to be thrown away:
    // the caller got "failed to instantiate script module: mem:/broken.nut" and nothing
    // about the throw. The reported text now travels with the exception. Both assertions
    // are REQUIRE: as CHECK they were warning-level and this test passed with the report
    // removed.
    REQUIRE(!message.empty());
    REQUIRE(message.find("failed to instantiate script module") != std::string::npos);
    REQUIRE(message.find("module body exploded") != std::string::npos);
}

TEST_CASE("scriptModule.dofileInstantiatesImports") {
    cap::detail::clearAllRaw();
    VirtualFileSystem filesystem;
    filesystem.files["lib/value.nut"] = "export const VALUE = 41\n";
    cap::provide<service::IFileSystem>(&filesystem);

    const auto root =
        std::filesystem::temp_directory_path() /
        ("eve_dofile_import_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root);
    const auto entry = root / "entry.nut";
    {
        std::ofstream out(entry, std::ios::binary);
        out << "import { VALUE } from \"game:/lib/value.nut\"\n"
               "imported_from_dofile <- VALUE + 1\n";
    }

    Runtime runtime(512, ssq::Libs::ALL);
    eve::filesystem::physfs::installScriptFileApi(runtime.vm());
    const std::string loader = "dofile(\"" + entry.generic_string() + "\")\n";
    runtime.runSource(loader, "game:/loader.nut");
    CHECK_EQ(runtime.vm().get<int64_t>("imported_from_dofile"), int64_t(42));

    std::filesystem::remove_all(root);
    cap::detail::clearAllRaw();
}

// script_property_model.cpp


using namespace eve;
using namespace eve::property_access;

namespace {

const char* kModelScript = R"SQ(
class ModelBase {
    baseValue = 2
}
class ModelHero extends ModelBase {
    </ editor = "slider", min = 0, max = 100, step = 1,
       label = "Health", tooltip = "Current health" />
    hp = 80.0
    </ editor = "combo", options = "warrior,mage" />
    job = "warrior"
    alive = true
    tags = ["player", "hero"]
    stats = { armor = 3 }
    </ editor = "color" />
    tint = [1.0, 0.5, 0.25, 1.0]
    </ editor = "vec2" />
    size = [2.0, 3.0]
}
)SQ";

}  // namespace

TEST_CASE("property_access.reflection_builds_shared_schema_and_structured_values") {
    Runtime runtime(512, ssq::Libs::ALL);
    runtime.initialize();
    runtime.runSource(kModelScript, "property_access.nut");
    ssq::Object hero = runtime.createInstance("ModelHero");

    ReflectedPropertyModel model(runtime, hero);
    CHECK_EQ(model.schema().typeId, std::string("ModelHero"));

    auto hpRef = model.schema().find("hp");
    REQUIRE(hpRef.has_value());
    const PropertyDescriptor* hp = &hpRef->get();
    CHECK(static_cast<int>(hp->kind) == static_cast<int>(PropertyKind::Number));
    CHECK_EQ(hp->displayName, std::string("Health"));
    CHECK_EQ(hp->description, std::string("Current health"));
    CHECK_EQ(hp->category, std::string("ModelHero"));
    REQUIRE(hp->numeric.minimum.has_value());
    REQUIRE(hp->numeric.maximum.has_value());
    CHECK_EQ(*hp->numeric.minimum, 0.0);
    CHECK_EQ(*hp->numeric.maximum, 100.0);

    auto baseRef = model.schema().find("baseValue");
    REQUIRE(baseRef.has_value());
    const PropertyDescriptor* base = &baseRef->get();
    CHECK_EQ(base->category, std::string("ModelBase"));

    auto jobRef = model.schema().find("job");
    REQUIRE(jobRef.has_value());
    const PropertyDescriptor* job = &jobRef->get();
    CHECK(static_cast<int>(job->kind) == static_cast<int>(PropertyKind::Enum));
    CHECK(job->choices == std::vector<std::string>({"warrior", "mage"}));

    auto tagsRef = model.schema().find("tags");
    REQUIRE(tagsRef.has_value());
    const PropertyDescriptor* tags = &tagsRef->get();
    CHECK(!hasFlag(tags->flags, PropertyFlag::ReadOnly));
    CHECK(static_cast<int>(tags->kind) == static_cast<int>(PropertyKind::Array));
    const std::optional<Value> tagValue = model.read("tags");
    REQUIRE(tagValue.has_value());
    const Value::Array* tagArray = tagValue->getIf<Value::Array>();
    REQUIRE(tagArray != nullptr);
    CHECK_EQ(tagArray->size(), static_cast<std::size_t>(2));

    const std::optional<Value> statsValue = model.read("stats");
    REQUIRE(statsValue.has_value());
    const Value::Object* stats = statsValue->getIf<Value::Object>();
    REQUIRE(stats != nullptr);
    CHECK(stats->contains("armor"));

    auto tintRef = model.schema().find("tint");
    REQUIRE(tintRef.has_value());
    CHECK(static_cast<int>(tintRef->get().kind) == static_cast<int>(PropertyKind::Color));
    CHECK(!hasFlag(tintRef->get().flags, PropertyFlag::ReadOnly));

    auto sizeRef = model.schema().find("size");
    REQUIRE(sizeRef.has_value());
    CHECK(static_cast<int>(sizeRef->get().kind) == static_cast<int>(PropertyKind::Vec2));
}

TEST_CASE("property_access.writes_and_refreshes_through_shared_mvvm_contract") {
    Runtime runtime(512, ssq::Libs::ALL);
    runtime.initialize();
    runtime.runSource(kModelScript, "property_access.nut");
    ssq::Object            hero = runtime.createInstance("ModelHero");
    ReflectedPropertyModel model(runtime, hero);

    std::vector<PropertyChange> changes;
    auto subscription = model.subscribe([&](const PropertyChange& change) { changes.push_back(change); });

    CHECK(model.write("hp", Value(42.0)).ok());
    CHECK_EQ(runtime.readProperty(hero, "hp").asFloat(), 42.0);
    CHECK(!changes.empty());
    CHECK_EQ(changes.back().path, std::string("hp"));

    CHECK(!model.write("hp", Value(101.0)).ok());
    CHECK(!model.write("hp", Value("fast")).ok());
    CHECK_EQ(runtime.readProperty(hero, "hp").asFloat(), 42.0);

    Value::Array tags = {Value(std::string("player")), Value(std::string("elite"))};
    CHECK(model.write("tags", Value(tags)).ok());
    CHECK_EQ(runtime.arraySize(hero, "tags"), static_cast<std::size_t>(2));
    CHECK_EQ(runtime.arrayGet(hero, "tags", 1).asString(), std::string("elite"));

    Value::Array tint = {Value(0.1), Value(0.2), Value(0.3), Value(0.4)};
    CHECK(model.write("tint", Value(tint)).ok());
    CHECK(std::fabs(runtime.arrayGet(hero, "tint", 2).asFloat() - 0.3f) < 1e-5f);

    Value::Array badTint = {Value(0.1), Value(0.2)};
    CHECK(!model.write("tint", Value(badTint)).ok());

    Value::Object stats;
    stats.emplace("armor", Value(std::int64_t(9)));
    stats.emplace("resist", Value(1.5));
    CHECK(model.write("stats", Value(stats)).ok());
    CHECK_EQ(runtime.tableGet(hero, "stats", "armor").asInt(), static_cast<std::int64_t>(9));
    CHECK_EQ(runtime.tableGet(hero, "stats", "resist").asFloat(), 1.5);

    ReflectedValue external;
    external.kind    = ReflectedValueKind::Bool;
    external.boolean = false;
    REQUIRE(runtime.writeProperty(hero, "alive", external));
    const std::size_t before = changes.size();
    model.refresh();
    REQUIRE(changes.size() > before);
    CHECK_EQ(changes.back().path, std::string("alive"));
    const bool* alive = changes.back().value.getIf<bool>();
    REQUIRE(alive != nullptr);
    CHECK(!*alive);
}

TEST_CASE("property_access.script_validation_matches_shared_contract") {
    Runtime runtime(512, ssq::Libs::ALL);
    runtime.initialize();
    runtime.runSource(kModelScript, "property_access.nut");
    ssq::Object            hero = runtime.createInstance("ModelHero");
    ReflectedPropertyModel model(runtime, hero);

    auto hpRef   = model.schema().find("hp");
    auto jobRef  = model.schema().find("job");
    auto tintRef = model.schema().find("tint");
    REQUIRE(hpRef.has_value());
    REQUIRE(jobRef.has_value());
    REQUIRE(tintRef.has_value());
    const PropertyDescriptor* hp   = &hpRef->get();
    const PropertyDescriptor* job  = &jobRef->get();
    const PropertyDescriptor* tint = &tintRef->get();

    const auto sharedType  = validatePropertyValue(*hp, Value("fast"));
    const auto runtimeType = model.write("hp", Value("fast"));
    CHECK(!sharedType.ok());
    CHECK(!runtimeType.ok());
    CHECK_EQ(writeRule(sharedType), std::string("property_access.property.type"));
    CHECK_EQ(writeRule(runtimeType), std::string("property_access.script.type"));

    const auto sharedFinite  = validatePropertyValue(*hp, Value(std::numeric_limits<double>::quiet_NaN()));
    const auto runtimeFinite = model.write("hp", Value(std::numeric_limits<double>::quiet_NaN()));
    CHECK(!sharedFinite.ok());
    CHECK(!runtimeFinite.ok());
    CHECK_EQ(writeRule(sharedFinite), std::string("property_access.property.finite"));
    CHECK_EQ(writeRule(runtimeFinite), std::string("property_access.script.finite"));

    const auto sharedMinimum  = validatePropertyValue(*hp, Value(-1.0));
    const auto runtimeMinimum = model.write("hp", Value(-1.0));
    CHECK(!sharedMinimum.ok());
    CHECK(!runtimeMinimum.ok());
    CHECK_EQ(writeRule(sharedMinimum), std::string("property_access.property.minimum"));
    CHECK_EQ(writeRule(runtimeMinimum), std::string("property_access.script.minimum"));

    const auto sharedMaximum  = validatePropertyValue(*hp, Value(101.0));
    const auto runtimeMaximum = model.write("hp", Value(101.0));
    CHECK(!sharedMaximum.ok());
    CHECK(!runtimeMaximum.ok());
    CHECK_EQ(writeRule(sharedMaximum), std::string("property_access.property.maximum"));
    CHECK_EQ(writeRule(runtimeMaximum), std::string("property_access.script.maximum"));

    const auto sharedChoice  = validatePropertyValue(*job, Value("rogue"));
    const auto runtimeChoice = model.write("job", Value("rogue"));
    CHECK(!sharedChoice.ok());
    CHECK(!runtimeChoice.ok());
    CHECK_EQ(writeRule(sharedChoice), std::string("property_access.property.choice"));
    CHECK_EQ(writeRule(runtimeChoice), std::string("property_access.script.choice"));

    Value::Array badArity     = {Value(1.0), Value(2.0)};
    const auto   sharedArity  = validatePropertyValue(*tint, Value(badArity));
    const auto   runtimeArity = model.write("tint", Value(badArity));
    CHECK(!sharedArity.ok());
    CHECK(!runtimeArity.ok());
    CHECK_EQ(writeRule(sharedArity), std::string("property_access.property.arity"));
    CHECK_EQ(writeRule(runtimeArity), std::string("property_access.script.arity"));
}
}  // namespace test_script_module
#endif
#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_script_ownership)
#include <memory>
#include <simplesquirrel/simplesquirrel.hpp>
#include <string>
#include "PrivateTestScope.h"
#include "authority/Authority.h"
#include "common/SquirrelOwnership.h"
#include "effects/Effects.h"
#include "npc_ai/Decision.h"
#include "orders/CommandQueue.h"
#include "procgen/Procgen.h"
#include "production/Production.h"
#include "statepatch/StatePatch.h"
#include "zeroerr/unittest.h"
// script_ownership.cpp
namespace test_script_ownership {
/**
 * @file script_ownership.cpp
 * @brief Contract tests for Value/Owned/Borrowed script object semantics.
 */


// A RuntimePin references its producing store by address, so that store must never be
// relocated while a pin can exist. RuntimeObjectRegistry keeps it behind a stable heap
// address and stays movable itself (statepatch::Store move-assigns one).
static_assert(!std::is_move_constructible_v<eve::script::detail::RuntimeSlotStore>);
static_assert(!std::is_move_assignable_v<eve::script::detail::RuntimeSlotStore>);


TEST_CASE("scriptOwnership.ordersAndEffectsOwnedBinding") {
    ssq::VM vm(1024, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        result <- "fail";
        local orders = eve.Orders();
        local oq = orders.newQueueOwned();
        local effects = eve.Effects();
        local ec = effects.newContainer();
        if (oq.ok && ec.ok && oq.value.ownership() == "owned" &&
            ec.value.ownership() == "owned" && !oq.value.isStale() &&
            !ec.value.isStale()) {
            local order = oq.value.append("ownership_probe", 1, 0.0);
            local effect = ec.value.apply("subject", "probe", "test", 1,
                                                  0.0, "probe", "refresh");
            local releasedOrder = oq.value.release();
            local releasedEffect = ec.value.release();
            if (order.ok && effect.ok && releasedOrder.ok && releasedEffect.ok &&
                oq.value.isStale() && ec.value.isStale() &&
                oq.value.release().code == "rejected" &&
                ec.value.release().code == "rejected") result = "ok";
        }
    )"));
    CHECK_EQ(vm.find("result").toString(), std::string("ok"));
}

TEST_CASE("scriptOwnership.procgenAndStateModulesOwnedBindings") {
    ssq::VM vm(1024, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        result <- "fail";
        local procgen = eve.Procgen();
        local params = procgen.newParams();
        local grid = procgen.newGrid(2, 3);
        local context = procgen.beginSystem("ownership_probe", 7);
        local production = eve.Production().newWorkQueue();
        local authority = eve.Authority().newStore();
        local statepatch = eve.StatePatch().newStore();
        local decision = eve.Decision().newContext();
        local pointSet = procgen.newPointSet();
        local output = procgen.newOutput();
        local cloud = procgen.newCloudField();
        local batch = statepatch.ok ? statepatch.value.newBatch() : { ok = false };
        if (params.ok && grid.ok && context.ok && production.ok && authority.ok &&
            statepatch.ok && decision.ok && pointSet.ok && output.ok && cloud.ok && batch.ok &&
            params.value.ownerEpoch() > 0 && pointSet.ownerEpoch > 0 && output.ownerEpoch > 0 &&
            cloud.ownerEpoch > 0 && pointSet.value.empty() && output.value.getTarget() == "" &&
            grid.value.getWidth() == 2 && grid.value.getHeight() == 3 &&
            params.value.setSeed(42).ok && params.value.getSeed() == 42 &&
            grid.value.fill(1).ok && grid.value.getCell(1, 2) == 1 &&
            context.value.getName() == "ownership_probe" &&
            production.value.enqueue("probe", "build", "unit", "{}", 1.0, 0).ok &&
            authority.value.grant("actor", "scope", "cap", "probe", 1, 0.0).ok &&
            statepatch.value.revision() == 0 && decision.value.setState("fsm", "idle").ok &&
            decision.value.state("fsm") == "idle" && batch.value.set("actor", "probe", "true").ok) {
            local pReleased = params.value.release();
            local gReleased = grid.value.release();
            local cReleased = context.value.release();
            local qReleased = production.value.release();
            local aReleased = authority.value.release();
            local bReleased = batch.value.release();
            local sReleased = statepatch.value.release();
            local dReleased = decision.value.release();
            if (pReleased.ok && gReleased.ok && cReleased.ok && qReleased.ok &&
                aReleased.ok && bReleased.ok && sReleased.ok && dReleased.ok &&
                params.value.isStale() && grid.value.isStale() &&
                context.value.isStale() && production.value.isStale() &&
                authority.value.isStale() && statepatch.value.isStale() &&
                batch.value.isStale() && decision.value.isStale()) result = "ok";
        }
    )"));
    CHECK_EQ(vm.find("result").toString(), std::string("ok"));
}

TEST_CASE("scriptOwnership.procgenOwnedGridExposesAssetPlacement") {
    ssq::VM vm(1024, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        result <- "fail";
        local procgen = eve.Procgen();
        local paramsResult = procgen.newParams();
        if (paramsResult.ok) {
            local params = paramsResult.value;
            params.setSize(32, 24);
            params.setSeed(23);
            params.setString("assets.light", "sconce_probe");
            local generated = procgen.generate("level.roguelike", params);
            if (generated.ok) {
                local grid = generated.value;
                for (local i = 0; i < grid.getObjectCount(); ++i) {
                    if (grid.getObjectType(i) == "light" &&
                        grid.getObjectAsset(i) == "sconce_probe" &&
                        grid.getObjectRotation(i) >= 0.0 &&
                        (grid.getObjectFlags(i) & 4) != 0) result = "ok";
                }
            }
        }
    )"));
    CHECK_EQ(vm.find("result").toString(), std::string("ok"));
}
}  // namespace test_script_ownership
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_script_user_doc_examples)
#include "PrivateTestScope.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include "common/Capability.h"
#include "common/Module.h"
#include "common/Runtime.h"
#include "common/ServiceInterfaces.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// script_user_doc_examples.cpp
namespace test_script_user_doc_examples {
namespace {
std::filesystem::path repository() { return std::filesystem::path(EVENGINE_SOURCE_DIR); }
std::string           readSource(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot read documentation fixture: " + path.string());
    std::ostringstream output;
    output << input.rdbuf();
    return output.str();
}
void runChapter(const char* chapter, int selectedBlock = -1, const std::string& prefix = "",
                const std::string& suffix = "") {
    const auto  text = readSource(repository() / "docs/usr/modules" / (std::string(chapter) + ".md"));
    std::string code;
    size_t      position = 0;
    int         block    = 0;
    while ((position = text.find("```squirrel", position)) != std::string::npos) {
        const auto start = text.find('\n', position) + 1;
        const auto end   = text.find("```", start);
        if (end == std::string::npos) throw std::runtime_error("unclosed documentation code fence");
        if (selectedBlock < 0 || selectedBlock == block) code += text.substr(start, end - start) + "\n";
        ++block;
        position = end + 3;
    }
    if (code.empty()) throw std::runtime_error("documentation has no executable example");
    eve::Runtime runtime(2048, ssq::Libs::ALL);
    runtime.initialize();
    runtime.runSource(
        "attributes <- eve.Attributes(); decision <- eve.Decision(); "
        "definitions <- eve.Definitions(); tags <- eve.Tags(); "
        "game_event <- eve.GameEvent(); policies <- eve.PolicyRegistryModule(); "
        "transaction <- eve.Transaction();",
        "doc-fixture");
    runtime.runSource(prefix + "\nfunction checked_doc_example() {\n" + code + suffix + "\n}\nchecked_doc_example();\n",
                      chapter);
}
class TutorialFiles final : public eve::service::IFileSystem {
public:
    bool readFile(const std::string& path, std::vector<uint8_t>& output) override {
        if (path != "scripts/rules.nut") return false;
        const auto source = readSource(repository() / "examples/game-tutorial/scripts/rules.nut");
        output.assign(source.begin(), source.end());
        return true;
    }
    bool writeFile(const std::string&, const void*, size_t) override { return false; }
    bool fileExists(const std::string& path) override { return path == "scripts/rules.nut"; }
};
}  // namespace

TEST_CASE("userDocs.economy") { runChapter("economy"); }
TEST_CASE("userDocs.attributes") { runChapter("attributes"); }
TEST_CASE("userDocs.decision") { runChapter("decision"); }
TEST_CASE("userDocs.definitions") { runChapter("definitions"); }
TEST_CASE("userDocs.tags") { runChapter("tags"); }
TEST_CASE("userDocs.gameEvent") { runChapter("game_event"); }
TEST_CASE("userDocs.policyRegistry") { runChapter("policyregistry"); }
TEST_CASE("userDocs.transaction") { runChapter("transaction"); }
TEST_CASE("userDocs.profiler") { runChapter("profiler"); }
TEST_CASE("userDocs.rope") { runChapter("physics_rope"); }
TEST_CASE("userDocs.ecsLifecycle") {
    runChapter("entity", 1,
               "eve_init <- null; eve_update <- null; eve_render <- null; eve_quit <- null; "
               "config <- {width=960, height=540}; gfx <- {"
               "setBackgroundColor=function(r,g,b,a) {}, clear=function() {}, "
               "drawSolidRect=function(x,y,w,h,r,g,b,a) {}};",
               "\neve_init(); eve_update(1.0); assert(mover.position.x == 200.0); "
               "eve_render(); eve_update(10.0); assert(!mover.isAlive()); "
               "assert(eve.view(Mover).len() == 0); eve_quit();");
}
TEST_CASE("userDocs.tutorialRules") {
    eve::cap::detail::clearAllRaw();
    TutorialFiles files;
    eve::cap::provide<eve::service::IFileSystem>(&files);
    {
        eve::Runtime runtime(2048, ssq::Libs::ALL);
        runtime.runSource(R"(
import { new_state, target_at, update_state, encode_save, decode_save } from "game:/scripts/rules.nut"
local state = new_state();
assert(decode_save(encode_save(state)).score == 0);
foreach (invalid in ["", "tutorial.collect|2|1", "tutorial.collect|1|-1",
                     "tutorial.collect|1|01", "tutorial.collect|1|1000001", "tutorial.collect|1|1|"])
    { local rejected = false; try { decode_save(invalid); } catch (error) { rejected = true; }
      assert(rejected); }
state.score = 999;
assert(decode_save(encode_save(state)).score == 999);
state.x = 0.0; state.y = 0.0;
update_state(state, -1.0, -1.0, 1.0, 960.0, 540.0);
assert(state.x >= 0.0 && state.y >= 0.0);
local goal = target_at(state.score);
state.x = goal.x; state.y = goal.y;
update_state(state, 0.0, 0.0, 0.0, 960.0, 540.0);
assert(state.score == 1000);
)",
                          "game:/main.nut");
    }
    eve::cap::detail::clearAllRaw();
}

TEST_CASE("userDocs.tutorialLifecycleAndSaveFailure") {
    eve::cap::detail::clearAllRaw();
    TutorialFiles files;
    eve::cap::provide<eve::service::IFileSystem>(&files);
    {
        eve::Runtime runtime(2048, ssq::Libs::ALL);
        runtime.runSource(R"(
eve_init <- null; eve_update <- null; eve_render <- null; eve_quit <- null;
config <- {width=960, height=540};
texture_creations <- 0; texture_updates <- 0;
test_files <- {}; failed_write <- "";
gfx <- {
    setBackgroundColor=function(r,g,b,a) {}, clear=function() {},
    drawTexturedRect=function(t,x,y,w,h,r,g,b,a) {},
    drawSolidRect=function(x,y,w,h,r,g,b,a) {},
    newTexture=function(p,u,v) { ++texture_creations; return {}; },
    updateTextureFromImageData=function(t,p) { ++texture_updates; }
};
eve <- {};
eve.Image <- function() { return {newEmptyImageData=function(w,h,f) {return {setPixel=function(x,y,r,g,b,a) {}};}}; };
ui <- {
    beginBuild=function() {}, beginWindow=function(title,id) {},
    text=function(text,id) {}, button=function(text,id) {}, end=function() {},
    mountBuildAs=function(name) {},
    setHostPos=function(x,y,px,py) {}, setHostSize=function(w,h) {},
    setHostMovable=function(on) {}, setHostResizable=function(on) {}, consumeClick=function() {return "";},
    setText=function(id,text) {}, beginFrameAndRender=function() {}
};
keyboard <- {isDown=function(key) {return false;}};
key_just_pressed <- function(key) {return false;};
fs <- {
    setIdentity=function(id,append) {return true;}, setupWriteDirectory=function() {return true;},
    readText=function(path) {return path in test_files ? test_files[path] : "";},
    writeTextAtomic=function(path,text) {
        if (path == failed_write) return 0;
        test_files[path] <- text; return 1;
    }
};
)",
                          "tutorial-fixture");
        runtime.runSource(readSource(repository() / "examples/game-tutorial/main.nut"), "game:/main.nut");
        runtime.runSource(R"(
eve_init(); eve_update(1.0 / 60.0); eve_render();
assert(texture_creations == 1);
tutorial_state.score = 7; save_game(); restart_game(); load_game();
assert(tutorial_state.score == 7);
tutorial_state.score = 8; save_game();
assert(test_files["tutorial-slot.backup.txt"] == "tutorial.collect|1|7");
test_files["tutorial-slot.txt"] = "broken"; load_game(); assert(tutorial_state.score == 7);
test_files["tutorial-slot.backup.txt"] = "broken";
tutorial_state.score = 9; load_game(); assert(tutorial_state.score == 9);
test_files["tutorial-slot.txt"] = "tutorial.collect|1|8";
failed_write = "tutorial-slot.backup.txt"; save_game();
assert(test_files["tutorial-slot.txt"] == "tutorial.collect|1|8");
assert(tutorial_state.score == 9);
eve_reload(); assert(texture_creations == 1 && texture_updates == 1);
assert(tutorial_state.score == 9); eve_quit();
)",
                          "tutorial-assertions");
    }
    eve::cap::detail::clearAllRaw();
}
}  // namespace test_script_user_doc_examples
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_sensing)
#include "PrivateTestScope.h"
#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include "common/Module.h"
#include "sensing/LineOfSightRouter.h"
#include "sensing/Sensing.h"
#include "sensing/TargetingPipeline.h"
#include "simplesquirrel/simplesquirrel.hpp"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// sensing.cpp
namespace test_sensing {
// sensing.cpp
using namespace eve::sensing;


TEST_CASE("sensing.worldHandleAndScriptResultContract") {
    auto created = Sensing::newWorld();
    REQUIRE(created.ok());
    const auto reference = std::move(created).takeValue();
    auto       borrowed  = Sensing::resolve(reference);
    REQUIRE(borrowed.isBound());
    auto released = Sensing::release(reference);
    REQUIRE(released.ok());
    CHECK(Sensing::isStale(reference));
    CHECK(!Sensing::resolve(reference).isBound());

    ssq::VM vm(1024, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        result <- "fail";
        local sensing = eve.Sensing();
        local created = sensing.newWorld();
        if (created.ok && created.value.ownership() == "owned" && !created.value.isStale()) {
            local world = created.value;
            local inserted = world.upsert("script.subject", 0.0, 0.0, "red", "unit", "all");
            local queried = world.circle(0.0, 0.0, 2.0, "unit", "", "", "", "all", 4);
            local candidate = world.resultAt(0);
            local snapshot = world.snapshotJson();
            local restored = snapshot.ok ? world.restoreJson(snapshot.value) : { ok = false };
            local rejected = world.restoreJson("{}");
            local released = world.release();
            if (inserted.ok && queried.ok && queried.value == 1 && candidate != null &&
                snapshot.ok && restored.ok && !rejected.ok && released.ok && world.isStale())
                result = "ok";
        }
    )"));
    CHECK_EQ(vm.find("result").toString(), std::string("ok"));
}


// sensing_line_of_sight_router.cpp
namespace {

/** @brief A backend that answers everything one way, so routing is what the test observes. */
class StubBackend final : public eve::sensing::ILineOfSightQuery {
public:
    explicit StubBackend(bool answer) : answer_(answer) {}

    [[nodiscard]] eve::Result<eve::sensing::LineOfSightResult> query(
        const eve::sensing::TargetLocation&, const eve::sensing::TargetLocation&) const override {
        ++calls;
        eve::sensing::LineOfSightResult result;
        result.visible = answer_;
        return eve::Result<eve::sensing::LineOfSightResult>::success(result);
    }

    mutable int calls = 0;

private:
    bool answer_ = false;
};

[[maybe_unused]] eve::sensing::TargetLocation grid(std::int32_t x, std::int32_t y) {
    return eve::sensing::TargetLocation(eve::sensing::GridPoint::grid2D(x, y));
}

[[maybe_unused]] eve::sensing::TargetLocation world(float x, float y, float z) {
    // World points are validated on construction, so the test checks the Result here instead of
    // letting a bad literal silently become an invalid location.
    auto point = eve::sensing::WorldPoint::world3D(x, y, z);
    REQUIRE(point.ok());
    return eve::sensing::TargetLocation(std::move(point).takeValue());
}

}  // namespace
}  // namespace test_sensing
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_settlement_script)
#include "PrivateTestScope.h"
#include <simplesquirrel/simplesquirrel.hpp>
#include "common/Module.h"
#include "common/SquirrelBinding.h"
#include "settlement/SettlementModule.h"
#include "settlement/SettlementRules.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// settlement_script.cpp
namespace test_settlement_script {
TEST_CASE("settlementScript.runtimeOwnsRulesAndLedger") {
    ssq::VM vm(1024, ssq::Libs::STRING | ssq::Libs::MATH);
    auto    eve = vm.addTable("eve");
    eve::script::exposeResultBindings(eve);
    eve::settlement::Settlement::expose(eve);

    // Build a minimal settlement.rules document in C++ then feed it to script.
    eve::settlement::SettlementRuleSet rules;
    eve::settlement::SettlementRule    multiply;
    multiply.id           = "buff.attack";
    multiply.source       = "effect:test";
    multiply.stage        = eve::settlement::StageKind::SourceModifiers;
    multiply.operation    = eve::settlement::RuleOperation::Multiply;
    multiply.value        = 1.5;
    multiply.filter.kinds = {"damage"};
    REQUIRE(rules.configure({multiply}).ok());
    auto rulesJson = rules.canonicalJson();
    REQUIRE(rulesJson.ok());

    const std::string rulesLiteral = rulesJson.value();
    eve.addFunc("rulesJson", [rulesLiteral]() { return rulesLiteral; });
    vm.run(vm.compileSource(R"(
        settlement <- eve.Settlement();
        created <- settlement.newRuntime();
        runtime <- created.value;
        configured <- runtime.configureRulesJson(eve.rulesJson());
        validated <- runtime.validateRulesJson(eve.rulesJson());
        source <- "01020304-0506-0708-890a-0b0c0d0e0f10";
        target <- "11121314-1516-1718-991a-1b1c1d1e1f20";
        upserted <- runtime.upsertResource(target, "hp", 100.0, 100.0);
        settled <- runtime.settle(source, target, "damage", "hp", 20.0, "", "{}", 1);
        after <- runtime.getResource(target, "hp");
        missing <- runtime.settle(source, "22222222-2222-2222-2222-222222222222", "damage", "hp", 1.0, "", "{}", 2);
        digest <- runtime.rulesDigest();
        applied <- settled.value.applied;
        current <- after.value.current;
        disposition <- settled.value.disposition;
        ruleCount <- runtime.ruleCount();
        ownership <- runtime.ownership();
    )"));
    CHECK(vm.find("created").toTable().get<bool>("ok"));
    CHECK_EQ(vm.find("created").toTable().get<std::string>("ownership"), std::string("owned"));
    CHECK_EQ(vm.find("ownership").toString(), std::string("owned"));
    CHECK(vm.find("configured").toTable().get<bool>("ok"));
    CHECK(vm.find("validated").toTable().get<bool>("ok"));
    CHECK(vm.find("upserted").toTable().get<bool>("ok"));
    CHECK(vm.find("settled").toTable().get<bool>("ok"));
    CHECK_EQ(vm.find("applied").toFloat(), 30.0f);  // 20 * 1.5
    CHECK_EQ(vm.find("current").toFloat(), 70.0f);
    CHECK_EQ(vm.find("disposition").toString(), std::string("applied"));
    CHECK(!vm.find("missing").toTable().get<bool>("ok"));
    CHECK(vm.find("digest").toTable().get<bool>("ok"));
    CHECK_EQ(vm.find("ruleCount").toInt(), 1);
}

TEST_CASE("settlementScript.rejectsUnknownKindAndNormalizesSubjectCase") {
    ssq::VM vm(1024, ssq::Libs::STRING | ssq::Libs::MATH);
    auto    eve = vm.addTable("eve");
    eve::script::exposeResultBindings(eve);
    eve::settlement::Settlement::expose(eve);
    vm.run(vm.compileSource(R"(
        settlement <- eve.Settlement();
        runtime <- settlement.newRuntime().value;
        source <- "01020304-0506-0708-890a-0b0c0d0e0f10";
        targetUpper <- "11121314-1516-1718-991A-1B1C1D1E1F20";
        targetLower <- "11121314-1516-1718-991a-1b1c1d1e1f20";
        upserted <- runtime.upsertResource(targetUpper, "hp", 50.0, 100.0);
        settled <- runtime.settle(source, targetLower, "damage", "hp", 10.0, "", "{}", 1);
        after <- runtime.getResource(targetUpper, "hp");
        unknown <- runtime.settle(source, targetLower, "burn", "hp", 5.0, "", "{}", 2);
        afterUnknown <- runtime.getResource(targetLower, "hp");
        applied <- settled.value.applied;
        current <- after.value.current;
        currentAfterUnknown <- afterUnknown.value.current;
    )"));
    CHECK(vm.find("upserted").toTable().get<bool>("ok"));
    CHECK(vm.find("settled").toTable().get<bool>("ok"));
    CHECK_EQ(vm.find("applied").toFloat(), 10.0f);
    CHECK_EQ(vm.find("current").toFloat(), 40.0f);
    CHECK(!vm.find("unknown").toTable().get<bool>("ok"));
    CHECK_EQ(vm.find("currentAfterUnknown").toFloat(), 40.0f);
}
}  // namespace test_settlement_script
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_simplesquirrel)
#include <cassert>
#include <iostream>
#include <map>
#include <string>
#include "PrivateTestScope.h"
#include "ScriptTest.h"
// Class-layout probes need these headers, but SQVM stays opaque in binding tests.
// clang-format off
#include "sqpcheader.h"
#include "sqtable.h"
#include "sqclass.h"
// clang-format on
extern const char* simplesquirrel_content;
// simplesquirrel.cpp
namespace test_simplesquirrel {
using namespace eve;


UnitSciptTest(SimpleSquirrelTest, simplesquirrel_content);

TEST_CASE_FIXTURE(SimpleSquirrelTest, "SimpleSquirrelTest.ExportClass") { REQUIRE(callScript("exportClass")); }

class A {
public:
    SSQ_REG
    A() {
        data = new char[32];
        size = 0;
        std::cerr << "A()" << std::endl;
    }
    ~A() {
        delete[] data;
        std::cerr << "~A()" << std::endl;
    }
    void setString(std::string name) {
        if (name.size() < 32) {
            strncpy(data, name.c_str(), name.size());
            data[name.size()] = '\0';
            size              = static_cast<int>(name.size());
        }
    }
    char* data;
    int   size;
};

class B {
public:
    SSQ_REG
    B() { std::cerr << "B()" << std::endl; }
    ~B() { std::cerr << "~B()" << std::endl; }

    void          setA(ssq::Instance a) { this->a = a; }
    void          print() { std::cerr << a.to<A*>()->data << std::endl; }
    ssq::Instance a;
};

void A::expose(ssq::Table& table) {
    auto cls = table.addClass("A", ssq::Class::Ctor<A()>());
    expose(cls);
}

void A::expose(ssq::Class& cls) { cls.addFunc("setString", &A::setString); }

void B::expose(ssq::Table& table) {
    auto cls = table.addClass("B", ssq::Class::Ctor<B()>());
    expose(cls);
}

void B::expose(ssq::Class& cls) {
    cls.addFunc("setA", &B::setA);
    cls.addFunc("print", &B::print);
}

TEST_CASE_FIXTURE(SimpleSquirrelTest, "SimpleSquirrelTest.RefTest") {
    A::expose(vm);
    B::expose(vm);
    REQUIRE(callScript("refTest"));
}

#if !(defined(__APPLE__) && defined(NDEBUG))
TEST_CASE_FIXTURE(SimpleSquirrelTest, "SimpleSquirrelTest.TestDefClass") {
    auto root = vm.getRaw();
    CHECK_EQ(sq_type(root), OT_TABLE);
    auto rt = _table(root);
    for (SQInteger i = 0; i < rt->_numofnodes; i++) {
        if (sq_type(rt->_nodes[i].key) != OT_NULL) {
            auto p = rt->_nodes[i].val;
            if (sq_type(p) == OT_CLASS) {
                printf("%s ", _string(rt->_nodes[i].key)->_val);
            }
        }
    }
    printf("\n");

    ssq::Class cls = vm.findClass("Test1");
    auto       o   = cls.getRaw();
    CHECK_EQ(o._type, OT_CLASS);
    auto pclass = o._unVal.pClass;

    auto t = pclass->_members;
    for (SQInteger i = 0; i < t->_numofnodes; i++) {
        if (sq_type(t->_nodes[i].key) != OT_NULL) {
            printf("%s ", _string(t->_nodes[i].key)->_val);
            auto p = t->_nodes[i].val;
            if (sq_type(p) == OT_INTEGER) {
                if (_isfield(p)) {
                    printf("field %d\n", static_cast<int>(_member_idx(p)));
                }
                if (_ismethod(p)) {
                    printf("method %d\n", static_cast<int>(_member_idx(p)));
                }
            }
        }
    }

    ssq::Object obj  = cls.find("attr1");
    ssq::Class  attr = obj.toClass();

    REQUIRE(callScript("testDefClass"));
}
#endif


TEST_CASE("SimpleSquirrelTest.PerformInteration") {
    ssq::VM vm(128);
    auto    script = vm.compileSource(R"(
        class Attr {}
        class Test1 {
            attr1 = Attr;
            attr2 = null;
            constructor(a, b) { attr1 = a; attr2 = b; }
            function test() {}
        }
    )");
    vm.run(script);
    auto                                cls        = vm.findClass("Test1");
    auto                                v          = vm.getHandle();
    const auto                          initialTop = sq_gettop(v);
    std::map<std::string, SQObjectType> members;
    sq_pushobject(v, cls.getRaw());
    sq_pushnull(v);
    while (SQ_SUCCEEDED(sq_next(v, -2))) {
        const char* name   = nullptr;
        const auto  result = sq_getstring(v, -2, &name);
        REQUIRE(SQ_SUCCEEDED(result));
        REQUIRE(name != nullptr);
        members.emplace(name, sq_gettype(v, -1));
        sq_pop(v, 2);
    }
    sq_pop(v, 2);
    const std::map<std::string, SQObjectType> expected = {
        {"attr1", OT_CLASS}, {"attr2", OT_NULL}, {"constructor", OT_CLOSURE}, {"test", OT_CLOSURE}};
    REQUIRE(members == expected);
    REQUIRE(sq_gettop(v) == initialTop);
}


TEST_CASE_FIXTURE(SimpleSquirrelTest, "SimpleSquirrelTest.GetterTest") { REQUIRE(callScript("getterTest")); }


TEST_CASE("SimpleSquirrelTest.GetAttr") {
    ssq::VM vm(128);
    auto    script = vm.compileSource(R"(
        class Canvas {}
        class Avatar {
            name = "";
            </ type = Canvas /> can = null;
        }
    )");
    vm.run(script);
    auto       cls        = vm.findClass("Avatar");
    auto       v          = vm.getHandle();
    const auto initialTop = sq_gettop(v);
    sq_pushobject(v, cls.getRaw());
    sq_pushstring(v, "can", -1);
    const auto result = sq_getattributes(v, -2);
    REQUIRE(SQ_SUCCEEDED(result));
    REQUIRE(sq_gettype(v, -1) == OT_TABLE);
    std::map<std::string, SQObjectType> attributes;
    sq_pushnull(v);
    while (SQ_SUCCEEDED(sq_next(v, -2))) {
        const char* name      = nullptr;
        const auto  keyResult = sq_getstring(v, -2, &name);
        REQUIRE(SQ_SUCCEEDED(keyResult));
        REQUIRE(name != nullptr);
        attributes.emplace(name, sq_gettype(v, -1));
        sq_pop(v, 2);
    }
    sq_pop(v, 2);
    const std::map<std::string, SQObjectType> expected = {{"type", OT_CLASS}};
    REQUIRE(attributes == expected);
    sq_pushstring(v, "name", -1);
    const auto emptyResult = sq_getattributes(v, -2);
    REQUIRE(SQ_SUCCEEDED(emptyResult));
    REQUIRE(sq_gettype(v, -1) == OT_NULL);
    sq_pop(v, 2);
    REQUIRE(sq_gettop(v) == initialTop);
}

// ---------------------------------------------------------------------------
// Instance `_get` / `_set` metamethods.
//
// The plan's step 2 makes the engine the authoritative store for script component
// fields. Whether that forces every script to switch to setters depends on these
// three VM behaviours, so they are pinned here rather than assumed.
// ---------------------------------------------------------------------------

TEST_CASE("SimpleSquirrelTest.InstanceGetAndSetMetamethodsAreConsulted") {
    ssq::VM vm(128);
    vm.run(vm.compileSource(R"(
        class Probe {
            log = ""
            function _get(key) { log = "get:" + key; return 42 }
            function _set(key, value) { log = "set:" + key + ":" + value; return value }
        }
        probe <- Probe()
        readMissing <- probe.missing
        probe.other = 7
        writeLog <- probe.log
    )"));

    // 未声明的成员读写都落到元方法上。
    REQUIRE_EQ(vm.find("readMissing").toInt(), 42);
    REQUIRE_EQ(vm.find("writeLog").toString(), std::string("set:other:7"));
}

TEST_CASE("SimpleSquirrelTest.DeclaredFieldShadowsTheMetamethods") {
    ssq::VM vm(128);
    vm.run(vm.compileSource(R"(
        class Probe {
            x = 0.0
            calls = 0
            function _get(key) { calls = calls + 1; return 99.0 }
            function _set(key, value) { calls = calls + 1 }
        }
        probe <- Probe()
        declaredRead <- probe.x
        probe.x = 5.0
        declaredReread <- probe.x
        calls <- probe.calls
    )"));

    // 声明过的字段走实例槽位，元方法完全不会被调用：这正是"字段留在类里"就没法让引擎当权威的原因。
    REQUIRE_EQ(vm.find("declaredRead").toFloat(), 0.0);
    REQUIRE_EQ(vm.find("declaredReread").toFloat(), 5.0);
    REQUIRE_EQ(vm.find("calls").toInt(), 0);
}


}  // namespace test_simplesquirrel
#endif
#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_simplesquirrel_direct_member)
#include "PrivateTestScope.h"
#include <zeroerr/assert.h>
#include <zeroerr/unittest.h>
#include <simplesquirrel/simplesquirrel.hpp>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>
// simplesquirrel_direct_member.cpp
namespace test_simplesquirrel_direct_member {
namespace {
struct MemberBindingProbe {
    inline static int live  = 0;
    int               value = 0;
    std::string       text  = "probe";
    MemberBindingProbe() { ++live; }
    ~MemberBindingProbe() { --live; }
    void                         set(int input) { value = input; }
    int                          get() const { return value; }
    int                          add(int input) { return value += input; }
    const std::string&           label() const { return text; }
    std::string                  echo(const std::string& input) const { return input + text; }
    std::vector<int>             values() const { return {value, value + 1}; }
    std::tuple<int, std::string> pair() const { return {value, text}; }
    int                          fail() const { throw std::runtime_error("member failure"); }
};
}  // namespace

TEST_CASE("SimpleSquirrel.DirectMemberConversionsAndLifetime") {
    REQUIRE(MemberBindingProbe::live == 0);
    {
        ssq::VM vm(2048, ssq::Libs::ALL);
        auto    cls = vm.addClass("MemberProbe", ssq::Class::Ctor<MemberBindingProbe()>());
        cls.addFunc("set", &MemberBindingProbe::set);
        cls.addFunc("get", &MemberBindingProbe::get);
        cls.addFunc("add", &MemberBindingProbe::add);
        cls.addFunc("label", &MemberBindingProbe::label);
        cls.addFunc("echo", &MemberBindingProbe::echo);
        cls.addFunc("values", &MemberBindingProbe::values);
        cls.addFunc("pair", &MemberBindingProbe::pair);
        cls.addFunc("fail", &MemberBindingProbe::fail);
        // Capturing callables still use the upstream std::function overload.
        const int offset = 3;
        cls.addFunc("captured", [offset](MemberBindingProbe* p) { return p->value + offset; });
        cls.addFunc("stateless", [](MemberBindingProbe* p) { return p->value + 2; });
        cls.addFunc("setStateless", [](MemberBindingProbe* p, int value) { p->value = value; });
        cls.addFunc("statelessValues", [](MemberBindingProbe* p) { return std::vector<int>{p->value, 8}; });
        cls.addFunc("statelessPair",
                    [](MemberBindingProbe* p) { return std::tuple<int, std::string>{p->value, "pair"}; });
        cls.addFunc("statelessFail", [](MemberBindingProbe*) -> int { throw std::runtime_error("stateless failure"); });
        vm.addFunc("sum", +[](int a, int b) { return a + b; });
        vm.addFunc("zero", []() { return 0; });
        vm.run(vm.compileSource(R"(
            function verifyMembers() {
                local p = MemberProbe();
                p.set(4);
                if (p.get() != 4 || p.add(2) != 6 || p.get() != 6) return false;
                if (p.label() != "probe" || p.echo("x") != "xprobe") return false;
                local values = p.values();
                local pair = p.pair();
                if (values[0] != 6 || values[1] != 7 || pair[0] != 6 || pair[1] != "probe") return false;
                if (p.captured() != 9) return false;
                if (p.stateless() != 8 || sum(3, 4) != 7 || zero() != 0) return false;
                p.setStateless(9);
                if (p.get() != 9) return false;
                local sv = p.statelessValues();
                local sp = p.statelessPair();
                if (sv[0] != 9 || sv[1] != 8 || sp[0] != 9 || sp[1] != "pair") return false;
                p.set(6);
                local caught = false;
                try { p.statelessFail(); } catch (e) { caught = (e.find("stateless failure") != null); }
                if (!caught) return false;
                caught = false;
                try { p.setStateless("wrong"); } catch (e) { caught = true; }
                if (!caught) return false;
                caught = false;
                try { sum(1); } catch (e) { caught = true; }
                if (!caught) return false;
                caught = false;
                try { p.fail(); } catch (e) { caught = (e.find("member failure") != null); }
                if (!caught) return false;
                caught = false;
                try { p.set("wrong"); } catch (e) { caught = true; }
                if (!caught) return false;
                caught = false;
                try { p.get(1); } catch (e) { caught = true; }
                return caught;
            }
        )"));
        CHECK(vm.callFunc(vm.findFunc("verifyMembers"), vm).toBool());
    }
    CHECK_EQ(MemberBindingProbe::live, 0);
}
}  // namespace test_simplesquirrel_direct_member
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_social)
#include "PrivateTestScope.h"
#include "ScriptTest.h"
#include "social/Social.h"
#include "social/SocialGraph.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// social.cpp
namespace test_social {
using namespace eve;


using eve::social::Social;
using eve::social::SocialGraph;


static const char* kSocialScript = R"SQ(
function testSocialBindings() {
    local social = eve.Social();
    social.clear();
    if (!social.setOwner("asset:b", "org")) return false;
    if (!social.setOwner("asset:a", "org")) return false;
    if (social.ownedAt("org", 0) != "asset:a") return false;
    if (!social.setControllerInt(7, 9)) return false;
    if (social.controllerOfInt(7) != social.idFromInt(9)) return false;
    if (!social.assign("person", "operator", "asset:a")) return false;
    if (social.assigneeAt("asset:a", "operator", 0) != "person") return false;
    if (!social.setRelation("person", "org", "affinity", 30.0)) return false;
    if (social.addRelation("person", "org", "affinity", 15.0) != 45.0) return false;
    if (social.relationSourceAt("org", "affinity", 40.0, 0) != "person") return false;
    if (social.eventCount() < 5) return false;
    return social.eventAction(social.eventCount() - 1) == "relation_changed";
}
)SQ";

UnitSciptTest(SocialScriptTest, kSocialScript);

TEST_CASE_FIXTURE(SocialScriptTest, "social.script.bindings") { REQUIRE(callScript("testSocialBindings")); }
}  // namespace test_social
#endif
#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_squirrel_binding)
#include <cmath>
#include <limits>
#include <simplesquirrel/simplesquirrel.hpp>
#include <string>
#include <utility>
#include "PrivateTestScope.h"
#include "common/ScriptError.h"
#include "common/SquirrelBinding.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// squirrel_binding.cpp
namespace test_squirrel_binding {
using eve::Diagnostic;
using eve::DiagnosticCode;
using eve::Result;
using eve::Value;

TEST_CASE("squirrel_binding.value_round_trip_preserves_nested_value") {
    ssq::VM      vm(1024, ssq::Libs::ALL);
    Value::Array items;
    items.emplace_back(Value::Object{{"enabled", true}, {"label", "nested"}});
    items.emplace_back(std::int64_t{9});
    const Value original(Value::Object{
        {"name", "round-trip"},
        {"items", Value(std::move(items))},
        {"ratio", 1.25},
    });

    auto pushed = eve::script::pushValue(vm.getHandle(), original);
    REQUIRE(pushed.ok());
    auto restored = eve::script::valueFromSquirrel(vm.getHandle(), -1);
    REQUIRE(restored.ok());
    CHECK(restored.value() == original);
    sq_pop(vm.getHandle(), 1);
}

TEST_CASE("squirrel_binding.value_rejects_non_finite_numbers_with_path_and_source") {
    ssq::VM    vm(1024, ssq::Libs::ALL);
    const auto top = sq_gettop(vm.getHandle());
    sq_newarray(vm.getHandle(), 0);
    sq_pushfloat(vm.getHandle(), std::numeric_limits<SQFloat>::infinity());
    REQUIRE(SQ_SUCCEEDED(sq_arrayappend(vm.getHandle(), -2)));

    auto result = eve::script::valueFromSquirrel(vm.getHandle(), -1);
    CHECK(!result.ok());
    REQUIRE(result.error() != nullptr);
    CHECK(static_cast<int>(result.error()->code()) == static_cast<int>(DiagnosticCode::InvalidArgument));
    CHECK(result.error()->path() == "$[0]");
    CHECK(result.error()->source() == "squirrel.binding");
    sq_settop(vm.getHandle(), top);
}

TEST_CASE("squirrel_binding.value_enforces_depth_limit") {
    ssq::VM    vm(1024, ssq::Libs::ALL);
    const auto top = sq_gettop(vm.getHandle());
    sq_newarray(vm.getHandle(), 0);
    sq_newarray(vm.getHandle(), 0);
    REQUIRE(SQ_SUCCEEDED(sq_arrayappend(vm.getHandle(), -2)));

    eve::script::SquirrelValueOptions options;
    options.maxDepth = 1;
    auto result      = eve::script::valueFromSquirrel(vm.getHandle(), -1, options);
    CHECK(!result.ok());
    REQUIRE(result.error() != nullptr);
    CHECK(result.error()->path() == "$[0]");
    sq_settop(vm.getHandle(), top);
}

TEST_CASE("squirrel_binding.result_projection_and_explicit_ignore_are_uniform") {
    ssq::VM    vm(1024, ssq::Libs::ALL);
    ssq::Table eve = vm.addTable("eve");
    eve::script::exposeResultBindings(eve);

    auto projected =
        eve::script::projectResult(vm.getHandle(),
                                   Result<int>::failure(Diagnostic::error(DiagnosticCode::NotFound, "missing value",
                                                                          "payload.name", {}, "test.source")),
                                   [](int value) { return Value(static_cast<std::int64_t>(value)); });
    CHECK(!projected.find("ok").toBool());
    CHECK(projected.find("checked").toBool());
    CHECK(projected.find("hasValue").toBool() == false);
    auto diagnostics = projected.find("diagnostics").toArray();
    REQUIRE(diagnostics.size() == 1u);
    const auto diagnostic = diagnostics.get<ssq::Table>(0);
    CHECK(diagnostic.find("path").toString() == "payload.name");
    CHECK(diagnostic.find("source").toString() == "test.source");

    eve.set("target", projected);
    vm.run(vm.compileSource("ignored <- eve.result.ignore(eve.target, \"optional feature\");\n",
                            "result-ignore-test.nut"));
    CHECK(vm.find("ignored").toBool());
    CHECK(projected.find("ignored").toBool());
    CHECK(projected.find("ignoreReason").toString() == "optional feature");
    CHECK(eve::script::ignoreResult(projected, "second explicit observation"));
}

// ---------------------------------------------------------------------------
// Argument marshalling of the engine -> script call helpers (R-FACE-7)
// ---------------------------------------------------------------------------
//
// `callFunction` / `callFunctionResult` are the mandated way to call script from native
// code, so what they do with an argument is part of the contract. Two marshalling
// mistakes were live here: a string literal did not compile at all (simplesquirrel's
// pushByCopy cannot take `const char[N]`), and a `const char*` arrived in script as
// *userdata*, because ssq has no `const char*` pushValue overload.

namespace {

const char* kCallArgumentScript = R"SQ(
function argKind(v) { return typeof v }
function argText(v) { return "" + v }
function twoArgs(a, b) { return "" + a + "|" + b }
function boom() { throw "boom" }
)SQ";

void runCallArgumentScript(ssq::VM& vm) {
    eve::script::installScriptErrorHook(vm.getHandle());
    vm.run(vm.compileSource(kCallArgumentScript, "call-argument-test.nut"));
}

std::string topString(HSQUIRRELVM vm) {
    const SQChar* out = nullptr;
    if (sq_gettype(vm, -1) != OT_STRING) return {};
    if (SQ_FAILED(sq_getstring(vm, -1, &out))) return {};
    return out ? std::string(out) : std::string();
}

}  // namespace

TEST_CASE("squirrel_binding.call_argument_c_string_literal_arrives_as_string") {
    ssq::VM vm(1024, ssq::Libs::ALL);
    runCallArgumentScript(vm);
    const SQInteger top = sq_gettop(vm.getHandle());
    ssq::Object     fn  = vm.findFunc("argText");

    REQUIRE(
        SQ_SUCCEEDED(eve::script::callFunctionResult(vm.getHandle(), fn.getRaw(), eve::script::rootTable, "literal")));
    REQUIRE(topString(vm.getHandle()) == "literal");
    sq_settop(vm.getHandle(), top);
}

TEST_CASE("squirrel_binding.call_argument_c_string_pointer_arrives_as_string") {
    ssq::VM vm(1024, ssq::Libs::ALL);
    runCallArgumentScript(vm);
    const SQInteger top  = sq_gettop(vm.getHandle());
    ssq::Object     fn   = vm.findFunc("argKind");
    const char*     text = "dynamic";

    // Measured before the fix: `typeof v` was "userdata".
    REQUIRE(SQ_SUCCEEDED(eve::script::callFunctionResult(vm.getHandle(), fn.getRaw(), eve::script::rootTable, text)));
    REQUIRE(topString(vm.getHandle()) == "string");
    sq_settop(vm.getHandle(), top);
}

TEST_CASE("squirrel_binding.call_argument_std_strings_scalars_and_stack_discipline") {
    ssq::VM vm(1024, ssq::Libs::ALL);
    runCallArgumentScript(vm);
    const SQInteger top = sq_gettop(vm.getHandle());

    ssq::Object       fn    = vm.findFunc("twoArgs");
    const std::string owned = "owned";
    REQUIRE(
        SQ_SUCCEEDED(eve::script::callFunctionResult(vm.getHandle(), fn.getRaw(), eve::script::rootTable, owned, 7)));
    REQUIRE(topString(vm.getHandle()) == "owned|7");
    sq_settop(vm.getHandle(), top);

    // callFunction() restores the stack it was given, on success and on a throwing callee.
    fn = vm.findFunc("argKind");
    REQUIRE(SQ_SUCCEEDED(eve::script::callFunction(vm.getHandle(), fn.getRaw(), eve::script::rootTable, "x")));
    REQUIRE(sq_gettop(vm.getHandle()) == top);

    fn               = vm.findFunc("boom");
    const int before = eve::script::reportedScriptCallFailures();
    REQUIRE(SQ_FAILED(eve::script::callFunction(vm.getHandle(), fn.getRaw(), eve::script::rootTable)));
    REQUIRE(sq_gettop(vm.getHandle()) == top);
    CHECK(eve::script::reportedScriptCallFailures() == before + 1);
}
}  // namespace test_squirrel_binding
#endif
#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_statepatch)
#include "PrivateTestScope.h"
#include <simplesquirrel/simplesquirrel.hpp>
#include <string>
#include "StatePatchTestSupport.h"
#include "common/Module.h"
#include "statepatch/StatePatch.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// statepatch.cpp
namespace test_statepatch {
using namespace eve::statepatch;

namespace {}  // namespace


TEST_CASE("statepatch.script.batchConflictQueryAndSnapshot") {
    ssq::VM vm(1024, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        result <- "fail";
        local module = eve.StatePatch();
        local storeResult = module.newStore();
        local store = storeResult.ok ? storeResult.value : null;
        local firstResult = store != null ? store.newBatch() : { ok = false };
        local first = firstResult.ok ? firstResult.value : null;
        if (store != null && first != null && first.set("actor-1", "rank", "3").ok &&
            first.set("actor-1", "loyalty", "0.75").ok && store.commit(first).ok) {
            local conflictResult = store.newBatch();
            local conflict = conflictResult.ok ? conflictResult.value : null;
            if (conflict != null && conflict.set("actor-2", "rank", "1").ok &&
                conflict.setExpected("actor-1", "rank", "4", "2").ok &&
                !store.commit(conflict).ok && conflict.result().errorAt(0).getCode() == "conflict" &&
                !store.has("actor-2", "rank")) {
                local copyResult = module.newStore();
                local copy = copyResult.ok ? copyResult.value : null;
                local restoredResult = copy != null ? copy.restoreJson(store.snapshotJson()) : { ok = false };
                if (copy != null && restoredResult.ok && copy.queryKeys("actor-1") == 2)
                    result = copy.get("actor-1", "rank");
            }
        }
    )"));
    CHECK_EQ(vm.find("result").toString(), std::string("3"));
}
}  // namespace test_statepatch
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_tactics_graph)
#include "PrivateTestScope.h"
#include <simplesquirrel/simplesquirrel.hpp>
#include "common/ECS.h"
#include "common/Module.h"
#include "hexmap/HexSphereTopology.h"
#include "tactics/LineOfSight.h"
#include "tactics/TacticsPath.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// tactics_graph.cpp
namespace test_tactics_graph {
TEST_CASE("tactics.graphScriptMovementSnapshotAndStaleRelease") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    ssq::VM          vm(2048, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        function value(r) { if (!r.ok) throw r.status.summary; return ("value" in r) ? r.value : null; }
        local battle=value(eve.Tactics().newBattle("00000000-0000-0000-0000-000000009000",7));
        value(battle.setTopology("graph"));
        value(battle.addCell(4,0,0,1)); value(battle.addCell(99,0,0,1)); value(battle.addCell(5,0,0,1));
        value(battle.addEdge(4,0,0,99,0,0,"{}"));
        local side="00000000-0000-0000-0000-000000009201";
        local actor="00000000-0000-0000-0000-000000009101";
        value(battle.addSide(side)); value(battle.addUnit(actor,side,"test:unit",4,0,0,1,4,0,10));
        value(battle.start("initiative"));
        for(local i=1;i<=3;i++) value(battle.advance(i,1));
        local snapshot=value(battle.snapshotJson());
        if(battle.move(actor,5,0,0).ok) throw "invented grid adjacency";
        value(battle.move(actor,99,0,0));
        if(battle.restoreJson("{}").ok || value(battle.unitCell(actor)).x!=99) throw "failed restore mutated board";
        if(battle.move(actor,4,0,0).ok) throw "invented reverse edge";
        value(battle.restoreJson(snapshot));
        if(value(battle.unitCell(actor)).x!=4) throw "graph restore failed";
        value(battle.move(actor,99,0,0));
        value(battle.release());
        if(!battle.isStale()) throw "release failed";
    )"));
}
}  // namespace test_tactics_graph
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_tactics_presentation_script)
#include "PrivateTestScope.h"
#include <algorithm>
#include <memory>
#include <simplesquirrel/simplesquirrel.hpp>
#include <string>
#include <utility>
#include <vector>
#include "common/Capability.h"
#include "common/ECS.h"
#include "common/Module.h"
#include "tactics/LineOfSight.h"
#include "tactics/Presentation.h"
#include "tactics/Tactics.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// tactics_presentation_script.cpp
namespace test_tactics_presentation_script {
namespace {

[[maybe_unused]] eve::SubjectRef subject(const char* text) {
    const auto id = eve::PersistentId::parse(text);
    REQUIRE(id.has_value());
    return eve::SubjectRef::fromPersistentId(*id);
}

[[maybe_unused]] eve::SimulationStep step(std::uint64_t tick) {
    return {eve::SimulationTick(tick), eve::Duration::fromNanoseconds(1)};
}

/** @brief One side with two units on a straight corridor, ready to produce events. */
struct Fixture {
    ecs::Table            world;
    ecs::ScopedTable      guard{world};
    eve::tactics::Tactics tactics;
    ecs::EntityHandle     battle{};
    eve::SubjectRef       battleSubject = subject("00000000-0000-0000-0000-000000000a00");
    eve::SubjectRef       actor         = subject("00000000-0000-0000-0000-000000000a01");
    eve::SubjectRef       ally          = subject("00000000-0000-0000-0000-000000000a02");

    void build() {
        auto created = tactics.newBattle(battleSubject, 19);
        REQUIRE(created.ok());
        battle = std::move(created).takeValue();
        for (int x = 0; x < 4; ++x) REQUIRE(tactics.addCell(battle, {x, 0, 0}).ok());
        auto side = tactics.newSide(battle, subject("00000000-0000-0000-0000-000000000a03"));
        REQUIRE(side.ok());
        REQUIRE(tactics.newUnit(battle, side.value(), actor, {}, {0, 0, 0}, {2, 300, 0, 10}).ok());
        REQUIRE(tactics.newUnit(battle, side.value(), ally, {}, {3, 0, 0}, {1, 300, 0, 5}).ok());
        REQUIRE(tactics.start(battle, eve::tactics::kInitiativePolicyId).ok());
        REQUIRE(tactics.advance(battle, step(1)).ok());
        REQUIRE(tactics.advance(battle, step(2)).ok());
        REQUIRE(tactics.advance(battle, step(3)).ok());
    }
};

/** @brief A sight policy that blocks everything, to prove the registered one is used. */
class BlindPolicy final : public eve::tactics::ILineOfSightPolicy {
public:
    [[nodiscard]] std::string_view  id() const noexcept override { return "test-blind"; }
    [[nodiscard]] eve::Result<bool> visible(const eve::tactics::BoardState&, eve::tactics::Cell,
                                            eve::tactics::Cell) const override {
        return eve::Result<bool>::success(false);
    }
};

}  // namespace


TEST_CASE("tactics.scriptInteractionSessionTurnsClicksIntoIntents") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    ssq::VM          vm(4096, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        result <- "fail";
        local module = eve.Tactics();
        local created = module.newBattle("00000000-0000-0000-0000-000000000b10", 21);
        local battle = created.ok ? created.value : null;
        if (battle != null) {
            local unitId = "00000000-0000-0000-0000-000000000b11";
            local allyId = "00000000-0000-0000-0000-000000000b13";
            local sideId = "00000000-0000-0000-0000-000000000b12";
            local c0 = battle.addCell(0, 0, 0, 100);
            local c1 = battle.addCell(1, 0, 0, 100);
            local c2 = battle.addCell(2, 0, 0, 100);
            local side = battle.addSide(sideId);
            local u = battle.addUnit(unitId, sideId, "test:unit", 0, 0, 0, 2, 300, 0, 10);
            local a = battle.addUnit(allyId, sideId, "test:unit", 2, 0, 0, 1, 300, 0, 5);
            local started = battle.start("initiative");
            local p1 = battle.advance(1, 1);
            local p2 = battle.advance(2, 1);
            local p3 = battle.advance(3, 1);

            // `newInteraction` returns the usual Result table; its `value` is the session object.
            local ixResult = battle.newInteraction(unitId);
            local interaction = ixResult.ok ? ixResult.value : null;
            local idleResult = battle.newInteraction(allyId);
            local idle = idleResult.ok ? idleResult.value : null;
            if (interaction != null && idle != null) {
                local initial = interaction.state();               // "await_selection"
                local reachable = interaction.reachableCells();    // the projection the machine reads
                local revision = interaction.expectedRevision();

                // Selecting the controlled unit, then a reachable cell, yields an intention.
                local select = interaction.click(0, 0, 0);
                local afterSelect = interaction.state();
                local move = interaction.click(1, 0, 0);
                local afterMove = interaction.state();
                // A pending intention must be committed or dropped before the next one.
                local queued = interaction.click(2, 0, 0);
                local resolved = interaction.resolve();
                local afterResolve = interaction.state();

                // Arming an ability with script-supplied target cells.
                local armed = interaction.armAbility("test:strike", "[[2,0,0]]");
                local targeting = interaction.state();
                local declared = interaction.click(2, 0, 0);

                // A unit that is not active gets a session that exists and reports itself blocked.
                local idleState = idle.state();
                local idleClick = idle.click(0, 0, 0);

                if (c0.ok && c1.ok && c2.ok && side.ok && u.ok && a.ok && started.ok &&
                    p1.ok && p2.ok && p3.ok &&
                    initial == "await_selection" && revision > 0 &&
                    reachable.value.len() >= 2 &&
                    select.ok && select.value.kind == "select_unit" && select.value.actor == unitId &&
                    afterSelect == "unit_selected" &&
                    move.ok && move.value.kind == "move_to" && move.value.cell.x == 1 &&
                    afterMove == "resolving" && !queued.ok &&
                    resolved.ok && afterResolve == "await_selection" &&
                    armed.ok && targeting == "targeting" &&
                    declared.ok && declared.value.kind == "use_ability_on" &&
                    declared.value.action == "test:strike" && declared.value.targetUnit == allyId &&
                    idleState == "blocked" && !idleClick.ok) {
                    result = "ok";
                } else {
                    result = "s=" + initial + "/" + afterSelect + "/" + afterMove + "/" + afterResolve + "/" +
                             targeting + "/" + idleState +
                             " f=" + (select.ok?1:0) + (move.ok?1:0) + (queued.ok?1:0) + (resolved.ok?1:0) +
                             (armed.ok?1:0) + (declared.ok?1:0) + (idleClick.ok?1:0) +
                             " n=" + reachable.value.len() + " rev=" + revision;
                }
            } else {
                result = "no interaction instance";
            }
        }
    )"));
    CHECK_EQ(vm.find("result").toString(), std::string("ok"));
}

TEST_CASE("tactics.scriptReachesPresentationAndSightQueries") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    ssq::VM          vm(4096, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        result <- "fail";
        local module = eve.Tactics();
        local created = module.newBattle("00000000-0000-0000-0000-000000000a10", 20);
        local battle = created.ok ? created.value : null;
        if (battle != null) {
            local unitId = "00000000-0000-0000-0000-000000000a11";
            local sideId = "00000000-0000-0000-0000-000000000a12";
            local c0 = battle.addCell(0, 0, 0, 100);
            local c1 = battle.addCell(1, 0, 0, 100);
            local c2 = battle.addCell(2, 0, 0, 100);
            local side = battle.addSide(sideId);
            local u = battle.addUnit(unitId, sideId, "test:unit", 0, 0, 0, 2, 300, 0, 10);
            local started = battle.start("initiative");
            local p1 = battle.advance(1, 1);
            local p2 = battle.advance(2, 1);
            local p3 = battle.advance(3, 1);

            // Sight-aware range, and which policy answered it.
            local visible = battle.visibleCellsInRange(0, 0, 0, 1, 2, "chebyshev");
            local algorithm = battle.lineOfSightAlgorithm();

            // Presentation intents: a move must come back as Moving with a revert contract.
            local before = battle.eventCount();
            local moved = battle.move(unitId, 1, 0, 0);
            local intents = battle.presentationIntents(before, 5);
            local sawMoving = false;
            local sawRevert = false;
            local toX = -1;
            if (intents.ok) {
                foreach (intent in intents.value) {
                    if (intent.state == "moving") {
                        sawMoving = true;
                        toX = intent.to.x;
                        if (intent.revert.trigger == "on_expiry" &&
                            intent.revert.restingState == "idle" &&
                            intent.revert.sequence == intent.sequence) sawRevert = true;
                    }
                }
            }

            if (c0.ok && c1.ok && c2.ok && side.ok && u.ok && started.ok &&
                p1.ok && p2.ok && p3.ok && moved.ok &&
                visible.ok && visible.value.len() > 0 &&
                algorithm == "grid_line_of_sight" &&
                intents.ok && sawMoving && sawRevert && toX == 1) {
                result = "ok";
            }
        }
    )"));
    CHECK_EQ(vm.find("result").toString(), std::string("ok"));
}
}  // namespace test_tactics_presentation_script
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_tactics_script)
#include "PrivateTestScope.h"
#include <simplesquirrel/simplesquirrel.hpp>
#include "common/ECS.h"
#include "common/Module.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// tactics_script.cpp
namespace test_tactics_script {
TEST_CASE("tactics.scriptOwnedBattleRunsDeterministicTurn") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    ssq::VM          vm(2048, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        result <- "fail";
        local module = eve.Tactics();
        local created = module.newBattle("00000000-0000-0000-0000-0000000000d0", 42);
        local battle = created.ok ? created.value : null;
        if (battle != null) {
            local c0 = battle.addCell(0, 0, 0, 100);
            local c1 = battle.addCell(1, 0, 0, 100);
            local side = battle.addSide("00000000-0000-0000-0000-0000000000d1");
            local unit = battle.addUnit("00000000-0000-0000-0000-0000000000d2",
                                       "00000000-0000-0000-0000-0000000000d1",
                                       "test:unit", 0, 0, 0, 1, 100, 1, 10);
            local started = battle.start("initiative");
            local p1 = battle.advance(1, 1);
            local p2 = battle.advance(2, 1);
            local p3 = battle.advance(3, 1);
            local moved = battle.move("00000000-0000-0000-0000-0000000000d2", 1, 0, 0);
            local rolled = battle.roll("combat:hit");
            local event = battle.eventAt(battle.eventCount() - 1);
            if (c0.ok && c1.ok && side.ok && unit.ok && started.ok && p1.ok && p2.ok && p3.ok && moved.ok && rolled.ok &&
                battle.status().value == "running" && battle.phase().value == "acting" &&
                moved.value.cost == 100 && rolled.value.len() > 0 && event.ok && event.value.type == "random.rolled" &&
                battle.ownership() == "owned" && !battle.isStale()) {
                result = "ok";
            }
        }
    )"));
    CHECK_EQ(vm.find("result").toString(), std::string("ok"));
}

TEST_CASE("tactics.scriptReleaseMakesProxyStale") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    ssq::VM          vm(1024, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        result <- "fail";
        local created = eve.Tactics().newBattle("00000000-0000-0000-0000-0000000000e0", 1);
        local battle = created.ok ? created.value : null;
        if (battle != null) {
            local released = battle.release();
            if (released.ok && battle.isStale() && !battle.addCell(0, 0, 0, 100).ok) result = "ok";
        }
    )"));
    CHECK_EQ(vm.find("result").toString(), std::string("ok"));
}

TEST_CASE("tactics.scriptReactionWindowOpensAcceptsAndDeclines") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    ssq::VM          vm(4096, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        result <- "fail";
        local module = eve.Tactics();
        local created = module.newBattle("00000000-0000-0000-0000-000000000100", 3);
        local battle = created.ok ? created.value : null;
        if (battle != null) {
            local actorId = "00000000-0000-0000-0000-000000000101";
            local defenderId = "00000000-0000-0000-0000-000000000102";
            local sideId = "00000000-0000-0000-0000-000000000103";
            local c0 = battle.addCell(0, 0, 0, 100);
            local c1 = battle.addCell(1, 0, 0, 100);
            local side = battle.addSide(sideId);
            // defender carries one reaction point, actor carries none
            local u0 = battle.addUnit(actorId, sideId, "test:unit", 0, 0, 0, 1, 0, 0, 20);
            local u1 = battle.addUnit(defenderId, sideId, "test:unit", 1, 0, 0, 1, 0, 1, 10);
            local started = battle.start("initiative");
            local p1 = battle.advance(1, 1);
            local p2 = battle.advance(2, 1);
            local p3 = battle.advance(3, 1);

            // A malformed candidate array must be refused with a path, not crash.
            local badJson = battle.openReaction(4, "[{\"reactor\":\"not-a-uuid\",\"action\":\"tactics:counter\"}]");
            // An empty candidate list is refused.
            local emptyJson = battle.openReaction(4, "[]");

            local opened = battle.openReaction(4,
                "[{\"reactor\":\"" + defenderId + "\",\"action\":\"tactics:counter\"," +
                "\"priority\":5,\"initiative\":10}]");
            local accepted = battle.acceptReaction(defenderId, "tactics:counter");
            // The window is closed now, so there is nothing left to decline.
            local declined = battle.declineReaction();

            if (c0.ok && c1.ok && side.ok && u0.ok && u1.ok && started.ok &&
                p1.ok && p2.ok && p3.ok &&
                !badJson.ok && !emptyJson.ok &&
                opened.ok && opened.value == 1 &&
                accepted.ok && accepted.value.remainingReactionPoints == 0 &&
                !declined.ok) {
                result = "ok";
            }
        }
    )"));
    CHECK_EQ(vm.find("result").toString(), std::string("ok"));
}

TEST_CASE("tactics.scriptEndTurnAndFinishAreReachableWithoutWait") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    ssq::VM          vm(2048, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        result <- "fail";
        local module = eve.Tactics();
        local created = module.newBattle("00000000-0000-0000-0000-0000000000f0", 7);
        local battle = created.ok ? created.value : null;
        if (battle != null) {
            local c0 = battle.addCell(0, 0, 0, 100);
            local side = battle.addSide("00000000-0000-0000-0000-0000000000f1");
            local unit = battle.addUnit("00000000-0000-0000-0000-0000000000f2",
                                        "00000000-0000-0000-0000-0000000000f1",
                                        "test:unit", 0, 0, 0, 1, 100, 1, 10);
            local started = battle.start("initiative");
            local p1 = battle.advance(1, 1);
            local p2 = battle.advance(2, 1);
            local p3 = battle.advance(3, 1);
            // endTurn takes the battle out of Acting without forcing a wait action.
            local ended = battle.endTurn("00000000-0000-0000-0000-0000000000f2");
            local phaseAfterEndTurn = battle.phase().value;
            local turnEvent = battle.eventAt(battle.eventCount() - 1);
            // finish closes a still-running battle from any phase.
            local finished = battle.finish();
            local statusAfterFinish = battle.status().value;
            local phaseAfterFinish = battle.phase().value;
            local battleEvent = battle.eventAt(battle.eventCount() - 1);
            // A second finish must be refused, not silently accepted.
            local secondFinish = battle.finish();
            if (c0.ok && side.ok && unit.ok && started.ok && p1.ok && p2.ok && p3.ok &&
                ended.ok && finished.ok && !secondFinish.ok &&
                phaseAfterEndTurn == "turn_end" && statusAfterFinish == "ended" &&
                phaseAfterFinish == "battle_end" && turnEvent.value.type == "turn.completed" &&
                battleEvent.value.type == "battle.ended") {
                result = "ok";
            }
        }
    )"));
    CHECK_EQ(vm.find("result").toString(), std::string("ok"));
}

TEST_CASE("tactics.scriptPreviewReachabilityAndResourcesMatchCommit") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    ssq::VM          vm(4096, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        result <- "fail";
        local module = eve.Tactics();
        local created = module.newBattle("00000000-0000-0000-0000-000000000110", 5);
        local battle = created.ok ? created.value : null;
        if (battle != null) {
            local unitId = "00000000-0000-0000-0000-000000000111";
            local sideId = "00000000-0000-0000-0000-000000000112";
            local c0 = battle.addCell(0, 0, 0, 100);
            local c1 = battle.addCell(1, 0, 0, 100);
            local c2 = battle.addCell(2, 0, 0, 100);
            local side = battle.addSide(sideId);
            // actionPoints 1, movePoints 300, reactionPoints 0, initiative 10
            local u = battle.addUnit(unitId, sideId, "test:unit", 0, 0, 0, 1, 300, 0, 10);
            local started = battle.start("initiative");
            local p1 = battle.advance(1, 1);
            local p2 = battle.advance(2, 1);
            local p3 = battle.advance(3, 1);

            local before = battle.unitResources(unitId);
            local reach = battle.reachable(unitId, 300);
            // unknown metric must be refused, not silently defaulted
            local badMetric = battle.cellsInRange(0, 0, 0, 1, 2, "nope");
            // manhattan distances from (0,0): 0,1,2 -> [1,2] keeps two cells
            local range = battle.cellsInRange(0, 0, 0, 1, 2, "manhattan");
            // preview of the full-length move: two steps of cost 100
            local preview = battle.previewMove(unitId, 2, 0, 0);
            local previewWait = battle.previewWait(unitId);

            local moved = battle.move(unitId, 1, 0, 0);
            // after committing one step, the remaining cost to (2,0) is one step
            local afterPreview = battle.previewMove(unitId, 2, 0, 0);
            local after = battle.unitResources(unitId);

            local ended = battle.endTurn(unitId);
            local afterEnd = battle.unitResources(unitId);

            if (c0.ok && c1.ok && c2.ok && side.ok && u.ok && started.ok &&
                p1.ok && p2.ok && p3.ok &&
                before.ok && before.value.movePoints == 300 && before.value.acted == false &&
                reach.ok && reach.value.cells.len() == 3 &&
                !badMetric.ok &&
                range.ok && range.value.len() == 2 &&
                preview.ok && preview.value.cost == 200 && previewWait.ok &&
                moved.ok && afterPreview.ok && afterPreview.value.cost == 100 &&
                after.value.movePoints == 200 &&
                ended.ok && afterEnd.value.acted == true) {
                result = "ok";
            }
        }
    )"));
    CHECK_EQ(vm.find("result").toString(), std::string("ok"));
}

TEST_CASE("tactics.scriptEdgesAreDirectedAndAffectReachability") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    ssq::VM          vm(4096, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        result <- "fail";
        local module = eve.Tactics();
        local created = module.newBattle("00000000-0000-0000-0000-000000000120", 9);
        local battle = created.ok ? created.value : null;
        if (battle != null) {
            local unitId = "00000000-0000-0000-0000-000000000121";
            local sideId = "00000000-0000-0000-0000-000000000122";
            local c0 = battle.addCell(0, 0, 0, 100);
            local c1 = battle.addCell(1, 0, 0, 100);
            local c2 = battle.addCell(2, 0, 0, 100);
            local side = battle.addSide(sideId);
            local u = battle.addUnit(unitId, sideId, "test:unit", 0, 0, 0, 1, 1000, 0, 10);

            // Malformed spec shapes are refused before they reach the board.
            local badBool = battle.addEdge(0, 0, 0, 1, 0, 0, "{\"passable\":1}");
            local badTags = battle.addEdge(0, 0, 0, 1, 0, 0, "{\"tags\":[1]}");
            // (2,0) is two steps away, so it is not adjacent to (0,0).
            local notAdjacent = battle.addEdge(0, 0, 0, 2, 0, 0, "");

            local declared = battle.addEdge(0, 0, 0, 1, 0, 0, "{\"passable\":false,\"tags\":[\"door\"]}");
            local duplicate = battle.addEdge(0, 0, 0, 1, 0, 0, "");
            local forward = battle.edge(0, 0, 0, 1, 0, 0);
            local forwardPresent = battle.hasEdge(0, 0, 0, 1, 0, 0);
            local reversePresent = battle.hasEdge(1, 0, 0, 0, 0, 0);

            local started = battle.start("initiative");
            local p1 = battle.advance(1, 1);
            local p2 = battle.advance(2, 1);
            local p3 = battle.advance(3, 1);
            // The only outgoing direction is blocked, so only the origin is reachable.
            local reach = battle.reachable(unitId, 1000);

            if (c0.ok && c1.ok && c2.ok && side.ok && u.ok && started.ok &&
                p1.ok && p2.ok && p3.ok &&
                !badBool.ok && !badTags.ok && !notAdjacent.ok &&
                declared.ok && !duplicate.ok &&
                forward.ok && forward.value.passable == false &&
                forward.value.tags.len() == 1 && forward.value.tags[0] == "door" &&
                forwardPresent && !reversePresent &&
                reach.ok && reach.value.cells.len() == 1) {
                result = "ok";
            }
        }
    )"));
    CHECK_EQ(vm.find("result").toString(), std::string("ok"));
}

TEST_CASE("tactics.scriptAbilityDeclarationIsCostedAndLogged") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    ssq::VM          vm(4096, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        result <- "fail";
        local module = eve.Tactics();
        local created = module.newBattle("00000000-0000-0000-0000-000000000140", 14);
        local battle = created.ok ? created.value : null;
        if (battle != null) {
            local unitId = "00000000-0000-0000-0000-000000000141";
            local sideId = "00000000-0000-0000-0000-000000000142";
            local c0 = battle.addCell(0, 0, 0, 100);
            local c1 = battle.addCell(1, 0, 0, 100);
            local side = battle.addSide(sideId);
            // Two action points, so exactly two declarations are affordable.
            local u = battle.addUnit(unitId, sideId, "test:unit", 0, 0, 0, 2, 300, 0, 10);
            local started = battle.start("initiative");
            local p1 = battle.advance(1, 1);
            local p2 = battle.advance(2, 1);
            local p3 = battle.advance(3, 1);

            local first = battle.useAbility(unitId, "test:strike", 1, 0, 0, unitId, "{\"power\":3}");
            // Refusals run while a point is still available, so they exercise the
            // validation path rather than the exhausted-resource path.
            local badLayer = battle.useAbility(unitId, "test:strike", 1, 0, 4, "", "");
            local badAction = battle.useAbility(unitId, "", 1, 0, 0, "", "");
            local badTarget = battle.useAbility(unitId, "test:strike", 1, 0, 0, "not-a-uuid", "");
            // A preview runs the commit validator without spending anything.
            local preview = battle.previewAbility(unitId, "test:strike", 0, 0, 0, unitId, "");
            local afterPreview = battle.unitResources(unitId);
            local second = battle.useAbility(unitId, "test:strike", 0, 0, 0, "", "");
            local exhausted = battle.useAbility(unitId, "test:strike", 1, 0, 0, "", "");

            local resources = battle.unitResources(unitId);
            local all = battle.commandsFrom(0);
            local declared = 0;
            local sawActor = false;
            local sawCell = false;
            local sawTarget = false;
            local sawPayload = false;
            if (all.ok) {
                foreach (command in all.value) {
                    if (command.kind == "use_ability") {
                        declared = declared + 1;
                        if (command.actor == unitId) sawActor = true;
                        if (command.cell.x == 1 && command.cell.y == 0 && command.cell.layer == 0) sawCell = true;
                        if (command.targetUnit == unitId) sawTarget = true;
                        if (command.payload == "{\"power\":3}") sawPayload = true;
                    }
                }
            }

            if (c0.ok && c1.ok && side.ok && u.ok && started.ok &&
                p1.ok && p2.ok && p3.ok &&
                first.ok && second.ok && !exhausted.ok && !badLayer.ok && !badAction.ok && !badTarget.ok &&
                preview.ok && preview.value.remainingActionPoints == 0 &&
                afterPreview.ok && afterPreview.value.actionPoints == 1 &&
                resources.ok && resources.value.actionPoints == 0 &&
                all.ok && declared == 2 && sawActor && sawCell && sawTarget && sawPayload) {
                result = "ok";
            }
        }
    )"));
    CHECK_EQ(vm.find("result").toString(), std::string("ok"));
}

TEST_CASE("tactics.scriptCommandLogIsReadableAndRevisionScoped") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    ssq::VM          vm(4096, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        result <- "fail";
        local module = eve.Tactics();
        local created = module.newBattle("00000000-0000-0000-0000-000000000130", 13);
        local battle = created.ok ? created.value : null;
        if (battle != null) {
            local unitId = "00000000-0000-0000-0000-000000000131";
            local sideId = "00000000-0000-0000-0000-000000000132";
            local c0 = battle.addCell(0, 0, 0, 100);
            local c1 = battle.addCell(1, 0, 0, 100);
            local side = battle.addSide(sideId);
            local u = battle.addUnit(unitId, sideId, "test:unit", 0, 0, 0, 1, 300, 0, 10);
            local started = battle.start("initiative");
            local p1 = battle.advance(1, 1);
            local p2 = battle.advance(2, 1);
            local p3 = battle.advance(3, 1);
            local moved = battle.move(unitId, 1, 0, 0);
            local ended = battle.endTurn(unitId);

            // Negative revision is a validation error, not an empty log.
            local negative = battle.commandsFrom(-1);
            local all = battle.commandsFrom(0);
            local future = battle.commandsFrom(999999);

            local sawStart = false;
            local sawMove = false;
            local startPolicy = "";
            if (all.ok) {
                foreach (command in all.value) {
                    if (command.kind == "start") { sawStart = true; startPolicy = command.policyId; }
                    if (command.kind == "move") sawMove = true;
                }
            }

            if (c0.ok && c1.ok && side.ok && u.ok && started.ok &&
                p1.ok && p2.ok && p3.ok && moved.ok && ended.ok &&
                !negative.ok &&
                all.ok && all.value.len() >= 5 && sawStart && sawMove &&
                startPolicy == "initiative" &&
                future.ok && future.value.len() == 0) {
                result = "ok";
            }
        }
    )"));
    CHECK_EQ(vm.find("result").toString(), std::string("ok"));
}
}  // namespace test_tactics_script
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_tactics_snapshot_script)
#include "PrivateTestScope.h"
#include <memory>
#include <string>
#include <utility>
#include "common/Capability.h"
#include "common/ECS.h"
#include "common/SnapshotHash.h"
#include "tactics/Tactics.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// tactics_snapshot_script.cpp
namespace test_tactics_snapshot_script {
namespace {

[[maybe_unused]] eve::SubjectRef subject(const char* text) {
    const auto id = eve::PersistentId::parse(text);
    REQUIRE(id.has_value());
    return eve::SubjectRef::fromPersistentId(*id);
}

[[maybe_unused]] eve::SimulationStep step(std::uint64_t tick) {
    return {eve::SimulationTick(tick), eve::Duration::fromNanoseconds(1)};
}

/** @brief A hasher that counts calls and delegates, to prove the registration is used. */
class CountingHasher final : public eve::ISnapshotContentHasher {
public:
    [[nodiscard]] std::string_view       id() const noexcept override { return "test-counting-noncrypto"; }
    [[nodiscard]] eve::ContentDigestKind digestKind() const noexcept override {
        return eve::ContentDigestKind::NonCryptographic;
    }
    [[nodiscard]] eve::Result<eve::ContentId> hash(std::string_view canonicalInput) const override {
        ++calls;
        return eve::builtinSnapshotContentHasher()->hash(canonicalInput);
    }

    mutable int calls = 0;
};

struct Fixture {
    ecs::Table            world;
    ecs::ScopedTable      guard{world};
    eve::tactics::Tactics tactics;
    ecs::EntityHandle     battle{};
    eve::SubjectRef       unitSubject = subject("00000000-0000-0000-0000-000000000900");

    void build() {
        auto created = tactics.newBattle(subject("00000000-0000-0000-0000-000000000901"), 11);
        REQUIRE(created.ok());
        battle = std::move(created).takeValue();
        REQUIRE(tactics.addCell(battle, {0, 0, 0}).ok());
        REQUIRE(tactics.addCell(battle, {1, 0, 0}).ok());
        auto side = tactics.newSide(battle, subject("00000000-0000-0000-0000-000000000902"));
        REQUIRE(side.ok());
        REQUIRE(tactics.newUnit(battle, side.value(), unitSubject, {}, {0, 0, 0}, {2, 300, 0, 10}).ok());
        REQUIRE(tactics.start(battle, eve::tactics::kInitiativePolicyId).ok());
        REQUIRE(tactics.advance(battle, step(1)).ok());
        REQUIRE(tactics.advance(battle, step(2)).ok());
        REQUIRE(tactics.advance(battle, step(3)).ok());
    }
};

}  // namespace


TEST_CASE("tactics.snapshotScriptSurfaceRoundTripsThroughSquirrel") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    ssq::VM          vm(4096, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        result <- "fail";
        local module = eve.Tactics();
        local created = module.newBattle("00000000-0000-0000-0000-000000000910", 12);
        local battle = created.ok ? created.value : null;
        if (battle != null) {
            local unitId = "00000000-0000-0000-0000-000000000911";
            local sideId = "00000000-0000-0000-0000-000000000912";
            local c0 = battle.addCell(0, 0, 0, 100);
            local c1 = battle.addCell(1, 0, 0, 100);
            local side = battle.addSide(sideId);
            local u = battle.addUnit(unitId, sideId, "test:unit", 0, 0, 0, 2, 300, 0, 10);
            local started = battle.start("initiative");
            local p1 = battle.advance(1, 1);
            local p2 = battle.advance(2, 1);
            local p3 = battle.advance(3, 1);

            // Capture the acting state and the log anchor, then play forward. The log must start
            // at the revision the snapshot was taken at, or replay is refused by design.
            local baselineRevision = battle.revision();
            local snapshot = battle.snapshotJson();
            local log = battle.commandLogJson(baselineRevision);
            local algorithm = battle.snapshotAlgorithm();
            local moved = battle.move(unitId, 1, 0, 0);
            local ended = battle.endTurn(unitId);
            local directCell = battle.unitCell(unitId);
            local directPhase = battle.phase().value;

            // Restore and replay the captured log: the battle must come back byte for byte.
            local restored = battle.restoreJson(snapshot.value);
            local replayed = battle.replayJson(log.value);
            local replayCell = battle.unitCell(unitId);
            local replayPhase = battle.phase().value;

            if (c0.ok && c1.ok && side.ok && u.ok && started.ok &&
                p1.ok && p2.ok && p3.ok && moved.ok && ended.ok &&
                snapshot.ok && log.ok && restored.ok && replayed.ok &&
                algorithm == "fnv1a64x2-noncrypto" &&
                directCell.value.x == 1 && replayCell.value.x == 1 &&
                directPhase == "turn_end" && replayPhase == "turn_end") {
                result = "ok";
            }
        }
    )"));
    CHECK_EQ(vm.find("result").toString(), std::string("ok"));
}
}  // namespace test_tactics_snapshot_script
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_tags)
#include "PrivateTestScope.h"
#include "ScriptTest.h"
#include "common/Capability.h"
#include "common/StateAccess.h"
#include "tags/TagStore.h"
#include "tags/Tags.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// tags.cpp
namespace test_tags {
using namespace eve;


using eve::tags::Tags;
using eve::tags::TagStore;


static const char* kTagsScript = R"SQ(
function testTagsBindings() {
    local tags = eve.Tags();
    if (!tags.add("subject:b", "zeta")) return false;
    if (!tags.add("subject:b", "alpha")) return false;
    if (!tags.add("subject:a", "alpha")) return false;
    if (tags.at("subject:b", 0) != "alpha") return false;
    if (tags.subjectAt("alpha", 0) != "subject:a") return false;
    if (!tags.requireAll("subject:b", ["alpha", "zeta"])) return false;
    if (!tags.requireAny("subject:b", ["missing", "zeta"])) return false;
    if (!tags.addCapability("subject:b", "can.perform")) return false;
    if (!tags.requireAllCapabilities("subject:b", ["can.perform"])) return false;
    if (!tags.hasCapability("subject:b", "can.perform")) return false;
    return tags.eventAction(tags.eventCount() - 1) == "capability_added";
}
)SQ";

UnitSciptTest(TagsScriptTest, kTagsScript);

TEST_CASE_FIXTURE(TagsScriptTest, "tags.script.bindings") { REQUIRE(callScript("testTagsBindings")); }
}  // namespace test_tags
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_map_tile_collision_sink)
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>
#include "PrivateTestScope.h"
#include "ScriptTest.h"
#include "common/Capability.h"
#include "common/ScriptProvider.h"
#include "map/Map.h"
#include "map/TileCollision.h"
#include "map/TileCollisionScriptSink.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// map_tile_collision_sink.cpp
namespace test_map_tile_collision_sink {
// ---------------------------------------------------------------------------
// 决定③ 的第三个落地插座：`ITileCollisionSink` 可以由脚本提供。
//
// 这一片的前置是**接口改**（§9.71.1）：参数从 `const void* layer` 换成稳定身份 `TileLayerRef`
// （ECS 槽位 + 换代号）。脚本因此既能认出"这批矩形是哪一层的"，也看得懂过期引用。派发走的是既有
// 的 listener 面（`Map::publishCollision` 用 `cap::forEach`），所以原生 sink 与脚本 sink 可以同时
// 在位、按优先级依次收到同一批几何。
// ---------------------------------------------------------------------------

namespace {

using namespace eve;
using namespace eve::map;

/** 脚本侧记录的层身份形状：`"<槽位>:<换代号>,"`。 */
std::string formatLayer(TileLayerRef layer) {
    return std::to_string(layer.slot) + ":" + std::to_string(layer.generation) + ",";
}

const char* kScriptTileCollisionSink = R"SQ(
calls <- 0
seenLayerId <- -1
seenLayerGeneration <- -1
seenCount <- -1
seenX <- -1.0
seenY <- -1.0
seenWidth <- -1.0
seenHeight <- -1.0
recordedLayers <- ""
recordedSlots <- ""
selfLayerId <- -1
selfLayerGeneration <- -1
selfRects <- -1

class CollisionSink {
    function replaceTileCollision(payload) {
        calls = calls + 1
        seenLayerId = payload.layer.id
        seenLayerGeneration = payload.layer.generation
        seenCount = payload.rects.len()
        if (seenCount > 0) {
            seenX = payload.rects[0].x
            seenY = payload.rects[0].y
            seenWidth = payload.rects[0].width
            seenHeight = payload.rects[0].height
        }
    }
}

// 记录每一次发布的层身份：用来看"脚本认得出这是哪一层"。
class RecordingSink {
    function replaceTileCollision(payload) {
        recordedLayers = recordedLayers + format("%d:%d,", payload.layer.id, payload.layer.generation)
        recordedSlots = recordedSlots + format("%d,", payload.rects.len())
    }
}

class BrokenSink {
    function replaceTileCollision(payload) { throw "sink refused the geometry" }
}

class SelfSink {
    function replaceTileCollision(payload) {
        selfLayerId = payload.layer.id
        selfLayerGeneration = payload.layer.generation
        selfRects = payload.rects.len()
    }
}

sink <- CollisionSink()
recorder <- RecordingSink()
broken <- BrokenSink()
selfSink <- SelfSink()

// 整条回路都在脚本里：建层、发布 sink、派发、按 `getEntityId()` 认自己那一层、退订。
// 这就是这一片的验收点——**不需要任何 C++ 代码**。
function runCollisionLoopFromScript() {
    local map = eve.Map()
    local layer = map.newLayer(2, 2, 16.0, 16.0)
    if (layer == null) return false
    // 绑定要求传满每个实参（R-FACE-6）：`cost` 在 C++ 侧有默认值，脚本侧必须显式给。
    layer.setTileMetadata(9, 1, 1, false, 1.0)
    layer.fillRect(0, 0, 2, 1, 9)
    local published = eve.provider.publishFor("ITileCollisionSink", "test.collision.self", selfSink)
    if (!published.ok) return false
    local rects = map.publishCollision(layer)
    if (rects != 1) return false
    if (selfLayerId != layer.getEntityId()) return false
    if (selfLayerGeneration != layer.getEntityGeneration()) return false
    if (selfRects != 1) return false
    local unloaded = eve.provider.unload("test.collision.self")
    return unloaded.ok
}
)SQ";

}  // namespace

/** 接缝由 `Map` 模块构造时声明，所以夹具只准备 VM 与隔离。 */
class MapTileCollisionSinkTest : public ScriptTest {
public:
    MapTileCollisionSinkTest() : ScriptTest(kScriptTileCollisionSink) {
        auto& registry = eve::ScriptProviderRegistry::instance();
        registry.clearAll();
        declaredBefore_ = registry.capabilities();
        eve::cap::detail::clearAllRaw();
        eve::map::resetScriptTileCollisionSinkFailures();
    }

    ~MapTileCollisionSinkTest() {
        eve::map::withdrawScriptTileCollisionSinkSeam();
        eve::cap::detail::clearAllRaw();
        auto& registry = eve::ScriptProviderRegistry::instance();
        registry.clearAll();
        for (const std::string& capability : registry.capabilities()) {
            if (std::find(declaredBefore_.begin(), declaredBefore_.end(), capability) != declaredBefore_.end())
                continue;
            registry.removeCapability(capability).ignore("fixture teardown withdraws only this case's seams");
        }
    }

    /** 发布一个脚本提供方；返回结果表，测试据此断言接线成没成。 */
    ssq::Object publish(const char* name, const char* instance) {
        const std::string source = std::string("published <- eve.provider.publishFor(\"ITileCollisionSink\", \"") +
                                   name + "\", " + instance + ")";
        vm.run(vm.compileSource(source.c_str()));
        return vm.find("published");
    }

private:
    std::vector<std::string> declaredBefore_;
};

TEST_CASE_FIXTURE(MapTileCollisionSinkTest, "map.collision.scriptSinkReceivesLayerIdentityAndGeometry") {
    Map        map;
    TileLayer* layer = map.newLayer(5, 4, 16.f, 8.f);
    REQUIRE(layer != nullptr);
    layer->setTileMetadata(9, 1, 1, false);
    layer->fillRect(1, 1, 3, 2, 9);

    REQUIRE(eve::ScriptProviderRegistry::instance().hasCapabilitySeam("ITileCollisionSink"));
    REQUIRE_EQ(eve::cap::listenerCount<ITileCollisionSink>(), std::size_t{0});

    ssq::Object published = publish("test.collision", "sink");
    REQUIRE_EQ(published.getType(), ssq::Type::TABLE);
    REQUIRE(published.find("ok").toBool());
    REQUIRE_EQ(eve::cap::listenerCount<ITileCollisionSink>(), std::size_t{1});

    // 合并出来的那一个矩形：原点 (16, 8)、尺寸 48x16。
    // 带副作用的调用先落到局部变量：`REQUIRE_EQ` 会把实参各求值两次（§9.76.3），
    // 直接把 `publishCollision` 写进断言会派发两次。
    const int publishedRects = map.publishCollision(layer);
    REQUIRE_EQ(publishedRects, 1);
    REQUIRE_EQ(vm.find("calls").toInt(), 1);
    REQUIRE_EQ(vm.find("seenLayerId").toInt(), int(layer->layerRef().slot));
    REQUIRE_EQ(vm.find("seenLayerGeneration").toInt(), int(layer->layerRef().generation));
    REQUIRE_EQ(vm.find("seenCount").toInt(), 1);
    REQUIRE_EQ(vm.find("seenX").toFloat(), 16.0);
    REQUIRE_EQ(vm.find("seenY").toFloat(), 8.0);
    REQUIRE_EQ(vm.find("seenWidth").toFloat(), 48.0);
    REQUIRE_EQ(vm.find("seenHeight").toFloat(), 16.0);

    // 脚本 sink 不会替换 map 自己的权威矩形表：`collisionRects_` 仍然是唯一权威。
    REQUIRE_EQ(map.getCollisionRectCount(), 1);
    REQUIRE_EQ(map.getCollisionRectWidth(0), 48.f);

    // 退订之后派发停止，而 map 侧的行为完全不变。
    vm.run(vm.compileSource("unloaded <- eve.provider.unload(\"test.collision\")"));
    REQUIRE(vm.find("unloaded").find("ok").toBool());
    REQUIRE_EQ(eve::cap::listenerCount<ITileCollisionSink>(), std::size_t{0});
    const int rectsAfterUnload = map.publishCollision(layer);
    REQUIRE_EQ(rectsAfterUnload, 1);
    REQUIRE_EQ(vm.find("calls").toInt(), 1);
}

TEST_CASE_FIXTURE(MapTileCollisionSinkTest, "map.collision.scriptSinkCanTellLayersApart") {
    Map        map;
    TileLayer* first  = map.newLayer(3, 3, 16.f, 16.f);
    TileLayer* second = map.newLayer(3, 3, 16.f, 16.f);
    REQUIRE(first != nullptr);
    REQUIRE(second != nullptr);
    REQUIRE(first->layerRef().slot != second->layerRef().slot);

    first->setTileMetadata(9, 1, 1, false);
    first->setTile(0, 0, 9);
    second->setTileMetadata(9, 1, 1, false);
    second->setTile(0, 0, 9);
    second->setTile(2, 0, 9);  // 与 (0,0) 不相邻，所以合并后是两个矩形

    ssq::Object published = publish("test.collision.recording", "recorder");
    REQUIRE(published.find("ok").toBool());

    const int firstRects  = map.publishCollision(first);
    const int secondRects = map.publishCollision(second);
    REQUIRE_EQ(firstRects, 1);
    REQUIRE_EQ(secondRects, 2);

    // 层身份进得来，所以脚本分得清两次发布属于哪一层（这正是换掉 `const void*` 的收益）。
    const std::string expectedLayers = formatLayer(first->layerRef()) + formatLayer(second->layerRef());
    REQUIRE_EQ(vm.find("recordedLayers").toString(), expectedLayers);
    REQUIRE_EQ(vm.find("recordedSlots").toString(), std::string("1,2,"));
}

TEST_CASE_FIXTURE(MapTileCollisionSinkTest, "map.collision.scriptSinkFailureIsCountedNotSwallowed") {
    Map        map;
    TileLayer* layer = map.newLayer(5, 4, 16.f, 8.f);
    REQUIRE(layer != nullptr);
    layer->setTileMetadata(9, 1, 1, false);
    layer->fillRect(1, 1, 3, 2, 9);

    ssq::Object published = publish("test.collision.broken", "broken");
    REQUIRE(published.find("ok").toBool());
    REQUIRE_EQ(eve::map::scriptTileCollisionSinkFailures(), std::uint64_t{0});

    // 接口是 `void`（通知不是事务），所以"脚本拒绝了"必须从计数上看出来——否则"没接上"和
    // "接上了但每次都失败"从外部完全一样。
    const int rejectedRects = map.publishCollision(layer);
    REQUIRE_EQ(rejectedRects, 1);
    REQUIRE_EQ(eve::map::scriptTileCollisionSinkFailures(), std::uint64_t{1});
    REQUIRE_EQ(map.getCollisionRectCount(), 1);

    // 提供方在失败后还活着：同一代继续派发，计数继续涨。
    const int rejectedAgain = map.publishCollision(layer);
    REQUIRE_EQ(rejectedAgain, 1);
    REQUIRE_EQ(eve::map::scriptTileCollisionSinkFailures(), std::uint64_t{2});

    // 换一个能用的脚本 sink：派发恢复，计数不再变。
    REQUIRE(
        eve::ScriptProviderRegistry::instance()
            .unload(eve::ScriptProviderHandle{
                "test.collision.broken", eve::ScriptProviderRegistry::instance().generationOf("test.collision.broken")})
            .ok());
    REQUIRE(publish("test.collision.good", "sink").find("ok").toBool());
    const int recoveredRects = map.publishCollision(layer);
    REQUIRE_EQ(recoveredRects, 1);
    REQUIRE_EQ(eve::map::scriptTileCollisionSinkFailures(), std::uint64_t{2});
    REQUIRE_EQ(vm.find("calls").toInt(), 1);
}

TEST_CASE_FIXTURE(MapTileCollisionSinkTest, "map.collision.scriptOwnsTheWholeCollisionLoop") {
    // 建层、发布、派发、按身份认层、退订全在脚本里：这就是"用户只写脚本"的验收形态。
    // 身份对不对由脚本自己比（`selfLayerId != layer.getEntityId()` 就返回 false）；这里只确认
    // 脚本确实收到了那次派发——槽位从 0 开始，所以断言用**收到过**而不是"大于 0"。
    REQUIRE(callScript("runCollisionLoopFromScript"));
    REQUIRE_EQ(vm.find("selfRects").toInt(), 1);
    REQUIRE(vm.find("selfLayerGeneration").toInt() >= 1);
    REQUIRE_EQ(eve::cap::listenerCount<ITileCollisionSink>(), std::size_t{0});
}

TEST_CASE_FIXTURE(MapTileCollisionSinkTest, "map.collision.scriptSinkSeamIsAbsentWithoutTheModule") {
    // 没有 `Map` 实例 = 模块从没声明过接缝：发布被明确拒绝（`not_found`），而不是"绑了但不响"。
    REQUIRE(!eve::ScriptProviderRegistry::instance().hasCapabilitySeam("ITileCollisionSink"));
    ssq::Object refused = publish("test.collision.orphan", "sink");
    REQUIRE_EQ(refused.getType(), ssq::Type::TABLE);
    REQUIRE(!refused.find("ok").toBool());
    REQUIRE_EQ(refused.find("code").toString(), std::string("not_found"));
    REQUIRE_EQ(eve::cap::listenerCount<ITileCollisionSink>(), std::size_t{0});
}
}  // namespace test_map_tile_collision_sink
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_motion_script_sink)
#include <cmath>
#include <string>
#include <vector>
#include "PrivateTestScope.h"
#include "ScriptTest.h"
#include "animation/Animation.h"
#include "animation/MotionRuntime.h"
#include "animation/MotionScriptBindings.h"
#include "common/ScriptProvider.h"
#include "common/Time.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// motion_script_sink.cpp
namespace test_motion_script_sink {
// ---------------------------------------------------------------------------
// 阶段 5 的最后一个插座：动画侧的 `IMotionFloatSink` 可以由脚本对象扮演。
//
// 这一条的形态和其它插座不同：`MotionRuntime` 的槽位**直接持有** sink 指针（每个 motion 一个 sink，
// 不是全局能力槽），所以接法不是 `cap::provide`，而是**让 builder 接受一个脚本 sink 对象**
// （§9.71.1 给的正是这一条路）。适配器实现 `IMotionFloatSink`，脚本对象实现 `write(value)`，而
// `write` 的失败会原样回到 runtime —— `cancelOnError` 因此对脚本 sink 同样有效。
//
// 类型全部写全限定名：这一 TU 里 `animation` 是命名空间，用 `using namespace eve::animation` 会与
// 局部变量名打架。
// ---------------------------------------------------------------------------

namespace {

using namespace eve;

const char* kScriptMotionSink = R"SQ(
received <- []
writeCount <- 0
lastValue <- -1.0

class RecordingSink {
    function write(value) {
        received.append(value)
        writeCount = writeCount + 1
        lastValue = value
    }
}

class BrokenSink {
    function write(value) { throw "motion sink refused the value" }
}

// 初始化值（from = 0）收下，之后任何一个值都拒绝：用来看"运行中失败"这条路。
class LateBrokenSink {
    function write(value) { if (value > 0.0) throw "motion sink refused a later value" }
}

sink <- RecordingSink()
broken <- BrokenSink()
lateBroken <- LateBrokenSink()
bindError <- ""

function startMotionWithSink() {
    local anim = eve.Animation()
    // `bind(sink)` = 绑上脚本 sink 并开跑（对应 C++ 的 MotionBuilder::bind）；
    // 要放进 sequence 就先 `to(sink)` 再 append，那条路不启动。
    local motion = anim.newMotion(0.0, 10.0, 1.0).bind(sink)
    return motion != null && motion.isActive()
}

// spawn 会先推一次初始化值，所以"一上来就拒绝"的 sink 会让 bind 自己失败。
function startMotionWithImmediateBrokenSink() {
    local anim = eve.Animation()
    try {
        local motion = anim.newMotion(0.0, 10.0, 1.0).bind(broken)
        return motion != null
    } catch (e) {
        bindError = e
        return false
    }
}

function startMotionWithLateBrokenSink() {
    local anim = eve.Animation()
    local motion = anim.newMotion(0.0, 10.0, 1.0).bind(lateBroken)
    return motion != null && motion.isActive()
}

function startMotionWithoutSink() {
    local anim = eve.Animation()
    local motion = anim.newMotion(0.0, 10.0, 1.0).run()
    return motion != null
}

// ---- 非 float 形状：vec2 / vec3 / color / quat 的脚本 builder + 脚本 sink ----

vec2Sum <- -1.0
vec3Sum <- -1.0
colorSum <- -1.0
colorLastA <- -1.0
quatSum <- -1.0

class Vec2Sink { function write(x, y) { vec2Sum = x + y } }
class Vec3Sink { function write(x, y, z) { vec3Sum = x + y + z } }
class ColorSink { function write(r, g, b, a) { colorSum = r + g + b; colorLastA = a } }
class QuatSink { function write(x, y, z, w) { quatSum = x + y + z + w } }

vec2Sink <- Vec2Sink()
vec3Sink <- Vec3Sink()
colorSink <- ColorSink()
quatSink <- QuatSink()

// 四种形状各起一条，全部绑脚本 sink：这就是"最后四个 IMotion*Sink 也接上了"。
function startNonFloatMotions() {
    local anim = eve.Animation()
    local two = anim.newMotionVec2({ x = 0.0, y = 0.0 }, { x = 2.0, y = 4.0 }, 1.0).bind(vec2Sink)
    local three = anim.newMotionVec3({ x = 0.0, y = 0.0, z = 0.0 },
                                     { x = 1.0, y = 2.0, z = 3.0 }, 1.0).bind(vec3Sink)
    local color = anim.newMotionColor({ r = 0.0, g = 0.0, b = 0.0, a = 0.5 },
                                      { r = 1.0, g = 2.0, b = 3.0, a = 1.0 }, 1.0).bind(colorSink)
    // 四元数走 slerp：端点必须是**单位**四元数，否则它插到的位置不是你以为的那个（见用例断言）。
    local quat = anim.newMotionQuat({ x = 0.0, y = 0.0, z = 0.0, w = 1.0 },
                                    { x = 0.0, y = 0.0, z = 0.70710678, w = 0.70710678 }, 1.0).bind(quatSink)
    return two != null && three != null && color != null && quat != null
}

// 形状不对的表：要抛，而不是悄悄按全零跑一条动画。
function startMotionWithBadTable() {
    local anim = eve.Animation()
    try {
        local m = anim.newMotionVec3({ x = 0.0, y = 0.0 }, { x = 1.0, y = 1.0, z = 1.0 }, 1.0)
        return m != null
    } catch (e) {
        tableError = e
        return false
    }
}
tableError <- ""
)SQ";

class MotionScriptSinkTest : public ScriptTest {
public:
    // NOTE: the bundle entry (`ctest -L bundle`, opt-in) sees motions left active by an
    // earlier case, because the Animation module is process-level and MotionRuntime has no
    // public reset: case 3 starts with activeCount()==5 instead of 4. Per-case runs -- the
    // CI shape -- are clean. A `MotionRuntime::clear()` test hook or per-case cancellation
    // in this file is the fix; it is not part of the merge.
    MotionScriptSinkTest() : ScriptTest(kScriptMotionSink) { ScriptProviderRegistry::instance().clearAll(); }

    ~MotionScriptSinkTest() {
        eve::animation::clearScriptMotionSinkAdapters();
        ScriptProviderRegistry::instance().clearAll();
    }

    /** 推进一次动画：`Animation::advance` 是唯一的求值入口。 */
    void advanceAnimation(eve::animation::Animation& target, double seconds, std::uint64_t tick) {
        const eve::SimulationStep step{eve::SimulationTick{tick}, eve::Duration::fromSeconds(seconds).expect("dt")};
        auto                      result = target.advance(step);
        REQUIRE(result.ok());
    }
};

}  // namespace

TEST_CASE_FIXTURE(MotionScriptSinkTest, "animation.motion.scriptSinkReceivesEveryEvaluatedValue") {
    REQUIRE(callScript("startMotionWithSink"));
    eve::animation::Animation* module = eve::animation::Animation::create();
    REQUIRE(module != nullptr);
    REQUIRE_EQ(module->motions().activeCount(), 1);

    advanceAnimation(*module, 0.25, 1);
    advanceAnimation(*module, 0.25, 2);
    advanceAnimation(*module, 0.25, 3);

    // 每次 advance 都推一个值，而且单调上升（0→10 的线性 tween）。
    REQUIRE(vm.find("writeCount").toInt() >= 3);
    REQUIRE_EQ(vm.find("received").getType(), ssq::Type::ARRAY);
    const double last = vm.find("lastValue").toFloat();
    REQUIRE(last > 0.0);
    REQUIRE(last <= 10.0);

    // 跑完 1 秒的 tween：motion 结束，不再活跃。
    advanceAnimation(*module, 0.5, 4);
    REQUIRE_EQ(module->motions().activeCount(), 0);
    REQUIRE(std::fabs(vm.find("lastValue").toFloat() - 10.0) < 1e-3);

    // 撤回适配器与 provider 登记（模块析构走同一条路）。
    eve::animation::clearScriptMotionSinkAdapters();
    REQUIRE(!ScriptProviderRegistry::instance().contains("animation.motion-sink.1"));
}

TEST_CASE_FIXTURE(MotionScriptSinkTest, "animation.motion.sinkIsOptionalAndOnlyBindsWhenAsked") {
    // 不调用 `to()`：motion 照跑，脚本一次都不会被调（sink 是可选的，不是隐式默认）。
    REQUIRE(callScript("startMotionWithoutSink"));
    eve::animation::Animation* module = eve::animation::Animation::create();
    REQUIRE(module != nullptr);
    REQUIRE_EQ(module->motions().activeCount(), 1);
    advanceAnimation(*module, 0.5, 1);
    REQUIRE_EQ(vm.find("writeCount").toInt(), 0);
    REQUIRE_EQ(ScriptProviderRegistry::instance().size(), std::size_t{0});
}

TEST_CASE_FIXTURE(MotionScriptSinkTest, "animation.motion.nonFloatShapesReachScriptSinksToo") {
    REQUIRE(callScript("startNonFloatMotions"));
    eve::animation::Animation* module = eve::animation::Animation::create();
    REQUIRE(module != nullptr);
    REQUIRE_EQ(module->motions().activeCount(), 4);

    // 四条一起跑：每次 advance 每个 sink 都收到自己那几维的值（位置参数编组）。
    advanceAnimation(*module, 0.25, 1);
    REQUIRE(vm.find("vec2Sum").toFloat() > 0.0);
    REQUIRE(vm.find("vec3Sum").toFloat() > 0.0);
    REQUIRE(vm.find("colorSum").toFloat() > 0.0);
    REQUIRE(vm.find("colorLastA").toFloat() > 0.0);
    REQUIRE(vm.find("quatSum").toFloat() > 0.0);

    // 跑到 1 秒结束：终点值就是 to 那一端（vec2 6、vec3 6、color 6/1、单位四元数 z+w≈1.414）。
    advanceAnimation(*module, 1.0, 2);
    REQUIRE_EQ(module->motions().activeCount(), 0);
    REQUIRE(std::fabs(vm.find("vec2Sum").toFloat() - 6.0) < 1e-3);
    REQUIRE(std::fabs(vm.find("vec3Sum").toFloat() - 6.0) < 1e-3);
    REQUIRE(std::fabs(vm.find("colorSum").toFloat() - 6.0) < 1e-3);
    REQUIRE(std::fabs(vm.find("colorLastA").toFloat() - 1.0) < 1e-3);
    REQUIRE(std::fabs(vm.find("quatSum").toFloat() - 1.41421356) < 1e-3);
}

TEST_CASE_FIXTURE(MotionScriptSinkTest, "animation.motion.badValueTablesAreRejectedNotZeroFilled") {
    // 缺字段的表要明确抛错：静默按 0 跑一条动画会让"我传错了"变成"动画看起来没生效"。
    REQUIRE(!callScript("startMotionWithBadTable"));
    REQUIRE(!vm.find("tableError").toString().empty());
    eve::animation::Animation* module = eve::animation::Animation::create();
    REQUIRE(module != nullptr);
    REQUIRE_EQ(module->motions().activeCount(), 0);
}

TEST_CASE_FIXTURE(MotionScriptSinkTest, "animation.motion.scriptSinkFailureCancelsInsteadOfBeingSwallowed") {
    eve::animation::Animation* module = eve::animation::Animation::create();
    REQUIRE(module != nullptr);

    // ① sink 连初始化值都拒绝：`bind` 自己失败（脚本看得见），没有 motion 留在运行中。
    REQUIRE(!callScript("startMotionWithImmediateBrokenSink"));
    REQUIRE(!vm.find("bindError").toString().empty());
    REQUIRE_EQ(module->motions().activeCount(), 0);

    // ② sink 在后续采样上失败：`advance` 返回失败，并且 motion 被取消（不是继续跑在一个已经
    //    不再接收值的目标上——吞掉失败就会变成那样）。
    REQUIRE(callScript("startMotionWithLateBrokenSink"));
    REQUIRE_EQ(module->motions().activeCount(), 1);
    const eve::SimulationStep step{eve::SimulationTick{1}, eve::Duration::fromSeconds(0.25).expect("dt")};
    const auto                advanced = module->advance(step);
    REQUIRE(!advanced.ok());
    REQUIRE_EQ(module->motions().activeCount(), 0);
}
}  // namespace test_motion_script_sink
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_scene_script_link)
#include <cmath>
#include <string>
#include "PrivateTestScope.h"
#include "ScriptTest.h"
#include "common/Diagnostic.h"
#include "common/ScriptProvider.h"
#include "common/Status.h"
#include "scene/NodeDesc.h"
#include "scene/Scene.h"
#include "scene/SceneHost.h"
#include "scene/TransformSystem.h"
// scene_script_link.cpp
namespace test_scene_script_link {
// Script-backed scene links (judgement criterion J-2).
//
// A user attaches their own object to the scene transform pipeline without a C++
// build: publish the instance through `eve.provider`, then link it with
// `scene.linkScriptAt`. These cases drive both sync directions through the real
// `TransformSystem` dispatch and pin the absent cases (no host, no node, nothing
// published, retired provider, broken target).


using namespace eve;
using namespace eve::scene;

namespace {

const char* kScriptLinkContent = R"SQ(
class Follower {
    pushes = 0
    lastId = ""
    lastX = 0.0
    lastY = 0.0
    lastZ = 0.0
    lastYaw = 0.0
    lastScale = 0.0

    function onNodeTransform(nodeId, worldX, worldY, worldZ, yaw, pitch, roll, sx, sy, sz) {
        pushes += 1
        lastId = nodeId
        lastX = worldX
        lastY = worldY
        lastZ = worldZ
        lastYaw = yaw
        lastScale = sx
    }
}

// Drives the node it is linked to: the local transform the node must take.
class Driver {
    x = 0.0
    y = 0.0
    z = 0.0
    yaw = 0.0

    function nodeTransform() { return [x, y, z, yaw, 0.0, 0.0] }
}

// Deliberately incomplete: two numbers where the contract wants six.
class ShortDriver {
    function nodeTransform() { return [1.0, 2.0] }
}

// Deliberately broken: raising in the push direction must not stall its siblings.
class RaisingFollower {
    function onNodeTransform(nodeId, worldX, worldY, worldZ, yaw, pitch, roll, sx, sy, sz) {
        throw "follower is broken"
    }
}

follower <- Follower()
driver <- Driver()
shortDriver <- ShortDriver()
raisingFollower <- RaisingFollower()
)SQ";

bool approxEq(float actual, float expected, float tolerance = 1e-4f) {
    return std::fabs(actual - expected) <= tolerance;
}

eve::DiagnosticCode firstDiagnosticCode(const eve::Status& status) {
    return status.diagnostics().empty() ? eve::DiagnosticCode::None : status.diagnostics().front().code();
}

/** Reads one numeric member of a live script instance whatever numeric kind it is. */
double memberNumber(const ssq::Object& instance, const char* name) {
    HSQUIRRELVM     vm  = instance.getHandle();
    const SQInteger top = sq_gettop(vm);
    sq_pushobject(vm, instance.getRaw());
    sq_pushstring(vm, name, -1);
    double out = 0.0;
    if (SQ_SUCCEEDED(sq_get(vm, -2))) {
        if (sq_gettype(vm, -1) == OT_INTEGER) {
            SQInteger value = 0;
            if (SQ_SUCCEEDED(sq_getinteger(vm, -1, &value))) out = double(value);
        } else {
            SQFloat value = 0.f;
            if (SQ_SUCCEEDED(sq_getfloat(vm, -1, &value))) out = double(value);
        }
    }
    sq_settop(vm, top);
    return out;
}

/** Reads one string member of a live script instance; "" when it is missing. */
std::string memberString(const ssq::Object& instance, const char* name) {
    HSQUIRRELVM     vm  = instance.getHandle();
    const SQInteger top = sq_gettop(vm);
    sq_pushobject(vm, instance.getRaw());
    sq_pushstring(vm, name, -1);
    std::string out;
    if (SQ_SUCCEEDED(sq_get(vm, -2))) {
        const SQChar* value = nullptr;
        if (SQ_SUCCEEDED(sq_getstring(vm, -1, &value)) && value != nullptr) out = value;
    }
    sq_settop(vm, top);
    return out;
}

/** Writes one numeric member of a live script instance. */
bool setMemberNumber(const ssq::Object& instance, const char* name, float value) {
    HSQUIRRELVM     vm  = instance.getHandle();
    const SQInteger top = sq_gettop(vm);
    sq_pushobject(vm, instance.getRaw());
    sq_pushstring(vm, name, -1);
    sq_pushfloat(vm, SQFloat(value));
    const bool ok = SQ_SUCCEEDED(sq_newslot(vm, -3, SQFalse));
    sq_settop(vm, top);
    return ok;
}

/**
 * One script VM, one `eve.Scene` module, and the provider registry reset around it.
 * Each case mounts its own host name, so the cases stay independent in one process.
 */
class ScriptLinkTest : public ScriptTest {
public:
    ScriptLinkTest() : ScriptTest(kScriptLinkContent) {
        eve::ScriptProviderRegistry::instance().clearAll();
        module = Scene::create();
    }

    ~ScriptLinkTest() { eve::ScriptProviderRegistry::instance().clearAll(); }

protected:
    /** Mounts `root` under `name` and returns the host. */
    SceneHost* mount(const std::string& name, NodeDesc root) {
        auto mounted = module->mountAs(name, std::move(root));
        REQUIRE(mounted.ok());
        if (!mounted.ok()) return nullptr;
        return mounted.value();
    }

    /** Publishes a script instance under `name`; the instance must be in the VM. */
    eve::ScriptProviderHandle publish(const std::string& name, const char* instanceName) {
        auto published = eve::ScriptProviderRegistry::instance().publish(name, vm.find(instanceName));
        REQUIRE(published.ok());
        return published.value();
    }

    SceneNode* nodeOf(SceneHost* host, const std::string& id) {
        auto found = host->findById(id);
        REQUIRE(found.ok());
        return found.ok() ? found.value() : nullptr;
    }

    /** Moves a node locally and marks it dirty, the way a game system would. */
    void moveLocal(SceneHost* host, const std::string& id, float x, float y, float z) {
        SceneNode* node = nodeOf(host, id);
        REQUIRE(node != nullptr);
        if (node == nullptr) return;
        node->x          = x;
        node->y          = y;
        node->z          = z;
        node->localDirty = true;
        eve::scene::TransformSystem::updateHost(host);
    }

    /**
     * Marks every node dirty and runs one transform update.
     *
     * The pipeline only syncs dirty nodes, so a case that asserts "the follower was
     * called" has to say *when* the node moved instead of depending on the tree still
     * being dirty from mounting it.
     */
    void syncAll(SceneHost* host) {
        for (SceneNode& node : host->tree()->nodes) node.localDirty = true;
        eve::scene::TransformSystem::updateHost(host);
    }

    Scene* module = nullptr;
};

}  // namespace

TEST_CASE_FIXTURE(ScriptLinkTest, "scene.scriptLink.pushDrivesFollower") {
    // Provider-present probe for the node -> script direction: the follower is
    // called with the node's *world* transform, not its local one.
    SceneHost* host = mount("scriptlink_push",
                            node("root", {node("child").withPosition(1.f, 2.f, 3.f)}).withPosition(10.f, 0.f, 0.f));

    REQUIRE(module->linkScriptAt("scriptlink_push", "child", publish("link.follower", "follower").name, "node").ok());
    CHECK_EQ(host->linkCount("child"), 1);

    syncAll(host);
    const ssq::Object follower = vm.find("follower");
    CHECK(memberNumber(follower, "pushes") >= 1.0);
    CHECK_EQ(memberString(follower, "lastId"), std::string("child"));
    CHECK_EQ(memberNumber(follower, "lastX"), 11.0);
    CHECK_EQ(memberNumber(follower, "lastY"), 2.0);
    CHECK_EQ(memberNumber(follower, "lastZ"), 3.0);
    CHECK_EQ(memberNumber(follower, "lastScale"), 1.0);

    const double pushes = memberNumber(follower, "pushes");

    // Moving the node re-syncs it; the reported position is still world-space.
    moveLocal(host, "child", 5.f, 6.f, 7.f);
    CHECK(memberNumber(follower, "pushes") > pushes);
    CHECK_EQ(memberNumber(follower, "lastX"), 15.0);
    CHECK_EQ(memberNumber(follower, "lastY"), 6.0);
    CHECK_EQ(memberNumber(follower, "lastZ"), 7.0);
}

TEST_CASE_FIXTURE(ScriptLinkTest, "scene.scriptLink.pullDrivesNode") {
    // Script -> node direction: "body" mode asks the provider for a local transform
    // and moves the node to it, which is what physics does natively.
    SceneHost* host = mount("scriptlink_pull", node("root", {node("child").withPosition(1.f, 2.f, 3.f)}));

    const ssq::Object driver = vm.find("driver");
    REQUIRE(setMemberNumber(driver, "x", 4.f));
    REQUIRE(setMemberNumber(driver, "y", 5.f));
    REQUIRE(setMemberNumber(driver, "z", 6.f));
    REQUIRE(setMemberNumber(driver, "yaw", 0.25f));

    REQUIRE(module->linkScriptAt("scriptlink_pull", "child", publish("link.driver", "driver").name, "body").ok());

    // Linking already ran a transform update, so the node is at the driver's pose.
    const SceneNode* node = nodeOf(host, "child");
    REQUIRE(node != nullptr);
    CHECK(approxEq(node->x, 4.f));
    CHECK(approxEq(node->y, 5.f));
    CHECK(approxEq(node->z, 6.f));
    CHECK(approxEq(node->yaw, 0.25f));

    // And it keeps following: the node is not merely set once.
    REQUIRE(setMemberNumber(driver, "x", -1.5f));
    eve::scene::TransformSystem::updateHost(host);
    CHECK(approxEq(nodeOf(host, "child")->x, -1.5f));
}

TEST_CASE_FIXTURE(ScriptLinkTest, "scene.scriptLink.absentInputsAreReported") {
    // J-4 absent probes: every way of not having a target is a structured failure,
    // and none of them silently creates a link.
    SceneHost* host = mount("scriptlink_absent", node("root", {node("child")}));

    auto unknownHost = module->linkScriptAt("scriptlink_nope", "child", "link.follower", "node");
    CHECK(!unknownHost.ok());
    CHECK_EQ(firstDiagnosticCode(unknownHost.status()), eve::DiagnosticCode::NotFound);

    auto unknownNode = module->linkScriptAt("scriptlink_absent", "nope", "link.follower", "node");
    CHECK(!unknownNode.ok());
    CHECK_EQ(firstDiagnosticCode(unknownNode.status()), eve::DiagnosticCode::NotFound);

    // Nothing published under that name: the socket is empty, and that is not a link.
    auto unpublished = module->linkScriptAt("scriptlink_absent", "child", "link.missing", "node");
    CHECK(!unpublished.ok());
    CHECK_EQ(firstDiagnosticCode(unpublished.status()), eve::DiagnosticCode::NotFound);

    CHECK_EQ(host->linkCount("child"), 0);

    // With the provider published but never linked, the pipeline has nothing to do.
    publish("link.follower", "follower");
    eve::scene::TransformSystem::updateHost(host);
    CHECK_EQ(memberNumber(vm.find("follower"), "pushes"), 0.0);
    CHECK_EQ(host->linkCount("child"), 0);
}

TEST_CASE_FIXTURE(ScriptLinkTest, "scene.scriptLink.retiredProviderIsDropped") {
    // A retired generation must stop being driven *and* stop being linked: the
    // record stays answerable so the scene can purge it instead of following a
    // script object that no longer exists.
    SceneHost* host = mount("scriptlink_retire", node("root", {node("child").withPosition(1.f, 1.f, 1.f)}));

    const eve::ScriptProviderHandle handle = publish("link.follower", "follower");
    REQUIRE(module->linkScriptAt("scriptlink_retire", "child", handle.name, "node").ok());
    CHECK_EQ(host->linkCount("child"), 1);

    syncAll(host);
    const ssq::Object follower = vm.find("follower");
    const double      pushes   = memberNumber(follower, "pushes");
    REQUIRE(pushes >= 1.0);

    REQUIRE(eve::ScriptProviderRegistry::instance().unload(handle).ok());

    // The next sync drops the link instead of calling a retired generation.
    syncAll(host);
    CHECK_EQ(host->linkCount("child"), 0);
    CHECK_EQ(memberNumber(follower, "pushes"), pushes);
}

TEST_CASE_FIXTURE(ScriptLinkTest, "scene.scriptLink.rejectedPullKeepsNodeIntact") {
    // A provider that returns too few numbers is rejected as a whole: the node must
    // not end up half-moved, and the link stays because the provider is alive.
    SceneHost* host = mount("scriptlink_short", node("root", {node("child").withPosition(1.f, 2.f, 3.f)}));

    REQUIRE(module->linkScriptAt("scriptlink_short", "child", publish("link.short", "shortDriver").name, "body").ok());

    const SceneNode* node = nodeOf(host, "child");
    REQUIRE(node != nullptr);
    CHECK(approxEq(node->x, 1.f));
    CHECK(approxEq(node->y, 2.f));
    CHECK(approxEq(node->z, 3.f));
    CHECK_EQ(host->linkCount("child"), 1);
}

TEST_CASE_FIXTURE(ScriptLinkTest, "scene.scriptLink.brokenTargetDoesNotStallSiblings") {
    // A raising follower is reported once and skipped; the next node still syncs.
    // The broken one is linked first on purpose, so a stall would be visible.
    SceneHost* host =
        mount("scriptlink_broken",
              node("root", {node("bad").withPosition(1.f, 1.f, 1.f), node("good").withPosition(2.f, 2.f, 2.f)}));

    REQUIRE(
        module->linkScriptAt("scriptlink_broken", "bad", publish("link.raising", "raisingFollower").name, "node").ok());
    REQUIRE(module->linkScriptAt("scriptlink_broken", "good", publish("link.follower", "follower").name, "node").ok());

    const ssq::Object follower = vm.find("follower");
    const double      pushes   = memberNumber(follower, "pushes");

    syncAll(host);
    CHECK(memberNumber(follower, "pushes") > pushes);
    CHECK_EQ(memberNumber(follower, "lastX"), 2.0);
    CHECK_EQ(host->linkCount("bad"), 1);  // its target is alive, so the link survives
}

TEST_CASE_FIXTURE(ScriptLinkTest, "scene.scriptLink.scriptBindingReportsResult") {
    // The script face of the same call: a script links its own provider and reads the
    // result table, so `eve.Scene().linkScriptAt` is exercised, not just the C++ method.
    SceneHost* host = mount("scriptlink_api", node("root", {node("child").withPosition(3.f, 0.f, 0.f)}));
    publish("link.follower", "follower");

    vm.run(
        vm.compileSource("local scene = eve.Scene()\n"
                         "linked <- scene.linkScriptAt(\"scriptlink_api\", \"child\", \"link.follower\", \"node\")\n"
                         "missing <- scene.linkScriptAt(\"scriptlink_api\", \"child\", \"nope\", \"node\")\n"
                         "orphanHost <- scene.linkScriptAt(\"nowhere\", \"child\", \"link.follower\", \"node\")\n"));

    ssq::Object linked = vm.find("linked");
    CHECK_EQ(linked.getType(), ssq::Type::TABLE);
    CHECK(linked.find("ok").toBool());
    CHECK_EQ(host->linkCount("child"), 1);

    ssq::Object missing = vm.find("missing");
    CHECK_EQ(missing.getType(), ssq::Type::TABLE);
    CHECK(!missing.find("ok").toBool());
    CHECK_EQ(missing.find("code").toString(), std::string("not_found"));

    ssq::Object orphanHost = vm.find("orphanHost");
    CHECK(!orphanHost.find("ok").toBool());
    CHECK_EQ(orphanHost.find("code").toString(), std::string("not_found"));

    // The script-linked follower is the one the pipeline drives.
    syncAll(host);
    CHECK(memberNumber(vm.find("follower"), "lastX") >= 3.0);
}
}  // namespace test_scene_script_link
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_sensing_script_provider)
#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
#include <vector>
#include "PrivateTestScope.h"
#include "ScriptTest.h"
#include "common/Capability.h"
#include "common/ScriptProvider.h"
#include "sensing/Sensing.h"
#include "sensing/SensingScriptCandidateSeam.h"
#include "sensing/Targeting.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// sensing_script_provider.cpp
namespace test_sensing_script_provider {
// ---------------------------------------------------------------------------
// 决定③ 的第四个落地插座：`ISensingCandidateProvider` 可以由脚本提供。
//
// 与地块碰撞那一类不同，这条插座的载荷是**带坐标空间与区域几何的记录**：下行是一张查询表，上行是
// 一串候选表。编组的两条纪律在用例里钉住：①位置一律自带 `space`；②候选的空间必须与查询空间一致
// （不一致返回 `Unsupported`，不静默换算）。可见性与数量策略不下发——那是 resolver 的活。
// ---------------------------------------------------------------------------

namespace {

using namespace eve;
using namespace eve::sensing;

const char* kScriptSensingProvider = R"SQ(
lastOrigin <- ""
lastSpace <- ""
lastDomain <- ""
lastMaxRange <- -1.0
lastMinRange <- -1.0
lastRequiredTagCount <- -1
lastExcludedTagCount <- -1
lastZone <- ""
lastAreaShape <- ""
lastAreaRadius <- -1.0
lastOriginX <- -1.0

class CandidateProvider {
    function queryCandidates(query) {
        lastOrigin = query.origin
        lastSpace = query.space
        lastDomain = query.domain
        lastMaxRange = query.maxRange == null ? -1.0 : query.maxRange
        lastMinRange = query.minRange
        lastRequiredTagCount = query.requiredTags.len()
        lastExcludedTagCount = query.excludedTags.len()
        lastZone = query.zone == null ? "" : query.zone
        lastAreaShape = query.area == null ? "none" : query.area.shape
        lastAreaRadius = query.area == null ? -1.0 : query.area.radius
        lastOriginX = query.originLocation.x
        return [
            { subject = "00000000-0000-7000-8000-000000000002",
              location = { space = "world2d", x = 3.0, y = 4.0 },
              domain = "enemy", tags = ["unit", "visible"], zones = ["arena:central"] },
            { subject = "00000000-0000-7000-8000-000000000004",
              location = { space = "world2d", x = 2.0, y = 0.0 } },
        ]
    }
}

class GridCandidateProvider {
    function queryCandidates(query) {
        // 查询是 world2d，却回了 grid2d：适配器必须拒绝，而不是当成同一套单位算距离。
        return [{ subject = "00000000-0000-7000-8000-000000000002",
                  location = { space = "grid2d", x = 3, y = 4 } }]
    }
}

class NotAnArrayProvider {
    function queryCandidates(query) { return { subject = "not-an-array" } }
}

class BadSubjectProvider {
    function queryCandidates(query) {
        return [{ subject = "not-a-uuid", location = { space = "world2d", x = 1.0, y = 1.0 } }]
    }
}

class FractionalGridProvider {
    function queryCandidates(query) {
        return [{ subject = "00000000-0000-7000-8000-000000000002",
                  location = { space = "grid2d", x = 1.5, y = 2.0 } }]
    }
}

// 格子查询配格子候选是合法的：适配器不是"只认世界坐标"。
class GridOkProvider {
    function queryCandidates(query) {
        return [{ subject = "00000000-0000-7000-8000-000000000002",
                  location = { space = "grid2d", x = 3, y = 4 }, domain = "enemy", tags = ["unit"] }]
    }
}

class BrokenProvider {
    function queryCandidates(query) { throw "sensing provider is broken" }
}

candidateProvider <- CandidateProvider()
gridCandidateProvider <- GridCandidateProvider()
notAnArrayProvider <- NotAnArrayProvider()
badSubjectProvider <- BadSubjectProvider()
fractionalGridProvider <- FractionalGridProvider()
gridOkProvider <- GridOkProvider()
brokenProvider <- BrokenProvider()
)SQ";

constexpr const char* kOriginId = "00000000-0000-7000-8000-000000000001";
constexpr const char* kEnemyId  = "00000000-0000-7000-8000-000000000002";
constexpr const char* kAllyId   = "00000000-0000-7000-8000-000000000004";

SubjectRef subject(const char* text) {
    const auto id = PersistentId::parse(text);
    REQUIRE(id.has_value());
    return SubjectRef::fromPersistentId(*id);
}

WorldPoint world3D(float x, float y, float z) {
    auto point = WorldPoint::world3D(x, y, z);
    return std::move(point).expect("test World3D point must be valid");
}

WorldPoint world2D(float x, float y) {
    auto point = WorldPoint::world2D(x, y);
    return std::move(point).expect("test World2D point must be valid");
}

/** 一个带齐各类约束的 World2D 查询：用来检查下行编组有没有把字段丢掉。 */
TargetingQuery worldQuery() {
    TargetingSpec spec;
    spec.space        = CoordinateSpace::World2D;
    spec.domain       = TargetDomain::Enemy;
    spec.minRange     = 1.5f;
    spec.maxRange     = 10.f;
    spec.requiredTags = {"unit"};
    spec.excludedTags = {"hidden"};
    const auto zoneId = LogicalId::parse("arena:central");
    REQUIRE(zoneId.has_value());
    auto zone = ZoneRef::fromLogicalId(*zoneId);
    REQUIRE(zone.has_value());
    spec.zone      = *zone;
    spec.worldArea = std::move(WorldArea::circle2D(world2D(0.f, 0.f), 10.f)).expect("circle area");
    return TargetingQuery{subject(kOriginId), world2D(1.f, 2.f), spec};
}

/** 只给空间与原点的最小查询：坐标空间用例用它，避免被其它约束掩盖。 */
TargetingQuery minimalWorldQuery() {
    TargetingSpec spec;
    spec.space = CoordinateSpace::World2D;
    return TargetingQuery{subject(kOriginId), world2D(0.f, 0.f), spec};
}

class ScriptSensingProviderTest : public ScriptTest {
public:
    ScriptSensingProviderTest() : ScriptTest(kScriptSensingProvider) {
        auto& registry = ScriptProviderRegistry::instance();
        registry.clearAll();
        declaredBefore_ = registry.capabilities();
        cap::detail::clearAllRaw();
    }

    ~ScriptSensingProviderTest() {
        withdrawScriptSensingCandidateSeam();
        cap::detail::clearAllRaw();
        auto& registry = ScriptProviderRegistry::instance();
        registry.clearAll();
        for (const std::string& capability : registry.capabilities()) {
            if (std::find(declaredBefore_.begin(), declaredBefore_.end(), capability) != declaredBefore_.end())
                continue;
            registry.removeCapability(capability).ignore("fixture teardown withdraws only this case's seams");
        }
    }

    /** 发布一个脚本候选提供方；返回结果表，测试据此断言接线成没成。 */
    ssq::Object publish(const char* name, const char* instance) {
        const std::string source = std::string("published <- eve.provider.publishFor(\"") +
                                   ISensingCandidateProvider::capabilityName + "\", \"" + name + "\", " + instance +
                                   ")";
        vm.run(vm.compileSource(source.c_str()));
        return vm.find("published");
    }

    /** 按名字退订（脚本侧 `publishFor` 拿不到 C++ 句柄，这里用名字 + 当前代际还原一个）。 */
    void unload(const char* name) {
        auto& registry = ScriptProviderRegistry::instance();
        REQUIRE(registry.unload(ScriptProviderHandle{name, registry.generationOf(name)}).ok());
    }

private:
    std::vector<std::string> declaredBefore_;
};

}  // namespace

TEST_CASE_FIXTURE(ScriptSensingProviderTest, "sensing.script.candidateProviderAnswersTheResolver") {
    declareScriptSensingCandidateSeam();
    ssq::Object published = publish("test.sensing.candidates", "candidateProvider");
    REQUIRE_EQ(published.getType(), ssq::Type::TABLE);
    REQUIRE(published.find("ok").toBool());
    REQUIRE(cap::query<ISensingCandidateProvider>() != nullptr);

    auto resolved = TargetingResolver{}.resolve(worldQuery());
    REQUIRE(resolved.ok());
    // 脚本回了两条：一条 enemy（被 domain 约束保留），一条缺省 neutral（被挡掉）。
    REQUIRE_EQ(resolved.value().subjects().size(), std::size_t{1});
    REQUIRE_EQ(resolved.value().subjects().front(), subject(kEnemyId));

    // 下行编组：脚本看到的字段与查询里的约束逐个对上（包括区域与无界/有界距离）。
    REQUIRE_EQ(vm.find("lastOrigin").toString(), std::string(kOriginId));
    REQUIRE_EQ(vm.find("lastSpace").toString(), std::string("world2d"));
    REQUIRE_EQ(vm.find("lastDomain").toString(), std::string("enemy"));
    REQUIRE_EQ(vm.find("lastMinRange").toFloat(), 1.5);
    REQUIRE_EQ(vm.find("lastMaxRange").toFloat(), 10.0);
    REQUIRE_EQ(vm.find("lastRequiredTagCount").toInt(), 1);
    REQUIRE_EQ(vm.find("lastExcludedTagCount").toInt(), 1);
    REQUIRE_EQ(vm.find("lastZone").toString(), std::string("arena:central"));
    REQUIRE_EQ(vm.find("lastAreaShape").toString(), std::string("circle2d"));
    REQUIRE_EQ(vm.find("lastAreaRadius").toFloat(), 10.0);
    REQUIRE_EQ(vm.find("lastOriginX").toFloat(), 1.0);

    // 退订之后能力槽位重新为空，resolver 回到"没有提供方"。
    unload("test.sensing.candidates");
    REQUIRE(cap::query<ISensingCandidateProvider>() == nullptr);
    const auto absent = TargetingResolver{}.resolve(minimalWorldQuery());
    REQUIRE(!absent.ok());
    REQUIRE_EQ(static_cast<int>(absent.code()), static_cast<int>(StatusCode::Unsupported));
}

TEST_CASE_FIXTURE(ScriptSensingProviderTest, "sensing.script.candidateProviderSeamIsAbsentWithoutTheModule") {
    // 没有声明接缝时，发布被明确拒绝；resolver 报 Unsupported，而不是"绑了但不响"。
    REQUIRE(!ScriptProviderRegistry::instance().hasCapabilitySeam(ISensingCandidateProvider::capabilityName));
    ssq::Object refused = publish("test.sensing.orphan", "candidateProvider");
    REQUIRE(!refused.find("ok").toBool());
    REQUIRE_EQ(refused.find("code").toString(), std::string("not_found"));
    const auto absent = TargetingResolver{}.resolve(minimalWorldQuery());
    REQUIRE(!absent.ok());
    REQUIRE_EQ(static_cast<int>(absent.code()), static_cast<int>(StatusCode::Unsupported));

    // `Sensing` 模块自己构造时会声明接缝：这就是生产通路。
    Sensing sensing;
    REQUIRE(ScriptProviderRegistry::instance().hasCapabilitySeam(ISensingCandidateProvider::capabilityName));
}

TEST_CASE_FIXTURE(ScriptSensingProviderTest, "sensing.script.candidateAnswerMustStayInTheQuerySpace") {
    declareScriptSensingCandidateSeam();
    REQUIRE(publish("test.sensing.grid", "gridCandidateProvider").find("ok").toBool());

    // 查询是 world2d、候选自称 grid2d：明确拒绝（`Unsupported`），而不是把格子当世界单位算距离。
    const auto resolved = TargetingResolver{}.resolve(minimalWorldQuery());
    REQUIRE(!resolved.ok());
    REQUIRE_EQ(static_cast<int>(resolved.code()), static_cast<int>(StatusCode::Unsupported));
    REQUIRE(resolved.error() != nullptr);
    REQUIRE(resolved.error()->message().find("does not match the query space") != std::string::npos);
}

TEST_CASE_FIXTURE(ScriptSensingProviderTest, "sensing.script.malformedCandidateAnswersAreRejected") {
    declareScriptSensingCandidateSeam();

    // 返回的不是数组：拒绝（`Rejected`），而不是"当成零个候选"。
    REQUIRE(publish("test.sensing.not-array", "notAnArrayProvider").find("ok").toBool());
    auto notArray = TargetingResolver{}.resolve(minimalWorldQuery());
    REQUIRE(!notArray.ok());
    REQUIRE_EQ(static_cast<int>(notArray.code()), static_cast<int>(StatusCode::Rejected));
    unload("test.sensing.not-array");

    // 主体不是规范 UUID：拒绝。
    REQUIRE(publish("test.sensing.bad-subject", "badSubjectProvider").find("ok").toBool());
    auto badSubject = TargetingResolver{}.resolve(minimalWorldQuery());
    REQUIRE(!badSubject.ok());
    REQUIRE_EQ(static_cast<int>(badSubject.code()), static_cast<int>(StatusCode::Rejected));
    unload("test.sensing.bad-subject");

    // 格子坐标不是整数：拒绝，而不是截断成整数后继续。
    REQUIRE(publish("test.sensing.fractional", "fractionalGridProvider").find("ok").toBool());
    TargetingSpec gridSpec;
    gridSpec.space = CoordinateSpace::Grid2D;
    TargetingQuery gridQuery{subject(kOriginId), TargetLocation(GridPoint::grid2D(0, 0)), gridSpec};
    auto           fractional = TargetingResolver{}.resolve(gridQuery);
    REQUIRE(!fractional.ok());
    REQUIRE_EQ(static_cast<int>(fractional.code()), static_cast<int>(StatusCode::Rejected));
    unload("test.sensing.fractional");

    // 同一个空间换成整数的格子候选就是合法答案：适配器不是"只认世界坐标"。
    REQUIRE(publish("test.sensing.grid-ok", "gridOkProvider").find("ok").toBool());
    auto gridAnswer = TargetingResolver{}.resolve(gridQuery);
    REQUIRE(gridAnswer.ok());
    REQUIRE_EQ(gridAnswer.value().subjects().size(), std::size_t{1});
    REQUIRE_EQ(gridAnswer.value().subjects().front(), subject(kEnemyId));
}

TEST_CASE_FIXTURE(ScriptSensingProviderTest, "sensing.script.candidateProviderFailureIsReportedNotSwallowed") {
    declareScriptSensingCandidateSeam();
    REQUIRE(publish("test.sensing.broken", "brokenProvider").find("ok").toBool());

    // 脚本抛错 → 解析失败（`Failed`），不吞：吞掉会让上层把"没候选"当成"真的没有敌人"。
    const auto failed = TargetingResolver{}.resolve(minimalWorldQuery());
    REQUIRE(!failed.ok());
    REQUIRE_EQ(static_cast<int>(failed.code()), static_cast<int>(StatusCode::Failed));

    // 提供方在失败后还活着：换回能用的脚本对象，同一条通路恢复。
    unload("test.sensing.broken");
    REQUIRE(publish("test.sensing.good", "candidateProvider").find("ok").toBool());
    REQUIRE(TargetingResolver{}.resolve(minimalWorldQuery()).ok());
}

TEST_CASE("sensing.script.coordinateSpaceSpellingMatchesTheModuleName") {
    // 脚本词表只有一份，但它必须与模块既有的 `coordinateSpaceName` 拼写一致；这条用例把两者钉在
    // 一起：任何一边改名字，这里就红。
    const SubjectRef origin   = subject(kOriginId);
    const auto       queryFor = [&origin](CoordinateSpace space) {
        TargetingSpec spec;
        spec.space = space;
        TargetLocation location;
        switch (space) {
            case CoordinateSpace::World2D: location = TargetLocation(world2D(0.f, 0.f)); break;
            case CoordinateSpace::World3D: location = TargetLocation(world3D(0.f, 0.f, 0.f)); break;
            case CoordinateSpace::Grid2D: location = TargetLocation(GridPoint::grid2D(0, 0)); break;
            case CoordinateSpace::Grid3D: location = TargetLocation(GridPoint::grid3D(0, 0, 0)); break;
        }
        return TargetingQuery{origin, location, spec};
    };
    CHECK_EQ(coordinateSpaceName(CoordinateSpace::World2D), std::string_view("world2d"));
    CHECK_EQ(coordinateSpaceName(CoordinateSpace::World3D), std::string_view("world3d"));
    CHECK_EQ(coordinateSpaceName(CoordinateSpace::Grid2D), std::string_view("grid2d"));
    CHECK_EQ(coordinateSpaceName(CoordinateSpace::Grid3D), std::string_view("grid3d"));
    // 四个空间都能构造出通过校验的查询：词表与校验规则对齐。
    for (const CoordinateSpace space :
         {CoordinateSpace::World2D, CoordinateSpace::World3D, CoordinateSpace::Grid2D, CoordinateSpace::Grid3D}) {
        REQUIRE(queryFor(space).validate().ok());
    }
}
}  // namespace test_sensing_script_provider
#endif
