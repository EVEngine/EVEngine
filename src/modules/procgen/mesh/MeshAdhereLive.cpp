#include "procgen/mesh/MeshAdhereLive.h"

#include <utility>

namespace eve::procgen {

Result<MeshBuild> meshAdhereResult(const MeshBuild& sourceA, const MeshBuild& surfaceB,
                                   const MeshContactBlendParams& params) {
    MeshContactBlendParams adhere = params;
    adhere.softSnapPositions      = true;
    return meshContactBlendAgainstSurfaceResult(sourceA, surfaceB, adhere);
}

Result<void> MeshAdhereLive::activateResult(const MeshBuild& sourceA, const MeshBuild& surfaceB) {
    if (sourceA.empty() || surfaceB.empty())
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "mesh adhere requires non-empty source and surface",
                                                       "activate", {}, "procgen.mesh.adhere"));
    source_                  = sourceA;
    surface_                 = surfaceB;
    derived_                 = {};
    active_                  = true;
    dirty_                   = true;
    revision_                = 0;
    params_                  = MeshContactBlendParams{};
    params_.softSnapPositions = true;
    params_.strength          = 1.f;
    params_.edgeRadius        = 0.5f;
    params_.materialRadius    = 0.5f;
    params_.normalsBlend      = 1.f;
    params_.materialBlend     = 1.f;
    falloffStorage_           = "smooth";
    params_.falloff           = falloffStorage_;
    return Result<void>::success();
}

Result<void> MeshAdhereLive::setParamsResult(const MeshContactBlendParams& params) {
    if (!active_)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::PreconditionViolation,
                                                       "mesh adhere session is not active", "params", {},
                                                       "procgen.mesh.adhere"));
    if (params.falloff != "smooth" && params.falloff != "linear" && params.falloff != "sharp" &&
        params.falloff != "sphere")
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "mesh adhere falloff is invalid", "falloff", {},
                                                       "procgen.mesh.adhere"));
    falloffStorage_      = std::string(params.falloff);
    params_              = params;
    params_.falloff      = falloffStorage_;
    dirty_               = true;
    return Result<void>::success();
}

Result<void> MeshAdhereLive::setSoftSnapPositionsResult(bool enabled) noexcept {
    if (!active_)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::PreconditionViolation,
                                                       "mesh adhere session is not active", "softSnap", {},
                                                       "procgen.mesh.adhere"));
    params_.softSnapPositions = enabled;
    dirty_                    = true;
    return Result<void>::success();
}

Result<void> MeshAdhereLive::setSurfaceResult(const MeshBuild& surfaceB) {
    if (!active_)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::PreconditionViolation,
                                                       "mesh adhere session is not active", "surface", {},
                                                       "procgen.mesh.adhere"));
    if (surfaceB.empty())
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "mesh adhere surface is empty", "surface", {},
                                                       "procgen.mesh.adhere"));
    surface_ = surfaceB;
    dirty_   = true;
    return Result<void>::success();
}

Result<void> MeshAdhereLive::setSourceResult(const MeshBuild& sourceA) {
    if (!active_)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::PreconditionViolation,
                                                       "mesh adhere session is not active", "source", {},
                                                       "procgen.mesh.adhere"));
    if (sourceA.empty())
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "mesh adhere source is empty", "source", {},
                                                       "procgen.mesh.adhere"));
    source_ = sourceA;
    dirty_  = true;
    return Result<void>::success();
}

Result<std::uint64_t> MeshAdhereLive::evaluateResult(bool force) {
    if (!active_)
        return Result<std::uint64_t>::failure(Diagnostic::error(DiagnosticCode::PreconditionViolation,
                                                                "mesh adhere session is not active", "evaluate", {},
                                                                "procgen.mesh.adhere"));
    if (!dirty_ && !force && !derived_.empty()) return Result<std::uint64_t>::success(revision_);
    auto blended = meshContactBlendAgainstSurfaceResult(source_, surface_, params_);
    if (!blended.ok()) return Result<std::uint64_t>::failure(blended.status());
    derived_ = std::move(blended).takeValue();
    dirty_   = false;
    ++revision_;
    return Result<std::uint64_t>::success(revision_);
}

const MeshBuild* MeshAdhereLive::derivedMesh() const noexcept {
    return derived_.empty() ? nullptr : &derived_;
}

Result<MeshBuild> MeshAdhereLive::derivedMeshResult() const {
    if (!active_)
        return Result<MeshBuild>::failure(Diagnostic::error(DiagnosticCode::PreconditionViolation,
                                                            "mesh adhere session is not active", "derived", {},
                                                            "procgen.mesh.adhere"));
    if (derived_.empty())
        return Result<MeshBuild>::failure(Diagnostic::error(DiagnosticCode::PreconditionViolation,
                                                            "mesh adhere has no derived mesh; evaluate first",
                                                            "derived", {}, "procgen.mesh.adhere"));
    return Result<MeshBuild>::success(derived_);
}

const MeshBuild* MeshAdhereLive::sourceMesh() const noexcept {
    return source_.empty() ? nullptr : &source_;
}

void MeshAdhereLive::removeSetup() noexcept {
    active_    = false;
    dirty_     = false;
    revision_  = 0;
    source_    = {};
    surface_   = {};
    derived_   = {};
    params_    = {};
}

Result<MeshBuild> MeshAdhereLive::bakeToMeshResult() {
    auto evaluated = evaluateResult(false);
    if (!evaluated.ok()) return Result<MeshBuild>::failure(evaluated.status());
    MeshBuild baked = derived_;
    removeSetup();
    return Result<MeshBuild>::success(std::move(baked));
}

}  // namespace eve::procgen
