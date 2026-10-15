#include "editor/EditorMcpProjector.h"

#include "common/Capability.h"
#include "editing/EditorPackage.h"
#include "editor/EditorAutomationTargetFactory.h"
#include "editor/EditorHost.h"

#include <algorithm>
#include <set>

namespace eve::editor {

EditorMcpProjector& editorMcpProjector() {
    static EditorMcpProjector projector;
    return projector;
}

Result<void> EditorMcpProjector::attach(EditorHost* host, EditorMcpExportMode mode) {
    if (mode != EditorMcpExportMode::Commands)
        return eve::editing::failed<void>(EditorStatus::Unsupported, RuleId("editor.mcp.mode"),
                                          "Only MCP export mode Commands (A) is supported");
    host_ = host;
    mode_ = mode;
    return eve::editing::applied<void>();
}

void EditorMcpProjector::detach() { host_ = nullptr; }

std::unordered_set<std::string> EditorMcpProjector::mountedOwners() const {
    std::unordered_set<std::string> owners;
    if (!host_) return owners;
    for (const std::string& owner : host_->mountedOwnerNames()) owners.insert(owner);
    return owners;
}

std::unordered_set<std::string> EditorMcpProjector::mountedTargetTypes() const {
    std::unordered_set<std::string> types;
    if (!host_) return types;
    for (const std::string& type : host_->mountedTargetTypeNames()) types.insert(type);
    return types;
}

Result<EditorValue> EditorMcpProjector::auditCompleteness() const {
    EditorValue::Array incomplete;
    eve::cap::forEach<editing::IEditorPackageProvider>([&](editing::IEditorPackageProvider* provider) {
        if (!provider) return;
        const editing::EditorPackage& package = provider->package();
        if (!package.incomplete) return;
        EditorValue::Object item;
        item["owner"]  = package.ownerModule;
        item["reason"] = "package.incomplete";
        incomplete.emplace_back(std::move(item));
    });

    std::set<std::string> packagedTypes;
    eve::cap::forEach<editing::IEditorPackageProvider>([&](editing::IEditorPackageProvider* provider) {
        if (!provider) return;
        const editing::EditorPackage& package = provider->package();
        if (package.incomplete) return;
        for (const editing::EditorTargetTypeSpec& target : package.targetTypes) packagedTypes.insert(target.type);
    });

    EditorValue::Array orphans;
    eve::cap::forEach<IEditorAutomationTargetFactory>([&](IEditorAutomationTargetFactory* factory) {
        if (!factory) return;
        for (const std::string_view type : factory->types()) {
            if (packagedTypes.contains(std::string(type))) continue;
            orphans.emplace_back(std::string(type));
        }
    });
    std::sort(orphans.begin(), orphans.end(), [](const EditorValue& left, const EditorValue& right) {
        const auto* leftString  = left.getIf<std::string>();
        const auto* rightString = right.getIf<std::string>();
        return (leftString ? *leftString : std::string{}) < (rightString ? *rightString : std::string{});
    });

    EditorValue::Object report;
    report["incompletePackages"] = std::move(incomplete);
    report["orphanFactoryTypes"] = std::move(orphans);
    report["mode"]               = "commands";
    report["attached"]           = isAttached();
    if (host_) report["hostId"] = host_->workspace().getId();
    return eve::editing::applied<EditorValue>(EditorValue(std::move(report)));
}

}  // namespace eve::editor
