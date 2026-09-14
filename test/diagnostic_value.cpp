#include "common/DiagnosticValue.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

namespace {
enum class UnregisteredValue { Value };
}

static_assert(eve::DiagnosticValue<eve::Severity>);
static_assert(eve::DiagnosticValue<eve::DiagnosticCode>);
static_assert(eve::DiagnosticValue<eve::StatusCode>);
static_assert(!eve::DiagnosticValue<UnregisteredValue>);

TEST_CASE("common.diagnosticValue.stableNamesAndCodes") {
    CHECK_EQ(eve::diagnosticValueName(eve::Severity::Fatal), std::string_view("fatal"));
    CHECK_EQ(eve::diagnosticValueCode(eve::Severity::Fatal), eve::DiagnosticCode::Failed);
    CHECK_EQ(eve::diagnosticValueName(eve::StatusCode::Conflict), std::string_view("conflict"));
    CHECK_EQ(eve::diagnosticValueCode(eve::StatusCode::Conflict), eve::DiagnosticCode::Conflict);
}
