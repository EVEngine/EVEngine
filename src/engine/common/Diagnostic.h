#pragma once

/**
 * @file Diagnostic.h
 * @brief Stable, structured diagnostics shared by engine modules.
 */

#include "common/Export.h"

#include <cstdint>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace eve {

/** @brief Stable severity of a diagnostic, independent of its display text. */
enum class Severity : uint8_t {
    Info,
    Warning,
    Error,
    Fatal,
};

/** @brief Returns the stable lowercase severity spelling. */
[[nodiscard]] constexpr std::string_view severityName(Severity severity) noexcept {
    switch (severity) {
        case Severity::Info: return "info";
        case Severity::Warning: return "warning";
        case Severity::Error: return "error";
        case Severity::Fatal: return "fatal";
    }
    return "unknown";
}

/** @brief Writes a severity for diagnostics and test output. */
inline std::ostream& operator<<(std::ostream& stream, Severity severity) { return stream << severityName(severity); }

/**
 * @brief Stable machine-readable diagnostic codes.
 *
 * Numeric values are part of the public contract. Messages are intended for
 * people and must not be used as program-branch conditions.
 */
enum class DiagnosticCode : uint32_t {
    None                  = 0,
    InvalidArgument       = 1,
    PreconditionViolation = 2,
    NotFound              = 3,
    AlreadyExists         = 4,
    Conflict              = 5,
    Unsupported           = 6,
    Cancelled             = 7,
    StaleHandle           = 8,
    ParseError            = 9,
    SerializationError    = 10,
    InvariantViolation    = 11,
    Failed                = 12,
    HashMismatch          = 13,
    UnknownVersion        = 14,
    CallbackFailure       = 15,
    /** @brief Dialogue runner is not positioned at a choice suspension point. */
    DialogueNotWaitingForChoice = 16,
    /** @brief Dialogue runner is not positioned at an asynchronous command. */
    DialogueNotWaitingForCommand = 17,
    /** @brief A dialogue condition rejected the requested route. */
    DialogueConditionRejected = 18,
    /** @brief A requested dialogue route does not exist on the current node. */
    DialogueRouteNotFound = 19,
    /** @brief Mesh group sidecar data is inconsistent with its dense streams. */
    ProcgenGroupDataInvalid = 20,
};

/**
 * @brief A structured detail attached to a Diagnostic.
 *
 * Keys and values are intentionally strings at this foundation layer. Domain
 * modules can project their canonical Value type into this representation
 * without making common depend on data, graphics, physics, or scripting.
 */
using DiagnosticDetail = std::pair<std::string, std::string>;

/** @brief Owning collection of diagnostic details with stable insertion order. */
using DiagnosticDetails = std::vector<DiagnosticDetail>;

/**
 * @brief A structured explanation of a failed, degraded, or noteworthy result.
 * @note `message()` is for humans; use `code()` for program decisions.
 */
class [[nodiscard("Diagnostic should be inspected or passed to a Result")]] EVENGINE_API Diagnostic {
public:
    /** @brief Construct an empty informational diagnostic. */
    Diagnostic() = default;

    /**
     * @brief Construct a diagnostic with its stable code and human text.
     * @param code Stable machine-readable code.
     * @param severity Display and logging severity.
     * @param message Human-readable explanation; may be empty.
     * @param path Optional field, property, URI, or logical path involved.
     * @param details Optional structured context.
     * @param source Optional producer/boundary name, such as a Squirrel
     *               binding or a serialized document importer.
     */
    Diagnostic(DiagnosticCode code, Severity severity, std::string message, std::string path = {},
               DiagnosticDetails details = {}, std::string source = {})
        : code_(code),
          severity_(severity),
          message_(std::move(message)),
          path_(std::move(path)),
          details_(std::move(details)),
          source_(std::move(source)) {}

