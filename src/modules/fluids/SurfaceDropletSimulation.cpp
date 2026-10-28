#include "fluids/SurfaceDropletSimulation.h"
#include <algorithm>
#include <cmath>
#include <glm/gtx/norm.hpp>
#include <utility>
#include "common/Exception.h"
#include "fluids/FluidSurfaceRenderer.h"
#include "fluids/SurfaceFluidContracts.h"
#include "fluids/SurfaceFluidRenderData.h"
#include "fluids/SurfaceFluidSceneRenderer.h"
#include "graphics/Graphics.h"
#include "graphics/Mesh.h"

// SurfaceDropletSimulation.cpp
namespace eve::fluids {

SurfaceDropletSimulation::SurfaceDropletSimulation(FluidSurfaceBinding* binding, const SurfaceDropletParams& params,
                                                   SurfaceWetnessField* wetness)
    : binding_(binding), params_(params), wetness_(wetness) {}

float SurfaceDropletSimulation::dropletRadius(float volume) const {
    const float     theta  = glm::radians(std::clamp(params_.contactAngleDegrees, 5.f, 175.f));
    const float     cosine = std::cos(theta);
    const float     sine   = std::max(1e-3f, std::sin(theta));
    constexpr float pi     = 3.14159265358979323846f;
    const float     factor = pi * (2.f - 3.f * cosine + cosine * cosine * cosine) / (3.f * sine * sine * sine);
    return std::cbrt(std::max(0.f, volume) / std::max(1e-6f, factor));
}

bool SurfaceDropletSimulation::addDroplet(const SurfaceLocation& location, float volume,
                                          const glm::vec3& relativeVelocity) {
    if (!binding_ || !binding_->isValid() || location.triangle >= uint32_t(binding_->triangleCount()) || volume <= 0.f)
        return false;
    SurfaceDroplet droplet;
    droplet.id                 = nextDropletId_++;
    droplet.location           = location;
    droplet.volume             = volume;
    const SurfaceSample sample = binding_->evaluate(location, 0.f);
    droplet.relativeVelocity   = relativeVelocity - sample.normal * glm::dot(relativeVelocity, sample.normal);
    droplets_.push_back(droplet);
    return true;
}

void SurfaceDropletSimulation::step(float dt) {
    detached_.clear();
    if (!binding_ || !binding_->isValid() || dt <= 0.f || (droplets_.empty() && airborne_.empty())) return;
    float remaining = dt;
    while (remaining > 1e-7f) {
        const float substep = std::min(remaining, 0.05f);
        stepSubstep(substep, dt);
        remaining -= substep;
    }
}

void SurfaceDropletSimulation::stepSubstep(float dt, float poseDt) {
    std::vector<SurfaceDroplet> attached;
    attached.reserve(droplets_.size());
    for (SurfaceDroplet droplet : droplets_) {
        const SurfaceLocation previousLocation = droplet.location;
        const SurfaceSample   previousSample   = binding_->evaluate(previousLocation, poseDt);
        const SurfaceSample   sample           = binding_->evaluate(droplet.location, poseDt);
        glm::vec3             surfaceAcceleration(0.f);
        if (droplet.hasPreviousSurfaceVelocity)
            surfaceAcceleration = (sample.velocity - droplet.previousSurfaceVelocity) / poseDt;

        const glm::vec3 relativeAcceleration = params_.gravity - surfaceAcceleration;
        const float     outwardAcceleration  = glm::dot(relativeAcceleration, sample.normal);
        if (outwardAcceleration > std::max(0.f, params_.adhesionAcceleration)) {
            detached_.push_back({sample.position, sample.velocity + droplet.relativeVelocity, droplet.volume});
            airborne_.push_back({sample.position, sample.velocity + droplet.relativeVelocity, droplet.volume});
            continue;
        }

        glm::vec3 tangentAcceleration =
            relativeAcceleration - sample.normal * glm::dot(relativeAcceleration, sample.normal);
        droplet.relativeVelocity += tangentAcceleration * dt;
        droplet.relativeVelocity *= std::exp(-std::max(0.f, params_.friction) * dt);
        const float speed2   = glm::length2(droplet.relativeVelocity);
        const float maxSpeed = std::max(0.f, params_.maxSpeed);
        if (maxSpeed > 0.f && speed2 > maxSpeed * maxSpeed) droplet.relativeVelocity *= maxSpeed / std::sqrt(speed2);

        const SurfaceWalkResult walk = binding_->walkAcrossSurface(droplet.location, droplet.relativeVelocity * dt,
                                                                   std::max(0, params_.maxCrossings));
        if (!walk.valid) continue;
        droplet.location = walk.location;
        if (walk.reachedBoundary) {
            const SurfaceSample edge = binding_->evaluate(walk.location, poseDt);
            detached_.push_back({edge.position, edge.velocity + droplet.relativeVelocity, droplet.volume});
            airborne_.push_back({edge.position, edge.velocity + droplet.relativeVelocity, droplet.volume, 0.f});
            continue;
        }

        const SurfaceSample end = binding_->evaluate(droplet.location, poseDt);
        droplet.relativeVelocity -= end.normal * glm::dot(droplet.relativeVelocity, end.normal);
        droplet.previousSurfaceVelocity    = end.velocity;
        droplet.hasPreviousSurfaceVelocity = true;
        if (wetness_) {
            const float distance = glm::distance(previousSample.position, end.position);
            wetness_->deposit(previousLocation, distance * params_.trailDeposition * droplet.volume);
            wetness_->deposit(droplet.location, distance * params_.trailDeposition * droplet.volume);
        }
        attached.push_back(droplet);
    }
    droplets_.swap(attached);

    // Merge overlapping spherical caps without depending on mesh UV seams.
    for (size_t i = 0; i < droplets_.size(); ++i) {
        const SurfaceSample a = binding_->evaluate(droplets_[i].location, poseDt);
        for (size_t j = i + 1u; j < droplets_.size();) {
            const SurfaceSample b = binding_->evaluate(droplets_[j].location, poseDt);
            const float         mergeDistance =
                params_.mergeRadiusScale * (dropletRadius(droplets_[i].volume) + dropletRadius(droplets_[j].volume));
            if (glm::distance2(a.position, b.position) > mergeDistance * mergeDistance) {
                ++j;
                continue;
            }
            const float combined          = droplets_[i].volume + droplets_[j].volume;
            droplets_[i].relativeVelocity = (droplets_[i].relativeVelocity * droplets_[i].volume +
                                             droplets_[j].relativeVelocity * droplets_[j].volume) /
                                            combined;
            droplets_[i].volume = combined;
            droplets_.erase(droplets_.begin() + std::ptrdiff_t(j));
        }
    }

    std::vector<AirborneDroplet> flying;
    flying.reserve(airborne_.size());
    for (AirborneDroplet drop : airborne_) {
        drop.age += dt;
        drop.velocity += params_.gravity * dt;
        drop.velocity *= std::exp(-std::max(0.f, params_.airDrag) * dt);
        drop.position += drop.velocity * dt;
        SurfaceLocation hit;
        if (drop.age > 0.08f && binding_->project(drop.position, std::max(0.f, params_.reattachDistance), hit)) {
            const SurfaceSample surface = binding_->evaluate(hit, poseDt);
            if (glm::dot(drop.velocity - surface.velocity, surface.normal) <= 0.f) {
                addDroplet(hit, drop.volume, drop.velocity - surface.velocity);
                if (wetness_) wetness_->deposit(hit, drop.volume * 0.15f);
                continue;
            }
        }
        flying.push_back(drop);
    }
    airborne_.swap(flying);
}

void SurfaceDropletSimulation::clear() {
    droplets_.clear();
    detached_.clear();
    airborne_.clear();
    nextDropletId_ = 1;
}

}  // namespace eve::fluids

