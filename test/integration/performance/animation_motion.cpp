#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <utility>
#include <vector>
#include "animation/AnimClip.h"
#include "animation/AnimSkeleton.h"
#include "animation/Animation.h"
#include "animation/MotionBuilder.h"
#include "animation/MotionDatabase.h"
#include "animation/MotionFeatureLayout.h"
#include "animation/MotionMatcher.h"
#include "animation/MotionSequence.h"
#include "animation/MotionTypes.h"
#include "animation/PoseInertiaInternal.h"
#include "common/Time.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

// animation_motion.cpp
using namespace eve::animation;

namespace {

[[maybe_unused]] eve::SimulationStep stepAt(std::uint64_t tick, double seconds) {
    return eve::SimulationStep{eve::SimulationTick{tick}, eve::Duration::fromSeconds(seconds).expect("motion test dt")};
}

}  // namespace


TEST_CASE("animation.motion.bench10kFloatAdvance") {
    if (!std::getenv("EVENGINE_MOTION_BENCH")) return;

    constexpr int kCount  = 10000;
    constexpr int kFrames = 120;
    auto         *anim    = Animation::create();
    anim->ensureMotionCapacity(static_cast<std::size_t>(kCount));

    std::vector<float>                            buffer(static_cast<std::size_t>(kCount), 0.f);
    std::vector<std::unique_ptr<FloatBufferSink>> sinks;
    sinks.reserve(static_cast<std::size_t>(kCount));
    for (int i = 0; i < kCount; ++i) {
        sinks.push_back(std::make_unique<FloatBufferSink>(buffer.data(), static_cast<std::size_t>(i)));
        anim->motion(0.f, 1.f, 1.f).ease("linear").bind(*sinks.back()).expect("bench spawn");
    }
    CHECK_EQ(anim->getMotionCount(), kCount);

    using clock   = std::chrono::steady_clock;
    const auto t0 = clock::now();
    for (int f = 0; f < kFrames; ++f) {
        auto advanced = anim->advance(stepAt(static_cast<std::uint64_t>(1 + f), 1.0 / 60.0));
        REQUIRE(advanced.ok());
    }
    const auto   t1 = clock::now();
    const double ms = std::chrono::duration_cast<std::chrono::duration<double, std::milli>>(t1 - t0).count();
    std::cout << "[motion-bench] N=" << kCount << " frames=" << kFrames << " total_ms=" << ms
              << " per_frame_ms=" << (ms / kFrames) << " active=" << anim->getMotionCount() << std::endl;
    CHECK(ms > 0.0);
}

// animation_motion_continuity.cpp
using namespace eve::animation;

namespace {
[[maybe_unused]] void locomotion(AnimClip &clip, float speed) {
    clip.setDuration(0.8f);
    clip.setLoop(true);
    clip.addPositionKey(0, 0.f, 0.f, 0.f, 0.f);
    clip.addPositionKey(0, 0.8f, 0.f, 0.f, speed * 0.8f);
    clip.addPositionKey(1, 0.f, 0.f, 0.1f, -0.2f);
    clip.addPositionKey(1, 0.4f, 0.f, 0.3f, 0.2f);
    clip.addPositionKey(1, 0.8f, 0.f, 0.1f, -0.2f);
}
}  // namespace


// animation_motion_curve_features.cpp
using namespace eve::animation;
namespace {
[[maybe_unused]] std::vector<std::byte> curveBytes() {
    std::vector<std::byte> out;
    auto                   integer = [&](std::uint32_t value) {
        for (int i = 0; i < 4; ++i) out.push_back(std::byte((value >> (8 * i)) & 255));
    };
    auto number = [&](float value) { integer(std::bit_cast<std::uint32_t>(value)); };
    auto string = [&](std::string_view value) {
        integer(static_cast<std::uint32_t>(value.size()));
        for (char c : value) out.push_back(std::byte(c));
    };
    integer(0x43465645);
    integer(1);
    integer(3);
    for (const auto name : {"positive", "negative", "absent"}) {
        string(name);
        number(1.f);
        const bool absent = std::string_view(name) == "absent";
        integer(absent ? 0 : 1);
        if (absent) continue;
        string("Phase");
        integer(2);
        const float value = std::string_view(name) == "positive" ? 1.f : -1.f;
        number(0.f);
        number(value);
        number(1.f);
        number(value);
    }
    return out;
}
}  // namespace


// animation_motion_features.cpp
using namespace eve::animation;
namespace {
[[maybe_unused]] void bones(AnimSkeleton &skeleton) {
    skeleton.addBone("root");
    skeleton.addBone("left", 0);
    skeleton.addBone("right", 0);
    skeleton.addBone("pelvis", 0);
}
[[maybe_unused]] void swing(AnimClip &clip, float direction) {
    clip.setDuration(1.f);
    clip.setSampleRate(60.f);
    clip.addPositionKey(1, 0.f, 0.f, 0.f, -0.5f * direction);
    clip.addPositionKey(1, 1.f, 0.f, 0.f, 0.5f * direction);
}
}  // namespace
