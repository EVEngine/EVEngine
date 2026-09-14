#include "fluids/VolumeFluidEmitter.h"
#include <algorithm>
#include <cmath>
#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>

namespace eve::fluids {
namespace {
Result<unsigned> invalidEmission(const char* message) {
    return Result<unsigned>::failure(
        Diagnostic::error(DiagnosticCode::InvalidArgument, message, "fluids.volume.emitter"));
}
bool finiteVector(glm::vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
bool valid(const VolumeFluidEmission& e) {
    if (e.distribution.size() > 4096) return false;
    for (const auto& p : e.distribution) {
        const float directionLength = glm::length(p.direction);
        if (!finiteVector(p.position) || !finiteVector(p.direction) || !std::isfinite(directionLength) ||
            directionLength <= 1e-6f || !finiteVector(glm::vec3(p.color)) || !std::isfinite(p.color.a) ||
            p.color.r < 0.f || p.color.g < 0.f || p.color.b < 0.f || p.color.a < 0.f || p.color.r > 1.f ||
            p.color.g > 1.f || p.color.b > 1.f || p.color.a > 1.f)
            return false;
    }
    const float length = glm::length(e.direction);
    return finiteVector(e.origin) && finiteVector(e.direction) && std::isfinite(length) && length > 1e-6f &&
           finiteVector(e.extent) && e.extent.x >= 0.f && e.extent.y >= 0.f && e.extent.z >= 0.f &&
           e.extent.x <= 10000.f && e.extent.y <= 10000.f && e.extent.z <= 10000.f && std::isfinite(e.speed) &&
           e.speed >= 0.f && std::isfinite(e.jitter) && e.jitter >= 0.f && std::isfinite(e.randomVelocity) &&
           e.randomVelocity >= 0.f && e.randomVelocity <= 1.f && std::isfinite(e.granularRadiusRandomness) &&
           e.granularRadiusRandomness >= 0.f && e.granularRadiusRandomness <= 100.f && e.actorCapacity >= 1 &&
           e.actorCapacity <= 65536 && e.speed + std::sqrt(3.f) * e.jitter <= 100.f &&
           e.shape >= VolumeFluidEmissionShape::Edge && e.shape <= VolumeFluidEmissionShape::Distribution;
}

Result<void> invalidCheckpoint(const char* message) {
    return Result<void>::failure(
        Diagnostic::error(DiagnosticCode::InvalidArgument, message, "fluids.volume.emitterCheckpoint"));
}

template <class Controller>
Result<void> restoreCheckpoint(VolumeFluid& solver, Controller& controller,
                               const VolumeFluidEmitterCheckpoint& checkpoint) {
    if (checkpoint.schema != "eve.volume-fluid-emitter-checkpoint" || checkpoint.version != 1)
        return invalidCheckpoint("Unsupported emitter checkpoint schema/version");
    if (!valid(checkpoint.emission)) return invalidCheckpoint("Invalid checkpoint emission description");
    auto candidateResult = VolumeFluid::create(checkpoint.solver.settings);
    if (!candidateResult) return Result<void>::failure(candidateResult.status());
    auto candidate   = std::move(candidateResult).takeValue();
    auto solverValid = candidate->restore(checkpoint.solver);
    if (!solverValid) return solverValid;
    Controller controllerCandidate;
    auto       controllerValid = controllerCandidate.restore(checkpoint.controller);
    if (!controllerValid) return controllerValid;
    auto solverRestored = solver.restore(checkpoint.solver);
    if (!solverRestored) return solverRestored;
    auto controllerRestored = controller.restore(checkpoint.controller);
    if (!controllerRestored) return controllerRestored;
    return Result<void>::success();
}

float sample(uint32_t& state);
void  randomizeGranularRadius(VolumeFluidParticle& particle, float spacing, float randomness, uint32_t& rng);

Result<unsigned> emitBurst(VolumeFluid& solver, const VolumeFluidEmission& e, unsigned count, uint32_t sequence,
                           size_t distributionOffset, size_t actorFree, float stepOffset, float dt) {
    if (!valid(e) || count > 4096) return invalidEmission("Invalid nozzle or burst work limit");
    if (count > 0 && e.shape == VolumeFluidEmissionShape::Distribution && e.distribution.empty())
        return invalidEmission("Cannot emit a nonempty burst from an empty distribution");
    if (count > solver.availableCapacity()) return invalidEmission("Burst exceeds available pool");
    if (count > actorFree) return invalidEmission("Burst exceeds emitter actor capacity");
    const auto forward           = glm::normalize(e.direction);
    const auto reference         = std::abs(forward.y) < 0.9f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
    const auto right             = glm::normalize(glm::cross(reference, forward));
    const auto up                = glm::cross(forward, right);
    const auto nozzleRotation    = glm::quat_cast(glm::mat3(right, up, forward));
    const auto prototypeRotation = glm::quat(e.prototype.orientation.w, e.prototype.orientation.x,
                                             e.prototype.orientation.y, e.prototype.orientation.z);
    uint32_t   rng               = e.seed + sequence * 0x9e3779b9u;
    std::vector<VolumeFluidParticle> particles;
    particles.reserve(count);
    for (unsigned i = 0; i < count; ++i) {
        const float                         a = sample(rng), b = sample(rng), c = sample(rng);
        glm::vec3                           local(0.f);
        const VolumeFluidDistributionPoint* distributionPoint = nullptr;
        switch (e.shape) {
            case VolumeFluidEmissionShape::Distribution:
                distributionPoint = &e.distribution[(distributionOffset + i) % e.distribution.size()];
                local             = distributionPoint->position;
                break;
            case VolumeFluidEmissionShape::Edge: local.x = (2.f * a - 1.f) * e.extent.x; break;
            case VolumeFluidEmissionShape::Square:
                local = {(2.f * a - 1.f) * e.extent.x, (2.f * b - 1.f) * e.extent.y, 0.f};
                break;
            case VolumeFluidEmissionShape::Cube:
                local = {(2.f * a - 1.f) * e.extent.x, (2.f * b - 1.f) * e.extent.y, (2.f * c - 1.f) * e.extent.z};
                break;
            case VolumeFluidEmissionShape::Disk: {
                const float radius = std::sqrt(a) * e.extent.x, angle = 6.28318530718f * b;
                local = {radius * std::cos(angle), radius * std::sin(angle), 0.f};
                break;
            }
            case VolumeFluidEmissionShape::Sphere: {
                const float z = 2.f * a - 1.f, angle = 6.28318530718f * b;
                const float xy = std::sqrt(std::max(0.f, 1.f - z * z)), radius = std::cbrt(c) * e.extent.x;
                local = radius * glm::vec3(xy * std::cos(angle), xy * std::sin(angle), z);
                break;
            }
        }
        auto particle = e.prototype;
        randomizeGranularRadius(particle, solver.spacing(), e.granularRadiusRandomness, rng);
        const auto worldOrientation = glm::normalize(nozzleRotation * prototypeRotation);
        particle.orientation        = {worldOrientation.x, worldOrientation.y, worldOrientation.z, worldOrientation.w};
        if (distributionPoint && e.useShapeColor) particle.color = distributionPoint->color;
        particle.position = e.origin + right * local.x + up * local.y + forward * local.z;
        const float jx = 2.f * sample(rng) - 1.f, jy = 2.f * sample(rng) - 1.f, jz = 2.f * sample(rng) - 1.f;
        const auto  emissionDirection =
            distributionPoint ? nozzleRotation * glm::normalize(distributionPoint->direction) : forward;
        const float randomZ     = jx;
        const float randomAngle = 3.14159265359f * (jy + 1.f);
        const float randomXY    = std::sqrt(std::max(0.f, 1.f - randomZ * randomZ));
        const auto  randomDirection =
            glm::vec3(randomXY * std::cos(randomAngle), randomXY * std::sin(randomAngle), randomZ);
        const auto spawnDirection = emissionDirection * (1.f - e.randomVelocity) + randomDirection * e.randomVelocity;
        particle.velocity         = spawnDirection * e.speed + e.jitter * glm::vec3(jx, jy, jz);
        particle.position += spawnDirection * e.speed * dt * stepOffset;
        particles.push_back(particle);
    }
    auto admitted = solver.emit(particles);
    if (!admitted) return Result<unsigned>::failure(admitted.status());
    return Result<unsigned>::success(count);
}
// A fixed integer permutation makes sample cost bounded (no rejection sampling).
float sample(uint32_t& state) {
    state += 0x9e3779b9u;
    uint32_t x = state;
    x          = (x ^ (x >> 16u)) * 0x85ebca6bu;
    x          = (x ^ (x >> 13u)) * 0xc2b2ae35u;
    x ^= x >> 16u;
    return float(x >> 8u) * (1.f / 16777216.f);
}
void randomizeGranularRadius(VolumeFluidParticle& particle, float spacing, float randomness, uint32_t& rng) {
    if (randomness == 0.f || particle.material.phase != VolumeFluidPhase::Granular) return;
    const float radius = std::max(.001f, spacing * .5f + .001f - sample(rng) * spacing * (randomness * .01f));
    particle.radii     = glm::vec3(radius);
}
}  // namespace

Result<VolumeFluidEmitterBlueprintMetrics> evaluateVolumeFluidEmitterBlueprint3D(float resolution, float restDensity,
                                                                                 float smoothing) {
    if (!std::isfinite(resolution) || resolution < .001f || resolution > 1000000.f || !std::isfinite(restDensity) ||
        restDensity < .001f || restDensity > 1000000.f || !std::isfinite(smoothing) || smoothing < 1.f ||
        smoothing > 100.f)
        return Result<VolumeFluidEmitterBlueprintMetrics>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "Invalid 3D emitter blueprint metrics", "fluids.volume.emitterBlueprint"));
    VolumeFluidEmitterBlueprintMetrics metrics;
    metrics.particleSize    = 1.f / (10.f * std::cbrt(resolution));
    metrics.particleMass    = restDensity * metrics.particleSize * metrics.particleSize * metrics.particleSize;
    metrics.smoothingRadius = metrics.particleSize * smoothing;
    return Result<VolumeFluidEmitterBlueprintMetrics>::success(metrics);
}

