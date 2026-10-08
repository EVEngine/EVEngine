#pragma once
#include "common/Export.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>

namespace eve::gpuagents {

/** @brief Standardized per-agent simulation output shared by all effect kinds. */
struct EVENGINE_API_DOMAINS AgentState {
    glm::vec3    position{0.f};
    glm::vec3    velocity{0.f};
    glm::quat    rotation{1.f, 0.f, 0.f, 0.f};  ///< wxyz storage (glm default).
    float        age = 0.f;
    float        customData[4]{0.f, 0.f, 0.f, 0.f};
    std::uint32_t alive = 0;  ///< 0 = recycled / inactive slot.
};

/** @brief Effect kind selecting the solver kernel. */
enum class EffectKind : std::uint32_t {
    Fish        = 0,
    LifeNetwork = 1,
    Bird        = 2,
    Petal       = 3,
};

/** @brief Converts a quaternion to a 3x3 rotation matrix (column-major). */
inline glm::mat3 rotationMatrix(const glm::quat& q) {
    /** @brief Mat 3 cast. */
    return glm::mat3_cast(q);
}

/** @brief Builds a facing quaternion from a unit forward and approximate up. */
inline glm::quat lookRotation(const glm::vec3& forward, const glm::vec3& upHint = glm::vec3(0.f, 1.f, 0.f)) {
    const glm::vec3 f = glm::normalize(forward);
    glm::vec3       u = upHint;
    if (glm::length(glm::cross(f, u)) < 1e-4f) {
        u = glm::abs(f.y) < 0.99f ? glm::vec3(0.f, 1.f, 0.f) : glm::vec3(1.f, 0.f, 0.f);
    }
    const glm::vec3 r = glm::normalize(glm::cross(u, f));
    u                 = glm::cross(f, r);
    /** @brief Normalize. */
    return glm::normalize(glm::quat_cast(glm::mat3(r, u, f)));
}

}  // namespace eve::gpuagents
