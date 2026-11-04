// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "default": "script-binding",
  "module": "devtools",
  "cases": {
    "platform-file": [
      "devtools.snapshot.reloadMergesDefaultsWithoutBreakingGraph",
      "devtools.snapshot.reloadDefaultAliasesToCapturedArrayChildren"
    ]
  }
}
*/
// clang-format on
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "common/Capability.h"
#include "common/IStateProvider.h"
#include "devtools/ReloadSession.h"
#include "devtools/Snapshot.hpp"

#include <simplesquirrel/simplesquirrel.hpp>
#include <stdexcept>

namespace {

struct SnapshotFixture {
    ssq::VM             vm{2048, ssq::Libs::ALL};
    eve::dev::Snapshot& snapshot = eve::dev::Snapshot::instance();

    SnapshotFixture() {
        snapshot.clearRoots();
        snapshot.markRoot("gameState");
    }
    ~SnapshotFixture() { snapshot.clearRoots(); }

    void run(const char* source) { vm.run(vm.compileSource(source)); }
    bool unchanged() { return vm.callFunc(vm.findFunc("unchanged"), vm).toBool(); }

    void roundTrips() {
        std::string     error;
        eve::StateValue out;
        const auto      top = sq_gettop(vm.getHandle());
        REQUIRE(snapshot.captureState(vm.getHandle(), out, &error));
        REQUIRE_EQ(out.find("version")->asInt(), int64_t(3));
        REQUIRE(out.find("objects")->isArray());
        REQUIRE_EQ(sq_gettop(vm.getHandle()), top);
        REQUIRE(unchanged());
        run("gameState = null; if (\"otherState\" in getroottable()) otherState = null;");
        REQUIRE(snapshot.restoreState(vm.getHandle(), out, &error));
        REQUIRE_EQ(sq_gettop(vm.getHandle()), top);
        REQUIRE(unchanged());
        const auto json = snapshot.capture(vm.getHandle(), &error);
        REQUIRE(!json.empty());
        run("gameState = null; if (\"otherState\" in getroottable()) otherState = null;");
        REQUIRE(snapshot.restore(vm.getHandle(), json, &error));
        REQUIRE_EQ(sq_gettop(vm.getHandle()), top);
        REQUIRE(unchanged());
    }
};

class RestoreFailureProvider : public eve::caps::IStateProvider {
public:
    explicit RestoreFailureProvider(const char* kind) : kind_(kind) {
        eve::cap::addListener<eve::caps::IStateProvider>(this);
    }
    ~RestoreFailureProvider() { eve::cap::removeListener<eve::caps::IStateProvider>(this); }
    const char*             stateKind() const override { return kind_; }
    eve::Result<eve::Value> capture() override { return eve::Result<eve::Value>::success(eve::Value::integer(value)); }
    eve::Result<void>       restore(const eve::Value& state) override {
        value = state.asInt();
        if (value == 99) {
            if (throws) throw std::runtime_error("injected restore exception");
            return eve::Result<void>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::Conflict, "injected restore failure"));
        }
        return eve::Result<void>::success();
    }
    eve::Result<void> resetToDefaults() override {
        value = 0;
        return eve::Result<void>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::Conflict, "injected reset failure"));
    }
    eve::caps::StateReloadPolicy reloadPolicy() const override {
        return reset ? eve::caps::StateReloadPolicy::Reset : eve::caps::StateReloadPolicy::Preserve;
    }
    int64_t value  = 7;
    bool    throws = false;
    bool    reset  = false;

private:
    const char* kind_;  // Borrowed static test literal; valid for this provider's lifetime.
};

}  // namespace

TEST_CASE_FIXTURE(SnapshotFixture, "devtools.snapshot.preservesSharedTable") {
    run(R"(
        gameState <- { a = { value = 1 } };
        gameState.b <- gameState.a;
        function unchanged() { return gameState.a == gameState.b && gameState.b.value == 1; }
    )");
    roundTrips();
    run("gameState.a.value = 9; assert(gameState.b.value == 9);");
}

TEST_CASE_FIXTURE(SnapshotFixture, "devtools.snapshot.preservesSharedArray") {
    run(R"(
        gameState <- [[1, 2]];
        gameState.append(gameState[0]);
        function unchanged() { return gameState[0] == gameState[1] && gameState[1][1] == 2; }
    )");
    roundTrips();
    run("gameState[0].append(3); assert(gameState[1].len() == 3);");
}

