#include "physics/bake/BakeRigidTransform.h"
#include <cmath>
#include <limits>
#include "physics/bake/RigidPoseTrack.h"
namespace eve::physics {
namespace {
Diagnostic transformError(const char* message) {
    return Diagnostic::error(DiagnosticCode::InvalidArgument, message, "bakeRigidTransform");
}
}  // namespace
Result<void> BakeRigidTransform::set(BakeRigidPlacement placement) {
    for (float value : {placement.x, placement.y, placement.z, placement.rotationX, placement.rotationY,
                        placement.rotationZ, placement.rotationW})
        if (!std::isfinite(value)) return Result<void>::failure(transformError("nonfinite playback placement"));
    const double x = placement.rotationX, y = placement.rotationY, z = placement.rotationZ, w = placement.rotationW;
    const double norm = std::sqrt(x * x + y * y + z * z + w * w);
    if (norm == 0) return Result<void>::failure(transformError("zero placement quaternion"));
    placement.rotationX = static_cast<float>(x / norm);
    placement.rotationY = static_cast<float>(y / norm);
    placement.rotationZ = static_cast<float>(z / norm);
    placement.rotationW = static_cast<float>(w / norm);
    placement_          = placement;
    return Result<void>::success();
}
BakeRigidPlacement     BakeRigidTransform::placement() const noexcept { return placement_; }
Result<RigidTrackPose> BakeRigidTransform::apply(RigidTrackPose pose) const {
    for (float value : {pose.x, pose.y, pose.z, pose.qx, pose.qy, pose.qz, pose.qw})
        if (!std::isfinite(value)) return Result<RigidTrackPose>::failure(transformError("nonfinite pose"));
    // Double intermediates avoid overflow before the final representability check.
    const double qx = placement_.rotationX, qy = placement_.rotationY, qz = placement_.rotationZ,
                 qw   = placement_.rotationW;
    const double norm = qx * qx + qy * qy + qz * qz + qw * qw;
    const double tx   = 2 * (qy * pose.z - qz * pose.y) / norm;
    const double ty   = 2 * (qz * pose.x - qx * pose.z) / norm;
    const double tz   = 2 * (qx * pose.y - qy * pose.x) / norm;
    const double px   = pose.x + qw * tx + qy * tz - qz * ty + placement_.x;
    const double py   = pose.y + qw * ty + qz * tx - qx * tz + placement_.y;
    const double pz   = pose.z + qw * tz + qx * ty - qy * tx + placement_.z;
    for (double value : {px, py, pz})
        if (!std::isfinite(value) || std::abs(value) > std::numeric_limits<float>::max())
            return Result<RigidTrackPose>::failure(transformError("placed sample position overflow"));
    const double bx = pose.qx, by = pose.qy, bz = pose.qz, bw = pose.qw;
    const double rx     = qw * bx + qx * bw + qy * bz - qz * by;
    const double ry     = qw * by - qx * bz + qy * bw + qz * bx;
    const double rz     = qw * bz + qx * by - qy * bx + qz * bw;
    const double rw     = qw * bw - qx * bx - qy * by - qz * bz;
    const double length = std::sqrt(rx * rx + ry * ry + rz * rz + rw * rw);
    if (length == 0) return Result<RigidTrackPose>::failure(transformError("zero pose quaternion"));
    pose.x  = static_cast<float>(px);
    pose.y  = static_cast<float>(py);
    pose.z  = static_cast<float>(pz);
    pose.qx = static_cast<float>(rx / length);
    pose.qy = static_cast<float>(ry / length);
    pose.qz = static_cast<float>(rz / length);
    pose.qw = static_cast<float>(rw / length);
    return Result<RigidTrackPose>::success(pose);
}
}  // namespace eve::physics
