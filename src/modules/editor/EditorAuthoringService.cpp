#include "editor/EditorAuthoringService.h"

#include "editor/EditorAuthority.h"
#include "editor/EditorMaterialTarget.h"
#include "editor/EditorSceneTarget.h"
#include "editor/EditorSession.h"
#include "editor/EditorTransactionService.h"

#include <map>
#include <utility>

namespace eve::editor {
namespace {

template <class T>
EditorResult<T> authoringError(EditorStatus status, const char* rule, std::string message) {
    return EditorResult<T>::error(status, RuleId(rule), std::move(message));
}

const EditorValue* field(const EditorValue& value, const char* key) {
    const auto* object = value.getIf<EditorValue::Object>();
    if (!object) return nullptr;
    const auto found = object->find(key);
    return found == object->end() ? nullptr : &found->second;
}

const std::string* stringField(const EditorValue& value, const char* key) {
    const EditorValue* entry = field(value, key);
    return entry ? entry->getIf<std::string>() : nullptr;
}

bool assignVector3(const EditorValue& payload, const char* key, double& x, double& y, double& z) {
    const EditorValue* entry = field(payload, key);
    if (!entry) return true;
    const auto* values = entry->getIf<EditorValue::Array>();
    if (!values || values->size() != 3) return false;
    const auto number = [](const EditorValue& value, double& output) {
        if (const auto* real = value.getIf<double>()) {
            output = *real;
            return true;
        }
        if (const auto* integer = value.getIf<std::int64_t>()) {
            output = static_cast<double>(*integer);
            return true;
        }
        return false;
    };
    if (!number((*values)[0], x) || !number((*values)[1], y) || !number((*values)[2], z)) return false;
    return true;
}

SelectionSnapshot materialSelection(const MaterialDocumentTarget& target) {
    SelectionSnapshot selection;
    selection.channel = "asset";
    SelectionItem item;
    item.domain = SelectionDomain::Asset;
    item.target = TargetId(target.targetId());
    item.item   = StableId(target.targetId());
    item.type   = "graphics.material";
    selection.items.push_back(item);
    selection.primary = item;
    return selection;
}

EditorValue targetDescriptorValue(const TargetDescriptor& descriptor) {
    EditorValue::Array capabilities;
    for (const CapabilityId& capability : descriptor.capabilities)
        capabilities.emplace_back(capability.value());
    return EditorValue::Object{{"id", descriptor.id.value()},
                               {"type", descriptor.type},
                               {"revision", static_cast<std::int64_t>(descriptor.revision)},
                               {"readOnly", descriptor.readOnly},
                               {"capabilities", std::move(capabilities)}};
}

}  // namespace

struct EditorAuthoringService::Impl {
    struct TargetEntry {
        explicit TargetEntry(IEditableTargetV2& value, IDomainOperationTarget& operations)
            : target(&value), authority(&operations), transactions(&authority) {}

        IEditableTargetV2*      target = nullptr;
        LocalWorldAuthority     authority;
        LocalTransactionBackend transactions;
    };

    explicit Impl(EditorCommandService& value) : commands(&value) { registerCommands(); }

    TargetEntry* entry(const TargetId& id) {
        const auto found = targets.find(id);
        return found == targets.end() ? nullptr : found->second.get();
    }

    const TargetEntry* entry(const TargetId& id) const {
        const auto found = targets.find(id);
        return found == targets.end() ? nullptr : found->second.get();
    }

    EditorResult<TransactionReceipt> execute(const CommandRequest&, const CommandPlan& plan) {
        TargetEntry* selected = entry(plan.target);
        if (!selected || !selected->target)
            return authoringError<TransactionReceipt>(EditorStatus::NotFound,
                                                       "editor.authoring.target-not-found",
                                                       "Authoring target is not registered: " + plan.target.value());
        if (selected->target->revision() != plan.baseRevision)
            return authoringError<TransactionReceipt>(EditorStatus::Conflict,
                                                       "editor.authoring.revision-conflict",
                                                       "Authoring target changed after the command was planned");
        TransactionSpec specification;
        specification.id           = TransactionId(plan.id.value() + ".transaction");
        specification.label        = plan.command.value();
        specification.origin       = ActionOrigin::Automation;
        specification.target       = plan.target;
        specification.baseRevision = plan.baseRevision;
        auto begun = selected->transactions.begin(std::move(specification));
        if (!begun.accepted())
            return authoringError<TransactionReceipt>(begun.status, "editor.authoring.begin-failed",
                                                       "Could not begin the authoring transaction");
        for (const DomainOperation& operation : plan.operations) {
            auto appended = selected->transactions.append(operation);
            if (!appended.accepted()) {
                auto discarded = selected->transactions.discard();
                if (!discarded.accepted())
                    return authoringError<TransactionReceipt>(EditorStatus::Failed,
                                                               "editor.authoring.discard-failed",
                                                               "Could not discard a rejected authoring transaction");
                return authoringError<TransactionReceipt>(appended.status,
                                                           "editor.authoring.append-failed",
                                                           "Could not stage the authoring operation");
            }
        }
        return selected->transactions.commit();
    }

