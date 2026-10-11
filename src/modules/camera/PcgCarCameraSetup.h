#pragma once

#include "common/Result.h"

#include <cstdint>

namespace ssq { class Table; }
namespace eve::graphics { class Camera3D; }
namespace eve::scene { class SceneNodeRef; }

namespace eve::camera {
class CameraController;

/** @brief Serializable values matching Pcg CarControllerSetup camera defaults. */
struct PcgCarCameraProfile {
    float targetHeight = 1.5f;
    float distance = 6.f;
    float offsetFromWall = 0.1f;
    float maximumDistance = 20.f;
    float minimumDistance = 0.6f;
    float horizontalSpeed = 200.f;
    float verticalSpeed = 200.f;
    float minimumPitch = -80.f;
    float maximumPitch = 80.f;
    float zoomRate = 40.f;
    float rotationDamping = 0.5f;
    float zoomDamping = 5.f;
    uint64_t collisionLayers = 3841;
    bool lockToRearOfTarget = false;
    bool allowMouseHorizontal = true;
    bool allowMouseVertical = true;
};

/** @brief Caller-owned Pcg vehicle orbit-camera setup and input adapter. */
class PcgCarCameraSetup {
public:
    /** @brief Validate and copy a complete profile without partial mutation. */
    [[nodiscard]] Result<void> configure(const PcgCarCameraProfile& profile);
    /** @brief Apply the profile to a live native controller and its stable target. */
    [[nodiscard]] Result<void> apply(CameraController* controller, graphics::Camera3D* camera,
                                     scene::SceneNodeRef* focus, float initialYaw, float initialPitch);
    /** @brief Apply one device-independent mouse/scroll frame. */
    [[nodiscard]] Result<void> update(CameraController* controller, float mouseX, float mouseY,
                                      float scroll, float dt, bool targetIsMoving, float targetYaw);
    /** @brief Return the copied profile. */
    const PcgCarCameraProfile& getProfile() const noexcept { return profile_; }
    /** @brief Return current orbit yaw. */ float getYaw() const noexcept { return yaw_; }
    /** @brief Return current orbit pitch. */ float getPitch() const noexcept { return pitch_; }
private:
    PcgCarCameraProfile profile_{};
    float yaw_ = 0.f;
    float pitch_ = 0.f;
    bool configured_ = false;
};

/** @brief Register Pcg vehicle camera bindings. */
void exposePcgCarCameraSetupBindings(ssq::Table& table);
}
