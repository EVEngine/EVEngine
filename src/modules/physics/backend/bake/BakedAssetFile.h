#pragma once
#include <string>
#include <string_view>
#include "common/Export.h"
#include "common/Result.h"

namespace eve::physics {
/** @brief Stable noncryptographic 128-bit content digest, encoded as 32 lowercase hexadecimal bytes.
 * @ownership Borrows bytes only for this call; returns owning text. Reentrant, no callbacks.
 * @cost Linear in bytes, for asset preparation/integrity checks outside sampling.
 */
[[nodiscard]] EVENGINE_API_BACKENDS std::string bakedAssetContentKey(std::string_view bytes);
/** @brief Publish complete baked asset bytes atomically without replacing an existing path.
 * @ownership Borrows path and bytes only during this synchronous call; retains neither.
 * @thread Concurrent writers are supported; exactly one can publish a given new path. No callbacks.
 * @cost Linear in bytes plus filesystem durability barriers, once per baked asset outside the frame loop.
 * @return Checked IO status; unsupported hard links fail explicitly. Linux requires O_TMPFILE support
 * and /proc/self/fd links, syncs anonymous data before publication and its parent directory afterwards.
 * Unsupported anonymous staging returns Unsupported; no named staging degradation is attempted.
 * Other platforms currently guarantee atomic visibility only.
 * A barrier failure reports publication=not_published or published and durability=unconfirmed in details.
 * Published failures retain the complete target; callers must inspect it before retrying. Existing targets
 * are never removed. Linux anonymous staging is reclaimed on process exit, including abrupt interruption.
 * Other platforms may leave private staging directories; older staging directories are never auto-deleted.
 */
[[nodiscard]] EVENGINE_API_BACKENDS Result<void> publishBakedAsset(std::string_view path, std::string_view bytes);
}  // namespace eve::physics
