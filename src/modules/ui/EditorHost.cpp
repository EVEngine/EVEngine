#include "ui/EditorHost.h"

#include "common/config.h"

#if defined(EVENGINE_WEBGPU) && defined(__EMSCRIPTEN__)

// The browser runtime trims Poco / DevTools / the host path. Keep EVUI
// linkable with a no-op implementation; `eve mcp` is desktop-only anyway.
namespace eve::ui {
struct EditorHost::Impl {};

EditorHost& EditorHost::instance() {
    static EditorHost* inst = new EditorHost();
    return *inst;
}
EditorHost::EditorHost() = default;
EditorHost::~EditorHost() = default;
void        EditorHost::start(ssq::VM&, const std::string&, bool) {}
void        EditorHost::stop() {}
bool        EditorHost::windowOpen() const { return false; }
std::string EditorHost::openWindow(const std::string&, int, int) {
    return "error: editor host unavailable on this platform";
}
std::string EditorHost::closeWindow() { return "ok"; }
std::string EditorHost::windowState() const { return "{}"; }
std::string EditorHost::applyEditor(const std::string&) {
    return "error: editor host unavailable on this platform";
}
std::string EditorHost::removeEditor(const std::string&) {
    return "error: editor host unavailable on this platform";
}
std::string EditorHost::listEditors() const { return "{\"editors\":[]}"; }
std::string EditorHost::editorState(const std::string&) const { return "{\"editors\":[]}"; }
void        EditorHost::syncBindings() {}
std::string EditorHost::setEditorValue(const std::string&, const std::string&, const std::string&) {
    return "error: editor host unavailable on this platform";
}
std::string EditorHost::consumeEvents(const std::string&) { return "[]"; }
std::string EditorHost::widgetRect(const std::string&, const std::string&) const {
    return "{\"x\":0,\"y\":0,\"width\":0,\"height\":0}";
}
std::string EditorHost::registerVM(const std::string&, const std::string&) {
    return "error: editor host unavailable on this platform";
}
std::string EditorHost::unregisterVM(const std::string&) {
    return "error: editor host unavailable on this platform";
}
std::string EditorHost::saveEditor(const std::string&) {
    return "error: editor host unavailable on this platform";
}
std::string EditorHost::unloadEditor(const std::string&) {
    return "error: editor host unavailable on this platform";
}
void        EditorHost::loadEditorsFromDisk() {}
std::string EditorHost::runScript(const std::string&) {
    return "error: editor host unavailable on this platform";
}
std::string EditorHost::capture(const std::string&) {
    return "error: editor host unavailable on this platform";
}
std::string EditorHost::status() const {
    return "{\"running\":false,\"windowOpen\":false,\"editors\":[],\"viewModels\":[]}";
}
void EditorHost::frame() {}
void EditorHost::renderImGui() {}
void EditorHost::exposeScriptApi(ssq::VM&) {}
}  // namespace eve::ui

#else  // full implementation

#include "common/Module.h"
#include "event/Event.h"
#include "filesystem/FileData.h"
#include "graphics/Graphics.h"
#include "image/ImageData.h"
#include "timer/Timer.h"
#include "ui/UI.h"
#include "window/Window.h"

#include <Poco/Dynamic/Var.h>
#include <Poco/Exception.h>
#include <Poco/JSON/Array.h>
#include <Poco/JSON/Object.h>
#include <Poco/JSON/Parser.h>
#include <Poco/JSON/Stringifier.h>

