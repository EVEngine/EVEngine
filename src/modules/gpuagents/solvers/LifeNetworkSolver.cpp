#include "gpuagents/solvers/LifeNetworkSolver.h"

#include "gpuagents/solvers/BoidsCommon.h"

#include "common/Assert.h"

#include <cmath>
#include <vector>

namespace eve::gpuagents {

void LifeNetworkSolver::step(std::span<const AgentState> read, std::span<AgentState> write,
                             const EffectProfile& profile, const EnvironmentSnapshot& env, float dt) {
    EV_PARAM_CHECK(profile.kind == EffectKind::LifeNetwork);
    EV_PARAM_CHECK(read.size() == write.size());
    const LifeNetworkProfile& p = profile.life;
    EV_PARAM_CHECK(env.surface != nullptr);

    SurfaceField& surface = *env.surface;
    std::vector<glm::vec3> deposits;
    deposits.reserve(read.size());

    for (size_t i = 0; i < read.size(); ++i) {
        const AgentState& in = read[i];
        if (in.alive == 0) {
            write[i] = in;
            continue;
        }

        glm::vec3 heading = in.velocity;
        if (glm::length(glm::vec2(heading.x, heading.z)) < 1e-4f) heading = glm::vec3(0.f, 0.f, 1.f);
        heading.y = 0.f;
        heading   = glm::normalize(heading);

        const float s = std::sin(p.sensorAngle);
        const float c = std::cos(p.sensorAngle);
        const glm::vec3 leftDir(heading.x * c + heading.z * s, 0.f, -heading.x * s + heading.z * c);
        const glm::vec3 rightDir(heading.x * c - heading.z * s, 0.f, heading.x * s + heading.z * c);

        const auto sense = [&](const glm::vec3& dir) {
            const glm::vec3 sp = in.position + dir * p.sensorDistance;
            const glm::vec4 life = surface.sampleLife(sp.x, sp.z);
            const glm::vec3 n    = surface.sampleNormal(sp.x, sp.z);
            float score = life.r * p.trailFollow + life.g * p.nutrient - life.b * p.danger;
            // Soft repulsion from saturated trails encourages exploration.
            score -= life.r * life.r * p.repulsion * 0.25f;
            if (n.y < p.minSurfaceNormalZ) score -= 10.f;
            for (const auto& d : env.dangers) {
                const float dist = glm::length(sp - d.position);
                if (dist < d.radius) score -= detail::compactWeight(dist, d.radius) * d.strength * p.danger;
            }
            for (const auto& nMark : env.nutrients) {
                const float dist = glm::length(sp - nMark.position);
                if (dist < nMark.radius)
                    score += detail::compactWeight(dist, nMark.radius) * nMark.strength * p.nutrient;
            }
            return score;
        };

        const float scoreF = sense(heading);
        const float scoreL = sense(leftDir);
        const float scoreR = sense(rightDir);

        glm::vec3 choose = heading;
        if (scoreL > scoreF && scoreL >= scoreR) choose = leftDir;
        else if (scoreR > scoreF && scoreR > scoreL)
            choose = rightDir;

        AgentState out   = in;
        out.velocity     = choose * p.moveSpeed;
        out.position     = in.position + out.velocity * dt;
        out.position     = surface.project(out.position);
        const glm::vec3 n = surface.sampleNormal(out.position.x, out.position.z);
        if (n.y < p.minSurfaceNormalZ) {
            // Slide back toward previous feasible point.
            out.position = surface.project(in.position);
            out.velocity = -choose * p.moveSpeed * 0.5f;
        }
        out.rotation      = lookRotation(out.velocity, n);
        out.age           = in.age + dt;
        out.customData[0] = surface.sampleLife(out.position.x, out.position.z).r;
        out.alive         = in.alive;
        deposits.push_back(out.position);
        write[i] = out;
    }

    surface.stepLife(dt, p.trailDecayRate, p.freshnessHalfLife, p.diffusionRate, deposits, p.depositStrength);
}

}  // namespace eve::gpuagents
