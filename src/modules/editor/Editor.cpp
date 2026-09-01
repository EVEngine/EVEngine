#include "editor/Editor.h"

#include "common/Capability.h"
#include "common/EditorAutomation.h"
#include "editor/EditorAutomationProvider.h"
#include "editor/EditorDock.h"
#include "editor/EditorInspector.h"
#include "editor/EditorSession.h"
#include "editor/EditorTargetCoordinator.h"
#include "editor/EditorToolbar.h"
#include "editor/EditorWorkspace.h"
#include "editor/FieldBrushTool.h"
#include "editor/GizmoManager.h"
#include "editor/ScriptEditorTool.h"
#include "editor/TransformGizmo.h"
#include "editor/VolumeBrushTool.h"


#include <simplesquirrel/simplesquirrel.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace eve::editor {

Module_IMPL(Editor, new Editor());

namespace {

const char* statusName(EditorStatus status) {
    switch (status) {
        case EditorStatus::Applied: return "applied";
        case EditorStatus::Pending: return "pending";
        case EditorStatus::NoOp: return "no-op";
        case EditorStatus::Rejected: return "rejected";
        case EditorStatus::Conflict: return "conflict";
        case EditorStatus::NotFound: return "not-found";
        case EditorStatus::Unsupported: return "unsupported";
        case EditorStatus::Cancelled: return "cancelled";
        case EditorStatus::Failed: return "failed";
    }
    return "failed";
}

const char* transactionStateName(TransactionState state) {
    switch (state) {
        case TransactionState::Planning: return "planning";
        case TransactionState::Previewing: return "previewing";
        case TransactionState::PendingAuthority: return "pending-authority";
        case TransactionState::Committed: return "committed";
        case TransactionState::RolledBack: return "rolled-back";
        case TransactionState::Rejected: return "rejected";
        case TransactionState::Conflicted: return "conflicted";
        case TransactionState::Failed: return "failed";
    }
    return "failed";
}

bool squirrelToEditorValue(HSQUIRRELVM vm, SQInteger index, EditorValue& out, size_t depth = 0) {
    if (!vm || depth > 32) return false;
    const SQInteger absolute = index > 0 ? index : sq_gettop(vm) + index + 1;
    switch (sq_gettype(vm, absolute)) {
        case OT_NULL: out = EditorValue{}; return true;
        case OT_BOOL: {
            SQBool value = SQFalse;
            if (SQ_FAILED(sq_getbool(vm, absolute, &value))) return false;
            out = EditorValue(value != SQFalse);
            return true;
        }
        case OT_INTEGER: {
            SQInteger value = 0;
            if (SQ_FAILED(sq_getinteger(vm, absolute, &value))) return false;
            out = EditorValue(static_cast<int64_t>(value));
            return true;
        }
        case OT_FLOAT: {
            SQFloat value = 0;
            if (SQ_FAILED(sq_getfloat(vm, absolute, &value))) return false;
            out = EditorValue(static_cast<double>(value));
            return true;
        }
        case OT_STRING: {
            const SQChar* value = nullptr;
            if (SQ_FAILED(sq_getstring(vm, absolute, &value))) return false;
            out = EditorValue(value ? value : "");
            return true;
        }
        case OT_ARRAY: {
            EditorValue::Array values;
            const SQInteger    count = sq_getsize(vm, absolute);
            values.reserve(static_cast<size_t>(count));
            for (SQInteger i = 0; i < count; ++i) {
                sq_pushinteger(vm, i);
                if (SQ_FAILED(sq_get(vm, absolute))) return false;
                EditorValue value;
                const bool  ok = squirrelToEditorValue(vm, -1, value, depth + 1);
                sq_pop(vm, 1);
                if (!ok) return false;
                values.push_back(std::move(value));
            }
            out = EditorValue(std::move(values));
            return true;
        }
        case OT_TABLE: {
            EditorValue::Object values;
            sq_pushnull(vm);
            while (SQ_SUCCEEDED(sq_next(vm, absolute))) {
                const SQChar* key = nullptr;
                const bool keyOk  = sq_gettype(vm, -2) == OT_STRING && SQ_SUCCEEDED(sq_getstring(vm, -2, &key)) && key;
                const std::string stableKey = keyOk ? key : "";
                EditorValue       value;
                const bool        valueOk = keyOk && squirrelToEditorValue(vm, -1, value, depth + 1);
                sq_pop(vm, 2);
                if (!valueOk) {
                    sq_pop(vm, 1);
                    return false;
                }
                values[stableKey] = std::move(value);
            }
            sq_pop(vm, 1);
            out = EditorValue(std::move(values));
            return true;
        }
        default: return false;
    }
}

bool objectToEditorValue(const ssq::Object& object, EditorValue& out) {
    HSQUIRRELVM vm = object.getHandle();
    if (!vm) return false;
    const SQInteger top = sq_gettop(vm);
    sq_pushobject(vm, object.getRaw());
    const bool ok = squirrelToEditorValue(vm, -1, out);
    sq_settop(vm, top);
    return ok;
}

void pushEditorValue(HSQUIRRELVM vm, const EditorValue& value) {
    std::visit(
        [&](const auto& current) {
            using T = std::decay_t<decltype(current)>;
            if constexpr (std::is_same_v<T, std::monostate>) {
                sq_pushnull(vm);
            } else if constexpr (std::is_same_v<T, bool>) {
                sq_pushbool(vm, current ? SQTrue : SQFalse);
            } else if constexpr (std::is_same_v<T, int64_t>) {
                sq_pushinteger(vm, static_cast<SQInteger>(current));
            } else if constexpr (std::is_same_v<T, double>) {
                sq_pushfloat(vm, static_cast<SQFloat>(current));
            } else if constexpr (std::is_same_v<T, std::string>) {
                sq_pushstring(vm, current.c_str(), static_cast<SQInteger>(current.size()));
            } else if constexpr (std::is_same_v<T, EditorValue::Array>) {
                sq_newarray(vm, 0);
                for (const EditorValue& entry : current) {
                    pushEditorValue(vm, entry);
                    sq_arrayappend(vm, -2);
                }
            } else if constexpr (std::is_same_v<T, EditorValue::Object>) {
                sq_newtable(vm);
                for (const auto& [key, entry] : current) {
                    sq_pushstring(vm, key.c_str(), static_cast<SQInteger>(key.size()));
                    pushEditorValue(vm, entry);
                    sq_newslot(vm, -3, SQFalse);
                }
            }
        },
        value.storage());
}

void setValue(ssq::Table& table, const char* name, const EditorValue& value) {
    HSQUIRRELVM vm = table.getHandle();
    sq_pushobject(vm, table.getRaw());
    sq_pushstring(vm, name, -1);
    pushEditorValue(vm, value);
    sq_newslot(vm, -3, SQFalse);
    sq_pop(vm, 1);
}

ssq::Array diagnosticArray(HSQUIRRELVM vm, const std::vector<EditorDiagnostic>& diagnostics) {
    ssq::Array out(vm);
    for (const EditorDiagnostic& diagnostic : diagnostics) {
        ssq::Table item(vm);
        item.set("rule", editing::diagnosticRule(diagnostic).value());
        item.set("message", diagnostic.message());
        item.set("severity", static_cast<int>(diagnostic.severity()));
        out.push(item);
    }
    return out;
}

template <class T>
ssq::Table resultTable(HSQUIRRELVM vm, const EditorResult<T>& result) {
    ssq::Table out(vm);
    out.set("status", std::string(statusName(result.code())));
    out.set("accepted", result.ok());
    out.set("diagnostics", diagnosticArray(vm, result.diagnostics()));
    return out;
}

EditorResult<EditorValue> invalidScriptPayload() {
    return eve::editing::failed<EditorValue>(
        EditorStatus::Rejected, RuleId("editor.script.invalid-payload"),
        "Script payload must contain only null, bool, number, string, array, or table values");
}

bool invokeScriptCommand(const ssq::Object& callback, const EditorValue& payload) {
    const SQObjectType type = callback.getRaw()._type;
    if (type != OT_CLOSURE && type != OT_NATIVECLOSURE) return false;
    HSQUIRRELVM vm  = callback.getHandle();
    SQInteger   top = sq_gettop(vm);
    sq_pushobject(vm, callback.getRaw());
    sq_pushroottable(vm);
    pushEditorValue(vm, payload);
    bool accepted = false;
    if (SQ_SUCCEEDED(sq_call(vm, 2, SQTrue, SQTrue))) {
        SQBool result = SQFalse;
        accepted      = SQ_SUCCEEDED(sq_getbool(vm, -1, &result)) && result != SQFalse;
    }
    sq_settop(vm, top);
    return accepted;
}

bool registerScriptCommand(Editor* editor, const std::string& id, const std::string& displayName,
                           const std::string& category, ssq::Object callback) {
    if (!editor || id.empty()) return false;
    const SQObjectType callbackType = callback.getRaw()._type;
    if (callbackType != OT_CLOSURE && callbackType != OT_NATIVECLOSURE) return false;

    CommandDescriptor descriptor;
    descriptor.id                = CommandId(id);
    descriptor.ownerModule       = "script:" + id;
    descriptor.displayName       = displayName;
    descriptor.category          = category;
    descriptor.automationAllowed = false;
    return editor->commandService()
        .registerPlannedCommand(
            descriptor,
            [id](const CommandRequest& request) {
                CommandPlan plan;
                plan.summary = EditorValue::Object{{"command", EditorValue(id)}, {"payload", request.payload}};
                DomainOperation operation;
                operation.type    = id;
                operation.payload = request.payload;
                plan.operations.push_back(std::move(operation));
                return eve::editing::applied<CommandPlan>(std::move(plan));
            },
            [callback = std::move(callback)](const CommandRequest& request, const CommandPlan& plan) {
                if (!invokeScriptCommand(callback, request.payload))
                    return eve::editing::failed<TransactionReceipt>(
                        EditorStatus::Rejected, RuleId("editor.script.command-rejected"),
                        "Script command callback rejected the planned payload");
                TransactionReceipt receipt;
                receipt.id               = TransactionId(plan.id.value());
                receipt.state            = TransactionState::Committed;
                receipt.beforeRevision   = plan.baseRevision;
                receipt.afterRevision    = plan.baseRevision + 1;
                receipt.authorityReceipt = "script:local";
                return eve::editing::applied<TransactionReceipt>(std::move(receipt));
            },
            true)
        .ok();
}

}  // namespace

