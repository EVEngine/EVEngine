#include "gpuagents/solvers/BirdSolver.h"

#include "gpuagents/solvers/BoidsCommon.h"

#include "common/Assert.h"

#include <algorithm>
#include <cmath>

namespace eve::gpuagents {

void BirdSolver::step(std::span<const AgentState> read, std::span<AgentState> write, const EffectProfile& profile,
                      const EnvironmentSnapshot& env, float dt) {
    EV_PARAM_CHECK(profile.kind == EffectKind::Bird);
    EV_PARAM_CHECK(read.size() == write.size());
    const BirdProfile& p = profile.bird;

    for (size_t i = 0; i < read.size(); ++i) {
        const AgentState& in = read[i];
        if (in.alive == 0) {
            write[i] = in;
            continue;
        }

        glm::vec3 forward = in.velocity;
        if (glm::length(forward) < 1e-4f) forward = glm::vec3(0.f, 0.f, 1.f);
        else
            forward = glm::normalize(forward);

        const auto acc = detail::gatherNeighbors(read, static_cast<int>(i), forward, p.separationRadius,
                                                 p.cohesionRadius, p.alignmentRadius, p.perceptionFovDegrees,
                                                 p.maxNeighborSamples);

        glm::vec3 steer(0.f);
        if (acc.sepWeightSum > 0.f) steer += (acc.separation / acc.sepWeightSum) * p.separationWeight;
        if (acc.cohWeightSum > 0.f) {
            const glm::vec3 center = acc.cohesionPos / acc.cohWeightSum;
            const glm::vec3 toC    = center - in.position;
            const float     len    = glm::length(toC);
            if (len > 1e-4f) steer += (toC / len) * p.cohesionWeight;
        }
        if (acc.aliWeightSum > 0.f) {
            steer += (acc.alignmentVel / acc.aliWeightSum - in.velocity) * p.alignmentWeight;
        }
        steer += detail::markerForce(in.position, env.goals, true) * p.goalWeight;

        // Wind response (blend toward uniform + gust).
        const glm::vec3 windTarget = p.uniformWindLocal + env.windVelocity;
        const float     windAlpha  = 1.f - std::exp(-dt / std::max(p.windResponseTime, 1e-3f));
        glm::vec3       windVel    = glm::mix(glm::vec3(in.customData[1], in.customData[2], in.customData[3]),
                                              windTarget, windAlpha);

        // Flight dynamics.
        glm::vec3 vel = in.velocity;
        float     speed = glm::length(vel);
        if (speed < 1e-4f) {
            vel   = forward * p.cruiseSpeed;
            speed = p.cruiseSpeed;
        }
        const glm::vec3 dir = vel / speed;

        const glm::vec3 relAir = vel - windVel;
        const float     airSpeed = std::max(glm::length(relAir), 1e-3f);
        const glm::vec3 liftDir =
            glm::normalize(glm::cross(glm::cross(dir, glm::vec3(0.f, 1.f, 0.f)), dir) + glm::vec3(0.f, 1e-4f, 0.f));
        const float liftMag = p.liftCoefficient * airSpeed * airSpeed * 0.05f;
        glm::vec3   accel   = steer + liftDir * liftMag;
        accel += glm::vec3(0.f, -9.8f * p.gravityScale, 0.f);
        accel -= relAir * (p.airDrag * airSpeed);
        accel += glm::normalize(windTarget - windVel + glm::vec3(1e-5f, 0.f, 0.f)) * p.gustAcceleration * 0.1f;

        // Stall recovery: pitch up when below stall speed.
        if (speed < p.stallSpeed) {
            accel += glm::vec3(0.f, (p.stallSpeed - speed) * 4.f, 0.f);
            accel += dir * ((p.cruiseSpeed - speed) * 2.f);
        }

        // Predictive clearance vs ground / ceiling.
        const float ttc = p.timeToCollision;
        const float predY = in.position.y + vel.y * ttc;
        if (predY < env.groundY + p.groundClearance) {
            accel.y += (env.groundY + p.groundClearance - predY) * 6.f;
        }
        if (predY > env.ceilingY - p.ceilingClearance) {
            accel.y -= (predY - (env.ceilingY - p.ceilingClearance)) * 6.f;
        }

        vel += accel * dt;
        speed = glm::length(vel);
        if (speed > 1e-6f) {
            // Turn-rate limit.
            glm::vec3 newDir = vel / speed;
            const float ang  = std::acos(std::clamp(glm::dot(dir, newDir), -1.f, 1.f));
            const float maxA = p.maxTurnRate * dt;
            if (ang > maxA && ang > 1e-5f) newDir = glm::normalize(glm::mix(dir, newDir, maxA / ang));
            speed = std::clamp(speed, p.stallSpeed * 0.5f, p.maxSpeed);
            vel   = newDir * speed;
            vel.y = std::clamp(vel.y, -p.maxDescentSpeed, p.maxClimbSpeed);
        }

        // Bank from horizontal turn (stored in customData[0]).
        glm::vec3 prevH = glm::normalize(glm::vec3(dir.x, 0.f, dir.z) + glm::vec3(1e-5f, 0.f, 0.f));
        glm::vec3 newH  = glm::normalize(glm::vec3(vel.x, 0.f, vel.z) + glm::vec3(1e-5f, 0.f, 0.f));
        const float crossY = prevH.x * newH.z - prevH.z * newH.x;
        float       bank   = std::clamp(crossY * 4.f, -p.maxBankAngle, p.maxBankAngle);

        AgentState out       = in;
        out.velocity         = vel;
        out.position         = in.position + vel * dt;
        out.rotation         = lookRotation(vel, glm::vec3(std::sin(bank), std::cos(bank), 0.f));
        out.age              = in.age + dt;
        out.customData[0]    = bank;
        out.customData[1]    = windVel.x;
        out.customData[2]    = windVel.y;
        out.customData[3]    = windVel.z;
        out.alive            = in.alive;

        if (env.obstacles) {
            env.obstacles->resolve(out.position, out.velocity, p.base.agentRadius, p.base.obstaclePredictTime);
        }
        write[i] = out;
    }
}

}  // namespace eve::gpuagents
