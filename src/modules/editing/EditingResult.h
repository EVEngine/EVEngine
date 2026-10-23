#pragma once

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

/**
 * @brief Build an owning common diagnostic carrying an open editing rule identity.
 * @param code Stable diagnostic category.
 * @param rule Owned identity whose text is copied into the rule detail.
 * @param severity Diagnostic severity.
 * @param message Owned explanation transferred into the diagnostic.
 * @return Independent diagnostic with the rule detail and editing source.
 * @cost Copies rule text and allocates detail storage once per diagnostic construction.
 * @thread Synchronous on the caller thread; no shared mutable state.
 * @reentrancy Invokes no callbacks.
 */
[[nodiscard]] EVENGINE_API_PLATFORM Diagnostic ruleDiagnostic(eve::DiagnosticCode code, RuleId rule,
                                                              DiagnosticSeverity severity, std::string message);

/** @brief Return the projected editing rule, or an empty RuleId when absent. */
[[nodiscard]] inline RuleId diagnosticRule(const Diagnostic& diagnostic) {
    for (const auto& [key, value] : diagnostic.details())
        if (key == kRuleDiagnosticDetail) return RuleId(value);
    return {};
}

/** @brief Construct an Applied result with an owning payload and optional diagnostics. */
template <class T>
/** @brief Applied. */
[[nodiscard]] Result<T> applied(T value, std::vector<Diagnostic> diagnostics = {}) {
    /** @brief Success. */
    return Result<T>::success(std::move(value), eve::Status(Status::Applied, std::move(diagnostics)));
}

/** @brief Construct an Applied void result with optional diagnostics. */
template <class T = void>
    requires std::is_void_v<T>
/** @brief Applied. */
[[nodiscard]] Result<void> applied(std::vector<Diagnostic> diagnostics = {}) {
    /** @brief Success. */
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

namespace detail {
/**
 * @brief Build the canonical editing failure status outside payload template instantiations.
 * @param status Failure category.
 * @param code Diagnostic category.
 * @param rule Owned rule identity.
 * @param message Owned diagnostic message.
 * @param file Static-lifetime source file name used by the assertion exception.
 * @param line Source line containing the check.
 * @param assertionsEnabled Assertion policy selected by the calling translation unit.
 * @return Owning failure status with the rule projection and editing source.
 * @throws zeroerr::AssertionData if assertions are enabled and status is successful.
 * @note Synchronous on the caller thread; only the static file name is borrowed by the exception.
 * @cost Allocates diagnostic strings and detail storage once per failure construction.
 */
[[nodiscard]] EVENGINE_API_PLATFORM eve::Status makeFailureStatus(Status status, eve::DiagnosticCode code, RuleId rule,
                                                                  std::string message, const char* file, unsigned line,
                                                                  bool assertionsEnabled);
}  // namespace detail

/** @brief Construct a failed editing result with explicit status and diagnostic categories. */
template <class T>
/** @brief Failed. */
[[nodiscard]] Result<T> failed(Status status, eve::DiagnosticCode code, RuleId rule, std::string message) {
#if !defined(ZEROERR_NO_ASSERT)
    constexpr bool assertionsEnabled = true;
#else
    constexpr bool assertionsEnabled = false;
#endif
    return Result<T>::failure(detail::makeFailureStatus(status, code, std::move(rule), std::move(message), __FILE__,
                                                        __LINE__, assertionsEnabled));
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
/** @brief Failed. */
[[nodiscard]] Result<T> failed(Status status, RuleId rule, std::string message) {
    return failed<T>(status, diagnosticCodeForStatus(status), std::move(rule), std::move(message));
}

/** @brief Construct a Rejected result for invalid editing input or preconditions. */
template <class T>
/** @brief Rejected. */
[[nodiscard]] Result<T> rejected(RuleId rule, std::string message,
                                 eve::DiagnosticCode code = eve::DiagnosticCode::PreconditionViolation) {
    return failed<T>(Status::Rejected, code, std::move(rule), std::move(message));
}

}  // namespace eve::editing
