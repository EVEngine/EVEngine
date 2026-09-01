#include "editor/EditorHostProfile.h"

namespace eve::editor {

HostProfile HostProfile::developer() {
    HostProfile profile(HostKind::Developer);
    profile.features_ = HostFeature::ProjectDocuments | HostFeature::SourceAssets | HostFeature::RuntimeWorld |
                        HostFeature::ArbitraryScript | HostFeature::BuildCook | HostFeature::SourceControl |
                        HostFeature::MultiplayerSubmit | HostFeature::UserCreations | HostFeature::DebugOverride;
    profile.allowAllCommands_ = true;
    profile.maxPayloadBytes_  = 4 * 1024 * 1024;
    return profile;
}

HostProfile HostProfile::runtimeBuilder() {
    HostProfile profile(HostKind::RuntimeBuilder);
    profile.features_ = HostFeature::RuntimeWorld | HostFeature::UserCreations;
    return profile;
}

HostProfile HostProfile::automation() {
    HostProfile profile(HostKind::Automation);
    profile.features_        = HostFeature::ProjectDocuments | HostFeature::RuntimeWorld;
    profile.maxPayloadBytes_ = 1024 * 1024;
    return profile;
}

bool HostProfile::hasFeatures(HostFeature required) const { return (features_ & required) == required; }

void HostProfile::allowCommand(CommandId id) {
    if (!id) return;
    deniedCommands_.erase(id);
    allowedCommands_.insert(std::move(id));
}

void HostProfile::allowCommands(std::initializer_list<CommandId> ids) {
    for (const auto& id : ids) allowCommand(id);
}

void HostProfile::denyCommand(const CommandId& id) {
    if (!id) return;
    allowedCommands_.erase(id);
    deniedCommands_.insert(id);
}

bool HostProfile::allowsCommand(const CommandId& id) const {
    if (!id || deniedCommands_.contains(id)) return false;
    return allowAllCommands_ || allowedCommands_.contains(id);
}

}  // namespace eve::editor
