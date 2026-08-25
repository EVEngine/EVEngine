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

UINode* nodeById(UIHost* host, const std::string& id) { return host ? host->findById(id) : nullptr; }

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
    shell.open(inspector.host(), database.host(), nullptr);
    UIHost* menu = shell.host();
    REQUIRE(menu != nullptr);
    REQUIRE(nodeById(menu, "menu_inspector") != nullptr);
    REQUIRE(nodeById(menu, "menu_database") != nullptr);
    REQUIRE(nodeById(menu, "menu_scene") != nullptr);
    REQUIRE(nodeById(menu, "menu_close") != nullptr);

    UIHost* inspectorHost = inspector.host();
    UIHost* databaseHost  = database.host();
    REQUIRE(inspectorHost != nullptr);
    REQUIRE(databaseHost != nullptr);
    CHECK(inspectorHost->meta()->visible);
    CHECK(databaseHost->meta()->visible);
    CHECK(!inspectorHost->meta()->lockPos);
    CHECK(!inspectorHost->meta()->lockSize);
    CHECK(inspectorHost->meta()->percentW == 0.3f);
    REQUIRE(nodeById(menu, "editor_toolbar") != nullptr);
    CHECK(int(nodeById(menu, "editor_toolbar")->type) == int(NodeType::Toolbar));

    auto    tree           = menu->tree();
    UINode* databaseButton = nodeById(menu, "menu_database");
    REQUIRE_GE(databaseButton->handlerClick, 1u);
    tree->clickHandlers[size_t(databaseButton->handlerClick - 1)]();
    CHECK(!databaseHost->meta()->visible);
    CHECK(inspectorHost->meta()->visible);
    tree->clickHandlers[size_t(databaseButton->handlerClick - 1)]();
    CHECK(databaseHost->meta()->visible);

    CHECK(shell.selectPanel("inspector"));
    CHECK(inspectorHost->meta()->visible);
    CHECK(!databaseHost->meta()->visible);

    UINode* inspectorButton = nodeById(menu, "menu_inspector");
    REQUIRE_GE(inspectorButton->handlerClick, 1u);
    tree->clickHandlers[size_t(inspectorButton->handlerClick - 1)]();
    CHECK(!inspectorHost->meta()->visible);

    UINode* closeButton = nodeById(menu, "menu_close");
    REQUIRE_GE(closeButton->handlerClick, 1u);
    tree->clickHandlers[size_t(closeButton->handlerClick - 1)]();
    CHECK(!inspectorHost->meta()->visible);
    CHECK(!databaseHost->meta()->visible);

    shell.close();
    CHECK(!shell.isOpen());
    CHECK(!inspectorHost->meta()->visible);
    CHECK(!databaseHost->meta()->visible);
}
