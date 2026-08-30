#pragma once

// Asset ownership interface, shared by two consumers:
//
// 1. Hot reload: let each module react to its own asset files. The filesystem
//    watcher knows a path changed but nothing about what lives at that path,
//    so every module that owns an asset kind registers a reloader and the
//    watcher just dispatches. Modules the build does not contain simply are
//    not registered.
// 2. Unified resource cache (common/Resource.h): on a cache miss the
//    ResourceManager asks the same registered providers to `load()` the CPU
//    resource for a key, so the engine-common layer still knows nothing about
//    graphics / particles / map / font / model / sound.
//
// Ordering matters when several reloaders react to one file: the cached CPU
// resource must refresh before the GPU texture is re-uploaded, and the GPU
// texture before the emitters / tile layers that sample it re-bind. Register
// with a priority to express that.

#include "common/Export.h"
#include "common/Result.h"

#include <string>

namespace eve {
class Resource;
}

namespace eve::caps {

class EVENGINE_API IAssetReloader {
public:
    /** Suggested priorities; lower runs first. */
    enum Priority {
        kCache = 0,    // refresh cached CPU resources (ResourceManager)
        kTexture = 10,   // refresh GPU resources before their consumers re-bind
        kConsumer = 20,  // things that reference a reloaded resource
    };

    static constexpr const char* capabilityName = "IAssetReloader";
    virtual ~IAssetReloader() = default;

    /**
     * @brief Stable name for this reloader ("texture" / "particle" / "tilemap").
     * A path bound to this kind is offered here regardless of its extension.
     * @return Borrowed, non-null, null-terminated text owned by the provider.
     * @ownership Borrowed; the caller must not free or retain a mutable pointer.
     * @lifetime Valid for the provider lifetime; implementations normally return
     *           a static string. It must not be retained past provider removal.
     * @thread May be queried from the dispatch thread after registration settles.
     * @reentrancy The call must not mutate the capability registry or invoke callbacks.
     */
    virtual const char* reloadKind() const = 0;

    /**
     * Whether this reloader claims `normPath` during automatic dispatch.
     * For providers of cached CPU resources the key may carry parameters
     * ("fonts/a.ttf?size=16"); use ResourceManager::pathOfKey() to strip them.
     */
    virtual bool handlesPath(const std::string& normPath) const = 0;

    /**
     * @brief Create the CPU-side resource identified by `key` (a normalized VFS path,
     * optionally followed by `?param=value` entries that change the decoded
     * asset, e.g. "fonts/a.ttf?size=16").
     *
     * @return Borrowed raw pointer to a newly loaded resource, or nullptr when
     *         this provider does not claim the key. The caller
     *         (ResourceManager) caches the result and keeps it alive; later
     *         reloads drain the freshly loaded instance into the cached one via
     *         Resource::adopt, so the cached identity stays stable.
     * @ownership Owned by the ResourceManager cache after return; the caller
     *            must not delete the pointer or transfer it elsewhere.
     * @nullable Yes; nullptr means that this provider does not claim `key`.
     * @lifetime The pointer remains valid while the cache entry/provider-owned
     *           resource remains alive; it must not cross unload or module removal.
     * @thread Provider-specific; reload dispatch itself is main-thread affine.
     * @reentrancy The provider must not call back into the reloader registry while
     *             loading; callbacks occur after registry/cache locks are released.
     */
    [[nodiscard("loaded resource ownership must be retained or explicitly handled")]] virtual Resource* load(
        const std::string& key) {
        return nullptr;
    }

    /**
     * @brief Reload everything referencing `normPath`.
     * @return A checked result whose value is true when at least one object changed.
     * @remarks Failure is reported through the structured status; a successful
     *          false value means the provider handled the path but nothing changed.
     */
    [[nodiscard("asset reload outcome must be checked")]] virtual eve::Result<bool> reload(
        const std::string& normPath) = 0;
};

}  // namespace eve::caps
