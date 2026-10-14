#include "common/Result.h"

#include <zeroerr/unittest.h>
#include <iostream>
#include <sstream>
#include <string>

namespace {
struct AssertionOutput {
    std::ostringstream text;
    std::streambuf*    previous = std::cerr.rdbuf(text.rdbuf());
    ~AssertionOutput() { restore(); }
    void restore() {
        if (previous) {
            std::cerr.rdbuf(previous);
            previous = nullptr;
        }
    }
};
}  // namespace


TEST_CASE("common.result.assertionPreservesSourceAndExpression") {
#if !defined(ZEROERR_NO_ASSERT)
    bool            caught = false;
    AssertionOutput output;
    const unsigned  line = __LINE__ + 2;
    try {
        eve::detail::checkResultInvariant(false, "originalPredicate", __FILE__, line, "original context");
    } catch (const zeroerr::AssertionData& failure) {
        caught = true;
        CHECK(std::string(failure.file) == __FILE__);
        CHECK(failure.line == line);
        CHECK(failure.cond == "originalPredicate");
        CHECK(!failure.passed);
        CHECK(failure.message.find("false") != std::string::npos);
    }
    output.restore();
    CHECK(caught);
    CHECK(output.text.str().find("original context") != std::string::npos);
#else
    CHECK(true);
#endif
}

TEST_CASE("common.result.expectPreservesFailureAndHeaderLocation") {
#if !defined(ZEROERR_NO_ASSERT)
    bool            caught = false;
    AssertionOutput output;
    auto result = eve::Result<int>::failure(eve::Diagnostic::error(eve::DiagnosticCode::Failed, "broken input"));
    try {
        (void)std::move(result).expect("operation context");
    } catch (const zeroerr::AssertionData& failure) {
        caught = true;
        CHECK(std::string(failure.file).find("Result.h") != std::string::npos);
        CHECK(failure.line > 0);
        CHECK(failure.cond == "false");
        CHECK(!failure.passed);
    }
    output.restore();
    CHECK(caught);
    CHECK(output.text.str().find("operation context") != std::string::npos);
    CHECK(output.text.str().find("broken input") != std::string::npos);
#else
    CHECK(true);
#endif
}
