#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "ui/UI.h"
#include "ui/UIHost.h"
#include "ui/UISystem.h"
#include "ui/Widget.h"

#include <string>

using namespace eve::ui;

/** Game-facing panel: subclass UIHost so View<UIHost,…> still finds it. */
class HudPanel : public UIHost {
public:
    ENTITY(HudPanel, UIHost)
    void release() override {}

    struct Hp {
        int value = 100;
    };
    COMPONENT(Hp, hp)
};

TEST_CASE("UI.ecs.namedHostsAndSelect") {
    UI *ui = UI::create();
    ui->mountAs("hud", window("HUD", {text("HP", "hp"), button("Pause", "pause")}));
    ui->mountAs("menu", window("Menu", {button("Quit", "quit")}));

    CHECK(UISystem::findHost("hud") != nullptr);
    CHECK(UISystem::findHost("menu") != nullptr);
    CHECK(ui->select("hud"));
    CHECK(ui->current() == UISystem::findHost("hud"));
    ui->setText("hp", "HP 80");
    CHECK(ui->current()->findById("hp")->text == "HP 80");

    CHECK(ui->select("menu"));
    CHECK(ui->current()->findById("quit") != nullptr);
}

TEST_CASE("UI.ecs.subclassVisibleInView") {
    HudPanel *hud = HudPanel::create();
    hud->meta()->entity = hud;
    hud->setName("playerhud");
    hud->hp()->value = 42;
    hud->setTree(window("P", {text("x", "x")}));

    int found = 0;
    auto view = ecs::View<UIHost, UIHost::Meta, UIHost::Tree>();
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto [meta, tree] = *it;
        (void)tree;
        if (meta->name == "playerhud") {
            ++found;
            CHECK(meta->entity != nullptr);
        }
    }
    CHECK_GE(found, 1);
    CHECK_EQ(hud->hp()->value, 42);
    CHECK(UISystem::findHost("playerhud") == static_cast<UIHost *>(hud));
}

TEST_CASE("UI.ecs.ownerBindingAndClicks") {
    UI *ui = UI::create();
    ui->mountAs("inv", window("Inv", {button("Use", "use")}));
    ui->bindOwner(7);
    CHECK(UISystem::findHostByOwner(7) == ui->current());

    UIEvent ev;
    ev.host = ui->current();
    ev.hostName = "inv";
    ev.nodeId = "use";
    ev.kind = "click";
    ev.handlerIndex = 0;
    UISystem::pendingEvents().push_back(ev);
    UISystem::dispatchEvents();

    ui->select("inv");
    CHECK(ui->consumeClick() == "inv/use");
    CHECK(ui->consumeClick().empty());
}

TEST_CASE("UI.scriptStyleBuilder.mountBuildAs") {
    UI *ui = UI::create();
    ui->beginBuild();
    ui->beginWindow("Shop", "root");
    ui->addText("Gold: 10", "gold");
    ui->beginGroup("row");
    ui->addButton("Buy", "buy");
    ui->addSameLine();
    ui->addButton("Sell", "sell");
    ui->end();
    ui->end();
    CHECK(ui->mountBuildAs("shop"));
    CHECK(ui->select("shop"));
    CHECK(ui->current()->findById("gold") != nullptr);
    ui->setText("gold", "Gold: 9");
    CHECK(ui->current()->findById("gold")->text == "Gold: 9");
}
