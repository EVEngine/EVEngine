#include "gpuagents/solvers/PetalSolver.h"

#include "common/Assert.h"

#include <algorithm>
#include <cmath>

namespace eve::gpuagents {
namespace {

/** @brief customData: [0]=settled, [1]=sizeScale, [2]=unused, [3]=spinRate */
constexpr int kSettled = 0;
constexpr int kScale   = 1;
constexpr int kSpin    = 3;

}  // namespace

void PetalSolver::step(std::span<const AgentState> read, std::span<AgentState> write, const EffectProfile& profile,
                       const EnvironmentSnapshot& env, float dt) {
    EV_PARAM_CHECK(profile.kind == EffectKind::Petal);
    EV_PARAM_CHECK(read.size() == write.size());
    const PetalProfile& p = profile.petal;

    for (size_t i = 0; i < read.size(); ++i) {
        const AgentState& in = read[i];
        if (in.alive == 0) {
            write[i] = in;
            continue;
        }

        AgentState out = in;
        const float scale = in.customData[kScale] > 1e-4f ? in.customData[kScale] : 1.f;
        const float length = p.length * scale;
        const float width  = p.width * scale;
        const float mass   = std::max(p.mass * scale, 1e-5f);

        if (in.customData[kSettled] > 0.5f) {
            out.velocity *= 0.9f;
            out.position.y = p.groundY + length * 0.02f;
            out.age += dt;
            if (out.age > p.lifetime) {
                out.position =
                    glm::vec3(glm::mix(p.worldMin.x, p.worldMax.x, 0.5f), p.worldMax.y * 0.8f,
                              glm::mix(p.worldMin.z, p.worldMax.z, 0.5f));
                out.velocity             = env.windVelocity * 0.5f + glm::vec3(0.f, -0.5f, 0.f);
                out.age                  = 0.f;
                out.customData[kSettled] = 0.f;
            }
            write[i] = out;
            continue;
        }

        const glm::mat3 R      = rotationMatrix(in.rotation);
        const glm::vec3 normal = R[1];
        const glm::vec3 edge   = R[0];

        glm::vec3 wind = env.windVelocity;
        if (env.obstacles) {
            for (const auto& src : env.obstacles->dynamicSources) {
                const glm::vec3 d    = in.position - src.center;
                const float     dist = glm::length(d);
                if (dist < src.radius * 3.f && dist > 1e-4f) {
                    const glm::vec3 tangential = glm::cross(d, glm::vec3(0.f, 1.f, 0.f));
                    if (glm::length(tangential) > 1e-4f) {
                        wind += glm::normalize(tangential) * (1.5f / dist);
                    }
                }
            }
        }

        const glm::vec3 rel    = in.velocity - wind;
        const float     speed  = glm::length(rel);
        const glm::vec3 relDir = speed > 1e-5f ? rel / speed : glm::vec3(0.f, -1.f, 0.f);

        const float areaFront = length * width;
        const float areaEdge  = length * p.thickness * scale;
        const float cosA      = std::abs(glm::dot(normal, relDir));
        const float sinA      = std::sqrt(std::max(0.f, 1.f - cosA * cosA));

        glm::vec3 force = glm::vec3(0.f, -p.gravity * mass, 0.f);
        force -= relDir * (0.5f * speed * speed * (p.frontalDrag * areaFront * cosA + p.edgeDrag * areaEdge * sinA));
        glm::vec3 liftAxis = glm::cross(relDir, normal);
        if (glm::length(liftAxis) > 1e-4f) {
            liftAxis = glm::normalize(liftAxis);
            force += glm::cross(liftAxis, relDir) * (p.liftCoeff * areaFront * speed * speed * sinA * 0.5f);
        }

        out.velocity = in.velocity + (force / mass) * dt;
        out.position = in.position + out.velocity * dt;

        const glm::vec3 lever  = edge * (length * p.pressureCenterOffset);
        const glm::vec3 torque = glm::cross(lever, force);
        const float inertia    = mass * (length * length + width * width) / 12.f;
        float       spin       = in.customData[kSpin];
        spin += (glm::length(torque) / std::max(inertia, 1e-8f) - p.angularDrag * spin) * dt;
        spin = std::clamp(spin, -40.f, 40.f);
        const glm::vec3 axis =
            glm::length(torque) > 1e-6f ? glm::normalize(torque) : edge;
        out.rotation           = glm::normalize(glm::angleAxis(spin * dt, axis) * in.rotation);
        out.customData[kSpin]  = spin;
        out.customData[kScale] = scale;
        out.age                = in.age + dt;

        if (out.position.y <= p.groundY + length * 0.05f && out.velocity.y <= p.settleSpeed) {
            out.position.y           = p.groundY + length * 0.02f;
            out.velocity.y           = 0.f;
            out.velocity.x *= 0.5f;
            out.velocity.z *= 0.5f;
            out.customData[kSettled] = 1.f;
        }

        const glm::vec3 pad(p.softBoundPadding);
        const bool outOfBounds = out.position.x < p.worldMin.x - pad.x || out.position.y < p.worldMin.y - pad.y ||
                                 out.position.z < p.worldMin.z - pad.z || out.position.x > p.worldMax.x + pad.x ||
                                 out.position.y > p.worldMax.y + pad.y || out.position.z > p.worldMax.z + pad.z;
        if (outOfBounds || out.age > p.lifetime) {
            out.position = glm::vec3(glm::mix(p.worldMin.x + 1.f, p.worldMax.x - 1.f,
                                              static_cast<float>((i * 17) % 100) / 100.f),
                                     p.worldMax.y * 0.85f,
                                     glm::mix(p.worldMin.z + 1.f, p.worldMax.z - 1.f,
                                              static_cast<float>((i * 31) % 100) / 100.f));
            out.velocity             = env.windVelocity + glm::vec3(0.f, -1.f, 0.f);
            out.age                  = 0.f;
            out.customData[kSettled] = 0.f;
            out.customData[kSpin]    = 0.f;
        }

        if (env.obstacles) {
            env.obstacles->resolve(out.position, out.velocity, p.base.agentRadius, p.base.obstaclePredictTime);
        }
        write[i] = out;
    }
}

}  // namespace eve::gpuagents
