#include "common/Capability.h"
#include "common/Runtime.h"
#include "common/SceneQuery.h"
#include "ui/ScenePanel.h"
#include "ui/UIHost.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <string>
#include <vector>

using namespace eve;
using namespace eve::ui;

namespace {

class FakeScene final : public eve::ISceneQuery {
public:
    std::string activeHost() const override { return "test_host"; }
    int hostCount() const override { return 1; }
    std::string hostNameAt(int) const override { return "test_host"; }
    int nodeCount() const override { return 3; }
    std::string rootId() const override { return "root"; }

    std::vector<eve::SceneNodeInfo> nodes(int) const override {
        eve::SceneNodeInfo root;
        root.id = "root";
        root.name = "Root";
        root.path = "/";
        root.children = {"hero"};
        eve::SceneNodeInfo hero;
        hero.id = "hero";
        hero.name = "Hero";
        hero.path = "/hero";
        hero.parent = "root";
        hero.x = 1.f;
        hero.y = 2.f;
        hero.z = 3.f;
        return {root, hero};
    }
    std::vector<eve::SceneNodeInfo> nodesOf(const std::string&, int) const override {
        return nodes(0);
    }

    bool getNode(const std::string& id, eve::SceneNodeInfo* out) const override {
        const std::vector<eve::SceneNodeInfo> all = nodes(0);
        for (const eve::SceneNodeInfo& n : all) {
            if (n.id == id) {
                if (out) *out = n;
                return true;
            }
        }
        return false;
    }
    bool getNodeIn(const std::string&, const std::string& id,
                   eve::SceneNodeInfo* out) const override {
        return getNode(id, out);
    }

    bool setNodeTransform(const std::string& id, float x, float y, float z) override {
        if (id != "hero") return false;
        setX = x;
        setY = y;
        setZ = z;
        return true;
    }
    bool setNodeVisible(const std::string& id, bool visible) override {
        if (id != "hero") return false;
        setVisible = visible;
        return true;
    }
    void syncTransforms() override {}

    float setX = 0.f, setY = 0.f, setZ = 0.f;
    bool setVisible = true;
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
