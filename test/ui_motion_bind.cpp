#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "animation/Animation.h"
#include "ui/UI.h"
#include "ui/UIHost.h"
#include "ui/UiMotionSinks.h"
#include "ui/UiTween.h"
#include "ui/Widget.h"

#include "common/Time.h"

#include <cmath>
#include <string>

namespace ui = eve::ui;
using eve::animation::Animation;
using eve::animation::MotionVec2;

namespace {

eve::SimulationStep stepAt(std::uint64_t tick, double seconds) {
    return eve::SimulationStep{eve::SimulationTick{tick},
                               eve::Duration::fromSeconds(seconds).expect("ui motion dt")};
}

ui::UIHost *resolveHost(ui::UIHostHandle handle) {
    auto host = ui::UIHost::resolve(handle);
    return host ? &host->get() : nullptr;
}

}  // namespace

TEST_CASE("UI.motion.bindHostPosAndOpacity") {
    auto *anim = Animation::create();
    ui::UI *uimod = ui::UI::create();
    REQUIRE(ui::UIHost::resolve(
                uimod->mountAs("motion-bind", ui::window("T", {ui::text("hi", "hi"), ui::button("Go", "go")},
                                                        "root")))
                .has_value());
    const ui::UIHostHandle handle = uimod->current();
    ui::UIHost *host = resolveHost(handle);
    REQUIRE(host != nullptr);
    host->meta()->hasPos = true;
    host->meta()->posX = 0.f;
    host->meta()->posY = 0.f;

    ui::UiHostPosSink posSink(handle);
    ui::UiNodeOpacitySink opacitySink(handle, "go");

    auto pos = anim->motionVec2(MotionVec2{0.f, 0.f}, MotionVec2{100.f, 40.f}, 1.f)
                   .ease("linear")
                   .bind(posSink)
                   .expect("host pos");
    auto fade = anim->motion(1.f, 0.25f, 1.f).ease("linear").bind(opacitySink).expect("opacity");

    {
        auto advanced = anim->advance(stepAt(1, 0.5));
        REQUIRE(advanced.ok());
    }
    CHECK(std::abs(host->meta()->posX - 50.f) < 1e-3f);
    CHECK(std::abs(host->meta()->posY - 20.f) < 1e-3f);
    auto go = host->findById("go");
    REQUIRE(go.has_value());
    CHECK(std::abs(go->get().opacity - 0.625f) < 1e-3f);

    {
        auto advanced = anim->advance(stepAt(2, 0.5));
        REQUIRE(advanced.ok());
    }
    CHECK(std::abs(host->meta()->posX - 100.f) < 1e-3f);
    CHECK(std::abs(host->meta()->posY - 40.f) < 1e-3f);
    CHECK(std::abs(go->get().opacity - 0.25f) < 1e-3f);
    CHECK(!anim->motions().isActive(pos));
    CHECK(!anim->motions().isActive(fade));
}

TEST_CASE("UI.motion.staleHostCancelsOnError") {
    auto *anim = Animation::create();
    auto handle = ui::UIHost::createHost("motion-stale");
    {
        auto host = ui::UIHost::resolve(handle);
        REQUIRE(host.has_value());
        host->get().setTree(ui::window("W", {ui::text("hi", "hi")}, "root"));
        host->get().meta()->hasPos = true;
        host->get().meta()->posX = 0.f;
        host->get().meta()->posY = 0.f;
    }

    ui::UiHostPosSink sink(handle);
    int cancelled = 0;
    auto motion = anim->motionVec2(MotionVec2{0.f, 0.f}, MotionVec2{10.f, 10.f}, 1.f)
                      .ease("linear")
                      .cancelOnError(true)
                      .onCancel([&] { ++cancelled; })
                      .bind(sink)
                      .expect("spawn");

    {
        auto advanced = anim->advance(stepAt(1, 0.25));
        REQUIRE(advanced.ok());
    }
    {
        auto host = ui::UIHost::resolve(handle);
        REQUIRE(host.has_value());
        ecs::DestroyEntity(&host->get());
    }
    CHECK(!ui::UIHost::resolve(handle).has_value());

    {
        auto advanced = anim->advance(stepAt(2, 0.25));
        // Sink write fails with StaleHandle; cancelOnError recycles the slot.
        CHECK(!advanced.ok());
    }
    CHECK_EQ(cancelled, 1);
    CHECK(!anim->motions().isActive(motion));
}

TEST_CASE("UI.motion.bindHostSizeOverlayAndItemPos") {
    auto *anim = Animation::create();
    ui::UI *uimod = ui::UI::create();
    uimod->beginBuild();
    uimod->beginWindow("Panel", "root");
    uimod->beginFlex("row", "flex");
    uimod->addButton("Go", "go");
    uimod->setItemAbsolute(0.f, 0.f, 0.f, 0.f);
    uimod->end();
    uimod->end();
    REQUIRE(uimod->mountBuildAs("motion-size"));
    const ui::UIHostHandle handle = uimod->current();
    ui::UIHost *host = resolveHost(handle);
    REQUIRE(host != nullptr);
    host->meta()->hasSize = true;
    host->meta()->sizeX = 100.f;
    host->meta()->sizeY = 50.f;
    host->meta()->overlayBgAlpha = 1.f;

    ui::UiHostSizeSink sizeSink(handle);
    ui::UiHostOverlayAlphaSink alphaSink(handle);
    ui::UiNodePosSink posSink(handle, "go");

    anim->motionVec2(MotionVec2{100.f, 50.f}, MotionVec2{200.f, 120.f}, 0.5f)
        .ease("linear")
        .bind(sizeSink)
        .expect("size");
    anim->motion(1.f, 0.f, 0.5f).ease("linear").bind(alphaSink).expect("alpha");
    anim->motionVec2(MotionVec2{0.f, 0.f}, MotionVec2{30.f, 40.f}, 0.5f)
        .ease("linear")
        .bind(posSink)
        .expect("item pos");

    {
        auto advanced = anim->advance(stepAt(1, 0.5));
        REQUIRE(advanced.ok());
    }
    CHECK(std::abs(host->meta()->sizeX - 200.f) < 1e-3f);
    CHECK(std::abs(host->meta()->sizeY - 120.f) < 1e-3f);
    CHECK(std::abs(host->meta()->overlayBgAlpha - 0.f) < 1e-3f);
    auto go = host->findById("go");
    REQUIRE(go.has_value());
    CHECK(go->get().absolute);
    CHECK(std::abs(go->get().posX - 30.f) < 1e-3f);
    CHECK(std::abs(go->get().posY - 40.f) < 1e-3f);
}
