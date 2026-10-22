#include <algorithm>
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

namespace {
void failingHelper() { CHECK(false); }
void failingInvariant() { ASSERT(false); }
}  // namespace

TEST_CASE("assertionProbe.singleEvaluation") {
    int left  = 0;
    int right = 0;
    CHECK_EQ(++left, ++right);
    REQUIRE_EQ(left, 1);
    REQUIRE_EQ(right, 1);
    CHECK_EQ(std::min(left + 1, right + 2), 2);
    // Compile every CHECK comparison directly, including omitted diagnostics.
    // Nested variadic forwarding breaks these calls with MSVC's legacy preprocessor.
    CHECK_NE(left, right + 1);
    CHECK_LT(left, right + 1);
    CHECK_LE(left, right);
    CHECK_GT(left + 1, right);
    CHECK_GE(left, right);
}
TEST_CASE("assertionProbe.checkFails") { CHECK(false); }
TEST_CASE("assertionProbe.comparisonFails") { CHECK_EQ(1, 2); }
TEST_CASE("assertionProbe.requireFails") { REQUIRE(false); }
TEST_CASE("assertionProbe.assertFails") { ASSERT(false); }
TEST_CASE("assertionProbe.helperFails") { failingHelper(); }
TEST_CASE("assertionProbe.expectedInvariant") { CHECK_THROWS((failingInvariant(), false)); }

int main(int argc, char** argv) { return zeroerr::UnitTest().parseArgs(argc, const_cast<const char**>(argv)).run(); }
