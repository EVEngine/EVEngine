#pragma once
#include "common/Value.h"
namespace ssq {
class Table;
}
namespace eve::graphics {
class Graphics;
/**
 * @brief Single-use, owning visibility request; no ECS/resource pointer survives a call.
 * @thread Prepare and measure on the engine owner thread. No callbacks.
 * @lifetime Query owns protocol values; Graphics is borrowed only during measure.
 */
class EVENGINE_API_BACKENDS SceneVisibilityQuery {
public:
    /** @brief Construct an empty query; only prepare returns a usable request. */
    SceneVisibilityQuery() = default;
    /** @brief Queries cannot be copied or replayed. */
    SceneVisibilityQuery(const SceneVisibilityQuery&) = delete;
    /** @brief Queries cannot be copied or replayed. */
    SceneVisibilityQuery& operator=(const SceneVisibilityQuery&) = delete;
    /** @brief Transfer the owned request. */
    SceneVisibilityQuery(SceneVisibilityQuery&&) noexcept = default;
    /** @brief Transfer the owned request. */
    SceneVisibilityQuery& operator=(SceneVisibilityQuery&&) noexcept = default;
    /** @brief Validate schema v1/v2 and copy the bounded request without drawing. */
    [[nodiscard]] static Result<SceneVisibilityQuery> prepare(const Value& request);
    /**
     * @brief Consume the request, resolve current generation-qualified meshes and capture private GPU targets.
     * @cost Blocking GPU raster/readback of one scene pass plus one isolated pass per unique target object, per
     * candidate. CPU isolated-mask intersections are quadratic in target count and linear in viewport pixels.
     * @return Owning v3 evidence including depth-independent directed projected overlaps, or explicit
     * unsupported/stale/budget diagnostic; never edits live transforms.
     */
    [[nodiscard]] Result<Value> measure(Graphics& graphics);

private:
    Value request_;
    bool  consumed_ = false;
};
/** @brief Bind and register the owner-thread native visibility provider during Graphics startup. */
void registerSceneVisibilityProvider(Graphics* graphics);
/** @brief Revoke the matching provider before Graphics destruction. */
void detachSceneVisibilityProvider(Graphics* graphics) noexcept;
/** @brief Register the owning two-step visibility query script API, synchronously. */
void exposeSceneVisibility(ssq::Table& table);
}  // namespace eve::graphics
