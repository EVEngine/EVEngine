#pragma once
#include "common/Export.h"

#include <memory>
#include "animation/editing/AnimationClip.h"
#include "animation/editing/SkeletonOverlay.h"

namespace eve::editing {
class TaskContext;
}
namespace eve::animation_editor {
/** @brief Owning hierarchy metadata used only by a preview worker. */
struct AnimationPreviewBone {
    std::string name;
    std::string parent;
};
/** @brief Atomic pose and overlay publication from a single immutable input revision. */
struct AnimationPreviewPublication {
    animation_editing::AnimationClipPreview pose;
    editing::GizmoSnapshot                  overlay;
};
/**
 * @brief Immutable animation preview provider retained by submitted jobs across replacement.
 * @thread Worker only; inputs are job-owned and borrowed for the call. No editor pointers or callbacks.
 * @reentrancy Shared providers can be called concurrently by different sessions; mutable provider state must be
 * synchronized.
 * @note Final shared-provider release may run on the owner or worker thread; provider destruction must support both.
 * @lifetime Computation must be bounded and cooperate with cancellation; destruction joins the worker.
 */
class EVENGINE_API_EDITORS IAnimationClipPreviewProvider {
public:
    virtual ~IAnimationClipPreviewProvider() = default;
    /** @brief Sample pose and build its overlay from one document snapshot.
     * @cost Linear in authored keys and skeleton size per invalidation; result is consumed without recomputation.
     */
    [[nodiscard]] virtual animation_editing::Result<AnimationPreviewPublication> generate(
        const animation_editing::AnimationClipDocumentTarget& document, double time,
        const std::vector<AnimationPreviewBone>& skeleton, const std::string& selectedBone,
        const editing::TaskContext& context) const = 0;
};
/** @brief Create the deterministic CPU pose/overlay provider. */
[[nodiscard]] EVENGINE_API_EDITORS std::shared_ptr<const IAnimationClipPreviewProvider>
                                   makeAnimationClipPreviewProvider();
}  // namespace eve::animation_editor