#include <imgui.h>
#include <simplesquirrel/simplesquirrel.hpp>
#include <squirrel.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <fstream>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace eve::ui {
namespace {

using Poco::Dynamic::Var;
using Poco::JSON::Array;
using Poco::JSON::Object;
using Poco::JSON::Parser;

std::string jsonStringify(const Var& v) {
    std::ostringstream oss;
    Poco::JSON::Stringifier::stringify(v, oss, 0, 0);
    return oss.str();
}

Object::Ptr parseObject(const std::string& json) {
    Poco::JSON::Parser parser;
    return parser.parse(json).extract<Object::Ptr>();
}

std::string strOf(Object::Ptr o, const char* key, const std::string& def = {}) {
    if (!o || !o->has(key)) return def;
    try {
        return o->get(key).convert<std::string>();
    } catch (...) {
        return def;
    }
}

int intOf(Object::Ptr o, const char* key, int def = 0) {
    if (!o || !o->has(key)) return def;
    try {
        return o->get(key).convert<int>();
    } catch (...) {
        return def;
    }
}

float floatOf(Object::Ptr o, const char* key, float def = 0.f) {
    if (!o || !o->has(key)) return def;
    try {
        return static_cast<float>(o->get(key).convert<double>());
    } catch (...) {
        return def;
    }
}

bool boolOf(Object::Ptr o, const char* key, bool def = false) {
    if (!o || !o->has(key)) return def;
    try {
        return o->get(key).convert<bool>();
    } catch (...) {
        return def;
    }
}

float varFloat(const Var& v, float def = 0.f) {
    try {
        return static_cast<float>(v.convert<double>());
    } catch (...) {
        return def;
    }
}

bool varBool(const Var& v, bool def = false) {
    try {
        return v.convert<bool>();
    } catch (...) {
        return def;
    }
}

std::string varString(const Var& v, const std::string& def = {}) {
    try {
        return v.convert<std::string>();
    } catch (...) {
        return def;
    }
}

Array::Ptr varToArray(const Var& v) {
    try {
        return v.extract<Array::Ptr>();
    } catch (...) {
        return nullptr;
    }
}

std::vector<float> varFloats(const Var& v) {
    std::vector<float> out;
    Array::Ptr arr = varToArray(v);
    if (!arr) return out;
    for (size_t i = 0; i < arr->size(); ++i) {
        try {
            out.push_back(
                static_cast<float>(arr->get(static_cast<unsigned int>(i)).convert<double>()));
        } catch (...) {
            out.push_back(0.f);
        }
    }
    return out;
}

/** Slider init: array (slider2/slider3) or single scalar (slider). */
std::vector<float> widgetFloatInit(const Var& v) {
    std::vector<float> init = varFloats(v);
    if (!init.empty()) return init;
    try {
        init.push_back(static_cast<float>(v.convert<double>()));
    } catch (...) {
    }
    return init;
}

std::vector<std::string> varStrings(const Var& v) {
    std::vector<std::string> out;
    Array::Ptr arr = varToArray(v);
    if (!arr) return out;
    for (size_t i = 0; i < arr->size(); ++i)
        out.push_back(varString(arr->get(static_cast<unsigned int>(i))));
    return out;
}

// ---- raw Squirrel helpers -------------------------------------------------

Var stackVar(HSQUIRRELVM vm, SQInteger index) {
    switch (sq_gettype(vm, index)) {
        case OT_NULL:
            return Var();
        case OT_BOOL: {
            SQBool b = SQFalse;
            if (SQ_SUCCEEDED(sq_getbool(vm, index, &b))) return Var(bool(b));
            return Var();
        }
        case OT_INTEGER: {
            SQInteger i = 0;
            if (SQ_SUCCEEDED(sq_getinteger(vm, index, &i)))
                return Var(static_cast<Poco::Int64>(i));
            return Var();
        }
        case OT_FLOAT: {
            SQFloat f = 0.f;
            if (SQ_SUCCEEDED(sq_getfloat(vm, index, &f)))
                return Var(static_cast<double>(f));
            return Var();
        }
        case OT_STRING: {
            const SQChar* s = nullptr;
            if (SQ_SUCCEEDED(sq_getstring(vm, index, &s)) && s) return Var(std::string(s));
            return Var();
        }
        case OT_ARRAY: {
            HSQOBJECT obj;
            if (SQ_FAILED(sq_getstackobj(vm, index, &obj))) return Var();
            sq_pushobject(vm, obj);
            sq_pushnull(vm);
            Array::Ptr arr = new Array();
            while (SQ_SUCCEEDED(sq_next(vm, -2))) {
                arr->add(stackVar(vm, -1));
                sq_pop(vm, 2);
            }
            sq_pop(vm, 1);
            return Var(arr);
        }
        default:
            return Var();
    }
}

void pushVar(HSQUIRRELVM vm, const Var& v) {
    if (v.isEmpty()) {
        sq_pushnull(vm);
        return;
    }
    try {
        if (v.isBoolean()) {
            sq_pushbool(vm, v.convert<bool>() ? SQTrue : SQFalse);
            return;
        }
        if (v.isNumeric()) {
            sq_pushfloat(vm, static_cast<SQFloat>(v.convert<double>()));
            return;
        }
        if (v.isString()) {
            const std::string s = v.convert<std::string>();
            sq_pushstring(vm, s.c_str(), static_cast<SQInteger>(s.size()));
            return;
        }
        if (v.isArray()) {
            Array::Ptr arr;
            try {
                arr = v.extract<Array::Ptr>();
            } catch (...) {
                sq_pushnull(vm);
                return;
            }
            sq_newarray(vm, 0);
            if (arr) {
                for (size_t i = 0; i < arr->size(); ++i) {
                    pushVar(vm, arr->get(static_cast<unsigned int>(i)));
                    if (SQ_FAILED(sq_arrayappend(vm, -2))) break;
                }
            }
            return;
        }
    } catch (...) {
    }
    sq_pushnull(vm);
}

/** Run Squirrel source against the root table; returns "" on success. */
std::string runSquirrel(HSQUIRRELVM vm, const std::string& source, const char* name) {
    const SQInteger top = sq_gettop(vm);
    if (SQ_FAILED(sq_compilebuffer(vm, source.c_str(), static_cast<SQInteger>(source.size()),
                                   name, SQTrue))) {
        sq_settop(vm, top);
        return "compile failed";
    }
    sq_pushroottable(vm);
    if (SQ_FAILED(sq_call(vm, 1, SQFalse, SQTrue))) {
        sq_settop(vm, top);
        return "runtime failed";
    }
    sq_settop(vm, top);
    return {};
}

void callRootFunc(HSQUIRRELVM vm, const std::string& name, const std::vector<Var>& args) {
    const SQInteger top = sq_gettop(vm);
    sq_pushroottable(vm);
    sq_pushstring(vm, name.c_str(), static_cast<SQInteger>(name.size()));
    if (SQ_FAILED(sq_get(vm, -2))) {
        sq_settop(vm, top);
        return;  // hook not defined
    }
    if (sq_gettype(vm, -1) != OT_CLOSURE) {
        sq_settop(vm, top);
        return;
    }
    sq_pushroottable(vm);
    for (const auto& a : args) pushVar(vm, a);
    if (SQ_FAILED(sq_call(vm, static_cast<SQInteger>(args.size()) + 1, SQFalse, SQTrue)))
        fprintf(stderr, "eve.host: %s() failed\n", name.c_str());
    sq_settop(vm, top);
}

bool callTableFunc(HSQUIRRELVM vm, const ssq::Table& tbl, const std::string& name,
                   const std::vector<Var>& args, std::string* err) {
    if (name.empty()) return true;
    const SQInteger top = sq_gettop(vm);
    try {
        ssq::Function fn = tbl.findFunc(name.c_str());
        sq_pushobject(vm, fn.getRaw());
        sq_pushobject(vm, tbl.getRaw());
        for (const auto& a : args) pushVar(vm, a);
        if (SQ_FAILED(sq_call(vm, static_cast<SQInteger>(args.size()) + 1, SQFalse, SQTrue))) {
            if (err) *err = "runtime failed: " + name;
            sq_settop(vm, top);
            return false;
        }
        sq_settop(vm, top);
        return true;
    } catch (const ssq::NotFoundException&) {
        sq_settop(vm, top);
        return true;  // optional callback / command absent
    } catch (const std::exception& e) {
        if (err) *err = e.what();
        sq_settop(vm, top);
        return false;
    }
}

/** Split "vm.slot" / "vm.table.slot" into slot (+ nested + nestedSlot). */
bool splitBindPath(const std::string& bind, std::string& slot, std::string& nested) {
    if (bind.rfind("vm.", 0) != 0) return false;
    std::string rest = bind.substr(3);
    const size_t dot = rest.find('.');
    if (dot == std::string::npos) {
        slot   = rest;
        nested.clear();
    } else if (rest.find('.', dot + 1) == std::string::npos) {
        slot   = rest.substr(0, dot);
        nested = rest.substr(dot + 1);
    } else {
        return false;  // only one nesting level supported in v1
    }
    return !slot.empty();
}

Var objectToVar(ssq::VM& vm, const ssq::Object& o) {
    // Push the object and convert through the stack so arrays recurse cleanly.
    HSQUIRRELVM v = vm.getHandle();
    const SQInteger top = sq_gettop(v);
    sq_pushobject(v, o.getRaw());
    Var out = stackVar(v, -1);
    sq_settop(v, top);
    return out;
}

Var readVMVar(ssq::VM& vm, const ssq::Table& tbl, const std::string& bind, bool* ok = nullptr) {
    std::string slot, nested;
    if (ok) *ok = false;
    if (!splitBindPath(bind, slot, nested)) return Var();
    try {
        ssq::Object o = tbl.find(slot.c_str());
        if (!nested.empty()) {
            if (o.getType() != ssq::Type::TABLE) return Var();
            o = o.toTable().find(nested.c_str());
        }
        if (ok) *ok = true;
        return objectToVar(vm, o);
    } catch (...) {
        return Var();
    }
}

void writeVMVar(ssq::VM& vm, const ssq::Table& tbl, const std::string& bind, const Var& value) {
    std::string slot, nested;
    if (!splitBindPath(bind, slot, nested)) return;
    HSQUIRRELVM v = vm.getHandle();
    const SQInteger top = sq_gettop(v);
    if (nested.empty()) {
        sq_pushobject(v, tbl.getRaw());
        sq_pushstring(v, slot.c_str(), static_cast<SQInteger>(slot.size()));
        pushVar(v, value);
        sq_newslot(v, -3, false);
    } else {
        try {
            ssq::Table t2 = tbl.find(slot.c_str()).toTable();
            sq_pushobject(v, t2.getRaw());
            sq_pushstring(v, nested.c_str(), static_cast<SQInteger>(nested.size()));
            pushVar(v, value);
            sq_newslot(v, -3, false);
        } catch (...) {
        }
    }
    sq_settop(v, top);
}

struct HostEvent {
    std::string type;  // "click" | "change" | "closed"
    std::string widget;
    Var         value;
};

struct Editor {
    std::string                 id;
    std::string                 title;
    std::string                 vmName;
    Object::Ptr                 view;
    float                       x = 0.f, y = 0.f, width = 0.f, height = 0.f;
    bool                        resizable  = true;
    bool                        collapsible = true;
    std::string                 layout = "vertical";
    Object::Ptr                 theme;
    std::map<std::string, Var>  values;
    std::vector<HostEvent>      events;
    std::map<std::string, ImVec2> rectMin, rectMax;
    bool                        removed = false;
};

void forEachWidget(Object::Ptr parent, const std::function<void(Object::Ptr)>& fn) {
    if (!parent) return;
    Array::Ptr kids = parent->has("children") ? parent->getArray("children") : nullptr;
    if (!kids) return;
    for (size_t i = 0; i < kids->size(); ++i) {
        Object::Ptr w = kids->getObject(static_cast<unsigned>(i));
        if (!w) continue;
        fn(w);
        forEachWidget(w, fn);
    }
}

Object::Ptr findWidget(Object::Ptr parent, const std::string& id) {
    if (!parent || id.empty()) return nullptr;
    Array::Ptr kids = parent->has("children") ? parent->getArray("children") : nullptr;
    if (!kids) return nullptr;
    for (size_t i = 0; i < kids->size(); ++i) {
        Object::Ptr w = kids->getObject(static_cast<unsigned>(i));
        if (!w) continue;
        if (strOf(w, "id") == id) return w;
        if (Object::Ptr sub = findWidget(w, id)) return sub;
    }
    return nullptr;
}

std::string widgetIdLabel(Object::Ptr w) {
    const std::string id = strOf(w, "id");
    const std::string label = strOf(w, "label");
    if (label.empty()) return "##" + id;
    if (id.empty()) return label;
    return label + "##" + id;
}

/** Resolve "vm.slot" paths to a plain ViewModel slot/function name. */
std::string vmFuncName(const std::string& path) {
    if (path.rfind("vm.", 0) == 0) return path.substr(3);
    return path;
}

void recordRect(Editor& ed, const std::string& id) {
    if (id.empty()) return;
    ed.rectMin[id] = ImGui::GetItemRectMin();
    ed.rectMax[id] = ImGui::GetItemRectMax();
}

void emitEvent(Editor& ed, const std::string& type, const std::string& widget, const Var& value) {
    ed.events.push_back({type, widget, value});
}

void renderChildren(EditorHost::Impl& I, Editor& ed, Array::Ptr kids, const std::string& layout);
void syncVMToView(EditorHost::Impl& I, Editor& ed);

}  // namespace

