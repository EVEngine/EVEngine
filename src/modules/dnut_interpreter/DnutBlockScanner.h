#pragma once

/** @file DnutBlockScanner.h @brief Shared top-level block discovery for `.dnut` dialects. */

#include "common/Export.h"
#include "common/Result.h"
#include "dnut_interpreter/DnutLexer.h"

#include <cstddef>
#include <string>
#include <vector>

namespace eve::dnut {

/**
 * @brief One top-level dialect block in a shared token buffer.
 *
 * `beginToken` names the dialect keyword and `endToken` is one past the
 * matching closing brace. The token buffer remains caller-owned.
 */
struct DnutBlock {
    std::string kind;
    std::string id;
    std::size_t beginToken = 0;
    std::size_t endToken   = 0;
    int         line       = 1;
    int         column     = 1;
};

/**
 * @brief Discover balanced top-level dialect blocks in one lexed document.
 * @param tokens Borrowed tokens returned by `lexDnut`.
 * @param path Source identity used by diagnostics.
 * @return Owned block spans, or a parse failure for malformed top-level text.
 * @thread Reentrant and side-effect free.
 */
[[nodiscard]] EVENGINE_API_PLATFORM eve::Result<std::vector<DnutBlock>> scanDnutBlocks(
    const std::vector<DnutToken>& tokens, const std::string& path);

}  // namespace eve::dnut