Editor::Editor()
    : targets_(std::make_unique<EditorTargetCoordinator>(commandService_)),
      automation_(std::make_unique<EditorAutomationProvider>(commandService_, *targets_)) {
    eve::cap::provide<eve::editing::IEditingCommandRegistry>(targets_.get());
    eve::cap::provide<eve::IEditorAutomation>(automation_.get());
}

Editor::~Editor() {
    eve::cap::revoke<eve::IEditorAutomation>(automation_.get());
    eve::cap::revoke<eve::editing::IEditingCommandRegistry>(targets_.get());
}

EditorResult<void> Editor::registerEditingTarget(IEditableTarget& target) { return targets_->registerTarget(target); }

EditorResult<void> Editor::unregisterEditingTarget(const TargetId& target) {
    auto result = targets_->unregisterTarget(target);
    if (result.code() == EditorStatus::Applied) automation_->targetUnregistered(target);
    return result;
}

EditorResult<void> Editor::bindEditingTarget(EditorSession& session, const TargetId& target) {
    return targets_->bind(session, target);
}

std::unique_ptr<TransformGizmo> Editor::newGizmo() { return std::make_unique<TransformGizmo>(); }

std::unique_ptr<GizmoManager> Editor::newGizmoManager() { return std::make_unique<GizmoManager>(); }

std::unique_ptr<EditorToolbar> Editor::newToolbar() { return std::make_unique<EditorToolbar>(); }

