#include "fluids/editor/FluidsEditorModule.h"

#include "common/Capability.h"
#include "fluids/editing/FluidTarget.h"
#include "fluids/editing/SurfaceFluidTarget.h"
#include "fluids/editing/VolumeFluidTarget.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <string_view>
#include <utility>
#include <vector>

namespace eve::fluids_editor {
namespace {

template <class Target>
editor::EditorResult<editor::AutomationOwnedTarget> makeTarget(const editor::TargetId&            id,
                                                               const editor::EditorValue::Object& request) {
    auto       target   = std::make_unique<Target>(id.value());
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

std::vector<std::string_view> FluidsAutomationTargetFactory::types() const {
    return {"fluid-simulation", "surface-fluid", "volume-fluid"};
}

editor::EditorResult<editor::AutomationOwnedTarget> FluidsAutomationTargetFactory::create(
    const editor::TargetId& target, std::string_view type, const editor::EditorValue::Object& request) {
    if (type == "fluid-simulation") {
        return makeTarget<fluids_editing::FluidSimulationTarget>(target, request);
    }
    if (type == "surface-fluid") {
        return makeTarget<fluids_editing::SurfaceFluidTarget>(target, request);
    }
    if (type == "volume-fluid") {
        return makeTarget<fluids_editing::VolumeFluidTarget>(target, request);
    }
    return eve::editing::failed<editor::AutomationOwnedTarget>(editor::EditorStatus::Unsupported,
                                                               editor::RuleId("editor.fluids.target-type"),
                                                               "Unsupported fluid editor target type");
}

Module_IMPL(FluidsEditorModule, new FluidsEditorModule());

FluidsEditorModule::FluidsEditorModule() : factory_(std::make_unique<FluidsAutomationTargetFactory>()) {
    eve::cap::addListener<editor::IEditorAutomationTargetFactory>(factory_.get());
}

FluidsEditorModule::~FluidsEditorModule() {
    eve::cap::removeListener<editor::IEditorAutomationTargetFactory>(factory_.get());
}

void FluidsEditorModule::expose(ssq::Table& table) { table.addClass(name, FluidsEditorModule::create, false); }

void FluidsEditorModule::expose(ssq::Class&) {}

}  // namespace eve::fluids_editor
