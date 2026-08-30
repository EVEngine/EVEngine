#pragma once

/** @file Utf8Validation.h @brief Strict allocation-free UTF-8 validation. */

#include <cstdint>
#include <string_view>

namespace eve {

/** @brief Policy for embedded U+0000 in otherwise valid UTF-8 text. */
enum class Utf8NullPolicy : std::uint8_t { Allow, Reject };

/**
 * @brief Validate shortest-form UTF-8, Unicode scalar range and optional NUL exclusion.
 * @param text Borrowed bytes used only for this call.
 * @param nullPolicy Whether U+0000 is accepted.
 * @return True only for a complete sequence containing no surrogate code points.
 * @thread Worker-safe; no shared state or allocation.
 */
[[nodiscard]] bool isValidUtf8(
    std::string_view text, Utf8NullPolicy nullPolicy = Utf8NullPolicy::Allow) noexcept;

}  // namespace eve
