#include "editing/EditingResult.h"

namespace eve::editing::detail {

eve::Status makeFailureStatus(Status status, eve::DiagnosticCode code, RuleId rule, std::string message,
                              const char* file, unsigned line, bool assertionsEnabled) {
    if (assertionsEnabled) {
        const bool isFailure = eve::Status(status, {}).isFailure();
        eve::detail::checkResultInvariant(isFailure, "eve::Status(status, {}).isFailure()", file, line,
                                          "editing::failed requires a failure StatusCode");
    }
    return eve::Status::failure(status,
                                ruleDiagnostic(code, std::move(rule), DiagnosticSeverity::Error, std::move(message)));
}

}  // namespace eve::editing::detail

namespace eve::editing {

[[nodiscard]] Diagnostic ruleDiagnostic(eve::DiagnosticCode code, RuleId rule, DiagnosticSeverity severity,
                                        std::string message) {
    eve::DiagnosticDetails details;
    details.emplace_back(kRuleDiagnosticDetail, rule.value());
    /** @brief Diagnostic. */
    return Diagnostic(code, severity, std::move(message), {}, std::move(details), "editing");
}

}  // namespace eve::editing