std::unique_ptr<EditorInspector> Editor::newInspector() { return std::make_unique<EditorInspector>(); }

std::unique_ptr<EditorDock> Editor::newDock() { return std::make_unique<EditorDock>(); }

std::unique_ptr<EditorSession> Editor::newSession() {
    auto session = std::make_unique<EditorSession>();
    session->setCommandService(&commandService_);
    return session;
}

EditorResult<std::unique_ptr<EditorWorkspace>> Editor::newWorkspace(const std::string& id, const std::string& title) {
    if (id.empty())
        return eve::editing::failed<std::unique_ptr<EditorWorkspace>>(
            EditorStatus::Rejected, RuleId("editor.workspace.empty-id"), "Workspace id must be non-empty");
    return eve::editing::applied<std::unique_ptr<EditorWorkspace>>(
        std::make_unique<EditorWorkspace>(id, title.empty() ? id : title));
}

std::unique_ptr<ScriptEditorTool> Editor::newScriptTool(const std::string& id, const std::string& label) {
    return std::make_unique<ScriptEditorTool>(id, label);
}

std::unique_ptr<ConstantBrushFalloff> Editor::newConstantBrushFalloff() {
    return std::make_unique<ConstantBrushFalloff>();
}
std::unique_ptr<LinearBrushFalloff> Editor::newLinearBrushFalloff() { return std::make_unique<LinearBrushFalloff>(); }
std::unique_ptr<SmoothBrushFalloff> Editor::newSmoothBrushFalloff() { return std::make_unique<SmoothBrushFalloff>(); }
std::unique_ptr<CircleBrushKernel> Editor::newCircleBrushKernel() { return std::make_unique<CircleBrushKernel>(); }
std::unique_ptr<BoxBrushKernel> Editor::newBoxBrushKernel() { return std::make_unique<BoxBrushKernel>(); }
std::unique_ptr<PaintIntFieldOperation> Editor::newPaintIntFieldOperation(int value) {
    return std::make_unique<PaintIntFieldOperation>(value);
}
std::unique_ptr<AddScalarFieldOperation> Editor::newAddScalarFieldOperation() {
    return std::make_unique<AddScalarFieldOperation>();
}
std::unique_ptr<FieldBrushTool> Editor::newFieldBrushTool(const std::string& id, const std::string& label) {
    return std::make_unique<FieldBrushTool>(id, label, nullptr, nullptr);
}

std::unique_ptr<SphereVolumeBrushKernel> Editor::newSphereVolumeBrushKernel() {
    return std::make_unique<SphereVolumeBrushKernel>();
}
std::unique_ptr<BoxVolumeBrushKernel> Editor::newBoxVolumeBrushKernel() {
    return std::make_unique<BoxVolumeBrushKernel>();
}
std::unique_ptr<PaintIntVolumeOperation> Editor::newPaintIntVolumeOperation(int value) {
    return std::make_unique<PaintIntVolumeOperation>(value);
}
std::unique_ptr<VolumeBrushTool> Editor::newVolumeBrushTool(const std::string& id, const std::string& label) {
    return std::make_unique<VolumeBrushTool>(id, label);
}

