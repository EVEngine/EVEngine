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

eve::OptionalRef<UINode> nodeById(UIHost& host, const std::string& id) { return host.findById(id); }

}  // namespace

TEST_CASE("database.registryCreatesRegistersAndLists") {
    Runtime runtime(512, ssq::Libs::ALL);
    runtime.initialize();
    runtime.runSource(kDatabaseScript, "database.nut");

    ObjectRegistry& registry = ObjectRegistry::instance();
    registry.clearAll();

    CHECK_EQ(static_cast<int>(registry.unregister(ObjectHandle::invalid()).code()),
             static_cast<int>(eve::StatusCode::Rejected));

    auto firstResult = registry.create("CharacterData");
    REQUIRE(firstResult.ok());
    const ObjectHandle first = firstResult.value();
    CHECK_EQ(registry.count("CharacterData"), size_t(1));

    ssq::Object hero = runtime.createInstance("CharacterData");
    auto        secondResult = registry.registerObject("CharacterData", hero);
    REQUIRE(secondResult.ok());
    const ObjectHandle second = secondResult.value();
    CHECK_EQ(registry.count("CharacterData"), size_t(2));

    const std::vector<ObjectEntry> entries = registry.entries("CharacterData");
    REQUIRE_EQ(entries.size(), size_t(2));
    CHECK_EQ(entries[0].handle, first);
    CHECK_EQ(entries[1].handle, second);
    CHECK(!entries[1].label.empty());

    CHECK(registry.unregister(first).ok());
    CHECK_EQ(registry.count("CharacterData"), size_t(1));
    CHECK(!registry.unregister(first).ok());  // already removed

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
    REQUIRE(UIHost::resolve(panel.host()).has_value());
    CHECK(panel.selectClass("CharacterData"));
    auto idResult = panel.createInstance();
    REQUIRE(idResult.ok());
    const ObjectHandle id = idResult.value();
    CHECK_EQ(panel.selectedClass(), std::string("CharacterData"));

    auto resolvedHost = UIHost::resolve(panel.host());
    REQUIRE(resolvedHost.has_value());
    UIHost& host = resolvedHost->get();
    // Header cells for reflected members.
    REQUIRE(nodeById(host, "db_hdr_name").has_value());
    REQUIRE(nodeById(host, "db_hdr_hp").has_value());
    REQUIRE(nodeById(host, "db_hdr_alive").has_value());
    REQUIRE(nodeById(host, "db_hdr_job").has_value());
    // Row cells + delete button.
    const std::string row = "db_row_" + std::to_string(id.packed());
    REQUIRE(nodeById(host, row).has_value());
    auto nameCell = nodeById(host, "cell_" + std::to_string(id.packed()) + "_name");
    REQUIRE(nameCell.has_value());
    CHECK_EQ(nameCell->get().valueText, std::string("Hero"));
    REQUIRE(nodeById(host, "cell_" + std::to_string(id.packed()) + "_alive").has_value());
    REQUIRE(nodeById(host, "db_del_" + std::to_string(id.packed())).has_value());

    // View → model: typing into a cell writes back to the live instance.
    const ObjectEntry* entry = ObjectRegistry::instance().entry(id);
    REQUIRE(entry != nullptr);
    auto tree = host.tree();
    REQUIRE_GE(nameCell->get().handlerText, 1u);
    tree->textHandlers[size_t(nameCell->get().handlerText - 1)]("Axe");
    CHECK_EQ(runtime.readProperty(entry->object, "name").asString(),
             std::string("Axe"));

    auto aliveCell = nodeById(host, "cell_" + std::to_string(id.packed()) + "_alive");
    REQUIRE(aliveCell.has_value());
    REQUIRE_GE(aliveCell->get().handlerToggle, 1u);
    tree->toggleHandlers[size_t(aliveCell->get().handlerToggle - 1)](false);
    CHECK(!runtime.readProperty(entry->object, "alive").asBool());

    // Model → view: external change is pulled into the grid by sync().
    ReflectedValue value;
    value.kind = ReflectedValueKind::Float;
    value.floating = 250.0;
    CHECK(runtime.writeProperty(entry->object, "hp", value));
    panel.sync();
    auto hpCell = nodeById(host, "cell_" + std::to_string(id.packed()) + "_hp");
    REQUIRE(hpCell.has_value());
    CHECK_EQ(hpCell->get().valueText, std::string("250"));

    // Deleting the row removes it from the grid.
    CHECK(panel.unregister(id).ok());
    CHECK(!nodeById(host, row).has_value());

    panel.close();
}

TEST_CASE("database.registryHandlesRejectStaleAndReuseSlots") {
    Runtime runtime(512, ssq::Libs::ALL);
    runtime.initialize();
    runtime.runSource(kDatabaseScript, "database_handles.nut");

    ObjectRegistry& registry = ObjectRegistry::instance();
    registry.clearAll();

    auto firstResult = registry.create("CharacterData");
    REQUIRE(firstResult.ok());
    const ObjectHandle first = firstResult.value();
    REQUIRE(registry.unregister(first).ok());
    CHECK(registry.isStale(first));
    CHECK(registry.entry(first) == nullptr);

    auto secondResult = registry.create("CharacterData");
    REQUIRE(secondResult.ok());
    const ObjectHandle second = secondResult.value();
    CHECK_EQ(second.index(), first.index());
    CHECK_EQ(second.generation(), first.generation() + 1u);
    CHECK(registry.entry(first) == nullptr);
    CHECK_EQ(static_cast<int>(registry.unregister(first).code()), static_cast<int>(eve::StatusCode::Rejected));
    CHECK_EQ(static_cast<int>(registry.unregister(second).code()), static_cast<int>(eve::StatusCode::Applied));
}
