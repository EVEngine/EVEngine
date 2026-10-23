#pragma once

#include "gpuagents/AgentState.h"
#include "gpuagents/EnvironmentSnapshot.h"

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <span>

namespace eve::gpuagents::detail {

/** @brief Compact-support radial weight w(q)=(1-q)^2 for q in [0,1). */
inline float compactWeight(float dist, float radius) {
    if (radius <= 1e-6f || dist >= radius) return 0.f;
    const float q = dist / radius;
    const float t = 1.f - q;
    return t * t;
}

/** @brief True when neighbor direction is inside a forward cone (degrees). */
inline bool isInFieldOfView(const glm::vec3& forward, const glm::vec3& toNeighbor, float fovDegrees) {
    if (fovDegrees >= 359.f) return true;
    const float len = glm::length(toNeighbor);
    if (len < 1e-6f) return true;
    const float cosHalf = std::cos(0.5f * glm::radians(fovDegrees));
    /** @brief Dot. */
    return glm::dot(glm::normalize(forward), toNeighbor / len) >= cosHalf;
}

/** @brief NeighborAccum public API. */
struct NeighborAccum {
    glm::vec3 separation{0.f};
    glm::vec3 cohesionPos{0.f};
    glm::vec3 alignmentVel{0.f};
    float     sepWeightSum = 0.f;
    float     cohWeightSum = 0.f;
    float     aliWeightSum = 0.f;
    int       samples      = 0;
};

/**
 * @brief Gather neighborhood forces with compact weights and FOV / sample caps.
 */
inline NeighborAccum gatherNeighbors(std::span<const AgentState> agents, int selfIndex, const glm::vec3& forward,
                                     float sepR, float cohR, float aliR, float fovDeg, int maxSamples) {
    NeighborAccum acc;
    const glm::vec3 selfPos = agents[static_cast<size_t>(selfIndex)].position;
    const float     maxR    = std::max({sepR, cohR, aliR});
    for (int j = 0; j < static_cast<int>(agents.size()) && acc.samples < maxSamples; ++j) {
        if (j == selfIndex || agents[static_cast<size_t>(j)].alive == 0) continue;
        const glm::vec3 delta = agents[static_cast<size_t>(j)].position - selfPos;
        const float     dist  = glm::length(delta);
        if (dist > maxR || dist < 1e-5f) continue;
        if (!isInFieldOfView(forward, delta, fovDeg)) continue;

        if (const float w = compactWeight(dist, sepR); w > 0.f) {
            acc.separation -= (delta / dist) * w;
            acc.sepWeightSum += w;
        }
        if (const float w = compactWeight(dist, cohR); w > 0.f) {
            acc.cohesionPos += agents[static_cast<size_t>(j)].position * w;
            acc.cohWeightSum += w;
        }
        if (const float w = compactWeight(dist, aliR); w > 0.f) {
            acc.alignmentVel += agents[static_cast<size_t>(j)].velocity * w;
            acc.aliWeightSum += w;
        }
        ++acc.samples;
    }
    return acc;
}

/** @brief Marker force. */
inline glm::vec3 markerForce(const glm::vec3& pos, const std::vector<EnvironmentMarker>& markers, bool attract) {
    /** @brief F. */
    glm::vec3 f(0.f);
    for (const auto& m : markers) {
        const glm::vec3 d    = m.position - pos;
        const float     dist = glm::length(d);
        if (dist < 1e-4f || dist > m.radius) continue;
        const float w = compactWeight(dist, m.radius) * m.strength;
        f += (attract ? 1.f : -1.f) * (d / dist) * w;
    }
    return f;
}

/** @brief Clamp acceleration then integrate semi-implicit Euler with turn-rate limits. */
inline void integrateBoid(AgentState& out, const AgentState& in, glm::vec3 accel, float dt, float maxAccel,
                          float maxSpeed, float maxHTurn, float maxVTurn) {
    const float aLen = glm::length(accel);
    if (aLen > maxAccel && aLen > 1e-6f) accel *= maxAccel / aLen;

    glm::vec3 vel = in.velocity + accel * dt;
    const float speed = glm::length(vel);
    if (speed > maxSpeed && speed > 1e-6f) vel *= maxSpeed / speed;

    // Horizontal / vertical turn limiting relative to previous heading.
    glm::vec3 prevDir = in.velocity;
    if (glm::length(prevDir) < 1e-4f) prevDir = glm::vec3(0.f, 0.f, 1.f);
    prevDir = glm::normalize(prevDir);
    glm::vec3 newDir = vel;
    if (glm::length(newDir) < 1e-4f) newDir = prevDir;
    else
        newDir = glm::normalize(newDir);

    glm::vec3 prevH = glm::normalize(glm::vec3(prevDir.x, 0.f, prevDir.z) + glm::vec3(1e-5f, 0.f, 0.f));
    glm::vec3 newH  = glm::normalize(glm::vec3(newDir.x, 0.f, newDir.z) + glm::vec3(1e-5f, 0.f, 0.f));
    float     hAng  = std::acos(std::clamp(glm::dot(prevH, newH), -1.f, 1.f));
    const float maxH = maxHTurn * dt;
    if (hAng > maxH && hAng > 1e-5f) {
        const float t = maxH / hAng;
        newH          = glm::normalize(glm::mix(prevH, newH, t));
    }
    float prevPitch = std::asin(std::clamp(prevDir.y, -1.f, 1.f));
    float newPitch  = std::asin(std::clamp(newDir.y, -1.f, 1.f));
    const float maxV = maxVTurn * dt;
    newPitch         = prevPitch + std::clamp(newPitch - prevPitch, -maxV, maxV);
    newDir           = glm::normalize(glm::vec3(newH.x * std::cos(newPitch), std::sin(newPitch), newH.z * std::cos(newPitch)));

    const float outSpeed = std::min(glm::length(vel), maxSpeed);
    out                  = in;
    out.velocity         = newDir * outSpeed;
    out.position         = in.position + out.velocity * dt;
    out.rotation         = lookRotation(out.velocity);
    out.age              = in.age + dt;
    out.alive            = in.alive;
}

}  // namespace eve::gpuagents::detail
