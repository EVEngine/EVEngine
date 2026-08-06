#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "ui/Component.h"
#include "ui/Theme.h"
#include "ui/UI.h"
#include "ui/UIHost.h"
#include "ui/UISystem.h"
#include "ui/Widget.h"

#include <string>
#include <vector>

using namespace eve::ui;

class InventoryPanel : public Component {
public:
    std::vector<std::string> items{"Sword", "Potion"};
    int gold = 10;
    bool showExtra = true;

    WidgetDesc build() override {
        return window("Inventory",
                      {
                          text("Gold " + std::to_string(gold), "gold"),
                          separator("sep"),
                          listButtons("items", items),
                          when(showExtra, text("Extra tip", "tip")),
                          checkbox("Show tip", showExtra, "tipchk", [this](bool v) {
                              showExtra = v;
                              setState();
                          }),
                      },
                      "root");
    }
};

TEST_CASE("UI.b.listAndWhen") {
    auto tree = window("W", {
                                when(false, text("hidden", "h")),
                                when(true, text("shown", "s")),
                                listButtons("L", {"a", "b", "c"}),
                            });
    UIHost *h = UIHost::createHost("listwhen");
    h->setTree(std::move(tree));
    CHECK(h->findById("s") != nullptr);
    CHECK(h->findById("h") == nullptr);  // when(false) → empty group, no "h"
    CHECK(h->findById("L/0") != nullptr);
    CHECK(h->findById("L/2") != nullptr);
}

TEST_CASE("UI.b.keyReconcilePropsOnly") {
    UIHost *h = UIHost::createHost("rec");
    h->setTree(window("W", {text("v1", "label"), button("Go", "btn")}, "root"));
    bool rebuilt = h->setTreeReconcile(window("W", {text("v2", "label"), button("Go", "btn")}, "root"));
    CHECK(!rebuilt);  // structure same → props patch
    CHECK(h->findById("label")->text == "v2");

    rebuilt = h->setTreeReconcile(
        window("W", {text("v3", "label"), button("Go", "btn"), button("New", "n")}, "root"));
    CHECK(rebuilt);  // child count changed → full rebuild
    CHECK(h->findById("n") != nullptr);
}

TEST_CASE("UI.b.componentRebuild") {
    InventoryPanel panel;
    panel.mountAs("inv");
    UIHost *h = UISystem::findHost("inv");
    REQUIRE(h != nullptr);
    CHECK(h->findById("gold")->text == "Gold 10");
    CHECK(h->findById("items/0") != nullptr);

    panel.gold = 9;
    panel.items.push_back("Shield");
    panel.markDirty();
    CHECK(panel.updateIfDirty());
    CHECK(h->findById("gold")->text == "Gold 9");
    CHECK(h->findById("items/2") != nullptr);
}

TEST_CASE("UI.b.themeAndCheckbox") {
    UI *ui = UI::create();
    ui->setThemeLight();
    CHECK(globalTheme().windowBg[0] > 0.5f);
    ui->setThemeDark();
    ui->setNavKeyboard(true);
    CHECK(globalTheme().navEnableKeyboard);

    ui->mountAs("chk", window("C", {checkbox("Mute", false, "mute")}, "root"));
    CHECK(ui->current()->findById("mute") != nullptr);
    CHECK(!ui->current()->findById("mute")->checked);
    ui->setChecked("mute", true);
    CHECK(ui->current()->findById("mute")->checked);
}

TEST_CASE("UI.b.scriptListBuilder") {
    UI *ui = UI::create();
    ui->beginBuild();
    ui->beginWindow("Shop", "root");
    ui->beginList("goods");
    ui->addListItem("Apple", "goods/0");
    ui->addListItem("Bread", "goods/1");
    ui->end();
    ui->addSeparator("s");
    ui->addCheckbox("Member", false, "mem");
    ui->end();
    CHECK(ui->mountBuildAs("shop"));
    CHECK(ui->current()->findById("goods/1") != nullptr);
    CHECK(ui->current()->findById("mem") != nullptr);
}
