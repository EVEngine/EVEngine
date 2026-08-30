#pragma once

#include "common/Result.h"
#include "common/Subscription.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace eve::animation {

class AnimClip;

/**
 * @brief Weak path→clip registry for animation hot reload.
 *
 * `Animation::newClipFromAnimationFixtureText` registers every clip it creates under its
 * source path. When that `*.anim.txt` fixture changes, the "animclip" IAssetReloader
 * re-imports the source and adopts the fresh content into the registered
 * instances (AnimClip::adopt), so existing holders — state machines, scripts —
 * keep valid pointers and see updated content without re-binding.
 *
 * Entries hold raw pointers and are removed automatically by ~AnimClip;
 * registrations must not outlive the clip (the engine's script bindings keep
 * clips alive for the process lifetime, and tests use owning pointers).
 */
class AnimClipRegistry {
public:
    /** @brief Owning event value emitted after an EVA path reload attempt. */
    struct ReloadEvent {
        std::string path;
        int         clipsRefreshed = 0;
        bool        succeeded      = false;
    };
    using ReloadCallback = std::function<void(const ReloadEvent&)>;

    /** @brief Track a clip under a source path (keys are normalized). */
    static void registerPath(const std::string& path, AnimClip* clip);
    /** @brief Forget a clip everywhere (called from ~AnimClip). */
    static void unregister(AnimClip* clip);
    /** @brief Clips registered under a path; may be empty. */
    static std::vector<AnimClip*> findByPath(const std::string& path);
    /** @brief Whether any clip is registered under this path. */
    static bool hasPath(const std::string& path);
    /**
     * @brief Reload a path and report a structured import failure.
     * @return Refreshed clip count; an unknown path is a successful NoOp.
     * @remarks The reload mutation is completed before listeners run. Listener
     *          exceptions are contained and counted, never used to roll back
     *          adopted clips.
     */
    [[nodiscard]] static eve::Result<int> reloadPath(const std::string& path);
    /** @brief Subscribe to reload attempts; token disposal is owner-thread-affine. */
    [[nodiscard("retain Subscription or explicitly dispose it")]] static eve::Subscription subscribeReload(
        ReloadCallback callback);
    /** @brief Number of reload listener exceptions contained so far. */
    [[nodiscard]] static std::uint64_t reloadCallbackFailureCount();
    /** @brief Total registered clips (tests). */
    static int count();
    /** @brief Drop all entries (tests). */
    static void clear();
    /** @brief Normalize a path to the registry key form. */
    static std::string normalizePath(const std::string& path);
};

}  // namespace eve::animation
