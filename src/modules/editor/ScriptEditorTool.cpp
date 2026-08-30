#include "editor/ScriptEditorTool.h"

#include <simplesquirrel/simplesquirrel.hpp>

namespace eve::editor {

struct ScriptEditorTool::Impl {
    ToolDescriptor descriptor;
    ssq::Object activate;
    ssq::Object deactivate;
    ssq::Object pointer;
    ssq::Object key;
    ssq::Object update;
    ssq::Object cancel;
};

namespace {

template <typename... Args>
void invoke(const ssq::Object &callback, const Args &...args) {
    if (callback.isEmpty()) return;
    ssq::Function function = callback.toFunction();
    if (function.isEmpty()) return;
    HSQUIRRELVM vm = function.getHandle();
    const SQInteger top = sq_gettop(vm);
    sq_pushobject(vm, function.getRaw());
    sq_pushroottable(vm);
    (ssq::detail::pushValue(vm, args), ...);
    sq_call(vm, sizeof...(Args) + 1, SQFalse, SQTrue);
    sq_settop(vm, top);
}

template <typename... Args>
int invokeFlags(const ssq::Object &callback, const Args &...args) {
    if (callback.isEmpty()) return 0;
    ssq::Function function = callback.toFunction();
    if (function.isEmpty()) return 0;
    HSQUIRRELVM vm = function.getHandle();
    const SQInteger top = sq_gettop(vm);
    sq_pushobject(vm, function.getRaw());
    sq_pushroottable(vm);
    (ssq::detail::pushValue(vm, args), ...);
    int flags = 0;
    if (SQ_SUCCEEDED(sq_call(vm, sizeof...(Args) + 1, SQTrue, SQTrue))) {
        SQInteger result = 0;
        if (SQ_SUCCEEDED(sq_getinteger(vm, -1, &result))) flags = static_cast<int>(result);
    }
    sq_settop(vm, top);
    return flags;
}

ToolResponse responseFromFlags(int flags) {
    return {(flags & 1) != 0, (flags & 2) != 0, (flags & 4) != 0};
}

}  // namespace

ScriptEditorTool::ScriptEditorTool(std::string id, std::string label) : impl_(new Impl) {
    impl_->descriptor.id = std::move(id);
    impl_->descriptor.label = std::move(label);
}

ScriptEditorTool::~ScriptEditorTool() = default;

const ToolDescriptor &ScriptEditorTool::descriptor() const { return impl_->descriptor; }
void ScriptEditorTool::setShortcut(const std::string &shortcut) { impl_->descriptor.shortcut = shortcut; }
void ScriptEditorTool::setActivateCallback(ssq::Object callback) { impl_->activate = std::move(callback); }
void ScriptEditorTool::setDeactivateCallback(ssq::Object callback) { impl_->deactivate = std::move(callback); }
void ScriptEditorTool::setPointerCallback(ssq::Object callback) { impl_->pointer = std::move(callback); }
void ScriptEditorTool::setKeyCallback(ssq::Object callback) { impl_->key = std::move(callback); }
void ScriptEditorTool::setUpdateCallback(ssq::Object callback) { impl_->update = std::move(callback); }
void ScriptEditorTool::setCancelCallback(ssq::Object callback) { impl_->cancel = std::move(callback); }

void ScriptEditorTool::activate(EditorContext &) { invoke(impl_->activate); }
void ScriptEditorTool::deactivate(EditorContext &) { invoke(impl_->deactivate); }
void ScriptEditorTool::cancel(EditorContext &) { invoke(impl_->cancel); }
void ScriptEditorTool::update(EditorContext &, float dt) { invoke(impl_->update, dt); }

ToolResponse ScriptEditorTool::pointerEvent(EditorContext &, const EditorPointerEvent &event) {
    return responseFromFlags(invokeFlags(impl_->pointer, static_cast<int>(event.phase),
                                         event.pointerId, event.button, event.x, event.y,
                                         event.deltaX, event.deltaY, event.pressure,
                                         event.shift, event.control, event.alt));
}

ToolResponse ScriptEditorTool::keyEvent(EditorContext &, const EditorKeyEvent &event) {
    return responseFromFlags(invokeFlags(impl_->key, event.key, event.pressed, event.repeated,
                                         event.shift, event.control, event.alt));
}

}  // namespace eve::editor
