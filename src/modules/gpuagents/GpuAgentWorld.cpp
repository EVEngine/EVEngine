#include "gpuagents/GpuAgentWorld.h"

#include "common/Diagnostic.h"

namespace eve::gpuagents {

Result<void> GpuAgentWorld::registerBackend(std::string name, EffectBackend* backend) {
    if (name.empty()) {
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "backend name is empty", "name", {}, "gpuagents"));
    }
    if (!backend) {
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "backend is null", "backend", {}, "gpuagents"));
    }
    if (backends_.count(name) != 0) {
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::AlreadyExists, "backend name already registered", name, {}, "gpuagents"));
    }
    backends_.emplace(std::move(name), backend);
    return Result<void>::success();
}

Result<void> GpuAgentWorld::unregisterBackend(const std::string& name) {
    if (backends_.erase(name) == 0) {
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "backend not registered", name, {}, "gpuagents"));
    }
    return Result<void>::success();
}

EffectBackend* GpuAgentWorld::find(const std::string& name) const {
    const auto it = backends_.find(name);
    return it == backends_.end() ? nullptr : it->second;
}

EnvironmentSnapshot GpuAgentWorld::makeSnapshot(bool includeSurface) const {
    EnvironmentSnapshot snap;
    snap.waterCurrent = waterCurrent_;
    snap.windVelocity = wind_;
    snap.groundY      = groundY_;
    snap.ceilingY     = ceilingY_;
    snap.goals        = goals_;
    snap.dangers      = dangers_;
    snap.nutrients    = nutrients_;
    snap.obstacles    = &obstacles_;
    snap.surface      = includeSurface ? const_cast<SurfaceField*>(&surface_) : nullptr;
    return snap;
}

Result<void> GpuAgentWorld::stepAll(float dt) {
    for (auto& [name, backend] : backends_) {
        (void)name;
        if (!backend) {
            return Result<void>::failure(
                Diagnostic::error(DiagnosticCode::PreconditionViolation, "registered backend is null", name, {},
                                  "gpuagents"));
        }
        const bool needSurface = backend->kind() == EffectKind::LifeNetwork;
        EnvironmentSnapshot snap = makeSnapshot(needSurface);
        auto stepped = backend->step(dt, snap);
        if (!stepped.ok()) return stepped;
    }
    return Result<void>::success();
}

}  // namespace eve::gpuagents
