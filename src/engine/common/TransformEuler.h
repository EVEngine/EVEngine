#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#ifndef GLM_ENABLE_EXPERIMENTAL
#define GLM_ENABLE_EXPERIMENTAL
#endif
#include <glm/gtx/euler_angles.hpp>

namespace eve {

/**
 * @brief Yaw/pitch/roll for the engine's transform order `T * Ry * Rx * Rz * S`.
 *
 * Everything that composes a node, renderable or body transform uses that order:
 * scene's `localMatrix()`, `RenderSystem3D`'s `modelFromTransform()` and physics'
 * `pushBody3D()`. A conversion from a matrix or quaternion back to yaw/pitch/roll
 * must therefore invert *that* order, and `glm::extractEulerAngleYXZ` is exactly
 * its inverse (GLM documents it as extracting the `Y * X * Z` angles).
 *
 * `glm::eulerAngles()` inverts a *different* Euler family, so using it here
 * silently produced wrong orientations for any multi-axis rotation. Single-axis
 * rotations happen to agree between the two families, which is why this survived
 * for so long. Extracting and recomposing is a round trip, so both sides must use
 * the same helper -- never `glm::eulerAngles()` on an engine transform.
 *
 * Note the output order: yaw first, then pitch, then roll. `glm::eulerAngles()`
 * produced the pitch/yaw/roll order instead, so call sites that read `.x/.y/.z`
 * must map them onto yaw/pitch/roll explicitly.
 *
 * @param[in]  m Rotation matrix, or one whose scale has already been divided out.
 * @param[out] yaw Set to the rotation about +Y.
 * @param[out] pitch Set to the rotation about +X.
 * @param[out] roll Set to the rotation about +Z.
 *
 * @note Ownership/lifetime: the parameters are plain scalar out-references, written
 * once before the call returns and never retained, aliased or freed by this helper,
 * so callers keep ownership of their own storage. This mirrors the signature of
 * `glm::extractEulerAngleYXZ`, which it forwards to.
 */
template <typename T>
inline void eulerYxzFromMatrix(const glm::mat<4, 4, T, glm::defaultp>& m, T& yaw, T& pitch, T& roll) {
    glm::extractEulerAngleYXZ(m, yaw, pitch, roll);
}

/** @brief Quaternion overload of eulerYxzFromMatrix(); float and double both work. */
template <typename T>
inline void eulerYxzFromQuat(const glm::qua<T, glm::defaultp>& q, T& yaw, T& pitch, T& roll) {
    eulerYxzFromMatrix(glm::mat4_cast(glm::normalize(q)), yaw, pitch, roll);
}

}  // namespace eve