Result<VolumeFluidEmitterBlueprintApplication3D> prepareVolumeFluidEmitterBlueprint3D(
    const VolumeFluidEmitterBlueprint3D& blueprint) {
    const auto metrics =
        evaluateVolumeFluidEmitterBlueprint3D(blueprint.resolution, blueprint.restDensity, blueprint.smoothing);
    const auto finiteRange = [](float value, float minimum, float maximum) {
        return std::isfinite(value) && value >= minimum && value <= maximum;
    };
    if (!metrics || blueprint.capacity == 0 || blueprint.capacity > 65536 ||
        !finiteRange(blueprint.viscosity, 0.f, 100.f) || !finiteRange(blueprint.surfaceTension, 0.f, 10.f) ||
        !finiteRange(blueprint.buoyancy, -10.f, 10.f) || !finiteRange(blueprint.atmosphericDrag, 0.f, 100.f) ||
        !finiteRange(blueprint.atmosphericPressure, 0.f, 1000.f) || !finiteRange(blueprint.vorticity, 0.f, 10.f) ||
        !finiteRange(blueprint.diffusion, 0.f, 100.f) || !finiteVector(glm::vec3(blueprint.diffusionData)) ||
        !std::isfinite(blueprint.diffusionData.w))
        return Result<VolumeFluidEmitterBlueprintApplication3D>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "Invalid 3D fluid emitter blueprint", "fluids.volume.emitterBlueprint"));

    VolumeFluidEmitterBlueprintApplication3D result;
    result.metrics                 = metrics.value();
    result.settings.capacity       = blueprint.capacity;
    result.settings.spacing        = result.metrics.particleSize;
    result.emission.actorCapacity  = blueprint.capacity;
    auto& material                 = result.emission.prototype.material;
    material.phase                 = VolumeFluidPhase::Liquid;
    material.density               = blueprint.restDensity;
    material.smoothing             = blueprint.smoothing;
    material.viscosity             = blueprint.viscosity;
    material.cohesion              = blueprint.surfaceTension;
    material.buoyancy              = blueprint.buoyancy;
    material.drag                  = blueprint.atmosphericDrag;
    material.atmosphericPressure   = blueprint.atmosphericPressure;
    material.vorticity             = blueprint.vorticity;
    material.diffusion             = blueprint.diffusion;
    result.emission.prototype.data = blueprint.diffusionData;
    return Result<VolumeFluidEmitterBlueprintApplication3D>::success(std::move(result));
}

