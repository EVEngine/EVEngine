// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "default": "runtime-lifecycle",
  "cases": {
    "concurrency": [
      "event.workerPushWakesWait",
      "timer.step.deltaPositiveAfterSleep"
    ],
    "script-binding": [
      "gameplay.control.squirrelFacadeUsesTheSharedJsonRouter",
      "devtools.playHost.liveObservationProjectsBeforeSerializing",
      "Scene.componentScheduling.scriptCoalescingAndFailureRecovery"
    ],
    "render-window": [
      "keyboard.textInput",
      "keyboard.keyScancodeRoundTrip"
    ]
  },
  "module": "mouse",
  "modules": [
    "devtools",
    "keyboard",
    "mouse",
    "os",
    "platform_event",
    "plugins",
    "scene",
    "timer",
    "touch",
    "ui"
  ],
  "origins": [
    "MouseWheel.cpp",
    "ScenePanel.cpp",
    "event.cpp",
    "gameplay_control_json.cpp",
    "keyboard_cpp.cpp",
    "os.cpp",
    "pcg_build_config.cpp",
    "play_host.cpp",
    "plugins.cpp",
    "renderflow.cpp",
    "scene_component_scheduling.cpp",
    "service_mocks.cpp",
    "timer_cpp.cpp",
    "touch.cpp"
  ]
}
*/
// clang-format on

#include "zeroerr/assert.h"

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_MouseWheel)
#include "PrivateTestScope.h"
#include <SDL2/SDL.h>
#include "mouse/Mouse.h"
#include "platform_event/PlatformEvent.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// MouseWheel.cpp
namespace test_MouseWheel {
TEST_CASE("mouse.wheel snapshot accumulates precise flipped input and resets per pump") {
    auto* mouse  = eve::mouse::Mouse::create();
    auto* events = eve::platform_event::PlatformEvent::create();
    events->pump();
    SDL_Event event{};
    event.type    = SDL_MOUSEWHEEL;
    event.wheel.x = 2;
    event.wheel.y = 3;
#if SDL_VERSION_ATLEAST(2, 0, 18)
    event.wheel.preciseX = 2.25f;
    event.wheel.preciseY = 3.5f;
#endif
    REQUIRE(SDL_PushEvent(&event) == 1);
    event.wheel.direction = SDL_MOUSEWHEEL_FLIPPED;
    event.wheel.x         = 1;
    event.wheel.y         = 1;
#if SDL_VERSION_ATLEAST(2, 0, 18)
    event.wheel.preciseX = 0.5f;
    event.wheel.preciseY = 1.25f;
#endif
    REQUIRE(SDL_PushEvent(&event) == 1);
    events->pump();
#if SDL_VERSION_ATLEAST(2, 0, 18)
    CHECK_EQ(mouse->getWheelX(), 1.75f);
    CHECK_EQ(mouse->getWheelY(), 2.25f);
    CHECK_EQ(mouse->getWheelY(), 2.25f);
#else
    CHECK_EQ(mouse->getWheelX(), 1.f);
    CHECK_EQ(mouse->getWheelY(), 2.f);
    CHECK_EQ(mouse->getWheelY(), 2.f);
#endif
    events->pump();
    CHECK_EQ(mouse->getWheelX(), 0.f);
    CHECK_EQ(mouse->getWheelY(), 0.f);
}
}  // namespace test_MouseWheel
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_ScenePanel)
#include "PrivateTestScope.h"
#include <string>
#include <vector>
#include "common/Capability.h"
#include "common/Runtime.h"
#include "common/SceneQuery.h"
#include "ui/ScenePanel.h"
#include "ui/UIHost.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// ScenePanel.cpp
namespace test_ScenePanel {
using namespace eve;
using namespace eve::ui;

namespace {

class FakeScene final : public eve::ISceneQuery {
public:
    std::string activeHost() const override { return "test_host"; }
    int         hostCount() const override { return 1; }
    std::string hostNameAt(int) const override { return "test_host"; }
    int         nodeCount() const override { return 3; }
    std::string rootId() const override { return "root"; }

    std::vector<eve::SceneNodeInfo> nodes(int) const override {
        eve::SceneNodeInfo root;
        root.id       = "root";
        root.name     = "Root";
        root.path     = "/";
        root.children = {"hero"};
        eve::SceneNodeInfo hero;
        hero.id     = "hero";
        hero.name   = "Hero";
        hero.path   = "/hero";
        hero.parent = "root";
        hero.x      = 1.f;
        hero.y      = 2.f;
        hero.z      = 3.f;
        return {root, hero};
    }
    std::vector<eve::SceneNodeInfo> nodesOf(const std::string&, int) const override { return nodes(0); }

    eve::Result<eve::SceneNodeInfo> getNode(const std::string& id) const override {
        const std::vector<eve::SceneNodeInfo> all = nodes(0);
        for (const eve::SceneNodeInfo& n : all) {
            if (n.id == id) return eve::Result<eve::SceneNodeInfo>::success(n);
        }
        return eve::Result<eve::SceneNodeInfo>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::NotFound, "scene node not found", "node"));
    }
    eve::Result<eve::SceneNodeInfo> getNodeIn(const std::string&, const std::string& id) const override {
        return getNode(id);
    }

    eve::Result<void> setNodeTransform(const std::string& id, float x, float y, float z) override {
        if (id != "hero") {
            return eve::Result<void>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::NotFound, "scene node not found", "node"));
        }
        setX = x;
        setY = y;
        setZ = z;
        return eve::Result<void>::success();
    }
    eve::Result<void> setNodeVisible(const std::string& id, bool visible) override {
        if (id != "hero") {
            return eve::Result<void>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::NotFound, "scene node not found", "node"));
        }
        setVisible = visible;
        return eve::Result<void>::success();
    }
    void syncTransforms() override {}

    float setX = 0.f, setY = 0.f, setZ = 0.f;
    bool  setVisible = true;
};

UINode* nodeById(UIHost* host, const std::string& id) {
    if (host == nullptr) return nullptr;
    auto node = host->findById(id);
    return node ? &node->get() : nullptr;
}

UIHost* resolveHost(UIHostHandle handle) {
    auto host = UIHost::resolve(handle);
    return host ? &host->get() : nullptr;
}

}  // namespace

