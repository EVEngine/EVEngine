#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "common/Capability.h"
#include "common/IStateProvider.h"
#include "common/Module.h"
#include "devtools/DevTool.hpp"
#include "devtools/ReloadSession.h"
#include "devtools/Snapshot.hpp"
#include "scripts.h"

#include <simplesquirrel/simplesquirrel.hpp>

namespace {

const char* kReloadScript = R"(
// Mirror of the load.nut migrate_instance helper (tables fully; class
// instances via their declared members).
function migrate_inst(old, NewClass) {
    local n = NewClass();
    if (old == null) return n;
    if (typeof old == "table") {
        foreach (k, v in old) {
            try { n[k] <- v; } catch (e) {}
        }
        return n;
    }
    if (typeof old != "instance") return n;
    foreach (k, v in getclass(old)) {
        if (typeof v == "function") continue;
        try { n[k] <- old[k]; } catch (e) {}
    }
    return n;
}

function remap_inst(container, NewClass) {
    if (typeof container == "array") {
        for (local i = 0; i < container.len(); ++i) {
            container[i] = migrate_inst(container[i], NewClass);
        }
    } else if (typeof container == "table") {
        local keys = [];
        foreach (k, v in container) keys.append(k);
        foreach (k in keys) {
            container[k] = migrate_inst(container[k], NewClass);
        }
    }
    return container;
}

function reloadProtocolKeepsOldValuesAndNewFields() {
    eve.dev.clearStateRoots();
    getroottable().gameState <- { level = 3, hero = "alice" };
    eve.dev.markStateRoot("gameState");
    local err = eve.dev.beginStateReload();
    if (err != "") return false;
    // Simulate a script reload: the definition is replaced by new defaults.
    getroottable().gameState <- { level = 0, hero = "nobody", quest = "intro" };
    err = eve.dev.commitStateReload();
    if (err != "") return false;
    return gameState.level == 3 && gameState.hero == "alice" &&
           gameState.quest == "intro";
}

function reloadProtocolRestoresMissingRoot() {
    eve.dev.clearStateRoots();
    getroottable().gameState <- { level = 7 };
    eve.dev.markStateRoot("gameState");
    local err = eve.dev.beginStateReload();
    if (err != "") return false;
    // The new script no longer defines the root.
    delete getroottable().gameState;
    err = eve.dev.commitStateReload();
    if (err != "") return false;
    return ("gameState" in getroottable()) && gameState.level == 7;
}

function reloadAbortRollsBack() {
    eve.dev.clearStateRoots();
    getroottable().gameState <- { hp = 50 };
    eve.dev.markStateRoot("gameState");
    local err = eve.dev.beginStateReload();
    if (err != "") return false;
    getroottable().gameState <- { hp = 1 };
    err = eve.dev.abortStateReload();
    if (err != "") return false;
    return gameState.hp == 50;
}

function migrateInstanceCopiesFieldsAndKeepsDefaults() {
    class HeroV1 {
        level = 1;
        name = "?";
    }
    class HeroV2 {
        level = 1;
        name = "?";
        title = "novice";
    }
    local old = HeroV1();
    old.level = 5;
    old.name = "alice";
    local n = migrate_inst(old, HeroV2);
    return n.level == 5 && n.name == "alice" && n.title == "novice";
}

function migrateInstanceFromTable() {
    class HeroV2 {
        level = 1;
        name = "?";
        title = "novice";
    }
    local t = { level = 9, name = "bob" };
    local n = migrate_inst(t, HeroV2);
    return n.level == 9 && n.name == "bob" && n.title == "novice";
}

function remapInstancesContainer() {
    class ItemV1 {
        id = "?";
    }
    class ItemV2 {
        id = "?";
        tag = "new";
    }
    local arr = [ItemV1(), ItemV1()];
    arr[0].id = "a";
    arr[1].id = "b";
    remap_inst(arr, ItemV2);
    return arr[0].id == "a" && arr[0].tag == "new" && arr[1].id == "b" &&
           arr[1].tag == "new";
}

function stateRootsReportsMarkedAndHeuristic() {
    eve.dev.clearStateRoots();
    eve.dev.markStateRoot("customState");
    local roots = eve.dev.stateRoots();
    if (roots.len() != 1 || roots[0] != "customState") return false;

    eve.dev.clearStateRoots();
    getroottable().myGame <- { x = 1 };
    local found = false;
    foreach (r in eve.dev.stateRoots()) {
        if (r == "myGame") found = true;
    }
    return found;
}
)";