Result<VolumeFluidEmitterBlueprintApplication3D> prepareVolumeGranularEmitterBlueprint3D(
    const VolumeGranularEmitterBlueprint3D& blueprint) {
    const auto metrics = evaluateVolumeFluidEmitterBlueprint3D(blueprint.resolution, blueprint.restDensity, 1.f);
    if (!metrics || blueprint.capacity == 0 || blueprint.capacity > 65536 || !std::isfinite(blueprint.randomness) ||
        blueprint.randomness < 0.f || blueprint.randomness > 100.f)
        return Result<VolumeFluidEmitterBlueprintApplication3D>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "Invalid 3D granular emitter blueprint",
                              "fluids.volume.granularEmitterBlueprint"));

    VolumeFluidEmitterBlueprintApplication3D result;
    result.metrics                             = metrics.value();
    result.metrics.smoothingRadius             = 0.f;
    result.settings.capacity                   = blueprint.capacity;
    result.settings.spacing                    = result.metrics.particleSize;
    result.emission.actorCapacity              = blueprint.capacity;
    result.emission.granularRadiusRandomness   = blueprint.randomness;
    result.emission.prototype.material.phase   = VolumeFluidPhase::Granular;
    result.emission.prototype.material.density = blueprint.restDensity;
    return Result<VolumeFluidEmitterBlueprintApplication3D>::success(std::move(result));
}

Result<unsigned> emitVolumeFluidBurst(VolumeFluid& solver, const VolumeFluidEmission& e, unsigned count) {
    if (!valid(e)) return invalidEmission("Invalid emission description");
    auto active = solver.actorParticleCount(e.prototype.actorGroup);
    if (!active) return Result<unsigned>::failure(active.status());
    const size_t actorFree = e.actorCapacity > active.value() ? e.actorCapacity - active.value() : 0;
    return emitBurst(solver, e, count, 0, 0, actorFree, 0.f, 0.f);
}

Result<unsigned> VolumeFluidEmitter::emitParticle(VolumeFluid& solver, const VolumeFluidEmission& emission,
                                                  float offset, float dt) {
    if (!valid(emission) || !std::isfinite(offset) || offset < 0.f || offset > 1.f || !std::isfinite(dt) || dt < 0.f ||
        dt > 1.f / 30.f)
        return invalidEmission("Invalid direct particle emission offset or duration");
    auto active = solver.actorParticleCount(emission.prototype.actorGroup);
    if (!active) return Result<unsigned>::failure(active.status());
    const size_t actorFree = emission.actorCapacity > active.value() ? emission.actorCapacity - active.value() : 0;
    if (actorFree == 0 || solver.availableCapacity() == 0 ||
        (emission.shape == VolumeFluidEmissionShape::Distribution && emission.distribution.empty())) {
        emitting_ = false;
        return Result<unsigned>::success(0);
    }
    auto result = emitBurst(solver, emission, 1, sequence_,
                            emission.distribution.empty() ? 0 : size_t(sequence_) % emission.distribution.size(),
                            actorFree, offset, dt);
    if (!result) return result;
    ++sequence_;
    emitting_ = true;
    return result;
}

Result<unsigned> VolumeFluidEmitter::advance(VolumeFluid& solver, const VolumeFluidEmission& emission, float dt,
                                             float rate, unsigned maxPerStep, float minimumPoolFraction) {
    if (!valid(emission) || !std::isfinite(dt) || dt < 0.f || dt > 1.f / 30.f || !std::isfinite(rate) || rate < 0.f ||
        rate > 1000000.f || maxPerStep == 0 || maxPerStep > 4096 || !std::isfinite(minimumPoolFraction) ||
        minimumPoolFraction < 0.f || minimumPoolFraction > 1.f)
        return invalidEmission("Invalid stream duration, rate, threshold or work limit");
    // Fluid3D's runtime controllers stop the emitter by setting speed to zero.
    // Discard fractional credit so re-enabling does not catch up disabled time.
    if (emission.speed == 0.f) {
        credit_ = 0.0;
        return Result<unsigned>::success(0);
    }
    auto active = solver.actorParticleCount(emission.prototype.actorGroup);
    if (!active) return Result<unsigned>::failure(active.status());
    const size_t free      = solver.availableCapacity();
    const size_t actorFree = emission.actorCapacity > active.value() ? emission.actorCapacity - active.value() : 0;
    if ((emission.shape == VolumeFluidEmissionShape::Distribution && emission.distribution.empty()) || free == 0 ||
        actorFree == 0 ||
        (!emitting_ && double(actorFree) <= std::floor(double(emission.actorCapacity) * minimumPoolFraction))) {
        credit_   = 0.0;
        emitting_ = false;
        return Result<unsigned>::success(0);
    }
    const double   accumulated = credit_ + double(dt) * rate;
    const double   whole       = std::floor(accumulated);
    const unsigned count       = unsigned(std::min({whole, double(maxPerStep), double(free), double(actorFree)}));
    auto result = emitBurst(solver, emission, count, sequence_,
                            emission.distribution.empty() ? 0 : size_t(sequence_) % emission.distribution.size(),
                            actorFree, 0.f, 0.f);
    if (!result) return result;
    credit_ = accumulated - whole;
    sequence_ += count;
    emitting_ = count != free && count != actorFree;
    return result;
}