TEST_CASE("scenePanel.treeSelectionAndNodeEditing") {
    Runtime runtime(512, ssq::Libs::ALL);
    runtime.initialize();
    FakeScene fake;
    eve::cap::provide<eve::ISceneQuery>(&fake);

    ScenePanel panel;
    panel.open();
    UIHost* host = resolveHost(panel.host());
    REQUIRE(host != nullptr);
    REQUIRE(nodeById(host, "node_root") != nullptr);
    REQUIRE(nodeById(host, "node_hero") != nullptr);

    // Select "hero" via its select button.
    UINode* selectHero = nodeById(host, "sel_hero");
    REQUIRE(selectHero != nullptr);
    auto tree = host->tree();
    REQUIRE_GE(selectHero->handlerClick, 1u);
    tree->clickHandlers[size_t(selectHero->handlerClick - 1)]();
    CHECK_EQ(panel.selectedNode(), std::string("hero"));
    REQUIRE(nodeById(host, "node_hero_x") != nullptr);
    REQUIRE(nodeById(host, "node_hero_y") != nullptr);
    REQUIRE(nodeById(host, "node_hero_z") != nullptr);

    // Transform edit -> ISceneQuery::setNodeTransform.
    UINode* xCell = nodeById(host, "node_hero_x");
    REQUIRE_GE(xCell->handlerText, 1u);
    tree->textHandlers[size_t(xCell->handlerText - 1)]("10");
    CHECK_EQ(fake.setX, 10.f);
    CHECK_EQ(fake.setY, 2.f);
    CHECK_EQ(fake.setZ, 3.f);

    // Visibility toggle -> ISceneQuery::setNodeVisible.
    UINode* visibleCell = nodeById(host, "node_hero_visible");
    REQUIRE(visibleCell != nullptr);
    REQUIRE_GE(visibleCell->handlerToggle, 1u);
    tree->toggleHandlers[size_t(visibleCell->handlerToggle - 1)](false);
    CHECK(!fake.setVisible);

    // Pick button hands the selected node id to the registered handler.
    std::string pickedId;
    panel.setPickHandler([&](const std::string& id) { pickedId = id; });
    panel.refresh();  // rebuild so the Pick button exists
    UINode* pick = nodeById(host, "node_hero_pick");
    REQUIRE(pick != nullptr);
    auto tree2 = host->tree();
    REQUIRE_GE(pick->handlerClick, 1u);
    tree2->clickHandlers[size_t(pick->handlerClick - 1)]();
    CHECK_EQ(pickedId, std::string("hero"));

    eve::cap::revoke<eve::ISceneQuery>(&fake);
}
}  // namespace test_ScenePanel
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_event)
#include "PrivateTestScope.h"
#include <chrono>
#include <memory>
#include <thread>
#include "platform_event/PlatformEvent.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// event.cpp
namespace test_event {
using eve::platform_event::Message;
using eve::platform_event::PlatformEvent;
using eve::platform_event::Variant;

namespace {

struct TrackedPayload {
    explicit TrackedPayload(int* destructions) : destructions(destructions) {}
    ~TrackedPayload() { ++*destructions; }
    int* destructions;
};

}  // namespace

TEST_CASE("PlatformEvent.quitMessageSurvivesPumpPoll") {
    auto* ev = PlatformEvent::create();
    ev->push(new Message("quit"));
    auto* msg = ev->poll();
    REQUIRE(msg != nullptr);
    CHECK(msg->name == "quit");
    delete msg;
    CHECK(ev->poll() == nullptr);
}

TEST_CASE("PlatformEvent.pollNameReturnsEmptyWhenEmpty") {
    auto* ev = PlatformEvent::create();
    CHECK(ev->pollName() == "");
}

TEST_CASE("event.Variant.makeNil") {
    auto v = Variant::makeNil();
    CHECK(static_cast<int>(v.type) == static_cast<int>(Variant::Type::Nil));
    CHECK(v.i == 0);
    CHECK(v.s.empty());
    CHECK(v.p == nullptr);
}

TEST_CASE("event.Variant.makeInt") {
    auto v = Variant::makeInt(-42);
    CHECK(static_cast<int>(v.type) == static_cast<int>(Variant::Type::Int));
    CHECK(v.i == -42);
}

TEST_CASE("event.Variant.makeString") {
    auto v = Variant::makeString("hello");
    CHECK(static_cast<int>(v.type) == static_cast<int>(Variant::Type::String));
    CHECK(v.s == "hello");
}

TEST_CASE("event.Variant.makePtr") {
    int  x = 7;
    auto v = Variant::makePtr(&x);
    CHECK(static_cast<int>(v.type) == static_cast<int>(Variant::Type::Ptr));
    CHECK(v.p == &x);
    CHECK(!v.ownsPointer());
}

TEST_CASE("event.Variant.makeOwnedPtrSharesOneOwnerAcrossCopies") {
    int destructions = 0;
    {
        auto a = Variant::makeOwnedPtr(new TrackedPayload(&destructions));
        auto b = a;
        CHECK(a.ownsPointer());
        CHECK(b.ownsPointer());
        CHECK(a.p == b.p);
    }
    CHECK_EQ(destructions, 1);
}

TEST_CASE("event.Message.withArgs") {
    std::vector<Variant> args;
    args.push_back(Variant::makeInt(100));
    args.push_back(Variant::makeString("payload"));
    args.push_back(Variant::makeNil());
    Message msg("custom", args);
    CHECK(msg.name == "custom");
    REQUIRE(msg.args.size() == 3);
    CHECK(static_cast<int>(msg.args[0].type) == static_cast<int>(Variant::Type::Int));
    CHECK(msg.args[0].i == 100);
    CHECK(msg.args[1].s == "payload");
    CHECK(static_cast<int>(msg.args[2].type) == static_cast<int>(Variant::Type::Nil));
}

TEST_CASE("event.pushPollRoundTrip") {
    auto* ev = PlatformEvent::create();
    ev->push(new Message("first"));
    ev->push(new Message("second"));
    auto* m1 = ev->poll();
    REQUIRE(m1 != nullptr);
    CHECK(m1->name == "first");
    delete m1;
    auto* m2 = ev->poll();
    REQUIRE(m2 != nullptr);
    CHECK(m2->name == "second");
    delete m2;
    CHECK(ev->poll() == nullptr);
}

TEST_CASE("event.ownedPushPollDestroysPayloadWithMessage") {
    auto*                ev           = PlatformEvent::create();
    int                  destructions = 0;
    std::vector<Variant> args;
    args.push_back(Variant::makeOwnedPtr(new TrackedPayload(&destructions)));
    ev->push(std::make_unique<Message>("owned", args));

    {
        auto message = ev->pollOwned();
        REQUIRE(message.get() != nullptr);
        REQUIRE(message->args.size() == 1u);
        CHECK(message->args[0].ownsPointer());
        CHECK_EQ(destructions, 0);
    }
    CHECK_EQ(destructions, 1);
}

TEST_CASE("event.clearDestroysOwnedPayloadExactlyOnce") {
    auto*                ev           = PlatformEvent::create();
    int                  destructions = 0;
    std::vector<Variant> args;
    args.push_back(Variant::makeOwnedPtr(new TrackedPayload(&destructions)));
    ev->push(std::make_unique<Message>("unconsumed", args));

    ev->clear();
    CHECK_EQ(destructions, 1);
    auto empty = ev->pollOwned();
    CHECK(empty.get() == nullptr);
}

TEST_CASE("event.pollNameConsumesMessage") {
    auto* ev = PlatformEvent::create();
    ev->push(new Message("hello"));
    CHECK(ev->pollName() == "hello");
    CHECK(ev->pollName() == "");
    CHECK(ev->poll() == nullptr);
}

TEST_CASE("event.pollEmptyQueue") {
    auto* ev = PlatformEvent::create();
    CHECK(ev->poll() == nullptr);
}

TEST_CASE("event.clearThenPollEmpty") {
    auto* ev = PlatformEvent::create();
    ev->push(new Message("a"));
    ev->push(new Message("b"));
    ev->clear();
    CHECK(ev->poll() == nullptr);
    CHECK(ev->pollName() == "");
}

TEST_CASE("PlatformEvent.wait.returnsQueuedMessage") {
    auto* ev = PlatformEvent::create();
    ev->pushData("wait.queued", "payload");
    auto* msg = ev->wait();
    REQUIRE(msg != nullptr);
    CHECK(msg->name == "wait.queued");
    delete msg;
}

TEST_CASE("event.pumpSmoke") {
    auto* ev = PlatformEvent::create();
    ev->pump();
}

TEST_CASE("event.workerPushWakesWait") {
    auto* ev = PlatformEvent::create();
    ev->clear();
    std::thread producer([ev] {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        ev->pushData("worker-wake", "ready");
    });
    Message*    message = ev->wait();
    producer.join();
    REQUIRE(message != nullptr);
    CHECK_EQ(message->name, std::string("worker-wake"));
    REQUIRE(message->args.size() == 1);
    CHECK_EQ(message->args[0].s, std::string("ready"));
    delete message;
}
}  // namespace test_event
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_gameplay_control_json)
#include "PrivateTestScope.h"
#include "common/Capability.h"
#include "common/GameplayControlJson.h"
#include "common/GameplayInstanceCatalog.h"
#include "common/Module.h"
#include "common/Runtime.h"
#include "devtools/DevTool.hpp"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// gameplay_control_json.cpp
namespace test_gameplay_control_json {
namespace {

eve::SubjectRef subject(const char* text) {
    const auto id = eve::PersistentId::parse(text);
    REQUIRE(id.has_value());
    return eve::SubjectRef::fromPersistentId(*id);
}

eve::LogicalId logical(const char* text) {
    const auto id = eve::LogicalId::parse(text);
    REQUIRE(id.has_value());
    return *id;
}

class JsonProvider final : public eve::IGameplayControlProvider, public eve::IGameplayInstanceCatalog {
public:
    explicit JsonProvider(eve::SubjectRef instance) : instance_(instance) {
        eve::cap::addListener<eve::IGameplayControlProvider>(this);
        eve::cap::addListener<eve::IGameplayInstanceCatalog>(this);
    }
    ~JsonProvider() override {
        eve::cap::removeListener<eve::IGameplayInstanceCatalog>(this);
        eve::cap::removeListener<eve::IGameplayControlProvider>(this);
    }

    [[nodiscard]] std::string_view gameplayDomain() const noexcept override { return "json-fixture"; }
    /** 目录：该 fixture 只服务构造时给它的那一个实例，路由器据此在同域多 provider 间分派。 */
    [[nodiscard]] std::vector<eve::SubjectRef>          gameplayInstances() const override { return {instance_}; }
    [[nodiscard]] eve::Result<eve::GameplayObservation> observeGameplay(const eve::GameplaySession&,
                                                                        eve::SubjectRef instance) const override {
        if (instance != instance_)
            return eve::Result<eve::GameplayObservation>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::NotFound, "fixture not found", "instance"));
        eve::GameplayObservation result;
        result.domain   = logical("gameplay:json-fixture");
        result.instance = instance_;
        result.tick     = tick_;
        result.revision = revision_;
        result.state    = eve::Value(eve::Value::Object{{"count", eve::Value(count_)}});
        return eve::Result<eve::GameplayObservation>::success(std::move(result));
    }
    [[nodiscard]] eve::Result<std::vector<eve::GameplayActionDescriptor>> availableGameplayActions(
        const eve::GameplaySession&, eve::SubjectRef, eve::SubjectRef) const override {
        return eve::Result<std::vector<eve::GameplayActionDescriptor>>::success(
            {{logical("fixture:increment"), eve::Value(eve::Value::Object{})}});
    }
    [[nodiscard]] eve::Result<eve::GameplayCommandReceipt> submitGameplay(
        const eve::GameplaySession&, eve::SubjectRef, const eve::GameplayCommand& command) override {
        ++count_;
        ++revision_;
        eve::GameplayCommandReceipt result;
        result.commandId         = command.id;
        result.executionId       = "fixture-execution";
        result.acceptedTick      = tick_;
        result.resultingRevision = revision_;
        result.details           = eve::Value(eve::Value::Object{{"count", eve::Value(count_)}});
        return eve::Result<eve::GameplayCommandReceipt>::success(std::move(result));
    }
    [[nodiscard]] eve::Result<eve::GameplayObservation> advanceGameplay(const eve::GameplaySession& session,
                                                                        eve::SubjectRef             instance,
                                                                        const eve::SimulationStep&  step) override {
        tick_ = step.tick;
        return observeGameplay(session, instance);
    }
    [[nodiscard]] eve::Result<std::vector<eve::GameplayEvent>> gameplayEvents(const eve::GameplaySession&,
                                                                              eve::SubjectRef,
                                                                              std::uint64_t) const override {
        return eve::Result<std::vector<eve::GameplayEvent>>::success({});
    }

private:
    eve::SubjectRef     instance_;
    eve::SimulationTick tick_     = eve::SimulationTick::zero();
    std::uint64_t       revision_ = 1;
    int                 count_    = 0;
};

std::string envelope(const std::string& operation, const std::string& instance, const std::string& extra = {}) {
    return "{\"schemaId\":\"evengine.gameplay-control-request\",\"schemaVersion\":1,"
           "\"op\":\"" +
           operation + "\",\"domain\":\"json-fixture\",\"instance\":\"" + instance +
           "\",\"session\":{\"id\":\"test\",\"access\":\"player\",\"controlledSubjects\":[\"" + instance + "\"]}" +
           extra + "}";
}

}  // namespace

TEST_CASE("gameplay.control.jsonFacadeRoutesObserveActionsSubmitAdvanceAndEvents") {
    const auto        instance = subject("00000000-0000-7000-8000-000000000a01");
    JsonProvider      provider(instance);
    const std::string instanceText = instance.format();

    auto domains = eve::executeGameplayControlJson(
        R"({"schemaId":"evengine.gameplay-control-request","schemaVersion":1,"op":"domains"})");
    REQUIRE(domains.ok());
    CHECK(domains.value().find("json-fixture") != std::string::npos);

    auto observed = eve::executeGameplayControlJson(envelope("observe", instanceText));
    REQUIRE(observed.ok());
    CHECK(observed.value().find("\"revision\":1") != std::string::npos);

    auto actions =
        eve::executeGameplayControlJson(envelope("actions", instanceText, ",\"subject\":\"" + instanceText + "\""));
    REQUIRE(actions.ok());
    CHECK(actions.value().find("fixture:increment") != std::string::npos);

    const std::string command =
        ",\"command\":{\"id\":\"json-command-1\",\"action\":\"fixture:increment\","
        "\"subject\":\"" +
        instanceText + "\",\"observedTick\":0,\"expectedRevision\":1,\"parameters\":{}}";
    auto submitted = eve::executeGameplayControlJson(envelope("submit", instanceText, command));
    REQUIRE(submitted.ok());
    CHECK(submitted.value().find("json-command-1") != std::string::npos);
    CHECK(submitted.value().find("\"resultingRevision\":2") != std::string::npos);

    auto advanced =
        eve::executeGameplayControlJson(envelope("advance", instanceText, ",\"tick\":2,\"deltaNanoseconds\":16666667"));
    REQUIRE(advanced.ok());
    CHECK(advanced.value().find("\"tick\":2") != std::string::npos);

    auto events = eve::executeGameplayControlJson(envelope("events", instanceText, ",\"afterSequence\":0"));
    REQUIRE(events.ok());
    CHECK(events.value().find("\"events\":[]") != std::string::npos);
}

