#pragma once
#include "common/Export.h"
#include "common/Result.h"
namespace eve::physics {
struct RigidTrackPose;
/** @brief Rotation about the baked origin followed by translation, in the track's explicit units; no scaling. */
struct BakeRigidPlacement {
    float x = 0, y = 0, z = 0;
    float rotationX = 0, rotationY = 0, rotationZ = 0, rotationW = 1;
};
/** @brief Value-owned validated rigid placement shared by baked pose consumers.
 * @ownership Owns a normalized placement; copies are independent, no source pointers or callbacks.
 * @thread Each instance requires exclusive mutation; const application is reentrant.
 */
class EVENGINE_API_BACKENDS BakeRigidTransform {
public:
    /** @brief Validate finite translation and a nonzero XYZW quaternion; failures preserve prior placement. */
    [[nodiscard]] Result<void> set(BakeRigidPlacement placement);
    /** @brief Return the normalized placement by value. */
    [[nodiscard]] BakeRigidPlacement placement() const noexcept;
    /** @brief Rotate and translate a finite pose, preserving inputs on failure.
     * @return Owning pose or nonfinite/zero-rotation/float-overflow diagnostic.
     */
    [[nodiscard]] Result<RigidTrackPose> apply(RigidTrackPose pose) const;

private:
    BakeRigidPlacement placement_;
};
}  // namespace eve::physics