Result<unsigned> VolumeFluidEmitter::advanceBurst(VolumeFluid& solver, const VolumeFluidEmission& emission,
                                                  unsigned count, float minimumPoolFraction) {
    if (!valid(emission) || count == 0 || count > 4096 || !std::isfinite(minimumPoolFraction) ||
        minimumPoolFraction < 0.f || minimumPoolFraction > 1.f)
        return invalidEmission("Invalid burst count, threshold or emission description");
    auto active = solver.actorParticleCount(emission.prototype.actorGroup);
    if (!active) return Result<unsigned>::failure(active.status());
    if (active.value() != 0) {
        emitting_ = true;
        return Result<unsigned>::success(0);
    }
    const size_t free = solver.availableCapacity();
    if (count > emission.actorCapacity || free < count ||
        double(emission.actorCapacity) <= std::floor(double(emission.actorCapacity) * minimumPoolFraction)) {
        emitting_ = false;
        return Result<unsigned>::success(0);
    }
    auto result = emitBurst(solver, emission, count, sequence_, 0, emission.actorCapacity, 0.f, 0.f);
    if (!result) return result;
    sequence_ += count;
    emitting_ = true;
    return result;
}
}  // namespace eve::fluids

namespace eve::fluids {
Result<unsigned> VolumeFluidJetEmitter::advance(VolumeFluid& solver, const VolumeFluidEmission& e, float dt,
                                                unsigned maxPerStep, float minimumPoolFraction) {
    if (!valid(e)) return invalidEmission("Invalid jet description");
    const auto                  forward   = glm::normalize(e.direction);
    const auto                  reference = std::abs(forward.y) < .9f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
    const auto                  right     = glm::normalize(glm::cross(reference, forward));
    const auto                  rotation  = glm::quat_cast(glm::mat3(right, glm::cross(forward, right), forward));
    const VolumeFluidNozzlePose pose{e.origin, {rotation.x, rotation.y, rotation.z, rotation.w}};
    return advanceMoving(solver, e, pose, pose, dt, maxPerStep, minimumPoolFraction, 0.f);
}

Result<unsigned> VolumeFluidJetEmitter::advanceMoving(VolumeFluid& solver, const VolumeFluidEmission& e,
                                                      const VolumeFluidNozzlePose& begin,
                                                      const VolumeFluidNozzlePose& end, float dt, unsigned maxPerStep,
                                                      float minimumPoolFraction, float inheritVelocity) {
    const auto validPose = [](const VolumeFluidNozzlePose& pose) {
        const float norm = glm::dot(pose.rotation, pose.rotation);
        return finiteVector(pose.position) && std::isfinite(norm) && std::abs(norm - 1.f) < .001f;
    };
    if (!validPose(begin) || !validPose(end) || !std::isfinite(inheritVelocity) || inheritVelocity < 0.f ||
        inheritVelocity > 1.f)
        return invalidEmission("Invalid nozzle pose or inherited velocity fraction");
    if (!valid(e) || e.shape == VolumeFluidEmissionShape::Sphere || e.shape == VolumeFluidEmissionShape::Cube ||
        !std::isfinite(dt) || dt < 0.f || dt > 1.f / 30.f || maxPerStep == 0 || maxPerStep > 4096 ||
        !std::isfinite(minimumPoolFraction) || minimumPoolFraction < 0.f || minimumPoolFraction > 1.f)
        return invalidEmission("Invalid jet shape or timestep/work limit");
    if (e.speed == 0.f) {
        distance_ = 0.0;
        return Result<unsigned>::success(0);
    }
    auto active = solver.actorParticleCount(e.prototype.actorGroup);
    if (!active) return Result<unsigned>::failure(active.status());
    const size_t actorFree = e.actorCapacity > active.value() ? e.actorCapacity - active.value() : 0;
    if (actorFree == 0 ||
        (!emitting_ && double(actorFree) <= std::floor(double(e.actorCapacity) * minimumPoolFraction))) {
        distance_ = 0.0;
        emitting_ = false;
        return Result<unsigned>::success(0);
    }
    const float                                   spacing = solver.spacing();
    std::span<const VolumeFluidDistributionPoint> points;
    if (e.shape == VolumeFluidEmissionShape::Distribution) {
        points = e.distribution;
        if (points.empty()) {
            distance_ = 0.0;
            emitting_ = false;
            return Result<unsigned>::success(0);
        }
    } else {
        const double nx = std::floor(double(e.extent.x) / spacing);
        const double ny =
            e.shape == VolumeFluidEmissionShape::Edge
                ? 0.0
                : std::floor(double(e.shape == VolumeFluidEmissionShape::Disk ? e.extent.x : e.extent.y) / spacing);
        if ((2.0 * nx + 1.0) * (2.0 * ny + 1.0) > 4096.0)
            return invalidEmission("Jet lattice exceeds 4096 candidate points");
        layerPoints_.clear();
        layerPoints_.reserve(size_t((2.0 * nx + 1.0) * (2.0 * ny + 1.0)));
        for (int y = -int(ny); y <= int(ny); ++y)
            for (int x = -int(nx); x <= int(nx); ++x) {
                const glm::vec3 point(float(x) * spacing, float(y) * spacing, 0.f);
                if (e.shape == VolumeFluidEmissionShape::Disk && glm::dot(point, point) > e.extent.x * e.extent.x)
                    continue;
                layerPoints_.push_back({point, glm::vec4(1.f)});
            }
        points = layerPoints_;
    }
    if (points.size() > maxPerStep) return invalidEmission("Jet work cap cannot fit a complete nozzle layer");
    const size_t free = solver.availableCapacity();
    if (free < points.size() || actorFree < points.size()) {
        distance_ = 0.0;
        emitting_ = false;
        return Result<unsigned>::success(0);
    }
    const double   travelled = distance_ + double(e.speed) * dt;
    const double   requested = std::floor(travelled / spacing);
    const unsigned layers    = unsigned(std::min({requested, double(maxPerStep / points.size()),
                                                  double(free / points.size()), double(actorFree / points.size())}));
    const double   remainder = travelled - requested * spacing;
    const auto     rotation  = [](glm::vec4 q) { return glm::normalize(glm::quat(q.w, q.x, q.y, q.z)); };
    const auto     q0 = rotation(begin.rotation), q1 = rotation(end.rotation);
    const auto     prototypeRotation = rotation(e.prototype.orientation);
    auto           difference        = q1 * glm::conjugate(q0);
    if (difference.w < 0.f) difference = -difference;
    const float angle  = 2.f * std::acos(std::clamp(difference.w, -1.f, 1.f));
    const float sine   = glm::length(glm::vec3(difference.x, difference.y, difference.z));
    const auto  omega  = dt > 0.f && sine > 1e-6f
                             ? glm::vec3(difference.x, difference.y, difference.z) * (angle / (sine * dt))
                             : glm::vec3(0.f);
    const auto  linear = dt > 0.f ? (end.position - begin.position) / dt : glm::vec3(0.f);
    std::vector<VolumeFluidParticle> batch;
    batch.reserve(layers * points.size());
    uint32_t rng = e.seed + sequence_ * 0x9e3779b9u;
    for (unsigned layer = 0; layer < layers; ++layer) {
        const float distance    = float(remainder + double(layer) * spacing);
        const float age         = e.speed > 0.f ? distance / e.speed : 0.f;
        const float fraction    = dt > 0.f ? std::clamp(1.f - age / dt, 0.f, 1.f) : 1.f;
        const auto  orientation = glm::slerp(q0, q1, fraction);
        const auto  origin      = glm::mix(begin.position, end.position, fraction);
        for (const auto& point : points) {
            auto p = e.prototype;
            randomizeGranularRadius(p, spacing, e.granularRadiusRandomness, rng);
            const auto worldOrientation = glm::normalize(orientation * prototypeRotation);
            p.orientation      = {worldOrientation.x, worldOrientation.y, worldOrientation.z, worldOrientation.w};
            const auto  offset = orientation * point.position;
            const float jx = 2.f * sample(rng) - 1.f, jy = 2.f * sample(rng) - 1.f, jz = 2.f * sample(rng) - 1.f;
            const auto  emissionDirection = orientation * glm::normalize(point.direction);
            p.velocity                    = emissionDirection * e.speed + e.jitter * glm::vec3(jx, jy, jz) +
                                            inheritVelocity * (linear + glm::cross(omega, offset));
            p.position                    = origin + offset + p.velocity * age;
            if (e.useShapeColor) p.color = point.color;
            batch.push_back(p);
        }
    }
    auto admitted = solver.emit(batch);
    if (!admitted) return Result<unsigned>::failure(admitted.status());
    distance_ = remainder;
    if (layers != 0) ++sequence_;
    emitting_ = free - batch.size() >= points.size() && actorFree - batch.size() >= points.size();
    return Result<unsigned>::success(unsigned(batch.size()));
}
}  // namespace eve::fluids

