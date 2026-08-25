#pragma once

#include "editor/EditorIds.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace eve::editor {

/** @brief Stable outcome category for commands and editor services. */
enum class EditorStatus { Applied, Pending, NoOp, Rejected, Conflict, NotFound, Unsupported, Cancelled, Failed };

/** @brief Severity of a structured editor diagnostic. */
enum class DiagnosticSeverity { Info, Warning, Error };

/** @brief Human-readable diagnostic with a stable rule identity. */
struct EditorDiagnostic {
    RuleId             rule;
    DiagnosticSeverity severity = DiagnosticSeverity::Error;
    std::string        message;
};

/**
 * @brief Structured result returned by editor commands and services.
 * @tparam T Optional successful value type.
 */
template <class T>
struct EditorResult {
    EditorStatus                  status = EditorStatus::Failed;
    std::optional<T>              value;
    std::vector<EditorDiagnostic> diagnostics;

    /** @brief True when the request was accepted by the service. */
    bool accepted() const {
        return status == EditorStatus::Applied || status == EditorStatus::Pending || status == EditorStatus::NoOp;
    }

    /** @brief Construct a successful result containing a value. */
    static EditorResult applied(T result) {
        EditorResult out;
        out.status = EditorStatus::Applied;
        out.value  = std::move(result);
        return out;
    }

    /** @brief Construct a result with one error diagnostic. */
    static EditorResult error(EditorStatus resultStatus, RuleId rule, std::string message) {
        EditorResult out;
        out.status = resultStatus;
        out.diagnostics.push_back({std::move(rule), DiagnosticSeverity::Error, std::move(message)});
        return out;
    }
};

/** @brief Void specialization for editor operations with no return payload. */
template <>
struct EditorResult<void> {
    EditorStatus                  status = EditorStatus::Failed;
    std::vector<EditorDiagnostic> diagnostics;

    /** @brief True when the request was accepted by the service. */
    bool accepted() const {
        return status == EditorStatus::Applied || status == EditorStatus::Pending || status == EditorStatus::NoOp;
    }

    /** @brief Construct a successful result. */
    static EditorResult applied() {
        EditorResult out;
        out.status = EditorStatus::Applied;
        return out;
    }

    /** @brief Construct a result with one error diagnostic. */
    static EditorResult error(EditorStatus resultStatus, RuleId rule, std::string message) {
        EditorResult out;
        out.status = resultStatus;
        out.diagnostics.push_back({std::move(rule), DiagnosticSeverity::Error, std::move(message)});
        return out;
    }
};

}  // namespace eve::editor
