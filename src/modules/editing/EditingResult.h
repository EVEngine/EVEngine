#pragma once

#include "editing/EditingIds.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace eve::editing {

/** @brief Stable outcome category for authoring operations and services. */
enum class Status { Applied, Pending, NoOp, Rejected, Conflict, NotFound, Unsupported, Cancelled, Failed };

/** @brief Severity of a structured authoring diagnostic. */
enum class DiagnosticSeverity { Info, Warning, Error };

/** @brief Human-readable diagnostic with a stable rule identity. */
struct Diagnostic {
    RuleId             rule;
    DiagnosticSeverity severity = DiagnosticSeverity::Error;
    std::string        message;
};

/**
 * @brief Structured result returned by authoring operations and services.
 * @tparam T Successful owning value type.
 */
template <class T>
struct [[nodiscard]] Result {
    Status                  status = Status::Failed;
    std::optional<T>        value;
    std::vector<Diagnostic> diagnostics;

    /** @brief True when the request was accepted by the service. */
    bool isAccepted() const { return status == Status::Applied || status == Status::Pending || status == Status::NoOp; }

    /** @brief Construct a successful result containing a value. */
    static Result applied(T result) {
        Result out;
        out.status = Status::Applied;
        out.value  = std::move(result);
        return out;
    }

    /** @brief Construct a result with one error diagnostic. */
    static Result error(Status resultStatus, RuleId rule, std::string message) {
        Result out;
        out.status = resultStatus;
        out.diagnostics.push_back({std::move(rule), DiagnosticSeverity::Error, std::move(message)});
        return out;
    }
};

/** @brief Void specialization for authoring operations with no return payload. */
template <>
struct [[nodiscard]] Result<void> {
    Status                  status = Status::Failed;
    std::vector<Diagnostic> diagnostics;

    /** @brief True when the request was accepted by the service. */
    bool isAccepted() const { return status == Status::Applied || status == Status::Pending || status == Status::NoOp; }

    /** @brief Construct a successful result. */
    static Result applied() {
        Result out;
        out.status = Status::Applied;
        return out;
    }

    /** @brief Construct a result with one error diagnostic. */
    static Result error(Status resultStatus, RuleId rule, std::string message) {
        Result out;
        out.status = resultStatus;
        out.diagnostics.push_back({std::move(rule), DiagnosticSeverity::Error, std::move(message)});
        return out;
    }
};

}  // namespace eve::editing
