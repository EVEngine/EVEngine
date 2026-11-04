// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "role": "compile-contract",
  "default": "module-composition",
  "module": "common"
}
*/
// clang-format on

#define ZEROERR_NO_ASSERT
#include "common/Result.h"
namespace result_policy_test {
eve::Result<int> observedProductionResult() {
    auto value = eve::Result<int>::success(7);
    if (!value.ok()) return eve::Result<int>::failure(value.status());
    return value;
}
eve::Result<int> movedProductionResult() {
    auto value = eve::Result<int>::success(11);
    auto moved = std::move(value);
    return eve::Result<int>::success(std::move(moved).takeValue());
}
}  // namespace result_policy_test