// Out-of-line definition of the pimpl. Lives at eve::ui scope (not inside the
// anonymous namespace) so it can be named as eve::ui::EditorHost::Impl.
struct EditorHost::Impl {
    ssq::VM*                  vm          = nullptr;
    bool                      allowWindow = true;
    std::string               rootDir;
    eve::window::Window*      win         = nullptr;
    eve::graphics::Graphics*  gfx         = nullptr;
    eve::ui::UI*              ui          = nullptr;
    eve::event::Event*        event       = nullptr;
    eve::timer::Timer*        timer       = nullptr;
    bool                      windowOpen  = false;
    bool                      inFrame     = false;
    std::string               windowTitle;
    std::map<std::string, Editor>      editors;
    std::map<std::string, ssq::Table>  vms;
    std::map<std::string, std::string> vmSources;
};

// ---------------------------------------------------------------------------
// public surface
// ---------------------------------------------------------------------------

EditorHost& EditorHost::instance() {
    // Intentionally leaked: the host VM may be torn down after the singleton.
    static EditorHost* inst = new EditorHost();
    return *inst;
}

EditorHost::EditorHost() = default;
EditorHost::~EditorHost() = default;

void EditorHost::start(ssq::VM& vm, const std::string& gameRoot, bool allowWindow) {
    if (running_) return;
    impl_       = std::make_unique<Impl>();
    impl_->vm   = &vm;
    impl_->allowWindow = allowWindow;
    allowWindow_ = allowWindow;
    gameRoot_   = gameRoot.empty() ? std::filesystem::current_path().string() : gameRoot;
    impl_->rootDir = gameRoot_;
    running_    = true;
    loadEditorsFromDisk();
}

void EditorHost::stop() {
    if (!running_) return;
    closeWindow();
    impl_.reset();
    running_       = false;
    exitRequested_ = false;
}

std::string EditorHost::openWindow(const std::string& title, int width, int height) {
    if (!impl_) return "error: host not started";
    auto& I = *impl_;
    if (!I.allowWindow) return "error: window creation disabled";
    try {
        if (!I.win) I.win = ModuleManager::requireInstance<eve::window::Window>("Window");
        if (!I.gfx) I.gfx = ModuleManager::requireInstance<eve::graphics::Graphics>("Graphics");
        eve::window::WindowSettings s;
        s.width    = static_cast<uint16_t>(width > 0 ? width : 1280);
        s.height   = static_cast<uint16_t>(height > 0 ? height : 800);
        s.centered = true;
        s.resizable = true;
        if (!I.win->setWindowSettings(s)) return "error: setWindowSettings failed";
        I.win->setWindowTitle(title.empty() ? "EVEngine AI Host" : title);
        if (!I.ui) I.ui = ModuleManager::requireInstance<eve::ui::UI>("UI");
        I.windowOpen  = true;
        I.windowTitle = title;
        // Keep screen readback on while the host window is open: the AI's whole
        // feedback loop is "render -> capture", so every presented frame should
        // be available to eve_host_capture immediately (the Vulkan backend
        // installs the present-copy hook as soon as readback is enabled).
        if (I.gfx) I.gfx->setScreenReadbackEnabled(true);
        return "ok";
    } catch (const std::exception& e) {
        return std::string("error: ") + e.what();
    }
}

std::string EditorHost::closeWindow() {
    if (!impl_) return "ok";
    auto& I = *impl_;
    if (I.windowOpen && I.win) {
        try {
            I.win->close();
        } catch (...) {
        }
        if (I.ui) I.ui->shutdownBackend();
        I.windowOpen = false;
    }
    return "ok";
}

bool EditorHost::windowOpen() const {
    return impl_ && impl_->windowOpen;
}

std::string EditorHost::windowState() const {
    if (!impl_ || !impl_->windowOpen) return "{\"open\":false}";
    Object::Ptr o = new Object();
    o->set("open", true);
    o->set("title", impl_->windowTitle);
    if (impl_->win) {
        o->set("width", impl_->win->getWidth());
        o->set("height", impl_->win->getHeight());
    }
    return jsonStringify(Var(o));
}

std::string EditorHost::applyEditor(const std::string& json) {
    if (!impl_) return "error: host not started";
    auto& I = *impl_;
    try {
        Object::Ptr root = parseObject(json);
        const std::string id = strOf(root, "id");
        if (id.empty()) return "error: editor requires id";
        Editor ed;
        ed.id         = id;
        ed.title      = strOf(root, "title", id);
        ed.vmName     = strOf(root, "vm");
        ed.view       = root;
        ed.x          = static_cast<float>(intOf(root, "x", 0));
        ed.y          = static_cast<float>(intOf(root, "y", 0));
        ed.width      = static_cast<float>(intOf(root, "width", 0));
        ed.height     = static_cast<float>(intOf(root, "height", 0));
        ed.resizable  = boolOf(root, "resizable", true);
        ed.collapsible = boolOf(root, "collapsible", true);
        ed.layout     = strOf(root, "layout", "vertical");
        if (root->has("theme")) ed.theme = root->getObject("theme");
        forEachWidget(root, [&](Object::Ptr w) {
            const std::string wid = strOf(w, "id");
            if (wid.empty()) return;
            if (w->has("value")) ed.values[wid] = w->get("value");
        });
        I.editors[id] = std::move(ed);
        if (!I.windowOpen && I.allowWindow) {
            std::string err = openWindow(ed.title + " - EVEngine AI Host", 1280, 800);
            if (err.rfind("error:", 0) == 0) return err;
        }
        return editorState(id);
    } catch (const std::exception& e) {
        return std::string("error: ") + e.what();
    }
}

