#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "ui/UI.h"
#include "ui/UIHost.h"
#include "ui/UISystem.h"
#include "ui/Widget.h"

#include <string>

using namespace eve::ui;

namespace {

UIHost *resolveHost(UIHostHandle handle) {
    auto host = UIHost::resolve(handle);
    return host ? &host->get() : nullptr;
}

UINode *findNode(UIHostHandle handle, const std::string &id) {
    UIHost *host = resolveHost(handle);
    if (host == nullptr) return nullptr;
    auto node = host->findById(id);
    return node ? &node->get() : nullptr;
}

}  // namespace

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
    REQUIRE(ui->mountAs("hud", window("HUD", {text("HP", "hp"), button("Pause", "pause")})));
    REQUIRE(ui->mountAs("menu", window("Menu", {button("Quit", "quit")})));

    CHECK(resolveHost(UISystem::findHost("hud")) != nullptr);
    CHECK(resolveHost(UISystem::findHost("menu")) != nullptr);
    CHECK(ui->select("hud"));
    CHECK(resolveHost(ui->current()) == resolveHost(UISystem::findHost("hud")));
    ui->setText("hp", "HP 80");
    REQUIRE(findNode(ui->current(), "hp") != nullptr);
    CHECK(findNode(ui->current(), "hp")->text == "HP 80");

    CHECK(ui->select("menu"));
    CHECK(findNode(ui->current(), "quit") != nullptr);
}

TEST_CASE("UI.ecs.subclassVisibleInView") {
    HudPanel *hud = HudPanel::create();
    hud->meta()->entity = ecs::handle_of(hud);
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
            CHECK(ecs::try_get(meta->entity) != nullptr);
        }
    }
    CHECK_GE(found, 1);
    CHECK_EQ(hud->hp()->value, 42);
    CHECK(resolveHost(UISystem::findHost("playerhud")) == static_cast<UIHost *>(hud));
}

TEST_CASE("UI.ecs.ownerBindingAndClicks") {
    UI *ui = UI::create();
    REQUIRE(ui->mountAs("inv", window("Inv", {button("Use", "use")})));
    ui->bindOwner(7);
    CHECK(resolveHost(UISystem::findHostByOwner(7)) == resolveHost(ui->current()));

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
    CHECK(findNode(ui->current(), "gold") != nullptr);
    ui->setText("gold", "Gold: 9");
    REQUIRE(findNode(ui->current(), "gold") != nullptr);
    CHECK(findNode(ui->current(), "gold")->text == "Gold: 9");
}
