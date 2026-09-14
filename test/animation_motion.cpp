#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "animation/Animation.h"
#include "animation/MotionBuilder.h"
#include "animation/MotionTypes.h"

#include "common/Time.h"

#include <cmath>

using namespace eve::animation;

namespace {

eve::SimulationStep stepAt(std::uint64_t tick, double seconds) {
    return eve::SimulationStep{eve::SimulationTick{tick},
                               eve::Duration::fromSeconds(seconds).expect("motion test dt")};
}

}  // namespace

TEST_CASE("animation.motion.floatBindLinear") {
    auto *anim = Animation::create();
    float out  = -1.f;
    FloatPointerSink sink(&out);

    auto handle = anim->motion(0.f, 100.f, 1.f).ease("linear").bind(sink).expect("spawn");
    CHECK(anim->motions().isActive(handle));
    CHECK(std::fabs(out - 0.f) < 1e-5f);

    {
        auto advanced = anim->advance(stepAt(1, 0.5));
        REQUIRE(advanced.ok());
    }
    CHECK(std::fabs(out - 50.f) < 1e-3f);

    {
        auto advanced = anim->advance(stepAt(2, 0.5));
        REQUIRE(advanced.ok());
    }
    CHECK(std::fabs(out - 100.f) < 1e-3f);
    CHECK(!anim->motions().isActive(handle));
    CHECK_EQ(anim->getMotionCount(), 0);
}

TEST_CASE("animation.motion.completeAndCancel") {
    auto *anim = Animation::create();
    float out  = 0.f;
    FloatPointerSink sink(&out);
    int completed = 0;
    int cancelled = 0;

    auto a = anim->motion(0.f, 10.f, 1.f)
                 .onComplete([&] { ++completed; })
                 .bind(sink)
                 .expect("spawn a");
    {
        auto advanced = anim->advance(stepAt(1, 0.25));
        REQUIRE(advanced.ok());
    }
    {
        auto done = anim->motions().complete(a);
        REQUIRE(done.ok());
    }
    CHECK(std::fabs(out - 10.f) < 1e-4f);
    CHECK_EQ(completed, 1);
    CHECK(!anim->motions().isActive(a));

    auto b = anim->motion(0.f, 10.f, 1.f)
                 .onCancel([&] { ++cancelled; })
                 .bind(sink)
                 .expect("spawn b");
    {
        auto advanced = anim->advance(stepAt(2, 0.1));
        REQUIRE(advanced.ok());
    }
    {
        auto cancelledResult = anim->motions().cancel(b);
        REQUIRE(cancelledResult.ok());
    }
    CHECK_EQ(cancelled, 1);
    CHECK(!anim->motions().isActive(b));
}

TEST_CASE("animation.motion.yoyoLoopsAndVec2") {
    auto *anim = Animation::create();
    float x = 0.f, y = 0.f;
    Vec2PointerSink sink(&x, &y);

    auto handle = anim->motionVec2(MotionVec2{0.f, 0.f}, MotionVec2{10.f, 20.f}, 1.f)
                      .ease("linear")
                      .loops(2, MotionLoopMode::Yoyo)
                      .bind(sink)
                      .expect("spawn vec2");

    {
        auto advanced = anim->advance(stepAt(1, 1.0));
        REQUIRE(advanced.ok());
    }
    CHECK(std::fabs(x - 10.f) < 1e-3f);
    CHECK(std::fabs(y - 20.f) < 1e-3f);
    CHECK(anim->motions().isActive(handle));

    {
        auto advanced = anim->advance(stepAt(2, 1.0));
        REQUIRE(advanced.ok());
    }
    CHECK(std::fabs(x - 0.f) < 1e-3f);
    CHECK(std::fabs(y - 0.f) < 1e-3f);
    CHECK(!anim->motions().isActive(handle));
}

TEST_CASE("animation.motion.staleHandleRejected") {
    auto *anim  = Animation::create();
    auto handle = anim->motion(0.f, 1.f, 0.1f).run().expect("spawn");
    {
        auto advanced = anim->advance(stepAt(1, 0.1));
        REQUIRE(advanced.ok());
    }
    CHECK(!anim->motions().isActive(handle));
    auto value = anim->motions().floatValue(handle);
    CHECK(!value.ok());
    value.ignore("expected stale handle");
}
