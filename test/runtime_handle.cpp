#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "common/RuntimeHandle.h"

#include <cstdint>
#include <limits>
#include <type_traits>
#include <unordered_set>

namespace {

struct UiTag {};
struct PhysicsTag {};

using UiHandle      = eve::RuntimeHandle<UiTag>;
using PhysicsHandle = eve::RuntimeHandle<PhysicsTag>;

static_assert(!std::is_convertible_v<UiHandle, std::uint64_t>);
static_assert(!std::is_convertible_v<std::uint64_t, UiHandle>);
static_assert(!std::is_convertible_v<UiHandle, PhysicsHandle>);
static_assert(!std::is_convertible_v<PhysicsHandle, UiHandle>);
static_assert(std::is_constructible_v<UiHandle, std::uint32_t, std::uint32_t>);
static_assert(!std::is_constructible_v<UiHandle, std::uint64_t>);

}  // namespace

TEST_CASE("common.runtimeHandle.invalidAndPackedBoundary") {
    constexpr UiHandle invalid = UiHandle::invalid();
    CHECK(invalid.isInvalid());
    CHECK(!invalid.isValid());
    CHECK_EQ(invalid.index(), UiHandle::invalidIndex);
    CHECK_EQ(invalid.generation(), UiHandle::invalidGeneration);

    constexpr UiHandle handle(7u, 3u);
    CHECK(handle.isValid());
    CHECK_EQ(handle.index(), 7u);
    CHECK_EQ(handle.generation(), 3u);
    CHECK_EQ(UiHandle::fromPacked(handle.packed()), handle);
    CHECK(UiHandle::fromPacked(0).isInvalid());
}

TEST_CASE("common.runtimeHandle.bumpRejectsOverflow") {
    constexpr UiHandle first(4u, 1u);
    const auto bumped = first.nextGeneration();
    REQUIRE(bumped.has_value());
    CHECK_EQ(bumped->index(), first.index());
    CHECK_EQ(bumped->generation(), 2u);

    const auto fromInvalidGeneration = UiHandle::nextGeneration(0u);
    REQUIRE(fromInvalidGeneration.has_value());
    CHECK_EQ(*fromInvalidGeneration, 1u);

    constexpr auto maximum = std::numeric_limits<UiHandle::generation_type>::max();
    CHECK(!UiHandle::nextGeneration(maximum).has_value());
    CHECK(!UiHandle(4u, maximum).nextGeneration().has_value());
    CHECK(!UiHandle::invalid().nextGeneration().has_value());
}

TEST_CASE("common.runtimeHandle.hashAndTypeIsolation") {
    const UiHandle first(2u, 1u);
    const UiHandle second(2u, 2u);
    CHECK(first != second);

    std::unordered_set<UiHandle> values;
    values.insert(first);
    values.insert(second);
    CHECK_EQ(values.size(), size_t(2));
    CHECK(values.find(first) != values.end());
    CHECK(values.find(second) != values.end());

    const PhysicsHandle other(2u, 1u);
    CHECK(other.isValid());
    CHECK_EQ(other.index(), first.index());
    CHECK_EQ(other.generation(), first.generation());
}
