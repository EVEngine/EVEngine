#include "graphics/DamageNumbers.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <limits>

using namespace eve::graphics;

TEST_CASE("damageNumbers.mergeIsolationAndBoundedLifetime") {
    DamageNumbers numbers;
    REQUIRE(numbers.spawn({10, 100, 200, DamageNumberKind::Damage, "enemy"}).ok());
    auto merged = numbers.spawn({20, 100, 200, DamageNumberKind::Damage, "enemy"});
    REQUIRE(merged.ok());
    CHECK(merged.value() == DamageNumberAdmission::Merged);
    CHECK_EQ(numbers.snapshot()[0].text, std::string("-30"));
    REQUIRE(numbers.spawn({40, 100, 200, DamageNumberKind::Critical, "enemy"}).ok());
    REQUIRE(numbers.spawn({50, 100, 200, DamageNumberKind::Damage, "other"}).ok());
    REQUIRE(numbers.spawn({60, 900, 200, DamageNumberKind::Damage, "enemy"}).ok());
    CHECK_EQ(numbers.count(), 4u);
    REQUIRE(numbers.update(0.2).ok());
    REQUIRE(numbers.spawn({70, 100, 200, DamageNumberKind::Damage, "enemy"}).ok());
    CHECK_EQ(numbers.count(), 5u);
    REQUIRE(numbers.update(2.0).ok());
    CHECK_EQ(numbers.count(), 0u);
}

TEST_CASE("damageNumbers.failureIsAtomicAndCapacityIsBounded") {
    DamageNumbers numbers;
    REQUIRE(numbers.spawn({1, 10, 20}).ok());
    const auto original = numbers.snapshot();
    CHECK(!numbers.update(-0.01).ok());
    CHECK(!numbers.update(std::numeric_limits<double>::quiet_NaN()).ok());
    CHECK(!numbers.spawn({-1, 10, 20}).ok());
    CHECK(!numbers.spawn({std::numeric_limits<double>::infinity(), 10, 20}).ok());
    DamageNumberStyle invalid;
    invalid.lifetime = 0;
    CHECK(!numbers.setStyle(DamageNumberKind::Damage, invalid).ok());
    CHECK_EQ(numbers.snapshot()[0].y, original[0].y);
    for (std::size_t i = 1; i < numbers.capacity(); ++i) REQUIRE(numbers.spawn({double(i), 10, 20}).ok());
    auto replaced = numbers.spawn({999, 10, 20});
    REQUIRE(replaced.ok());
    CHECK(replaced.value() == DamageNumberAdmission::ReplacedOldest);
    CHECK_EQ(numbers.count(), numbers.capacity());
    CHECK_EQ(numbers.snapshot().back().text, std::string("-999"));
    numbers.clear();
    CHECK_EQ(numbers.count(), 0u);
}

TEST_CASE("damageNumbers.injectedTimeAndStyleCopies") {
    DamageNumbers a, b;
    REQUIRE(a.spawn({12, 100, 200, DamageNumberKind::Critical}).ok());
    REQUIRE(b.spawn({12, 100, 200, DamageNumberKind::Critical}).ok());
    REQUIRE(a.update(0.25).ok());
    REQUIRE(b.update(0.125).ok());
    REQUIRE(b.update(0.125).ok());
    CHECK_EQ(a.snapshot()[0].y, b.snapshot()[0].y);
    CHECK_EQ(a.snapshot()[0].x, b.snapshot()[0].x);
    CHECK(a.snapshot()[0].alpha > 0.99f);
    auto style = a.style(DamageNumberKind::Critical);
    REQUIRE(style.ok());
    style.value().scale = 2.0f;
    REQUIRE(a.setStyle(DamageNumberKind::Critical, style.value()).ok());
    CHECK_EQ(a.snapshot()[0].scale, b.snapshot()[0].scale);
    REQUIRE(a.update(1.0).ok());
    CHECK(a.snapshot()[0].alpha < 0.5f);
}
