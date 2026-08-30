#pragma once

// Internal bounds math shared between the scene module and the scene-picking
// entry points that live in graphics (graphics/ScenePicking.cpp). This is not
// a public API: scene is the owner of node bounds, graphics just consumes it.
// Keeping the helpers here (instead of duplicating them in both modules) means
// scene/Scene.cpp and graphics/ScenePicking.cpp stay in step.

#include "scene/SceneHost.h"

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace eve::scene {

/** @brief Axis-aligned box in world space. */
struct AABB3f {
    glm::vec3 min;
    glm::vec3 max;
};

/** @brief World-space AABB of a node's local bounds (8 corners through world matrix). */
inline AABB3f worldBoundsOf(const SceneNode &n) {
    glm::vec3 lo(n.bminX, n.bminY, n.bminZ);
    glm::vec3 hi(n.bmaxX, n.bmaxY, n.bmaxZ);
    glm::vec3 mn(std::numeric_limits<float>::max());
    glm::vec3 mx(std::numeric_limits<float>::lowest());
    for (int i = 0; i < 8; ++i) {
        glm::vec3 p((i & 1) ? hi.x : lo.x, (i & 2) ? hi.y : lo.y,
                    (i & 4) ? hi.z : lo.z);
        glm::vec4 w = n.world * glm::vec4(p, 1.f);
        for (int c = 0; c < 3; ++c) {
            mn[c] = std::min(mn[c], w[c]);
            mx[c] = std::max(mx[c], w[c]);
        }
    }
    return {mn, mx};
}

/** @brief Slab ray-AABB intersection; fills entry/exit t (t0 <= t1). */
inline bool rayAABB(const glm::vec3 &o, const glm::vec3 &d, const AABB3f &b, float &t0,
                    float &t1) {
    t0 = -std::numeric_limits<float>::max();
    t1 = std::numeric_limits<float>::max();
    for (int axis = 0; axis < 3; ++axis) {
        const float oa = o[axis];
        const float da = d[axis];
        const float mn = b.min[axis];
        const float mx = b.max[axis];
        if (std::fabs(da) < 1e-12f) {
            if (oa < mn || oa > mx) return false;
        } else {
            const float inv = 1.f / da;
            float tA = (mn - oa) * inv;
            float tB = (mx - oa) * inv;
            if (tA > tB) std::swap(tA, tB);
            t0 = std::max(t0, tA);
            t1 = std::min(t1, tB);
            if (t0 > t1) return false;
        }
    }
    return true;
}

inline bool cornerInsideClip(const glm::mat4 &m, const glm::vec3 &p) {
    const glm::vec4 c = m * glm::vec4(p, 1.f);
    return c.w > 1e-8f && std::fabs(c.x) <= c.w && std::fabs(c.y) <= c.w &&
           c.z >= 0.f && c.z <= c.w;
}

/** @brief Conservative AABB ↔ frustum overlap (AABB corners + frustum corners). */
inline bool aabbIntersectsFrustum(const glm::mat4 &clip, const glm::mat4 &invClip,
                                  const AABB3f &b) {
    for (int i = 0; i < 8; ++i) {
        const glm::vec3 p((i & 1) ? b.max.x : b.min.x, (i & 2) ? b.max.y : b.min.y,
                          (i & 4) ? b.max.z : b.min.z);
        if (cornerInsideClip(clip, p)) return true;
    }
    for (int i = 0; i < 8; ++i) {
        const glm::vec3 ndc((i & 1) ? 1.f : -1.f, (i & 2) ? 1.f : -1.f,
                            (i & 4) ? 1.f : 0.f);
        const glm::vec4 w = invClip * glm::vec4(ndc, 1.f);
        if (std::fabs(w.w) < 1e-8f) continue;
        const glm::vec3 q = glm::vec3(w) / w.w;
        if (q.x >= b.min.x && q.x <= b.max.x && q.y >= b.min.y && q.y <= b.max.y &&
            q.z >= b.min.z && q.z <= b.max.z) {
            return true;
        }
    }
    return false;
}

}  // namespace eve::scene
