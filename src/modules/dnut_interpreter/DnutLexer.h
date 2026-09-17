#pragma once

/** @file DnutLexer.h @brief Shared lexer for every `.dnut` dialect. */

#include "common/Result.h"
#include "dnut_interpreter/DnutDiagnostic.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace eve::dnut {

/** @brief Token classes produced by the shared `.dnut` lexer. */
enum class DnutTokenKind : std::uint8_t {
    /** @brief Bare word: keyword, field name, identifier, or bare enum value. */
    Identifier,
    /** @brief Quoted string with escapes already resolved. */
    String,
    /** @brief Unsigned numeric literal; sign handling belongs to the parser. */
    Number,
    /** @brief One- or two-character punctuator such as `{`, `=`, `==`, `->`. */
    Punctuator,
    /** @brief Synthetic terminal token; always the last element. */
    EndOfFile,
};

/** @brief One lexed token with its exact source anchor. */
struct DnutToken {
    DnutTokenKind kind = DnutTokenKind::EndOfFile;
    std::string text;
    /** @brief One-based source line. */
    int line = 1;
    /** @brief One-based source column of the first character. */
    int column = 1;
};

/**
 * @brief Lex one `.dnut` source buffer into owned tokens.
 *
 * Every dialect shares this lexer so a single file may hold several dialects
 * without a second, silently divergent tokenizer.
 *
 * @param source Full UTF-8 source text; it is not retained.
 * @param path Source identity reported in diagnostics and in the error path.
 * @return Owned tokens ending in one `EndOfFile` token, or a `ParseError`
 *         diagnostic naming the line and column of the offending text.
 * @thread Reentrant and side-effect free; the caller owns the returned buffer.
 * @reentrancy Invokes no callbacks.
 */
[[nodiscard]] eve::Result<std::vector<DnutToken>> lexDnut(std::string_view source, const std::string& path);

/** @brief Return the stable lowercase spelling of a token kind. */
[[nodiscard]] const char* dnutTokenKindName(DnutTokenKind kind) noexcept;

}  // namespace eve::dnut
