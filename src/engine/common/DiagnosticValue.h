#pragma once

/**
 * @file DiagnosticValue.h
 * @brief Compile-time contract for stable names and codes of domain values.
 */

#include "common/Diagnostic.h"
#include "common/Status.h"

#include <concepts>
#include <string_view>
#include <type_traits>

namespace eve {

/** @brief Customization point for values that cross diagnostic boundaries. */
template <class T>
struct DiagnosticValueTraits;

/**
 * @brief A value with a stable protocol name and machine-readable diagnostic code.
 * @note Specializations must be pure, thread-safe, and return static text.
 */
template <class T>
concept DiagnosticValue = std::is_enum_v<T> && requires(T value) {
    { DiagnosticValueTraits<T>::name(value) } noexcept -> std::convertible_to<std::string_view>;
    { DiagnosticValueTraits<T>::code(value) } noexcept -> std::same_as<DiagnosticCode>;
};

/** @brief Return the stable name supplied by a DiagnosticValue specialization. */
template <DiagnosticValue T>
[[nodiscard]] constexpr std::string_view diagnosticValueName(T value) noexcept {
    return DiagnosticValueTraits<T>::name(value);
}

/** @brief Return the stable machine-readable code supplied by a DiagnosticValue specialization. */
template <DiagnosticValue T>
[[nodiscard]] constexpr DiagnosticCode diagnosticValueCode(T value) noexcept {
    return DiagnosticValueTraits<T>::code(value);
}

/** @brief DiagnosticValue mapping for diagnostic severities. */
template <>
struct DiagnosticValueTraits<Severity> {
    static constexpr std::string_view name(Severity value) noexcept { return severityName(value); }
    static constexpr DiagnosticCode   code(Severity value) noexcept {
        return value == Severity::Error || value == Severity::Fatal ? DiagnosticCode::Failed : DiagnosticCode::None;
    }
};

/** @brief DiagnosticValue mapping for machine-readable diagnostic codes. */
template <>
struct DiagnosticValueTraits<DiagnosticCode> {
    static constexpr std::string_view name(DiagnosticCode value) noexcept { return diagnosticCodeName(value); }
    static constexpr DiagnosticCode   code(DiagnosticCode value) noexcept { return value; }
};

/** @brief DiagnosticValue mapping for operation status categories. */
template <>
struct DiagnosticValueTraits<StatusCode> {
    static constexpr std::string_view name(StatusCode value) noexcept { return statusCodeName(value); }
    static constexpr DiagnosticCode   code(StatusCode value) noexcept {
        switch (value) {
            case StatusCode::Ok:
            case StatusCode::Applied:
            case StatusCode::NoOp:
            case StatusCode::Pending: return DiagnosticCode::None;
            case StatusCode::Rejected: return DiagnosticCode::PreconditionViolation;
            case StatusCode::Conflict: return DiagnosticCode::Conflict;
            case StatusCode::NotFound: return DiagnosticCode::NotFound;
            case StatusCode::Unsupported: return DiagnosticCode::Unsupported;
            case StatusCode::Cancelled: return DiagnosticCode::Cancelled;
            case StatusCode::Failed: return DiagnosticCode::Failed;
        }
        return DiagnosticCode::Failed;
    }
};

}  // namespace eve
