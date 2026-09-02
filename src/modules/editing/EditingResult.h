#pragma once

#include "common/Assert.h"
#include "common/Result.h"
#include "editing/EditingIds.h"

#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace eve::editing {

using Status = eve::StatusCode;
using DiagnosticSeverity = eve::Severity;
using Diagnostic = eve::Diagnostic;

template <class T>
using Result = eve::Result<T>;

/** @brief Reserved DiagnosticDetails key containing an editing RuleId projection. */
inline constexpr const char* kRuleDiagnosticDetail = "rule";

/** @brief Build a common diagnostic carrying an open editing rule identity. */
[[nodiscard]] inline Diagnostic ruleDiagnostic(eve::DiagnosticCode code, RuleId rule,
                                               DiagnosticSeverity severity, std::string message) {
    eve::DiagnosticDetails details;
    details.emplace_back(kRuleDiagnosticDetail, rule.value());
    return Diagnostic(code, severity, std::move(message), {}, std::move(details), "editing");
}

/** @brief Return the projected editing rule, or an empty RuleId when absent. */
[[nodiscard]] inline RuleId diagnosticRule(const Diagnostic& diagnostic) {
    for (const auto& [key, value] : diagnostic.details())
        if (key == kRuleDiagnosticDetail) return RuleId(value);
    return {};
}

/** @brief Construct an Applied result with an owning payload and optional diagnostics. */
template <class T>
[[nodiscard]] Result<T> applied(T value, std::vector<Diagnostic> diagnostics = {}) {
    return Result<T>::success(std::move(value), eve::Status(Status::Applied, std::move(diagnostics)));
}

/** @brief Construct an Applied void result with optional diagnostics. */
template <class T = void>
    requires std::is_void_v<T>
[[nodiscard]] Result<void> applied(std::vector<Diagnostic> diagnostics = {}) {
    return Result<void>::success(eve::Status(Status::Applied, std::move(diagnostics)));
}

/** @brief Construct a successful void result for an operation that made no change. */
[[nodiscard]] inline Result<void> noOp(std::vector<Diagnostic> diagnostics = {}) {
    return Result<void>::success(eve::Status(Status::NoOp, std::move(diagnostics)));
}

/** @brief Construct a successful void result for accepted asynchronous work. */
[[nodiscard]] inline Result<void> pending(std::vector<Diagnostic> diagnostics = {}) {
    return Result<void>::success(eve::Status(Status::Pending, std::move(diagnostics)));
}

/** @brief Construct a failed editing result with explicit status and diagnostic categories. */
template <class T>
[[nodiscard]] Result<T> failed(Status status, eve::DiagnosticCode code, RuleId rule, std::string message) {
    EV_ASSERT(eve::Status(status, {}).isFailure(), "editing::failed requires a failure StatusCode");
    return Result<T>::failure(
        eve::Status::failure(status, ruleDiagnostic(code, std::move(rule), DiagnosticSeverity::Error,
                                                   std::move(message))));
}

/** @brief Coarse default diagnostic category for a failure status. */
[[nodiscard]] constexpr eve::DiagnosticCode diagnosticCodeForStatus(Status status) noexcept {
    switch (status) {
        case Status::Rejected: return eve::DiagnosticCode::PreconditionViolation;
        case Status::Conflict: return eve::DiagnosticCode::Conflict;
        case Status::NotFound: return eve::DiagnosticCode::NotFound;
        case Status::Unsupported: return eve::DiagnosticCode::Unsupported;
        case Status::Cancelled: return eve::DiagnosticCode::Cancelled;
        case Status::Failed: return eve::DiagnosticCode::Failed;
        case Status::Ok:
        case Status::Applied:
        case Status::NoOp:
        case Status::Pending: return eve::DiagnosticCode::None;
    }
    return eve::DiagnosticCode::Failed;
}

/** @brief Migration convenience using the canonical coarse category for the failure status. */
template <class T>
[[nodiscard]] Result<T> failed(Status status, RuleId rule, std::string message) {
    return failed<T>(status, diagnosticCodeForStatus(status), std::move(rule), std::move(message));
}

/** @brief Construct a Rejected result for invalid editing input or preconditions. */
template <class T>
[[nodiscard]] Result<T> rejected(RuleId rule, std::string message,
                                 eve::DiagnosticCode code = eve::DiagnosticCode::PreconditionViolation) {
    return failed<T>(Status::Rejected, code, std::move(rule), std::move(message));
}

}  // namespace eve::editing
