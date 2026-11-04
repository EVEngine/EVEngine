// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "role": "tests",
  "default": "module-composition",
  "module": "common"
}
*/
// clang-format on

#include "common/Result.h"
#include "zeroerr/unittest.h"
namespace result_policy_test {
/** @brief Return a Result already observed in an assertion-disabled producer. */
eve::Result<int> observedProductionResult();
/** @brief Move and consume intermediate Results in an assertion-disabled producer. */
eve::Result<int> movedProductionResult();
}  // namespace result_policy_test
TEST_CASE("result.assertionPolicyInteropPreservesObservationAndMoveDuty") {
    [[maybe_unused]] const auto observed = result_policy_test::observedProductionResult();
    // The producer already observed this result; do not manufacture an extra consumer observation.
    auto moved = result_policy_test::movedProductionResult();
    REQUIRE(moved.ok());
    CHECK_EQ(moved.value(), 11);
}
