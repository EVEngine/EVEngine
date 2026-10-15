#include <stdexcept>
#include "scene/SceneComponent.h"
#include "scene/TransformSystem.h"
#include "zeroerr/unittest.h"
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
    } catch (const std::runtime_error &) {
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

#include "ScriptTest.h"

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
