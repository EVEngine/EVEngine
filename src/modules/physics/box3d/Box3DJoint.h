#pragma once
#include <box3d/id.h>
#include <memory>
#include "physics/backend/JointBackend3D.h"
namespace eve::physics::box3d {
/** @brief Adopts a live Box3D constraint; internal native composition boundary, simulation thread only. */
[[nodiscard]] std::unique_ptr<IJointBackend3D> adoptJoint(b3JointId id, JointKind3D kind);
}  // namespace eve::physics::box3d
