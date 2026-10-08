#include "gpuagents/solvers/FishSolver.h"

#include "gpuagents/solvers/BoidsCommon.h"

#include "common/Assert.h"

namespace eve::gpuagents {

void FishSolver::step(std::span<const AgentState> read, std::span<AgentState> write, const EffectProfile& profile,
                      const EnvironmentSnapshot& env, float dt) {
    EV_PARAM_CHECK(profile.kind == EffectKind::Fish);
    EV_PARAM_CHECK(read.size() == write.size());
    const FishProfile& p = profile.fish;

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

        glm::vec3 force(0.f);
        if (acc.sepWeightSum > 0.f) force += (acc.separation / acc.sepWeightSum) * p.separationWeight;
        if (acc.cohWeightSum > 0.f) {
            const glm::vec3 center = acc.cohesionPos / acc.cohWeightSum;
            const glm::vec3 toC    = center - in.position;
            const float     len    = glm::length(toC);
            if (len > 1e-4f) force += (toC / len) * p.cohesionWeight;
        }
        if (acc.aliWeightSum > 0.f) {
            const glm::vec3 avgV = acc.alignmentVel / acc.aliWeightSum;
            const glm::vec3 delta = avgV - in.velocity;
            force += delta * p.alignmentWeight;
        }

        force += detail::markerForce(in.position, env.goals, true) * p.goalWeight;
        force += detail::markerForce(in.position, env.dangers, false) * p.dangerWeight;
        force += env.waterCurrent * p.currentWeight;

        const float depthErr = (in.position.y - p.preferredDepth) / std::max(p.depthTolerance, 1e-3f);
        force.y -= depthErr * p.depthWeight;

        AgentState out;
        detail::integrateBoid(out, in, force, dt, p.maxAcceleration, p.maxSpeed, p.maxHorizontalTurn,
                              p.maxVerticalTurn);
        if (env.obstacles) {
            env.obstacles->resolve(out.position, out.velocity, p.base.agentRadius, p.base.obstaclePredictTime);
            out.rotation = lookRotation(out.velocity);
        }
        write[i] = out;
    }
}

}  // namespace eve::gpuagents
