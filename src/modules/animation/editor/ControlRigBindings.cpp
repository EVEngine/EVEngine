#include "animation/editor/ControlRigBindings.h"
#if defined(EVE_ANIMATION_EDITOR_RUNTIME)
#include <algorithm>
#include "animation/AnimClip.h"
#include "animation/AnimPose.h"
#include "animation/AnimSkeleton.h"
#include "animation/ControlRig.h"
#include "animation/editor/AnimationEditorModule.h"
#include "animation/editor/ControlRigEditor.h"
#include "editor/EditorScriptProjection.h"

namespace eve::animation_editor {
namespace {
Value trsValue(const animation::TransformTRS& t) {
    return Value::object({{"px", t.px},
                          {"py", t.py},
                          {"pz", t.pz},
                          {"qx", t.qx},
                          {"qy", t.qy},
                          {"qz", t.qz},
                          {"qw", t.qw},
                          {"sx", t.sx},
                          {"sy", t.sy},
                          {"sz", t.sz}});
}
template <class F>
Result<void> edit(ControlRigEditor* self, F function) {
    auto value = self->snapshot();
    if (!value.ok()) return Result<void>::failure(value.status());
    auto definition = animation::decodeControlRig(value.value());
    if (!definition.ok()) return Result<void>::failure(definition.status());
    auto changed = function(definition.value());
    if (!changed.ok()) return changed;
    return self->replace(definition.value());
}
Result<void> invalid(std::string message) {
    return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, std::move(message), "controlRig"));
}
}  // namespace
void exposeControlRigBindings(ssq::Table& table, ssq::Class& module) {
    const editor::ScriptBind bind{table.getHandle(), "animation.controlRig"};
    auto                     cls = editor::addScriptClass<ControlRigEditor>(table, "ControlRigEditor");
    module.addFunc("createRig", [bind](AnimationEditorModule*, const std::string& id) {
        return bind.ownedCreate<ControlRigEditor>("rig target ID must not be empty", id);
    });
    cls.addFunc("initialize", [bind](ControlRigEditor* self, animation::AnimSkeleton* skeleton) {
        return bind.checked(self && skeleton, "editor and skeleton required",
                            [&] { return self->initialize(*skeleton); });
    });
    cls.addFunc("getRevision", [](ControlRigEditor* self) { return std::to_string(self->revision()); });
    cls.addFunc("enableAutomation", [bind](ControlRigEditor* self) {
        return bind.checked(self, "editor required", [&] { return self->enableAutomation(); });
    });
    cls.addFunc("disableAutomation", [bind](ControlRigEditor* self) {
        return bind.checked(self, "editor required", [&] { return self->disableAutomation(); });
    });
    cls.addFunc("seek", [bind](ControlRigEditor* self, float time) {
        return bind.checked(self, "editor required", [&] { return self->seek(time); });
    });
    cls.addFunc("undo", [bind](ControlRigEditor* self) {
        return bind.checked(self, "editor required", [&] { return self->undo(); });
    });
    cls.addFunc("redo", [bind](ControlRigEditor* self) {
        return bind.checked(self, "editor required", [&] { return self->redo(); });
    });
    cls.addFunc("copyPose", [bind](ControlRigEditor* self, animation::AnimPose* pose) {
        return bind.checked(self && pose, "editor and pose required", [&] { return self->copyPose(*pose); });
    });
    cls.addFunc("keyFromPose", [bind](ControlRigEditor* self, animation::AnimPose* pose) {
        return bind.checked(self && pose, "editor and pose required", [&] { return self->keyFromPose(*pose); });
    });
    cls.addFunc("bakeInto", [bind](ControlRigEditor* self, animation::AnimClip* clip, float rate) {
        return bind.checked(self && clip, "editor and clip required", [&] { return self->bakeInto(*clip, rate); });
    });
    cls.addFunc("setControl", [bind](ControlRigEditor* self, const std::string& id, float x, float y, float z, float qx,
                                     float qy, float qz, float qw, float scale, bool key) {
        return bind.checked(self, "editor required", [&] {
            animation::TransformTRS t{x, y, z, qx, qy, qz, qw, scale, scale, scale};
            return self->setControl(id, t, key);
        });
    });
    cls.addFunc("deleteKey", [bind](ControlRigEditor* self, const std::string& id) {
        return bind.checked(self, "editor required", [&] { return self->deleteKey(id); });
    });
    cls.addFunc("moveControl", [bind](ControlRigEditor* self, int index, float x, float y, float z, bool key) {
        return bind.checked(self, "editor required", [&] { return self->moveControl(index, x, y, z, key); });
    });
    cls.addFunc("previewMoveControl", [bind](ControlRigEditor* self, int index, float x, float y, float z) {
        return bind.checked(self, "editor required", [&] { return self->previewMoveControl(index, x, y, z); });
    });
    cls.addFunc("reparent", [bind](ControlRigEditor* self, const std::string& id, const std::string& parent) {
        return bind.checked(self, "editor required", [&] { return self->reparent(id, parent); });
    });
    cls.addFunc("getControlWorld", [bind](ControlRigEditor* self, int index) {
        if (!self) return bind.fail(DiagnosticCode::InvalidArgument, "editor required");
        return script::projectResult(bind.vm(), self->controlWorld(index), trsValue);
    });
    cls.addFunc("getControlLocal", [bind](ControlRigEditor* self, int index) {
        if (!self) return bind.fail(DiagnosticCode::InvalidArgument, "editor required");
        return script::projectResult(bind.vm(), self->controlLocal(index), trsValue);
    });
    cls.addFunc("getControlCount", &ControlRigEditor::controlCount);
    cls.addFunc("getDocument", [bind](ControlRigEditor* self) {
        if (!self) return bind.fail(DiagnosticCode::InvalidArgument, "editor required");
        return script::projectResult(bind.vm(), self->snapshot(), [](Value value) { return value; });
    });
    cls.addFunc("getControlId", &ControlRigEditor::controlId);
    cls.addFunc("getTime", [](ControlRigEditor* self) { return static_cast<float>(self->time()); });
    cls.addFunc("getDuration", [](ControlRigEditor* self) { return static_cast<float>(self->duration()); });
    cls.addFunc("exportJson", [bind](ControlRigEditor* self) {
        if (!self) return bind.fail(DiagnosticCode::InvalidArgument, "editor required");
        auto snapshot = self->snapshot();
        if (!snapshot.ok()) return script::projectStatusResult(bind.vm(), snapshot.status());
        return script::projectResult(bind.vm(), snapshot.value().toJson(),
                                     [](std::string value) { return Value(std::move(value)); });
    });
    cls.addFunc("importJson", [bind](ControlRigEditor* self, const std::string& text) {
        return bind.checked(self, "editor required", [&] {
            auto parsed = Value::fromJson(text);
            if (!parsed.ok()) return Result<void>::failure(parsed.status());
            return self->restore(parsed.value());
        });
    });
    cls.addFunc("setLimitsJson", [bind](ControlRigEditor* self, const std::string& id, const std::string& text) {
        return bind.checked(self, "editor required", [&] {
            auto limits = Value::fromJson(text);
            if (!limits.ok()) return Result<void>::failure(limits.status());
            auto document = self->snapshot();
            if (!document.ok()) return Result<void>::failure(document.status());
            for (int i = 0; i < self->controlCount(); ++i) {
                if (self->controlId(i) != id) continue;
                document.value().find("controls")->at(static_cast<size_t>(i)).set("limits", limits.value());
                return self->restore(document.value());
            }
            return invalid("unknown control ID");
        });
    });
    cls.addFunc("addControl", [bind](ControlRigEditor* self, const std::string& id, const std::string& parent, float x,
                                     float y, float z) {
        return bind.checked(self, "editor required", [&] {
            return edit(self, [&](auto& d) {
                animation::RigControl c;
                c.id               = id;
                c.parent           = parent;
                c.initial.px       = x;
                c.initial.py       = y;
                c.initial.pz       = z;
                c.limits.reference = c.initial;
                d.controls.push_back(c);
                return Result<void>::success();
            });
        });
    });
    cls.addFunc("removeControl", [bind](ControlRigEditor* self, const std::string& id) {
        return bind.checked(self, "editor required", [&] {
            return edit(self, [&](auto& d) {
                auto found =
                    std::find_if(d.controls.begin(), d.controls.end(), [&](const auto& c) { return c.id == id; });
                if (found == d.controls.end()) return invalid("unknown control");
                d.controls.erase(found);
                return Result<void>::success();
            });
        });
    });
    cls.addFunc("setNode", [bind](ControlRigEditor* self, const std::string& id, const std::string& kind,
                                  const std::string& control, const std::string& bone, const std::string& middle,
                                  const std::string& tip, const std::string& after, float weight) {
        return bind.checked(self, "editor required", [&] {
            return edit(self, [&](auto& d) {
                animation::RigNode n;
                n.id      = id;
                n.control = control;
                n.bone    = bone;
                n.middle  = middle;
                n.tip     = tip;
                n.weight  = weight;
                if (!after.empty()) n.after.push_back(after);
                if (kind == "fk")
                    n.operation = animation::RigOperation::FK;
                else if (kind == "aim")
                    n.operation = animation::RigOperation::Aim;
                else if (kind == "twoBoneIK")
                    n.operation = animation::RigOperation::TwoBoneIK;
                else
                    return invalid("unknown node operation");
                auto found =
                    std::find_if(d.nodes.begin(), d.nodes.end(), [&](const auto& node) { return node.id == id; });
                if (found == d.nodes.end())
                    d.nodes.push_back(n);
                else
                    *found = n;
                return Result<void>::success();
            });
        });
    });
    cls.addFunc("removeNode", [bind](ControlRigEditor* self, const std::string& id) {
        return bind.checked(self, "editor required", [&] {
            return edit(self, [&](auto& d) {
                auto found = std::find_if(d.nodes.begin(), d.nodes.end(), [&](const auto& n) { return n.id == id; });
                if (found == d.nodes.end()) return invalid("unknown node");
                d.nodes.erase(found);
                return Result<void>::success();
            });
        });
    });
}
}  // namespace eve::animation_editor
#else
namespace eve::animation_editor {
void exposeControlRigBindings(ssq::Table&, ssq::Class&) {}
}  // namespace eve::animation_editor
#endif
