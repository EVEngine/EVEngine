#pragma once
namespace eve::physics {
/** @brief Allocation-free owning three-dimensional physics value. */
struct PhysicsVector3D {
    float x = 0.f, y = 0.f, z = 0.f;
};
}  // namespace eve::physics
