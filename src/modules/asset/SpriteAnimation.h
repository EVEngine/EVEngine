#pragma once
#include <array>
#include "asset/EvpackResourceReader.h"
namespace eve {
class Value;
}

namespace eve::asset {
/** @brief Owning sprite key snapshot; pixel rectangles and pivots use top-left coordinates. */
struct SpriteAnimationFrame {
    AssetRef              image;
    std::string           name;
    double                time = 0;
    std::array<double, 4> rect{};
    std::array<double, 2> pivot{};
    std::array<double, 2> imageSize{};
    double                pixelsPerUnit = 100;
    bool                  nearest       = true;
};
/** @brief Immutable validated sprite timeline. No GPU/ECS objects, scheduler, callbacks or mutable playback clock.
 * @ownership Owns frame metadata; snapshots remain valid independently of the clip or source package.
 * @thread Const sampling is worker-safe. Callers supply finite nonnegative elapsed seconds.
 * @reentrancy No callbacks or locks. Reimport builds a new candidate without mutating existing clips.
 */
class SpriteAnimationClip {
public:
    /** @brief Decode eve.sprite-animation/1; reject unknown versions, ignore unknown fields.
     * @return Owning candidate or structured validation error. No previous schema exists to migrate.
     */
    [[nodiscard]] static Result<SpriteAnimationClip> decode(const Value& definition);
    /** @brief Load an immutable runtime definition; reader is borrowed only during this call. */
    [[nodiscard]] static Result<SpriteAnimationClip> load(const EvpackResourceReader& reader, const AssetRef& asset,
                                                          const EvpackCapabilities& capabilities);
    /** @brief Sample a held key; loop wraps at duration, non-loop playback holds the final frame.
     * @return Owning snapshot; negative/nonfinite seconds are rejected. Identical inputs select identical keys.
     */
    [[nodiscard]] Result<SpriteAnimationFrame> sample(double seconds) const;
    /** @brief Number of authored keys. */
    [[nodiscard]] std::size_t frameCount() const noexcept { return frames_.size(); }
    /** @brief Authored duration in seconds. */
    [[nodiscard]] double duration() const noexcept { return duration_; }

private:
    std::vector<SpriteAnimationFrame> frames_;
    double                            duration_ = 0;
    bool                              loop_     = false;
};
}  // namespace eve::asset
