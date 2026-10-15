#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>
#include "animation/AnimClip.h"
#include "animation/AnimClipBinary.h"
#include "animation/AnimParallelInternal.h"
#include "animation/AnimPose.h"
#include "animation/AnimSkeleton.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
using namespace eve::animation;
namespace {
using Bytes = std::vector<std::byte>;
[[maybe_unused]] void integer(Bytes& b, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) b.push_back(static_cast<std::byte>((v >> (8 * i)) & 255));
}
[[maybe_unused]] void number(Bytes& b, float v) { integer(b, std::bit_cast<std::uint32_t>(v)); }
[[maybe_unused]] void string(Bytes& b, const std::string& v) {
    integer(b, static_cast<std::uint32_t>(v.size()));
    for (auto c : v) b.push_back(static_cast<std::byte>(c));
}
[[maybe_unused]] Bytes fixture() {
    Bytes b;
    integer(b, 0x43415645);
    integer(b, 1);
    integer(b, 1);
    number(b, 2.f);
    number(b, 30.f);
    integer(b, 1);
    string(b, "test");
    string(b, "root");
    integer(b, 2);
    for (float t : {0.f, 2.f}) {
        number(b, t);
        number(b, 0.f);
        number(b, 0.f);
        number(b, 3.f * t);
    }
    integer(b, 1);
    number(b, 0.f);
    number(b, 0.f);
    number(b, 0.f);
    number(b, 0.f);
    number(b, 1.f);
    integer(b, 1);
    number(b, 0.f);
    number(b, 1.f);
    number(b, 1.f);
    number(b, 1.f);
    return b;
}
}  // namespace
TEST_CASE("animation.binary.parallelBatchMatchesSerialAndRejectsAtomically") {
    AnimSkeleton sk;
    sk.addBone("root");
    const auto bytes = fixture();
    for (int workers : {1, 2, 8}) {
        std::vector<AnimClip>            clips(32);
        std::vector<AnimationTrackInput> inputs;
        for (auto& clip : clips) inputs.push_back({clip, bytes});
        auto loaded = loadAnimationTrackBatch(inputs, sk, workers);
        REQUIRE(loaded.ok());
        CHECK(loaded.value() >= 1);
        CHECK(loaded.value() <= workers);
        for (auto& clip : clips) {
            AnimPose pose;
            clip.sample(0.5f, &pose, &sk);
            CHECK(pose.getLocalPositionZ(0) == 1.5f);
            clip.setName("unchanged");
        }
        inputs[17].bytes = std::span<const std::byte>(bytes).first(12);
        auto rejected    = loadAnimationTrackBatch(inputs, sk, workers);
        CHECK(!rejected.ok());
        for (auto& clip : clips) CHECK(clip.getName() == "unchanged");
        inputs[17].bytes       = bytes;
        inputs[31].destination = clips[0];
        auto duplicate         = loadAnimationTrackBatch(inputs, sk, workers);
        CHECK(!duplicate.ok());
        for (auto& clip : clips) CHECK(clip.getName() == "unchanged");
    }
    auto empty = loadAnimationTrackBatch({}, sk);
    REQUIRE(empty.ok());
    CHECK(empty.value() == 0);
    auto invalid = loadAnimationTrackBatch({}, sk, -1);
    CHECK(!invalid.ok());
}
TEST_CASE("animation.binary.parallelWorkersJoinBeforeFailureReturns") {
    std::vector<int> visited(64, 0);
    bool             caught = false;
    try {
        eve::animation::detail::parallelAnimationItems(visited.size(), 8, [&](std::size_t i) {
            visited[i] = 1;
            if (i == 3 || i == 30) throw std::runtime_error(std::to_string(i));
        });
    } catch (const std::runtime_error& error) {
        caught = true;
        CHECK(std::string(error.what()) == "3");
    }
    CHECK(caught);
    for (int item : visited) CHECK(item == 1);
}