TEST_CASE_FIXTURE(SnapshotFixture, "devtools.snapshot.preservesCrossRootAliases") {
    run(R"(
        gameState <- { value = 1 };
        otherState <- gameState;
        function unchanged() { return otherState == gameState && gameState.value == 1; }
    )");
    snapshot.markRoot("otherState");
    roundTrips();
}

TEST_CASE_FIXTURE(SnapshotFixture, "devtools.snapshot.preservesTableCycle") {
    run(R"(
        gameState <- {};
        gameState.self <- gameState;
        function unchanged() { return gameState.self == gameState; }
    )");
    roundTrips();
}

TEST_CASE_FIXTURE(SnapshotFixture, "devtools.snapshot.preservesArrayTableCycle") {
    run(R"(
        gameState <- [];
        gameState.append({ back = gameState });
        function unchanged() { return gameState[0].back == gameState; }
    )");
    roundTrips();
}

TEST_CASE_FIXTURE(SnapshotFixture, "devtools.snapshot.restoreDepthFailureIsAtomic") {
    run(R"(
        gameState <- { value = 7 };
        original <- gameState;
        function unchanged() {
            return gameState == original && gameState.value == 7 && !("lateRoot" in getroottable());
        }
    )");
    auto nested = eve::StateValue::integer(0);
    for (int i = 0; i < 34; ++i) {
        auto parent = eve::StateValue::array();
        parent.pushBack(std::move(nested));
        nested = std::move(parent);
    }
    auto roots = eve::StateValue::object();
    roots.set("gameState", eve::StateValue::integer(99));
    roots.set("lateRoot", std::move(nested));
    auto state = eve::StateValue::object();
    state.set("roots", std::move(roots));
    const auto  top = sq_gettop(vm.getHandle());
    std::string error;
    REQUIRE(!snapshot.restoreState(vm.getHandle(), state, &error));
    REQUIRE(error.find("lateRoot") != std::string::npos);
    REQUIRE_EQ(sq_gettop(vm.getHandle()), top);
    REQUIRE(unchanged());
    REQUIRE(!snapshot.restore(vm.getHandle(), R"({"roots":{"gameState":99,"lateRoot":[})", &error));
    REQUIRE(unchanged());
}

TEST_CASE_FIXTURE(SnapshotFixture, "devtools.snapshot.nativeFailureRollsBackAndKeepsRootIdentity") {
    run(R"(
        gameState <- { value = 7 };
        original <- gameState;
        function unchanged() { return gameState == original && gameState.value == 7; }
    )");
    RestoreFailureProvider first("first"), second("second");
    const auto             top = sq_gettop(vm.getHandle());
    for (bool throws : {false, true}) {
        second.throws = throws;
        std::string error;
        REQUIRE(!snapshot.restore(vm.getHandle(), R"({"roots":{"gameState":99},"native":{"first":42,"second":99}})",
                                  &error));
        REQUIRE(error.find("injected restore") != std::string::npos);
        REQUIRE_EQ(first.value, int64_t(7));
        REQUIRE_EQ(second.value, int64_t(7));
        REQUIRE_EQ(sq_gettop(vm.getHandle()), top);
        REQUIRE(unchanged());
    }
}

TEST_CASE_FIXTURE(SnapshotFixture, "devtools.snapshot.independentEqualContainersRoundTrip") {
    run(R"(
        gameState <- { a = { value = [1] }, b = { value = [1] }, empty = null };
        function unchanged() {
            return gameState.a != gameState.b && gameState.a.value != gameState.b.value &&
                   gameState.a.value[0] == 1 && gameState.b.value[0] == 1 && gameState.empty == null;
        }
    )");
    std::string error;
    const auto  json = snapshot.capture(vm.getHandle(), &error);
    REQUIRE(!json.empty());
    run("gameState = null;");
    REQUIRE(snapshot.restore(vm.getHandle(), json, &error));
    REQUIRE(unchanged());
}

TEST_CASE_FIXTURE(SnapshotFixture, "devtools.snapshot.preservesArraySelfCycle") {
    run(R"(
        gameState <- [];
        gameState.append(gameState);
        function unchanged() { return gameState[0] == gameState; }
    )");
    roundTrips();
}

