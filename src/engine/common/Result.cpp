#include "common/Result.h"
#include "common/Assert.h"

namespace eve::detail {

void checkResultInvariant(bool condition, const char* expression, const char* file, unsigned line,
                          const char* message) {
    if (condition) return;
    zeroerr::assert_info   info{zeroerr::assert_level::ZEROERR_FATAL_l, zeroerr::assert_throw::no_throw, false};
    zeroerr::AssertionData assertion_data(file, line, expression, info);
    try {
        assertion_data.setResult(zeroerr::ExpressionDecomposer() << condition);
    } catch (const std::exception& e) {
        assertion_data.setException(e);
    }
    ZEROERR_PRINT_ASSERT(assertion_data.passed == false, ZEROERR_FATAL, "{}", message);
    assertion_data();
}

void failResultExpectation(const Status& status, std::string_view message, const char* file, unsigned line,
                           bool assertionsEnabled) {
    if (assertionsEnabled) {
        const std::string context(message);
        const std::string description = status.describe();
        const std::string text        = context + ": " + description;
        checkResultInvariant(false, "false", file, line, text.c_str());
    }
    std::terminate();
}

}  // namespace eve::detail
