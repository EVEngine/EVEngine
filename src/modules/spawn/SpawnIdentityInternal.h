#pragma once
#include <optional>
#include <string>
#include "common/Identity.h"
namespace eve::spawn::detail {
/** @brief Allocate a non-repeating process-local incarnation; zero means exhausted. No callbacks. */
[[nodiscard]] std::uint64_t nextSpawnIncarnation() noexcept;
/** @brief Derive the canonical v1 identity after the caller validates owner/epoch/id/ordinal.
 * @ownership Returns a value; retains no inputs. Owner-thread boundary work, no callbacks.
 */
[[nodiscard]] PersistentId spawnIdentity(PersistentId owner, std::uint64_t epoch, const std::string& id,
                                         std::uint64_t ordinal, std::optional<std::uint32_t> unit);
}  // namespace eve::spawn::detail