std::string EditorHost::removeEditor(const std::string& id) {
    if (!impl_) return "error: host not started";
    auto it = impl_->editors.find(id);
    if (it == impl_->editors.end()) return "error: editor not found: " + id;
    impl_->editors.erase(it);
    return "ok";
}

std::string EditorHost::listEditors() const {
    if (!impl_) return "{\"editors\":[]}";
    Array::Ptr arr = new Array();
    for (const auto& [k, ed] : impl_->editors) {
        Object::Ptr o = new Object();
        o->set("id", k);
        o->set("title", ed.title);
        o->set("vm", ed.vmName);
        arr->add(o);
    }
    Object::Ptr root = new Object();
    root->set("editors", arr);
    return jsonStringify(Var(root));
}

namespace {

Poco::JSON::Object::Ptr editorStateObject(const Editor& ed) {
    Object::Ptr o = new Object();
    o->set("id", ed.id);
    o->set("title", ed.title);
    o->set("vm", ed.vmName);
    Object::Ptr vals = new Object();
    for (const auto& [k, v] : ed.values) vals->set(k, v);
    o->set("values", vals);
    Array::Ptr evs = new Array();
    for (const auto& e : ed.events) {
        Object::Ptr eo = new Object();
        eo->set("editor", ed.id);
        eo->set("widget", e.widget);
        eo->set("type", e.type);
        if (!e.value.isEmpty()) eo->set("value", e.value);
        evs->add(eo);
    }
    o->set("events", evs);
    return o;
}

}  // namespace

std::string EditorHost::editorState(const std::string& id) const {
    if (!impl_) return "{\"editors\":[]}";
    const_cast<EditorHost*>(this)->syncBindings();
    Array::Ptr arr = new Array();
    if (!id.empty()) {
        auto it = impl_->editors.find(id);
        if (it == impl_->editors.end()) return "error: editor not found: " + id;
        arr->add(editorStateObject(it->second));
    } else {
        for (const auto& [k, ed] : impl_->editors) arr->add(editorStateObject(ed));
    }
    Object::Ptr root = new Object();
    root->set("editors", arr);
    return jsonStringify(Var(root));
}

void EditorHost::syncBindings() {
    if (!impl_) return;
    for (auto& [k, ed] : impl_->editors) syncVMToView(*impl_, ed);
}

namespace {

void pushViewToVM(EditorHost::Impl& I, Editor& ed, const std::string& widgetId, const Var& value) {
    if (!I.vm || ed.vmName.empty()) return;
    auto it = I.vms.find(ed.vmName);
    if (it == I.vms.end()) return;
    Object::Ptr w = findWidget(ed.view, widgetId);
    if (!w) return;
    const std::string bind = strOf(w, "bind");
    if (!bind.empty()) writeVMVar(*I.vm, it->second, bind, value);
    const std::string onChange = strOf(w, "onChange");
    if (!onChange.empty()) {
        std::string err;
        callTableFunc(I.vm->getHandle(), it->second, vmFuncName(onChange),
                      {Var(widgetId), value}, &err);
        if (!err.empty())
            fprintf(stderr, "eve.host: onChange %s: %s\n", onChange.c_str(), err.c_str());
    }
}

void setWidgetValue(EditorHost::Impl& I, Editor& ed, const std::string& widgetId,
                    const Var& value) {
    ed.values[widgetId] = value;
    emitEvent(ed, "change", widgetId, value);
    pushViewToVM(I, ed, widgetId, value);
}

void resolveOneWay(EditorHost::Impl& I, Editor& ed, Object::Ptr w) {
    if (!I.vm || ed.vmName.empty()) return;
    auto it = I.vms.find(ed.vmName);
    if (it == I.vms.end()) return;
    const char* oneWay[] = {"bind:label", "bind:visible", "bind:enabled"};
    for (const char* key : oneWay) {
        const std::string bind = strOf(w, key);
        if (bind.empty()) continue;
        const Var v = readVMVar(*I.vm, it->second, bind);
        if (v.isEmpty()) continue;
        const std::string k(key);
        if (k == "bind:label") w->set("label", varString(v));
        else if (k == "bind:visible") w->set("visible", varBool(v, true));
        else if (k == "bind:enabled") w->set("enabled", varBool(v, true));
    }
    const std::string optBind = strOf(w, "bind:options");
    if (!optBind.empty()) {
        const Var v = readVMVar(*I.vm, it->second, optBind);
        if (!v.isEmpty()) {
            Array::Ptr opts = new Array();
            for (const auto& s : varStrings(v)) opts->add(s);
            w->set("options", opts);
        }
    }
}

void syncVMToView(EditorHost::Impl& I, Editor& ed) {
    if (!I.vm || ed.vmName.empty()) return;
    auto it = I.vms.find(ed.vmName);
    if (it == I.vms.end()) return;
    forEachWidget(ed.view, [&](Object::Ptr w) {
        const std::string bind = strOf(w, "bind");
        const std::string id   = strOf(w, "id");
        if (bind.empty() || id.empty()) return;
        const Var v = readVMVar(*I.vm, it->second, bind);
        if (v.isEmpty()) return;
        auto cur = ed.values.find(id);
        if (cur == ed.values.end() || jsonStringify(cur->second) != jsonStringify(v))
            ed.values[id] = v;
    });
}

void renderWidget(EditorHost::Impl& I, Editor& ed, Object::Ptr w);

void renderChildren(EditorHost::Impl& I, Editor& ed, Array::Ptr kids, const std::string& layout) {
    if (!kids) return;
    for (size_t i = 0; i < kids->size(); ++i) {
        Object::Ptr w = kids->getObject(static_cast<unsigned>(i));
        if (!w) continue;
        resolveOneWay(I, ed, w);
        renderWidget(I, ed, w);
        if (layout == "horizontal" && i + 1 < kids->size()) ImGui::SameLine();
    }
}

void renderWidget(EditorHost::Impl& I, Editor& ed, Object::Ptr w) {
    if (!w) return;
    if (!boolOf(w, "visible", true)) return;
    const std::string type = strOf(w, "type", "label");
    const std::string id   = strOf(w, "id");
    const std::string label = strOf(w, "label");
    const bool enabled = boolOf(w, "enabled", true);
    if (!enabled) {
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);
    }

    const auto cur = [&](const char* key, const Var& def) -> Var {
        if (!id.empty()) {
            auto it = ed.values.find(id);
            if (it != ed.values.end()) return it->second;
        }
        if (w->has(key)) return w->get(key);
        return def;
    };