namespace eve::fluids {
namespace {
using DistributionResult = Result<std::vector<VolumeFluidDistributionPoint>>;
DistributionResult distributionFailure(const char* message, const char* source) {
    return DistributionResult::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, message, source));
}
bool appendDistributionPoint(std::vector<VolumeFluidDistributionPoint>& output, const glm::vec3& position,
                             const glm::vec3& direction) {
    if (output.size() >= 4096) return false;
    output.push_back({position, glm::vec4(1.f), direction});
    return true;
}
}  // namespace
Result<std::vector<VolumeFluidDistributionPoint>> buildVolumeFluidSphereDistribution(float radius, float spacing,
                                                                                     bool surface) {
    constexpr const char* source = "fluids.volume.sphereDistribution";
    if (!std::isfinite(radius) || radius <= 0.f || !std::isfinite(spacing) || spacing <= 0.f)
        return distributionFailure("Sphere radius and spacing must be finite and positive", source);
    std::vector<VolumeFluidDistributionPoint> output;
    if (!surface) {
        const double ratio      = double(radius) / spacing;
        const double countValue = std::ceil(ratio - std::max(1.0, ratio) * 1e-7);
        if (countValue <= 0.0 || countValue > 32767.0)
            return distributionFailure("Sphere lattice exceeds 65536 candidates", source);
        const uint64_t side = uint64_t(2.0 * countValue + 1.0);
        if (side * side * side > 65536) return distributionFailure("Sphere lattice exceeds 65536 candidates", source);
        const int   count = int(countValue);
        const float step  = radius / float(count);
        for (int z = -count; z <= count; ++z)
            for (int y = -count; y <= count; ++y)
                for (int x = -count; x <= count; ++x) {
                    const glm::vec3 point(float(x) * step, float(y) * step, float(z) * step);
                    if (glm::length(point) < radius && !appendDistributionPoint(output, point, {0, 0, 1}))
                        return distributionFailure("Sphere distribution exceeds 4096 points", source);
                }
        return DistributionResult::success(std::move(output));
    }
    if (spacing > 2.f * radius) {
        output.push_back({{0, 0, radius}, glm::vec4(1.f), {0, 0, 1}});
        return DistributionResult::success(std::move(output));
    }
    const double increment = 2.0 * std::asin(std::clamp(double(spacing) / (2.0 * radius), 0.0, 1.0));
    const double ringValue = std::ceil(3.141592653589793 / increment);
    if (!std::isfinite(ringValue) || ringValue > 65535.0)
        return distributionFailure("Sphere lattice exceeds 65536 candidates", source);
    const int rings      = std::max(1, int(ringValue));
    uint64_t  candidates = 0;
    for (int ring = 0; ring <= rings; ++ring) {
        const double polar      = 3.141592653589793 * ring / rings;
        const float  ringRadius = float(std::sin(polar) * radius), z = float(std::cos(polar) * radius);
        const double sampleValue = std::ceil(2.0 * 3.141592653589793 * ringRadius / spacing);
        if (!std::isfinite(sampleValue) || sampleValue > 65536.0)
            return distributionFailure("Sphere lattice exceeds 65536 candidates", source);
        const int samples = ring == 0 || ring == rings ? 1 : std::max(1, int(sampleValue));
        candidates += unsigned(samples);
        if (candidates > 65536) return distributionFailure("Sphere lattice exceeds 65536 candidates", source);
        for (int sample = 0; sample < samples; ++sample) {
            const float     angle = float(2.0 * 3.141592653589793 * sample / samples);
            const glm::vec3 point(ringRadius * std::cos(angle), ringRadius * std::sin(angle), z);
            if (!appendDistributionPoint(output, point, glm::normalize(point)))
                return distributionFailure("Sphere distribution exceeds 4096 points", source);
        }
    }
    return DistributionResult::success(std::move(output));
}

