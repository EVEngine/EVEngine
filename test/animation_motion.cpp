#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "animation/Animation.h"
#include "animation/MotionBuilder.h"
#include "animation/MotionSequence.h"
#include "animation/MotionTypes.h"

#include "common/Time.h"

#include <cmath>
#include <utility>

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

TEST_CASE("animation.motion.easeBackElasticBounce") {
    CHECK(evaluateMotionEase(0.f, "inBack") == 0.f);
    CHECK(evaluateMotionEase(1.f, "outBack") == 1.f);
    CHECK(evaluateMotionEase(0.5f, "outBack") > 1.f);
    CHECK(evaluateMotionEase(0.f, "inElastic") == 0.f);
    CHECK(evaluateMotionEase(1.f, "outElastic") == 1.f);
    CHECK(evaluateMotionEase(0.f, "inBounce") == 0.f);
    CHECK(evaluateMotionEase(1.f, "outBounce") == 1.f);
    CHECK(evaluateMotionEase(0.5f, "inOutBounce") > 0.f);
}

TEST_CASE("animation.motion.sequenceAppendJoinInterval") {
    auto *anim = Animation::create();
    float a = -1.f;
    float b = -1.f;
    FloatPointerSink sinkA(&a);
    FloatPointerSink sinkB(&b);

    auto seq = anim->sequence();
    {
        auto scheduled =
            seq.append(std::move(anim->motion(0.f, 10.f, 1.f).ease("linear").to(sinkA)));
        REQUIRE(scheduled.ok());
    }
    {
        auto scheduled = seq.appendInterval(0.5f);
        REQUIRE(scheduled.ok());
    }
    {
        auto scheduled =
            seq.join(std::move(anim->motion(0.f, 20.f, 1.f).ease("linear").to(sinkB)));
        REQUIRE(scheduled.ok());
    }
    CHECK_EQ(seq.itemCount(), 2);
    CHECK(std::fabs(seq.duration() - 1.5f) < 1e-4f);

    auto playback = seq.run().expect("sequence run");
    CHECK_EQ(playback.childCount(), 2);
    CHECK(playback.isActive());

    // Join starts at the last Append start (t=0), so A and B advance together.
    {
        auto advanced = anim->advance(stepAt(1, 0.5));
        REQUIRE(advanced.ok());
    }
    CHECK(std::fabs(a - 5.f) < 1e-3f);
    CHECK(std::fabs(b - 10.f) < 1e-3f);

    {
        auto advanced = anim->advance(stepAt(2, 0.5));
        REQUIRE(advanced.ok());
    }
    CHECK(std::fabs(a - 10.f) < 1e-3f);
    CHECK(std::fabs(b - 20.f) < 1e-3f);
    CHECK(!playback.isActive());
}

TEST_CASE("animation.motion.sequenceInsertAndComplete") {
    auto *anim = Animation::create();
    float x = 0.f;
    float y = 0.f;
    FloatPointerSink sinkX(&x);
    FloatPointerSink sinkY(&y);

    auto seq = anim->sequence();
    {
        auto scheduled =
            seq.append(std::move(anim->motion(0.f, 10.f, 1.f).ease("linear").to(sinkX)));
        REQUIRE(scheduled.ok());
    }
    {
        auto scheduled = seq.insert(
            0.5f, std::move(anim->motion(0.f, 100.f, 0.5f).ease("linear").to(sinkY)));
        REQUIRE(scheduled.ok());
    }

    auto playback = seq.run().expect("sequence run");
    {
        auto advanced = anim->advance(stepAt(1, 0.5));
        REQUIRE(advanced.ok());
    }
    CHECK(std::fabs(x - 5.f) < 1e-3f);
    CHECK(std::fabs(y - 0.f) < 1e-3f);

    {
        auto advanced = anim->advance(stepAt(2, 0.25));
        REQUIRE(advanced.ok());
    }
    CHECK(std::fabs(y - 50.f) < 1e-2f);

    {
        auto done = playback.complete();
        REQUIRE(done.ok());
    }
    CHECK(std::fabs(x - 10.f) < 1e-3f);
    CHECK(std::fabs(y - 100.f) < 1e-3f);
    CHECK(!playback.isActive());
}

TEST_CASE("animation.motion.sequenceRejectsInfiniteLoops") {
    auto *anim = Animation::create();
    auto seq   = anim->sequence();
    auto result =
        seq.append(std::move(anim->motion(0.f, 1.f, 0.2f).loops(-1, MotionLoopMode::Restart)));
    CHECK(!result.ok());
    result.ignore("expected infinite-loop rejection");
}

