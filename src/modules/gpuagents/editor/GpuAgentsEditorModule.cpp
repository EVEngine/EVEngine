#include "gpuagents/editor/GpuAgentsEditorModule.h"

#include "common/Capability.h"
#include "editing/EditorPackage.h"
#include "gpuagents/EffectBackend.h"
#include "gpuagents/GpuAgentWorld.h"
#include "gpuagents/editing/GpuAgentsDocument.h"
#include "gpuagents/editor/GpuAgentsEditorPackage.h"
#include "gpuagents/editor/GpuAgentsPreview.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <algorithm>
#include <cmath>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

// GpuAgentsEditorModule.cpp
namespace eve::gpuagents_editor {
namespace {

editor::Result<editor::AutomationOwnedTarget> makeTarget(const editor::TargetId&            id,
                                                         const editor::EditorValue::Object& request) {
    auto       target   = std::make_unique<gpuagents_editing::GpuAgentsDocumentTarget>(id.value());
    const auto snapshot = request.find("snapshot");
    if (snapshot != request.end()) {
        auto loaded = target->loadSnapshot(snapshot->second);
        if (!loaded.ok()) {
            return editor::Result<editor::AutomationOwnedTarget>::failure(loaded.status());
        }
    }
    editor::AutomationOwnedTarget owned;
    owned.target = std::move(target);
    return eve::editing::applied<editor::AutomationOwnedTarget>(std::move(owned));
}

}  // namespace

Module_IMPL(GpuAgentsEditorModule, new GpuAgentsEditorModule());

std::vector<std::string_view> GpuAgentsAutomationTargetFactory::types() const { return {"gpuagents-effect"}; }

editor::Result<editor::AutomationOwnedTarget> GpuAgentsAutomationTargetFactory::create(
    const editor::TargetId& target, std::string_view type, const editor::EditorValue::Object& request) {
    if (type != "gpuagents-effect") {
        return eve::editing::failed<editor::AutomationOwnedTarget>(editor::EditorStatus::Rejected,
                                                                   editor::RuleId("editor.gpuagents.factory.type"),
                                                                   "Unsupported GPU Agents automation target type");
    }
    return makeTarget(target, request);
}

GpuAgentsEditorModule::GpuAgentsEditorModule()
    : factory_(std::make_unique<GpuAgentsAutomationTargetFactory>()),
      packageProvider_(std::make_unique<GpuAgentsEditorPackageProvider>()) {
    eve::cap::addListener<editor::IEditorAutomationTargetFactory>(factory_.get());
    eve::cap::addListener<editing::IEditorPackageProvider>(packageProvider_.get());
}

GpuAgentsEditorModule::~GpuAgentsEditorModule() {
    eve::cap::removeListener<editing::IEditorPackageProvider>(packageProvider_.get());
    eve::cap::removeListener<editor::IEditorAutomationTargetFactory>(factory_.get());
}

void GpuAgentsEditorModule::expose(ssq::Table& table) { table.addClass(name, GpuAgentsEditorModule::create, false); }

void GpuAgentsEditorModule::expose(ssq::Class&) {}

}  // namespace eve::gpuagents_editor

// GpuAgentsPreview.cpp
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