    if (type == "label" || type == "text") {
        const std::string text = strOf(w, "text", label);
        if (type == "text") ImGui::TextWrapped("%s", text.c_str());
        else ImGui::TextUnformatted(text.c_str());
        recordRect(ed, id);
    } else if (type == "separator") {
        ImGui::Separator();
    } else if (type == "spacer") {
        ImGui::Dummy(ImVec2(floatOf(w, "width", 0.f), floatOf(w, "height", 8.f)));
    } else if (type == "progress") {
        float v = std::clamp(varFloat(cur("value", 0.0), 0.f), 0.f, 1.f);
        const float pw = floatOf(w, "width", 0.f);
        ImGui::ProgressBar(v, ImVec2(pw > 0.f ? pw : -FLT_MIN, 0.f),
                           label.empty() ? nullptr : label.c_str());
        recordRect(ed, id);
    } else if (type == "plot") {
        const std::vector<float> data = varFloats(cur("data", Var()));
        if (!data.empty()) {
            ImGui::PlotLines(widgetIdLabel(w).c_str(), data.data(), static_cast<int>(data.size()), 0,
                             nullptr, floatOf(w, "min", 0.f), floatOf(w, "max", 1.f),
                             ImVec2(floatOf(w, "width", 0.f), floatOf(w, "height", 80.f)));
            recordRect(ed, id);
        }
    } else if (type == "input") {
        std::string buf = varString(cur("value", strOf(w, "value")));
        const bool multi = boolOf(w, "multiline", false);
        bool changed = false;
        if (multi) {
            changed = ImGui::InputTextMultiline(widgetIdLabel(w).c_str(), buf.data(),
                                                buf.size() + 64,
                                                ImVec2(floatOf(w, "width", 0.f), floatOf(w, "height", 96.f)));
        } else {
            buf.resize(512);
            changed = ImGui::InputText(widgetIdLabel(w).c_str(), buf.data(), buf.size());
        }
        if (enabled && changed && !id.empty()) {
            buf.resize(strlen(buf.c_str()));
            setWidgetValue(I, ed, id, Var(buf));
        }
        recordRect(ed, id);
    } else if (type == "slider" || type == "slider2" || type == "slider3") {
        const int n = type == "slider" ? 1 : (type == "slider2" ? 2 : 3);
        float v[3] = {0.f, 0.f, 0.f};
        const std::vector<float> init = widgetFloatInit(cur("value", Var()));
        for (int i = 0; i < n; ++i) v[i] = i < static_cast<int>(init.size()) ? init[i] : 0.f;
        const float lo = floatOf(w, "min", 0.f), hi = floatOf(w, "max", 1.f);
        const std::string fmt = strOf(w, "format", "%.2f");
        bool changed = false;
        if (n == 1) changed = ImGui::SliderFloat(widgetIdLabel(w).c_str(), &v[0], lo, hi, fmt.c_str());
        else if (n == 2) changed = ImGui::SliderFloat2(widgetIdLabel(w).c_str(), v, lo, hi, fmt.c_str());
        else changed = ImGui::SliderFloat3(widgetIdLabel(w).c_str(), v, lo, hi, fmt.c_str());
        if (enabled && changed && !id.empty()) {
            Array::Ptr arr = new Array();
            for (int i = 0; i < n; ++i) arr->add(static_cast<double>(v[i]));
            setWidgetValue(I, ed, id, Var(arr));
        }
        recordRect(ed, id);
    } else if (type == "color") {
        const bool alpha = boolOf(w, "alpha", false);
        const std::vector<float> init = varFloats(cur("value", Var()));
        float c[4] = {1.f, 1.f, 1.f, 1.f};
        for (size_t i = 0; i < std::min<size_t>(init.size(), 4); ++i) c[i] = init[i];
        const bool changed = alpha ? ImGui::ColorEdit4(widgetIdLabel(w).c_str(), c)
                                   : ImGui::ColorEdit3(widgetIdLabel(w).c_str(), c);
        if (enabled && changed && !id.empty()) {
            Array::Ptr arr = new Array();
            const int n = alpha ? 4 : 3;
            for (int i = 0; i < n; ++i) arr->add(static_cast<double>(c[i]));
            setWidgetValue(I, ed, id, Var(arr));
        }
        recordRect(ed, id);
    } else if (type == "checkbox") {
        bool b = varBool(cur("value", false), boolOf(w, "value", false));
        if (enabled && ImGui::Checkbox(widgetIdLabel(w).c_str(), &b) && !id.empty())
            setWidgetValue(I, ed, id, Var(b));
        recordRect(ed, id);
    } else if (type == "dropdown" || type == "listbox") {
        const std::vector<std::string> opts = varStrings(cur("options", Var()));
        std::vector<const char*> items;
        for (const auto& s : opts) items.push_back(s.c_str());
        std::string sel = varString(cur("value", strOf(w, "value")));
        int idx = 0;
        for (size_t i = 0; i < opts.size(); ++i)
            if (opts[i] == sel) idx = static_cast<int>(i);
        bool changed = false;
        if (type == "dropdown")
            changed = !opts.empty() && ImGui::Combo(widgetIdLabel(w).c_str(), &idx, items.data(),
                                                    static_cast<int>(items.size()));
        else
            changed = !opts.empty() && ImGui::ListBox(widgetIdLabel(w).c_str(), &idx, items.data(),
                                                      static_cast<int>(items.size()),
                                                      intOf(w, "heightItems", -1));
        if (enabled && changed && !id.empty() && idx >= 0 && idx < static_cast<int>(opts.size()))
            setWidgetValue(I, ed, id, Var(opts[static_cast<size_t>(idx)]));
        recordRect(ed, id);
    } else if (type == "button") {
        if (enabled && ImGui::Button(widgetIdLabel(w).c_str(), ImVec2(floatOf(w, "width", 0.f), 0.f))) {
            emitEvent(ed, "click", id, Var());
            const std::string cmd = strOf(w, "command", strOf(w, "action"));
            if (!cmd.empty() && !ed.vmName.empty() && I.vm) {
                auto it = I.vms.find(ed.vmName);
                if (it != I.vms.end()) {
                    std::string err;
                    callTableFunc(I.vm->getHandle(), it->second, vmFuncName(cmd),
                                  {Var(ed.id), Var(id)}, &err);
                    if (!err.empty())
                        fprintf(stderr, "eve.host: command %s: %s\n", cmd.c_str(), err.c_str());
                }
            }
        }
        recordRect(ed, id);
    } else if (type == "tree") {
        const bool open = boolOf(w, "open", true);
        if (ImGui::TreeNodeEx(widgetIdLabel(w).c_str(),
                              open ? ImGuiTreeNodeFlags_DefaultOpen : 0)) {
            renderChildren(I, ed,
                           w->has("children") ? w->getArray("children") : nullptr, ed.layout);
            ImGui::TreePop();
        }
        recordRect(ed, id);
    } else if (type == "group") {
        const bool border = boolOf(w, "border", true);
        if (border) {
            if (ImGui::BeginChild(widgetIdLabel(w).c_str(),
                                  ImVec2(floatOf(w, "width", 0.f), floatOf(w, "height", 0.f)), true)) {
                if (!label.empty()) ImGui::TextUnformatted(label.c_str());
                renderChildren(I, ed,
                               w->has("children") ? w->getArray("children") : nullptr,
                               strOf(w, "layout", "vertical"));
            }
            ImGui::EndChild();
        } else {
            ImGui::BeginGroup();
            if (!label.empty()) ImGui::TextUnformatted(label.c_str());
            renderChildren(I, ed,
                           w->has("children") ? w->getArray("children") : nullptr,
                           strOf(w, "layout", "vertical"));
            ImGui::EndGroup();
        }
        recordRect(ed, id);
    } else if (type == "tabs") {
        Array::Ptr tabs = w->has("children") ? w->getArray("children") : nullptr;
        if (tabs && ImGui::BeginTabBar((id.empty() ? "tabs" : id).c_str())) {
            for (size_t i = 0; i < tabs->size(); ++i) {
                Object::Ptr tab = tabs->getObject(static_cast<unsigned>(i));
                if (!tab) continue;
                if (ImGui::BeginTabItem(strOf(tab, "label", "Tab").c_str())) {
                    renderChildren(I, ed,
                                   tab->has("children") ? tab->getArray("children") : nullptr,
                                   strOf(tab, "layout", "vertical"));
                    ImGui::EndTabItem();
                }
            }
            ImGui::EndTabBar();
        }
    } else if (type == "tab") {
        renderChildren(I, ed, w->has("children") ? w->getArray("children") : nullptr,
                       strOf(w, "layout", "vertical"));
    } else if (type == "table") {
        const std::vector<std::string> cols =
            w->has("columns") ? varStrings(w->get("columns")) : std::vector<std::string>();
        Array::Ptr rows = w->has("rows") ? w->getArray("rows") : nullptr;
        if (!cols.empty() && ImGui::BeginTable((id.empty() ? "table" : id).c_str(),
                                               static_cast<int>(cols.size()),
                                               ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
            for (const auto& c : cols) ImGui::TableSetupColumn(c.c_str());
            ImGui::TableHeadersRow();
            if (rows) {
                for (size_t r = 0; r < rows->size(); ++r) {
                    ImGui::TableNextRow();
                    Array::Ptr cells = rows->getArray(static_cast<unsigned>(r));
                    if (!cells) continue;
                    for (size_t c = 0; c < cells->size(); ++c) {
                        ImGui::TableSetColumnIndex(static_cast<int>(c));
                        ImGui::TextUnformatted(
                            varString(cells->get(static_cast<unsigned int>(c))).c_str());
                    }
                }
            }
            ImGui::EndTable();
        }
        recordRect(ed, id);
    } else if (type == "viewport") {
        const float vw = floatOf(w, "width", 0.f);
        const float vh = floatOf(w, "height", 120.f);
        const std::vector<float> bg =
            w->has("bg") ? varFloats(w->get("bg")) : std::vector<float>();
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        ImVec2 size(vw > 0.f ? vw : ImGui::GetContentRegionAvail().x, vh);
        ImU32 col = IM_COL32(10, 12, 16, 255);
        if (bg.size() >= 3)
            col = IM_COL32(static_cast<int>(bg[0] * 255.f), static_cast<int>(bg[1] * 255.f),
                           static_cast<int>(bg[2] * 255.f), 255);
        ImGui::GetWindowDrawList()->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), col);
        if (ImGui::BeginChild(widgetIdLabel(w).c_str(), size, true)) {
            if (!label.empty()) ImGui::TextUnformatted(label.c_str());
            // AI scripts draw inside here using eve.host.widgetRect().
        }
        ImGui::EndChild();
        if (!id.empty()) {
            ed.rectMin[id] = ImGui::GetItemRectMin();
            ed.rectMax[id] = ImGui::GetItemRectMax();
        }
    }

    const std::string tip = strOf(w, "tooltip");
    if (!tip.empty() && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip.c_str());
    if (!enabled) {
        ImGui::PopStyleVar();
    }
}

