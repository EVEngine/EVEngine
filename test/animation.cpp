#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "animation/Animation.h"
#include "animation/Tween.h"

#include "common/Exception.h"

#include <cmath>
#include <memory>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

using namespace eve::animation;

TEST_CASE("animation.tween.lerpAbsolute") {
    auto *anim = Animation::create();
    std::unique_ptr<Tween> tw(anim->newTween(1.f));
    tw->setFrom("x", 0.f);
    tw->setTo("x", 100.f);
    tw->setEase("linear");
    tw->start();

    CHECK(tw->isRunning());
    CHECK(std::fabs(tw->get("x")) < 1e-5f);

    tw->update(0.5f);
    CHECK(std::fabs(tw->get("x") - 50.f) < 1e-4f);
    CHECK(std::fabs(tw->getProgress() - 0.5f) < 1e-5f);

    tw->update(0.5f);
    CHECK(std::fabs(tw->get("x") - 100.f) < 1e-4f);
    CHECK(tw->isFinished());
    CHECK(!tw->isActive());
}

TEST_CASE("animation.tween.setDeltaRelative") {
    auto *anim = Animation::create();
    std::unique_ptr<Tween> tw(anim->newTween(1.f));
    tw->setFrom("y", 10.f);
    tw->setDelta("y", 40.f);  // → 50
    tw->start();

    CHECK(std::fabs(tw->getTo("y") - 50.f) < 1e-5f);
    CHECK(std::fabs(tw->getDelta("y") - 40.f) < 1e-5f);

    tw->update(0.25f);
    CHECK(std::fabs(tw->get("y") - 20.f) < 1e-4f);

    tw->update(0.75f);
    CHECK(std::fabs(tw->get("y") - 50.f) < 1e-4f);
    CHECK(tw->isFinished());
}

TEST_CASE("animation.tween.multiProperty") {
    auto *anim = Animation::create();
    std::unique_ptr<Tween> tw(anim->newTween(2.f));
    tw->setFrom("x", 0.f);
    tw->setTo("x", 200.f);
    tw->setFrom("alpha", 1.f);
    tw->setDelta("alpha", -1.f);  // → 0
    tw->start();

    anim->update(1.f);
    CHECK(std::fabs(tw->get("x") - 100.f) < 1e-3f);
    CHECK(std::fabs(tw->get("alpha") - 0.5f) < 1e-4f);
    CHECK_EQ(tw->getPropertyCount(), 2);
    CHECK_EQ(anim->getActiveCount(), 1);
}

TEST_CASE("animation.tween.easeOutQuad") {
    std::unique_ptr<Tween> tw(new Tween(1.f));
    tw->setFrom("v", 0.f);
    tw->setTo("v", 1.f);
    tw->setEase("outQuad");
    // evaluate at t=0.5: outQuad(0.5) = 1 - 0.5^2 = 0.75
    CHECK(std::fabs(tw->evaluate("v", 0.5f) - 0.75f) < 1e-5f);
}

TEST_CASE("animation.tween.angleShortestPath") {
    std::unique_ptr<Tween> tw(new Tween(1.f));
    tw->setFromAngle("rot", float(M_PI * 0.9));   // ~162°
    tw->setToAngle("rot", float(-M_PI * 0.9));    // ~-162°, short path crosses ±π
    tw->start();
    tw->update(0.5f);
    // Midpoint of shortest arc should be near ±π
    float mid = tw->get("rot");
    CHECK(std::fabs(std::fabs(mid) - float(M_PI)) < 0.05f);
}

TEST_CASE("animation.tween.delayPauseResume") {
    auto *anim = Animation::create();
    std::unique_ptr<Tween> tw(anim->newTween(1.f));
    tw->setFrom("x", 0.f);
    tw->setTo("x", 10.f);
    tw->setDelay(0.5f);
    tw->start();

    CHECK(tw->isDelayed());
    tw->update(0.25f);
    CHECK(tw->isDelayed());
    CHECK(std::fabs(tw->get("x")) < 1e-5f);

    tw->pause();
    CHECK(tw->isPaused());
    tw->update(1.f);  // paused — no progress
    CHECK(tw->isPaused());

    tw->resume();
    tw->update(0.25f);  // finish delay
    CHECK(tw->isRunning());
    tw->update(0.5f);
    CHECK(std::fabs(tw->get("x") - 5.f) < 1e-3f);
}

TEST_CASE("animation.tween.yoyoRepeat") {
    std::unique_ptr<Tween> tw(new Tween(1.f));
    tw->setFrom("x", 0.f);
    tw->setTo("x", 100.f);
    tw->setRepeat(2);
    tw->setYoyo(true);
    tw->start();

    tw->update(1.f);  // end of first cycle → at 100, reverse starts
    CHECK(tw->isRunning());
    CHECK(std::fabs(tw->get("x") - 100.f) < 1e-3f);

    tw->update(0.5f);  // halfway back
    CHECK(std::fabs(tw->get("x") - 50.f) < 1e-2f);

    tw->update(0.5f);  // finished second (yoyo back) cycle
    CHECK(tw->isFinished());
    CHECK(std::fabs(tw->get("x")) < 1e-2f);
}

TEST_CASE("animation.module.clearFinished") {
    auto *anim = Animation::create();
    anim->clearAll();
    // Keep raw pointers: clearFinished only drops registry entries.
    Tween *a = anim->newTween(0.5f);
    Tween *b = anim->newTween(2.f);
    a->setFrom("x", 0.f);
    a->setTo("x", 1.f);
    a->start();
    b->setFrom("x", 0.f);
    b->setTo("x", 1.f);
    b->start();

    CHECK_EQ(anim->getTweenCount(), 2);
    anim->update(0.5f);
    CHECK(a->isFinished());
    CHECK(b->isRunning());
    anim->clearFinished();
    CHECK_EQ(anim->getTweenCount(), 1);
    CHECK_EQ(anim->getActiveCount(), 1);

    delete a;
    delete b;
}

TEST_CASE("animation.tween.unknownEaseThrows") {
    std::unique_ptr<Tween> tw(new Tween(1.f));
    bool threw = false;
    try {
        tw->setEase("notARealEase");
    } catch (const eve::Exception &) {
        threw = true;
    }
    CHECK(threw);
}

TEST_CASE("animation.tween.deltaWithoutFromUsesCurrent") {
    std::unique_ptr<Tween> tw(new Tween(1.f));
    tw->setDelta("x", 10.f);  // from defaults to current (0)
    tw->start();
    CHECK(std::fabs(tw->getFrom("x")) < 1e-5f);
    CHECK(std::fabs(tw->getTo("x") - 10.f) < 1e-5f);
    tw->update(1.f);
    CHECK(std::fabs(tw->get("x") - 10.f) < 1e-4f);
}
