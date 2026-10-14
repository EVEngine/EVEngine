#include "editing/EditorPackage.h"

namespace eve::editing {

Result<void> validateEditorPackage(const EditorPackage& package, bool allowIncomplete) {
    if (package.ownerModule.empty())
        return failed<void>(Status::Rejected, RuleId("editing.package.empty-owner"),
                            "EditorPackage.ownerModule must not be empty");
    if (package.incomplete && !allowIncomplete)
        return failed<void>(Status::Rejected, RuleId("editing.package.incomplete"),
                            "EditorPackage is marked incomplete and Host rejected the mount");

    const bool hasAutomationCommand = [&] {
        for (const EditingCommandDescriptor& command : package.commands)
            if (command.automationAllowed) return true;
        return false;
    }();

    if (hasAutomationCommand && package.panels.empty())
        return failed<void>(Status::Rejected, RuleId("editing.package.panel-required"),
                            "Automation-allowed commands require at least one package panel");

    for (const EditorPanelSpec& panel : package.panels) {
        if (panel.id.empty() || panel.region.empty() || panel.capability.empty() || panel.context.empty())
            return failed<void>(Status::Rejected, RuleId("editing.package.panel-incomplete"),
                                "EditorPanelSpec requires id, region, capability and context");
    }

    for (const EditingCommandDescriptor& command : package.commands) {
        if (command.id.empty() || command.ownerModule.empty())
            return failed<void>(Status::Rejected, RuleId("editing.package.command-id"),
                                "Package commands require id and ownerModule");
        if (command.ownerModule != package.ownerModule)
            return failed<void>(Status::Rejected, RuleId("editing.package.command-owner"),
                                "Package command ownerModule must match package.ownerModule");
        if (command.payloadSchema.typeId.empty() && command.payloadSchema.properties.empty())
            return failed<void>(Status::Rejected, RuleId("editing.package.payload-schema"),
                                "Package commands require an explicit payloadSchema (use emptyObject)");
        if (command.automationAllowed) {
            if (command.panelCapability.empty() || command.panelContext.empty())
                return failed<void>(Status::Rejected, RuleId("editing.package.panel-binding"),
                                    "Automation-allowed commands require panelCapability and panelContext");
            bool bound = false;
            for (const EditorPanelSpec& panel : package.panels) {
                if (panel.capability == command.panelCapability && panel.context == command.panelContext) {
                    bound = true;
                    break;
                }
            }
            if (!bound)
                return failed<void>(Status::Rejected, RuleId("editing.package.panel-binding-missing"),
                                    "Command panelBinding does not match any package panel");
        }
    }

    for (const EditorTargetTypeSpec& target : package.targetTypes) {
        if (target.type.empty())
            return failed<void>(Status::Rejected, RuleId("editing.package.target-type"),
                                "Package targetTypes require a non-empty type");
        if (target.createSchema.typeId.empty() && target.createSchema.properties.empty())
            return failed<void>(Status::Rejected, RuleId("editing.package.create-schema"),
                                "Package targetTypes require an explicit createSchema (use emptyObject)");
    }

    return applied<void>();
}

}  // namespace eve::editing