void renderEditor(EditorHost::Impl& I, Editor& ed) {
    if (ed.removed) return;
    if (ed.width > 0.f)
        ImGui::SetNextWindowSize(ImVec2(ed.width, ed.height), ImGuiCond_FirstUseEver);
    if (ed.x != 0.f || ed.y != 0.f)
        ImGui::SetNextWindowPos(ImVec2(ed.x, ed.y), ImGuiCond_FirstUseEver);

    ImVec4 accent(0.35f, 0.60f, 1.00f, 1.f), bg(0.10f, 0.11f, 0.13f, 1.f);
    ImVec4 panel(0.14f, 0.15f, 0.18f, 1.f), text(0.92f, 0.93f, 0.95f, 1.f);
    float radius = 4.f, fontScale = 1.f;
    if (ed.theme) {
        if (ed.theme->has("accent")) {
            const auto c = varFloats(ed.theme->get("accent"));
            if (c.size() >= 3) accent = ImVec4(c[0], c[1], c[2], 1.f);
        }
        if (ed.theme->has("bg")) {
            const auto c = varFloats(ed.theme->get("bg"));
            if (c.size() >= 3) bg = ImVec4(c[0], c[1], c[2], 1.f);
        }
        if (ed.theme->has("panel")) {
            const auto c = varFloats(ed.theme->get("panel"));
            if (c.size() >= 3) panel = ImVec4(c[0], c[1], c[2], 1.f);
        }
        if (ed.theme->has("text")) {
            const auto c = varFloats(ed.theme->get("text"));
            if (c.size() >= 3) text = ImVec4(c[0], c[1], c[2], 1.f);
        }
        radius    = floatOf(ed.theme, "radius", 4.f);
        fontScale = floatOf(ed.theme, "fontScale", 1.f);
        if (strOf(ed.theme, "preset") == "light") {
            bg    = ImVec4(0.93f, 0.93f, 0.93f, 1.f);
            panel = ImVec4(0.84f, 0.84f, 0.86f, 1.f);
            text  = ImVec4(0.10f, 0.10f, 0.12f, 1.f);
        }
    }

    ImGui::PushStyleColor(ImGuiCol_WindowBg, bg);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, panel);
    ImGui::PushStyleColor(ImGuiCol_Text, text);
    ImGui::PushStyleColor(ImGuiCol_Button, accent);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                          ImVec4(accent.x + 0.08f, accent.y + 0.08f, accent.z + 0.08f, 1.f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                          ImVec4(accent.x - 0.05f, accent.y - 0.05f, accent.z - 0.05f, 1.f));
    ImGui::PushStyleColor(ImGuiCol_Header, accent);
    ImGui::PushStyleColor(ImGuiCol_SliderGrab, accent);
    ImGui::PushStyleColor(ImGuiCol_CheckMark, accent);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, radius);
    ImGui::PushStyleVar(ImGuiStyleVar_GrabRounding, radius);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, radius);

    bool open = true;
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoSavedSettings;
    if (!ed.resizable) flags |= ImGuiWindowFlags_NoResize;
    if (!ed.collapsible) flags |= ImGuiWindowFlags_NoCollapse;
    if (ImGui::Begin((ed.title + "##" + ed.id).c_str(), &open, flags)) {
        if (fontScale != 1.f) ImGui::SetWindowFontScale(fontScale);
        renderChildren(I, ed,
                       (ed.view && ed.view->has("children")) ? ed.view->getArray("children")
                                                             : nullptr,
                       ed.layout);
    }
    if (!open) {
        emitEvent(ed, "closed", "", Var());
        ed.removed = true;
    }
    ImGui::End();
    if (fontScale != 1.f) ImGui::SetWindowFontScale(1.f);
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(9);
}

}  // namespace

