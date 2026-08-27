#include "fluids/SurfaceFluidContracts.h"

#include "common/Exception.h"
#include "fluids/FluidSurfaceRenderer.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace eve::fluids {
namespace {

eve::Result<void> staleSolver(const char* path) {
    return eve::Result<void>::failure(eve::Diagnostic::error(
        eve::DiagnosticCode::PreconditionViolation, "Surface-fluid adapter has no live borrowed solver", path));
}

eve::Result<void> invalidInput(const char* path, const char* message) {
    return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, message, path));
}

eve::Result<void> applied() { return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied)); }

eve::Result<void> validateTick(const eve::SimulationStep& step, eve::SimulationTick lastTick, const char* path) {
    const double seconds = step.delta.seconds();
    if (!std::isfinite(seconds) || seconds < 0.0)
        return invalidInput(path, "Surface-fluid duration must be finite and non-negative");
    if (!(step.tick > lastTick))
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::PreconditionViolation, "Surface-fluid simulation tick must increase strictly", path));
    return applied();
}

eve::Result<void> validateDuration(eve::Duration duration, const char* path) {
    const double seconds = duration.seconds();
    if (!std::isfinite(seconds) || seconds < 0.0)
        return invalidInput(path, "Surface pose duration must be finite and non-negative");
    return applied();
}

}  // namespace

eve::Result<void> FluidSimulationAdapter::step(const eve::SimulationStep& stepValue) {
    if (!solver_) return staleSolver("fluids.surfaceSimulation.solver");
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
    if (!binding_) return staleSolver("fluids.surfaceConstraint.binding");
    if (!binding_->build(positions, indices, uvs))
        return invalidInput("fluids.surfaceConstraint.topology",
                            "Surface constraint topology is not a valid triangle mesh");
    return applied();
}

eve::Result<void> FluidSurfaceConstraintAdapter::setTransform(const glm::mat4& transform) {
    if (!binding_) return staleSolver("fluids.surfaceConstraint.binding");
    binding_->setTransform(transform);
    return applied();
}

eve::Result<void> FluidSurfaceConstraintAdapter::setDeformedPositions(const std::vector<glm::vec3>& worldPositions) {
    if (!binding_) return staleSolver("fluids.surfaceConstraint.binding");
    if (!binding_->setDeformedPositions(worldPositions))
        return invalidInput("fluids.surfaceConstraint.pose", "Deformed surface vertex count does not match topology");
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
    if (!renderer_) return staleSolver("fluids.screenSpaceReconstruction.renderer");
    if (!std::isfinite(particleRadius) || particleRadius <= 0.f)
        return invalidInput("fluids.screenSpaceReconstruction.particleRadius",
                            "Screen-space particle radius must be finite and positive");
    for (const glm::vec3& position : positions) {
        if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z))
            return invalidInput("fluids.screenSpaceReconstruction.positions",
                                "Screen-space particle positions must be finite");
    }
    const std::vector<glm::vec3> ownedPositions(positions.begin(), positions.end());
    renderer_->render(ownedPositions, particleRadius);
    return applied();
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
