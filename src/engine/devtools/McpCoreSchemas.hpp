#pragma once
#include <string_view>

namespace eve::dev {
/** @brief Borrow legacy core tool schemas; process lifetime, synchronized first initialization.
 * @cost First access joins the core schema fragments once; subsequent reads are constant time.
 */
[[nodiscard]] std::string_view mcpCoreToolSchemas();
}  // namespace eve::dev