void EditorHost::renderImGui() {
    if (!impl_ || !impl_->windowOpen || !impl_->ui) return;
    auto& I = *impl_;
    for (auto& [id, ed] : I.editors) {
        syncVMToView(I, ed);
        renderEditor(I, ed);
    }
    for (auto it = I.editors.begin(); it != I.editors.end();) {
        if (it->second.removed) it = I.editors.erase(it);
        else ++it;
    }
}

void EditorHost::frame() {
    if (!impl_ || !impl_->windowOpen || !impl_->vm) return;
    auto& I = *impl_;
    I.inFrame = true;
    if (!I.event) I.event = ModuleManager::requireInstance<eve::event::Event>("Event");
    if (!I.timer) I.timer = ModuleManager::requireInstance<eve::timer::Timer>("Timer");
    if (!I.gfx) {
        I.inFrame = false;
        return;
    }

    I.event->pump();
    while (eve::event::Message* m = I.event->poll()) {
        const std::string name = m->name;
        delete m;
        if (name == "quit") {
            closeWindow();
            requestExit();
            I.inFrame = false;
            return;
        }
    }

    const float dt = I.timer ? I.timer->step() : 0.f;
    callRootFunc(I.vm->getHandle(), "eve_host_update", {Var(static_cast<double>(dt))});
    I.gfx->clearScreen();
    callRootFunc(I.vm->getHandle(), "eve_host_render", {});
    if (I.ui) {
        I.ui->beginFrameAndRender();
        renderImGui();
    }
    I.gfx->present();
    if (I.ui) I.ui->dispatchEvents();
    I.inFrame = false;
}

namespace {

Var parseEditorValue(const std::string& jsonValue) {
    Var v;
    try {
        Poco::JSON::Parser p;
        v = p.parse(jsonValue);
    } catch (...) {
        // Poco's parser wants a top-level JSON value; some clients pass bare
        // numbers/booleans without JSON quoting. Handle those explicitly.
        if (jsonValue == "true")
            v = Var(true);
        else if (jsonValue == "false")
            v = Var(false);
        else if (jsonValue == "null")
            v = Var();
        else {
            char*        end = nullptr;
            const double d   = std::strtod(jsonValue.c_str(), &end);
            if (end && end != jsonValue.c_str() && *end == '\0')
                v = Var(d);
            else
                v = Var(jsonValue);  // plain text
        }
    }
    return v;
}

}  // namespace

std::string EditorHost::setEditorValue(const std::string& editorId, const std::string& widgetId,
                                       const std::string& jsonValue) {
    if (!impl_) return "error: host not started";
    auto it = impl_->editors.find(editorId);
    if (it == impl_->editors.end()) return "error: editor not found: " + editorId;
    if (widgetId.empty()) return "error: missing widget";
    const Var v = parseEditorValue(jsonValue);
    setWidgetValue(*impl_, it->second, widgetId, v);
    return "ok";
}

std::string EditorHost::restoreEditorValue(const std::string& editorId, const std::string& widgetId,
                                           const std::string& jsonValue) {
    if (!impl_ || !impl_->vm) return "error: host not started";
    auto it = impl_->editors.find(editorId);
    if (it == impl_->editors.end()) return "error: editor not found: " + editorId;
    if (widgetId.empty()) return "error: missing widget";
    const Var v                 = parseEditorValue(jsonValue);
    it->second.values[widgetId] = v;
    auto       vmIt             = impl_->vms.find(it->second.vmName);
    const auto widget           = findWidget(it->second.view, widgetId);
    if (vmIt != impl_->vms.end() && widget) {
        const std::string bind = strOf(widget, "bind");
        if (!bind.empty()) writeVMVar(*impl_->vm, vmIt->second, bind, v);
    }
    return "ok";
}

std::string EditorHost::consumeEvents(const std::string& editorId) {
    if (!impl_) return "[]";
    Array::Ptr arr   = new Array();
    auto       drain = [&](Editor& ed) {
        for (const auto& e : ed.events) {
            Object::Ptr eo = new Object();
            eo->set("editor", ed.id);
            eo->set("widget", e.widget);
            eo->set("type", e.type);
            if (!e.value.isEmpty()) eo->set("value", e.value);
            arr->add(eo);
        }
        ed.events.clear();
    };
    if (!editorId.empty()) {
        auto it = impl_->editors.find(editorId);
        if (it == impl_->editors.end()) return "error: editor not found: " + editorId;
        drain(it->second);
    } else {
        for (auto& [k, ed] : impl_->editors) drain(ed);
    }
    return jsonStringify(Var(arr));
}

std::string EditorHost::widgetRect(const std::string& editorId, const std::string& widgetId) const {
    if (!impl_) return "{\"x\":0,\"y\":0,\"width\":0,\"height\":0}";
    auto it = impl_->editors.find(editorId);
    if (it == impl_->editors.end()) return "{\"x\":0,\"y\":0,\"width\":0,\"height\":0}";
    const Editor& ed = it->second;
    auto mn = ed.rectMin.find(widgetId), mx = ed.rectMax.find(widgetId);
    Object::Ptr o = new Object();
    if (mn != ed.rectMin.end() && mx != ed.rectMax.end()) {
        o->set("x", static_cast<double>(mn->second.x));
        o->set("y", static_cast<double>(mn->second.y));
        o->set("width", static_cast<double>(mx->second.x - mn->second.x));
        o->set("height", static_cast<double>(mx->second.y - mn->second.y));
    } else {
        o->set("x", 0);
        o->set("y", 0);
        o->set("width", 0);
        o->set("height", 0);
    }
    return jsonStringify(Var(o));
}

std::string EditorHost::registerVM(const std::string& name, const std::string& source) {
    if (!impl_ || !impl_->vm) return "error: host not started";
    if (name.empty()) return "error: missing name";
    const std::string err = runSquirrel(impl_->vm->getHandle(), source, "host_vm.nut");
    if (!err.empty()) return "error: " + err;
    try {
        ssq::Table tbl = impl_->vm->find(name.c_str()).toTable();
        impl_->vms[name] = tbl;
        impl_->vmSources[name] = source;
        return "ok";
    } catch (const std::exception& e) {
        return std::string("error: table '") + name + "' not found after run: " + e.what();
    }
}

std::string EditorHost::unregisterVM(const std::string& name) {
    if (!impl_) return "error: host not started";
    if (name.empty()) return "error: missing name";
    impl_->vms.erase(name);
    impl_->vmSources.erase(name);
    if (impl_->vm) {
        HSQUIRRELVM v = impl_->vm->getHandle();
        const SQInteger top = sq_gettop(v);
        sq_pushroottable(v);
        sq_pushstring(v, name.c_str(), static_cast<SQInteger>(name.size()));
        sq_deleteslot(v, -2, SQFalse);
        sq_pop(v, 1);
        sq_settop(v, top);
    }
    return "ok";
}

