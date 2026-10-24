#pragma once
#include "physics/bake/DeformTrack.h"
namespace eve::physics {
namespace bake_detail {
// Single frame-admission implementation used by immutable tracks and incremental recordings.
[[nodiscard]] Result<void> validateDeformFrame(const DeformFrame& frame, const DeformTopology& topology,
                                               const BakeRecipe& recipe, std::uint32_t index, float previous);
}  // namespace bake_detail
struct DeformTrack::Data {
    std::vector<DeformFrame>          frames;
    std::shared_ptr<const BakeRecipe> recipe;
};
}  // namespace eve::physics
