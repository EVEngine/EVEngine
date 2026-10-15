#include <zeroerr/unittest.h>
#include <string>
#include "editing/EditingResult.h"

TEST_CASE("editing.failureFactoryPreservesRuleAndStatus") {
    auto result = eve::editing::failed<int>(eve::StatusCode::Conflict, eve::DiagnosticCode::Conflict,
                                            eve::editing::RuleId("test.conflict"), "collision");
    CHECK(!result.ok());
    CHECK(result.code() == eve::StatusCode::Conflict);
    CHECK(result.diagnostics().size() == 1);
    const auto& diagnostic = result.diagnostics().front();
    CHECK(diagnostic.message() == "collision");
    CHECK(diagnostic.source() == "editing");
    CHECK(eve::editing::diagnosticRule(diagnostic).value() == "test.conflict");
}

TEST_CASE("editing.failureFactoryRejectsSuccessfulStatus") {
#if !defined(ZEROERR_NO_ASSERT)
    bool caught = false;
    try {
        auto result = eve::editing::failed<void>(eve::StatusCode::Applied, eve::editing::RuleId("test.invalid"),
                                                 "invalid category");
        result.ignore();
    } catch (const zeroerr::AssertionData& failure) {
        caught = true;
        CHECK(std::string(failure.file).find("EditingResult.h") != std::string::npos);
        CHECK(failure.cond == "eve::Status(status, {}).isFailure()");
        CHECK(!failure.passed);
    }
    CHECK(caught);
#else
    CHECK(true);
#endif
}

TEST_CASE("editing.ruleDiagnosticPreservesOwnedTextSeverityAndProjection") {
    std::string ruleText(4096, 'r');
    std::string message(4096, 'm');
    auto        diagnostic = eve::editing::ruleDiagnostic(eve::DiagnosticCode::Conflict, eve::editing::RuleId(ruleText),
                                                          eve::Severity::Warning, message);
    ruleText               = "changed";
    message                = "changed";
    CHECK(static_cast<bool>(diagnostic.code() == eve::DiagnosticCode::Conflict));
    CHECK(static_cast<bool>(diagnostic.severity() == eve::Severity::Warning));
    CHECK(diagnostic.message() == std::string(4096, 'm'));
    CHECK(diagnostic.path().empty());
    CHECK(diagnostic.source() == "editing");
    REQUIRE(diagnostic.details().size() == 1u);
    CHECK(diagnostic.details().front().first == "rule");
    CHECK(diagnostic.details().front().second == std::string(4096, 'r'));
    CHECK(eve::editing::diagnosticRule(diagnostic).value() == std::string(4096, 'r'));
}
