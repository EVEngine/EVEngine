#pragma once
#include <string>
#include <string_view>
#include <vector>
#include "common/Export.h"
#include "common/Result.h"
#include "physics/destruction/GeometryCollectionInstance.h"

namespace ssq {
class Table;
}
namespace eve::physics {
/**
 * @brief Owning, physics-independent baked rigid-fragment animation.
 * @ownership Owns all samples; retains no instance/world pointers.
 * @thread Single caller; no callbacks. Copies may play independently.
 * @lifetime Valid after source instance/world destruction and process restart.
 * File schema EVECOLL1 version 2 embeds optional mesh data; version 1 migrates as boxes. Unknown
 * versions, trailing bytes and malformed data are rejected before publication.
 */
class EVENGINE_API_DOMAINS CollapseAnimation {
public:
    /** @brief Stable noncryptographic cache identity over every source byte.
     * @param source Borrowed recipe/model/script bytes, consumed synchronously.
     * @return Owning 32-character lowercase hex key; not a security digest.
     * @cost Linear native byte hashing, once per source/recipe admission, never per frame.
     * @thread Reentrant, no IO, global state or callbacks.
     */
    [[nodiscard]] static std::string contentKey(std::string_view source);
    /** @brief Record a complete frame at strictly increasing seconds, starting at zero.
     * @cost Linear in fragments per frame; amortize by recording offline at fixed dt.
     * @return Failure leaves recorded frames unchanged. Source is borrowed only here.
     */
    [[nodiscard]] Result<void> capture(const GeometryCollectionInstance& source, float seconds);
    /** @brief Sample a fragment, clamping time to endpoints; linear translation and shortest quaternion nlerp.
     * @return Owning pose; state switches at the recorded frame boundary.
     */
    [[nodiscard]] Result<BonePresentation> sample(float seconds, int bone) const;
    /** @brief Save a new file; refuses existing paths to preserve previous bakes.
     * @cost Linear in total recorded samples; perform outside the frame loop.
     * @return Checked IO status. Failed writes remove their incomplete output.
     */
    [[nodiscard]] Result<void> save(std::string_view path) const;
    /** @brief Load and validate a file before publishing an owning animation.
     * @cost Linear in file size; perform once per asset, reuse for all playback instances.
     * @return Complete animation or failure, with no observable partial restore.
     */
    [[nodiscard]] static Result<CollapseAnimation> load(std::string_view path);
    /** @brief Recorded duration in seconds. */
    [[nodiscard]] float duration() const noexcept;
    /** @brief Number of fragments. */
    [[nodiscard]] int boneCount() const noexcept;
    /** @brief Number of recorded frames. */
    [[nodiscard]] int frameCount() const noexcept;

    /** @brief Shared immutable embedded meshes; empty for v1/box animations.
     * @ownership Return value extends mesh lifetime across clip replacement or destruction.
     * @thread Single caller during mutation; immutable geometry is safe to share.
     */
    [[nodiscard]] std::shared_ptr<const FractureGeometry> fragmentGeometry() const noexcept;

private:
    struct Frame {
        float                         seconds;
        std::vector<BonePresentation> bones;
    };
    std::vector<Frame>                      frames_;
    std::shared_ptr<const FractureGeometry> geometry_;
};
/** @brief Register owning animation and sampled pose script types. */
void exposeCollapseAnimation(ssq::Table& table);
}  // namespace eve::physics
