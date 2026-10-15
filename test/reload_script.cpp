#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "common/Capability.h"
#include "common/IStateProvider.h"
#include "common/Module.h"
#include "devtools/DevTool.hpp"
#include "devtools/ReloadSession.h"
#include "devtools/Snapshot.hpp"
#include "scripts.h"

#include <iostream>
#include <simplesquirrel/simplesquirrel.hpp>

namespace {

/**
 * @brief The production state-migration helpers, taken verbatim from the bootstrap.
 *
 * These used to be a hand-written mirror in this file, which meant the test could keep
 * passing while `load.nut`'s real helper diverged -- and it did: the production version
 * silently copied nothing for a while because `n[k] <- v` throws for a dynamic key on a
 * class instance (plan doc 9.21.1). Slicing the two functions out of `eve::load_content`
 * makes the test execute the same source scripts run, and a rename or a rewrite in
 * load.nut fails the reload cases instead of drifting unnoticed.
 *
 * The whole bootstrap cannot simply be run here: its top level reads config.nut,
 * instantiates modules, opens a window and drives frames.
 */
std::string migrationHelpersSource() {
    const std::string source(eve::load_content);
    const size_t      begin = source.find("function migrate_instance(");
    const size_t      remap = source.find("function remap_instances(");
    if (begin == std::string::npos || remap == std::string::npos || remap < begin) return {};
    const size_t end = source.find("\n}\n", remap);
    if (end == std::string::npos) return {};
    return source.substr(begin, end - begin + 3);
}

const char* kReloadScript = R"(
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
    local n = migrate_instance(old, HeroV2);
    return n.level == 5 && n.name == "alice" && n.title == "novice";
}

function migrateInstanceFromTable() {
    class HeroV2 {
        level = 1;
        name = "?";
        title = "novice";
    }
    local t = { level = 9, name = "bob" };
    local n = migrate_instance(t, HeroV2);
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
    remap_instances(arr, ItemV2);
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

function transientRootsAreExcludedFromCapture() {
    eve.dev.clearStateRoots();
    getroottable().scratchState <- { frame = 10 };
    eve.dev.markTransientStateRoot("scratchState");
    foreach (r in eve.dev.stateRoots()) {
        if (r == "scratchState") return false;
    }
    local transient = eve.dev.transientStateRoots();
    return transient.len() == 1 && transient[0] == "scratchState";
}

function abortRestoresStateRootPolicies() {
    eve.dev.clearStateRoots();
    getroottable().savedState <- { value = 1 };
    getroottable().scratchState <- { value = 2 };
    eve.dev.markStateRoot("savedState");
    eve.dev.markTransientStateRoot("scratchState");
    local err = eve.dev.beginStateReload();
    if (err != "") return false;

    eve.dev.clearStateRoots();
    eve.dev.markStateRoot("replacementState");
    err = eve.dev.abortStateReload();
    if (err != "") return false;

    local roots = eve.dev.stateRoots();
    local transient = eve.dev.transientStateRoots();
    return roots.len() == 1 && roots[0] == "savedState" &&
           transient.len() == 1 && transient[0] == "scratchState";
}

function commitKeepsNewStateRootPolicies() {
    eve.dev.clearStateRoots();
    getroottable().savedState <- { value = 1 };
    eve.dev.markStateRoot("savedState");
    local err = eve.dev.beginStateReload();
    if (err != "") return false;

    eve.dev.clearStateRoots();
    eve.dev.markTransientStateRoot("replacementState");
    err = eve.dev.commitStateReload();
    if (err != "") return false;

    local transient = eve.dev.transientStateRoots();
    return transient.len() == 1 && transient[0] == "replacementState";
}
)";