std::string EditorHost::saveEditor(const std::string& id) {
    if (!impl_) return "error: host not started";
    auto it = impl_->editors.find(id);
    if (it == impl_->editors.end()) return "error: editor not found: " + id;
    try {
        std::error_code ec;
        std::filesystem::path dir = std::filesystem::path(impl_->rootDir) / "editors";
        std::filesystem::create_directories(dir, ec);
        std::filesystem::path vp = dir / (id + ".editor.json");
        {
            std::ofstream ofs(vp, std::ios::trunc);
            if (!ofs) return "error: cannot write " + vp.string();
            ofs << jsonStringify(Var(it->second.view));
        }
        if (!it->second.vmName.empty()) {
            auto vs = impl_->vmSources.find(it->second.vmName);
            if (vs != impl_->vmSources.end()) {
                std::filesystem::path sp = dir / (id + ".vm.nut");
                std::ofstream ofs2(sp, std::ios::trunc);
                if (!ofs2) return "error: cannot write " + sp.string();
                ofs2 << vs->second;
            }
        }
        return "ok";
    } catch (const std::exception& e) {
        return std::string("error: ") + e.what();
    }
}

std::string EditorHost::unloadEditor(const std::string& id) {
    if (!impl_) return "error: host not started";
    auto it = impl_->editors.find(id);
    if (it == impl_->editors.end()) return "error: editor not found: " + id;
    impl_->editors.erase(it);
    return "ok";
}

void EditorHost::loadEditorsFromDisk() {
    if (!impl_) return;
    std::error_code ec;
    std::filesystem::path dir = std::filesystem::path(impl_->rootDir) / "editors";
    if (!std::filesystem::is_directory(dir, ec)) return;
    std::vector<std::pair<std::string, std::string>> pending;
    for (auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (ec) break;
        const std::filesystem::path p = entry.path();
        const std::string name = p.filename().string();
        const std::string suffix = ".editor.json";
        if (p.extension() != ".json" || name.size() <= suffix.size() ||
            name.compare(name.size() - suffix.size(), suffix.size(), suffix) != 0)
            continue;
        const std::string id = name.substr(0, name.size() - suffix.size());
        std::ifstream ifs(p);
        std::string json((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
        if (!json.empty()) pending.emplace_back(id, json);
    }
    for (const auto& [id, json] : pending) {
        try {
            Object::Ptr root = parseObject(json);
            const std::string vmName = strOf(root, "vm");
            if (!vmName.empty()) {
                std::ifstream ifs2(dir / (id + ".vm.nut"));
                std::string src((std::istreambuf_iterator<char>(ifs2)),
                                std::istreambuf_iterator<char>());
                if (!src.empty()) registerVM(vmName, src);
            }
        } catch (...) {
        }
        applyEditor(json);
    }
}

std::string EditorHost::runScript(const std::string& source) {
    if (!impl_ || !impl_->vm) return "error: host not started";
    if (source.empty()) return "error: missing source";
    const std::string err = runSquirrel(impl_->vm->getHandle(), source, "host_snippet.nut");
    return err.empty() ? "ok" : ("error: " + err);
}

std::string EditorHost::capture(const std::string& path) {
    if (!impl_ || !impl_->gfx) return "error: no window";
    auto& I = *impl_;
    try {
        std::string p = path.empty() ? "eve_host_capture.png" : path;
        I.gfx->setScreenReadbackEnabled(true);
        eve::image::ImageData* img = nullptr;
        try {
            img = I.gfx->newImageData();
        } catch (...) {
            // No presented frame yet (e.g. capture right after the window was
            // opened). Render one frame with readback enabled, then retry.
            if (I.inFrame) throw;  // cannot present reentrantly from a script hook
            frame();
            img = I.gfx->newImageData();
        }
        if (!img) return "error: readback returned no image";
        const int w = I.gfx->getPixelWidth();
        const int h = I.gfx->getPixelHeight();
        eve::filesystem::FileData* png =
            img->encode(eve::image::ImageData::FormatHandler::ENCODED_PNG, p.c_str(), false);
        delete img;
        if (!png) return "error: PNG encode failed";
        std::error_code ec;
        std::filesystem::create_directories(std::filesystem::path(p).parent_path(), ec);
        std::ofstream out(p, std::ios::binary | std::ios::trunc);
        if (!out.good()) {
            delete png;
            return "error: cannot write " + p;
        }
        out.write(static_cast<const char*>(png->getData()),
                  static_cast<std::streamsize>(png->getSize()));
        out.close();
        const bool wrote = out.good();
        delete png;
        if (!wrote) return "error: failed writing " + p;
        Object::Ptr o = new Object();
        o->set("path", p);
        o->set("width", w);
        o->set("height", h);
        return jsonStringify(Var(o));
    } catch (const std::exception& e) {
        return std::string("error: ") + e.what();
    }
}

std::string EditorHost::status() const {
    if (!impl_) return "{\"running\":false,\"windowOpen\":false,\"editors\":[],\"viewModels\":[]}";
    Object::Ptr o = new Object();
    o->set("running", running_);
    o->set("windowOpen", impl_->windowOpen);
    o->set("windowTitle", impl_->windowTitle);
    o->set("gameRoot", gameRoot_);
    Array::Ptr eds = new Array();
    for (const auto& [k, ed] : impl_->editors) {
        Object::Ptr e = new Object();
        e->set("id", k);
        e->set("title", ed.title);
        eds->add(e);
    }
    o->set("editors", eds);
    Array::Ptr vms = new Array();
    for (const auto& [k, v] : impl_->vms) vms->add(k);
    o->set("viewModels", vms);
    return jsonStringify(Var(o));
}

void EditorHost::exposeScriptApi(ssq::VM& vm) {
    try {
        ssq::Table eveTbl = vm.find("eve").toTable();
        ssq::Table host   = eveTbl.addTable("host");
        host.addFunc("status", []() { return EditorHost::instance().status(); });
        host.addFunc("openWindow", [](std::string t, int w, int h) {
            return EditorHost::instance().openWindow(t, w, h);
        });
        host.addFunc("closeWindow", []() { return EditorHost::instance().closeWindow(); });
        host.addFunc("windowState", []() { return EditorHost::instance().windowState(); });
        host.addFunc("applyEditor", [](std::string json) {
            return EditorHost::instance().applyEditor(json);
        });
        host.addFunc("removeEditor", [](std::string id) {
            return EditorHost::instance().removeEditor(id);
        });
        host.addFunc("setValue", [](std::string editor, std::string widget, std::string value) {
            return EditorHost::instance().setEditorValue(editor, widget, value);
        });
        host.addFunc("events", [](std::string editor) {
            return EditorHost::instance().consumeEvents(editor);
        });
        host.addFunc("registerVM", [](std::string name, std::string source) {
            return EditorHost::instance().registerVM(name, source);
        });
        host.addFunc("unregisterVM", [](std::string name) {
            return EditorHost::instance().unregisterVM(name);
        });
        host.addFunc("widgetRect", [](std::string editor, std::string widget) {
            return EditorHost::instance().widgetRect(editor, widget);
        });
        host.addFunc("capture", [](std::string path) {
            return EditorHost::instance().capture(path);
        });
        host.addFunc("save", [](std::string id) {
            return EditorHost::instance().saveEditor(id);
        });
        host.addFunc("runScript", [](std::string source) {
            return EditorHost::instance().runScript(source);
        });
        host.addFunc("reloadResource", [](std::string path) {
            return EditorHost::instance().reloadResource(path);
        });
        host.addFunc("hotReloadStatus", []() {
            return EditorHost::instance().hotReloadStatus();
        });
    } catch (...) {
        // eve table missing — the host surface is optional.
    }
}

}  // namespace eve::ui

#endif  // EVENGINE_WEBGPU && __EMSCRIPTEN__