void Editor::expose(ssq::Table& table) {
    auto cls = table.addClass(name, Editor::create, false);
    expose(cls);

    auto gizmo = table.addClass<TransformGizmo>(
        "TransformGizmo", std::function<TransformGizmo*()>([]() -> TransformGizmo* { return nullptr; }), true);
    gizmo.addFunc("setMode", &TransformGizmo::setMode);
    gizmo.addFunc("getMode", &TransformGizmo::getMode);
    gizmo.addFunc("setSpace", &TransformGizmo::setSpace);
    gizmo.addFunc("getSpace", &TransformGizmo::getSpace);
    gizmo.addFunc("setSize", &TransformGizmo::setSize);
    gizmo.addFunc("getSize", &TransformGizmo::getSize);
    gizmo.addFunc("setPosition", &TransformGizmo::setPosition);
    gizmo.addFunc("getPositionX", &TransformGizmo::getPositionX);
    gizmo.addFunc("getPositionY", &TransformGizmo::getPositionY);
    gizmo.addFunc("getPositionZ", &TransformGizmo::getPositionZ);
    gizmo.addFunc("setRotationEuler", &TransformGizmo::setRotationEuler);
    gizmo.addFunc("getRotationX", &TransformGizmo::getRotationX);
    gizmo.addFunc("getRotationY", &TransformGizmo::getRotationY);
    gizmo.addFunc("getRotationZ", &TransformGizmo::getRotationZ);
    gizmo.addFunc("setScale", &TransformGizmo::setScale);
    gizmo.addFunc("getScaleX", &TransformGizmo::getScaleX);
    gizmo.addFunc("getScaleY", &TransformGizmo::getScaleY);
    gizmo.addFunc("getScaleZ", &TransformGizmo::getScaleZ);
    gizmo.addFunc("setBounds", &TransformGizmo::setBounds);
    gizmo.addFunc("getBoundsMinX", &TransformGizmo::getBoundsMinX);
    gizmo.addFunc("getBoundsMinY", &TransformGizmo::getBoundsMinY);
    gizmo.addFunc("getBoundsMinZ", &TransformGizmo::getBoundsMinZ);
    gizmo.addFunc("getBoundsMaxX", &TransformGizmo::getBoundsMaxX);
    gizmo.addFunc("getBoundsMaxY", &TransformGizmo::getBoundsMaxY);
    gizmo.addFunc("getBoundsMaxZ", &TransformGizmo::getBoundsMaxZ);
    gizmo.addFunc("setSnapTranslate", &TransformGizmo::setSnapTranslate);
    gizmo.addFunc("setSnapRotate", &TransformGizmo::setSnapRotate);
    gizmo.addFunc("setSnapScale", &TransformGizmo::setSnapScale);
    gizmo.addFunc("getSnapTranslateX", &TransformGizmo::getSnapTranslateX);
    gizmo.addFunc("getSnapTranslateY", &TransformGizmo::getSnapTranslateY);
    gizmo.addFunc("getSnapTranslateZ", &TransformGizmo::getSnapTranslateZ);
    gizmo.addFunc("getSnapRotate", &TransformGizmo::getSnapRotate);
    gizmo.addFunc("getSnapScale", &TransformGizmo::getSnapScale);
    gizmo.addFunc("getMatrix", &TransformGizmo::getMatrix);
    gizmo.addFunc("pick", &TransformGizmo::pick);
    gizmo.addFunc("beginDrag", &TransformGizmo::beginDrag);
    gizmo.addFunc("updateDrag", &TransformGizmo::updateDrag);
    gizmo.addFunc("endDrag", &TransformGizmo::endDrag);
    gizmo.addFunc("isDragging", &TransformGizmo::isDragging);
    gizmo.addFunc("isHovered", &TransformGizmo::isHovered);
    gizmo.addFunc("getActiveAxis", &TransformGizmo::getActiveAxis);
    gizmo.addFunc("getHoverAxis", &TransformGizmo::getHoverAxis);
    gizmo.addFunc("rebuildParts", &TransformGizmo::rebuildParts);
    gizmo.addFunc("getPartCount", &TransformGizmo::getPartCount);
    gizmo.addFunc("getPartKind", &TransformGizmo::getPartKind);
    gizmo.addFunc("getPartAxis", &TransformGizmo::getPartAxis);
    gizmo.addFunc("getPartColorR", &TransformGizmo::getPartColorR);
    gizmo.addFunc("getPartColorG", &TransformGizmo::getPartColorG);
    gizmo.addFunc("getPartColorB", &TransformGizmo::getPartColorB);
    gizmo.addFunc("getPartColorA", &TransformGizmo::getPartColorA);
    gizmo.addFunc("getPartOriginX", &TransformGizmo::getPartOriginX);
    gizmo.addFunc("getPartOriginY", &TransformGizmo::getPartOriginY);
    gizmo.addFunc("getPartOriginZ", &TransformGizmo::getPartOriginZ);
    gizmo.addFunc("getPartDirX", &TransformGizmo::getPartDirX);
    gizmo.addFunc("getPartDirY", &TransformGizmo::getPartDirY);
    gizmo.addFunc("getPartDirZ", &TransformGizmo::getPartDirZ);
    gizmo.addFunc("getPartLength", &TransformGizmo::getPartLength);
    gizmo.addFunc("getPartRadius", &TransformGizmo::getPartRadius);

    auto mgr = table.addClass<GizmoManager>(
        "GizmoManager", std::function<GizmoManager*()>([]() -> GizmoManager* { return nullptr; }), true);
    mgr.addFunc("getGizmo", &GizmoManager::getGizmo);
    mgr.addFunc("setPositionEnabled", &GizmoManager::setPositionEnabled);
    mgr.addFunc("setRotationEnabled", &GizmoManager::setRotationEnabled);
    mgr.addFunc("setScaleEnabled", &GizmoManager::setScaleEnabled);
    mgr.addFunc("setBoundEnabled", &GizmoManager::setBoundEnabled);
    mgr.addFunc("getPositionEnabled", &GizmoManager::getPositionEnabled);
    mgr.addFunc("getRotationEnabled", &GizmoManager::getRotationEnabled);
    mgr.addFunc("getScaleEnabled", &GizmoManager::getScaleEnabled);
    mgr.addFunc("getBoundEnabled", &GizmoManager::getBoundEnabled);
    mgr.addFunc("attach", &GizmoManager::attach);
    mgr.addFunc("detach", &GizmoManager::detach);
    mgr.addFunc("isAttached", &GizmoManager::isAttached);
    mgr.addFunc("pick", &GizmoManager::pick);
    mgr.addFunc("beginDrag", &GizmoManager::beginDrag);
    mgr.addFunc("updateDrag", &GizmoManager::updateDrag);
    mgr.addFunc("endDrag", &GizmoManager::endDrag);
    mgr.addFunc("isDragging", &GizmoManager::isDragging);
    mgr.addFunc("isHovered", &GizmoManager::isHovered);

    auto tb = table.addClass<EditorToolbar>(
        "EditorToolbar", std::function<EditorToolbar*()>([]() -> EditorToolbar* { return nullptr; }), true);
    tb.addFunc("clear", &EditorToolbar::clear);
    tb.addFunc("addTool", &EditorToolbar::addTool);
    tb.addFunc("setShortcut", &EditorToolbar::setShortcut);
    tb.addFunc("setActive", &EditorToolbar::setActive);
    tb.addFunc("getActive", &EditorToolbar::getActive);
    tb.addFunc("matchShortcut", &EditorToolbar::matchShortcut);
    tb.addFunc("getToolCount", &EditorToolbar::getToolCount);
    tb.addFunc("getToolId", &EditorToolbar::getToolId);
    tb.addFunc("getToolLabel", &EditorToolbar::getToolLabel);
    tb.addFunc("getToolShortcut", &EditorToolbar::getToolShortcut);

    auto insp = table.addClass<EditorInspector>(
        "EditorInspector", std::function<EditorInspector*()>([]() -> EditorInspector* { return nullptr; }), true);
    insp.addFunc("clear", &EditorInspector::clear);
    insp.addFunc("addFloat", &EditorInspector::addFloat);
    insp.addFunc("addFloat3", &EditorInspector::addFloat3);
    insp.addFunc("addBool", &EditorInspector::addBool);
    insp.addFunc("addString", &EditorInspector::addString);
    insp.addFunc("addChoice", &EditorInspector::addChoice);
    insp.addFunc("getFieldCount", &EditorInspector::getFieldCount);
    insp.addFunc("getFieldKind", &EditorInspector::getFieldKind);
    insp.addFunc("getFieldId", &EditorInspector::getFieldId);
    insp.addFunc("getFieldLabel", &EditorInspector::getFieldLabel);
    insp.addFunc("getFloat", &EditorInspector::getFloat);
    insp.addFunc("setFloat", &EditorInspector::setFloat);
    insp.addFunc("getFloatMin", &EditorInspector::getFloatMin);
    insp.addFunc("getFloatMax", &EditorInspector::getFloatMax);
    insp.addFunc("getFloatStep", &EditorInspector::getFloatStep);
    insp.addFunc("getFloat3X", &EditorInspector::getFloat3X);
    insp.addFunc("getFloat3Y", &EditorInspector::getFloat3Y);
    insp.addFunc("getFloat3Z", &EditorInspector::getFloat3Z);
    insp.addFunc("setFloat3", &EditorInspector::setFloat3);
    insp.addFunc("getBool", &EditorInspector::getBool);
    insp.addFunc("setBool", &EditorInspector::setBool);
    insp.addFunc("getString", &EditorInspector::getString);
    insp.addFunc("setString", &EditorInspector::setString);
    insp.addFunc("getChoice", &EditorInspector::getChoice);
    insp.addFunc("setChoice", &EditorInspector::setChoice);
    insp.addFunc("getChoicesCsv", &EditorInspector::getChoicesCsv);
    insp.addFunc("isDirty", &EditorInspector::isDirty);
    insp.addFunc("clearDirty", &EditorInspector::clearDirty);
    insp.addFunc("clearAllDirty", &EditorInspector::clearAllDirty);
    insp.addFunc("pollChangedId", &EditorInspector::pollChangedId);

    auto dock = table.addClass<EditorDock>("EditorDock",
                                           std::function<EditorDock*()>([]() -> EditorDock* { return nullptr; }), true);
    dock.addFunc("setRegionSize", &EditorDock::setRegionSize);
    dock.addFunc("getRegionSize", &EditorDock::getRegionSize);
    dock.addFunc("layout", &EditorDock::layout);
    dock.addFunc("getRegionX", &EditorDock::getRegionX);
    dock.addFunc("getRegionY", &EditorDock::getRegionY);
    dock.addFunc("getRegionW", &EditorDock::getRegionW);
    dock.addFunc("getRegionH", &EditorDock::getRegionH);

    auto constantFalloff = table.addClass<ConstantBrushFalloff>(
        "ConstantBrushFalloff",
        std::function<ConstantBrushFalloff*()>([]() -> ConstantBrushFalloff* { return nullptr; }), true);
    auto linearFalloff = table.addClass<LinearBrushFalloff>(
        "LinearBrushFalloff", std::function<LinearBrushFalloff*()>([]() -> LinearBrushFalloff* { return nullptr; }),
        true);
    auto smoothFalloff = table.addClass<SmoothBrushFalloff>(
        "SmoothBrushFalloff", std::function<SmoothBrushFalloff*()>([]() -> SmoothBrushFalloff* { return nullptr; }),
        true);
    (void)constantFalloff;
    (void)linearFalloff;
    (void)smoothFalloff;

    auto circleKernel = table.addClass<CircleBrushKernel>(
        "CircleBrushKernel", std::function<CircleBrushKernel*()>([]() -> CircleBrushKernel* { return nullptr; }), true);
    circleKernel.addFunc("setConstantFalloff", [](CircleBrushKernel* self, ConstantBrushFalloff* falloff) {
        if (self) self->setFalloff(falloff);
    });
    circleKernel.addFunc("setLinearFalloff", [](CircleBrushKernel* self, LinearBrushFalloff* falloff) {
        if (self) self->setFalloff(falloff);
    });
    circleKernel.addFunc("setSmoothFalloff", [](CircleBrushKernel* self, SmoothBrushFalloff* falloff) {
        if (self) self->setFalloff(falloff);
    });

    auto boxKernel = table.addClass<BoxBrushKernel>(
        "BoxBrushKernel", std::function<BoxBrushKernel*()>([]() -> BoxBrushKernel* { return nullptr; }), true);
    boxKernel.addFunc("setConstantFalloff", [](BoxBrushKernel* self, ConstantBrushFalloff* falloff) {
        if (self) self->setFalloff(falloff);
    });
    boxKernel.addFunc("setLinearFalloff", [](BoxBrushKernel* self, LinearBrushFalloff* falloff) {
        if (self) self->setFalloff(falloff);
    });
    boxKernel.addFunc("setSmoothFalloff", [](BoxBrushKernel* self, SmoothBrushFalloff* falloff) {
        if (self) self->setFalloff(falloff);
    });

    auto paintOperation = table.addClass<PaintIntFieldOperation>(
        "PaintIntFieldOperation",
        std::function<PaintIntFieldOperation*()>([]() -> PaintIntFieldOperation* { return nullptr; }), true);
    paintOperation.addFunc("setValue", &PaintIntFieldOperation::setValue);
    paintOperation.addFunc("getValue", &PaintIntFieldOperation::value);
    auto addScalarOperation = table.addClass<AddScalarFieldOperation>(
        "AddScalarFieldOperation",
        std::function<AddScalarFieldOperation*()>([]() -> AddScalarFieldOperation* { return nullptr; }), true);
    (void)addScalarOperation;

    auto fieldTool = table.addClass<FieldBrushTool>(
        "FieldBrushTool", std::function<FieldBrushTool*()>([]() -> FieldBrushTool* { return nullptr; }), true);
    fieldTool.addFunc("setRadius", &FieldBrushTool::setRadius);
    fieldTool.addFunc("setStrength", &FieldBrushTool::setStrength);
    fieldTool.addFunc("getRadius", &FieldBrushTool::radius);
    fieldTool.addFunc("getStrength", &FieldBrushTool::strength);
    fieldTool.addFunc("setCircleKernel", [](FieldBrushTool* self, CircleBrushKernel* kernel) {
        if (self) self->setKernel(kernel);
    });
    fieldTool.addFunc("setBoxKernel", [](FieldBrushTool* self, BoxBrushKernel* kernel) {
        if (self) self->setKernel(kernel);
    });
    fieldTool.addFunc("setPaintIntOperation", [](FieldBrushTool* self, PaintIntFieldOperation* operation) {
        if (self) self->setOperation(operation);
    });
    fieldTool.addFunc("setAddScalarOperation", [](FieldBrushTool* self, AddScalarFieldOperation* operation) {
        if (self) self->setOperation(operation);
    });

    auto sphereVolumeKernel = table.addClass<SphereVolumeBrushKernel>(
        "SphereVolumeBrushKernel",
        std::function<SphereVolumeBrushKernel*()>([]() -> SphereVolumeBrushKernel* { return nullptr; }), true);
    sphereVolumeKernel.addFunc("setConstantFalloff", [](SphereVolumeBrushKernel* self, ConstantBrushFalloff* falloff) {
        if (self) self->setFalloff(falloff);
    });
    sphereVolumeKernel.addFunc("setLinearFalloff", [](SphereVolumeBrushKernel* self, LinearBrushFalloff* falloff) {
        if (self) self->setFalloff(falloff);
    });
    sphereVolumeKernel.addFunc("setSmoothFalloff", [](SphereVolumeBrushKernel* self, SmoothBrushFalloff* falloff) {
        if (self) self->setFalloff(falloff);
    });

    auto boxVolumeKernel = table.addClass<BoxVolumeBrushKernel>(
        "BoxVolumeBrushKernel",
        std::function<BoxVolumeBrushKernel*()>([]() -> BoxVolumeBrushKernel* { return nullptr; }), true);
    boxVolumeKernel.addFunc("setConstantFalloff", [](BoxVolumeBrushKernel* self, ConstantBrushFalloff* falloff) {
        if (self) self->setFalloff(falloff);
    });
    boxVolumeKernel.addFunc("setLinearFalloff", [](BoxVolumeBrushKernel* self, LinearBrushFalloff* falloff) {
        if (self) self->setFalloff(falloff);
    });
    boxVolumeKernel.addFunc("setSmoothFalloff", [](BoxVolumeBrushKernel* self, SmoothBrushFalloff* falloff) {
        if (self) self->setFalloff(falloff);
    });

    auto paintVolumeOperation = table.addClass<PaintIntVolumeOperation>(
        "PaintIntVolumeOperation",
        std::function<PaintIntVolumeOperation*()>([]() -> PaintIntVolumeOperation* { return nullptr; }), true);
    paintVolumeOperation.addFunc("setValue", &PaintIntVolumeOperation::setValue);
    paintVolumeOperation.addFunc("getValue", &PaintIntVolumeOperation::value);

    auto volumeTool = table.addClass<VolumeBrushTool>(
        "VolumeBrushTool", std::function<VolumeBrushTool*()>([]() -> VolumeBrushTool* { return nullptr; }), true);
    volumeTool.addFunc("setRadius", &VolumeBrushTool::setRadius);
    volumeTool.addFunc("getRadius", &VolumeBrushTool::radius);
    volumeTool.addFunc("setSphereKernel", [](VolumeBrushTool* self, SphereVolumeBrushKernel* kernel) {
        if (self) self->setKernel(kernel);
    });
    volumeTool.addFunc("setBoxKernel", [](VolumeBrushTool* self, BoxVolumeBrushKernel* kernel) {
        if (self) self->setKernel(kernel);
    });
    volumeTool.addFunc("setPaintIntOperation", [](VolumeBrushTool* self, PaintIntVolumeOperation* operation) {
        if (self) self->setOperation(operation);
    });

    auto session = table.addClass<EditorSession>(
        "EditorSession", std::function<EditorSession*()>([]() -> EditorSession* { return nullptr; }), true);
    session.addFunc("addTool", std::function<bool(EditorSession*, ScriptEditorTool*)>(
                                   [](EditorSession* self, ScriptEditorTool* tool) { return self->addTool(tool); }));
    session.addFunc("addFieldTool",
                    [](EditorSession* self, FieldBrushTool* tool) { return self && self->addTool(tool); });
    session.addFunc("addVolumeTool",
                    [](EditorSession* self, VolumeBrushTool* tool) { return self && self->addTool(tool); });
    session.addFunc("clearTarget", [](EditorSession* self) {
        if (self) self->bindTarget(nullptr);
    });
    session.addFunc("removeTool", &EditorSession::removeTool);
    session.addFunc("clearTools", &EditorSession::clearTools);
    session.addFunc("activateTool", &EditorSession::activateTool);
    session.addFunc("getToolCount", &EditorSession::getToolCount);
    session.addFunc("getActiveToolId", &EditorSession::activeToolId);
    session.addFunc("hasPointerCapture", &EditorSession::hasPointerCapture);
    session.addFunc("update", &EditorSession::update);
    session.addFunc("cancelActiveTool", &EditorSession::cancelActiveTool);
    session.addFunc(
        "undo", std::function<bool(EditorSession*)>([](EditorSession* self) { return self->transactions().undo(); }));
    session.addFunc(
        "redo", std::function<bool(EditorSession*)>([](EditorSession* self) { return self->transactions().redo(); }));
    session.addFunc("canUndo", [](EditorSession* self) { return self && self->transactions().canUndo(); });
    session.addFunc("canRedo", [](EditorSession* self) { return self && self->transactions().canRedo(); });
    session.addFunc("clearHistory", [](EditorSession* self) {
        if (self) self->transactions().clear();
    });
    session.addFunc("getCommandCount",
                    [](EditorSession* self) { return self ? static_cast<int>(self->availableCommands().size()) : 0; });
    session.addFunc("getCommandId", [](EditorSession* self, int index) {
        if (!self) return std::string{};
        const auto commands = self->availableCommands();
        return index >= 0 && index < static_cast<int>(commands.size()) ? commands[static_cast<size_t>(index)].id.value()
                                                                       : std::string{};
    });
    session.addFunc("getCommandName", [](EditorSession* self, int index) {
        if (!self) return std::string{};
        const auto commands = self->availableCommands();
        return index >= 0 && index < static_cast<int>(commands.size())
                   ? commands[static_cast<size_t>(index)].displayName
                   : std::string{};
    });
    session.addFunc("getCommandCategory", [](EditorSession* self, int index) {
        if (!self) return std::string{};
        const auto commands = self->availableCommands();
        return index >= 0 && index < static_cast<int>(commands.size()) ? commands[static_cast<size_t>(index)].category
                                                                       : std::string{};
    });
    session.addFunc("planCommand", [](EditorSession* self, const std::string& id, ssq::Object payload) -> ssq::Object {
        HSQUIRRELVM vm = payload.getHandle();
        EditorValue value;
        if (!self || !objectToEditorValue(payload, value)) return resultTable(vm, invalidScriptPayload());
        const EditorResult<PlanId> planned = self->retainPlan(CommandId(id), value, CommandSource::Script);
        ssq::Table                 out     = resultTable(vm, planned);
        if (planned.ok()) out.set("planId", planned.value().value());
        return out;
    });
    session.addFunc(
        "executePlan", [](EditorSession* self, const std::string& planId, ssq::Object scriptContext) -> ssq::Object {
            HSQUIRRELVM vm = scriptContext.getHandle();
            if (!self)
                return resultTable(vm, eve::editing::failed<TransactionReceipt>(EditorStatus::Failed,
                                                                               RuleId("editor.script.missing-session"),
                                                                               "Editor session is not available"));
            const EditorResult<TransactionReceipt> executed =
                self->executeRetainedPlan(PlanId(planId), CommandSource::Script);
            ssq::Table out = resultTable(vm, executed);
            if (executed.ok()) {
                out.set("transactionId", executed.value().id.value());
                out.set("transactionState", std::string(transactionStateName(executed.value().state)));
                out.set("beforeRevision", static_cast<int64_t>(executed.value().beforeRevision));
                out.set("afterRevision", static_cast<int64_t>(executed.value().afterRevision));
                out.set("authorityReceipt", executed.value().authorityReceipt);
            }
            return out;
        });
    session.addFunc("executeCommand",
                    [](EditorSession* self, const std::string& id, ssq::Object payload) -> ssq::Object {
                        HSQUIRRELVM vm = payload.getHandle();
                        EditorValue value;
                        if (!self || !objectToEditorValue(payload, value))
                            return resultTable(vm, invalidScriptPayload());
                        const EditorResult<EditorValue> executed =
                            self->executeCommand(CommandId(id), value, CommandSource::Script);
                        ssq::Table out = resultTable(vm, executed);
                        if (executed.ok()) setValue(out, "value", executed.value());
                        return out;
                    });
    session.addFunc("dispatchPointer",
                    std::function<int(EditorSession*, int, int, int, float, float, float, float, float)>(
                        [](EditorSession* self, int phase, int pointerId, int button, float x, float y, float dx,
                           float dy, float pressure) {
                            EditorPointerEvent event;
                            event.phase                 = static_cast<EditorPointerEvent::Phase>(phase);
                            event.pointerId             = pointerId;
                            event.button                = button;
                            event.x                     = x;
                            event.y                     = y;
                            event.deltaX                = dx;
                            event.deltaY                = dy;
                            event.pressure              = pressure;
                            const ToolResponse response = self->dispatchPointer(event);
                            return (response.handled ? 1 : 0) | (response.capturePointer ? 2 : 0) |
                                   (response.releasePointer ? 4 : 0);
                        }));
    session.addFunc("dispatchPointer3D",
                    std::function<int(EditorSession*, int, int, int, float, float, float, float, float, float, float)>(
                        [](EditorSession* self, int phase, int pointerId, int button, float x, float y, float z,
                           float dx, float dy, float dz, float pressure) {
                            EditorPointerEvent event;
                            event.phase                 = static_cast<EditorPointerEvent::Phase>(phase);
                            event.pointerId             = pointerId;
                            event.button                = button;
                            event.x                     = x;
                            event.y                     = y;
                            event.z                     = z;
                            event.deltaX                = dx;
                            event.deltaY                = dy;
                            event.deltaZ                = dz;
                            event.pressure              = pressure;
                            const ToolResponse response = self->dispatchPointer(event);
                            return (response.handled ? 1 : 0) | (response.capturePointer ? 2 : 0) |
                                   (response.releasePointer ? 4 : 0);
                        }));

    auto workspace = table.addClass<EditorWorkspace>(
        "EditorWorkspace", std::function<EditorWorkspace*()>([]() -> EditorWorkspace* { return nullptr; }), true);
    workspace.addFunc("getId", &EditorWorkspace::getId);
    workspace.addFunc("getTitle", &EditorWorkspace::getTitle);
    workspace.addFunc("setTitle", &EditorWorkspace::setTitle);
    workspace.addFunc("registerPanel",
                      static_cast<bool (EditorWorkspace::*)(const std::string&, const std::string&,
                                                            const std::string&, int)>(&EditorWorkspace::registerPanel));
    workspace.addFunc("removePanel",
                      static_cast<bool (EditorWorkspace::*)(const std::string&)>(&EditorWorkspace::removePanel));
    workspace.addFunc("clearPanels", &EditorWorkspace::clearPanels);
    workspace.addFunc("movePanel", static_cast<bool (EditorWorkspace::*)(const std::string&, const std::string&, int)>(
                                       &EditorWorkspace::movePanel));
    workspace.addFunc("setPanelCapability", &EditorWorkspace::setPanelCapability);
    workspace.addFunc("setPanelContext", &EditorWorkspace::setPanelContext);
    workspace.addFunc("setPanelVisible", &EditorWorkspace::setPanelVisible);
    workspace.addFunc("setPanelSingleton", &EditorWorkspace::setPanelSingleton);
    workspace.addFunc("activatePanel",
                      static_cast<bool (EditorWorkspace::*)(const std::string&)>(&EditorWorkspace::activatePanel));
    workspace.addFunc("getActivePanel", &EditorWorkspace::getActivePanel);
    workspace.addFunc("getPanelCount", &EditorWorkspace::getPanelCount);
    workspace.addFunc("getPanelId", &EditorWorkspace::getPanelId);
    workspace.addFunc("getPanelTitle", &EditorWorkspace::getPanelTitle);
    workspace.addFunc("getPanelRegion", &EditorWorkspace::getPanelRegion);
    workspace.addFunc("getPanelCapability", &EditorWorkspace::getPanelCapability);
    workspace.addFunc("getPanelContext", &EditorWorkspace::getPanelContext);
    workspace.addFunc("getPanelOrder", &EditorWorkspace::getPanelOrder);
    workspace.addFunc("getPanelVisible", &EditorWorkspace::getPanelVisible);
    workspace.addFunc("getPanelSingleton", &EditorWorkspace::getPanelSingleton);
    workspace.addFunc("setRegionSize", &EditorWorkspace::setRegionSize);
    workspace.addFunc("layout", &EditorWorkspace::layout);
    workspace.addFunc("getRegionX", &EditorWorkspace::getRegionX);
    workspace.addFunc("getRegionY", &EditorWorkspace::getRegionY);
    workspace.addFunc("getRegionW", &EditorWorkspace::getRegionW);
    workspace.addFunc("getRegionH", &EditorWorkspace::getRegionH);
    workspace.addFunc("setMode", &EditorWorkspace::setMode);
    workspace.addFunc("getMode", &EditorWorkspace::getMode);
    workspace.addFunc("select", &EditorWorkspace::select);
    workspace.addFunc("clearSelection", &EditorWorkspace::clearSelection);
    workspace.addFunc("getSelectionCount", &EditorWorkspace::getSelectionCount);
    workspace.addFunc("getSelectionItem", &EditorWorkspace::getSelectionItem);
    workspace.addFunc("getSelectionType", &EditorWorkspace::getSelectionType);
    workspace.addFunc("getPrimarySelection", &EditorWorkspace::getPrimarySelection);
    workspace.addFunc("getSelectionSequence", &EditorWorkspace::getSelectionSequence);
    workspace.addFunc("focus", &EditorWorkspace::focus);
    workspace.addFunc("getFocusedSurface", &EditorWorkspace::getFocusedSurface);
    workspace.addFunc("getRevision", &EditorWorkspace::getRevision);

    auto scriptTool = table.addClass<ScriptEditorTool>(
        "ScriptEditorTool", std::function<ScriptEditorTool*()>([]() -> ScriptEditorTool* { return nullptr; }), true);
    scriptTool.addFunc("setShortcut", &ScriptEditorTool::setShortcut);
    scriptTool.addFunc("setActivateCallback", &ScriptEditorTool::setActivateCallback);
    scriptTool.addFunc("setDeactivateCallback", &ScriptEditorTool::setDeactivateCallback);
    scriptTool.addFunc("setPointerCallback", &ScriptEditorTool::setPointerCallback);
    scriptTool.addFunc("setKeyCallback", &ScriptEditorTool::setKeyCallback);
    scriptTool.addFunc("setUpdateCallback", &ScriptEditorTool::setUpdateCallback);
    scriptTool.addFunc("setCancelCallback", &ScriptEditorTool::setCancelCallback);
}

