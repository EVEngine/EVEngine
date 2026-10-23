#include "animation/editor/ControlRigAutomation.h"
#if defined(EVE_ANIMATION_EDITOR_RUNTIME)
#include <algorithm>
#include "animation/editor/ControlRigEditorInternal.h"
#include "common/Assert.h"
#include "common/Capability.h"
#include "common/Value.h"
#include "editing/EditingValueJson.h"

namespace eve::animation_editor {
namespace {
template <class T>
Result<T> invalid(std::string text) {
    return Result<T>::failure(
        Diagnostic::error(DiagnosticCode::InvalidArgument, std::move(text), "controlRig.automation"));
}
}  // namespace
ControlRigEditor::Impl::~Impl() {
    if (!hostLifetime.expired()) {
        auto removed = host->unregisterSharedHistoryTarget(target, transactions);
        if (!removed.ok()) {
            EV_ASSERT(false, "Rig target teardown failed");
        }
    }
}
Result<void> ControlRigEditor::Impl::validate(const animation::RigDefinition& definition) const {
    if (!skeleton) return invalid<void>("initialize skeleton first");
    auto compiled = animation::ControlRig::compile(definition, *skeleton);
    if (!compiled.ok()) return Result<void>::failure(compiled.status());
    animation::AnimPose preview;
    return compiled.value()->evaluate(std::min(playhead, definition.duration), input, preview);
}
Result<void> ControlRigEditor::Impl::rebuild() {
    if (!skeleton) return invalid<void>("initialize skeleton first");
    auto compiled = animation::ControlRig::compile(document.definition(), *skeleton);
    if (!compiled.ok()) return Result<void>::failure(compiled.status());
    const double time      = std::min(playhead, document.definition().duration);
    auto         evaluated = compiled.value()->evaluate(time, input, pose);
    if (!evaluated.ok()) return evaluated;
    rig      = std::move(compiled).takeValue();
    playhead = time;
    return Result<void>::success();
}
editing::Value ControlRigEditor::Impl::LiveTarget::snapshotValue() const {
    auto value = animation::encodeControlRig(owner.document.definition());
    EV_ASSERT(value.ok(), "Authoritative rig must encode");
    auto text = value.value().toJson();
    EV_ASSERT(text.ok(), "Authoritative rig must serialize");
    auto result = editing::editorValueFromJson(text.value());
    EV_ASSERT(result.ok(), "Authoritative rig must project");
    return std::move(result).takeValue();
}
Result<editing::DomainOperation> ControlRigEditor::Impl::LiveTarget::prepare(const editing::Value& payload) const {
    if (!payload.isWithinLimits(32, 2000000, 16000000))
        return invalid<editing::DomainOperation>("Rig payload exceeds limits");
    auto value = Value::fromJson(editing::editorValueToJson(payload));
    if (!value.ok()) return Result<editing::DomainOperation>::failure(value.status());
    auto decoded = animation::decodeControlRig(value.value());
    if (!decoded.ok()) return Result<editing::DomainOperation>::failure(decoded.status());
    auto valid = owner.validate(decoded.value());
    if (!valid.ok()) return Result<editing::DomainOperation>::failure(valid.status());
    return owner.document.replace(decoded.value());
}
Result<void> ControlRigEditor::Impl::LiveTarget::applyDomainOperation(const editing::DomainOperation& operation) {
    auto candidate = cloneDomainState();
    auto applied   = candidate->applyDomainOperation(operation);
    if (!applied.ok()) return applied;
    return commitDomainState(std::move(candidate));
}
Result<void> ControlRigEditor::Impl::LiveTarget::commitDomainState(
    std::unique_ptr<editing::IDomainOperationTarget> candidate) {
    auto* document = dynamic_cast<animation_editing::ControlRigDocument*>(candidate.get());
    if (!document || document->targetId() != targetId()) return invalid<void>("Wrong Rig candidate");
    if (!owner.skeleton) return invalid<void>("initialize skeleton first");
    auto compiled = animation::ControlRig::compile(document->definition(), *owner.skeleton);
    if (!compiled.ok()) return Result<void>::failure(compiled.status());
    animation::AnimPose preview;
    const double        time      = std::min(owner.playhead, document->definition().duration);
    auto                evaluated = compiled.value()->evaluate(time, owner.input, preview);
    if (!evaluated.ok()) return evaluated;
    auto committed = owner.document.commitDomainState(std::move(candidate));
    if (!committed.ok()) return committed;
    owner.rig      = std::move(compiled).takeValue();
    owner.pose     = std::move(preview);
    owner.playhead = time;
    return Result<void>::success();
}
Result<void> ControlRigEditor::enableAutomation() {
    if (!impl_->skeleton) return invalid<void>("initialize skeleton first");
    if (!impl_->hostLifetime.expired())
        return impl_->host->registerSharedHistoryTarget(impl_->target, impl_->transactions);
    auto* host = dynamic_cast<editor::EditorTargetCoordinator*>(cap::query<editing::IEditingCommandRegistry>());
    if (!host)
        return editing::failed<void>(editing::EditorStatus::Unsupported, editing::RuleId("controlRig.host.absent"),
                                     "Active editor host required");
    auto result = host->registerSharedHistoryTarget(impl_->target, impl_->transactions);
    if (!result.ok()) return result;
    impl_->host         = host;
    impl_->hostLifetime = host->lifetime();
    return Result<void>::success();
}
Result<void> ControlRigEditor::disableAutomation() {
    if (!impl_->hostLifetime.expired()) {
        auto result = impl_->host->unregisterSharedHistoryTarget(impl_->target, impl_->transactions);
        if (!result.ok()) return result;
    }
    impl_->hostLifetime.reset();
    impl_->host = nullptr;
    return Result<void>::success();
}
Result<void> registerControlRigAutomation(editing::IEditingCommandRegistry& registry) {
    editing::EditingCommandDescriptor descriptor;
    descriptor.id          = editing::CommandId("animation.rig.replace.v1");
    descriptor.ownerModule = "animation_editing";
    descriptor.displayName = "Replace Control Rig document (one undo step)";
    descriptor.category    = "Animation / Control Rig";
    auto schema            = editing::editorValueFromJson(
        R"schema({"type":"object","properties":{"schema":{"const":"eve.control-rig"},"version":{"type":"integer","enum":[1,2]},"duration":{"type":"number","exclusiveMinimum":0,"maximum":36000},"controls":{"type":"array","items":{"type":"object","properties":{"id":{"type":"string","minLength":1},"parent":{"type":"string"},"initial":{"type":"array","items":{"type":"number"},"minItems":10,"maxItems":10,"description":"Local TRS: px,py,pz,qx,qy,qz,qw,sx,sy,sz. Quaternion unit length; positive uniform scale."},"shapeSize":{"type":"number","exclusiveMinimum":0},"keys":{"type":"array","items":{"type":"object","properties":{"time":{"type":"number","minimum":0},"transform":{"type":"array","items":{"type":"number"},"minItems":10,"maxItems":10,"description":"Local TRS: px,py,pz,qx,qy,qz,qw,sx,sy,sz. Quaternion unit length; positive uniform scale."}},"required":["time","transform"],"additionalProperties":false},"maxItems":100000},"limits":{"type":"object","description":"Parent-local reference-relative constraints. Invalid configuration rejects atomically. Valid out-of-range TRS and keys are projected on commit, sample and solve. Rotation q=reference*(qz*qy*qx), degrees X/Z [-180,180], Y [-90,90]. Limited bounds include neutral 0 (translation/rotation) or 1 (scale). Extension follows reference position direction and requires free translation axes.","properties":{"reference":{"type":"array","items":{"type":"number"},"minItems":10,"maxItems":10,"description":"Local TRS: px,py,pz,qx,qy,qz,qw,sx,sy,sz. Quaternion unit length; positive uniform scale."},"translation":{"type":"array","minItems":3,"maxItems":3,"items":{"type":"object","properties":{"mode":{"type":"string","enum":["free","locked","limited"]},"min":{"type":"number"},"max":{"type":"number"}},"required":["mode","min","max"],"additionalProperties":false}},"rotation":{"type":"array","minItems":3,"maxItems":3,"items":{"type":"object","properties":{"mode":{"type":"string","enum":["free","locked","limited"]},"min":{"type":"number"},"max":{"type":"number"}},"required":["mode","min","max"],"additionalProperties":false}},"scale":{"type":"object","properties":{"mode":{"type":"string","enum":["free","locked","limited"]},"min":{"type":"number"},"max":{"type":"number"}},"required":["mode","min","max"],"additionalProperties":false},"extension":{"type":"boolean"},"minLength":{"type":"number","exclusiveMinimum":0,"maximum":1},"maxLength":{"type":"number","minimum":1}},"required":["reference","translation","rotation","scale","extension","minLength","maxLength"],"additionalProperties":false}},"required":["id","parent","initial","shapeSize","keys"],"additionalProperties":false},"maxItems":4096},"nodes":{"type":"array","items":{"type":"object","properties":{"id":{"type":"string","minLength":1},"control":{"type":"string"},"bone":{"type":"string"},"middle":{"type":"string"},"tip":{"type":"string"},"operation":{"type":"string","enum":["fk","aim","twoBoneIK"]},"weight":{"type":"number","minimum":0,"maximum":1},"after":{"type":"array","items":{"type":"string"}}},"required":["id","control","bone","middle","tip","operation","weight","after"],"additionalProperties":false},"maxItems":4096}},"required":["schema","version","duration","controls","nodes"],"additionalProperties":false,"allOf":[{"if":{"properties":{"version":{"const":2}}},"then":{"properties":{"controls":{"items":{"required":["limits"]}}}},"else":{"properties":{"controls":{"items":{"not":{"required":["limits"]}}}}}}]})schema");
    if (!schema.ok()) return Result<void>::failure(schema.status());
    descriptor.payloadSchema.typeId = "eve.control-rig";
    descriptor.payloadSchema.version = 2;
    descriptor.payloadSchema.documentSchema = std::move(schema).takeValue();
    descriptor.targetKinds = {"control-rig"};
    descriptor.panelCapability = "eve.editor.animation.control-rig";
    descriptor.panelContext = "inspector";
    return registry.registerPlannedCommand(
        std::move(descriptor),
        [](editing::IEditableTarget& target, const editing::CommandRequest& request) -> Result<editing::CommandPlan> {
            auto* rig = dynamic_cast<RigCommandTarget*>(&target);
            if (!rig) return invalid<editing::CommandPlan>("Command requires a live Control Rig target");
            auto operation = rig->prepare(request.payload);
            if (!operation.ok()) return Result<editing::CommandPlan>::failure(operation.status());
            editing::CommandPlan plan;
            plan.operations.push_back(std::move(operation).takeValue());
            plan.summary =
                editing::Value::Object{{"target", target.targetId().value()}, {"operation", "replace-control-rig"}};
            return Result<editing::CommandPlan>::success(std::move(plan));
        });
}
}  // namespace eve::animation_editor
#else
namespace eve::animation_editor {
Result<void> registerControlRigAutomation(editing::IEditingCommandRegistry&) { return Result<void>::success(); }
}  // namespace eve::animation_editor
#endif