TEST_CASE("gameplay.control.routerDispatchesByInstanceAcrossSameDomainProviders") {
    const auto   first  = subject("00000000-0000-7000-8000-000000000a11");
    const auto   second = subject("00000000-0000-7000-8000-000000000a12");
    JsonProvider one(first);
    JsonProvider two(second);

    std::string firstText  = first.format();
    std::string secondText = second.format();

    // 两个 provider 同域：各自认领自己的实例，路由器不再对整个域报冲突。
    auto observedOne = eve::executeGameplayControlJson(envelope("observe", firstText));
    REQUIRE(observedOne.ok());
    CHECK(observedOne.value().find("\"revision\":1") != std::string::npos);
    auto observedTwo = eve::executeGameplayControlJson(envelope("observe", secondText));
    REQUIRE(observedTwo.ok());
    CHECK(observedTwo.value().find("\"revision\":1") != std::string::npos);

    // submit 只落到认领该实例的 provider：被路由到的那个前进，另一个不动。
    const std::string command = ",\"command\":{\"id\":\"routed-1\",\"action\":\"fixture:increment\",\"subject\":\"" +
                                secondText + "\",\"observedTick\":0,\"expectedRevision\":1,\"parameters\":{}}";
    auto submitted = eve::executeGameplayControlJson(envelope("submit", secondText, command));
    REQUIRE(submitted.ok());
    CHECK(submitted.value().find("routed-1") != std::string::npos);
    CHECK(submitted.value().find("\"resultingRevision\":2") != std::string::npos);

    auto firstStillIdle = eve::executeGameplayControlJson(envelope("observe", firstText));
    REQUIRE(firstStillIdle.ok());
    CHECK(firstStillIdle.value().find("\"revision\":1") != std::string::npos);
}

TEST_CASE("gameplay.control.routerRejectsAmbiguousOrUnownedInstances") {
    const auto   first  = subject("00000000-0000-7000-8000-000000000a21");
    const auto   second = subject("00000000-0000-7000-8000-000000000a22");
    const auto   third  = subject("00000000-0000-7000-8000-000000000a23");
    JsonProvider one(first);
    JsonProvider two(second);
    JsonProvider alsoFirst(first);

    // 两个 provider 都认领同一实例：这是真正的歧义，不能猜。
    auto ambiguous = eve::executeGameplayControlJson(envelope("observe", first.format()));
    CHECK(!ambiguous.ok());
    CHECK_EQ(ambiguous.code(), eve::StatusCode::Conflict);

    // 谁都不认领：保持原来的"同域多 provider"冲突，而不是随便挑一个执行副作用。
    auto unowned = eve::executeGameplayControlJson(envelope("observe", third.format()));
    CHECK(!unowned.ok());
    CHECK_EQ(unowned.code(), eve::StatusCode::Conflict);
}

TEST_CASE("gameplay.control.jsonFacadeRejectsUnknownSchemaFields") {
    auto rejected = eve::executeGameplayControlJson(
        R"({"schemaId":"evengine.gameplay-control-request","schemaVersion":1,"op":"domains","mystery":true})");
    CHECK(!rejected.ok());
    CHECK_EQ(rejected.code(), eve::StatusCode::Rejected);
}

TEST_CASE("gameplay.control.squirrelFacadeUsesTheSharedJsonRouter") {
    eve::dev::DevTool::instance().detach();
    eve::Runtime runtime(128, ssq::Libs::ALL);
    eve::ModuleManager::expose(runtime.vm());
    eve::dev::DevTool::instance().attach(runtime.vm(), false);
    eve::dev::DevTool::instance().exposeScriptApi(runtime.vm());
    runtime.runSource(
        R"SQ(function callGameplay() {
            return eve.dev.gameplay("{\"schemaId\":\"evengine.gameplay-control-request\",\"schemaVersion\":1,\"op\":\"domains\"}");
        })SQ",
        "gameplay-control-facade.nut");
    const std::string response = runtime.vm().callFunc(runtime.vm().findFunc("callGameplay"), runtime.vm()).toString();
    CHECK(response.find("\"domains\"") != std::string::npos);
    eve::dev::DevTool::instance().detach();
}
}  // namespace test_gameplay_control_json
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_keyboard_cpp)
#include "PrivateTestScope.h"
#include "keyboard/Keyboard.h"
#include "window/Window.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// keyboard_cpp.cpp
namespace test_keyboard_cpp {
namespace {

eve::keyboard::Keyboard* keyboard() { return eve::keyboard::Keyboard::create(); }

eve::window::Window* ensureVideoWindow() {
    auto* win = eve::window::Window::create();
    if (win == nullptr) return nullptr;
    eve::window::WindowSettings settings;
    settings.width    = 320;
    settings.height   = 240;
    settings.centered = true;
    if (!win->setWindowSettings(settings)) return nullptr;
    return win;
}

}  // namespace

TEST_CASE("keyboard.create") {
    auto* kb = keyboard();
    REQUIRE(kb != nullptr);
    CHECK(kb->getName() == "Keyboard");
}

TEST_CASE("keyboard.keyRepeat") {
    auto* kb = keyboard();
    REQUIRE(kb != nullptr);
    kb->setKeyRepeat(false);
    CHECK(!kb->hasKeyRepeat());
    kb->setKeyRepeat(true);
    CHECK(kb->hasKeyRepeat());
}

TEST_CASE("keyboard.textInput") {
    auto* win = ensureVideoWindow();
    if (win == nullptr) return;

    auto* kb = keyboard();
    REQUIRE(kb != nullptr);

    kb->setTextInput(false);
    CHECK(!kb->hasTextInput());
    kb->setTextInput(true);
    CHECK(kb->hasTextInput());
    kb->setTextInput(false);
    CHECK(!kb->hasTextInput());
}

TEST_CASE("keyboard.keyScancodeRoundTrip") {
    auto* win = ensureVideoWindow();
    if (win == nullptr) return;

    auto* kb = keyboard();
    REQUIRE(kb != nullptr);

    // SDL names; layout may remap but A should round-trip on QWERTY-like layouts.
    std::string scancode = kb->getScancodeFromKey("A");
    CHECK(!scancode.empty());
    std::string key = kb->getKeyFromScancode(scancode);
    CHECK(!key.empty());
}

TEST_CASE("keyboard.isDownUnknown") {
    auto* kb = keyboard();
    REQUIRE(kb != nullptr);
    CHECK(!kb->isDown("NotARealKey"));
    CHECK(!kb->isScancodeDown("NotARealScancode"));
}
}  // namespace test_keyboard_cpp
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_os)
#include "PrivateTestScope.h"
#include <cmath>
#include <string>
#include "os/OS.h"
#include "timer/Timer.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// os.cpp
namespace test_os {
using namespace eve::os;

TEST_CASE("os.module.basics") {
    auto* sys = OS::create();
    REQUIRE(sys != nullptr);
    CHECK_EQ(sys->getName(), std::string("OS"));
    CHECK(!sys->getEngineVersion().empty());
    CHECK(!sys->getPlatform().empty());
    CHECK(!sys->getOS().empty());
}

TEST_CASE("os.cpuAndRam") {
    auto* sys = OS::create();
    CHECK_GE(sys->getProcessorCount(), 1);
    CHECK_GE(sys->getSystemRAM(), 1);
    // Process RSS may be 0 on some platforms before pages are faulted; allow >= 0.
    CHECK_GE(sys->getProcessMemoryMB(), 0);
    CHECK_GE(sys->getCPUCacheLineSize(), 0);
}

TEST_CASE("os.wallTimeAndSleep") {
    auto* sys = OS::create();
    float t0  = sys->getWallTime();
    // float epoch seconds only have ~1s precision; just sanity-check magnitude.
    CHECK_GT(t0, 1.0e9f);
    // sleep must not throw / hang forever — measure with Timer (monotonic).
    auto* timer = eve::timer::Timer::create();
    timer->step();
    sys->sleepMilliseconds(30);
    float dt = timer->step();
    CHECK_GE(dt, 0.02f);
}

TEST_CASE("os.power") {
    auto*       sys = OS::create();
    std::string st  = sys->getPowerState();
    CHECK((st == "unknown" || st == "on_battery" || st == "no_battery" || st == "charging" || st == "charged"));
    int secs = sys->getPowerSecondsLeft();
    int pct  = sys->getPowerPercent();
    CHECK_GE(secs, -1);
    CHECK_GE(pct, -1);
    if (pct >= 0) CHECK_LE(pct, 100);
}

TEST_CASE("os.gpuBeforeInit") {
    // Graphics may exist as a module singleton but not be Vulkan-initialized in unit tests.
    auto* sys = OS::create();
    // Must not crash; empty/0 is fine.
    (void)sys->getGpuName();
    (void)sys->getGpuVendor();
    (void)sys->getGpuDeviceType();
    CHECK_GE(sys->getGpuMemoryTotalMB(), 0);
}

TEST_CASE("os.clipboardRoundTrip") {
    auto* sys = OS::create();
    // Clipboard may fail in headless CI; soft-check.
    try {
        sys->setClipboardText("eve-os-clipboard-test");
        std::string got = sys->getClipboardText();
        CHECK_EQ(got, std::string("eve-os-clipboard-test"));
    } catch (...) {
        // Accept failure when no clipboard provider is available.
        CHECK(true);
    }
}
}  // namespace test_os
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_pcg_build_config)
#include "PrivateTestScope.h"
#include <limits>
#include "scene/PcgBuildConfig.h"
#include "scene/PcgGrowth.h"
#include "scene/Scene.h"
#include "scene/SceneNodeRef.h"
#include "zeroerr/unittest.h"
// pcg_build_config.cpp
namespace test_pcg_build_config {
// pcg_build_config.cpp
using namespace eve::scene;
TEST_CASE("scene.pcgBuildConfig.preservesOrderedHistory") {
    PcgBuildConfig c;
    CHECK_EQ(c.getPublicationType(), 1);
    REQUIRE(c.setPublicationType(0).ok());
    REQUIRE(c.addHistory(4, "World", 123456789).ok());
    REQUIRE(c.addHistory(0, "World", 123456790).ok());
    CHECK_EQ(c.getHistoryCount(), uint64_t(2));
    auto a = c.getHistoryCategory(0);
    auto n = c.getHistorySceneName(1);
    auto t = c.getHistoryTimestamp(1);
    REQUIRE(a.ok());
    REQUIRE(n.ok());
    REQUIRE(t.ok());
    CHECK_EQ(a.value(), 4);
    CHECK_EQ(n.value(), "World");
    CHECK_EQ(t.value(), int64_t(123456790));
    c.clearHistory();
    CHECK_EQ(c.getHistoryCount(), uint64_t(0));
}
TEST_CASE("scene.pcgBuildConfig.rejectsInvalidEnumsAndIndices") {
    PcgBuildConfig c;
    CHECK(!c.setPublicationType(2).ok());
    CHECK_EQ(c.getPublicationType(), 1);
    CHECK(!c.addHistory(7, "", 0).ok());
    CHECK_EQ(c.getHistoryCount(), uint64_t(0));
    CHECK(!c.getHistoryCategory(0).ok());
}

// pcg_growth.cpp
using namespace eve::scene;
TEST_CASE("scene.pcgGrowth.linearVarianceAndDelayedDeath") {
    PcgGrowth g;
    REQUIRE(g.configure(.15f, 1, .25f, 5).ok());
    REQUIRE(g.start(10, 77).ok());
    float end = g.getActualEndScale();
    CHECK_GE(end, 1.f);
    CHECK_LT(end, 1.25f);
    auto start = g.advance(10);
    REQUIRE(start.ok());
    CHECK_EQ(start.value(), .15f);
    auto mid = g.advance(12.5f);
    REQUIRE(mid.ok());
    CHECK_GT(mid.value(), .15f);
    CHECK_LT(mid.value(), end);
    auto done = g.advance(15);
    REQUIRE(done.ok());
    CHECK_EQ(done.value(), end);
    CHECK(g.getFinished());
    REQUIRE(g.die(20).ok());
    auto early = g.shouldDestroy(24.999f);
    REQUIRE(early.ok());
    CHECK(!early.value());
    auto due = g.shouldDestroy(25);
    REQUIRE(due.ok());
    CHECK(due.value());
}
TEST_CASE("scene.pcgGrowth.rejectsInvalidInputAtomically") {
    PcgGrowth g;
    REQUIRE(g.configure(.2f, 1, .2f, 2).ok());
    REQUIRE(g.start(4, 9).ok());
    float before = g.getActualEndScale();
    CHECK(!g.configure(0, 1, .2f, 2).ok());
    CHECK_EQ(g.getActualEndScale(), before);
    CHECK(!g.advance(3).ok());
    CHECK_EQ(g.getScale(), .2f);
    CHECK(!g.die(std::numeric_limits<float>::quiet_NaN()).ok());
}
TEST_CASE("scene.pcgGrowth.appliesToAndDestroysRealNode") {
    Scene* scene = Scene::create();
    scene->beginBuild();
    scene->beginNode("root");
    scene->addNode("plant");
    scene->end();
    REQUIRE(scene->mountBuildAs("pcg-growth-test"));
    SceneNodeRef node("pcg-growth-test", "plant");
    REQUIRE(node.isValid());
    PcgGrowth g;
    REQUIRE(g.start(10, 3).ok());
    auto applied = applyPcgGrowth(g, node, 10);
    REQUIRE(applied.ok());
    CHECK(!applied.value());
    CHECK_EQ(node.getScaleX(), .15f);
    REQUIRE(g.die(11).ok());
    auto removed = applyPcgGrowth(g, node, 16);
    REQUIRE(removed.ok());
    CHECK(removed.value());
    CHECK(!node.isValid());
}
}  // namespace test_pcg_build_config
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_play_host)
#include "PrivateTestScope.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <simplesquirrel/simplesquirrel.hpp>
#include <string>
#include <vector>
#include "common/Diagnostic.h"
#include "common/Value.h"
#include "devtools/Debugger.hpp"
#include "devtools/McpServer.hpp"
#include "devtools/PlayHost.h"
#include "devtools/Snapshot.hpp"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// play_host.cpp
namespace test_play_host {
using namespace eve;
using namespace eve::dev;

namespace {

class FakePlayHostRuntime final : public IPlayHostRuntime {
public:
    bool          paused() const override { return paused_; }
    std::uint64_t hostFrame() const override { return hostFrame_; }
    void          pause() override { paused_ = true; }
    void          play() override { paused_ = false; }

