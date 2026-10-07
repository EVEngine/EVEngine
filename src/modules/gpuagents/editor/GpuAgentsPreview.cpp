#include "gpuagents/editor/GpuAgentsPreview.h"

#include "gpuagents/EffectBackend.h"
#include "gpuagents/GpuAgentWorld.h"
#include "gpuagents/editing/GpuAgentsDocument.h"

#include <algorithm>
#include <cmath>
#include <memory>

namespace eve::gpuagents_editor {

GpuAgentsPreviewSnapshot GpuAgentsPreviewService::build(const GpuAgentsDocumentTarget& target,
                                                        const GpuAgentsPreviewRequest& request) const {
    GpuAgentsPreviewSnapshot out;
    out.documentRevision = target.describe().revision;
    out.diagnostics      = target.validate();
    if (std::any_of(out.diagnostics.begin(), out.diagnostics.end(),
                    [](const auto& d) { return d.severity() == gpuagents_editing::DiagnosticSeverity::Error; })) {
        out.status = EditorStatus::Failed;
        return out;
    }
    if (request.agentCount <= 0 || request.seconds < 0.0 || request.fixedStep <= 0.0 || request.maximumSteps == 0) {
        out.status = EditorStatus::Failed;
        out.diagnostics.push_back(eve::editing::ruleDiagnostic(
            eve::DiagnosticCode::InvalidArgument, gpuagents_editing::RuleId("editor.gpuagents.preview.request"),
            gpuagents_editing::DiagnosticSeverity::Error, "Invalid GPU Agents preview request"));
        return out;
    }

    gpuagents::GpuAgentWorld                   world;
    auto                                       backend = std::make_unique<gpuagents::EffectBackend>();
    gpuagents_editing::GpuAgentsRuntimeApplier applier;
    auto                                       applied = applier.apply(target, backend.get(), &world);
    if (!applied.ok()) {
        out.status = EditorStatus::Failed;
        out.diagnostics.push_back(eve::editing::ruleDiagnostic(
            eve::DiagnosticCode::Failed, gpuagents_editing::RuleId("editor.gpuagents.preview.apply"),
            gpuagents_editing::DiagnosticSeverity::Error, "Failed to apply GPU Agents document to preview runtime"));
        return out;
    }

    const int count   = std::min(request.agentCount, backend->simulation().capacity());
    auto      spawned = backend->simulation().initialize(count, [&](int i) {
        gpuagents::AgentState s;
        const float           a = static_cast<float>(i) * 0.6180339f;
        const float           r = static_cast<float>(request.spawnSpread) * (0.2f + 0.8f * std::fmod(a, 1.f));
        s.position              = {static_cast<float>(request.centerX) + std::cos(a * 6.2831853f) * r,
                                   static_cast<float>(request.centerY),
                                   static_cast<float>(request.centerZ) + std::sin(a * 6.2831853f) * r};
        s.velocity              = {std::cos(a), 0.f, std::sin(a)};
        if (target.settings().kind == "Petal") {
            s.velocity      = {0.f, -1.f, 0.f};
            s.customData[1] = 1.f;
            s.position.y += 2.f;
        }
        if (target.settings().kind == "LifeNetwork") s.position.y = static_cast<float>(request.centerY);
        return s;
    });
    if (!spawned.ok()) {
        out.status = EditorStatus::Failed;
        return out;
    }

    auto registered = world.registerBackend("preview", backend.get());
    if (!registered.ok()) {
        out.status = EditorStatus::Failed;
        return out;
    }
    const double  step  = request.fixedStep;
    std::uint32_t steps = 0;
    double        t     = 0.0;
    while (t + 1e-12 < request.seconds && steps < request.maximumSteps) {
        auto stepped = world.stepAll(static_cast<float>(step));
        if (!stepped.ok()) {
            out.status = EditorStatus::Failed;
            return out;
        }
        t += step;
        ++steps;
    }
    out.simulatedSeconds = t;
    for (const auto& s : backend->simulation().states()) {
        if (!s.alive) continue;
        GpuAgentsPreviewAgent a;
        a.x       = s.position.x;
        a.y       = s.position.y;
        a.z       = s.position.z;
        a.vx      = s.velocity.x;
        a.vy      = s.velocity.y;
        a.vz      = s.velocity.z;
        a.age     = s.age;
        a.custom0 = s.customData[0];
        out.agents.push_back(a);
    }
    if (target.settings().kind == "LifeNetwork") {
        for (const auto& c : world.surface().lifeField) out.lifeTrailEnergy += c.r;
    }
    out.status = EditorStatus::Applied;
    return out;
}

}  // namespace eve::gpuagents_editor
