#include "gpuagents/editor/GpuAgentsEditorModule.h"

#include "common/Capability.h"
#include "gpuagents/editing/GpuAgentsDocument.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <string_view>
#include <utility>
#include <vector>

namespace eve::gpuagents_editor {
namespace {

editor::EditorResult<editor::AutomationOwnedTarget> makeTarget(const editor::TargetId&            id,
                                                               const editor::EditorValue::Object& request) {
    auto       target   = std::make_unique<gpuagents_editing::GpuAgentsDocumentTarget>(id.value());
    const auto snapshot = request.find("snapshot");
    if (snapshot != request.end()) {
        auto loaded = target->loadSnapshot(snapshot->second);
        if (!loaded.ok()) {
            return editor::EditorResult<editor::AutomationOwnedTarget>::failure(loaded.status());
        }
    }
    editor::AutomationOwnedTarget owned;
    owned.target = std::move(target);
    return eve::editing::applied<editor::AutomationOwnedTarget>(std::move(owned));
}

}  // namespace

Module_IMPL(GpuAgentsEditorModule, new GpuAgentsEditorModule());

std::vector<std::string_view> GpuAgentsAutomationTargetFactory::types() const { return {"gpuagents-effect"}; }

editor::EditorResult<editor::AutomationOwnedTarget> GpuAgentsAutomationTargetFactory::create(
    const editor::TargetId& target, std::string_view type, const editor::EditorValue::Object& request) {
    if (type != "gpuagents-effect") {
        return eve::editing::failed<editor::AutomationOwnedTarget>(editor::EditorStatus::Rejected,
                                                                   editor::RuleId("editor.gpuagents.factory.type"),
                                                                   "Unsupported GPU Agents automation target type");
    }
    return makeTarget(target, request);
}

GpuAgentsEditorModule::GpuAgentsEditorModule() : factory_(std::make_unique<GpuAgentsAutomationTargetFactory>()) {
    eve::cap::addListener<editor::IEditorAutomationTargetFactory>(factory_.get());
}

GpuAgentsEditorModule::~GpuAgentsEditorModule() {
    eve::cap::removeListener<editor::IEditorAutomationTargetFactory>(factory_.get());
}

void GpuAgentsEditorModule::expose(ssq::Table& table) { table.addClass(name, GpuAgentsEditorModule::create, false); }

void GpuAgentsEditorModule::expose(ssq::Class&) {}

}  // namespace eve::gpuagents_editor
