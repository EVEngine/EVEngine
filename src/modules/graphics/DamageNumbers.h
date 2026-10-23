#pragma once

#include "common/Result.h"

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace eve::graphics {
class Graphics;
class Font;

/** @brief Presentation categories, independent of any combat or settlement module. */
enum class DamageNumberKind { Damage, Critical, Heal, Miss };
/** @brief Observable bounded-pool admission result. */
enum class DamageNumberAdmission { Spawned, Merged, ReplacedOldest };

/** @brief Owning style copied into each popup at spawn. Coordinates are drawable pixels. */
struct DamageNumberStyle {
    float lifetime  = 1.1f;
    float velocityY = -90.f;
    float gravity   = 45.f;
    float scale     = 1.f;
    float pop       = 0.5f;
    float shake     = 0.f;
    float r = 1.f, g = 0.35f, b = 0.25f;
    float outline = 1.f;
    float fadeIn = 0.05f, fadeOut = 0.3f;
    float mergeWindow = 0.18f;
    float mergeRadius = 64.f;
    int   decimals    = 0;
    bool  compact     = false;
};

/** @brief Owning display request. The caller supplies actual committed amounts, never pending damage. */
struct DamageNumberRequest {
    double           amount = 0.0;
    float            x = 0.f, y = 0.f;
    DamageNumberKind kind = DamageNumberKind::Damage;
    /** @brief Stable caller-defined target/group key; empty disables merging. No entity is retained. */
    std::string group;
    /** @brief Explicit horizontal launch velocity; injected by the caller, no hidden RNG. */
    float velocityX = 0.f;
};

/** @brief Independent owning frame sample, useful for observation and alternative renderers. */
struct DamageNumberSample {
    std::string text;
    float       x = 0.f, y = 0.f, scale = 1.f, alpha = 0.f;
    float       r = 1.f, g = 1.f, b = 1.f, outline = 0.f;
};

/**
 * @brief Bounded transient floating-number presentation owned by a game/view.
 * @ownership Owns styles and all live popups; never owns gameplay, fonts or graphics.
 * @thread Owner/render thread only. No callbacks, locks, wall clock or global RNG.
 * @lifetime clear on scene teardown/reload. No persistent format or cross-frame object links.
 * Equal requests and accumulated time give equal samples within 1e-4 pixel floating tolerance.
 */
class DamageNumbers {
public:
    /** @brief Construct a pool with four built-in styles and 256 reusable slots. */
    DamageNumbers();
    ~DamageNumbers();
    DamageNumbers(const DamageNumbers&)            = delete;
    DamageNumbers& operator=(const DamageNumbers&) = delete;

    /** @brief Validate and replace one future-spawn style; existing entries keep their own copy. */
    [[nodiscard]] Result<void> setStyle(DamageNumberKind kind, const DamageNumberStyle& style);
    /** @brief Return an owning preset copy; invalid enum values fail. */
    [[nodiscard]] Result<DamageNumberStyle> style(DamageNumberKind kind) const;
    /**
     * @brief Validate and admit one popup. Failure leaves pool and sequence unchanged.
     * @return Spawned, Merged (same nonempty group/kind within window and radius), or ReplacedOldest.
     * Merging never extends lifetime, so a continuous damage stream cannot keep an entry forever.
     */
    [[nodiscard]] Result<DamageNumberAdmission> spawn(const DamageNumberRequest& request);
    /** @brief Advance injected seconds; nonfinite/negative dt fails without mutation. */
    [[nodiscard]] Result<void> update(double seconds);
    /** @brief Remove every transient entry; deterministic spawn sequence restarts. */
    void clear();
    /** @brief Return live popup count. */
    [[nodiscard]] std::size_t count() const;
    /** @brief Fixed maximum live popup budget; overflow replaces the oldest. */
    [[nodiscard]] static constexpr std::size_t capacity() { return 256; }
    /**
     * @brief Build an independent owning frame snapshot for observation.
     * @cost Linear allocations/copies in live entries, unlike direct render; use only when an owning snapshot is
     * needed.
     */
    [[nodiscard]] std::vector<DamageNumberSample> snapshot() const;
    /**
     * @brief Draw live samples centered on x through backend-neutral text quads, with outline/shadow.
     * @param graphics Borrowed active frame renderer; must match font's creating renderer.
     * @param font Borrowed preloaded atlas containing digits and +-.!KMIS; alive through this call only.
     * @thread Call in the owning render phase; references are not retained, no callbacks.
     * @cost Up to six text passes per live popup; no disk loads, atlas creation or owning snapshot allocation.
     */
    void render(Graphics& graphics, Font& font) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace eve::graphics
