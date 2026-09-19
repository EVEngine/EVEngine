#pragma once

/** @file DnutDiagnostic.h @brief Domain-neutral source diagnostics for `.dnut` documents. */

#include <cstdint>
#include <string>
#include <vector>

namespace eve::dnut {

/** @brief Severity of one `.dnut` source diagnostic. */
enum class DnutSeverity : std::uint8_t { Warning, Error };

/**
 * @brief One structured diagnostic anchored to a `.dnut` source location.
 *
 * `line` and `column` are one-based. A zero location means "the whole document"
 * and is used by asset-level checks that have no single source line.
 */
struct DnutDiagnostic {
    DnutSeverity severity = DnutSeverity::Error;
    /** @brief Source path supplied by the caller; never invented by the frontend. */
    std::string path;
    /** @brief One-based source line, or zero for a document-level diagnostic. */
    int line = 0;
    /** @brief One-based source column, or zero when unknown. */
    int column = 0;
    /** @brief Human-readable explanation; never used for program decisions. */
    std::string message;

    bool operator==(const DnutDiagnostic&) const = default;
};

/**
 * @brief Return whether any diagnostic is an error.
 * @param diagnostics Diagnostics collected by one frontend invocation.
 * @return True when at least one entry has error severity.
 * @thread Reentrant and side-effect free.
 */
[[nodiscard]] bool hasErrors(const std::vector<DnutDiagnostic>& diagnostics);

}  // namespace eve::dnut
