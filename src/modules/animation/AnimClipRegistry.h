#pragma once

#include <string>
#include <vector>

namespace eve::animation {

class AnimClip;

/**
 * @brief Weak path→clip registry for animation hot reload.
 *
 * `Animation::newClipFromEvaFile` registers every clip it creates under its
 * source path. When that `.eva` file changes, the "animclip" IAssetReloader
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
    /** @brief Track a clip under a source path (keys are normalized). */
    static void registerPath(const std::string& path, AnimClip* clip);
    /** @brief Forget a clip everywhere (called from ~AnimClip). */
    static void unregister(AnimClip* clip);
    /** @brief Clips registered under a path; may be empty. */
    static std::vector<AnimClip*> findByPath(const std::string& path);
    /** @brief Whether any clip is registered under this path. */
    static bool hasPath(const std::string& path);
    /**
     * @brief Re-import the source and adopt into every registered clip.
     * @return number of clips refreshed (0 when the path is unknown or the
     *         source cannot be re-imported).
     */
    static int reloadPath(const std::string& path);
    /** @brief Total registered clips (tests). */
    static int count();
    /** @brief Drop all entries (tests). */
    static void clear();
    /** @brief Normalize a path to the registry key form. */
    static std::string normalizePath(const std::string& path);
};

}  // namespace eve::animation