    Result<void> stepFrames(std::int64_t count) override {
        hostFrame_ += static_cast<std::uint64_t>(count);
        paused_ = true;
        return Result<void>::success();
    }

    Result<Value> loadContract() const override {
        if (contractMissing)
            return Result<Value>::failure(Diagnostic::error(DiagnosticCode::NotFound, "missing contract", "contract"));
        return Result<Value>::success(contract);
    }

    Result<Value> observeScriptRoot(std::string_view root, const std::vector<std::string>& fields) const override {
        const auto* object = roots.getIf<Value::Object>();
        if (!object)
            return Result<Value>::failure(Diagnostic::error(DiagnosticCode::NotFound, "no roots", "observation"));
        const auto found = object->find(std::string(root));
        if (found == object->end())
            return Result<Value>::failure(
                Diagnostic::error(DiagnosticCode::NotFound, "root missing", "observation.path"));
        if (fields.empty()) return Result<Value>::success(found->second);
        Value::Object projected;
        for (const auto& field : fields) {
            Value       current = found->second;
            std::string token;
            bool        ok = true;
            for (char ch : field + std::string(".")) {
                if (ch != '.') {
                    token.push_back(ch);
                    continue;
                }
                const auto* node = current.getIf<Value::Object>();
                if (!node) {
                    ok = false;
                    break;
                }
                auto child = node->find(token);
                if (child == node->end()) {
                    ok = false;
                    break;
                }
                Value next = child->second;
                current    = std::move(next);
                token.clear();
            }
            if (!ok)
                return Result<Value>::failure(
                    Diagnostic::error(DiagnosticCode::NotFound, "field missing", "observation.fields"));
            projected.emplace(field, std::move(current));
        }
        return Result<Value>::success(Value(std::move(projected)));
    }

    Result<Value> capturePng(std::string path) override {
        if (!capture)
            return Result<Value>::failure(Diagnostic::error(DiagnosticCode::Unsupported, "no capture", "capture"));
        lastCapturePath = path;
        return Result<Value>::success(Value(Value::Object{
            {"height", Value(std::int64_t{2})}, {"path", Value(std::move(path))}, {"width", Value(std::int64_t{3})}}));
    }

    Result<std::string> captureCheckpoint() override { return Result<std::string>::success(checkpoint); }

    Result<void> restoreCheckpoint(std::string_view json) override {
        restored    = std::string(json);
        auto parsed = Value::fromJson(json);
        if (parsed) {
            const auto* object = parsed.value().getIf<Value::Object>();
            if (object) {
                const auto found = object->find("roots");
                if (found != object->end()) roots = found->second;
            }
        }
        return Result<void>::success();
    }

    std::vector<std::string> gameplayDomains() const override { return domains; }

    Result<Value> invokeScriptAction(std::string_view id, std::string_view source) override {
        lastAction       = std::string(id);
        lastActionScript = std::string(source);
        if (id == "reset") {
            auto* object = roots.getIf<Value::Object>();
            if (object) {
                auto gameState = object->find("gameState");
                if (gameState != object->end()) {
                    auto* state = gameState->second.getIf<Value::Object>();
                    if (state) {
                        Value::Object copy     = *state;
                        copy["tick"]           = Value(std::int64_t{0});
                        (*object)["gameState"] = Value(std::move(copy));
                    }
                }
            }
        }
        return Result<Value>::success(Value(Value::Object{{"action", Value(std::string(id))}}));
    }

    Value                    contract;
    Value                    roots;
    std::string              checkpoint = R"({"version":2,"roots":{"gameState":{"tick":1}}})";
    std::string              restored;
    std::string              lastCapturePath;
    std::string              lastAction;
    std::string              lastActionScript;
    std::vector<std::string> domains{"tactics"};
    bool                     contractMissing = false;
    bool                     capture         = true;

private:
    bool          paused_    = false;
    std::uint64_t hostFrame_ = 0;
};

Value parse(std::string_view json) {
    auto value = Value::fromJson(json);
    REQUIRE(value.ok());
    return std::move(value).takeValue();
}

const char* kContract = R"json({
  "schemaId": "evengine.game-agent-contract",
  "schemaVersion": 1,
  "id": "examples/ai-game",
  "entry": "main.nut",
  "smoke": {"scene": "default", "seed": 42, "maxHostFrames": 180, "mustReach": ["combat-alive"]},
  "clock": {"defaultStep": "frame"},
  "observations": [
    {"id": "combat-alive", "kind": "script-root", "path": "gameState", "fields": ["tick", "enemy.hp"]}
  ],
  "actions": {"source": "script-map", "map": [{"id": "reset", "script": "game.reset()"}]},
  "capture": {"requiredFor": ["visual"], "backend": "engine-readback"},
  "tolerances": {"screenshot": "human-or-vlm", "numericAbs": 0.0001}
})json";

Value play(FakePlayHostRuntime& runtime, std::string_view json) {
    auto result = executePlayRequest(parse(json), runtime);
    REQUIRE(result.ok());
    return std::move(result).takeValue();
}

}  // namespace

