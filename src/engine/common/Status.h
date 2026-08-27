#pragma once

/**
 * @file Status.h
 * @brief Structured operation status used by the common Result foundation.
 */

#include "common/Diagnostic.h"
#include "common/Export.h"

#include <cstdint>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace eve {

/**
 * @brief Stable outcome category for an operation.
 *
 * `Ok`, `Applied`, `NoOp`, and `Pending` are non-failure outcomes. The other
 * values require the caller to inspect the diagnostics or otherwise make an
 * explicit policy decision.
 */
enum class StatusCode : uint8_t {
    Ok,
    Applied,
    NoOp,
    Pending,
    Rejected,
    Conflict,
    NotFound,
    Unsupported,
    Cancelled,
    Failed,
};

/**
 * @brief Return the stable protocol spelling of a status code.
 * @param code Status category to name.
 * @return Static non-owning text; unknown values return `"unknown"`.
 */
[[nodiscard]] constexpr std::string_view statusCodeName(StatusCode code) noexcept {
    switch (code) {
    case StatusCode::Ok:
        return "ok";
    case StatusCode::Applied:
        return "applied";
    case StatusCode::NoOp:
        return "no_op";
    case StatusCode::Pending:
        return "pending";
    case StatusCode::Rejected:
        return "rejected";
    case StatusCode::Conflict:
        return "conflict";
    case StatusCode::NotFound:
        return "not_found";
    case StatusCode::Unsupported:
        return "unsupported";
    case StatusCode::Cancelled:
        return "cancelled";
    case StatusCode::Failed:
        return "failed";
    }
    return "unknown";
}

/** @brief Write the stable StatusCode spelling to a stream. */
inline std::ostream& operator<<(std::ostream& stream, StatusCode code) {
    return stream << statusCodeName(code);
}

/**
 * @brief Structured status and zero or more diagnostics for an operation.
 * @note A Status returned by value is itself a nodiscard value.
 */
class [[nodiscard("Status must be inspected or explicitly passed onward")]] EVENGINE_API Status {
public:
    /** @brief Construct a successful `Ok` status with no diagnostics. */
    Status() = default;

    /**
     * @brief Construct a status from a code and diagnostics.
     * @param code Stable operation outcome.
     * @param diagnostics Structured explanations and context.
     */
    Status(StatusCode code, std::vector<Diagnostic> diagnostics)
        : code_(code), diagnostics_(std::move(diagnostics)) {}

    /** @brief Construct a successful status with an explicit non-error outcome. */
    static Status success(StatusCode code = StatusCode::Ok) {
        return Status(code, {});
    }

    /** @brief Construct a failed status with one diagnostic. */
    static Status failure(StatusCode code, Diagnostic diagnostic) {
        std::vector<Diagnostic> diagnostics;
        diagnostics.emplace_back(std::move(diagnostic));
        return Status(code, std::move(diagnostics));
    }

    /** @brief Construct the matching failure category from a diagnostic code. */
    static Status failure(Diagnostic diagnostic) {
        return failure(statusCodeFor(diagnostic.code()), std::move(diagnostic));
    }

    /** @brief Stable operation outcome. */
    StatusCode code() const noexcept { return code_; }

    /** @brief Whether the operation completed without a failure outcome. */
    bool isSuccess() const noexcept {
        switch (code_) {
        case StatusCode::Ok:
        case StatusCode::Applied:
        case StatusCode::NoOp:
        case StatusCode::Pending:
            return true;
        case StatusCode::Rejected:
        case StatusCode::Conflict:
        case StatusCode::NotFound:
        case StatusCode::Unsupported:
        case StatusCode::Cancelled:
        case StatusCode::Failed:
            return false;
        }
        return false;
    }

    /** @brief Whether the operation has a failure outcome. */
    bool isFailure() const noexcept { return !isSuccess(); }

    /** @brief True when this status has at least one diagnostic. */
    bool hasDiagnostics() const noexcept { return !diagnostics_.empty(); }

    /** @brief All diagnostics in stable insertion order. */
    const std::vector<Diagnostic>& diagnostics() const noexcept { return diagnostics_; }

    /**
     * @brief First diagnostic, or null when no diagnostic was supplied.
     * @return Borrowed pointer into this Status; nullptr when diagnostics() is empty.
     * @ownership Borrowed; Status owns the diagnostic vector.
     * @nullable Yes.
     * @lifetime Valid until this Status is destroyed or its diagnostic storage is replaced.
     * @thread Affine to this Status; no synchronization.
     * @reentrancy Does not invoke callbacks.
     */
    const Diagnostic* primaryDiagnostic() const noexcept {
        return diagnostics_.empty() ? nullptr : &diagnostics_.front();
    }

    /**
     * @brief Render a compact human-readable summary.
     * @return Status code followed by the first diagnostic message, if any.
     */
    std::string describe() const {
        std::string result(statusCodeName(code_));
        if (const Diagnostic* diagnostic = primaryDiagnostic()) {
            result += ": ";
            result += diagnostic->message();
            if (!diagnostic->path().empty()) {
                result += " [";
                result += diagnostic->path();
                result += ']';
            }
        }
        return result;
    }

private:
    static StatusCode statusCodeFor(DiagnosticCode code) noexcept {
        switch (code) {
        case DiagnosticCode::InvalidArgument:
        case DiagnosticCode::PreconditionViolation:
            return StatusCode::Rejected;
        case DiagnosticCode::NotFound:
            return StatusCode::NotFound;
        case DiagnosticCode::AlreadyExists:
        case DiagnosticCode::Conflict:
            return StatusCode::Conflict;
        case DiagnosticCode::Unsupported:
            return StatusCode::Unsupported;
        case DiagnosticCode::Cancelled:
            return StatusCode::Cancelled;
        case DiagnosticCode::StaleHandle:
            return StatusCode::Rejected;
        case DiagnosticCode::ParseError:
        case DiagnosticCode::SerializationError:
        case DiagnosticCode::InvariantViolation:
        case DiagnosticCode::Failed:
        case DiagnosticCode::HashMismatch:
        case DiagnosticCode::UnknownVersion:
        case DiagnosticCode::CallbackFailure:
        case DiagnosticCode::DialogueNotWaitingForChoice:
        case DiagnosticCode::DialogueNotWaitingForCommand:
        case DiagnosticCode::DialogueConditionRejected:
        case DiagnosticCode::ProcgenGroupDataInvalid:
        case DiagnosticCode::None:
            return StatusCode::Failed;
        case DiagnosticCode::DialogueRouteNotFound:
            return StatusCode::NotFound;
        }
        return StatusCode::Failed;
    }

    StatusCode              code_ = StatusCode::Ok;
    std::vector<Diagnostic> diagnostics_;
};

}  // namespace eve
