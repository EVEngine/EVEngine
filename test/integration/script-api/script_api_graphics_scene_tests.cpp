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
#include "PrivateTestScope.h"
#include <memory>
#include <simplesquirrel/simplesquirrel.hpp>
#include <string>
#include <vector>
#include "common/Module.h"
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
}  // namespace test_rx
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_scenario_recorder)
#include "PrivateTestScope.h"
#include <string>
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
    CHECK(rec.end(path));
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
    CHECK(rec.end(path));
    dev::DevTool::instance().detach();

    // --- Replay on a fresh VM: restore baseline, re-stage the recorded taps ---
    Runtime rt2(256, ssq::Libs::ALL);
    rt2.runSource(kTapScript, "taps.nut");
    CHECK(rec.beginReplay(rt2.handle(), path));

    // Baseline restored: hits is back to 0 before any staged input.
    const int64_t before = rt2.vm().find("hits").toInt();
    CHECK_EQ(before, 0);

    bool reproduced = false;
    while (rec.framesRemaining() > 0) {
        CHECK(rec.stageFrame());
        CHECK(drainTaps(rt2.vm(), reproduced));
    }
    // All 3 taps replayed from the clean baseline -> hits reaches 3 -> throws.
    CHECK(reproduced);

    dev::ScenarioRecorder::instance().cancel();
    dev::DevTool::instance().detach();
}
}  // namespace test_scenario_recorder
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_scene)
#include "PrivateTestScope.h"
#include <SDL2/SDL.h>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <simplesquirrel/simplesquirrel.hpp>
#include <string>
#include <utility>
#include "common/Capability.h"
#include "common/ECS.h"
#include "common/EntitySpatialResolver.h"
#include "common/Module.h"
#include "common/ProcgenSceneSink.h"
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


// The camera projection the picking entry points need is an optional
// capability: without a provider they must fail explicitly instead of silently
// returning a result, and with it restored they must work again.


// ---------------------------------------------------------------------------
// Script side: eve.SceneEntity attach / detach / update / view integration
// ---------------------------------------------------------------------------

static const char* kSceneEcsScriptContent = R"SQ(
[[maybe_unused]] function testSceneEntityLifecycle() {
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
    scene.beginNode("root")
    scene.addNode("enemy")
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
    scene.beginNode("root")
    scene.addNode("enemy")
    scene.end()
    scene.remountBuildAs("battle")
    if (!scene.hasEntity("enemy", EnemyAI)) return false
    if (eve.view(EnemyAI).len() != 1) return false
    if (!e.isAlive()) return false

    // full rebuild removing the node tears the binding down
    scene.beginBuild()
    scene.beginNode("root")
    scene.end()
    scene.mountBuildAs("battle")
    if (scene.hasEntity("enemy", EnemyAI)) return false
    if (eve.view(EnemyAI).len() != 0) return false
    if (!e.detached) return false
    if (e.isAlive()) return false
    return true
}

