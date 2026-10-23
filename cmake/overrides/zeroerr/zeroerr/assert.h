#pragma once

#include "../../../../external/zeroerr/include/zeroerr/assert.h"

#ifndef ZEROERR_NO_ASSERT
// Assertions in EVEngine are correctness checks. CHECK must fail the runner,
// including when it is called from a helper without an injected TestContext.
#undef ZEROERR_CHECK
#undef ZEROERR_CHECK_NOT
#undef ZEROERR_CHECK_THROWS
#define ZEROERR_CHECK(...)        ZEROERR_REQUIRE(__VA_ARGS__)
#define ZEROERR_CHECK_NOT(...)    ZEROERR_REQUIRE_NOT(__VA_ARGS__)
#define ZEROERR_CHECK_THROWS(...) ZEROERR_REQUIRE_THROWS(__VA_ARGS__)
// Keep named operands at this layer: MSVC's legacy preprocessor can collapse
// a forwarded __VA_ARGS__ pack into one argument of ZEROERR_REQUIRE_EQ, etc.
#undef ZEROERR_CHECK_EQ
#define ZEROERR_CHECK_EQ(lhs, rhs, ...) ZEROERR_ASSERT_CMP(lhs, ==, rhs, ZEROERR_ERROR, no_throw, false, __VA_ARGS__)
#undef ZEROERR_CHECK_NE
#define ZEROERR_CHECK_NE(lhs, rhs, ...) ZEROERR_ASSERT_CMP(lhs, !=, rhs, ZEROERR_ERROR, no_throw, false, __VA_ARGS__)
#undef ZEROERR_CHECK_LT
#define ZEROERR_CHECK_LT(lhs, rhs, ...) ZEROERR_ASSERT_CMP(lhs, <, rhs, ZEROERR_ERROR, no_throw, false, __VA_ARGS__)
#undef ZEROERR_CHECK_LE
#define ZEROERR_CHECK_LE(lhs, rhs, ...) ZEROERR_ASSERT_CMP(lhs, <=, rhs, ZEROERR_ERROR, no_throw, false, __VA_ARGS__)
#undef ZEROERR_CHECK_GT
#define ZEROERR_CHECK_GT(lhs, rhs, ...) ZEROERR_ASSERT_CMP(lhs, >, rhs, ZEROERR_ERROR, no_throw, false, __VA_ARGS__)
#undef ZEROERR_CHECK_GE
#define ZEROERR_CHECK_GE(lhs, rhs, ...) ZEROERR_ASSERT_CMP(lhs, >=, rhs, ZEROERR_ERROR, no_throw, false, __VA_ARGS__)

// Upstream evaluates lhs/rhs once for comparison and again for printing. Cache
// operands in an immediately invoked lambda. Even references into temporary
// arguments remain valid until comparison and diagnostic formatting finish.
#undef ZEROERR_ASSERT_CMP
#define ZEROERR_ASSERT_CMP(lhs, op, rhs, level, expect_throw, is_false, ...)                                          \
    ZEROERR_FUNC_SCOPE_BEGIN {                                                                                        \
        zeroerr::assert_info info{zeroerr::assert_level::ZEROERR_CAT(level, _l), zeroerr::assert_throw::expect_throw, \
                                  is_false};                                                                          \
        zeroerr::Printer     print;                                                                                   \
        print.isQuoted = false;                                                                                       \
        zeroerr::AssertionData assertion_data(__FILE__, __LINE__, #lhs " " #op " " #rhs, info);                       \
        try {                                                                                                         \
            ZEROERR_SUPPRESS_COMPARE                                                                                  \
            [&](const auto& zeroerr_lhs, const auto& zeroerr_rhs) {                                                   \
                assertion_data.setResult({zeroerr_lhs op zeroerr_rhs, print(zeroerr_lhs, #op, zeroerr_rhs)});         \
            }((lhs), (rhs));                                                                                          \
            ZEROERR_SUPPRESS_COMPARE_POP                                                                              \
        } catch (const std::exception& error) {                                                                       \
            assertion_data.setException(error);                                                                       \
        }                                                                                                             \
        zeroerr::detail::context_helper<                                                                              \
            decltype(_ZEROERR_TEST_CONTEXT),                                                                          \
            std::is_same<decltype(_ZEROERR_TEST_CONTEXT), const bool>::value>::setContext(assertion_data,             \
                                                                                          _ZEROERR_TEST_CONTEXT);     \
        ZEROERR_PRINT_ASSERT(assertion_data.passed == false, level, "" __VA_ARGS__);                                  \
        assertion_data();                                                                                             \
        ZEROERR_FUNC_SCOPE_RET(assertion_data.passed);                                                                \
    }                                                                                                                 \
    ZEROERR_FUNC_SCOPE_END
#endif
