#include "procgen/algorithms/CaveGeneration.h"

#include "procgen/ScalarField3D.h"
#include "procgen/algorithms/CaveGenerationState.h"

#include <cmath>

namespace eve::procgen {
namespace {
Result<void> wrongPhase() {
    return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                   "cave operation requires a different generation stage", {}, {},
                                                   "procgen.cave"));
}
bool editableField(CaveGenerationPhase phase) {
    return phase == CaveGenerationPhase::Sampled || phase == CaveGenerationPhase::Evolved;
}
}  // namespace

CaveGeneration::CaveGeneration()                                     = default;
CaveGeneration::~CaveGeneration()                                    = default;
CaveGeneration::CaveGeneration(CaveGeneration&&) noexcept            = default;
CaveGeneration& CaveGeneration::operator=(CaveGeneration&&) noexcept = default;

CaveGenerationPhase CaveGeneration::phase() const noexcept { return state_ ? phase_ : CaveGenerationPhase::Empty; }

Result<void> CaveGeneration::configure(const Params& params) {
    auto candidate    = std::make_unique<cave_detail::State>();
    candidate->params = params;
    auto result       = candidate->configure();
    if (!result.ok()) return result;
    state_ = std::move(candidate);
    phase_ = CaveGenerationPhase::Configured;
    return Result<void>::success();
}

Result<void> CaveGeneration::buildSkeleton() {
    if (phase() != CaveGenerationPhase::Configured) return wrongPhase();
    auto candidate = std::make_unique<cave_detail::State>(*state_);
    auto result    = candidate->skeleton();
    if (!result.ok()) return result;
    state_ = std::move(candidate);
    phase_ = CaveGenerationPhase::Skeleton;
    return Result<void>::success();
}

int CaveGeneration::chamberCount() const noexcept { return state_ ? int(state_->chambers.size()) : 0; }

Result<void> CaveGeneration::setChamber(int index, float x, float y, float z, float rx, float ry, float rz) {
    if (phase() != CaveGenerationPhase::Skeleton) return wrongPhase();
    if (index < 0 || index >= chamberCount() || !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) ||
        !std::isfinite(rx) || !std::isfinite(ry) || !std::isfinite(rz) || rx <= 0.f || ry <= 0.f || rz <= 0.f ||
        std::abs(x) > 1.f || std::abs(y) > 1.f || std::abs(z) > 1.f || rx > 2.f || ry > 2.f || rz > 2.f) {
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "invalid normalized chamber bounds or index", {}, {}, "procgen.cave"));
    }
    auto& chamber                            = state_->chambers[size_t(index)];
    chamber.center                           = {x, y, z};
    chamber.radii                            = {rx, ry, rz};
    state_->breakdownChambers[size_t(index)] = {x, y, z, rx, ry, rz};
    state_->breakdownSet = createCaveBreakdown(state_->breakdownChambers, state_->breakdownEvents, state_->breakdown,
                                               state_->params.getSeed());
    state_->obstacleScourSites =
        createCaveObstacleScourSites(state_->breakdownSet, state_->spine, state_->hydrology.trunk, state_->sedimentLoad,
                                     state_->multiscaleRoughness);
    if (index == state_->primaryChamber) state_->primaryChamberVerticalRadius = ry;
    state_->skeletonEdited = true;
    return Result<void>::success();
}

Result<void> CaveGeneration::sampleField() {
    if (phase() != CaveGenerationPhase::Skeleton) return wrongPhase();
    auto candidate = std::make_unique<cave_detail::State>(*state_);
    auto result    = candidate->sample();
    if (!result.ok()) return result;
    state_ = std::move(candidate);
    phase_ = CaveGenerationPhase::Sampled;
    return Result<void>::success();
}

Result<void> CaveGeneration::evolveSurface(CaveSurfaceOperation operation) {
    if (!editableField(phase())) return wrongPhase();
    if (operation != CaveSurfaceOperation::Reactivity && operation != CaveSurfaceOperation::ReactivePatches &&
        operation != CaveSurfaceOperation::Condensation && operation != CaveSurfaceOperation::Curvature)
        return wrongPhase();
    auto candidate = std::make_unique<cave_detail::State>(*state_);
    auto result    = candidate->evolve(operation);
    if (!result.ok()) return result;
    candidate->surfaceSchedule.push_back(operation);
    state_ = std::move(candidate);
    phase_ = CaveGenerationPhase::Evolved;
    return Result<void>::success();
}

Result<void> CaveGeneration::deposit() {
    if (!editableField(phase())) return wrongPhase();
    auto candidate = std::make_unique<cave_detail::State>(*state_);
    auto result    = candidate->deposit();
    if (!result.ok()) return result;
    candidate->depositsApplied = true;
    state_                     = std::move(candidate);
    phase_                     = CaveGenerationPhase::Deposited;
    return Result<void>::success();
}

Result<ScalarField3D> CaveGeneration::field() const {
    if (!editableField(phase()) && phase() != CaveGenerationPhase::Deposited)
        return Result<ScalarField3D>::failure(wrongPhase().status());
    ScalarField3D field;
    auto          result = field.assign(state_->nx, state_->ny, state_->nz, state_->density);
    if (!result.ok()) return Result<ScalarField3D>::failure(result.status());
    return Result<ScalarField3D>::success(std::move(field));
}

Result<void> CaveGeneration::replaceField(const ScalarField3D& field) {
    if (!editableField(phase())) return wrongPhase();
    if (field.width() != state_->nx || field.height() != state_->ny || field.depth() != state_->nz)
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "replacement cave field dimensions differ from configured resolution", {},
            {}, "procgen.cave"));
    state_->density.assign(field.samples().begin(), field.samples().end());
    state_->fieldEdited = true;
    return Result<void>::success();
}

Result<MeshBuild> CaveGeneration::buildMesh() const {
    if (!editableField(phase()) && phase() != CaveGenerationPhase::Deposited)
        return Result<MeshBuild>::failure(wrongPhase().status());
    auto      candidate = std::make_unique<cave_detail::State>(*state_);
    MeshBuild mesh;
    auto      result = candidate->mesh(mesh);
    if (!result.ok()) return Result<MeshBuild>::failure(result.status());
    return Result<MeshBuild>::success(std::move(mesh));
}
}  // namespace eve::procgen
