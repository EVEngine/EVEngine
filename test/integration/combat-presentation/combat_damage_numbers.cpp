// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "module": "combat"
}
*/
// clang-format on
#include "combat/Damage.h"
#include "graphics/DamageNumbers.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

using namespace eve::graphics;

TEST_CASE("damageNumbers.formatAndCombatComposition") {
    DamageNumbers numbers;
    auto          style = numbers.style(DamageNumberKind::Heal);
    REQUIRE(style.ok());
    style.value().compact  = true;
    style.value().decimals = 1;
    REQUIRE(numbers.setStyle(DamageNumberKind::Heal, style.value()).ok());
    REQUIRE(numbers.spawn({1250, 0, 0, DamageNumberKind::Heal}).ok());
    CHECK_EQ(numbers.snapshot()[0].text, std::string("+1.2K"));
    auto id = eve::PersistentId::parse("01020304-0506-0708-890a-0b0c0d0e0f10");
    REQUIRE(id.has_value());
    eve::combat::CombatState   target{eve::SubjectRef::fromPersistentId(*id), 12, 12, 10, 10};
    eve::combat::DamageRequest request;
    request.target       = target.subject;
    request.damageType   = "Damage.Physical.Slash";
    request.healthDamage = 100;
    request.critical     = true;
    eve::combat::DamageRuntime combat;
    auto                       damage = combat.apply(target, request);
    REQUIRE(damage.ok());
    const auto& result = damage.value().settlementResult;
    REQUIRE(
        numbers
            .spawn({result.applied, 100, 200, result.critical ? DamageNumberKind::Critical : DamageNumberKind::Damage})
            .ok());
    CHECK_EQ(numbers.snapshot().back().text, std::string("-12!"));
}