// SurfaceFluidContracts.cpp
namespace eve::fluids {
namespace {

eve::Result<void> applied() { return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied)); }

eve::Result<void> validateTick(const eve::SimulationStep& step, eve::SimulationTick lastTick, const char* path) {
    const double seconds = step.delta.seconds();
    if (!std::isfinite(seconds) || seconds < 0.0)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "Surface-fluid duration must be finite and non-negative", path));
    if (!(step.tick > lastTick))
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::PreconditionViolation, "Surface-fluid simulation tick must increase strictly", path));
    return applied();
}

eve::Result<void> validateDuration(eve::Duration duration, const char* path) {
    const double seconds = duration.seconds();
    if (!std::isfinite(seconds) || seconds < 0.0)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "Surface pose duration must be finite and non-negative", path));
    return applied();
}

}  // namespace

eve::Result<void> FluidSimulationAdapter::step(const eve::SimulationStep& stepValue) {
    if (!solver_)
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::PreconditionViolation,
                                                                 "Surface-fluid adapter has no live borrowed solver",
                                                                 "fluids.surfaceSimulation.solver"));
    auto valid = validateTick(stepValue, lastTick_, "fluids.surfaceSimulation.step");
    if (!valid) return valid;
    solver_->step(static_cast<float>(stepValue.delta.seconds()));
    lastTick_ = stepValue.tick;
    return applied();
}

