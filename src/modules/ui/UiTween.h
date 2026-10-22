#pragma once
#include "common/Export.h"

/**
 * @file UiTween.h
 * @brief Lightweight UI-owned tween driver for host/item visual properties.
 *
 * Convenience APIs (`UI::animate*`) auto-pump from `beginFrameAndRender`.
 * This is intentionally separate from `animation::Motion*` (which requires an
 * Animation module pump and typed sinks); both can coexist.
 */

#include "common/Result.h"
#include "ui/UIHost.h"

#include <cstdint>
#include <string>
#include <vector>

namespace eve::ui {

/**
 * @brief Evaluate a UI tween ease curve in [0,1].
 * @param t Normalized progress (clamped).
 * @param kind Ease name; empty / "smoothstep" keeps the historical default.
 * @return Eased progress in [0,1] for known kinds; unknown kinds return linear `t`.
 * @note Supported: smoothstep, linear, in/out/inOut + Quad/Cubic/Sine/Expo,
 *       and in/out/inOut + Back/Elastic/Bounce (Motion ease parity).
 */
[[nodiscard]] EVENGINE_API_WORLD float evaluateUiEase(float t, const char *kind);

/** @brief Host window property driven by a vec2 or float tween. */
enum class UiHostTweenTarget : std::uint8_t {
    Pos          = 0,  ///< Meta.posX / posY (px)
    Size         = 1,  ///< Meta.sizeX / sizeY (px)
    OverlayAlpha = 2,  ///< Meta.overlayBgAlpha
};

/** @brief Retained-node property driven by a float or vec2 tween. */
enum class UiItemTweenTarget : std::uint8_t {
    Opacity = 0,  ///< UINode.opacity (transient)
    Pos     = 1,  ///< UINode.posX / posY (absolute placement)
};

/** @brief One host-level tween slot. */
struct UiHostTween {
    UIHostHandle      host{};
    UiHostTweenTarget target     = UiHostTweenTarget::Pos;
    float             fromX      = 0.f;
    float             fromY      = 0.f;
    float             toX        = 0.f;
    float             toY        = 0.f;
    double            startMs    = 0.0;
    double            durationMs = 0.0;
    double            delayMs    = 0.0;
    std::string       ease       = "smoothstep";
};

/** @brief One item-level tween slot scoped to a host + node id. */
struct UiItemTween {
    UIHostHandle      host{};
    std::string       nodeId;
    UiItemTweenTarget target     = UiItemTweenTarget::Opacity;
    float             fromX      = 0.f;
    float             fromY      = 0.f;
    float             toX        = 0.f;
    float             toY        = 0.f;
    double            startMs    = 0.0;
    double            durationMs = 0.0;
    double            delayMs    = 0.0;
    std::string       ease       = "smoothstep";
};

/**
 * @brief Owns pending UI tweens and advances them against an injected clock.
 * @ownership UI owns one driver; handles are generation-checked via UIHost::resolve.
 */
class EVENGINE_API_WORLD UiTweenDriver {
public:
    /**
     * @brief Animate selected-host window position.
     * @param host Target host handle.
     * @param x Target posX (px).
     * @param y Target posY (px).
     * @param durationMs Duration; <=0 jumps immediately.
     * @param ease Ease kind (default smoothstep).
     * @param delayMs Delay before the first sample moves.
     * @param nowMs Clock sample used as the tween start.
     */
    void animateHostPos(UIHostHandle host, float x, float y, float durationMs, const std::string &ease, float delayMs,
                        double nowMs);

    /**
     * @brief Animate selected-host explicit window size.
     * @param host Target host handle.
     * @param w Target width (px).
     * @param h Target height (px).
     * @param durationMs Duration; <=0 jumps immediately.
     * @param ease Ease kind (default smoothstep).
     * @param delayMs Delay before the first sample moves.
     * @param nowMs Clock sample used as the tween start.
     */
    void animateHostSize(UIHostHandle host, float w, float h, float durationMs, const std::string &ease, float delayMs,
                         double nowMs);

    /**
     * @brief Animate selected-host overlay background alpha.
     * @param host Target host handle.
     * @param alpha Target overlay alpha clamped to [0,1].
     * @param durationMs Duration; <=0 jumps immediately.
     * @param ease Ease kind (default smoothstep).
     * @param delayMs Delay before the first sample moves.
     * @param nowMs Clock sample used as the tween start.
     */
    void animateHostOverlayAlpha(UIHostHandle host, float alpha, float durationMs, const std::string &ease,
                                 float delayMs, double nowMs);

    /**
     * @brief Animate one node's transient opacity.
     * @param host Host owning the node.
     * @param id Node id.
     * @param opacity Target opacity clamped to [0,1].
     * @param durationMs Duration; <=0 jumps immediately.
     * @param ease Ease kind (default smoothstep).
     * @param delayMs Delay before the first sample moves.
     * @param nowMs Clock sample used as the tween start.
     * @return Applied on success; StaleHandle when `host` is invalid; NotFound when `id` is missing.
     */
    [[nodiscard]] eve::Result<void> animateItemOpacity(UIHostHandle host, const std::string &id, float opacity,
                                                       float durationMs, const std::string &ease, float delayMs,
                                                       double nowMs);

    /**
     * @brief Animate one absolute node's placement offset.
     * @param host Host owning the node.
     * @param id Node id.
     * @param x Target posX.
     * @param y Target posY.
     * @param durationMs Duration; <=0 jumps immediately.
     * @param ease Ease kind (default smoothstep).
     * @param delayMs Delay before the first sample moves.
     * @param nowMs Clock sample used as the tween start.
     * @return Applied on success; StaleHandle when `host` is invalid; NotFound when `id` is missing.
     * @note Marks the node absolute so the offset is honored by layout.
     */
    [[nodiscard]] eve::Result<void> animateItemPos(UIHostHandle host, const std::string &id, float x, float y,
                                                   float durationMs, const std::string &ease, float delayMs,
                                                   double nowMs);

    /** @brief Cancel every pending host tween for `host`. */
    void cancelHost(UIHostHandle host);

    /** @brief Cancel every pending item tween for `host` + `id` (empty id = all items). */
    void cancelItem(UIHostHandle host, const std::string &id = {});

    /** @brief Cancel every pending tween for `host`. */
    void cancelAll(UIHostHandle host);

    /**
     * @brief Advance all pending tweens to `nowMs`.
     * @param nowMs Monotonic clock in milliseconds.
     */
    void tick(double nowMs);

    /** @brief Number of pending host tweens (tests / diagnostics). */
    [[nodiscard]] std::size_t hostTweenCount() const noexcept { return hostTweens_.size(); }

    /** @brief Number of pending item tweens (tests / diagnostics). */
    [[nodiscard]] std::size_t itemTweenCount() const noexcept { return itemTweens_.size(); }

private:
    void        replaceHost(UIHostHandle host, UiHostTweenTarget target);
    void        replaceItem(UIHostHandle host, const std::string &id, UiItemTweenTarget target);
    void        applyHost(UiHostTween &t, float k);
    void        applyItem(UiItemTween &t, float k);
    static bool sameHost(UIHostHandle a, UIHostHandle b) noexcept;

    std::vector<UiHostTween> hostTweens_;
    std::vector<UiItemTween> itemTweens_;
};

/** @brief Wall-clock milliseconds from steady_clock (presentation clock). */
[[nodiscard]] EVENGINE_API_WORLD double uiTweenWallClockMs();

}  // namespace eve::ui
