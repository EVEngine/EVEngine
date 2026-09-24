#pragma once
#include "common/Export.h"


/** @file DnutCompiler.h @brief Registry-driven compiler for the `.dnut` story dialect. */

#include "dnut_interpreter/DnutDiagnostic.h"
#include "dnut_interpreter/SequenceAsset.h"
#include "dnut_interpreter/StepKindRegistry.h"

#include <string>
#include <string_view>
#include <vector>

namespace eve::dnut {

/**
 * @brief Owning outcome of compiling one `.dnut` document.
 *
 * Compilation is total: it reports every recoverable problem through
 * `diagnostics` instead of stopping at the first one, so a tool or editor can
 * present the whole list. Call `hasErrors` before publishing `assets`.
 */
struct EVENGINE_API_PLATFORM DnutCompileOutput {
    std::vector<SequenceAsset>  assets;
    std::vector<DnutDiagnostic> diagnostics;

    /** @brief Return whether any collected diagnostic has error severity. */
    [[nodiscard]] bool hasErrors() const;
};

/**
 * @brief Compile every `story` block of one `.dnut` document.
 *
 * Top-level blocks belonging to another dialect (for example `pool` or
 * `conversation`) are skipped, so one file may hold several dialects without
 * this compiler rejecting the file. Step payloads are validated against
 * `registry`, which is the single source of truth for the step vocabulary: the
 * compiler knows only the control-flow keywords `if` / `choice` / `call` /
 * `wait` / `end`.
 *
 * @param source Full UTF-8 document text; it is not retained.
 * @param path Source identity reported in every diagnostic.
 * @param registry Step vocabulary used to validate non-control-flow steps.
 * @return Owned assets and diagnostics. An input containing errors still yields
 *         the assets that compiled cleanly, so an editor can keep working.
 * @thread Reentrant and side-effect free; the caller owns the result.
 * @reentrancy Invokes no callbacks.
 * @cost Linear in the document size; allocates one token buffer and one asset
 *       per `story` block.
 */
[[nodiscard]] EVENGINE_API_PLATFORM DnutCompileOutput compileDnut(std::string_view source, const std::string& path,
                                                                  const StepKindRegistry& registry);

}  // namespace eve::dnut
