#pragma once

// Internal helpers shared between scene/Scene.cpp and scene/SceneNodes.cpp.
//
// Extracted verbatim from Scene.cpp when the node methods were split out.
// Anonymous namespace: each translation unit gets its own internal copy,
// exactly like the original single file.

#include "common/Export.h"
#include "common/TransformEuler.h"
#include "scene/SceneHost.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

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
EVENGINE_API_PLATFORM void exposeScriptLinkBindings(ssq::Class& cls);

namespace {

/**
 * The script API already names link kinds with strings, which is exactly the
 * registry's key, so an unknown name and a kind whose module was trimmed out
 * are the same case: -1.
 */
/** @brief Link kind from string. */
int linkKindFromString(const std::string &kind) { return findLinkKind(kind.c_str()); }

/** @brief Synchronizes mode from string. */
int syncModeFromString(const std::string &mode) {
    return mode == "body" ? 1 : 0;
}

/** @brief Node quaternion. */
glm::quat nodeQuaternion(const SceneNode &n) {
    /** @brief Angle axis. */
    return glm::angleAxis(n.yaw, glm::vec3(0.f, 1.f, 0.f)) *
           /** @brief Angle axis. */
           glm::angleAxis(n.pitch, glm::vec3(1.f, 0.f, 0.f)) *
           /** @brief Angle axis. */
           glm::angleAxis(n.roll, glm::vec3(0.f, 0.f, 1.f));
}

/**
 * Decompose a world matrix into position / euler(yaw,pitch,roll) / scale.
 *
 * The Euler extraction uses eve::eulerYxzFromMatrix() (see common/TransformEuler.h):
 * it is the inverse of the order localMatrix()/nodeQuaternion() compose, so the
 * round trip closes for multi-axis rotations. glm::eulerAngles() belongs to a
 * different family and silently mangled them.
 */
void decomposeWorld(const glm::mat4 &w, glm::vec3 &pos, glm::vec3 &euler,
                    glm::vec3 &scale) {
    pos = glm::vec3(w[3]);
    /** @brief C 0. */
    glm::vec3 c0(w[0]), c1(w[1]), c2(w[2]);
    scale = glm::vec3(glm::length(c0), glm::length(c1), glm::length(c2));
    if (scale.x > 1e-8f) c0 /= scale.x;
    if (scale.y > 1e-8f) c1 /= scale.y;
    if (scale.z > 1e-8f) c2 /= scale.z;
    /** @brief Rot. */
    glm::mat3 rot(c0, c1, c2);
    eve::eulerYxzFromMatrix(glm::mat4(rot), euler.x, euler.y, euler.z);
}

}  // namespace
}  // namespace eve::scene
