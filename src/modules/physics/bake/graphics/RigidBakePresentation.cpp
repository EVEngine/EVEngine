#include "physics/bake/graphics/RigidBakePresentation.h"
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <numbers>
#include "graphics/RenderSystem.h"
#include "graphics/RenderSystem3D.h"
#include "physics/bake/RigidPlayback.h"
namespace eve::physics {
Result<void> submitRigidPlayback(const RigidPlayback& player, PersistentId identity, graphics::Renderable3D& target) {
    if (player.track().space() != RigidTrackSpace::Metres3D)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "3D renderer requires metre-space rigid playback",
                                                       "rigidBakePresentation"));
    auto sampled = player.sample(identity);
    if (!sampled) return Result<void>::failure(sampled.status());
    const auto pose      = sampled.value();
    auto       transform = target.transform();
    const auto matrix    = glm::translate(glm::mat4(1.f), glm::vec3(pose.x, pose.y, pose.z)) *
                        glm::mat4_cast(glm::quat(pose.qw, pose.qx, pose.qy, pose.qz)) *
                        glm::scale(glm::mat4(1.f), glm::vec3(transform->sx, transform->sy, transform->sz));
    for (int column = 0; column < 4; ++column)
        for (int row = 0; row < 4; ++row)
            if (!std::isfinite(matrix[column][row]))
                return Result<void>::failure(Diagnostic::error(
                    DiagnosticCode::InvalidArgument, "rigid renderer matrix is not finite", "rigidBakePresentation"));
    transform->x              = pose.x;
    transform->y              = pose.y;
    transform->z              = pose.z;
    transform->linkedWorld    = matrix;
    transform->hasLinkedWorld = true;
    return Result<void>::success();
}
Result<void> submitRigidPlayback(const RigidPlayback& player, PersistentId identity, graphics::Renderable2D& target) {
    if (player.track().space() != RigidTrackSpace::PlanarPixels)
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "2D renderer requires planar pixel playback", "rigidBakePresentation"));
    auto sampled = player.sample(identity);
    if (!sampled) return Result<void>::failure(sampled.status());
    const auto  pose      = sampled.value();
    const float degrees   = 2.f * std::atan2(pose.qz, pose.qw) * (180.f / std::numbers::pi_v<float>);
    auto        transform = target.transform();
    transform->x          = pose.x;
    transform->y          = pose.y;
    transform->rot        = degrees;
    return Result<void>::success();
}
}  // namespace eve::physics
