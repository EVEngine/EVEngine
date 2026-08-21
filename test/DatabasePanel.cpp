#include "common/Runtime.h"
#include "ui/DatabasePanel.h"
#include "ui/ObjectRegistry.h"
#include "ui/UIHost.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <string>

using namespace eve;
using namespace eve::ui;

namespace {

const char* kDatabaseScript = R"SQ(
class CharacterData {
    name = "Hero"
    hp = 100.0
    alive = true
    </ editor = "combo", options = "warrior,mage,rogue" />
    job = "warrior"
    skills = []
    function update(dt) { hp += dt }
}
)SQ";

UINode* nodeById(UIHost* host, const std::string& id) {
    return host ? host->findById(id) : nullptr;
}

}  // namespace

TEST_CASE("database.registryCreatesRegistersAndLists") {
    Runtime runtime(512, ssq::Libs::ALL);
    runtime.initialize();
    runtime.runSource(kDatabaseScript, "database.nut");

    ObjectRegistry& registry = ObjectRegistry::instance();
    registry.clearAll();

    const uint64_t first = registry.create("CharacterData");
    REQUIRE_NE(first, uint64_t(0));
    CHECK_EQ(registry.count("CharacterData"), size_t(1));

    ssq::Object hero = runtime.createInstance("CharacterData");
    const uint64_t second = registry.registerObject("CharacterData", hero);
    REQUIRE_NE(second, uint64_t(0));
    CHECK_EQ(registry.count("CharacterData"), size_t(2));

    const std::vector<ObjectEntry> entries = registry.entries("CharacterData");
    REQUIRE_EQ(entries.size(), size_t(2));
    CHECK_EQ(entries[0].id, first);
    CHECK_EQ(entries[1].id, second);
    CHECK(!entries[1].label.empty());

    CHECK(registry.unregister(first));
    CHECK_EQ(registry.count("CharacterData"), size_t(1));
    CHECK(!registry.unregister(first));  // already removed

    registry.clearAll();
    CHECK_EQ(registry.count("CharacterData"), size_t(0));
}

TEST_CASE("database.panelBuildsGridAndBindsCells") {
    Runtime runtime(512, ssq::Libs::ALL);
    runtime.initialize();
    runtime.runSource(kDatabaseScript, "database.nut");
    ObjectRegistry::instance().clearAll();

    DatabasePanel panel;
    panel.open();
    REQUIRE(panel.host() != nullptr);
    CHECK(panel.selectClass("CharacterData"));
    const uint64_t id = panel.createInstance();
    REQUIRE_NE(id, uint64_t(0));
    CHECK_EQ(panel.selectedClass(), std::string("CharacterData"));

    UIHost* host = panel.host();
    // Header cells for reflected members.
    REQUIRE(nodeById(host, "db_hdr_name") != nullptr);
    REQUIRE(nodeById(host, "db_hdr_hp") != nullptr);
    REQUIRE(nodeById(host, "db_hdr_alive") != nullptr);
    REQUIRE(nodeById(host, "db_hdr_job") != nullptr);
    // Row cells + delete button.
    const std::string row = "db_row_" + std::to_string(id);
    REQUIRE(nodeById(host, row) != nullptr);
    UINode* nameCell = nodeById(host, "cell_" + std::to_string(id) + "_name");
    REQUIRE(nameCell != nullptr);
    CHECK_EQ(nameCell->valueText, std::string("Hero"));
    REQUIRE(nodeById(host, "cell_" + std::to_string(id) + "_alive") != nullptr);
    REQUIRE(nodeById(host, "db_del_" + std::to_string(id)) != nullptr);

    // View → model: typing into a cell writes back to the live instance.
    const ObjectEntry* entry = ObjectRegistry::instance().entry(id);
    REQUIRE(entry != nullptr);
    auto tree = host->tree();
    REQUIRE_GE(nameCell->handlerText, 1u);
    tree->textHandlers[size_t(nameCell->handlerText - 1)]("Axe");
    CHECK_EQ(runtime.readProperty(entry->object, "name").asString(),
             std::string("Axe"));

    UINode* aliveCell = nodeById(host, "cell_" + std::to_string(id) + "_alive");
    REQUIRE_GE(aliveCell->handlerToggle, 1u);
    tree->toggleHandlers[size_t(aliveCell->handlerToggle - 1)](false);
    CHECK(!runtime.readProperty(entry->object, "alive").asBool());

    // Model → view: external change is pulled into the grid by sync().
    ReflectedValue value;
    value.kind = ReflectedValueKind::Float;
    value.floating = 250.0;
    CHECK(runtime.writeProperty(entry->object, "hp", value));
    panel.sync();
    CHECK_EQ(nodeById(host, "cell_" + std::to_string(id) + "_hp")->valueText,
             std::string("250"));

    // Deleting the row removes it from the grid.
    CHECK(panel.unregister(id));
    CHECK(nodeById(host, row) == nullptr);

    panel.close();
}