TEST_CASE_FIXTURE(SnapshotFixture, "devtools.snapshot.captureDepthFailureKeepsOutputAndStack") {
    run(R"(
        gameState <- [];
        local cursor = gameState;
        for (local i = 0; i < 34; ++i) {
            local child = [];
            cursor.append(child);
            cursor = child;
        }
        function unchanged() { return gameState.len() == 1 && gameState[0].len() == 1; }
    )");
    auto        out = eve::StateValue::string("sentinel");
    const auto  top = sq_gettop(vm.getHandle());
    std::string error;
    REQUIRE(!snapshot.captureState(vm.getHandle(), out, &error));
    REQUIRE_EQ(out.asString(), std::string("sentinel"));
    REQUIRE(error.find("depth limit exceeded at gameState[0]") != std::string::npos);
    REQUIRE_EQ(sq_gettop(vm.getHandle()), top);
    REQUIRE(unchanged());
}

TEST_CASE_FIXTURE(SnapshotFixture, "devtools.snapshot.invalidNativeIsAtomic") {
    run(R"(
        gameState <- { value = 7 };
        original <- gameState;
        function unchanged() { return gameState == original && gameState.value == 7; }
    )");
    std::string error;
    const auto  top = sq_gettop(vm.getHandle());
    REQUIRE(!snapshot.restore(vm.getHandle(), R"({"roots":{"gameState":99},"native":[]})", &error));
    REQUIRE(error.find("native must be an object") != std::string::npos);
    REQUIRE_EQ(sq_gettop(vm.getHandle()), top);
    REQUIRE(unchanged());
}

TEST_CASE_FIXTURE(SnapshotFixture, "devtools.snapshot.resetFailureRollsBack") {
    run(R"(
        gameState <- { value = 7 };
        original <- gameState;
        function unchanged() { return gameState == original && gameState.value == 7; }
    )");
    RestoreFailureProvider provider("reset");
    provider.reset = true;
    std::string error;
    REQUIRE(!snapshot.restore(vm.getHandle(), R"({"roots":{"gameState":99}})", &error));
    REQUIRE(error.find("injected reset failure") != std::string::npos);
    REQUIRE_EQ(provider.value, int64_t(7));
    REQUIRE(unchanged());
}

TEST_CASE_FIXTURE(SnapshotFixture, "devtools.snapshot.literalMetadataKeysRoundTrip") {
    run(R"(
        gameState <- { ["$ref"] = 999, type = "array", value = { ["$ref"] = "literal" } };
        function unchanged() {
            return gameState["$ref"] == 999 && gameState.type == "array" && gameState.value["$ref"] == "literal";
        }
    )");
    roundTrips();
}

TEST_CASE_FIXTURE(SnapshotFixture, "devtools.snapshot.invalidGraphLeavesAllStateUntouched") {
    run(R"(
        gameState <- { value = 7 };
        original <- gameState;
        function unchanged() {
            return gameState == original && gameState.value == 7 && !("lateRoot" in getroottable());
        }
    )");
    RestoreFailureProvider provider("first");
    const auto             top       = sq_gettop(vm.getHandle());
    const char*            invalid[] = {
        R"({"version":4,"roots":{"gameState":99}})",
        R"({"version":3,"roots":{"gameState":99}})",
        R"({"version":3,"roots":{"gameState":99,"lateRoot":{"$ref":1}},"objects":[]})",
        R"({"version":3,"roots":{"gameState":{"$ref":-1}},"objects":[]})",
        R"({"version":3,"roots":{"gameState":{"$ref":0.0}},"objects":[{"type":"table","value":{}}]})",
        R"({"version":3,"roots":{"gameState":{"$ref":0,"extra":true}},"objects":[{"type":"table","value":{}}]})",
        R"({"version":3,"roots":{"gameState":99},"objects":[{"type":"table","value":[]}]})",
        R"({"version":3,"roots":{"gameState":99},"objects":[{"type":"class","value":{}}]})",
        R"({"version":3,"roots":{"gameState":[]},"objects":[]})",
        R"({"version":3,"roots":{"gameState":99},"objects":[{"type":"array","value":[{"$ref":3}]}]})"};
    for (const char* json : invalid) {
        std::string error;
        REQUIRE(!snapshot.restore(vm.getHandle(), json, &error));
        REQUIRE(!error.empty());
        REQUIRE_EQ(sq_gettop(vm.getHandle()), top);
        REQUIRE_EQ(provider.value, int64_t(7));
        REQUIRE(unchanged());
    }
}

