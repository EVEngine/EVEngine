#include <zeroerr/unittest.h>
#include "common/Result.h"
#include "common/Value.h"

TEST_CASE("common.stringInstances.crossBoundaryGrowthCopyAndMove") {
    std::string input(8192, 'x');
    eve::Value  value(input);
    input.assign("changed");
    CHECK(value.asString().size() == 8192u);
    std::string output(value.asString());
    output.append(4096, 'y');
    CHECK(output.size() == 12288u);
    CHECK(value.asString().size() == 8192u);
    eve::Value moved(std::move(output));
    CHECK(moved.asString().back() == 'y');
    output.assign("reused");
    CHECK(output == "reused");
}

TEST_CASE("common.stringInstances.statusFormattingRemainsStable") {
    auto status = eve::Status::failure(
        eve::StatusCode::Rejected, eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "bad value", "field"));
    CHECK(status.describe() == "rejected: bad value [field]");
    CHECK(eve::Status::success().describe() == "ok");
}

TEST_CASE("common.stringInstances.resultFailureRetainsContextAndLocation") {
#if !defined(ZEROERR_NO_ASSERT)
    try {
        eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::Failed, "bad value"))
            .expect(std::string_view("context\0ignored", 15));
        CHECK(false);
    } catch (const zeroerr::AssertionData& error) {
        CHECK(std::string(error.file).find("Result.h") != std::string::npos);
        CHECK(error.line > 0u);
        CHECK(error.cond == "false");
        CHECK(error.message == "false");
    }
#else
    CHECK(true);
#endif
}
