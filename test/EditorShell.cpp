#include "ui/EditorShell.h"
#include "common/Runtime.h"
#include "ui/DatabasePanel.h"
#include "ui/Inspector.h"
#include "ui/UIHost.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

using namespace eve;
using namespace eve::ui;

namespace {

eve::OptionalRef<UINode> nodeById(UIHost& host, const std::string& id) { return host.findById(id); }

}  // namespace

TEST_CASE("editorShell.menuDocksAndSwitchesPanels") {
    Runtime runtime(512, ssq::Libs::ALL);
    runtime.initialize();
    runtime.runSource("class ShellHero { name = \"hero\" }", "shell.nut");

    Inspector inspector;
    inspector.refresh();
    CHECK(inspector.selectClass("ShellHero"));
    inspector.open();

    DatabasePanel database;
    database.open();
    CHECK(database.selectClass("ShellHero"));

    EditorShell shell;
    shell.open(inspector.host(), database.host(), {});
    auto resolvedMenu = UIHost::resolve(shell.host());
    REQUIRE(resolvedMenu.has_value());
    UIHost& menu = resolvedMenu->get();
    REQUIRE(nodeById(menu, "menu_inspector").has_value());
    REQUIRE(nodeById(menu, "menu_database").has_value());
    REQUIRE(nodeById(menu, "menu_scene").has_value());
    REQUIRE(nodeById(menu, "menu_close").has_value());

    auto resolvedInspector = UIHost::resolve(inspector.host());
    auto resolvedDatabase  = UIHost::resolve(database.host());
    REQUIRE(resolvedInspector.has_value());
    REQUIRE(resolvedDatabase.has_value());
    UIHost& inspectorHost = resolvedInspector->get();
    UIHost& databaseHost  = resolvedDatabase->get();
    CHECK(inspectorHost.meta()->visible);
    CHECK(databaseHost.meta()->visible);
    CHECK(!inspectorHost.meta()->lockPos);
    CHECK(!inspectorHost.meta()->lockSize);
    CHECK(inspectorHost.meta()->percentW == 0.3f);
    auto toolbar = nodeById(menu, "editor_toolbar");
    REQUIRE(toolbar.has_value());
    CHECK(int(toolbar->get().type) == int(NodeType::Toolbar));

    auto tree           = menu.tree();
    auto databaseButton = nodeById(menu, "menu_database");
    REQUIRE(databaseButton.has_value());
    REQUIRE_GE(databaseButton->get().handlerClick, 1u);
    tree->clickHandlers[size_t(databaseButton->get().handlerClick - 1)]();
    CHECK(!databaseHost.meta()->visible);
    CHECK(inspectorHost.meta()->visible);
    tree->clickHandlers[size_t(databaseButton->get().handlerClick - 1)]();
    CHECK(databaseHost.meta()->visible);

    CHECK(shell.selectPanel("inspector"));
    CHECK(inspectorHost.meta()->visible);
    CHECK(!databaseHost.meta()->visible);

    auto inspectorButton = nodeById(menu, "menu_inspector");
    REQUIRE(inspectorButton.has_value());
    REQUIRE_GE(inspectorButton->get().handlerClick, 1u);
    tree->clickHandlers[size_t(inspectorButton->get().handlerClick - 1)]();
    CHECK(!inspectorHost.meta()->visible);

    auto closeButton = nodeById(menu, "menu_close");
    REQUIRE(closeButton.has_value());
    REQUIRE_GE(closeButton->get().handlerClick, 1u);
    tree->clickHandlers[size_t(closeButton->get().handlerClick - 1)]();
    CHECK(!inspectorHost.meta()->visible);
    CHECK(!databaseHost.meta()->visible);

    shell.close();
    CHECK(!shell.isOpen());
    CHECK(!inspectorHost.meta()->visible);
    CHECK(!databaseHost.meta()->visible);
}