int FluidSimulationAdapter::particleCount() const noexcept { return solver_ ? solver_->particleCount() : 0; }

std::span<const FluidParticle> FluidSimulationAdapter::particles() const noexcept {
    if (!solver_) return {};
    const auto& values = solver_->particles();
    const auto  count  = static_cast<std::size_t>(std::max(0, solver_->particleCount()));
    return {values.data(), std::min(count, values.size())};
}

eve::Result<void> FluidSurfaceConstraintAdapter::build(const std::vector<glm::vec3>&     positions,
                                                       const std::vector<std::uint32_t>& indices,
                                                       const std::vector<glm::vec2>&     uvs) {
    if (!binding_)
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::PreconditionViolation,
                                                                 "Surface-fluid adapter has no live borrowed solver",
                                                                 "fluids.surfaceConstraint.binding"));
    if (!binding_->build(positions, indices, uvs))
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "Surface constraint topology is not a valid triangle mesh",
            "fluids.surfaceConstraint.topology"));
    return applied();
}

eve::Result<void> FluidSurfaceConstraintAdapter::setTransform(const glm::mat4& transform) {
    if (!binding_)
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::PreconditionViolation,
                                                                 "Surface-fluid adapter has no live borrowed solver",
                                                                 "fluids.surfaceConstraint.binding"));
    binding_->setTransform(transform);
    return applied();
}

eve::Result<void> FluidSurfaceConstraintAdapter::setDeformedPositions(const std::vector<glm::vec3>& worldPositions) {
    if (!binding_)
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::PreconditionViolation,
                                                                 "Surface-fluid adapter has no live borrowed solver",
                                                                 "fluids.surfaceConstraint.binding"));
    if (!binding_->setDeformedPositions(worldPositions))
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "Deformed surface vertex count does not match topology",
            "fluids.surfaceConstraint.pose"));
    return applied();
}

bool FluidSurfaceConstraintAdapter::isValid() const noexcept { return binding_ && binding_->isValid(); }

eve::Result<SurfaceSample> FluidSurfaceConstraintAdapter::evaluate(const SurfaceLocation& location,
                                                                   eve::Duration          poseDelta) const {
    auto validDuration = validateDuration(poseDelta, "fluids.surfaceConstraint.evaluate.dt");
    if (!validDuration) return eve::Result<SurfaceSample>::failure(validDuration.status());
    if (!binding_ || !binding_->isValid())
        return eve::Result<SurfaceSample>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::PreconditionViolation, "Cannot evaluate an invalid surface constraint",
            "fluids.surfaceConstraint.evaluate"));
    if (location.triangle >= static_cast<std::uint32_t>(binding_->triangleCount()))
        return eve::Result<SurfaceSample>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::NotFound, "Surface location triangle is outside the bound topology",
            "fluids.surfaceConstraint.evaluate.triangle"));
    return eve::Result<SurfaceSample>::success(binding_->evaluate(location, static_cast<float>(poseDelta.seconds())));
}

eve::Result<SurfaceLocation> FluidSurfaceConstraintAdapter::project(const glm::vec3& worldPosition,
                                                                    float            maxDistance) const {
    if (!binding_ || !binding_->isValid())
        return eve::Result<SurfaceLocation>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::PreconditionViolation, "Cannot project against an invalid surface constraint",
            "fluids.surfaceConstraint.project"));
    SurfaceLocation location;
    if (!binding_->project(worldPosition, maxDistance, location))
        return eve::Result<SurfaceLocation>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::NotFound, "No surface location satisfies the projection distance",
            "fluids.surfaceConstraint.project"));
    return eve::Result<SurfaceLocation>::success(location);
}

