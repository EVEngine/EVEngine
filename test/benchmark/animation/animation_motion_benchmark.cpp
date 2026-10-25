#include "animation/Animation.h"
#include "common/Time.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <chrono>
#include <cstdint>
#include <iostream>
#include <memory>
#include <vector>

using namespace eve::animation;

namespace {
eve::SimulationStep stepAt(std::uint64_t tick, double seconds) {
    return eve::SimulationStep{eve::SimulationTick{tick}, eve::Duration::fromSeconds(seconds).expect("motion test dt")};
}
}  // namespace

TEST_CASE("animation.motion.bench10kFloatAdvance") {
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
