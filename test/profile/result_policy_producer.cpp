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
