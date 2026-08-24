#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "ui/UI.h"
#include "ui/UIHost.h"
#include "ui/Widget.h"

#include <string>

using namespace eve::ui;

TEST_CASE("UI.editorKit.declarativeComponents") {
    UIHost* host = UIHost::createHost("editor_kit");
    applyTree(host, window("Editor",
                           {menuBar({menu("File", {menuItem("Save", "Ctrl+S", "save")}, "file")}, "main_menu"),
                            searchField("Search assets", "", "search").withTooltip("Filter assets"),
                            sectionHeader("Properties", "properties"),
                            card({toggleSwitch("Visible", true, "visible"), badge("Modified", "state")}, "details")},
                           "root"));

    CHECK(int(host->findById("main_menu")->type) == int(NodeType::MenuBar));
    CHECK(int(host->findById("file")->type) == int(NodeType::Menu));
    CHECK(int(host->findById("save")->type) == int(NodeType::MenuItem));
    CHECK(host->findById("save")->valueText == "Ctrl+S");
    CHECK(int(host->findById("search")->type) == int(NodeType::SearchField));
    CHECK(host->findById("search")->tooltip == "Filter assets");
    CHECK(int(host->findById("properties")->type) == int(NodeType::SectionHeader));
    CHECK(int(host->findById("details")->type) == int(NodeType::Card));
    CHECK(int(host->findById("visible")->type) == int(NodeType::Switch));
    CHECK(host->findById("visible")->checked);
    CHECK(int(host->findById("state")->type) == int(NodeType::Badge));
}

TEST_CASE("UI.editorKit.scriptBuilderAndSerialization") {
    UI* ui = UI::create();
    ui->beginBuild();
    ui->beginWindow("Editor", "root");
    ui->beginMenuBar("menu_bar");
    ui->beginMenu("Edit", "edit");
    ui->addMenuItem("Undo", "Ctrl+Z", "undo");
    ui->end();
    ui->end();
    ui->addSearchField("Search", "cube", "search");
    ui->setItemTooltip("Filter scene nodes");
    ui->addSectionHeader("Inspector", "inspector");
    ui->beginCard("card");
    ui->addSwitch("Enabled", true, "enabled");
    ui->addBadge("Active", "active");
    ui->end();
    ui->end();
    CHECK(ui->mountBuildAs("editor_builder"));

    const std::string json = ui->saveTreeJson();
    CHECK(json.find("searchField") != std::string::npos);
    CHECK(json.find("Filter scene nodes") != std::string::npos);
    CHECK(json.find("menuItem") != std::string::npos);
    CHECK(ui->loadTreeJson(json));
    CHECK(int(ui->current()->findById("enabled")->type) == int(NodeType::Switch));
    CHECK(ui->current()->findById("search")->tooltip == "Filter scene nodes");
}