/** ScriptTest variant that also exposes `eve.dev` (DevTool script API). */
class ReloadScriptTest {
public:
    explicit ReloadScriptTest(const char* script) : vm(2048, ssq::Libs::ALL) {
        eve::ModuleManager::expose(vm);
        eve::dev::DevTool::instance().attach(vm);
        eve::dev::DevTool::instance().exposeScriptApi(vm);
        // The production migration helpers first, then the cases that call them.
        const std::string helpersSource = migrationHelpersSource();
        ssq::Script       helpers       = vm.compileSource(helpersSource.c_str());
        vm.run(helpers);
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
    REQUIRE(vm.callFunc(vm.findFunc("reloadProtocolKeepsOldValuesAndNewFields"), vm).toBool());
}

TEST_CASE_FIXTURE(ReloadScriptTestFixture, "reloadScript.protocolRestoresMissingRoot") {
    REQUIRE(vm.callFunc(vm.findFunc("reloadProtocolRestoresMissingRoot"), vm).toBool());
}

TEST_CASE_FIXTURE(ReloadScriptTestFixture, "reloadScript.abortRollsBack") {
    REQUIRE(vm.callFunc(vm.findFunc("reloadAbortRollsBack"), vm).toBool());
}

TEST_CASE_FIXTURE(ReloadScriptTestFixture, "reloadScript.migrateInstanceCopiesFieldsAndKeepsDefaults") {
    REQUIRE(vm.callFunc(vm.findFunc("migrateInstanceCopiesFieldsAndKeepsDefaults"), vm).toBool());
}

TEST_CASE_FIXTURE(ReloadScriptTestFixture, "reloadScript.migrateInstanceFromTable") {
    REQUIRE(vm.callFunc(vm.findFunc("migrateInstanceFromTable"), vm).toBool());
}

TEST_CASE_FIXTURE(ReloadScriptTestFixture, "reloadScript.remapInstancesContainer") {
    REQUIRE(vm.callFunc(vm.findFunc("remapInstancesContainer"), vm).toBool());
}

TEST_CASE_FIXTURE(ReloadScriptTestFixture, "reloadScript.stateRootsReportsMarkedAndHeuristic") {
    REQUIRE(vm.callFunc(vm.findFunc("stateRootsReportsMarkedAndHeuristic"), vm).toBool());
}

TEST_CASE_FIXTURE(ReloadScriptTestFixture, "reloadScript.transientRootsAreExcludedFromCapture") {
    REQUIRE(vm.callFunc(vm.findFunc("transientRootsAreExcludedFromCapture"), vm).toBool());
}

TEST_CASE_FIXTURE(ReloadScriptTestFixture, "reloadScript.abortRestoresStateRootPolicies") {
    REQUIRE(vm.callFunc(vm.findFunc("abortRestoresStateRootPolicies"), vm).toBool());
}

TEST_CASE_FIXTURE(ReloadScriptTestFixture, "reloadScript.commitKeepsNewStateRootPolicies") {
    REQUIRE(vm.callFunc(vm.findFunc("commitKeepsNewStateRootPolicies"), vm).toBool());
}

TEST_CASE("reloadScript.migrationHelpersComeFromTheBootstrap") {
    // The migration cases above must exercise load.nut's helpers, not a copy of them:
    // a mirror let them pass while the real helper silently copied nothing (plan doc
    // 9.21.1). This pins the wiring, and a rename in load.nut fails here instead of
    // quietly turning the three migration cases into no-ops.
    const std::string source = migrationHelpersSource();
    REQUIRE(!source.empty());
    REQUIRE(source.rfind("function migrate_instance(", 0) == 0);
    REQUIRE(source.find("function remap_instances(") != std::string::npos);
}

TEST_CASE("reloadScript.embeddedLoadScriptCompiles") {
    ssq::VM vm(2048, ssq::Libs::ALL);
    bool    compiled = false;
    try {
        vm.compileSource(eve::load_content);
        compiled = true;
    } catch (const std::exception& error) {
        // A broken bootstrap has to say what broke; swallowing the text would leave a
        // bare "compiled == false" with no way to tell which line failed.
        std::cerr << "[reloadScript] embedded load_content does not compile: " << error.what() << std::endl;
    }
    REQUIRE(compiled);
}

namespace {

class FailFirstRestoreProvider : public eve::caps::IStateProvider {
public:
    const char* stateKind() const override { return "reload-failure-test"; }

    eve::Result<eve::Value> capture() override { return eve::Result<eve::Value>::success(eve::Value::integer(42)); }

    eve::Result<void> restore(const eve::Value&) override {
        ++restoreCount;
        if (restoreCount == 1)
            return eve::Result<void>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::Conflict, "intentional commit failure"));
        return eve::Result<void>::success();
    }

    eve::Result<void> resetToDefaults() override { return eve::Result<void>::success(); }

    int restoreCount = 0;
};

class ResetOnReloadProvider : public eve::caps::IStateProvider {
public:
    const char* stateKind() const override { return "reload-reset-test"; }

    eve::caps::StateReloadPolicy reloadPolicy() const override {
        return eve::caps::StateReloadPolicy::Reset;
    }

    eve::Result<eve::Value> capture() override {
        ++captureCount;
        return eve::Result<eve::Value>::success(eve::Value::null());
    }

    eve::Result<void> restore(const eve::Value&) override {
        ++restoreCount;
        return eve::Result<void>::success();
    }

    eve::Result<void> resetToDefaults() override {
        ++resetCount;
        return eve::Result<void>::success();
    }

    int captureCount = 0;
    int restoreCount = 0;
    int resetCount   = 0;
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
    REQUIRE(!session.commit(vm.getHandle(), &err));
    REQUIRE_EQ(provider.restoreCount, 1);

    err.clear();
    REQUIRE(session.abort(vm.getHandle(), &err));
    REQUIRE(err.empty());
    REQUIRE_EQ(provider.restoreCount, 2);

    eve::cap::removeListener<eve::caps::IStateProvider>(&provider);
}

TEST_CASE("reloadScript.nativeResetPolicySkipsCaptureAndResets") {
    ssq::VM vm(2048, ssq::Libs::ALL);
    eve::dev::Snapshot::instance().clearRoots();

    ResetOnReloadProvider provider;
    eve::cap::addListener<eve::caps::IStateProvider>(&provider);

    auto&       session = eve::dev::ReloadSession::instance();
    std::string err;
    REQUIRE(session.begin(vm.getHandle(), &err));
    REQUIRE(session.commit(vm.getHandle(), &err));
    REQUIRE_EQ(provider.captureCount, 0);
    REQUIRE_EQ(provider.restoreCount, 0);
    REQUIRE_EQ(provider.resetCount, 1);

    eve::cap::removeListener<eve::caps::IStateProvider>(&provider);
}

}  // namespace
