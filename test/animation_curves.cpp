#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>
#include "animation/AnimCurveLibrary.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
namespace {
using Bytes = std::vector<std::byte>;
void integer(Bytes& out, std::uint32_t value) {
    for (int i = 0; i < 4; ++i) out.push_back(std::byte((value >> (8 * i)) & 255));
}
void number(Bytes& out, float value) { integer(out, std::bit_cast<std::uint32_t>(value)); }
void string(Bytes& out, const std::string& value) {
    integer(out, static_cast<std::uint32_t>(value.size()));
    for (char c : value) out.push_back(std::byte(c));
}
Bytes fixture() {
    Bytes out;
    integer(out, 0x43465645);
    integer(out, 1);
    integer(out, 2);
    string(out, "walk");
    number(out, 1);
    integer(out, 1);
    string(out, "contact_l");
    integer(out, 3);
    for (auto key : {std::pair{0.f, 0.f}, std::pair{.25f, 1.f}, std::pair{1.f, 0.f}}) {
        number(out, key.first);
        number(out, key.second);
    }
    string(out, "absent");
    number(out, 0);
    integer(out, 0);
    return out;
}
}  // namespace
TEST_CASE("animation.curves.samplingWrapsClampsAndDistinguishesAbsence") {
    eve::animation::AnimCurveLibrary curves;
    auto                             loaded = curves.load(fixture());
    REQUIRE(loaded.ok());
    CHECK(curves.size() == 2);
    for (double t : {.125, 1.125, -.875, 1000000.125}) {
        auto sample = curves.sample("walk", "contact_l", t, true);
        REQUIRE(sample.ok());
        REQUIRE(sample.value().has_value());
        CHECK(std::abs(*sample.value() - .5f) < 1e-6f);
    }
    for (double t : {-1., 2.}) {
        auto sample = curves.sample("walk", "contact_l", t, false);
        REQUIRE(sample.ok());
        CHECK(*sample.value() == 0);
    }
    auto absent = curves.sample("walk", "contact_r", 0, false);
    REQUIRE(absent.ok());
    CHECK(!absent.value());
    auto empty = curves.sample("absent", "contact_l", 0, true);
    REQUIRE(empty.ok());
    CHECK(!empty.value());
    auto missing = curves.sample("missing", "contact_l", 0, false);
    CHECK(!missing.ok());
    auto invalid = curves.sample("walk", "contact_l", std::numeric_limits<double>::infinity(), true);
    CHECK(!invalid.ok());
}
TEST_CASE("animation.curves.rejectsMalformedInputAtomically") {
    auto                             bytes = fixture();
    eve::animation::AnimCurveLibrary curves;
    auto                             loaded = curves.load(bytes);
    REQUIRE(loaded.ok());
    for (std::size_t size = 0; size < bytes.size(); ++size) {
        auto rejected = curves.load(std::span(bytes).first(size));
        CHECK(!rejected.ok());
        CHECK(curves.size() == 2);
    }
    std::vector<Bytes> cases;
    auto               version = bytes;
    version[4]                 = std::byte(2);
    cases.push_back(version);
    auto trailing = bytes;
    trailing.push_back(std::byte(0));
    cases.push_back(trailing);
    auto nan = bytes;
    // Header(12), source length/name(8), duration at offset 20.
    for (int i = 0; i < 4; ++i) nan[20 + i] = std::byte((0x7fc00000u >> (i * 8)) & 255);
    cases.push_back(nan);
    auto duplicate = bytes;
    // Duplicate a complete source record by appending the first record instead of the absent record.
    duplicate.resize(bytes.size() - 18);  // absent: length/name(10), duration/count(8).
    duplicate.insert(duplicate.end(), bytes.begin() + 12, bytes.end() - 18);
    cases.push_back(duplicate);
    auto changedInteger = [&](std::size_t offset, std::uint32_t value) {
        auto changed = bytes;
        for (int i = 0; i < 4; ++i) changed[offset + i] = std::byte((value >> (i * 8)) & 255);
        cases.push_back(changed);
    };
    changedInteger(8, 100001);                              // source limit
    changedInteger(24, 65);                                 // channel limit
    changedInteger(41, 1000001);                            // key limit
    changedInteger(53, 0);                                  // duplicate key time
    changedInteger(61, std::bit_cast<std::uint32_t>(.9f));  // missing duration endpoint
    auto invalidName = bytes;
    invalidName[16]  = std::byte(0xff);
    cases.push_back(invalidName);
    auto duplicateChannel = bytes;
    duplicateChannel[24]  = std::byte(2);
    duplicateChannel.insert(duplicateChannel.begin() + 69, bytes.begin() + 28, bytes.begin() + 69);
    cases.push_back(duplicateChannel);
    for (const auto& invalid : cases) {
        auto rejected = curves.load(invalid);
        CHECK(!rejected.ok());
        auto sample = curves.sample("walk", "contact_l", .25, false);
        REQUIRE(sample.ok());
        CHECK(*sample.value() == 1);
    }
}