TEST_CASE("devtools.playHost.rejectsUnknownFieldsAndRequiresContractForObserve") {
    FakePlayHostRuntime runtime;
    runtime.contract = parse(kContract);
    auto unknown     = executePlayRequest(
        parse(R"({"schemaId":"evengine.play-request","schemaVersion":1,"op":"status","extra":true})"), runtime);
    CHECK(!unknown.ok());
    CHECK_EQ(unknown.code(), StatusCode::Failed);
    CHECK_EQ(unknown.status().diagnostics().front().code(), DiagnosticCode::ParseError);

    runtime.contractMissing = true;
    auto observe            = executePlayRequest(
        parse(R"({"schemaId":"evengine.play-request","schemaVersion":1,"op":"observe","observation":"combat-alive"})"),
        runtime);
    CHECK(!observe.ok());
    CHECK_EQ(observe.status().diagnostics().front().code(), DiagnosticCode::Unsupported);

    auto status = play(runtime, R"({"schemaId":"evengine.play-request","schemaVersion":1,"op":"status"})");
    CHECK_EQ(status.getIf<Value::Object>()->at("contractLoaded").asBool(), false);
}

TEST_CASE("devtools.playHost.pausesStepsObservesCapturesAndRestores") {
    FakePlayHostRuntime runtime;
    runtime.contract = parse(kContract);
    runtime.roots    = parse(R"({"gameState":{"tick":4,"enemy":{"hp":20.0},"player":{"hp":100.0}}})");

    auto clock = play(runtime, R"({"schemaId":"evengine.play-request","schemaVersion":1,"op":"clock","mode":"pause"})");
    CHECK(clock.getIf<Value::Object>()->at("paused").asBool());

    auto stepped = play(
        runtime, R"({"schemaId":"evengine.play-request","schemaVersion":1,"op":"step","clock":"frame","count":10})");
    CHECK_EQ(stepped.getIf<Value::Object>()->at("count").asInt(), std::int64_t{10});
    CHECK_EQ(runtime.hostFrame(), std::uint64_t{10});

    auto observed =
        play(runtime,
             R"({"schemaId":"evengine.play-request","schemaVersion":1,"op":"observe","observation":"combat-alive"})");
    const auto* state = observed.getIf<Value::Object>()->at("state").getIf<Value::Object>();
    REQUIRE(state);
    CHECK_EQ(state->at("tick").asInt(), std::int64_t{4});
    CHECK_EQ(state->at("enemy.hp").asDouble(), 20.0);

    auto captured =
        play(runtime, R"({"schemaId":"evengine.play-request","schemaVersion":1,"op":"capture","path":"frame.png"})");
    CHECK_EQ(captured.getIf<Value::Object>()->at("path").asString(), std::string("frame.png"));
    CHECK_EQ(captured.getIf<Value::Object>()->at("backend").asString(), std::string("engine-readback"));

    runtime.capture = false;
    auto missingCapture =
        executePlayRequest(parse(R"({"schemaId":"evengine.play-request","schemaVersion":1,"op":"capture"})"), runtime);
    CHECK(!missingCapture.ok());
    CHECK_EQ(missingCapture.status().diagnostics().front().code(), DiagnosticCode::Unsupported);

    auto checkpoint =
        play(runtime, R"({"schemaId":"evengine.play-request","schemaVersion":1,"op":"checkpoint","mode":"capture"})");
    const std::string json = checkpoint.getIf<Value::Object>()->at("json").asString();
    Value             restoreRequest{Value::Object{
                    {"schemaId", Value("evengine.play-request")},
                    {"schemaVersion", Value(std::int64_t{1})},
                    {"op", Value("checkpoint")},
                    {"mode", Value("restore")},
                    {"json", Value(json)},
    }};
    auto              restored = executePlayRequest(restoreRequest, runtime);
    REQUIRE(restored.ok());
    CHECK_EQ(runtime.restored, json);

    auto batched = play(runtime, R"json({
      "schemaId": "evengine.play-request",
      "schemaVersion": 1,
      "op": "batch",
      "requests": [
        {"schemaId":"evengine.play-request","schemaVersion":1,"op":"clock","mode":"play"},
        {"schemaId":"evengine.play-request","schemaVersion":1,"op":"status"}
      ]
    })json");
    CHECK_EQ(batched.getIf<Value::Object>()->at("count").asInt(), std::int64_t{2});
    CHECK(!runtime.paused());

    auto nested = executePlayRequest(parse(R"json({
      "schemaId": "evengine.play-request",
      "schemaVersion": 1,
      "op": "batch",
      "requests": [
        {"schemaId":"evengine.play-request","schemaVersion":1,"op":"batch","requests":[]}
      ]
    })json"),
                                     runtime);
    CHECK(!nested.ok());
    CHECK_EQ(nested.status().diagnostics().front().code(), DiagnosticCode::Unsupported);

    auto gameplayStep = executePlayRequest(
        parse(R"({"schemaId":"evengine.play-request","schemaVersion":1,"op":"step","clock":"gameplay","count":1})"),
        runtime);
    CHECK(!gameplayStep.ok());
    CHECK_EQ(gameplayStep.status().diagnostics().front().code(), DiagnosticCode::Unsupported);
}

TEST_CASE("devtools.debugger.stepFramesReArmsUntilCountConsumed") {
    Debugger& debugger = Debugger::instance();
    debugger.detach();
    debugger.pause(PauseReason::PauseKey);
    debugger.stepFrames(3);
    CHECK(debugger.shouldRunUpdate());
    debugger.notifyFrameDone();
    CHECK(debugger.shouldRunUpdate());
    debugger.notifyFrameDone();
    CHECK(debugger.shouldRunUpdate());
    debugger.notifyFrameDone();
    CHECK(debugger.isPaused());
    debugger.resume();
}

TEST_CASE("devtools.playHost.malformedRequestsObserveEveryResult") {
    FakePlayHostRuntime runtime;
    runtime.contract = parse(kContract);
    for (const auto* input :
         {R"({"op":"observe","observation":"combat-alive"})", R"({"schemaId":42,"schemaVersion":1,"op":"status"})",
          R"({"schemaId":"evengine.play-request","op":"status"})"}) {
        auto result = executePlayRequest(parse(input), runtime);
        CHECK(!result.ok());
        CHECK_EQ(result.status().diagnostics().front().code(), DiagnosticCode::ParseError);
    }
    const auto observe =
        parse(R"({"schemaId":"evengine.play-request","schemaVersion":1,"op":"observe","observation":"combat-alive"})");
    for (const auto* field : {"schemaId", "schemaVersion", "id", "entry"}) {
        runtime.contract = parse(kContract);
        runtime.contract.getIf<Value::Object>()->erase(field);
        auto result = executePlayRequest(observe, runtime);
        CHECK(!result.ok());
        CHECK_EQ(result.status().diagnostics().front().code(), DiagnosticCode::ParseError);
    }
    for (const auto* field : {"id", "kind", "path"}) {
        runtime.contract = parse(kContract);
        runtime.contract.find("observations")->at(0).getIf<Value::Object>()->erase(field);
        auto result = executePlayRequest(observe, runtime);
        CHECK(!result.ok());
        CHECK_EQ(result.status().diagnostics().front().code(), DiagnosticCode::ParseError);
    }
}
TEST_CASE("devtools.playHost.malformedRequestsAndContractsReturnDiagnostics") {
    FakePlayHostRuntime runtime;
    for (const auto* request : {R"({"op":"observe"})", R"({"schemaId":"evengine.play-request","op":"status"})"}) {
        auto result = executePlayRequest(parse(request), runtime);
        REQUIRE(!result.ok());
        CHECK_EQ(result.status().diagnostics().front().code(), DiagnosticCode::ParseError);
    }
    for (
        const auto* contract :
        {R"({})", R"({"schemaId":"evengine.game-agent-contract"})",
         R"({"schemaId":"evengine.game-agent-contract","schemaVersion":1})",
         R"({"schemaId":"evengine.game-agent-contract","schemaVersion":1,"id":"test"})",
         R"({"schemaId":"evengine.game-agent-contract","schemaVersion":1,"id":"test","entry":"main.nut","observations":[{}]})",
         R"({"schemaId":"evengine.game-agent-contract","schemaVersion":1,"id":"test","entry":"main.nut","observations":[{"id":"test"}]})"}) {
        runtime.contract = parse(contract);
        auto result      = executePlayRequest(
            parse(R"({"schemaId":"evengine.play-request","schemaVersion":1,"op":"observe","observation":"test"})"),
            runtime);
        REQUIRE(!result.ok());
    }
}

