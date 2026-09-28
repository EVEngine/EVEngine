#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "ui/UI.h"
#include "ui/UIHost.h"
#include "ui/UiTween.h"
#include "ui/Widget.h"

#include <cmath>
#include <string>

namespace ui = eve::ui;

namespace {

ui::UIHost *resolveHost(ui::UIHostHandle handle) {
    auto host = ui::UIHost::resolve(handle);
    return host ? &host->get() : nullptr;
}

ui::UINode *findNode(ui::UIHost *host, const std::string &id) {
    if (host == nullptr) return nullptr;
    auto node = host->findById(id);
    return node ? &node->get() : nullptr;
}

}  // namespace

TEST_CASE("UI.tween.conveniencePathIndependentOfAnimation") {
    // Provider-absent contract: presentation tweens must not require Animation.
    ui::UI *uimod = ui::UI::create();
    REQUIRE(ui::UIHost::resolve(
                uimod->mountAs("no-anim", ui::window("T", {ui::button("Go", "go")}, "root")))
                .has_value());
    ui::UIHost *current = resolveHost(uimod->current());
    REQUIRE(current != nullptr);

    uimod->animateHostPos(40.f, 80.f, 0.f, "outQuad");
    uimod->animateItemOpacity("go", 0.5f, 0.f, "linear");
    CHECK(std::abs(current->meta()->posX - 40.f) < 1e-4f);
    CHECK(std::abs(current->meta()->posY - 80.f) < 1e-4f);
    ui::UINode *go = findNode(current, "go");
    REQUIRE(go != nullptr);
    CHECK(std::abs(go->opacity - 0.5f) < 1e-5f);
    CHECK_EQ(uimod->getHostTweenCount(), 0u);
    CHECK_EQ(uimod->getItemTweenCount(), 0u);
}

TEST_CASE("UI.tween.easeKinds") {
    CHECK(std::abs(ui::evaluateUiEase(0.f, "linear") - 0.f) < 1e-5f);
    CHECK(std::abs(ui::evaluateUiEase(1.f, "linear") - 1.f) < 1e-5f);
    CHECK(std::abs(ui::evaluateUiEase(0.5f, "linear") - 0.5f) < 1e-5f);
    // smoothstep(0.5) == 0.5
    CHECK(std::abs(ui::evaluateUiEase(0.5f, "smoothstep") - 0.5f) < 1e-5f);
    CHECK(std::abs(ui::evaluateUiEase(0.5f, "") - 0.5f) < 1e-5f);
    // outQuad(0.5) = 1 - 0.5^2 = 0.75
    CHECK(std::abs(ui::evaluateUiEase(0.5f, "outQuad") - 0.75f) < 1e-5f);
    // unknown falls back to linear
    CHECK(std::abs(ui::evaluateUiEase(0.25f, "not-a-real-ease") - 0.25f) < 1e-5f);
}

TEST_CASE("UI.tween.hostPosDurationZeroAndMid") {
    ui::UI *uimod = ui::UI::create();
    REQUIRE(ui::UIHost::resolve(uimod->mountAs("tween-pos", ui::window("T", {ui::text("x", "x")}, "root")))
                .has_value());
    ui::UIHost *current = resolveHost(uimod->current());
    REQUIRE(current != nullptr);

    uimod->animateHostPos(120.f, 60.f, 0.f);
    CHECK(current->meta()->hasPos);
    CHECK(std::abs(current->meta()->posX - 120.f) < 1e-4f);
    CHECK(std::abs(current->meta()->posY - 60.f) < 1e-4f);
    CHECK_EQ(uimod->getHostTweenCount(), 0u);

    const double t0 = 1000.0;
    // Seed an in-flight tween via tickTweens clock injection: start at t0 by calling
    // animate then immediately tick at t0 (wall clock start is close enough for replace tests),
    // then drive with explicit ticks relative to a known baseline using the driver API.
    uimod->animateHostPos(300.f, 200.f, 1000.f, "linear");
    CHECK_EQ(uimod->getHostTweenCount(), 1u);
    // Still near start until we tick; pos should remain previous target until first tick advances.
    CHECK(std::abs(current->meta()->posX - 120.f) < 1e-4f);

    // Complete via explicit clock far in the future.
    uimod->tickTweens(1e15);
    CHECK(std::abs(current->meta()->posX - 300.f) < 1e-3f);
    CHECK(std::abs(current->meta()->posY - 200.f) < 1e-3f);
    CHECK_EQ(uimod->getHostTweenCount(), 0u);
    (void)t0;
}

TEST_CASE("UI.tween.hostPosReplaceAndCancel") {
    ui::UI *uimod = ui::UI::create();
    REQUIRE(ui::UIHost::resolve(uimod->mountAs("tween-replace", ui::window("T", {ui::text("x", "x")}, "root")))
                .has_value());
    ui::UIHost *current = resolveHost(uimod->current());
    REQUIRE(current != nullptr);

    uimod->setHostPos(0.f, 0.f);
    uimod->animateHostPos(100.f, 0.f, 5000.f, "linear");
    CHECK_EQ(uimod->getHostTweenCount(), 1u);
    uimod->animateHostPos(200.f, 50.f, 5000.f, "linear");
    // Retrigger replaces; still exactly one pending pos tween.
    CHECK_EQ(uimod->getHostTweenCount(), 1u);

    uimod->cancelHostTweens();
    CHECK_EQ(uimod->getHostTweenCount(), 0u);
    // Cancel leaves the last applied sample (still at from until a tick).
    CHECK(std::abs(current->meta()->posX - 0.f) < 1e-4f);
}