eve::Result<SurfaceWalkResult> FluidSurfaceConstraintAdapter::walk(const SurfaceLocation& start,
                                                                   const glm::vec3&       displacement,
                                                                   int                    maxCrossings) const {
    if (!binding_ || !binding_->isValid())
        return eve::Result<SurfaceWalkResult>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::PreconditionViolation,
                                   "Cannot walk an invalid surface constraint", "fluids.surfaceConstraint.walk"));
    if (maxCrossings < 0)
        return eve::Result<SurfaceWalkResult>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "Surface traversal maxCrossings must be non-negative",
            "fluids.surfaceConstraint.walk.maxCrossings"));
    const auto result = binding_->walkAcrossSurface(start, displacement, maxCrossings);
    if (!result.valid)
        return eve::Result<SurfaceWalkResult>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::NotFound, "Surface traversal start is outside the bound topology",
            "fluids.surfaceConstraint.walk.start"));
    return eve::Result<SurfaceWalkResult>::success(result);
}

ScreenSpaceSurfaceReconstructionAdapter::ScreenSpaceSurfaceReconstructionAdapter(const FluidSurfaceParams& params,
                                                                                 bool                      preferGpu) {
    if (params.width <= 0 || params.height <= 0) throw eve::Exception("Surface SSF output dimensions must be positive");
    renderer_ = std::make_unique<FluidSurfaceRenderer>(params, preferGpu);
}

ScreenSpaceSurfaceReconstructionAdapter::~ScreenSpaceSurfaceReconstructionAdapter() = default;

eve::Result<void> ScreenSpaceSurfaceReconstructionAdapter::reconstruct(std::span<const glm::vec3> positions,
                                                                       float                      particleRadius) {
    if (!renderer_)
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::PreconditionViolation,
                                                                 "Surface-fluid adapter has no live borrowed solver",
                                                                 "fluids.screenSpaceReconstruction.renderer"));
    if (!std::isfinite(particleRadius) || particleRadius <= 0.f)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "Screen-space particle radius must be finite and positive",
            "fluids.screenSpaceReconstruction.particleRadius"));
    for (const glm::vec3& position : positions) {
        if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z))
            return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                     "Screen-space particle positions must be finite",
                                                                     "fluids.screenSpaceReconstruction.positions"));
    }
    const std::vector<glm::vec3> ownedPositions(positions.begin(), positions.end());
    renderer_->render(ownedPositions, particleRadius);
    return applied();
}

eve::Result<void> ScreenSpaceSurfaceReconstructionAdapter::occlude(std::span<const float> sceneDepth, float depthBias) {
    if (!renderer_)
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::PreconditionViolation,
                                                                 "Surface-fluid adapter has no live borrowed solver",
                                                                 "fluids.screenSpaceReconstruction.renderer"));
    return renderer_->occludeWithSceneDepth(sceneDepth, depthBias);
}

int ScreenSpaceSurfaceReconstructionAdapter::width() const noexcept { return renderer_ ? renderer_->getWidth() : 0; }

int ScreenSpaceSurfaceReconstructionAdapter::height() const noexcept { return renderer_ ? renderer_->getHeight() : 0; }

std::span<const float> ScreenSpaceSurfaceReconstructionAdapter::depth() const noexcept {
    return renderer_ ? std::span<const float>(renderer_->depth()) : std::span<const float>{};
}

std::span<const glm::vec3> ScreenSpaceSurfaceReconstructionAdapter::normals() const noexcept {
    return renderer_ ? std::span<const glm::vec3>(renderer_->normals()) : std::span<const glm::vec3>{};
}

std::span<const float> ScreenSpaceSurfaceReconstructionAdapter::thickness() const noexcept {
    return renderer_ ? std::span<const float>(renderer_->thickness()) : std::span<const float>{};
}

std::span<const std::uint8_t> ScreenSpaceSurfaceReconstructionAdapter::color() const noexcept {
    return renderer_ ? std::span<const std::uint8_t>(renderer_->color()) : std::span<const std::uint8_t>{};
}

bool ScreenSpaceSurfaceReconstructionAdapter::usingGpu() const noexcept { return renderer_ && renderer_->usingGpu(); }

}  // namespace eve::fluids

