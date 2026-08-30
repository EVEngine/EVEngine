#include "editor/EditorExtension.h"

#include <algorithm>
#include <exception>

namespace eve::editor {

EditorResult<void> EditorExtensionRegistry::load(IGameEditorExtension& extension) {
    if (!commands_)
        return invalid("editor.extension.missing-command-service", "Extension registry has no command service");
    const std::string owner = extension.ownerModule();
    if (owner.empty()) return invalid("editor.extension.missing-owner", "Game editor extension owner is required");
    if (std::any_of(commandVisibility_.begin(), commandVisibility_.end(),
                    [&](const CommandVisibility& command) { return command.ownerModule == owner; }) ||
        std::any_of(tools_.begin(), tools_.end(),
                    [&](const ExtensionToolDescriptor& tool) { return tool.ownerModule == owner; }))
        return invalid("editor.extension.duplicate-owner", "Game editor extension owner is already loaded");
    activeOwner_ = owner;
    try {
        extension.registerEditor(*this);
    } catch (const std::exception& exception) {
        unload(owner);
        activeOwner_.clear();
        return invalid("editor.extension.exception", exception.what());
    } catch (...) {
        unload(owner);
        activeOwner_.clear();
        return invalid("editor.extension.exception", "Game editor extension threw an unknown exception");
    }
    activeOwner_.clear();
    return EditorResult<void>::applied();
}

std::size_t EditorExtensionRegistry::unload(const std::string& ownerModule) {
    std::size_t removed    = commands_ ? commands_->unregisterOwner(ownerModule) : 0;
    auto        eraseOwner = [&](auto& values) {
        const std::size_t before = values.size();
        std::erase_if(values, [&](const auto& value) { return value.ownerModule == ownerModule; });
        removed += before - values.size();
    };
    eraseOwner(commandVisibility_);
    eraseOwner(tools_);
    eraseOwner(palettes_);
    eraseOwner(rules_);
    return removed;
}

void EditorExtensionRegistry::configureProfile(HostProfile& profile) const {
    const ExtensionAudience audience = audienceFor(profile);
    for (const CommandVisibility& command : commandVisibility_)
        if (hasAudience(command.audiences, audience)) profile.allowCommand(command.id);
}

std::vector<ExtensionToolDescriptor> EditorExtensionRegistry::tools(const HostProfile& profile) const {
    std::vector<ExtensionToolDescriptor> result;
    const ExtensionAudience              audience = audienceFor(profile);
    for (const ExtensionToolDescriptor& tool : tools_)
        if (hasAudience(tool.audiences, audience)) result.push_back(tool);
    return result;
}

std::vector<ExtensionPaletteDescriptor> EditorExtensionRegistry::palettes(const HostProfile& profile) const {
    std::vector<ExtensionPaletteDescriptor> result;
    const ExtensionAudience                 audience = audienceFor(profile);
    for (const ExtensionPaletteDescriptor& palette : palettes_)
        if (hasAudience(palette.audiences, audience)) result.push_back(palette);
    return result;
}

std::vector<ExtensionRuleDescriptor> EditorExtensionRegistry::rules(const HostProfile& profile) const {
    std::vector<ExtensionRuleDescriptor> result;
    const ExtensionAudience              audience = audienceFor(profile);
    for (const ExtensionRuleDescriptor& rule : rules_)
        if (hasAudience(rule.audiences, audience)) result.push_back(rule);
    return result;
}

EditorResult<EditorValue> EditorExtensionRegistry::registerCommand(CommandDescriptor    descriptor,
                                                                   EditorCommandHandler handler,
                                                                   ExtensionAudience    audiences) {
    if (activeOwner_.empty())
        return EditorResult<EditorValue>::error(EditorStatus::Rejected,
                                                RuleId("editor.extension.registration-outside-load"),
                                                "Commands may only be registered while loading an extension");
    descriptor.ownerModule           = activeOwner_;
    EditorResult<EditorValue> result = commands_->registerCommand(descriptor, std::move(handler));
    if (result.isAccepted()) commandVisibility_.push_back({descriptor.id, activeOwner_, audiences});
    return result;
}

EditorResult<void> EditorExtensionRegistry::registerTool(ExtensionToolDescriptor descriptor) {
    if (activeOwner_.empty() || descriptor.id.empty())
        return invalid("editor.extension.invalid-tool", "Tool registration requires an active owner and id");
    if (std::any_of(tools_.begin(), tools_.end(),
                    [&](const ExtensionToolDescriptor& tool) { return tool.id == descriptor.id; }))
        return invalid("editor.extension.duplicate-tool", "Tool id is already registered");
    descriptor.ownerModule = activeOwner_;
    tools_.push_back(std::move(descriptor));
    return EditorResult<void>::applied();
}

EditorResult<void> EditorExtensionRegistry::registerPalette(ExtensionPaletteDescriptor descriptor) {
    if (activeOwner_.empty() || descriptor.id.empty())
        return invalid("editor.extension.invalid-palette", "Palette registration requires an active owner and id");
    descriptor.ownerModule = activeOwner_;
    palettes_.push_back(std::move(descriptor));
    return EditorResult<void>::applied();
}

EditorResult<void> EditorExtensionRegistry::registerRule(ExtensionRuleDescriptor descriptor) {
    if (activeOwner_.empty() || descriptor.id.empty())
        return invalid("editor.extension.invalid-rule", "Rule registration requires an active owner and id");
    descriptor.ownerModule = activeOwner_;
    rules_.push_back(std::move(descriptor));
    return EditorResult<void>::applied();
}

ExtensionAudience EditorExtensionRegistry::audienceFor(const HostProfile& profile) {
    switch (profile.kind()) {
        case HostKind::Developer: return ExtensionAudience::Developer;
        case HostKind::RuntimeBuilder: return ExtensionAudience::Player;
        case HostKind::RuntimeAdmin: return ExtensionAudience::Admin;
        case HostKind::Automation: return ExtensionAudience::Automation;
    }
    return ExtensionAudience::None;
}

EditorResult<void> EditorExtensionRegistry::invalid(const char* rule, std::string message) {
    return EditorResult<void>::error(EditorStatus::Rejected, RuleId(rule), std::move(message));
}

}  // namespace eve::editor
