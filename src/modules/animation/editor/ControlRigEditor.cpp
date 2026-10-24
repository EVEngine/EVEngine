#if defined(EVE_ANIMATION_EDITOR_RUNTIME)
#include <algorithm>
#include <cmath>
#include "animation/AnimClip.h"
#include "animation/AnimPose.h"
#include "animation/AnimSkeleton.h"
#include "animation/ControlRig.h"
#include "animation/editing/ControlRigDocument.h"
#include "animation/editor/ControlRigEditorInternal.h"
#include "common/Value.h"
#include "editor/EditorAuthority.h"
#include "editor/EditorTransactionService.h"

namespace eve::animation_editor {
namespace {
template <class T>
Result<T> failure(std::string message) {
    return Result<T>::failure(
        Diagnostic::error(DiagnosticCode::InvalidArgument, std::move(message), "controlRig.editor"));
}
}  // namespace
ControlRigEditor::ControlRigEditor(std::string id) : impl_(std::make_unique<Impl>(std::move(id))) {}
ControlRigEditor::~ControlRigEditor() = default;

Result<void> ControlRigEditor::initialize(const animation::AnimSkeleton& source) {
    auto candidate      = std::make_unique<Impl>(impl_->document.targetId().value());
    candidate->skeleton = std::make_unique<animation::AnimSkeleton>();
    animation::RigDefinition definition;
    for (int i = 0; i < source.getBoneCount(); ++i) {
        const auto  name   = source.getBoneName(i);
        const auto& t      = source.bindLocal(i);
        const int   parent = source.getParent(i);
        const int   bone   = candidate->skeleton->addBone(name, parent);
        candidate->skeleton->setBindPosition(bone, t.px, t.py, t.pz);
        candidate->skeleton->setBindRotation(bone, t.qx, t.qy, t.qz, t.qw);
        candidate->skeleton->setBindScale(bone, t.sx, t.sy, t.sz);
        animation::RigControl control;
        control.id               = name;
        control.parent           = parent < 0 ? "" : source.getBoneName(parent);
        control.initial          = t;
        control.limits.reference = t;
        definition.controls.push_back(std::move(control));
        animation::RigNode node;
        node.id      = "fk:" + name;
        node.control = name;
        node.bone    = name;
        if (parent >= 0) node.after.push_back("fk:" + source.getBoneName(parent));
        definition.nodes.push_back(std::move(node));
    }
    auto op = candidate->document.replace(definition);
    if (!op.ok()) return Result<void>::failure(op.status());
    auto applied = candidate->document.applyDomainOperation(op.value());
    if (!applied.ok()) return applied;
    candidate->skeleton->applyBindPose(&candidate->input);
    auto rebuilt = candidate->rebuild();
    if (!rebuilt.ok()) return rebuilt;
    if (!impl_->hostLifetime.expired()) return failure<void>("disable automation before reinitializing skeleton");
    impl_ = std::move(candidate);
    return Result<void>::success();
}

Result<void> ControlRigEditor::replace(const animation::RigDefinition& definition) {
    if (!impl_->skeleton) return failure<void>("initialize a skeleton first");
    auto compiled = animation::ControlRig::compile(definition, *impl_->skeleton);
    if (!compiled.ok()) return Result<void>::failure(compiled.status());
    animation::AnimPose preview;
    const double        time      = std::min(impl_->playhead, definition.duration);
    auto                evaluated = compiled.value()->evaluate(time, impl_->input, preview);
    if (!evaluated.ok()) return evaluated;
    auto op = impl_->document.replace(definition);
    if (!op.ok()) return Result<void>::failure(op.status());
    editor::TransactionSpec spec;
    spec.id           = editor::TransactionId("control-rig:" + std::to_string(++impl_->sequence));
    spec.target       = impl_->document.targetId();
    spec.baseRevision = impl_->document.revision();
    spec.label        = "Edit control rig";
    auto begun        = impl_->transactions.begin(std::move(spec));
    if (!begun.ok()) return Result<void>::failure(begun.status());
    auto appended = impl_->transactions.append(std::move(op).takeValue());
    if (!appended.ok()) {
        auto discarded = impl_->transactions.discard();
        if (!discarded.ok()) return discarded;
        return appended;
    }
    auto committed = impl_->transactions.commit();
    if (!committed.ok()) {
        auto discarded = impl_->transactions.discard();
        if (!discarded.ok()) return discarded;
        return Result<void>::failure(committed.status());
    }
    impl_->rig      = std::move(compiled).takeValue();
    impl_->pose     = std::move(preview);
    impl_->playhead = time;
    return Result<void>::success();
}
Result<Value> ControlRigEditor::snapshot() const { return animation::encodeControlRig(impl_->document.definition()); }
Result<void>  ControlRigEditor::restore(const Value& value) {
    auto decoded = animation::decodeControlRig(value);
    if (!decoded.ok()) return Result<void>::failure(decoded.status());
    return replace(decoded.value());
}
Result<void> ControlRigEditor::seek(double seconds) {
    if (!impl_->rig || !std::isfinite(seconds)) return failure<void>("invalid time or uninitialized rig");
    const double time      = std::clamp(seconds, 0.0, duration());
    auto         evaluated = impl_->rig->evaluate(time, impl_->input, impl_->pose);
    if (!evaluated.ok()) return evaluated;
    impl_->playhead = time;
    return Result<void>::success();
}
Result<void> ControlRigEditor::setControl(std::string_view id, const animation::TransformTRS& transform, bool key) {
    auto definition = impl_->document.definition();
    for (auto& control : definition.controls)
        if (control.id == id) {
            if (!key) {
                if (!control.keys.empty()) return failure<void>("animated control requires a key edit");
                control.initial = transform;
            } else {
                auto found = std::lower_bound(control.keys.begin(), control.keys.end(), impl_->playhead,
                                              [](const animation::RigKey& k, double t) { return k.time < t; });
                if (found != control.keys.end() && found->time == impl_->playhead)
                    found->transform = transform;
                else
                    control.keys.insert(found, {impl_->playhead, transform});
            }
            return replace(definition);
        }
    return failure<void>("unknown control ID");
}
Result<void> ControlRigEditor::deleteKey(std::string_view id) {
    auto definition = impl_->document.definition();
    for (auto& control : definition.controls)
        if (control.id == id) {
            auto found = std::find_if(control.keys.begin(), control.keys.end(),
                                      [&](const auto& key) { return key.time == impl_->playhead; });
            if (found == control.keys.end()) return failure<void>("no key at playhead");
            control.keys.erase(found);
            return replace(definition);
        }
    return failure<void>("unknown control ID");
}
Result<void> ControlRigEditor::moveControl(int index, float x, float y, float z, bool key) {
    auto world = controlWorld(index);
    if (!world.ok()) return Result<void>::failure(world.status());
    world.value().px = x;
    world.value().py = y;
    world.value().pz = z;
    auto local       = impl_->rig->localFromWorld(index, world.value(), impl_->playhead);
    if (!local.ok()) return Result<void>::failure(local.status());
    return setControl(controlId(index), local.value(), key);
}
Result<void> ControlRigEditor::previewMoveControl(int index, float x, float y, float z) {
    auto world = controlWorld(index);
    if (!world.ok()) return Result<void>::failure(world.status());
    world.value().px = x;
    world.value().py = y;
    world.value().pz = z;
    auto local       = impl_->rig->localFromWorld(index, world.value(), impl_->playhead);
    if (!local.ok()) return Result<void>::failure(local.status());
    auto controls = impl_->rig->sampleControls(impl_->playhead);
    if (!controls.ok()) return Result<void>::failure(controls.status());
    controls.value()[static_cast<size_t>(index)] = local.value();
    return impl_->rig->evaluate(impl_->playhead, impl_->input, impl_->pose, controls.value());
}
Result<void> ControlRigEditor::reparent(std::string_view id, std::string_view parent) {
    auto definition = impl_->document.definition();
    auto moved      = animation::reparentRigControl(definition, id, parent);
    if (!moved.ok()) return moved;
    return replace(definition);
}
Result<void> ControlRigEditor::undo() {
    auto result = impl_->transactions.undo();
    if (!result.ok()) return Result<void>::failure(result.status());
    auto rebuilt = impl_->rebuild();
    if (!rebuilt.ok()) {
        auto compensated = impl_->transactions.redo();
        if (!compensated.ok()) return Result<void>::failure(compensated.status());
    }
    return rebuilt;
}
Result<void> ControlRigEditor::redo() {
    auto result = impl_->transactions.redo();
    if (!result.ok()) return Result<void>::failure(result.status());
    auto rebuilt = impl_->rebuild();
    if (!rebuilt.ok()) {
        auto compensated = impl_->transactions.undo();
        if (!compensated.ok()) return Result<void>::failure(compensated.status());
    }
    return rebuilt;
}
Result<void> ControlRigEditor::bakeInto(animation::AnimClip& clip, double rate) const {
    if (!impl_->rig) return failure<void>("initialize a skeleton first");
    auto baked = impl_->rig->bake(rate, impl_->input);
    if (!baked.ok()) return Result<void>::failure(baked.status());
    clip.adopt(*baked.value());
    return Result<void>::success();
}
Result<void> ControlRigEditor::copyPose(animation::AnimPose& output) const {
    if (!impl_->rig) return failure<void>("initialize a skeleton first");
    output.copyFrom(&impl_->pose);
    return Result<void>::success();
}
Result<void> ControlRigEditor::keyFromPose(const animation::AnimPose& pose) {
    if (!impl_->rig) return failure<void>("initialize a skeleton first");
    auto matched = impl_->rig->matchFK(pose);
    if (!matched.ok()) return Result<void>::failure(matched.status());
    auto definition = impl_->document.definition();
    for (size_t i = 0; i < definition.controls.size(); ++i) {
        auto& keys  = definition.controls[i].keys;
        auto  found = std::lower_bound(keys.begin(), keys.end(), impl_->playhead,
                                       [](const animation::RigKey& k, double t) { return k.time < t; });
        if (found != keys.end() && found->time == impl_->playhead)
            found->transform = matched.value()[i];
        else
            keys.insert(found, {impl_->playhead, matched.value()[i]});
    }
    return replace(definition);
}
Result<animation::TransformTRS> ControlRigEditor::controlWorld(int index) const {
    if (!impl_->rig || index < 0 || index >= controlCount())
        return failure<animation::TransformTRS>("invalid control index");
    auto locals = impl_->rig->sampleControls(impl_->playhead);
    if (!locals.ok()) return Result<animation::TransformTRS>::failure(locals.status());
    auto worlds = impl_->rig->controlWorlds(locals.value());
    if (!worlds.ok()) return Result<animation::TransformTRS>::failure(worlds.status());
    return Result<animation::TransformTRS>::success(worlds.value()[static_cast<size_t>(index)]);
}
Result<animation::TransformTRS> ControlRigEditor::controlLocal(int index) const {
    if (!impl_->rig || index < 0 || index >= controlCount())
        return failure<animation::TransformTRS>("invalid control index");
    auto locals = impl_->rig->sampleControls(impl_->playhead);
    if (!locals.ok()) return Result<animation::TransformTRS>::failure(locals.status());
    return Result<animation::TransformTRS>::success(locals.value()[static_cast<size_t>(index)]);
}
int ControlRigEditor::controlCount() const { return static_cast<int>(impl_->document.definition().controls.size()); }
std::string ControlRigEditor::controlId(int index) const {
    const auto d = impl_->document.definition();
    return index < 0 || index >= static_cast<int>(d.controls.size()) ? "" : d.controls[static_cast<size_t>(index)].id;
}
std::uint64_t ControlRigEditor::revision() const { return impl_->document.revision(); }
double        ControlRigEditor::time() const { return impl_->playhead; }
double        ControlRigEditor::duration() const { return impl_->document.definition().duration; }
}  // namespace eve::animation_editor
#endif
