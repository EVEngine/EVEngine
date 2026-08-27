#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "common/Identity.h"
#include "common/EventSequence.h"
#include "common/Generation.h"
#include "common/Revision.h"
#include "common/SchemaVersion.h"
#include "common/Time.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <limits>
#include <string>
#include <type_traits>
#include <unordered_map>

static_assert(!std::is_convertible_v<eve::PersistentId, eve::ContentId>);
static_assert(!std::is_convertible_v<eve::PersistentId, std::string>);
static_assert(!std::is_convertible_v<eve::SchemaVersion, std::uint64_t>);
static_assert(!std::is_convertible_v<eve::Generation, eve::Revision>);
static_assert(!std::is_convertible_v<eve::EventSequence, eve::Generation>);
static_assert(!std::is_convertible_v<eve::SimulationTick, eve::FrameIndex>);

TEST_CASE("common.identity.persistentIdRoundTripAndNil") {
    const auto nil = eve::PersistentId::nil();
    CHECK(nil.isNil());
    CHECK_EQ(nil.format(), "00000000-0000-0000-0000-000000000000");

    const auto parsed = eve::PersistentId::parse("01020304-0506-0708-090a-0b0c0d0e0f10");
    REQUIRE(parsed.has_value());
    CHECK(!parsed->isNil());
    CHECK_EQ(parsed->format(), "01020304-0506-0708-090a-0b0c0d0e0f10");
    CHECK(parsed == eve::PersistentId::parse(parsed->format()));
    CHECK(parsed == eve::PersistentId::parse("01020304-0506-0708-090A-0B0C0D0E0F10"));
    CHECK(parsed->hash() != nil.hash());

    CHECK(!eve::PersistentId::parse("01020304-0506-0708-090a-0b0c0d0e0f1").has_value());
    CHECK(!eve::PersistentId::parse("{01020304-0506-0708-090a-0b0c0d0e0f10}").has_value());
    CHECK(!eve::PersistentId::parse("01020304-0506-0708-090a-0b0c0d0e0fzz").has_value());
}

TEST_CASE("common.identity.uuidV7UsesInjectedEntropyAndClock") {
    using namespace std::chrono_literals;
    const auto timestamp = std::chrono::system_clock::time_point{std::chrono::milliseconds{0x010203040506}};

    eve::UuidV7Generator generator(
        [](std::span<std::uint8_t> bytes) {
            for (std::size_t i = 0; i < bytes.size(); ++i) bytes[i] = static_cast<std::uint8_t>(0xa0 + i);
            return true;
        },
        [timestamp] { return timestamp; });

    const auto id = generator.generate();
    REQUIRE(id.has_value());
    CHECK_EQ(id->format(), "01020304-0506-70a1-a2a3-a4a5a6a7a8a9");
    const auto bytes = id->bytes();
    CHECK_EQ(bytes[6] & 0xf0u, 0x70u);
    CHECK_EQ(bytes[8] & 0xc0u, 0x80u);

    eve::UuidV7Generator unavailable([](std::span<std::uint8_t>) { return false; }, [timestamp] {
        return timestamp;
    });
    CHECK(!unavailable.generate().has_value());

    const auto negative = generator.generate(std::chrono::system_clock::time_point{} - 1ms);
    CHECK(!negative.has_value());
}

TEST_CASE("common.identity.logicalAndContentIdsHaveSeparateBoundaries") {
    const auto logical = eve::LogicalId::parse("rpg:skill.fire");
    REQUIRE(logical.has_value());
    CHECK(logical->isValid());
    CHECK_EQ(logical->namespaceName(), "rpg");
    CHECK_EQ(logical->name(), "skill.fire");
    CHECK_EQ(logical->format(), "rpg:skill.fire");
    CHECK(eve::LogicalId::fromParts("rpg", "skill.fire") == logical);

    CHECK(!eve::LogicalId::parse("rpg").has_value());
    CHECK(!eve::LogicalId::parse("rpg:").has_value());
    CHECK(!eve::LogicalId::parse("rpg:skill:fire").has_value());
    CHECK(!eve::LogicalId::parse("rpg/skill:fire").has_value());

    const std::array<std::uint8_t, 16> digest{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
    const auto content = eve::ContentId::fromBytes(digest);
    REQUIRE(content.has_value());
    CHECK(!content->isNil());
    CHECK_EQ(content->format(), "00010203-0405-0607-0809-0a0b0c0d0e0f");
    CHECK(content == eve::ContentId::parse(content->format()));
    CHECK(!eve::ContentId::fromBytes(std::span<const std::uint8_t>(digest).first(15)).has_value());

    std::unordered_map<eve::ContentId, int> values;
    values.emplace(*content, 42);
    CHECK_EQ(values.at(*content), 42);
}

TEST_CASE("common.versionTypesDoNotWrapOrMixDomains") {
    const eve::SchemaVersion schemaVersion(7);
    const eve::Generation generation(7);
    CHECK_EQ(schemaVersion.value(), 7u);
    CHECK_EQ(generation.value(), 7u);
    CHECK(schemaVersion != eve::SchemaVersion(8));
    CHECK(generation < eve::Generation(8));

    const auto next = schemaVersion.incremented();
    REQUIRE(next.has_value());
    CHECK_EQ(next->value(), 8u);

    const eve::Revision maximum(std::numeric_limits<std::uint64_t>::max());
    CHECK(!maximum.incremented().has_value());

    const eve::EventSequence sequence(3);
    const eve::FrameIndex frame(4);
    CHECK_EQ(sequence.value(), 3u);
    CHECK_EQ(frame.value(), 4u);

    std::unordered_map<eve::SimulationTick, eve::FrameIndex> frames;
    frames.emplace(eve::SimulationTick(12), eve::FrameIndex(24));
    CHECK_EQ(frames.at(eve::SimulationTick(12)).value(), 24u);
}