void Editor::expose(ssq::Class& cls) {
    cls.addFunc("getName", &Editor::getName);
    cls.addFunc("newGizmo", [](Editor* self) { return self->newGizmo().release(); });
    cls.addFunc("newGizmoManager", [](Editor* self) { return self->newGizmoManager().release(); });
    cls.addFunc("newToolbar", [](Editor* self) { return self->newToolbar().release(); });
    cls.addFunc("newInspector", [](Editor* self) { return self->newInspector().release(); });
    cls.addFunc("newDock", [](Editor* self) { return self->newDock().release(); });
    cls.addFunc("newSession", [](Editor* self) { return self->newSession().release(); });
    cls.addFunc("newWorkspace", [](Editor* self, const std::string& id, const std::string& title) {
        auto created = self->newWorkspace(id, title);
        return created.ok() ? std::move(created).takeValue().release() : nullptr;
    });
    cls.addFunc("newScriptTool", [](Editor* self, const std::string& id, const std::string& label) {
        return self->newScriptTool(id, label).release();
    });
    cls.addFunc("newConstantBrushFalloff", [](Editor* self) { return self->newConstantBrushFalloff().release(); });
    cls.addFunc("newLinearBrushFalloff", [](Editor* self) { return self->newLinearBrushFalloff().release(); });
    cls.addFunc("newSmoothBrushFalloff", [](Editor* self) { return self->newSmoothBrushFalloff().release(); });
    cls.addFunc("newCircleBrushKernel", [](Editor* self) { return self->newCircleBrushKernel().release(); });
    cls.addFunc("newBoxBrushKernel", [](Editor* self) { return self->newBoxBrushKernel().release(); });
    cls.addFunc("newPaintIntFieldOperation", [](Editor* self, int value) {
        return self->newPaintIntFieldOperation(value).release();
    });
    cls.addFunc("newAddScalarFieldOperation",
                [](Editor* self) { return self->newAddScalarFieldOperation().release(); });
    cls.addFunc("newFieldBrushTool", [](Editor* self, const std::string& id, const std::string& label) {
        return self->newFieldBrushTool(id, label).release();
    });
    cls.addFunc("newSphereVolumeBrushKernel",
                [](Editor* self) { return self->newSphereVolumeBrushKernel().release(); });
    cls.addFunc("newBoxVolumeBrushKernel", [](Editor* self) { return self->newBoxVolumeBrushKernel().release(); });
    cls.addFunc("newPaintIntVolumeOperation", [](Editor* self, int value) {
        return self->newPaintIntVolumeOperation(value).release();
    });
    cls.addFunc("newVolumeBrushTool", [](Editor* self, const std::string& id, const std::string& label) {
        return self->newVolumeBrushTool(id, label).release();
    });
    cls.addFunc("registerScriptCommand", registerScriptCommand);
    cls.addFunc("unregisterScriptCommand", [](Editor* self, const std::string& id) {
        return self && self->commandService().unregisterCommand(CommandId(id), "script:" + id);
    });
}

}  // namespace eve::editor
