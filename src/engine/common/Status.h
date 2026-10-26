#pragma once

/**
 * @file Status.h
 * @brief Structured operation status used by the common Result foundation.
 */

#include "common/Diagnostic.h"
#include "common/Export.h"

#include <cstdint>
#include <iosfwd>
#include <memory>
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
        case StatusCode::Ok: return "ok";
        case StatusCode::Applied: return "applied";
        case StatusCode::NoOp: return "no_op";
        case StatusCode::Pending: return "pending";
        case StatusCode::Rejected: return "rejected";
        case StatusCode::Conflict: return "conflict";
        case StatusCode::NotFound: return "not_found";
        case StatusCode::Unsupported: return "unsupported";
        case StatusCode::Cancelled: return "cancelled";
        case StatusCode::Failed: return "failed";
    }
    return "unknown";
}

/** @brief Write the stable StatusCode spelling to a stream. */
EVENGINE_API_FOUNDATION std::ostream& operator<<(std::ostream& stream, StatusCode code);

/**
 * @brief Structured status and zero or more diagnostics for an operation.
 * @note A Status returned by value is itself a nodiscard value. Success and copying immutable storage allocate no
 * workspace.
 */
class [[nodiscard("Status must be inspected or explicitly passed onward")]] EVENGINE_API_FOUNDATION Status {
public:
    /** @brief Construct a successful `Ok` status with no diagnostics. */
    Status() noexcept;
    /** @brief Release the owned diagnostic list. */
    ~Status() noexcept;
    /** @brief Deep-copy the diagnostic list. */
    Status(const Status& other);
    /** @brief Replace the status by deep-copying its diagnostic list. */
    Status& operator=(const Status& other);
    /** @brief Transfer diagnostic ownership without throwing. */
    Status(Status&& other) noexcept;
    /** @brief Replace the status by transferring diagnostic ownership. */
    Status& operator=(Status&& other) noexcept;

    /**
     * @brief Construct a status from a code and diagnostics.
     * @param code Stable operation outcome.
     * @param diagnostics Structured explanations and context.
     */
    Status(StatusCode code, std::vector<Diagnostic> diagnostics);

    /** @brief Construct a successful status with an explicit non-error outcome. */
    static Status success(StatusCode code = StatusCode::Ok);

    /** @brief Construct a failed status with one diagnostic. */
    static Status failure(StatusCode code, Diagnostic diagnostic);

    /** @brief Construct the matching failure category from a diagnostic code. */
    static Status failure(Diagnostic diagnostic);

    /** @brief Stable operation outcome. */
    StatusCode code() const noexcept { return code_; }

    /** @brief Whether the operation completed without a failure outcome. */
    bool isSuccess() const noexcept {
        switch (code_) {
            case StatusCode::Ok:
            case StatusCode::Applied:
            case StatusCode::NoOp:
            case StatusCode::Pending: return true;
            case StatusCode::Rejected:
            case StatusCode::Conflict:
            case StatusCode::NotFound:
            case StatusCode::Unsupported:
            case StatusCode::Cancelled:
            case StatusCode::Failed: return false;
        }
        return false;
    }

    /** @brief Whether the operation has a failure outcome. */
    bool isFailure() const noexcept { return !isSuccess(); }

    /** @brief True when this status has at least one diagnostic. */
    bool hasDiagnostics() const noexcept { return diagnostics_ && !diagnostics_->empty(); }

    /** @brief All diagnostics in stable insertion order. */
    const std::vector<Diagnostic>& diagnostics() const noexcept {
        return diagnostics_ ? *diagnostics_ : emptyDiagnostics();
    }

    /**
     * @brief First diagnostic, or null when no diagnostic was supplied.
     * @return Borrowed pointer into this Status; nullptr when diagnostics() is empty.
     * @ownership Borrowed; Status retains immutable shared diagnostic storage.
     * @nullable Yes.
     * @lifetime Valid until this Status is destroyed or its diagnostic storage is replaced.
     * @thread Affine to this Status; no synchronization.
     * @reentrancy Does not invoke callbacks.
     */
    const Diagnostic* primaryDiagnostic() const noexcept { return hasDiagnostics() ? &diagnostics_->front() : nullptr; }

    /**
     * @brief Render a compact human-readable summary.
     * @return Status code followed by the first diagnostic message, if any.
     */
    std::string describe() const;

private:
    static StatusCode statusCodeFor(DiagnosticCode code) noexcept {
        switch (code) {
            case DiagnosticCode::InvalidArgument:
            case DiagnosticCode::PreconditionViolation: return StatusCode::Rejected;
            case DiagnosticCode::NotFound: return StatusCode::NotFound;
            case DiagnosticCode::AlreadyExists:
            case DiagnosticCode::Conflict: return StatusCode::Conflict;
            case DiagnosticCode::Unsupported: return StatusCode::Unsupported;
            case DiagnosticCode::Cancelled: return StatusCode::Cancelled;
            case DiagnosticCode::StaleHandle: return StatusCode::Rejected;
            case DiagnosticCode::TypeMismatch: return StatusCode::Rejected;
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
            case DiagnosticCode::None: return StatusCode::Failed;
            case DiagnosticCode::DialogueRouteNotFound: return StatusCode::NotFound;
        }
        return StatusCode::Failed;
    }

    StatusCode              code_ = StatusCode::Ok;
    std::shared_ptr<const std::vector<Diagnostic>> diagnostics_;
    static const std::vector<Diagnostic>&          emptyDiagnostics() noexcept;
};

}  // namespace eve