Result<std::vector<VolumeFluidDistributionPoint>> buildVolumeFluidCubeDistribution(glm::vec3 size, float spacing,
                                                                                   bool surface) {
    constexpr const char* source = "fluids.volume.cubeDistribution";
    if (!finiteVector(size) || glm::any(glm::lessThanEqual(size, glm::vec3(0.f))) || !std::isfinite(spacing) ||
        spacing <= 0.f)
        return distributionFailure("Cube size and spacing must be finite and positive", source);
    const glm::dvec3 ratio      = glm::dvec3(size) / double(spacing);
    const glm::dvec3 countValue = glm::ceil(ratio - glm::max(glm::dvec3(1.0), ratio) * 1e-7);
    if (glm::any(glm::greaterThan(countValue, glm::dvec3(65535.0))))
        return distributionFailure("Cube lattice exceeds 65536 candidates", source);
    const glm::ivec3 count      = glm::max(glm::ivec3(1), glm::ivec3(countValue));
    const uint64_t   candidates = uint64_t(count.x + 1) * uint64_t(count.y + 1) * uint64_t(count.z + 1);
    if (candidates > 65536) return distributionFailure("Cube lattice exceeds 65536 candidates", source);
    std::vector<VolumeFluidDistributionPoint> output;
    output.reserve(size_t(std::min<uint64_t>(candidates, 4096)));
    for (int z = 0; z <= count.z; ++z)
        for (int y = 0; y <= count.y; ++y)
            for (int x = 0; x <= count.x; ++x) {
                if (surface && x != 0 && x != count.x && y != 0 && y != count.y && z != 0 && z != count.z) continue;
                const glm::vec3 point(float(x) / count.x - .5f, float(y) / count.y - .5f, float(z) / count.z - .5f);
                glm::vec3       direction(0, 0, 1);
                if (surface) {
                    direction = {x == 0 ? -1.f : (x == count.x ? 1.f : 0.f), y == 0 ? -1.f : (y == count.y ? 1.f : 0.f),
                                 z == 0 ? -1.f : (z == count.z ? 1.f : 0.f)};
                    direction = glm::normalize(direction);
                }
                if (!appendDistributionPoint(output, point * size, direction))
                    return distributionFailure("Cube distribution exceeds 4096 points", source);
            }
    return DistributionResult::success(std::move(output));
}

Result<std::vector<VolumeFluidDistributionPoint>> buildVolumeFluidEdgeDistribution(float length, float spacing,
                                                                                   float radialVelocityDegrees) {
    constexpr const char* source = "fluids.volume.edgeDistribution";
    if (!std::isfinite(length) || length < 0.f || length > 10000.f || !std::isfinite(spacing) || spacing <= 0.f ||
        !std::isfinite(radialVelocityDegrees) || std::abs(radialVelocityDegrees) > 360000.f)
        return distributionFailure("Edge length, spacing or radial velocity is invalid", source);
    const double separation  = double(spacing) + .01;
    const double amountValue = std::floor(double(length) / separation);
    if (amountValue > 4095.0) return distributionFailure("Edge distribution exceeds 4096 points", source);
    const int                                 amount = int(amountValue);
    std::vector<VolumeFluidDistributionPoint> output;
    output.reserve(size_t(amount) + 1u);
    for (int i = 0; i <= amount; ++i) {
        const float     angle = glm::radians(float(i) * radialVelocityDegrees);
        const glm::vec3 direction(0.f, -std::sin(angle), std::cos(angle));
        output.push_back({{float(double(i) * separation - double(length) * .5), 0, 0}, glm::vec4(1), direction});
    }
    return DistributionResult::success(std::move(output));
}