    void registerCommands() {
        CommandDescriptor transform;
        transform.id                = CommandId("scene.transform.set.v1");
        transform.ownerModule       = "editor";
        transform.displayName       = "Set scene transform";
        transform.category          = "Scene";
        transform.automationAllowed = true;
        auto sceneRegistered = commands->registerPlannedCommand(
            std::move(transform),
            [this](const CommandRequest& request) {
                TargetEntry* selected = entry(request.context.target);
                if (!selected || !selected->target)
                    return authoringError<CommandPlan>(EditorStatus::NotFound,
                                                       "editor.authoring.target-not-found",
                                                       "Scene target is not registered");
                auto* capability = static_cast<ITransformEditTarget*>(
                    selected->target->queryCapability(ITransformEditTarget::editorCapabilityId()));
                const std::string* object = stringField(request.payload, "object");
                if (!capability || !object)
                    return authoringError<CommandPlan>(EditorStatus::Rejected,
                                                       "editor.authoring.scene-payload",
                                                       "Scene transform requires a target with transform capability and an object id");
                auto current = capability->readTransform(ObjectId(*object));
                if (!current.accepted() || !current.value)
                    return authoringError<CommandPlan>(current.status,
                                                       "editor.authoring.scene-object",
                                                       "Scene object transform is unavailable");
                SceneTransformValue transformValue = *current.value;
                if (!assignVector3(request.payload, "position", transformValue.x, transformValue.y, transformValue.z) ||
                    !assignVector3(request.payload, "rotation", transformValue.rotationX, transformValue.rotationY,
                                   transformValue.rotationZ) ||
                    !assignVector3(request.payload, "scale", transformValue.scaleX, transformValue.scaleY,
                                   transformValue.scaleZ))
                    return authoringError<CommandPlan>(EditorStatus::Rejected,
                                                       "editor.authoring.scene-vector",
                                                       "Position, rotation and scale must be arrays of three numbers");
                auto operation = capability->makeSetTransform(ObjectId(*object), transformValue);
                if (!operation.accepted() || !operation.value)
                    return authoringError<CommandPlan>(operation.status,
                                                       "editor.authoring.scene-operation",
                                                       "Scene target rejected the transform");
                CommandPlan plan;
                plan.operations.push_back(std::move(*operation.value));
                plan.summary = EditorValue::Object{{"object", *object}};
                return EditorResult<CommandPlan>::applied(std::move(plan));
            },
            [this](const CommandRequest& request, const CommandPlan& plan) { return execute(request, plan); });

        CommandDescriptor material;
        material.id                = CommandId("material.property.set.v1");
        material.ownerModule       = "editor";
        material.displayName       = "Set material property";
        material.category          = "Material";
        material.automationAllowed = true;
        auto materialRegistered = commands->registerPlannedCommand(
            std::move(material),
            [this](const CommandRequest& request) {
                TargetEntry* selected = entry(request.context.target);
                auto* materialTarget = selected ? dynamic_cast<MaterialDocumentTarget*>(selected->target) : nullptr;
                const std::string* path = stringField(request.payload, "path");
                const EditorValue* value = field(request.payload, "value");
                if (!materialTarget || !path || !value)
                    return authoringError<CommandPlan>(EditorStatus::Rejected,
                                                       "editor.authoring.material-payload",
                                                       "Material property requires a material target, path and value");
                auto operation = materialTarget->makeSet(materialSelection(*materialTarget), PropertyPath(*path),
                                                         *value, PropertySetMode::Absolute);
                if (!operation.accepted() || !operation.value)
                    return authoringError<CommandPlan>(operation.status,
                                                       "editor.authoring.material-operation",
                                                       "Material target rejected the property value");
                CommandPlan plan;
                plan.operations.push_back(std::move(*operation.value));
                plan.summary = EditorValue::Object{{"path", *path}};
                return EditorResult<CommandPlan>::applied(std::move(plan));
            },
            [this](const CommandRequest& request, const CommandPlan& plan) { return execute(request, plan); });

        ready = sceneRegistered.accepted() && materialRegistered.accepted();
    }