[[maybe_unused]] function testSceneEntityViewCacheAfterDetach() {
    class Marker extends eve.Component { v = 1 }
    class Unit extends eve.SceneEntity { mark = Marker }

    local scene = eve.Scene()
    scene.beginBuild()
    scene.beginNode("root")
    scene.addNode("a")
    scene.addNode("b")
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

[[maybe_unused]] function testSceneEntityEnableAndDelayedDetach() {
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

[[maybe_unused]] function testSceneLinkPhysics2D() {
    local scene = eve.Scene()
    scene.beginBuild()
    scene.beginNode("root")
    scene.addNode("ball")
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

[[maybe_unused]] function testSceneApiCompleteness() {
    local scene = eve.Scene()
    scene.beginBuild()
    scene.beginNode("root")
    scene.addNode("pivot")
    scene.setBuildPosition(10.0, 0.0, 0.0)
    scene.addNode("ball")
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

    // world + space conversion
    scene.updateTransforms()
    local wp = scene.getNodeWorldPosition("ball")
    if (wp[0] != 11.0 || wp[1] != 2.0 || wp[2] != 3.0) return false
    local lw = scene.localToWorld("ball", 0.0, 0.0, 0.0)
    if (lw[0] != 11.0) return false
    local wl = scene.worldToLocal("ball", 11.0, 2.0, 3.0)
    if (wl[0] != 0.0 || wl[1] != 0.0) return false

    // hierarchy ops
    if (!scene.setNodeParent("ball", "pivot")) return false
    if (scene.getParentId("ball") != "pivot") return false
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

[[maybe_unused]] function testSceneNodeEvents() {
    local scene = eve.Scene()
    scene.beginBuild()
    scene.beginNode("root")
    scene.addNode("a")
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
    scene.beginNode("root")
    scene.addNode("a")
    scene.setBuildPosition(3.0, 0.0, 0.0)
    scene.end()
    scene.remountBuildAs("evt")
    if (events.len() != 2) return false
    if (events[0] != "node_changed:root") return false

    // full rebuild removing a node fires node_removed
    events.clear()
    scene.beginBuild()
    scene.beginNode("root")
    scene.end()
    scene.mountBuildAs("evt")
    if (events.len() < 1 || events[0] != "node_removed:a") return false

    // reparent fires node_moved
    scene.beginBuild()
    scene.beginNode("root")
    scene.addNode("x")
    scene.addNode("y")
    scene.end()
    scene.mountBuildAs("evt")
    events.clear()
    scene.setNodeParent("y", "x")
    if (events.len() < 1 || events[0] != "node_moved:y") return false
    return true
}

[[maybe_unused]] function testSceneComponentOnMount() {
    local mounted = 0
    scene <- eve.Scene()
    class MyComp extends eve.SceneComponent {
        function onMount() { mounted += 1 }
        function build() {
            scene.beginBuild()
            scene.beginNode("root")
            scene.end()
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

[[maybe_unused]] function testSceneErrorSemantics() {
    local scene = eve.Scene()
    scene.beginBuild()
    scene.beginNode("root")
    scene.addNode("a")
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

[[maybe_unused]] function testSceneSerializeAndPick() {
    local scene = eve.Scene()
    scene.beginBuild()
    scene.beginNode("root")
    scene.addNode("target")
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
    CHECK(vm.callFunc(vm.findFunc("testSceneEntityLifecycle"), vm).toBool());
}

TEST_CASE_FIXTURE(SceneEcsBridgeFixture, "Scene.script.entityViewCacheAfterDetach") {
    CHECK(vm.callFunc(vm.findFunc("testSceneEntityViewCacheAfterDetach"), vm).toBool());
}

TEST_CASE_FIXTURE(SceneEcsBridgeFixture, "Scene.script.entityEnableAndDelayedDetach") {
    CHECK(vm.callFunc(vm.findFunc("testSceneEntityEnableAndDelayedDetach"), vm).toBool());
}

TEST_CASE_FIXTURE(SceneEcsBridgeFixture, "Scene.script.linkPhysics2D") {
    CHECK(vm.callFunc(vm.findFunc("testSceneLinkPhysics2D"), vm).toBool());
}

TEST_CASE_FIXTURE(SceneEcsBridgeFixture, "Scene.script.apiCompleteness") {
    CHECK(vm.callFunc(vm.findFunc("testSceneApiCompleteness"), vm).toBool());
}

TEST_CASE_FIXTURE(SceneEcsBridgeFixture, "Scene.script.nodeEvents") {
    CHECK(vm.callFunc(vm.findFunc("testSceneNodeEvents"), vm).toBool());
}

TEST_CASE_FIXTURE(SceneEcsBridgeFixture, "Scene.script.componentOnMount") {
    CHECK(vm.callFunc(vm.findFunc("testSceneComponentOnMount"), vm).toBool());
}

TEST_CASE_FIXTURE(SceneEcsBridgeFixture, "Scene.script.errorSemantics") {
    CHECK(vm.callFunc(vm.findFunc("testSceneErrorSemantics"), vm).toBool());
}

TEST_CASE_FIXTURE(SceneEcsBridgeFixture, "Scene.script.serializeAndPick") {
    CHECK(vm.callFunc(vm.findFunc("testSceneSerializeAndPick"), vm).toBool());
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
#include "PrivateTestScope.h"
#include <simplesquirrel/simplesquirrel.hpp>
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
    CHECK(vm.callFunc(vm.findFunc("schemaBindingTest"), vm).toBool());
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
            if (s.getFieldType("card.definition", 3) != "integer") return "type";
            if (!s.getFieldRequired("card.definition", 0)) return "required";
            if (s.getFieldEnumCount("card.definition", 2) != 2) return "enum";
            if (!s.getFieldHasMinimum("card.definition", 3)) return "min";
            if (s.getFieldMaximum("card.definition", 3) != 20) return "max";
            local ok = @"{""id"":""card.scout"",""name"":""Scout"",""kind"":""creature"",""cost"":2,""attack"":3,""health"":2}";
            if (!s.validateJson("card.definition", ok)) return "valid";
            local bad = @"{""id"":""x"",""name"":""y"",""kind"":""air"",""cost"":25}";
            if (s.validateJson("card.definition", bad)) return "invalid";
            return s.getValidationErrorCode(0) == "enum";
        }
    )");
    vm.run(script);
    CHECK(vm.callFunc(vm.findFunc("schemaReflectTest"), vm).toBool());
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
        "eve.Foo <- function() { return { n = 1 } }\n"
        "eve.Bar <- function() { return { n = 2 } }\n"
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
        "eve.Foo <- function() { return { n = 1 } }\n"
        "eve.Bar <- function() { return { n = 2 } }\n"
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
#include "PrivateTestScope.h"
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
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
#include "PrivateTestScope.h"
#include <memory>
#include <simplesquirrel/simplesquirrel.hpp>
#include <string>
#include "authority/Authority.h"
#include "common/SquirrelOwnership.h"
#include "decision/Decision.h"
#include "effects/Effects.h"
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
#include "PrivateTestScope.h"
#include <cassert>
#include <iostream>
#include <map>
#include <string>
#include "ScriptTest.h"
// Squirrel internal headers require this dependency order.
// clang-format off
#include "sqpcheader.h"
#include "sqvm.h"
#include "sqtable.h"
#include "sqclass.h"
// clang-format on
extern const char* simplesquirrel_content;
// simplesquirrel.cpp
namespace test_simplesquirrel {
using namespace eve;


UnitSciptTest(SimpleSquirrelTest, simplesquirrel_content);

TEST_CASE_FIXTURE(SimpleSquirrelTest, "SimpleSquirrelTest.ExportClass") {
    CHECK(vm.callFunc(vm.findFunc("exportClass"), vm).toBool());
}

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
    CHECK(vm.callFunc(vm.findFunc("refTest"), vm).toBool());
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

    CHECK(vm.callFunc(vm.findFunc("testDefClass"), vm).toBool());
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


TEST_CASE_FIXTURE(SimpleSquirrelTest, "SimpleSquirrelTest.GetterTest") {
    CHECK(vm.callFunc(vm.findFunc("getterTest"), vm).toBool());
}


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
[[maybe_unused]] function testSocialBindings() {
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

TEST_CASE_FIXTURE(SocialScriptTest, "social.script.bindings") {
    CHECK(vm.callFunc(vm.findFunc("testSocialBindings"), vm).toBool());
}
}  // namespace test_social
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_squirrel_binding)
#include "PrivateTestScope.h"
#include <cmath>
#include <limits>
#include <simplesquirrel/simplesquirrel.hpp>
#include <string>
#include <utility>
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
[[maybe_unused]] function testTagsBindings() {
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

TEST_CASE_FIXTURE(TagsScriptTest, "tags.script.bindings") {
    CHECK(vm.callFunc(vm.findFunc("testTagsBindings"), vm).toBool());
}
}  // namespace test_tags
#endif