TEST_CASE("UI.tween.hostSizeAndOverlayAlpha") {
    ui::UI *uimod = ui::UI::create();
    REQUIRE(ui::UIHost::resolve(uimod->mountAs("tween-size", ui::window("T", {ui::text("x", "x")}, "root")))
                .has_value());
    ui::UIHost *current = resolveHost(uimod->current());
    REQUIRE(current != nullptr);

    uimod->animateHostSize(320.f, 180.f, 0.f);
    CHECK(current->meta()->hasSize);
    CHECK(std::abs(current->meta()->sizeX - 320.f) < 1e-4f);
    CHECK(std::abs(current->meta()->sizeY - 180.f) < 1e-4f);

    uimod->setHostOverlay(true);
    uimod->setHostOverlayAlpha(1.f);
    uimod->animateHostOverlayAlpha(0.25f, 0.f);
    CHECK(std::abs(current->meta()->overlayBgAlpha - 0.25f) < 1e-4f);
}

TEST_CASE("UI.tween.itemOpacityAndPos") {
    ui::UI *uimod = ui::UI::create();
    uimod->beginBuild();
    uimod->beginWindow("Panel", "root");
    uimod->beginFlex("row", "flex");
    uimod->addButton("Go", "go");
    uimod->setItemAbsolute(0.f, 0.f, 10.f, 20.f);
    uimod->end();
    uimod->end();
    REQUIRE(uimod->mountBuildAs("tween-item"));

    ui::UINode *go = findNode(resolveHost(uimod->current()), "go");
    REQUIRE(go != nullptr);

    uimod->animateItemOpacity("go", 0.4f, 0.f);
    CHECK(std::abs(go->opacity - 0.4f) < 1e-5f);

    uimod->animateItemPos("go", 80.f, 90.f, 0.f);
    CHECK(go->absolute);
    CHECK(std::abs(go->posX - 80.f) < 1e-4f);
    CHECK(std::abs(go->posY - 90.f) < 1e-4f);

    uimod->animateItemOpacity("go", 1.f, 2000.f, "linear");
    CHECK_EQ(uimod->getItemTweenCount(), 1u);
    uimod->cancelItemTweens("go");
    CHECK_EQ(uimod->getItemTweenCount(), 0u);
}

TEST_CASE("UI.tween.driverMidSampleLinear") {
    ui::UiTweenDriver driver;
    auto handle = ui::UIHost::createHost("driver-mid");
    auto host = ui::UIHost::resolve(handle);
    REQUIRE(host.has_value());
    host->get().setTree(ui::window("W", {ui::text("hi", "hi")}, "root"));
    host->get().meta()->hasPos = true;
    host->get().meta()->posX = 0.f;
    host->get().meta()->posY = 0.f;

    driver.animateHostPos(handle, 100.f, 50.f, 1000.f, "linear", 0.f, 0.0);
    CHECK_EQ(driver.hostTweenCount(), 1u);

    driver.tick(0.0);
    CHECK(std::abs(host->get().meta()->posX - 0.f) < 1e-4f);

    driver.tick(500.0);
    CHECK(std::abs(host->get().meta()->posX - 50.f) < 1e-3f);
    CHECK(std::abs(host->get().meta()->posY - 25.f) < 1e-3f);

    driver.tick(1000.0);
    CHECK(std::abs(host->get().meta()->posX - 100.f) < 1e-3f);
    CHECK(std::abs(host->get().meta()->posY - 50.f) < 1e-3f);
    CHECK_EQ(driver.hostTweenCount(), 0u);
}

TEST_CASE("UI.tween.delayHoldsFrom") {
    ui::UiTweenDriver driver;
    auto handle = ui::UIHost::createHost("driver-delay");
    auto host = ui::UIHost::resolve(handle);
    REQUIRE(host.has_value());
    host->get().setTree(ui::window("W", {ui::text("hi", "hi")}, "root"));
    host->get().meta()->hasPos = true;
    host->get().meta()->posX = 10.f;
    host->get().meta()->posY = 20.f;

    driver.animateHostPos(handle, 110.f, 120.f, 1000.f, "linear", 200.f, 0.0);
    driver.tick(100.0);
    CHECK(std::abs(host->get().meta()->posX - 10.f) < 1e-4f);
    driver.tick(200.0);
    CHECK(std::abs(host->get().meta()->posX - 10.f) < 1e-4f);
    driver.tick(700.0);  // 500ms into the 1000ms duration after 200ms delay
    CHECK(std::abs(host->get().meta()->posX - 60.f) < 1e-3f);
}

TEST_CASE("UI.tween.destroyedHostDropsTweens") {
    ui::UiTweenDriver driver;
    auto handle = ui::UIHost::createHost("driver-drop");
    auto host = ui::UIHost::resolve(handle);
    REQUIRE(host.has_value());
    host->get().setTree(ui::window("W", {ui::text("hi", "hi")}, "root"));
    host->get().meta()->hasPos = true;
    host->get().meta()->posX = 0.f;
    host->get().meta()->posY = 0.f;
    driver.animateHostPos(handle, 50.f, 50.f, 1000.f, "linear", 0.f, 0.0);
    CHECK_EQ(driver.hostTweenCount(), 1u);
    ecs::DestroyEntity(&host->get());
    CHECK(!ui::UIHost::resolve(handle).has_value());
    driver.tick(500.0);
    CHECK_EQ(driver.hostTweenCount(), 0u);
}