Result<std::vector<VolumeFluidDistributionPoint>> buildVolumeFluidDiskDistribution(float radius, float spacing,
                                                                                   bool edgeEmission) {
    constexpr const char* source = "fluids.volume.diskDistribution";
    if (!std::isfinite(radius) || radius <= 0.f || radius > 10000.f || !std::isfinite(spacing) || spacing <= 0.f)
        return distributionFailure("Disk radius and spacing must be finite and positive", source);
    std::vector<VolumeFluidDistributionPoint> output;
    const int                                 rings = edgeEmission ? 1 : int(std::floor(double(radius) / spacing));
    if (!edgeEmission) output.push_back({{0, 0, 0}, glm::vec4(1), {0, 0, 1}});
    uint64_t candidates = output.size();
    for (int ring = edgeEmission ? 1 : 1; ring <= rings; ++ring) {
        const float  r          = edgeEmission ? radius : spacing * float(ring);
        const double ratio      = std::clamp(double(spacing) / (2.0 * r), 0.0, 1.0);
        const double increment  = 2.0 * std::asin(ratio);
        const double stepsValue = increment > 0.0 ? 2.0 * 3.141592653589793 / increment : 65537.0;
        if (!std::isfinite(stepsValue) || stepsValue > 65536.0)
            return distributionFailure("Disk lattice exceeds 65536 candidates", source);
        const int steps = std::max(1, int(std::ceil(stepsValue)));
        candidates += unsigned(steps);
        if (candidates > 65536) return distributionFailure("Disk lattice exceeds 65536 candidates", source);
        for (int sample = 0; sample < steps; ++sample) {
            const float     angle = float(2.0 * 3.141592653589793 * sample / steps);
            const glm::vec3 point(r * std::cos(angle), r * std::sin(angle), 0.f);
            const glm::vec3 direction = edgeEmission ? glm::normalize(point) : glm::vec3(0, 0, 1);
            if (!appendDistributionPoint(output, point, direction))
                return distributionFailure("Disk distribution exceeds 4096 points", source);
        }
    }
    return DistributionResult::success(std::move(output));
}

Result<VolumeFluidEmission> composeVolumeFluidEmitterShapes(const VolumeFluidEmission&           base,
                                                            std::span<const VolumeFluidEmission> shapes) {
    constexpr const char* source = "fluids.volume.emitterShapes";
    if (shapes.size() > 64)
        return Result<VolumeFluidEmission>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "Emitter shape count exceeds 64", source));

    VolumeFluidEmission combined = base;
    combined.shape               = VolumeFluidEmissionShape::Distribution;
    combined.origin              = glm::vec3(0.f);
    combined.direction           = glm::vec3(0.f, 0.f, 1.f);
    combined.distribution.clear();

    const auto appendPoint = [&](glm::vec3 position, glm::vec3 direction, glm::vec4 color) {
        if (combined.distribution.size() == 4096) return false;
        combined.distribution.push_back({position, color, direction});
        return true;
    };
    const auto appendShape = [&](const VolumeFluidEmission& shape) {
        if (!valid(shape) || shape.shape != VolumeFluidEmissionShape::Distribution) return false;
        const auto forward   = glm::normalize(shape.direction);
        const auto reference = std::abs(forward.y) < .9f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
        const auto right     = glm::normalize(glm::cross(reference, forward));
        const auto up        = glm::cross(forward, right);
        if (shape.distribution.empty()) return appendPoint(shape.origin, forward, glm::vec4(1.f));
        for (const auto& point : shape.distribution) {
            const auto position =
                shape.origin + right * point.position.x + up * point.position.y + forward * point.position.z;
            const auto direction =
                glm::normalize(right * point.direction.x + up * point.direction.y + forward * point.direction.z);
            if (!appendPoint(position, direction, point.color)) return false;
        }
        return true;
    };

    if (shapes.empty()) {
        if (!valid(base) || !appendPoint(base.origin, glm::normalize(base.direction), glm::vec4(1.f)))
            return Result<VolumeFluidEmission>::failure(
                Diagnostic::error(DiagnosticCode::InvalidArgument, "Invalid emitter-wide description", source));
    } else {
        for (const auto& shape : shapes)
            if (!appendShape(shape))
                return Result<VolumeFluidEmission>::failure(
                    Diagnostic::error(DiagnosticCode::InvalidArgument,
                                      "Invalid emitter shape or distribution exceeds 4096 points", source));
    }
    if (!valid(combined))
        return Result<VolumeFluidEmission>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "Invalid emitter-wide description", source));
    return Result<VolumeFluidEmission>::success(std::move(combined));
}
}  // namespace eve::fluids