TEST_CASE("animation.motion.punchShakeFloat") {
    auto *anim = Animation::create();
    float out = -1.f;
    FloatPointerSink sink(&out);

    auto punch = anim->punch(0.f, 10.f, 1.f).frequency(2).dampingRatio(0.f).bind(sink).expect("punch");
    CHECK(anim->motions().isActive(punch));
    CHECK(std::fabs(out - 0.f) < 1e-5f);  // t=0 envelope is 0

    {
        auto advanced = anim->advance(stepAt(1, 0.25));
        REQUIRE(advanced.ok());
    }
    // frequency=2, undamped: sin(2*pi*0.25)=sin(pi/2)=1 => value ~= 10
    CHECK(std::fabs(out - 10.f) < 1e-2f);

    {
        auto advanced = anim->advance(stepAt(2, 0.75));
        REQUIRE(advanced.ok());
    }
    CHECK(std::fabs(out - 0.f) < 1e-3f);  // settles at base
    CHECK(!anim->motions().isActive(punch));

    out = 0.f;
    auto shake = anim->shake(0.f, 5.f, 0.5f).frequency(4).dampingRatio(0.f).seed(7).bind(sink).expect("shake");
    {
        auto advanced = anim->advance(stepAt(3, 0.1));
        REQUIRE(advanced.ok());
    }
    CHECK(std::fabs(out) > 1e-3f);  // moved off base
    {
        auto advanced = anim->advance(stepAt(4, 0.4));
        REQUIRE(advanced.ok());
    }
    CHECK(std::fabs(out - 0.f) < 1e-3f);
    CHECK(!anim->motions().isActive(shake));
}

TEST_CASE("animation.motion.colorLerpAndQuatSlerp") {
    auto *anim = Animation::create();
    float r = 0, g = 0, b = 0, a = 0;
    ColorPointerSink colorSink(&r, &g, &b, &a);

    auto color = anim->motionColor(MotionColor{0.f, 0.f, 0.f, 1.f}, MotionColor{1.f, 0.f, 0.f, 1.f}, 1.f)
                     .ease("linear")
                     .bind(colorSink)
                     .expect("color");
    {
        auto advanced = anim->advance(stepAt(1, 0.5));
        REQUIRE(advanced.ok());
    }
    CHECK(std::fabs(r - 0.5f) < 1e-3f);
    CHECK(std::fabs(a - 1.f) < 1e-5f);
    {
        auto advanced = anim->advance(stepAt(2, 0.5));
        REQUIRE(advanced.ok());
    }
    CHECK(std::fabs(r - 1.f) < 1e-3f);
    CHECK(!anim->motions().isActive(color));

    float qx = 0, qy = 0, qz = 0, qw = 1;
    QuatPointerSink quatSink(&qx, &qy, &qz, &qw);
    // 180 deg about Y: (0,1,0,0) from identity
    auto quat = anim->motionQuat(MotionQuat{0.f, 0.f, 0.f, 1.f}, MotionQuat{0.f, 1.f, 0.f, 0.f}, 1.f)
                    .ease("linear")
                    .bind(quatSink)
                    .expect("quat");
    {
        auto advanced = anim->advance(stepAt(3, 0.5));
        REQUIRE(advanced.ok());
    }
    // slerp mid of identity -> (0,1,0,0) is ~ (0, sin(pi/4), 0, cos(pi/4))
    CHECK(std::fabs(qy) > 0.1f);  // mid-slerp has Y component
    CHECK(std::fabs(std::sqrt(qx * qx + qy * qy + qz * qz + qw * qw) - 1.f) < 1e-3f);
    {
        auto advanced = anim->advance(stepAt(4, 0.5));
        REQUIRE(advanced.ok());
    }
    CHECK(std::fabs(qy - 1.f) < 1e-3f);
    CHECK(std::fabs(qw - 0.f) < 1e-3f);
    CHECK(!anim->motions().isActive(quat));
}

TEST_CASE("animation.motion.oscillationHelpers") {
    CHECK(std::fabs(evaluateMotionOscillation(0.f, 10, 0.f)) < 1e-6f);
    CHECK(std::fabs(evaluateMotionOscillation(1.f, 10, 0.f)) < 1e-6f);
    CHECK(std::fabs(evaluateMotionOscillation(0.25f, 2, 0.f) - 1.f) < 1e-4f);
    const float a = evaluateMotionShakeSign(42u, 8, 0.3f, 0);
    const float b = evaluateMotionShakeSign(42u, 8, 0.3f, 0);
    CHECK(std::fabs(a - b) < 1e-6f);  // deterministic
    CHECK(a >= -1.f);
    CHECK(a <= 1.f);
}
