#pragma once

#include "common/Export.h"
#include "devtools/LanguageIndex.h"

#include <string>
#include <string_view>
#include <vector>

namespace eve::dev::lsp {

/** @brief Editor formatting preferences from LSP FormattingOptions. */
struct FormatOptions {
    unsigned tabSize      = 4;
    bool     insertSpaces = true;
};

/** @brief One foldable region (0-based inclusive end line). */
struct FoldingRange {
    size_t      startLine = 0;
    size_t      endLine   = 0;
    std::string kind;
};

/** @brief Converts an LSP position into a UTF-8 byte offset (same contract as the rest of the server). */
EVENGINE_API size_t offsetAt(std::string_view source, Position position) noexcept;

/** @brief Full-document range covering every character in source. */
EVENGINE_API Range documentRange(std::string_view source) noexcept;

/** @brief Last 0-based line index (empty source is line 0). */
EVENGINE_API size_t lastLineIndex(std::string_view source) noexcept;

/**
 * @brief Half-open LSP range covering `startLine` through `endLine` inclusive.
 * Ends at the start of the following line when one exists.
 */
EVENGINE_API Range coveringLines(std::string_view source, size_t startLine, size_t endLineInclusive) noexcept;

/** @brief Source bytes for `coveringLines` of the same line span. */
EVENGINE_API std::string sliceLines(std::string_view source, size_t startLine, size_t endLineInclusive);

/**
 * @brief Applies one LSP content change.
 * @param range nullptr replaces the whole document.
 * @return false when the range is out of bounds.
 */
EVENGINE_API bool applyIncremental(std::string& source, const Range* range, std::string_view text);

/** @brief Indents EveScript by braces/brackets and trims trailing whitespace. */
EVENGINE_API std::string formatEveScript(std::string_view source, const FormatOptions& options = {});

/** @brief Brace, bracket, and block-comment folds that span more than one line. */
EVENGINE_API std::vector<FoldingRange> foldingRanges(std::string_view source);

}  // namespace eve::dev::lsp
