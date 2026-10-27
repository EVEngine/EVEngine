#pragma once
#include <span>
#include "physics/backend/bake/RigidPoseTrack.h"
namespace eve::physics {
namespace bake_detail {
// Sort/normalize only the owning candidate; the accepted roster is never modified.
[[nodiscard]] Result<void> validateRigidFrame(RigidTrackSpace space, RigidTrackFrame& frame,
                                              std::span<const RigidTrackSample> roster, float previous);
}  // namespace bake_detail
struct RigidPoseTrack::Data {
    RigidTrackSpace                   space;
    std::shared_ptr<const BakeRecipe> recipe;
    std::vector<RigidTrackFrame>      frames;
};
}  // namespace eve::physics