TEST_CASE("devtools.playHost.liveObservationProjectsBeforeSerializing") {
    ssq::VM    vm(1024, ssq::Libs::ALL);
    const auto directory =
        std::filesystem::temp_directory_path() /
        ("eve-play-observe-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(directory);
    struct Cleanup {
        std::filesystem::path    directory;
        std::string              root       = McpServer::instance().gameRoot();
        std::vector<std::string> persistent = Snapshot::instance().roots();
        std::vector<std::string> transient  = Snapshot::instance().transientRoots();
        ~Cleanup() {
            Debugger::instance().detach();
            McpServer::instance().setGameRoot(root);
            Snapshot::instance().setRootPolicies(persistent, transient);
            std::error_code error;
            std::filesystem::remove_all(directory, error);
        }
    } cleanup{directory};
    {
        std::ofstream file(directory / "game.agent.json");
        file << kContract;
    }
    McpServer::instance().setGameRoot(directory.string());
    Snapshot::instance().setRootPolicies({"gameState", "unrelated"}, {});
    vm.run(vm.compileSource(R"(
        class Handle {}
        gameState <- {tick=7, enemy={hp=20}, handle=Handle()};
        unrelated <- {handle=Handle()};
    )"));
    Debugger::instance().attach(vm.getHandle());
    const auto top     = sq_gettop(vm.getHandle());
    const auto request = parse(
        R"({"schemaId":"evengine.play-request","schemaVersion":1,"op":"observe","observation":"combat-alive","trace":"off"})");
    auto observed = executePlayRequest(request);
    REQUIRE(observed.ok());
    auto json = observed.value().toJson();
    REQUIRE(json.ok());
    CHECK(json.value().find("\"tick\":7") != std::string::npos);
    CHECK_EQ(sq_gettop(vm.getHandle()), top);
    vm.run(vm.compileSource("delete gameState.enemy.hp;"));
    auto missing = executePlayRequest(request);
    REQUIRE(!missing.ok());
    CHECK_EQ(missing.status().diagnostics().front().code(), DiagnosticCode::NotFound);
    CHECK_EQ(sq_gettop(vm.getHandle()), top);
    vm.run(vm.compileSource("gameState.enemy.hp <- Handle();"));
    auto unsupported = executePlayRequest(request);
    REQUIRE(!unsupported.ok());
    CHECK_EQ(sq_gettop(vm.getHandle()), top);
    Snapshot::instance().setRootPolicies({}, {"gameState"});
    auto excluded = executePlayRequest(request);
    REQUIRE(!excluded.ok());
    CHECK_EQ(excluded.status().diagnostics().front().code(), DiagnosticCode::NotFound);
}
}  // namespace test_play_host
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_plugins)
#include "PrivateTestScope.h"
#include <string>
#include "common/Runtime.h"
#include "plugins/Plugins.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// plugins.cpp
namespace test_plugins {
using eve::plugins::Plugins;

TEST_CASE("plugins.load.rejectsEmptyPath") {
    Plugins plugins;
    CHECK_THROWS((plugins.load(""), false));
    CHECK_EQ(plugins.isLoaded(""), false);
}

TEST_CASE("plugins.load.rejectsMissingLibrary") {
    Plugins           plugins;
    const std::string missing = "evengine-plugin-that-does-not-exist.dll";
    CHECK_THROWS((plugins.load(missing), false));
    CHECK_EQ(plugins.isLoaded(missing), false);
}

TEST_CASE("plugins.load.closesLibraryWithoutEntrypoint") {
    Plugins plugins;
#if defined(_WIN32)
    const std::string library = "kernel32.dll";
#elif defined(__APPLE__)
    const std::string library = "/usr/lib/libSystem.B.dylib";
#else
    const std::string library = "libm.so.6";
#endif
    CHECK_THROWS((plugins.load(library), false));
    CHECK_EQ(plugins.isLoaded(library), false);
    CHECK_EQ(plugins.unload(library), false);
}

TEST_CASE("plugins.unload.unknownPathIsNoop") {
    Plugins plugins;
    CHECK_EQ(plugins.unload("not-loaded"), false);
}

#if !defined(EVENGINE_ANDROID) && !defined(EVENGINE_IOS)
TEST_CASE("plugins.load.nativeLibraryAndInstantiateCppModule") {
    eve::Runtime runtime(512, ssq::Libs::ALL);
    runtime.initialize();

    Plugins           plugins;
    const std::string library =
        std::string(EVENGINE_TEST_BINARY_DIR) + "/native_test_plugin" + EVENGINE_TEST_SHARED_LIBRARY_SUFFIX;
    REQUIRE(plugins.load(library));
    CHECK(plugins.isLoaded(library));

    auto* module = eve::ModuleManager::requireInstance<eve::Module>("NativeTestPlugin");
    REQUIRE(module != nullptr);
    CHECK_EQ(module->getName(), std::string("NativeTestPlugin"));

    // Native classes and VM closures still point into the library, so plugins
    // deliberately remain resident for the rest of the process.
    CHECK_EQ(plugins.unload(library), false);
    CHECK(plugins.isLoaded(library));
}
#endif
}  // namespace test_plugins
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_renderflow)
#include <string>
#include <vector>
#include "PrivateTestScope.h"
#include "common/Exception.h"
#include "common/RenderTrace.h"
#include "devtools/RenderFlow.hpp"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// renderflow.cpp
namespace test_renderflow {
using namespace eve::dev;

static bool pathHas(const RenderSliceResult& s, RenderEventKind kind, const char* nameOrDetail) {
    for (const auto& e : s.path) {
        if (e.kind != kind) continue;
        if (e.name == nameOrDetail || e.detail == nameOrDetail) return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Core slicing
// ---------------------------------------------------------------------------

TEST_CASE("devtools.renderflow.passAndBindSlice") {
    RenderFlow flow;
    flow.frameBegin();
    flow.passBegin("RenderSystem2D");
    flow.target("screen");
    flow.bind("texture", "hero");
    flow.draw("drawTexturedRectShaderUV", "hero");
    flow.error("draw failed: bad descriptor");
    flow.passEnd("RenderSystem2D");
    flow.frameEnd();

    RenderSliceResult slice = flow.sliceBackward({});
    CHECK(!slice.passes.empty());
    CHECK_EQ(slice.passes.back(), std::string("RenderSystem2D"));
    CHECK(pathHas(slice, RenderEventKind::Bind, "hero"));
    CHECK(pathHas(slice, RenderEventKind::Draw, "drawTexturedRectShaderUV"));
    CHECK(pathHas(slice, RenderEventKind::Error, "error"));

    const std::string report = flow.formatErrorReport("draw failed: bad descriptor");
    CHECK(report.find("Render Pipeline Trace") != std::string::npos);
    CHECK(report.find("RenderSystem2D") != std::string::npos);
    CHECK(report.find("hero") != std::string::npos);
}

TEST_CASE("devtools.renderflow.nestedPasses") {
    RenderFlow flow;
    flow.frameBegin();
    flow.passBegin("RenderSystem3D");
    flow.passBegin("ShadowPass");
    flow.bind("mesh", "caster");
    flow.draw("drawMeshShadow", "cascade");
    flow.passEnd("ShadowPass");
    flow.bind("mesh", "rock");
    flow.bind("texture", "albedo");
    flow.draw("drawMeshShader", "default");
    flow.error("pipeline create failed");
    // still inside RenderSystem3D
    RenderSliceResult slice = flow.sliceBackward({});
    REQUIRE(slice.passes.size() >= 1u);
    CHECK_EQ(slice.passes[0], std::string("RenderSystem3D"));
    CHECK(pathHas(slice, RenderEventKind::Draw, "drawMeshShader"));
    const bool sawMeshOrAlbedo =
        pathHas(slice, RenderEventKind::Bind, "rock") || pathHas(slice, RenderEventKind::Bind, "albedo");
    CHECK(sawMeshOrAlbedo);
}

TEST_CASE("devtools.renderflow.resourceFocusedCriterion") {
    RenderFlow flow;
    flow.passBegin("RenderSystem2D");
    flow.bind("texture", "bg");
    flow.draw("drawTexturedRectShaderUV", "bg");
    flow.bind("texture", "hero");
    flow.draw("drawTexturedRectShaderUV", "hero");
    flow.error("hero atlas missing");
    RenderSliceCriterion c;
    c.resource              = "hero";
    RenderSliceResult slice = flow.sliceBackward(c);
    CHECK(pathHas(slice, RenderEventKind::Bind, "hero"));
    CHECK(pathHas(slice, RenderEventKind::Error, "error"));
}

TEST_CASE("devtools.renderflow.eventIdSeed") {
    RenderFlow flow;
    flow.passBegin("P");
    flow.bind("shader", "s1");
    const uint32_t drawId = [&]() {
        flow.draw("drawSolidRect", "solid");
        return flow.events().back().id;
    }();
    flow.bind("shader", "s2");
    flow.draw("drawSolidRect", "solid2");
    flow.error("later");

    RenderSliceCriterion c;
    c.eventId               = drawId;
    RenderSliceResult slice = flow.sliceBackward(c);
    CHECK(pathHas(slice, RenderEventKind::Draw, "drawSolidRect"));
    // Seeded on first draw — should see s1 bind via data deps / parent chain.
    bool sawS1 = pathHas(slice, RenderEventKind::Bind, "s1");
    CHECK(sawS1);
}

TEST_CASE("devtools.renderflow.emptySlice") {
    RenderFlow        flow;
    RenderSliceResult slice = flow.sliceBackward({});
    CHECK(slice.eventIds.empty());
    CHECK(slice.summary.find("empty") != std::string::npos);
}

TEST_CASE("devtools.renderflow.clear") {
    RenderFlow flow;
    flow.frameBegin();
    flow.draw("x", "y");
    flow.clear();
    CHECK_EQ(flow.eventCount(), 0u);
    CHECK(flow.event(1) == nullptr);
}

TEST_CASE("devtools.renderflow.drawWithoutBinds") {
    RenderFlow flow;
    flow.passBegin("RenderSystem2D");
    flow.draw("drawSolidRect", "solid");
    flow.error("clear color invalid");
    RenderSliceResult slice = flow.sliceBackward({});
    CHECK(pathHas(slice, RenderEventKind::PassBegin, "RenderSystem2D"));
    CHECK(pathHas(slice, RenderEventKind::Draw, "drawSolidRect"));
}

TEST_CASE("devtools.renderflow.errorWithoutDraw") {
    RenderFlow flow;
    flow.passBegin("Present");
    flow.error("swapchain recreate failed");
    RenderSliceResult slice = flow.sliceBackward({});
    CHECK(pathHas(slice, RenderEventKind::Error, "error"));
    CHECK(pathHas(slice, RenderEventKind::PassBegin, "Present"));
}

TEST_CASE("devtools.renderflow.multipleErrorsSeedsLast") {
    RenderFlow flow;
    flow.passBegin("A");
    flow.error("first");
    flow.passEnd("A");
    flow.passBegin("B");
    flow.bind("mesh", "m");
    flow.error("second");
    RenderSliceResult slice = flow.sliceBackward({});
    CHECK(pathHas(slice, RenderEventKind::PassBegin, "B"));
    CHECK(pathHas(slice, RenderEventKind::Bind, "m"));
    // Active passes at last error should be B only.
    REQUIRE(!slice.passes.empty());
    CHECK_EQ(slice.passes.back(), std::string("B"));
}

TEST_CASE("devtools.renderflow.targetSwitch") {
    RenderFlow flow;
    flow.passBegin("RenderSystem2D");
    flow.target("screen");
    flow.draw("drawSolidRect", "a");
    flow.target("canvas");
    flow.draw("drawSolidRect", "b");
    flow.error("canvas size 0");
    RenderSliceResult slice = flow.sliceBackward({});
    CHECK(pathHas(slice, RenderEventKind::Target, "canvas"));
}

TEST_CASE("devtools.renderflow.bindKinds") {
    RenderFlow flow;
    flow.passBegin("RenderSystem3D");
    flow.bind("texture", "albedo");
    flow.bind("shader", "pbr");
    flow.bind("mesh", "rock");
    flow.bind("font", "ui");
    flow.bind("canvas", "offscreen");
    flow.draw("drawMeshShader", "pbr");
    flow.error("descriptor");
    RenderSliceResult slice = flow.sliceBackward({});
    CHECK(pathHas(slice, RenderEventKind::Bind, "albedo"));
    const bool sawShaderOrDraw =
        pathHas(slice, RenderEventKind::Bind, "pbr") || pathHas(slice, RenderEventKind::Draw, "drawMeshShader");
    CHECK(sawShaderOrDraw);
    CHECK(pathHas(slice, RenderEventKind::Bind, "rock"));
}

TEST_CASE("devtools.renderflow.twoFramesErrorInSecond") {
    RenderFlow flow;
    flow.frameBegin();
    flow.passBegin("RenderSystem2D");
    flow.draw("drawSolidRect", "ok");
    flow.passEnd("RenderSystem2D");
    flow.frameEnd();

    flow.frameBegin();
    flow.passBegin("RenderSystem2D");
    flow.bind("texture", "bad");
    flow.draw("drawTexturedRectShaderUV", "bad");
    flow.error("oom");
    RenderSliceResult slice = flow.sliceBackward({});
    CHECK(pathHas(slice, RenderEventKind::Bind, "bad"));
    CHECK(pathHas(slice, RenderEventKind::Error, "error"));
}

TEST_CASE("devtools.renderflow.formatReportTruncates") {
    RenderFlow flow;
    flow.passBegin("P");
    for (int i = 0; i < 80; ++i) {
        flow.bind("texture", ("t" + std::to_string(i)).c_str());
        flow.draw("draw", ("d" + std::to_string(i)).c_str());
    }
    flow.error("boom");
    const std::string report = flow.formatErrorReport("boom");
    CHECK(report.find("...") != std::string::npos);
    CHECK(report.find("render slice:") != std::string::npos);
}

TEST_CASE("devtools.renderflow.passEndPopsStack") {
    RenderFlow flow;
    flow.passBegin("Outer");
    flow.passBegin("Inner");
    flow.passEnd("Inner");
    flow.error("after inner");
    RenderSliceResult slice = flow.sliceBackward({});
    REQUIRE(slice.passes.size() == 1u);
    CHECK_EQ(slice.passes[0], std::string("Outer"));
}

TEST_CASE("devtools.renderflow.nullNameArgs") {
    RenderFlow flow;
    flow.passBegin(nullptr);
    flow.bind(nullptr, nullptr);
    flow.draw(nullptr, nullptr);
    flow.target(nullptr);
    flow.error(nullptr);
    CHECK(flow.eventCount() >= 5u);
    RenderSliceResult slice = flow.sliceBackward({});
    CHECK(!slice.eventIds.empty());
}

// ---------------------------------------------------------------------------
// Ring buffer
// ---------------------------------------------------------------------------

TEST_CASE("devtools.renderflow.ringBuffer") {
    RenderFlow flow;
    flow.setMaxEvents(16);
    for (int i = 0; i < 40; ++i) flow.draw("drawSolidRect", "solid");
    CHECK_EQ(flow.eventCount(), 16u);
    CHECK(flow.event(flow.events().front().id - 1) == nullptr);
}

TEST_CASE("devtools.renderflow.setMaxEventsClampShrink") {
    RenderFlow flow;
    flow.setMaxEvents(1);
    CHECK_EQ(flow.maxEvents(), 16u);
    for (int i = 0; i < 20; ++i) flow.draw("d", "x");
    CHECK_EQ(flow.eventCount(), 16u);
    flow.setMaxEvents(16);
    CHECK_EQ(flow.eventCount(), 16u);
}

TEST_CASE("devtools.renderflow.ringKeepsRecentErrorContext") {
    RenderFlow flow;
    flow.setMaxEvents(16);
    for (int i = 0; i < 40; ++i) {
        flow.passBegin("Old");
        flow.draw("old", "x");
        flow.passEnd("Old");
    }
    flow.passBegin("Hot");
    flow.bind("mesh", "last");
    flow.draw("drawMeshShader", "last");
    flow.error("fail");
    CHECK_EQ(flow.eventCount(), 16u);
    RenderSliceResult slice = flow.sliceBackward({});
    const bool        sawHotOrError =
        pathHas(slice, RenderEventKind::PassBegin, "Hot") || pathHas(slice, RenderEventKind::Error, "error");
    CHECK(sawHotOrError);
    const bool sawLast =
        pathHas(slice, RenderEventKind::Bind, "last") || pathHas(slice, RenderEventKind::Draw, "drawMeshShader");
    CHECK(sawLast);
}

TEST_CASE("devtools.renderflow.oneInOneOut") {
    RenderFlow flow;
    flow.setMaxEvents(16);
    for (int i = 0; i < 16; ++i) flow.draw("d", "x");
    const uint32_t front = flow.events().front().id;
    flow.draw("d", "new");
    CHECK_EQ(flow.eventCount(), 16u);
    CHECK_EQ(flow.events().front().id, front + 1);
    CHECK_EQ(flow.events().back().detail, std::string("new"));
}

// ---------------------------------------------------------------------------
// Hooks / Exception / RAII
// ---------------------------------------------------------------------------

TEST_CASE("devtools.renderflow.hookNoopWhenUnset") {
    eve::debug::setRenderTracer(nullptr);
    eve::debug::rtFrameBegin();
    eve::debug::rtPassBegin("X");
    eve::debug::rtBind("texture", "t");
    eve::debug::rtDraw("draw", "d");
    eve::debug::rtPassEnd("X");
    eve::debug::rtFrameEnd();
    eve::debug::rtError("no tracer");
    CHECK(eve::debug::renderTracer() == nullptr);
}

TEST_CASE("devtools.renderflow.hookViaException") {
    RenderFlow flow;
    eve::debug::setRenderTracer(&flow);
    eve::debug::rtFrameBegin();
    eve::debug::rtPassBegin("RenderSystem3D");
    eve::debug::rtBind("mesh", "rock");
    eve::debug::rtDraw("drawMeshShader", "default");
    try {
        throw eve::Exception("failed to create mesh3d-style pipeline");
    } catch (const eve::Exception& e) {
        const std::string report = flow.formatErrorReport(e.what());
        CHECK(report.find("Render Pipeline Trace") != std::string::npos);
        CHECK(report.find("RenderSystem3D") != std::string::npos);
        const bool hasDrawOrBind =
            report.find("drawMeshShader") != std::string::npos || report.find("rock") != std::string::npos;
        CHECK(hasDrawOrBind);
        CHECK(std::string(e.what()).find("mesh3d") != std::string::npos);
    }
    eve::debug::setRenderTracer(nullptr);
    CHECK(eve::debug::renderTracer() == nullptr);
}

TEST_CASE("devtools.renderflow.exceptionWithoutTracer") {
    eve::debug::setRenderTracer(nullptr);
    try {
        throw eve::Exception("newTexture: invalid args");
    } catch (const eve::Exception& e) {
        CHECK(std::string(e.what()).find("newTexture") != std::string::npos);
    }
}

TEST_CASE("devtools.renderflow.passScopeRAII") {
    RenderFlow flow;
    eve::debug::setRenderTracer(&flow);
    {
        eve::debug::RenderPassScope scope("ScopedPass");
        eve::debug::rtDraw("inside", "x");
    }
    // After scope, pass should be ended.
    bool sawBegin = false, sawEnd = false;
    for (const auto& e : flow.events()) {
        if (e.kind == RenderEventKind::PassBegin && e.name == "ScopedPass") sawBegin = true;
        if (e.kind == RenderEventKind::PassEnd && e.name == "ScopedPass") sawEnd = true;
    }
    CHECK(sawBegin);
    CHECK(sawEnd);
    eve::debug::setRenderTracer(nullptr);
}

TEST_CASE("devtools.renderflow.rtHelpersForward") {
    RenderFlow flow;
    eve::debug::setRenderTracer(&flow);
    eve::debug::rtFrameBegin();
    eve::debug::rtTarget("screen");
    eve::debug::rtBind("font", "main");
    eve::debug::rtDraw("print", "text");
    eve::debug::rtError("Graphics::drawText: font must not be null");
    eve::debug::rtFrameEnd();
    CHECK(flow.eventCount() >= 6u);
    RenderSliceResult slice = flow.sliceBackward({});
    const bool        sawPrint =
        pathHas(slice, RenderEventKind::Bind, "main") || pathHas(slice, RenderEventKind::Draw, "print");
    CHECK(sawPrint);
    eve::debug::setRenderTracer(nullptr);
}

TEST_CASE("devtools.renderflow.passScopeForwardsTheCallersName") {
    struct CapturingTracer : eve::debug::IRenderTracer {
        std::vector<const char*> begins;
        void                     passBegin(const char* name) override { begins.push_back(name); }
    } tracer;

    // The engine's own pass scopes pass literals; the profiler retains the pointer until
    // frameMark(), so the scope must forward the caller's storage instead of handing out
    // its own buffer. Address identity is the observable that can be asserted: the
    // dangling read it prevents is non-deterministic (the freed bytes usually still hold
    // the old text), so a string comparison would pass either way.
    static const char* const kPassName = "RenderSystem2D";
    eve::debug::setRenderTracer(&tracer);
    { eve::debug::RenderPassScope scope(kPassName); }
    eve::debug::setRenderTracer(nullptr);

    REQUIRE(tracer.begins.size() == 1);
    REQUIRE(tracer.begins[0] == kPassName);
}

TEST_CASE("devtools.renderflow.shadowThenMainPass") {
    RenderFlow flow;
    flow.frameBegin();
    flow.passBegin("RenderSystem3D");
    for (int c = 0; c < 3; ++c) {
        flow.passBegin("ShadowPass");
        flow.bind("mesh", "caster");
        flow.draw("drawMeshShadow", "cascade");
        flow.passEnd("ShadowPass");
    }
    flow.target("screen");
    flow.bind("mesh", "hero");
    flow.bind("texture", "heroAlbedo");
    flow.draw("drawMeshShader", "default");
    flow.error("validation: unbound descriptor");
    flow.passEnd("RenderSystem3D");
    flow.frameEnd();

    RenderSliceResult slice = flow.sliceBackward({});
    CHECK(pathHas(slice, RenderEventKind::Draw, "drawMeshShader"));
    CHECK(pathHas(slice, RenderEventKind::PassBegin, "RenderSystem3D"));
    const std::string report = flow.formatErrorReport("validation: unbound descriptor");
    CHECK(report.find("Active passes") != std::string::npos);
}

TEST_CASE("devtools.renderflow.simulateSpriteBatch") {
    RenderFlow flow;
    flow.setMaxEvents(128);
    flow.frameBegin();
    flow.passBegin("RenderSystem2D");
    flow.target("screen");
    for (int i = 0; i < 50; ++i) {
        flow.bind("texture", "sprite");
        flow.draw("drawTexturedRectShaderUV", "textured");
    }
    flow.bind("texture", "boss");
    flow.draw("drawTexturedRectLitUV", "lit2d");
    flow.error("lit2d normal missing");
    flow.passEnd("RenderSystem2D");
    flow.passBegin("Present");
    flow.passEnd("Present");
    flow.frameEnd();

    RenderSliceResult slice = flow.sliceBackward({});
    CHECK(pathHas(slice, RenderEventKind::Draw, "drawTexturedRectLitUV"));
    CHECK(pathHas(slice, RenderEventKind::Error, "error"));
}

TEST_CASE("devtools.renderflow.rebindSameNameUpdatesReaching") {
    RenderFlow flow;
    flow.passBegin("P");
    flow.bind("texture", "hero");  // v1
    flow.draw("draw", "hero");
    flow.bind("texture", "hero");  // v2 overwrite
    flow.draw("draw", "hero");
    flow.error("bad");
    // Last draw should prefer the second bind; both may appear via control chain,
    // but data deps for the last draw should include a Bind of hero.
    RenderSliceResult slice = flow.sliceBackward({});
    CHECK(pathHas(slice, RenderEventKind::Bind, "hero"));
}

TEST_CASE("devtools.renderflow.eventLookupContiguous") {
    RenderFlow flow;
    flow.setMaxEvents(16);
    for (int i = 0; i < 20; ++i) flow.draw("d", "x");
    const uint32_t lo = flow.events().front().id;
    const uint32_t hi = flow.events().back().id;
    for (uint32_t id = lo; id <= hi; ++id) {
        REQUIRE(flow.event(id) != nullptr);
        CHECK_EQ(flow.event(id)->id, id);
    }
    CHECK(flow.event(lo - 1) == nullptr);
    CHECK(flow.event(hi + 1) == nullptr);
}
}  // namespace test_renderflow
#endif
#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_scene_component_scheduling)
#include "PrivateTestScope.h"
#include <stdexcept>
#include "ScriptTest.h"
#include "scene/SceneComponent.h"
#include "scene/TransformSystem.h"
#include "zeroerr/unittest.h"
// scene_component_scheduling.cpp
namespace test_scene_component_scheduling {
using namespace eve;


namespace {
class Tree final : public eve::scene::SceneComponent {
public:
    int                  builds          = 0;
    bool                 editDuringBuild = false;
    bool                 fail            = false;
    eve::scene::NodeDesc build() override {
        ++builds;
        if (editDuringBuild) {
            editDuringBuild = false;
            markDirty();
            eve::scene::detail::flushPendingComponents();
        }
        if (fail) throw std::runtime_error("injected scene failure");
        eve::scene::NodeDesc root;
        root.id = "root";
        return root;
    }
};
}  // namespace
TEST_CASE("scene.componentScheduling.automaticAndDestructionSafe") {
    Tree tree;
    tree.mountAs("scheduled-scene");
    eve::scene::TransformSystem::updateAll();
    CHECK_EQ(tree.builds, 1);
    tree.editDuringBuild = true;
    for (int i = 0; i < 20; ++i) tree.markDirty();
    eve::scene::TransformSystem::updateAll();
    CHECK_EQ(tree.builds, 2);
    CHECK(tree.isDirty());
    eve::scene::TransformSystem::updateAll();
    CHECK_EQ(tree.builds, 3);
    CHECK(!tree.isDirty());
    {
        Tree destroyed;
        destroyed.mountAs("destroyed-scene");
        destroyed.markDirty();
    }
    eve::scene::TransformSystem::updateAll();
    tree.host()->release();
    tree.markDirty();
    eve::scene::TransformSystem::updateAll();
    CHECK(tree.host() == nullptr);
}
TEST_CASE("scene.componentScheduling.failedBatchRetainsRemainingWork") {
    Tree first, second;
    first.mountAs("failed-scene");
    second.mountAs("remaining-scene");
    eve::scene::TransformSystem::updateAll();
    first.fail = true;
    first.markDirty();
    second.markDirty();
    bool threw = false;
    try {
        eve::scene::TransformSystem::updateAll();
    } catch (const std::runtime_error&) {
        threw = true;
    }
    CHECK(threw);
    CHECK(first.isDirty());
    CHECK(second.isDirty());
    first.fail = false;
    eve::scene::TransformSystem::updateAll();
    CHECK(!first.isDirty());
    CHECK(!second.isDirty());
    CHECK_EQ(second.builds, 2);
}


UnitSciptTest(SceneScheduledScriptTest, R"SQ(
function verifyScheduledComponent() {
    ::scene <- eve.Scene()
    class Scheduled extends eve.SceneComponent {
        builds = 0
        fail = false
        edit = false
        function build() {
            builds += 1
            if (edit) { edit = false; markDirty(); eve_scene_flush_components() }
            if (fail) throw "injected script publication failure"
            this.scene().beginNode("script-node", "Scheduled")
            this.scene().end()
        }
    }
    local first = Scheduled(eve.Scene())
    local second = Scheduled(eve.Scene())
    first.mountAs("script-schedule-first")
    second.mountAs("script-schedule-second")
    eve_scene_flush_components()
    if (first.builds != 1 || second.builds != 1) return false
    first.edit = true
    for (local i = 0; i < 20; ++i) first.markDirty()
    eve_scene_flush_components()
    if (first.builds != 2 || !first.dirty) return false
    eve_scene_flush_components()
    if (first.builds != 3 || first.dirty) return false
    first.fail = true
    first.markDirty()
    second.markDirty()
    local threw = false
    try { eve_scene_flush_components() } catch (error) { threw = true }
    if (!threw || !first.dirty || !second.dirty) return false
    first.fail = false
    eve_scene_flush_components()
    return !first.dirty && !second.dirty && second.builds == 2
}
)SQ");
TEST_CASE_FIXTURE(SceneScheduledScriptTest, "Scene.componentScheduling.scriptCoalescingAndFailureRecovery") {
    CHECK(vm.callFunc(vm.findFunc("verifyScheduledComponent"), vm).toBool());
}
}  // namespace test_scene_component_scheduling
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_service_mocks)
#include "PrivateTestScope.h"
#include <cstring>
#include <map>
#include <string>
#include <vector>
#include "common/Capability.h"
#include "common/ServiceInterfaces.h"
#include "timer/Timer.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// service_mocks.cpp
namespace test_service_mocks {
namespace {

// ---------------------------------------------------------------------------
// Deterministic test doubles: no PhysFS, no sockets, no wall clock.
// ---------------------------------------------------------------------------

class MockFileSystem : public eve::service::IFileSystem {
public:
    bool                                        failReads  = false;
    bool                                        failWrites = false;
    std::map<std::string, std::vector<uint8_t>> files;

