#include "scene/editor/SceneEditorSession.h"

#include "editing/EditingValueJson.h"
#include "editor/EditorCommandService.h"
#include "editor/EditorTargetCoordinator.h"
#include "scene/editing/SceneTarget.h"

#include <stdexcept>

namespace eve::scene_editor {
struct SceneEditorSession::Impl {
    explicit Impl(std::unique_ptr<scene_editing::SceneTargetBase> value)
        : target(std::move(value)), coordinator(commands) {
        if (!target || target->targetId().empty()) throw std::invalid_argument("Scene target must have an identity");
        if (!scene_editing::registerEditingCommands(coordinator).ok() || !coordinator.registerTarget(*target).ok())
            throw std::runtime_error("Could not register scene editing target");
    }
    std::unique_ptr<scene_editing::SceneTargetBase> target;
    editor::EditorCommandService                    commands;
    editor::EditorTargetCoordinator                 coordinator;
    editor::HostProfile                             profile = editor::HostProfile::developer();
};
SceneEditorSession::SceneEditorSession(std::string id)
    : SceneEditorSession(std::make_unique<scene_editing::SceneDocumentTarget>(std::move(id))) {}
SceneEditorSession::SceneEditorSession(std::unique_ptr<scene_editing::SceneTargetBase> target)
    : impl_(std::make_unique<Impl>(std::move(target))) {}
SceneEditorSession::~SceneEditorSession() = default;

editing::Result<editing::TransactionReceipt> SceneEditorSession::execute(std::string command, editing::Value payload) {
    editing::CommandRequest request;
    request.id                     = editing::CommandId(std::move(command));
    request.payload                = std::move(payload);
    request.context.target         = impl_->target->targetId();
    request.context.targetRevision = impl_->target->revision();
    const auto& profile            = impl_->profile;
    auto        plan               = impl_->commands.plan(request, profile);
    if (!plan.ok()) return editing::Result<editing::TransactionReceipt>::failure(plan.status());
    // Planning resolves the target generation; execution immediately validates it again.
    request.context.targetGeneration = plan.value().targetGeneration;
    return impl_->commands.executePlan(request, plan.value(), profile);
}
editing::Result<void> SceneEditorSession::restrictCommands(const std::vector<std::string>& commands) {
    auto profile = editor::HostProfile::runtimeBuilder();
    for (const auto& command : commands) {
        if (command.empty())
            return editing::failed<void>(editing::Status::Rejected, editing::RuleId("scene.session.empty-command"),
                                         "Allowed command id must not be empty");
        profile.allowCommand(editing::CommandId(command));
    }
    impl_->profile = std::move(profile);
    return editing::applied<void>();
}
editing::Result<editing::TransactionReceipt> SceneEditorSession::undo() {
    return impl_->coordinator.undo(impl_->target->targetId());
}
editing::Result<editing::TransactionReceipt> SceneEditorSession::redo() {
    return impl_->coordinator.redo(impl_->target->targetId());
}
editing::Value    SceneEditorSession::snapshot() const { return impl_->target->snapshotValue(); }
std::string       SceneEditorSession::saveJson() const { return editing::editorValueToJson(snapshot()); }
editing::Revision SceneEditorSession::revision() const { return impl_->target->revision(); }
editing::Result<editing::TransactionReceipt> SceneEditorSession::restoreJson(const std::string& json) {
    auto value = editing::editorValueFromJson(json);
    if (!value.ok()) return editing::Result<editing::TransactionReceipt>::failure(value.status());
    return execute("scene.snapshot.restore.v1", std::move(value.value()));
}
}  // namespace eve::scene_editor