namespace eve::fluids {
Result<std::vector<VolumeFluidDistributionPoint>> buildVolumeFluidImageDistribution(std::span<const glm::vec4> pixels,
                                                                                    unsigned width, unsigned height,
                                                                                    float pixelScale, float maximumSize,
                                                                                    float spacing,
                                                                                    float alphaThreshold) {
    using Output    = Result<std::vector<VolumeFluidDistributionPoint>>;
    const auto fail = [](const char* text) {
        return Output::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, text, "fluids.volume.imageDistribution"));
    };
    if (width == 0 || height == 0 || uint64_t(width) * height != pixels.size() || pixels.size() > 16777216 ||
        !std::isfinite(pixelScale) || pixelScale <= 0.f || !std::isfinite(maximumSize) || maximumSize <= 0.f ||
        !std::isfinite(spacing) || spacing <= 0.f || !std::isfinite(alphaThreshold) || alphaThreshold < 0.f ||
        alphaThreshold > 1.f)
        return fail("Invalid image dimensions, scale, spacing or alpha threshold");
    for (const auto pixel : pixels)
        for (int i = 0; i < 4; ++i)
            if (!std::isfinite(pixel[i]) || pixel[i] < 0.f || pixel[i] > 1.f)
                return fail("Pixels must be finite linear RGBA in [0,1]");
    double       worldWidth = double(width) * pixelScale, worldHeight = double(height) * pixelScale;
    const double scale = std::min(1.0, double(maximumSize) / std::max(worldWidth, worldHeight));
    worldWidth *= scale;
    worldHeight *= scale;
    const double countX = std::floor(worldWidth / spacing), countY = std::floor(worldHeight / spacing);
    if (countX > 65536 || countY > 65536 || countX * countY > 65536)
        return fail("Image lattice exceeds 65536 candidates");
    std::vector<VolumeFluidDistributionPoint> points;
    const unsigned                            nx = unsigned(countX), ny = unsigned(countY);
    for (unsigned x = 0; x < nx; ++x)
        for (unsigned y = 0; y < ny; ++y) {
            // Clamp-addressed bilinear sampling at normalized texture coordinates.
            const double   sx = std::clamp(double(x) / nx * width - .5, 0.0, double(width - 1));
            const double   sy = std::clamp(double(y) / ny * height - .5, 0.0, double(height - 1));
            const unsigned x0 = unsigned(sx), y0 = unsigned(sy), x1 = std::min(x0 + 1, width - 1),
                           y1 = std::min(y0 + 1, height - 1);
            const float    fx = float(sx - x0), fy = float(sy - y0);
            const auto     lower = pixels[size_t(y0) * width + x0] * (1.f - fx) + pixels[size_t(y0) * width + x1] * fx;
            const auto     upper = pixels[size_t(y1) * width + x0] * (1.f - fx) + pixels[size_t(y1) * width + x1] * fx;
            const auto     color = lower * (1.f - fy) + upper * fy;
            if (color.a <= alphaThreshold) continue;
            if (points.size() == 4096) return fail("Image distribution exceeds 4096 emission points");
            points.push_back(
                {{float(double(x) * spacing - worldWidth * .5), float(double(y) * spacing - worldHeight * .5), 0.f},
                 color});
        }
    return Output::success(std::move(points));
}

VolumeFluidEmitterCheckpoint captureVolumeFluidEmitterCheckpoint(const VolumeFluid&         solver,
                                                                 const VolumeFluidEmission& emission,
                                                                 const VolumeFluidEmitter&  controller) {
    return {"eve.volume-fluid-emitter-checkpoint", 1, solver.snapshot(), emission, controller.snapshot()};
}

VolumeFluidEmitterCheckpoint captureVolumeFluidEmitterCheckpoint(const VolumeFluid&           solver,
                                                                 const VolumeFluidEmission&   emission,
                                                                 const VolumeFluidJetEmitter& controller) {
    return {"eve.volume-fluid-emitter-checkpoint", 1, solver.snapshot(), emission, controller.snapshot()};
}

Result<void> restoreVolumeFluidEmitterCheckpoint(VolumeFluid& solver, VolumeFluidEmitter& controller,
                                                 const VolumeFluidEmitterCheckpoint& checkpoint) {
    return restoreCheckpoint(solver, controller, checkpoint);
}

Result<void> restoreVolumeFluidEmitterCheckpoint(VolumeFluid& solver, VolumeFluidJetEmitter& controller,
                                                 const VolumeFluidEmitterCheckpoint& checkpoint) {
    return restoreCheckpoint(solver, controller, checkpoint);
}
}  // namespace eve::fluids

#include <charconv>
#include <limits>
#include <stdexcept>
namespace eve::fluids {
namespace {
std::string phaseText(double value) {
    char buffer[64];
    auto converted = std::to_chars(buffer, buffer + sizeof(buffer), value, std::chars_format::general,
                                   std::numeric_limits<double>::max_digits10);
    if (converted.ec != std::errc{}) throw std::runtime_error("Emitter phase encoding failed");
    return std::string(buffer, converted.ptr);
}
Result<double> restoredPhase(const VolumeFluidEmitterSnapshot& s, unsigned kind, double upper) {
    double phase  = 0;
    auto   parsed = std::from_chars(s.phase.data(), s.phase.data() + s.phase.size(), phase);
    if (s.schema != "eve.volume-fluid-emitter-state" || s.version != 1 || s.kind != kind || parsed.ec != std::errc{} ||
        parsed.ptr != s.phase.data() + s.phase.size() || !std::isfinite(phase) || phase < 0 || phase >= upper)
        return Result<double>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                         "Invalid emitter state schema, kind or phase",
                                                         "fluids.volume.emitterState"));
    return Result<double>::success(phase);
}
}  // namespace
VolumeFluidEmitterSnapshot VolumeFluidEmitter::snapshot() const {
    return {"eve.volume-fluid-emitter-state", 1, 0, phaseText(credit_), sequence_, emitting_};
}
Result<void> VolumeFluidEmitter::restore(const VolumeFluidEmitterSnapshot& state) {
    auto phase = restoredPhase(state, 0, 1.0);
    if (!phase) return Result<void>::failure(phase.status());
    credit_   = phase.value();
    sequence_ = state.sequence;
    emitting_ = state.emitting;
    return Result<void>::success();
}
VolumeFluidEmitterSnapshot VolumeFluidJetEmitter::snapshot() const {
    return {"eve.volume-fluid-emitter-state", 1, 1, phaseText(distance_), sequence_, emitting_};
}
Result<void> VolumeFluidJetEmitter::restore(const VolumeFluidEmitterSnapshot& state) {
    auto phase = restoredPhase(state, 1, 10.0);
    if (!phase) return Result<void>::failure(phase.status());
    distance_ = phase.value();
    sequence_ = state.sequence;
    emitting_ = state.emitting;
    return Result<void>::success();
}
}  // namespace eve::fluids