/** ScriptTest variant that also exposes `eve.dev` (DevTool script API). */
class ReloadScriptTest {
public:
    explicit ReloadScriptTest(const char* script) : vm(2048, ssq::Libs::ALL) {
        eve::ModuleManager::expose(vm);
        eve::dev::DevTool::instance().attach(vm);
        eve::dev::DevTool::instance().exposeScriptApi(vm);
        ssq::Script s = vm.compileSource(script);
        vm.run(s);
    }

    ~ReloadScriptTest() { eve::dev::DevTool::instance().detach(); }

    ssq::VM vm;
};

#define UnitReloadScriptTest(name, content)   \
    class name : public ReloadScriptTest {    \
    public:                                   \
        name() : ReloadScriptTest(content) {} \
    }

UnitReloadScriptTest(ReloadScriptTestFixture, kReloadScript);

TEST_CASE_FIXTURE(ReloadScriptTestFixture, "reloadScript.protocolKeepsOldValuesAndNewFields") {
    CHECK(vm.callFunc(vm.findFunc("reloadProtocolKeepsOldValuesAndNewFields"), vm).toBool());
}

TEST_CASE_FIXTURE(ReloadScriptTestFixture, "reloadScript.protocolRestoresMissingRoot") {
    CHECK(vm.callFunc(vm.findFunc("reloadProtocolRestoresMissingRoot"), vm).toBool());
}

TEST_CASE_FIXTURE(ReloadScriptTestFixture, "reloadScript.abortRollsBack") {
    CHECK(vm.callFunc(vm.findFunc("reloadAbortRollsBack"), vm).toBool());
}

TEST_CASE_FIXTURE(ReloadScriptTestFixture, "reloadScript.migrateInstanceCopiesFieldsAndKeepsDefaults") {
    CHECK(vm.callFunc(vm.findFunc("migrateInstanceCopiesFieldsAndKeepsDefaults"), vm).toBool());
}

TEST_CASE_FIXTURE(ReloadScriptTestFixture, "reloadScript.migrateInstanceFromTable") {
    CHECK(vm.callFunc(vm.findFunc("migrateInstanceFromTable"), vm).toBool());
}

TEST_CASE_FIXTURE(ReloadScriptTestFixture, "reloadScript.remapInstancesContainer") {
    CHECK(vm.callFunc(vm.findFunc("remapInstancesContainer"), vm).toBool());
}

TEST_CASE_FIXTURE(ReloadScriptTestFixture, "reloadScript.stateRootsReportsMarkedAndHeuristic") {
    CHECK(vm.callFunc(vm.findFunc("stateRootsReportsMarkedAndHeuristic"), vm).toBool());
}

TEST_CASE("reloadScript.embeddedLoadScriptCompiles") {
    ssq::VM vm(2048, ssq::Libs::ALL);
    bool    compiled = false;
    try {
        vm.compileSource(eve::load_content);
        compiled = true;
    } catch (...) {
    }
    CHECK(compiled);
}

namespace {

class FailFirstRestoreProvider : public eve::caps::IStateProvider {
public:
    const char* stateKind() const override { return "reload-failure-test"; }

    bool captureState(eve::StateValue& out) override {
        out = eve::StateValue::integer(42);
        return true;
    }

    bool restoreState(const eve::StateValue&, std::string* err) override {
        ++restoreCount;
        if (restoreCount == 1) {
            if (err) *err = "intentional commit failure";
            return false;
        }
        return true;
    }

    bool resetToDefaults() override { return true; }

    int restoreCount = 0;
};

}  // namespace

TEST_CASE("reloadScript.failedCommitCanStillAbort") {
    ssq::VM vm(2048, ssq::Libs::ALL);
    eve::dev::Snapshot::instance().clearRoots();

    FailFirstRestoreProvider provider;
    eve::cap::addListener<eve::caps::IStateProvider>(&provider);

    auto&       session = eve::dev::ReloadSession::instance();
    std::string err;
    REQUIRE(session.begin(vm.getHandle(), &err));
    CHECK(!session.commit(vm.getHandle(), &err));
    CHECK_EQ(provider.restoreCount, 1);

    err.clear();
    CHECK(session.abort(vm.getHandle(), &err));
    CHECK(err.empty());
    CHECK_EQ(provider.restoreCount, 2);

    eve::cap::removeListener<eve::caps::IStateProvider>(&provider);
}

}  // namespace