TEST_CASE_FIXTURE(SnapshotFixture, "devtools.snapshot.forwardReferencesAndMutualCycle") {
    std::string error;
    REQUIRE(snapshot.restore(vm.getHandle(), R"({
        "version":3,"roots":{"gameState":{"$ref":1}},
        "objects":[{"type":"array","value":[{"$ref":1}]},
                   {"type":"table","value":{"items":{"$ref":0}}}]
    })",
                             &error));
    run("assert(gameState.items[0] == gameState);");
}

TEST_CASE_FIXTURE(SnapshotFixture, "devtools.snapshot.legacyRefKeyRemainsUserData") {
    std::string error;
    REQUIRE(snapshot.restore(vm.getHandle(), R"({"version":2,"roots":{"gameState":{"$ref":1}}})", &error));
    run("assert(gameState[\"$ref\"] == 1);");
}

TEST_CASE_FIXTURE(SnapshotFixture, "devtools.snapshot.reloadMergesDefaultsWithoutBreakingGraph") {
    run(R"(
        gameState <- { a = { value = 7 }, items = [1] };
        gameState.b <- gameState.a;
        gameState.self <- gameState;
    )");
    auto&       session = eve::dev::ReloadSession::instance();
    std::string error;
    REQUIRE(session.begin(vm.getHandle(), &error));
    run(R"(
        gameState = { a = { value = 0, added = "new" }, items = [0] };
        gameState.b <- gameState.a;
        gameState.self <- gameState;
        gameState.newAlias <- gameState.a;
        gameState.newArrayAlias <- gameState.items;
        gameState.extra <- [];
        gameState.extra.append(gameState.extra);
    )");
    REQUIRE(session.commit(vm.getHandle(), &error));
    run(R"(
        assert(gameState.a == gameState.b && gameState.self == gameState);
        assert(gameState.a.value == 7 && gameState.a.added == "new");
        assert(gameState.newAlias == gameState.a);
        assert(gameState.items[0] == 1 && gameState.newArrayAlias == gameState.items);
        assert(gameState.extra[0] == gameState.extra);
        gameState.newAlias.value = 8;
        assert(gameState.b.value == 8);
    )");
}

TEST_CASE_FIXTURE(SnapshotFixture, "devtools.snapshot.nativeFailureWithCyclicCandidateKeepsLiveGraph") {
    run(R"(
        gameState <- { a = [7] };
        gameState.b <- gameState.a;
        gameState.self <- gameState;
        original <- gameState;
        function unchanged() {
            return gameState == original && gameState.self == gameState &&
                   gameState.a == gameState.b && gameState.a[0] == 7;
        }
    )");
    eve::StateValue captured;
    std::string     error;
    REQUIRE(snapshot.captureState(vm.getHandle(), captured, &error));
    auto native = eve::StateValue::object();
    native.set("first", eve::StateValue::integer(99));
    captured.set("native", std::move(native));
    RestoreFailureProvider provider("first");
    const auto             top = sq_gettop(vm.getHandle());
    REQUIRE(!snapshot.restoreState(vm.getHandle(), captured, &error));
    REQUIRE(unchanged());
    REQUIRE_EQ(provider.value, int64_t(7));
    REQUIRE_EQ(sq_gettop(vm.getHandle()), top);
}

TEST_CASE_FIXTURE(SnapshotFixture, "devtools.snapshot.nonStringKeysFailExplicitly") {
    run("gameState <- {}; gameState[1] <- gameState;");
    auto        out = eve::StateValue::string("sentinel");
    const auto  top = sq_gettop(vm.getHandle());
    std::string error;
    REQUIRE(!snapshot.captureState(vm.getHandle(), out, &error));
    REQUIRE(error.find("non-string table key at gameState") != std::string::npos);
    REQUIRE_EQ(out.asString(), std::string("sentinel"));
    REQUIRE_EQ(sq_gettop(vm.getHandle()), top);
}

TEST_CASE_FIXTURE(SnapshotFixture, "devtools.snapshot.reloadDefaultAliasesToCapturedArrayChildren") {
    run("gameState <- { items = [{ value = 7 }] };");
    auto&       session = eve::dev::ReloadSession::instance();
    std::string error;
    REQUIRE(session.begin(vm.getHandle(), &error));
    run(R"(
        gameState = { items = [{ value = 0, added = true }] };
        gameState.selected <- gameState.items[0];
    )");
    REQUIRE(session.commit(vm.getHandle(), &error));
    run(R"(
        assert(gameState.selected == gameState.items[0]);
        assert(gameState.selected.value == 7);
        assert(!("added" in gameState.selected));
    )");
}