// SurfaceFluidRenderData.cpp
namespace eve::fluids {

void SurfaceFluidRenderData::update(const FluidSurfaceBinding& binding, const SurfaceDropletSimulation& simulation,
                                    const SurfaceWetnessField* wetness, const SurfaceFluidRenderParams& params) {
    droplets_.clear();
    droplets_.reserve(simulation.droplets().size());
    const float stretch       = std::max(0.f, params.velocityStretch);
    const float maximumAspect = std::max(1.f, params.maxAspectRatio);
    const float theta         = glm::radians(std::clamp(simulation.params().contactAngleDegrees, 5.f, 175.f));

    for (const SurfaceDroplet& droplet : simulation.droplets()) {
        const SurfaceSample surface   = binding.evaluate(droplet.location, 0.f);
        const float         radius    = simulation.dropletRadius(droplet.volume);
        const float         speed     = glm::length(droplet.relativeVelocity);
        const float         aspect    = std::clamp(1.f + speed * stretch, 1.f, maximumAspect);
        const float         axisScale = std::sqrt(aspect);

        glm::vec3 direction = droplet.relativeVelocity;
        direction -= surface.normal * glm::dot(direction, surface.normal);
        if (glm::length2(direction) > 1e-12f)
            direction = glm::normalize(direction);
        else
            direction = surface.tangent;
        glm::vec3 across = glm::cross(surface.normal, direction);
        if (glm::length2(across) > 1e-12f)
            across = glm::normalize(across);
        else
            across = surface.bitangent;

        SurfaceDropletRenderInstance instance;
        instance.id        = droplet.id;
        instance.position  = surface.position + surface.normal * std::max(0.f, params.surfaceOffset);
        instance.normal    = surface.normal;
        instance.majorAxis = direction * (radius * axisScale);
        instance.minorAxis = across * (radius / axisScale);
        instance.velocity  = surface.velocity + droplet.relativeVelocity;
        instance.capHeight = radius * std::tan(theta * 0.5f);
        instance.wetness   = wetness ? std::clamp(wetness->sample(droplet.location), 0.f, 1.f) : 0.f;
        droplets_.push_back(instance);
    }
}

WetSurfaceMaterialSample SurfaceFluidRenderData::evaluateMaterial(float                           wetness,
                                                                  const WetSurfaceMaterialParams& params) {
    WetSurfaceMaterialSample sample;
    sample.wetness        = std::clamp(wetness, 0.f, 1.f);
    const float response  = sample.wetness * sample.wetness * (3.f - 2.f * sample.wetness);
    sample.roughness      = glm::mix(params.dryRoughness, params.wetRoughness, response);
    sample.specular       = glm::mix(params.drySpecular, params.wetSpecular, response);
    sample.darkening      = std::max(0.f, params.wetDarkening) * response;
    sample.normalStrength = std::max(0.f, params.normalStrength) * response;
    return sample;
}

}  // namespace eve::fluids

// SurfaceFluidSceneRenderer.cpp
namespace eve::fluids {

SurfaceFluidSceneRenderer::SurfaceFluidSceneRenderer(graphics::Graphics* graphics) : graphics_(graphics) {}

glm::mat4 SurfaceFluidSceneRenderer::modelMatrix(const SurfaceDropletRenderInstance& instance) {
    glm::mat4 model(1.f);
    model[0] = glm::vec4(instance.majorAxis, 0.f);
    model[1] = glm::vec4(instance.normal * instance.capHeight, 0.f);
    model[2] = glm::vec4(instance.minorAxis, 0.f);
    model[3] = glm::vec4(instance.position + instance.normal * instance.capHeight, 1.f);
    return model;
}

void SurfaceFluidSceneRenderer::draw(const SurfaceFluidRenderData& renderData, const glm::vec4& tint) {
    drawCount_ = 0;
    if (!graphics_) return;
    if (!sphere_) sphere_ = graphics_->newMeshSphere(16, 8);
    if (!sphere_) return;

    for (const SurfaceDropletRenderInstance& instance : renderData.droplets()) {
        const WetSurfaceMaterialSample material =
            SurfaceFluidRenderData::evaluateMaterial(std::max(0.35f, instance.wetness));
        graphics_->setMesh3DMaterial(0.f, material.roughness);
        graphics_->drawMesh(sphere_, modelMatrix(instance), nullptr, tint);
        ++drawCount_;
    }
}

}  // namespace eve::fluids