    bool readFile(const std::string& path, std::vector<uint8_t>& out) override {
        if (failReads) return false;
        auto it = files.find(path);
        if (it == files.end()) return false;
        out = it->second;
        return true;
    }

    bool writeFile(const std::string& path, const void* data, size_t size) override {
        if (failWrites) return false;
        const auto* bytes = static_cast<const uint8_t*>(data);
        files[path]       = std::vector<uint8_t>(bytes, bytes + size);
        return true;
    }

    bool fileExists(const std::string& path) override { return files.find(path) != files.end(); }
};

class MockNetwork : public eve::service::INetwork {
public:
    bool        transportFails = false;
    int         cannedStatus   = 200;
    std::string cannedBody     = "{}";

    bool httpRequest(const std::string&, const std::string&, const std::string&, int, int& status,
                     std::string& responseBody) override {
        if (transportFails) return false;
        status       = cannedStatus;
        responseBody = cannedBody;
        return true;
    }
};

class MockTimer : public eve::service::ITimer {
public:
    double value = 0.0;
    double elapsedSeconds() override { return value; }
};

}  // namespace

TEST_CASE("service.providers.timerRegistersReal") {
    eve::cap::detail::clearAllRaw();
    auto* timer = eve::timer::Timer::create();
    REQUIRE(timer != nullptr);
    auto* svc = eve::cap::query<eve::service::ITimer>();
    REQUIRE(svc != nullptr);
    CHECK(svc->elapsedSeconds() >= 0.0);
    eve::cap::detail::clearAllRaw();
}
}  // namespace test_service_mocks
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_timer_cpp)
#include "PrivateTestScope.h"
#include <chrono>
#include <thread>
#include "timer/Timer.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// timer_cpp.cpp
namespace test_timer_cpp {
TEST_CASE("timer.getTime.monotonic") {
    auto* t  = eve::timer::Timer::create();
    float t0 = t->getTime();
    float t1 = t->getTime();
    CHECK(t1 >= t0);
}

TEST_CASE("timer.step.deltaNonNegative") {
    auto* t = eve::timer::Timer::create();
    t->step();
    CHECK(t->getDelta() >= 0.f);
    float d = t->step();
    CHECK(d >= 0.f);
    CHECK(t->getDelta() == d);
}

TEST_CASE("timer.step.deltaPositiveAfterSleep") {
    auto* t = eve::timer::Timer::create();
    t->step();
    std::this_thread::sleep_for(std::chrono::milliseconds(15));
    float delta = t->step();
    CHECK(delta > 0.f);
    CHECK(t->getDelta() == delta);
}

TEST_CASE("timer.simulationClockUsesInjectedFixedSteps") {
    auto*      t      = eve::timer::Timer::create();
    const auto before = t->getFrameIndex();
    auto       steps  = t->stepSimulation();
    REQUIRE(steps.ok());
    CHECK(t->getFrameIndex() > before);
    CHECK(t->getDeltaDuration().nanoseconds() >= 0);
    CHECK(t->monotonicNow() >= eve::MonotonicTimestamp::zero());
    CHECK(t->wallClockNow().unixNanoseconds() != 0);
}
}  // namespace test_timer_cpp
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_touch)
#include "PrivateTestScope.h"
#include <cstdint>
#include "common/Exception.h"
#include "touch/Touch.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// touch.cpp
namespace test_touch {
TEST_CASE("touch.getTouchesEmpty") {
    auto* t = eve::touch::Touch::create();
    REQUIRE(t != nullptr);
    CHECK(t->getTouches().empty());
}

TEST_CASE("touch.getTouchInvalidThrows") {
    auto* t = eve::touch::Touch::create();
    REQUIRE(t != nullptr);

    bool threw = false;
    try {
        (void)t->getTouch(999);
    } catch (const eve::Exception&) {
        threw = true;
    }
    CHECK(threw);
}
}  // namespace test_touch
#endif
