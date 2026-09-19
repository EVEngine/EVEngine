#pragma once

// Internal helpers shared between scene/Scene.cpp and scene/SceneNodes.cpp.
//
// Extracted verbatim from Scene.cpp when the node methods were split out.
// Anonymous namespace: each translation unit gets its own internal copy,
// exactly like the original single file.

#include "scene/SceneHost.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/euler_angles.hpp>

#include <string>

namespace ssq {
class Class;
}

namespace eve::scene {

/**
 * @brief Register `eve.Scene`'s script-link surface.
 * @param cls The `eve.Scene` class being exposed.
 * @remarks Defined in `scene/ScriptLinks.cpp` so the link kind, its marshalling and
 *          its script binding stay in one translation unit.
 */
void exposeScriptLinkBindings(ssq::Class& cls);

namespace {

/**
 * The script API already names link kinds with strings, which is exactly the
 * registry's key, so an unknown name and a kind whose module was trimmed out
 * are the same case: -1.
 */
int linkKindFromString(const std::string &kind) { return findLinkKind(kind.c_str()); }

int syncModeFromString(const std::string &mode) {
    return mode == "body" ? 1 : 0;
}

glm::quat nodeQuaternion(const SceneNode &n) {
    return glm::angleAxis(n.yaw, glm::vec3(0.f, 1.f, 0.f)) *
           glm::angleAxis(n.pitch, glm::vec3(1.f, 0.f, 0.f)) *
           glm::angleAxis(n.roll, glm::vec3(0.f, 0.f, 1.f));
}

/**
 * @brief Yaw/pitch/roll for the Y*X*Z order that `localMatrix()` composes.
 *
 * `glm::eulerAngles()` inverts a *different* order (its pitch/yaw/roll belong to the
 * ZYX family), so extracting with it and feeding the result back through
 * `localMatrix()`/`nodeQuaternion()` mangled every multi-axis rotation — the
 * get/setRotation vs get/setQuaternion round trip did not close. Use this for every
 * quaternion/rotation-matrix -> SceneNode yaw/pitch/roll conversion.
 */
void eulerYxzFromQuat(const glm::quat& q, float& yaw, float& pitch, float& roll) {
    glm::extractEulerAngleYXZ(glm::mat4_cast(glm::normalize(q)), yaw, pitch, roll);
}

/** @brief Bare rotation matrix variant of eulerYxzFromQuat() (scale already removed). */
void eulerYxzFromRotation(const glm::mat3& rot, float& yaw, float& pitch, float& roll) {
    glm::extractEulerAngleYXZ(glm::mat4(rot), yaw, pitch, roll);
}

/** Decompose a world matrix into position / euler(yaw,pitch,roll) / scale. */
void decomposeWorld(const glm::mat4 &w, glm::vec3 &pos, glm::vec3 &euler,
                    glm::vec3 &scale) {
    pos = glm::vec3(w[3]);
    glm::vec3 c0(w[0]), c1(w[1]), c2(w[2]);
    scale = glm::vec3(glm::length(c0), glm::length(c1), glm::length(c2));
    if (scale.x > 1e-8f) c0 /= scale.x;
    if (scale.y > 1e-8f) c1 /= scale.y;
    if (scale.z > 1e-8f) c2 /= scale.z;
    glm::mat3 rot(c0, c1, c2);
    eulerYxzFromRotation(rot, euler.x, euler.y, euler.z);
}

}  // namespace
}  // namespace eve::scene
