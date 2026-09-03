#include "common/ProtocolDispatch.h"

#include "zeroerr/unittest.h"

static_assert(eve::DiagnosticValue<eve::ProtocolDispatch>);

TEST_CASE("common.protocolDispatch.hasExhaustiveStableOutcomes") {
    CHECK_EQ(eve::diagnosticValueName(eve::ProtocolDispatch::ReplySent), std::string_view("reply_sent"));
    CHECK_EQ(eve::diagnosticValueName(eve::ProtocolDispatch::NotificationHandled),
             std::string_view("notification_handled"));
    CHECK_EQ(eve::diagnosticValueName(eve::ProtocolDispatch::Rejected), std::string_view("rejected"));
    CHECK_EQ(eve::diagnosticValueName(eve::ProtocolDispatch::Terminate), std::string_view("terminate"));
    CHECK_EQ(eve::diagnosticValueCode(eve::ProtocolDispatch::Rejected), eve::DiagnosticCode::PreconditionViolation);
}
