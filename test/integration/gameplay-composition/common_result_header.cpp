#include "common/Result.h"
#include "editing/EditingResult.h"

#if defined(EV_ASSERT) || defined(ZEROERR_ASSERT_EXP)
#error "Result headers must not expose the zeroerr assertion backend"
#endif

#include <zeroerr/unittest.h>
#include <type_traits>

#if defined(ZEROERR_NO_ASSERT)
static_assert(std::is_nothrow_destructible_v<eve::Result<int>>);
#else
static_assert(!std::is_nothrow_destructible_v<eve::Result<int>>);
#endif

TEST_CASE("common.result.headersKeepDiagnosticsInImplementation") {
    auto value = eve::Result<int>::success(7);
    CHECK(value.value() == 7);
    auto unchanged = eve::editing::noOp();
    CHECK(unchanged.ok());
    auto failed = eve::editing::rejected<int>(eve::editing::RuleId("header.contract"), "expected");
    CHECK(!failed.ok());
    CHECK(failed.status().code() == eve::StatusCode::Rejected);
}