    /** @brief Construct an error diagnostic with the standard error severity. */
    static Diagnostic error(DiagnosticCode code, std::string message, std::string path = {},
                            DiagnosticDetails details = {}, std::string source = {}) {
        return Diagnostic(code, Severity::Error, std::move(message), std::move(path), std::move(details),
                          std::move(source));
    }

    /** @brief Construct a warning diagnostic. */
    static Diagnostic warning(DiagnosticCode code, std::string message, std::string path = {},
                              DiagnosticDetails details = {}, std::string source = {}) {
        return Diagnostic(code, Severity::Warning, std::move(message), std::move(path), std::move(details),
                          std::move(source));
    }

    /** @brief Stable machine-readable code. */
    DiagnosticCode code() const noexcept { return code_; }
    /** @brief Severity for logging and presentation. */
    Severity severity() const noexcept { return severity_; }
    /** @brief Human-readable explanation. */
    const std::string& message() const noexcept { return message_; }
    /** @brief Optional field, property, URI, or logical path. */
    const std::string& path() const noexcept { return path_; }
    /** @brief Structured context in insertion order. */
    const DiagnosticDetails& details() const noexcept { return details_; }
    /** @brief Optional producer or subsystem that emitted the diagnostic. */
    const std::string& source() const noexcept { return source_; }

    /** @brief Add structured context without changing the stable code. */
    void addDetail(std::string key, std::string value) { details_.emplace_back(std::move(key), std::move(value)); }

    /** @brief Whether the diagnostic represents an error or fatal condition. */
    bool isError() const noexcept { return severity_ == Severity::Error || severity_ == Severity::Fatal; }

private:
    DiagnosticCode    code_     = DiagnosticCode::None;
    Severity          severity_ = Severity::Info;
    std::string       message_;
    std::string       path_;
    DiagnosticDetails details_;
    std::string       source_;
};

/**
 * @brief Return a stable, compact spelling useful for logs and diagnostics.
 * @return Borrowed static text for `code`; unknown values return `"unknown"`.
 * @thread Thread-safe and side-effect free.
 * @reentrancy Does not invoke callbacks or alter diagnostic state.
 */
[[nodiscard]] constexpr std::string_view diagnosticCodeName(DiagnosticCode code) noexcept {
    switch (code) {
        case DiagnosticCode::None: return "none";
        case DiagnosticCode::InvalidArgument: return "invalid_argument";
        case DiagnosticCode::PreconditionViolation: return "precondition_violation";
        case DiagnosticCode::NotFound: return "not_found";
        case DiagnosticCode::AlreadyExists: return "already_exists";
        case DiagnosticCode::Conflict: return "conflict";
        case DiagnosticCode::Unsupported: return "unsupported";
        case DiagnosticCode::Cancelled: return "cancelled";
        case DiagnosticCode::StaleHandle: return "stale_handle";
        case DiagnosticCode::ParseError: return "parse_error";
        case DiagnosticCode::SerializationError: return "serialization_error";
        case DiagnosticCode::InvariantViolation: return "invariant_violation";
        case DiagnosticCode::Failed: return "failed";
        case DiagnosticCode::HashMismatch: return "hash_mismatch";
        case DiagnosticCode::UnknownVersion: return "unknown_version";
        case DiagnosticCode::CallbackFailure: return "callback_failure";
        case DiagnosticCode::DialogueNotWaitingForChoice: return "dialogue_not_waiting_for_choice";
        case DiagnosticCode::DialogueNotWaitingForCommand: return "dialogue_not_waiting_for_command";
        case DiagnosticCode::DialogueConditionRejected: return "dialogue_condition_rejected";
        case DiagnosticCode::DialogueRouteNotFound: return "dialogue_route_not_found";
        case DiagnosticCode::ProcgenGroupDataInvalid: return "procgen_group_data_invalid";
    }
    return "unknown";
}

/** @brief Writes the stable diagnostic-code spelling to a stream. */
inline std::ostream& operator<<(std::ostream& stream, DiagnosticCode code) {
    return stream << diagnosticCodeName(code);
}

}  // namespace eve
