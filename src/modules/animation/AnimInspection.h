#pragma once
#include <vector>
#include "common/Export.h"
#include "common/Result.h"

namespace eve::animation {
class AnimClip;
class AnimSkeleton;
class AnimSkin;

/** @brief Timeline label and independently resolved clip time, both in seconds. */
struct AnimInspectionTime {
    double timelineTime = 0., clipTime = 0.;
};
/** @brief Owning column-major skin palette; joint order is exactly that of the supplied AnimSkin. */
struct AnimInspectionFrame {
    AnimInspectionTime time;
    std::vector<float> palette;
};
/**
 * @brief Build independent clip poses without changing playback, events, root motion or skin caches.
 * @details Times are explicit: callers resolve fps and looping before construction. Up to 16 samples,
 * 4096 skeleton bones and 4096 skin joints. Clamped clip endpoints include the final pose even for a
 * looping source clip. Root motion stays in the sampled pose; no graph, IK or procedural solver runs.
 * @thread Owner thread with immutable inputs for the entire build; no callbacks or retained references.
 * @lifetime The builder owns its schedule; returned frames own all data and survive source destruction.
 */
class EVENGINE_API_WORLD AnimInspectionBuilder {
public:
    /** @brief Own an explicit schedule; validation happens atomically in build(). */
    explicit AnimInspectionBuilder(std::vector<AnimInspectionTime> times);
    /**
     * @brief Evaluate all requested poses into independent palettes or return a diagnostic.
     * @param clip Borrowed clip, authored against the supplied skeleton's track indices.
     * @param skeleton Borrowed skeleton; all skin joint indices and names must match it.
     * @param skin Borrowed skin binding; its mutable palette and deformation caches remain unchanged.
     * @return Fully owning frames, or InvalidArgument for time, identity, size or finite-value violations.
     * @cost Up to 16 pose evaluations plus joint matrix products per build; reuse the returned frames.
     * @note Same inputs are repeatable within a build; cross-platform float comparisons use tolerance.
     */
    [[nodiscard]] Result<std::vector<AnimInspectionFrame>> build(const AnimClip& clip, const AnimSkeleton& skeleton,
                                                                 const AnimSkin& skin) const;

private:
    std::vector<AnimInspectionTime> times_;
};
}  // namespace eve::animation
