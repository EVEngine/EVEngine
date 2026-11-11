#pragma once
/** @file AtomicTextWriteLease.h
 * @brief filesystem module-interface: non-owning, destruction-checked access to the canonical atomic text writer.
 */
#include <memory>
#include <string_view>
#include "common/Export.h"
#include "common/Result.h"
namespace eve::filesystem {
class Filesystem;
namespace detail {
struct AtomicWriteOwner;
}
/** @brief Provider-specific weak observation, valid for module-owned or genuinely shared filesystems.
 * @ownership Never retains the Filesystem. The original provider owns a unique marker; destruction invalidates all
 * copies, and a replacement provider cannot revive them. Not serialized, not a global capability lookup.
 * @thread Provider owner thread only, including available(). No concurrent provider destruction or callback reentry;
 * never invoke a lease while its provider is being destroyed. Write executes synchronously without external callbacks.
 */
class EVENGINE_API_FOUNDATION AtomicTextWriteLease final {
public:
    /** @brief Construct an unavailable observation without creating or resolving a filesystem. */
    AtomicTextWriteLease() = default;
    /** @brief Whether the exact observed provider is still live; no allocation, I/O or callbacks. */
    [[nodiscard]] bool available() const noexcept;
    /** @brief Call the exact provider's canonical writeTextAtomic, or reject an expired/unbound observation.
     * @return Native atomic write result or StaleHandle before accessing a retired provider.
     * @ownership Borrows UTF-8 path/text only for this synchronous call; does not copy ownership of the provider.
     * @remarks Path policy/platform support are unchanged; does not bind a replacement or silently fall back.
     * @cost Linear byte write/flush/replace per explicit call; stale checks are constant.
     */
    [[nodiscard]] Result<void> write(std::string_view relativePath, std::string_view text) const;

private:
    friend class Filesystem;
    explicit AtomicTextWriteLease(std::weak_ptr<detail::AtomicWriteOwner> owner) noexcept;
    std::weak_ptr<detail::AtomicWriteOwner> owner_;
};
}  // namespace eve::filesystem
