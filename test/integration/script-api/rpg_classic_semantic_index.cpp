#include "PathBesideSource.h"
#include <filesystem>
#include <fstream>
#include <simplesquirrel/vm.hpp>
#include <sstream>
#include "common/Module.h"
#include "scene/Scene.h"
#include "scene/SceneHost.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

namespace {
std::string source(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("Missing RPG project input");
    std::ostringstream out;
    out << input.rdbuf();
    return out.str();
}
}  // namespace
TEST_CASE("rpg.classic.semanticQuestEventSceneRuntime") {
    const auto project = std::filesystem::path(eve_test_path::sourceBesideTestDir(__FILE__)).parent_path().parent_path() / "examples/rpg-classic";
    ssq::VM    vm(2048, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(source(project / "quest_scene.nut").c_str()));
    vm.run(vm.compileSource(source(project / "tests/quest_scene_probe.nut").c_str()));
    vm.set("questDefinitions", source(project / "data/quests.json"));
    vm.run(vm.compileSource(R"SQ(
        probePassed <- runQuestSceneProbe(questDefinitions);
        probeEventType <- questSceneLog.at(0).getType();
        probeEventSubject <- questSceneLog.at(0).getSubject();
    )SQ"));
    CHECK(vm.get<bool>("probePassed"));
    CHECK_EQ(vm.get<std::string>("probeEventType"), std::string("quest.completed"));
    CHECK_EQ(vm.get<std::string>("probeEventSubject"), std::string("quest.slayer"));
    auto host = eve::scene::Scene::create()->findHost("rpg-classic.quests");
    REQUIRE(host.ok());
    auto marker = host.value()->findById("quest.slayer.marker");
    REQUIRE(marker.ok());
    CHECK(marker.value()->visible);
    // New-game projection must clear the badge without emitting a fake event.
    vm.run(vm.compileSource(R"SQ(
        synchronizeQuestScene(eve.RPG().newTracker());
        probeCleared <- !questSceneMarkerVisible() && questSceneLog.size() == 1;
    )SQ"));
    CHECK(vm.get<bool>("probeCleared"));
    CHECK(!marker.value()->visible);
}