    EditorCommandService* commands = nullptr;
    std::map<TargetId, std::unique_ptr<TargetEntry>> targets;
    bool ready = false;
};

EditorAuthoringService::EditorAuthoringService(EditorCommandService& commands)
    : impl_(std::make_unique<Impl>(commands)) {}

EditorAuthoringService::~EditorAuthoringService() = default;

EditorResult<void> EditorAuthoringService::registerTarget(IEditableTargetV2& target) {
    if (!impl_->ready)
        return authoringError<void>(EditorStatus::Failed, "editor.authoring.commands-unavailable",
                                    "Built-in authoring commands could not be registered");
    auto* operations = dynamic_cast<IDomainOperationTarget*>(&target);
    if (!operations)
        return authoringError<void>(EditorStatus::Unsupported, "editor.authoring.operations-unsupported",
                                    "Authoring target does not support domain operations");
    const TargetId id(target.targetId());
    if (id.empty())
        return authoringError<void>(EditorStatus::Rejected, "editor.authoring.empty-target",
                                    "Authoring target id must not be empty");
    if (const auto found = impl_->targets.find(id); found != impl_->targets.end()) {
        if (found->second->target == &target) {
            EditorResult<void> result;
            result.status = EditorStatus::NoOp;
            return result;
        }
        return authoringError<void>(EditorStatus::Conflict, "editor.authoring.duplicate-target",
                                    "Another authoring target already uses id: " + id.value());
    }
    impl_->targets.emplace(id, std::make_unique<Impl::TargetEntry>(target, *operations));
    return EditorResult<void>::applied();
}

EditorResult<void> EditorAuthoringService::unregisterTarget(const TargetId& target) {
    if (impl_->targets.erase(target) != 0) return EditorResult<void>::applied();
    EditorResult<void> result;
    result.status = EditorStatus::NoOp;
    return result;
}

EditorResult<void> EditorAuthoringService::bind(EditorSession& session, const TargetId& target) {
    Impl::TargetEntry* selected = impl_->entry(target);
    if (!selected || !selected->target)
        return authoringError<void>(EditorStatus::NotFound, "editor.authoring.target-not-found",
                                    "Authoring target is not registered: " + target.value());
    session.clearRetainedPlans();
    session.bindTarget(selected->target);
    return EditorResult<void>::applied();
}

EditorResult<EditorValue> EditorAuthoringService::inspect(const TargetId& target) const {
    const Impl::TargetEntry* selected = impl_->entry(target);
    if (!selected || !selected->target)
        return authoringError<EditorValue>(EditorStatus::NotFound, "editor.authoring.target-not-found",
                                           "Authoring target is not registered: " + target.value());
    EditorValue::Object result;
    result["descriptor"] = targetDescriptorValue(selected->target->describe());
    if (const auto* scene = dynamic_cast<const SceneTargetBase*>(selected->target))
        result["snapshot"] = scene->snapshotValue();
    else if (const auto* material = dynamic_cast<const MaterialDocumentTarget*>(selected->target))
        result["snapshot"] = material->snapshotValue();
    else
        result["snapshot"] = EditorValue{};
    return EditorResult<EditorValue>::applied(EditorValue(std::move(result)));
}

EditorResult<TransactionReceipt> EditorAuthoringService::undo(const TargetId& target) {
    Impl::TargetEntry* selected = impl_->entry(target);
    if (!selected)
        return authoringError<TransactionReceipt>(EditorStatus::NotFound, "editor.authoring.target-not-found",
                                                   "Authoring target is not registered: " + target.value());
    return selected->transactions.undo();
}

EditorResult<TransactionReceipt> EditorAuthoringService::redo(const TargetId& target) {
    Impl::TargetEntry* selected = impl_->entry(target);
    if (!selected)
        return authoringError<TransactionReceipt>(EditorStatus::NotFound, "editor.authoring.target-not-found",
                                                   "Authoring target is not registered: " + target.value());
    return selected->transactions.redo();
}

}  // namespace eve::editor
