#include "ui/UI.h"
#include "ui/DatabasePanel.h"
#include "ui/EditorShell.h"
#include "ui/EditorHostCapabilities.h"
#include "ui/UIAutomationCapabilities.h"

#include "ui/Inspector.h"
#include "ui/ScenePanel.h"
#include "ui/Theme.h"
#include "ui/UISystem.h"
#include "ui/Widget.h"

#include "common/config.h"
#include "common/Module.h"
#include "graphics/Graphics.h"
#include "window/Window.h"
#include "window/sdl/Window.h"

#include <simplesquirrel/simplesquirrel.hpp>

#if !(defined(EVENGINE_WEBGPU) && defined(__EMSCRIPTEN__))
#include <Poco/JSON/Array.h>
#include <Poco/JSON/Object.h>
#include <Poco/JSON/Parser.h>
#include <Poco/JSON/Stringifier.h>
#endif

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <stdexcept>

namespace eve::ui {
namespace {

/** Call a registered script handler with the event's payload. */
void callScriptHandler(ssq::Function &fn, const std::string &kind, const UIEvent &ev) {
    HSQUIRRELVM vm = fn.getHandle();
    if (!vm) return;
    const SQInteger top = sq_gettop(vm);
    sq_pushobject(vm, fn.getRaw());
    sq_pushroottable(vm);  // env
    if (kind == "click") {
        if (SQ_FAILED(sq_call(vm, 1, SQFalse, SQTrue))) {
            sq_settop(vm, top);
            return;
        }
    } else {
        sq_pushstring(vm, kind.c_str(), -1);
        if (kind == "toggle") sq_pushbool(vm, ev.toggleValue ? SQTrue : SQFalse);
        else if (kind == "value") sq_pushfloat(vm, ev.floatValue);
        else sq_pushstring(vm, ev.textValue.c_str(), -1);
        if (SQ_FAILED(sq_call(vm, 3, SQFalse, SQTrue))) {
            sq_settop(vm, top);
            return;
        }
    }
    sq_settop(vm, top);
}

#if !(defined(EVENGINE_WEBGPU) && defined(__EMSCRIPTEN__))
// JSON UI asset helpers (defined below saveTreeJson/loadTreeJson).
void nodeToJson(const UIHost::Tree &tree, const UINode &n, Poco::JSON::Object &o);
WidgetDesc descFromJson(const Poco::JSON::Object &o);
#endif

/** Script base: `class X extends eve.UIComponent { function build() { ... } }`. */
const char *kUIComponentScript = R"SQ(
eve.UIComponent <- class {
    hostName = ""
    dirty = true
    forceFull = false
    _ui = null

    constructor(uiInstance = null) {
        _ui = uiInstance
        hostName = ""
        dirty = true
        forceFull = false
    }

    function setUI(uiInstance) { _ui = uiInstance }

    function mountAs(name) {
        hostName = name
        dirty = true
        forceFull = true
        updateIfDirty()
    }

    function setState() { dirty = true }
    function markDirty() { dirty = true }

    // Override in subclass: call this.ui().beginWindow / text / button / end ...
    function build() {}

    function ui() {
        if (_ui != null) return _ui
        try {
            if (::ui != null) return ::ui
        } catch (e) {}
        _ui = ::eve.UI()
        return _ui
    }

    function updateIfDirty() {
        if (!dirty) return false
        local u = ui()
        u.beginBuild()
        build()
        local name = hostName
        if (name == null || name == "") name = "default"
        if (forceFull) {
            u.mountBuildAs(name)
            forceFull = false
        } else {
            u.remountBuildAs(name)
        }
        dirty = false
        return true
    }

    function rebuild(force = false) {
        dirty = true
        forceFull = force
        return updateIfDirty()
    }
}
// Note: eve.Component is reserved for script ECS (see exposeECS). Use eve.UIComponent.
)SQ";

void injectUIComponentClass(ssq::Table &eveTable) {
    HSQUIRRELVM vm = eveTable.getHandle();
    const SQInteger top = sq_gettop(vm);
    if (SQ_FAILED(sq_compilebuffer(vm, kUIComponentScript,
                                   static_cast<SQInteger>(std::strlen(kUIComponentScript)),
                                   "UIComponent.nut", SQTrue))) {
        sq_settop(vm, top);
        return;
    }
    sq_pushroottable(vm);
    sq_call(vm, 1, SQFalse, SQTrue);
    sq_settop(vm, top);
}

}  // namespace

Module_IMPL(UI, new UI());

UI::UI() : backend_(createImGuiBackend()) {
    registerEditorHostCapabilities();
    registerUIAutomationCapabilities();
}
UI::~UI() { shutdownBackend(); }

bool UI::isBackendReady() const { return backend_ && backend_->isInitialized(); }

bool UI::initBackend() {
    if (!backend_) backend_ = createImGuiBackend();
    if (backend_->isInitialized()) return true;
    auto *win = eve::ModuleManager::getInstance<eve::window::Window>("Window");
    auto *gfx = eve::ModuleManager::getInstance<eve::graphics::Graphics>("Graphics");
    if (!win || !gfx) return false;
    auto *sdlWin = dynamic_cast<eve::window::sdl::Window *>(win);
    if (!sdlWin) return false;
    auto *native = static_cast<SDL_Window *>(sdlWin->getHandle());
    if (!native) return false;
    const bool ok = backend_->init(native, gfx);
    if (ok) UISystem::setBackend(backend_.get());
    return ok;
}

void UI::shutdownBackend() {
    if (backend_) backend_->shutdown();
}

void UI::processEvent(const SDL_Event *event) {
    if (backend_) backend_->processEvent(event);
}

void UI::beginFrameAndRender() {
    if (!isBackendReady()) {
        if (!initBackend()) return;
    }
    updateHostTweens();
    if (inspector_ && inspector_->isOpen()) inspector_->sync();
    if (databasePanel_ && databasePanel_->isOpen()) databasePanel_->sync();
    backend_->newFrame();
    UISystem::render();
}

void UI::updateHostTweens() {
    if (hostTweens_.empty()) return;
    const double now =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch())
            .count();
    for (auto &t : hostTweens_) {
        if (!t.host) continue;
        auto m = t.host->meta();
        const double elapsed = now - t.startMs;
        if (t.durationMs <= 0.0 || elapsed >= t.durationMs) {
            m->hasPos = true;
            m->posX = t.toX;
            m->posY = t.toY;
            t.host = nullptr;  // done; removed below
            continue;
        }
        const float k = float(elapsed / t.durationMs);
        const float ease = k * k * (3.f - 2.f * k);  // smoothstep
        m->hasPos = true;
        m->posX = t.fromX + (t.toX - t.fromX) * ease;
        m->posY = t.fromY + (t.toY - t.fromY) * ease;
    }
    hostTweens_.erase(std::remove_if(hostTweens_.begin(), hostTweens_.end(),
                                     [](const HostTween &t) { return t.host == nullptr; }),
                      hostTweens_.end());
}

void UI::dispatchEvents() {
    // Copy before dispatch: UISystem::dispatchEvents() consumes the pending list.
    const std::vector<UIEvent> events = UISystem::pendingEvents();
    UISystem::dispatchEvents();
    for (const auto &ev : events) {
        if (!ev.host) continue;
        fireScriptHandlers(ev);
    }
}

void UI::fireScriptHandlers(const UIEvent &ev) {
    for (const auto &h : scriptHandlers_) {
        if (h.hostName != ev.hostName || h.nodeId != ev.nodeId) continue;
        if (h.kind == "click" && ev.kind == "click") {
            ssq::Function fn = h.fn;
            callScriptHandler(fn, "click", ev);
        } else if (h.kind == ev.kind &&
                   (ev.kind == "toggle" || ev.kind == "value" || ev.kind == "text")) {
            ssq::Function fn = h.fn;
            callScriptHandler(fn, ev.kind, ev);
        }
    }
}

void UI::onClick(const std::string &id, ssq::Function fn) {
    if (!selected_ || id.empty()) return;
    scriptHandlers_.push_back(
        ScriptHandler(selected_->getName(), id, "click", std::move(fn)));
}

void UI::onChange(const std::string &id, ssq::Function fn) {
    if (!selected_ || id.empty()) return;
    for (const char *kind : {"toggle", "value", "text"}) {
        scriptHandlers_.push_back(ScriptHandler(selected_->getName(), id, kind, fn));
    }
}

bool UI::wantCaptureMouse() const {
    return backend_ ? backend_->wantCaptureMouse() : false;
}

bool UI::wantCaptureKeyboard() const {
    return backend_ ? backend_->wantCaptureKeyboard() : false;
}

UIHost *UI::findHost(const std::string &name) const { return UISystem::findHost(name); }

UIHost *UI::findHostByOwner(uint32_t ownerId) const {
    return UISystem::findHostByOwner(ownerId);
}

bool UI::select(const std::string &name) {
    UIHost *h = findHost(name);
    if (!h) return false;
    selected_ = h;
    return true;
}

void UI::bindOwner(uint32_t ownerId) {
    if (selected_) selected_->setOwnerId(ownerId);
}

UIHost *UI::ensureSelected(const std::string &preferredName) {
    if (selected_) return selected_;
    if (!preferredName.empty()) {
        if (UIHost *h = findHost(preferredName)) {
            selected_ = h;
            return selected_;
        }
        selected_ = UIHost::createHost(preferredName);
        return selected_;
    }
    selected_ = UIHost::createHost("default");
    return selected_;
}

UIHost *UI::mountAs(const std::string &name, WidgetDesc root) {
    UIHost *h = findHost(name);
    if (!h) h = UIHost::createHost(name);
    else h->setName(name);
    h->setTree(std::move(root));
    selected_ = h;
    return h;
}

UIHost *UI::mount(WidgetDesc root) {
    if (selected_) {
        selected_->setTree(std::move(root));
        return selected_;
    }
    return mountAs("default", std::move(root));
}

UIHost *UI::remount(WidgetDesc root) {
    UIHost *h = ensureSelected();
    h->setTree(std::move(root));
    return h;
}

UIHost *UI::remountReconcile(WidgetDesc root) {
    UIHost *h = ensureSelected();
    h->setTreeReconcile(std::move(root));
    return h;
}

UIHost *UI::remountAs(const std::string &name, WidgetDesc root) {
    return mountAs(name, std::move(root));
}

void UI::beginBuild() {
    openStack_.clear();
    hasBuiltRoot_ = false;
    builtRoot_ = WidgetDesc{};
}

void UI::pushOpen(WidgetDesc d) { openStack_.push_back(std::move(d)); }

WidgetDesc &UI::currentParent() {
    if (openStack_.empty()) throw std::runtime_error("ui: widget outside beginWindow/beginGroup");
    return openStack_.back();
}

void UI::beginWindow(const std::string &title, const std::string &id) {
    if (openStack_.empty() && hasBuiltRoot_) beginBuild();
    pushOpen(window(title, {}, id));
}

void UI::beginGroup(const std::string &id) { pushOpen(group({}, id)); }

void UI::beginList(const std::string &id) { pushOpen(group({}, id)); }

void UI::beginCollapsing(const std::string &label, const std::string &id, bool open) {
    pushOpen(collapsingHeader(label, {}, id, open));
}

void UI::beginChild(const std::string &id, float width, float height) {
    pushOpen(child(id, {}, width, height));
}

void UI::beginCard(const std::string &id) { pushOpen(card({}, id)); }

void UI::beginMenuBar(const std::string &id) { pushOpen(menuBar({}, id)); }

void UI::beginMenu(const std::string &label, const std::string &id) {
    pushOpen(menu(label, {}, id));
}

void UI::beginToolbar(const std::string &id) { pushOpen(toolbar({}, id)); }

void UI::beginToolbox(const std::string &id, float cellSize, int columns) {
    pushOpen(toolbox({}, id, cellSize, columns));
}

void UI::beginSidebar(const std::string &id, float width) {
    pushOpen(sidebar({}, id, width));
}

void UI::beginStatusBar(const std::string &id) { pushOpen(statusBar({}, id)); }

void UI::beginScrollList(const std::string &id, float height, float itemHeight) {
    pushOpen(scrollList(id, {}, height, itemHeight));
}

namespace {

std::string toLowerCopy(std::string s) {
    for (char &c : s) {
        if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
    }
    return s;
}

FlexDirection parseFlexDirection(const std::string &direction) {
    const std::string d = toLowerCopy(direction);
    if (d == "column" || d == "col" || d == "vertical" || d == "v") return FlexDirection::Column;
    return FlexDirection::Row;
}

FlexAlign parseFlexAlign(const std::string &align) {
    const std::string a = toLowerCopy(align);
    if (a == "center") return FlexAlign::Center;
    if (a == "end" || a == "right" || a == "bottom") return FlexAlign::End;
    if (a == "stretch") return FlexAlign::Stretch;
    return FlexAlign::Start;
}

FlexJustify parseFlexJustify(const std::string &justify) {
    const std::string j = toLowerCopy(justify);
    if (j == "center") return FlexJustify::Center;
    if (j == "end" || j == "right" || j == "bottom") return FlexJustify::End;
    if (j == "spacebetween" || j == "space-between" || j == "between")
        return FlexJustify::SpaceBetween;
    if (j == "spacearound" || j == "space-around" || j == "around") return FlexJustify::SpaceAround;
    return FlexJustify::Start;
}

}  // namespace

void UI::beginFlex(const std::string &direction, const std::string &id, float gap) {
    WidgetDesc d = flex(parseFlexDirection(direction), {}, id);
    d.gap = gap;
    pushOpen(std::move(d));
}

void UI::beginRow(const std::string &id, float gap) { beginFlex("row", id, gap); }

void UI::beginColumn(const std::string &id, float gap) { beginFlex("column", id, gap); }

void UI::beginSplitPane(const std::string &direction, float ratio, const std::string &id) {
    WidgetDesc d;
    d.type = NodeType::SplitPane;
    d.id = id;
    d.key = id;
    d.flexDirection = parseFlexDirection(direction);
    d.value = std::max(0.1f, std::min(0.9f, ratio));
    d.minValue = 0.1f;
    d.maxValue = 0.9f;
    pushOpen(std::move(d));
}

void UI::end() {
    if (openStack_.empty()) throw std::runtime_error("ui: end() without begin");
    WidgetDesc finished = std::move(openStack_.back());
    openStack_.pop_back();
    if (openStack_.empty()) {
        builtRoot_ = std::move(finished);
        hasBuiltRoot_ = true;
    } else {
        openStack_.back().children.push_back(std::move(finished));
    }
}

void UI::addText(const std::string &content, const std::string &id) {
    currentParent().children.push_back(text(content, id));
}

void UI::addTextWrapped(const std::string &content, float width, const std::string &id) {
    WidgetDesc d = text(content, id);
    d.wrapWidth = width;
    currentParent().children.push_back(std::move(d));
}

void UI::addButton(const std::string &label, const std::string &id) {
    currentParent().children.push_back(button(label, id));
}

void UI::addIcon(const std::string &name, const std::string &id) {
    Icon value = Icon::None;
    iconFromName(name, &value);
    currentParent().children.push_back(icon(value, id));
}

void UI::addIconButton(const std::string &name, const std::string &label, const std::string &id) {
    Icon value = Icon::None;
    iconFromName(name, &value);
    currentParent().children.push_back(iconButton(value, label, id));
}

void UI::addSameLine(const std::string &id) { currentParent().children.push_back(sameLine(id)); }

void UI::addSeparator(const std::string &id) {
    currentParent().children.push_back(separator(id));
}

void UI::addCheckbox(const std::string &label, bool checked, const std::string &id) {
    currentParent().children.push_back(checkbox(label, checked, id));
}

void UI::addSlider(const std::string &label, float value, float minV, float maxV,
                   const std::string &id) {
    currentParent().children.push_back(slider(label, value, minV, maxV, id));
}

void UI::addProgress(float fraction, const std::string &id, const std::string &overlay) {
    currentParent().children.push_back(progress(fraction, id, overlay));
}

void UI::addImage(const std::string &id, float width, float height) {
    currentParent().children.push_back(image(id, width, height));
}

void UI::addImageButton(const std::string &id, float width, float height) {
    currentParent().children.push_back(imageButton(id, width, height));
}

void UI::addViewport(const std::string &id, float width, float height) {
    currentParent().children.push_back(viewport(id, width, height));
}

void UI::addCombo(const std::string &label, const std::string &options, int selected,
                  const std::string &id) {
    std::vector<std::string> items;
    size_t start = 0;
    while (start <= options.size()) {
        const size_t end = options.find('\n', start);
        items.push_back(options.substr(start, end == std::string::npos ? std::string::npos
                                                                       : end - start));
        if (end == std::string::npos) break;
        start = end + 1;
    }
    currentParent().children.push_back(combo(label, items, selected, id));
}

void UI::addInputText(const std::string &label, const std::string &value, const std::string &id) {
    currentParent().children.push_back(inputText(label, value, id));
}

void UI::addSearchField(const std::string &hint, const std::string &value,
                        const std::string &id) {
    currentParent().children.push_back(searchField(hint, value, id));
}

void UI::addSwitch(const std::string &label, bool checked, const std::string &id) {
    currentParent().children.push_back(toggleSwitch(label, checked, id));
}

void UI::addBadge(const std::string &label, const std::string &id) {
    currentParent().children.push_back(badge(label, id));
}

void UI::addSectionHeader(const std::string &label, const std::string &id) {
    currentParent().children.push_back(sectionHeader(label, id));
}

void UI::addMenuItem(const std::string &label, const std::string &shortcut,
                     const std::string &id) {
    currentParent().children.push_back(menuItem(label, shortcut, id));
}

void UI::addSpacer(const std::string &id, float grow) {
    currentParent().children.push_back(spacer(id, grow));
}

void UI::setItemFlexGrow(float grow) {
    WidgetDesc &parent = currentParent();
    if (parent.children.empty()) return;
    parent.children.back().flexGrow = grow;
}

void UI::setItemSize(float width, float height) {
    WidgetDesc &parent = currentParent();
    if (parent.children.empty()) return;
    parent.children.back().sizeX = width;
    parent.children.back().sizeY = height;
}

void UI::setItemMargin(float l, float t, float r, float b) {
    WidgetDesc &parent = currentParent();
    if (parent.children.empty()) return;
    parent.children.back().marginL = l;
    parent.children.back().marginT = t;
    parent.children.back().marginR = r;
    parent.children.back().marginB = b;
}

void UI::setItemPadding(float l, float t, float r, float b) {
    WidgetDesc &parent = currentParent();
    if (parent.children.empty()) return;
    parent.children.back().paddingL = l;
    parent.children.back().paddingT = t;
    parent.children.back().paddingR = r;
    parent.children.back().paddingB = b;
}

void UI::setItemMinSize(float w, float h) {
    WidgetDesc &parent = currentParent();
    if (parent.children.empty()) return;
    parent.children.back().minSizeX = w;
    parent.children.back().minSizeY = h;
}

void UI::setItemMaxSize(float w, float h) {
    WidgetDesc &parent = currentParent();
    if (parent.children.empty()) return;
    parent.children.back().maxSizeX = w;
    parent.children.back().maxSizeY = h;
}

void UI::setItemPercent(float w, float h) {
    WidgetDesc &parent = currentParent();
    if (parent.children.empty()) return;
    parent.children.back().percentW = w;
    parent.children.back().percentH = h;
}

void UI::setItemAbsolute(float anchorX, float anchorY, float x, float y) {
    WidgetDesc &parent = currentParent();
    if (parent.children.empty()) return;
    parent.children.back().absolute = true;
    parent.children.back().anchorX = anchorX;
    parent.children.back().anchorY = anchorY;
    parent.children.back().posX = x;
    parent.children.back().posY = y;
}

void UI::setItemTooltip(const std::string &text) {
    WidgetDesc &parent = currentParent();
    if (parent.children.empty()) return;
    parent.children.back().tooltip = text;
}

void UI::setFlexAlign(const std::string &align) {
    WidgetDesc &parent = currentParent();
    if (parent.type != NodeType::Flex) return;
    parent.alignItems = parseFlexAlign(align);
}

void UI::setFlexJustify(const std::string &justify) {
    WidgetDesc &parent = currentParent();
    if (parent.type != NodeType::Flex) return;
    parent.justifyContent = parseFlexJustify(justify);
}

void UI::addListItem(const std::string &label, const std::string &id) {
    WidgetDesc &parent = currentParent();
    std::string itemId = id;
    if (itemId.empty())
        itemId = parent.id + "/" + std::to_string(parent.children.size());
    parent.children.push_back(button(label, itemId).withKey(itemId));
}

bool UI::buildComplete() const { return openStack_.empty() && hasBuiltRoot_; }

bool UI::mountBuild() {
    if (!buildComplete()) return false;
    remount(std::move(builtRoot_));
    hasBuiltRoot_ = false;
    builtRoot_ = WidgetDesc{};
    return true;
}

bool UI::mountBuildAs(const std::string &name) {
    if (!buildComplete()) return false;
    mountAs(name, std::move(builtRoot_));
    hasBuiltRoot_ = false;
    builtRoot_ = WidgetDesc{};
    return true;
}

bool UI::remountBuildAs(const std::string &name) {
    if (!buildComplete()) return false;
    UIHost *h = findHost(name);
    if (!h) h = UIHost::createHost(name);
    h->setTreeReconcile(std::move(builtRoot_));
    selected_ = h;
    hasBuiltRoot_ = false;
    builtRoot_ = WidgetDesc{};
    return true;
}

bool UI::setListItems(const std::string &listId, const std::vector<std::string> &items) {
    if (!selected_) return false;
    WidgetDesc listNode = listButtons(listId, items);
    auto *existing = selected_->findById(listId);
    if (existing && existing->type == NodeType::Group) {
        selected_->setTreeReconcile(
            window(selected_->getName().empty() ? "List" : selected_->getName(),
                   {std::move(listNode)}, "root"));
        return true;
    }
    selected_->setTree(
        window(selected_->getName().empty() ? "List" : selected_->getName(), {std::move(listNode)},
               "root"));
    return true;
}

void UI::setText(const std::string &id, const std::string &text) {
    if (selected_) selected_->setTextById(id, text);
}

void UI::setTextWrap(const std::string &id, float width) {
    if (!selected_) return;
    if (auto *n = selected_->findById(id)) n->wrapWidth = width;
}

void UI::setVisible(const std::string &id, bool visible) {
    if (selected_) selected_->setVisibleById(id, visible);
}

void UI::setChecked(const std::string &id, bool checked) {
    if (selected_) selected_->setCheckedById(id, checked);
}

void UI::setValue(const std::string &id, float value) {
    if (selected_) selected_->setValueById(id, value);
}

void UI::setValueText(const std::string &id, const std::string &value) {
    if (selected_) selected_->setValueTextById(id, value);
}

void UI::setImageTint(const std::string &id, float r, float g, float b, float a) {
    if (!selected_) return;
    if (auto *n = selected_->findById(id)) {
        n->tintR = r;
        n->tintG = g;
        n->tintB = b;
        n->tintA = a;
    }
}

void UI::setImageUv(const std::string &id, float u0, float v0, float u1, float v1) {
    if (!selected_) return;
    if (auto *n = selected_->findById(id)) {
        n->uv0x = u0;
        n->uv0y = v0;
        n->uv1x = u1;
        n->uv1y = v1;
    }
}

void UI::setImageNinePatch(const std::string &id, float l, float t, float r, float b) {
    if (!selected_) return;
    if (auto *n = selected_->findById(id)) {
        n->borderL = l;
        n->borderT = t;
        n->borderR = r;
        n->borderB = b;
    }
}

void UI::setImageCornerRadius(const std::string &id, float radius) {
    if (!selected_) return;
    if (auto *n = selected_->findById(id)) n->cornerRadius = radius;
}

void UI::setImageTextureId(const std::string &id, uint64_t textureId) {
    if (!selected_) return;
    if (auto *n = selected_->findById(id)) n->textureId = textureId;
}

uint64_t UI::registerTexture(graphics::Texture *tex) {
    if (!isBackendReady()) {
        if (!initBackend()) return 0;
    }
    return backend_ ? backend_->registerTexture(tex) : 0;
}

float UI::getValue(const std::string &id) const {
    if (!selected_) return 0.f;
    if (auto *n = selected_->findById(id)) return n->value;
    return 0.f;
}

std::string UI::getValueText(const std::string &id) const {
    if (!selected_) return {};
    if (auto *n = selected_->findById(id)) return n->valueText;
    return {};
}

bool UI::getChecked(const std::string &id) const {
    if (!selected_) return false;
    if (auto *n = selected_->findById(id)) return n->checked;
    return false;
}

void UI::setHostVisible(bool visible) {
    if (selected_) selected_->setVisible(visible);
}

void UI::setHostLayer(int layer) {
    if (selected_) selected_->setLayer(layer);
}

void UI::setHostModal(bool modal) {
    if (selected_) selected_->setModal(modal);
}

void UI::setHostOverlay(bool overlay) {
    if (selected_) selected_->meta()->overlay = overlay;
}

void UI::setHostPos(float x, float y, float pivotX, float pivotY) {
    if (!selected_) return;
    auto m = selected_->meta();
    m->hasPos = true;
    m->posX = x;
    m->posY = y;
    m->pivotX = pivotX;
    m->pivotY = pivotY;
}

void UI::setHostAnchor(float x, float y) {
    if (!selected_) return;
    auto m = selected_->meta();
    m->anchorX = x;
    m->anchorY = y;
}

void UI::setHostSize(float w, float h) {
    if (!selected_) return;
    auto m = selected_->meta();
    m->hasSize = true;
    m->sizeX = w;
    m->sizeY = h;
}

void UI::setHostPercent(float w, float h) {
    if (!selected_) return;
    auto m = selected_->meta();
    m->percentW = w;
    m->percentH = h;
}

void UI::animateHostPos(float x, float y, float durationMs) {
    if (!selected_) return;
    auto m = selected_->meta();
    HostTween t;
    t.host = selected_;
    t.fromX = m->hasPos ? m->posX : 0.f;
    t.fromY = m->hasPos ? m->posY : 0.f;
    t.toX = x;
    t.toY = y;
    t.startMs =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch())
            .count();
    t.durationMs = std::max(0.0, double(durationMs));
    m->hasPos = true;
    hostTweens_.push_back(t);
}

std::string UI::consumeClick() { return UISystem::consumeClick(); }

std::string UI::consumeChange() { return UISystem::consumeChange(); }

void UI::setThemeDark() { setThemeByName("dark"); }

void UI::setThemeLight() { setThemeByName("light"); }

bool UI::setTheme(const std::string &name) { return setThemeByName(name); }

std::string UI::getTheme() const { return globalThemeName(); }

void UI::setNavKeyboard(bool enabled) {
    globalTheme().navEnableKeyboard = enabled;
}

void UI::setNavGamepad(bool enabled) {
    globalTheme().navEnableGamepad = enabled;
}

void UI::setScale(float scale) {
    if (!isBackendReady()) {
        if (!initBackend()) return;
    }
    if (backend_) backend_->setScale(scale);
    setThemeUiScale(getScale());
}

float UI::getScale() const {
    return backend_ ? backend_->getScale() : 1.f;
}

std::string UI::getStats() const {
    const UIStats &s = UISystem::stats();
    char buf[160];
    std::snprintf(buf, sizeof(buf), "hosts=%d nodes=%d measureMs=%.3f walkMs=%.3f", s.hostCount,
                  s.nodeCount, s.measureMs, s.walkMs);
    return buf;
}

#if !(defined(EVENGINE_WEBGPU) && defined(__EMSCRIPTEN__))
std::string UI::saveTreeJson() const {
    if (!selected_) return "{}";
    auto t = selected_->tree();
    Poco::JSON::Object root;
    root.set("host", selected_->getName());
    if (t->root >= 0) nodeToJson(*t, t->nodes[size_t(t->root)], root);
    std::ostringstream oss;
    Poco::JSON::Stringifier::stringify(root, oss, 1);
    return oss.str();
}

bool UI::loadTreeJson(const std::string &json) {
    if (!selected_ || json.empty()) return false;
    try {
        Poco::JSON::Parser parser;
        const Poco::Dynamic::Var result = parser.parse(json);
        const Poco::JSON::Object::Ptr obj = result.extract<Poco::JSON::Object::Ptr>();
        if (!obj) return false;
        WidgetDesc root = descFromJson(*obj);
        selected_->setTree(std::move(root));
        return true;
    } catch (...) {
        return false;
    }
}
#else
// The Emscripten/WebGPU runtime trims Poco; keep the API but no-op.
std::string UI::saveTreeJson() const { return "{}"; }
bool UI::loadTreeJson(const std::string &) { return false; }
#endif

graphics::Canvas *UI::viewportCanvas(const std::string &id) {
    if (!selected_ || id.empty()) return nullptr;
    const std::string key = selected_->getName() + "/" + id;
    ViewportState *vs = UISystem::viewportState(selected_->getName(), id);
    if (!vs) vs = UISystem::ensureViewport(key, 320, 240);
    return vs ? vs->canvas : nullptr;
}

bool UI::viewportHovered(const std::string &id) {
    if (!selected_) return false;
    if (auto *vs = UISystem::viewportState(selected_->getName(), id)) return vs->hovered;
    return false;
}

bool UI::viewportActive(const std::string &id) {
    if (!selected_) return false;
    if (auto *vs = UISystem::viewportState(selected_->getName(), id)) return vs->active;
    return false;
}

float UI::viewportMouseX(const std::string &id) {
    if (!selected_) return 0.f;
    if (auto *vs = UISystem::viewportState(selected_->getName(), id)) return vs->mouseX;
    return 0.f;
}

float UI::viewportMouseY(const std::string &id) {
    if (!selected_) return 0.f;
    if (auto *vs = UISystem::viewportState(selected_->getName(), id)) return vs->mouseY;
    return 0.f;
}

float UI::viewportDragDX(const std::string &id) {
    if (!selected_) return 0.f;
    if (auto *vs = UISystem::viewportState(selected_->getName(), id)) return vs->dragDX;
    return 0.f;
}

float UI::viewportDragDY(const std::string &id) {
    if (!selected_) return 0.f;
    if (auto *vs = UISystem::viewportState(selected_->getName(), id)) return vs->dragDY;
    return 0.f;
}

float UI::viewportWheel(const std::string &id) {
    if (!selected_) return 0.f;
    if (auto *vs = UISystem::viewportState(selected_->getName(), id)) return vs->wheel;
    return 0.f;
}

#if !(defined(EVENGINE_WEBGPU) && defined(__EMSCRIPTEN__))
namespace {

const char *nodeTypeName(NodeType t) {
    switch (t) {
    case NodeType::Window: return "window";
    case NodeType::Text: return "text";
    case NodeType::Button: return "button";
    case NodeType::SameLine: return "sameLine";
    case NodeType::Group: return "group";
    case NodeType::Separator: return "separator";
    case NodeType::Checkbox: return "checkbox";
    case NodeType::Slider: return "slider";
    case NodeType::Progress: return "progress";
    case NodeType::InputText: return "inputText";
    case NodeType::CollapsingHeader: return "collapsingHeader";
    case NodeType::Child: return "child";
    case NodeType::Flex: return "flex";
    case NodeType::Spacer: return "spacer";
    case NodeType::Image: return "image";
    case NodeType::ImageButton: return "imageButton";
    case NodeType::Combo: return "combo";
    case NodeType::ScrollList: return "scrollList";
    case NodeType::Viewport: return "viewport";
    case NodeType::SearchField: return "searchField";
    case NodeType::Switch: return "switch";
    case NodeType::Badge: return "badge";
    case NodeType::Card: return "card";
    case NodeType::SectionHeader: return "sectionHeader";
    case NodeType::MenuBar: return "menuBar";
    case NodeType::Menu: return "menu";
    case NodeType::MenuItem: return "menuItem";
    case NodeType::Toolbar: return "toolbar";
    case NodeType::Toolbox: return "toolbox";
    case NodeType::Sidebar: return "sidebar";
    case NodeType::StatusBar: return "statusBar";
    case NodeType::SplitPane: return "splitPane";
    }
    return "text";
}

NodeType nodeTypeFromName(const std::string &s) {
    if (s == "window") return NodeType::Window;
    if (s == "button") return NodeType::Button;
    if (s == "sameLine") return NodeType::SameLine;
    if (s == "group") return NodeType::Group;
    if (s == "separator") return NodeType::Separator;
    if (s == "checkbox") return NodeType::Checkbox;
    if (s == "slider") return NodeType::Slider;
    if (s == "progress") return NodeType::Progress;
    if (s == "inputText") return NodeType::InputText;
    if (s == "collapsingHeader") return NodeType::CollapsingHeader;
    if (s == "child") return NodeType::Child;
    if (s == "flex") return NodeType::Flex;
    if (s == "spacer") return NodeType::Spacer;
    if (s == "image") return NodeType::Image;
    if (s == "imageButton") return NodeType::ImageButton;
    if (s == "combo") return NodeType::Combo;
    if (s == "scrollList") return NodeType::ScrollList;
    if (s == "viewport") return NodeType::Viewport;
    if (s == "searchField") return NodeType::SearchField;
    if (s == "switch") return NodeType::Switch;
    if (s == "badge") return NodeType::Badge;
    if (s == "card") return NodeType::Card;
    if (s == "sectionHeader") return NodeType::SectionHeader;
    if (s == "menuBar") return NodeType::MenuBar;
    if (s == "menu") return NodeType::Menu;
    if (s == "menuItem") return NodeType::MenuItem;
    if (s == "toolbar") return NodeType::Toolbar;
    if (s == "toolbox") return NodeType::Toolbox;
    if (s == "sidebar") return NodeType::Sidebar;
    if (s == "statusBar") return NodeType::StatusBar;
    if (s == "splitPane") return NodeType::SplitPane;
    return NodeType::Text;
}

void nodeToJson(const UIHost::Tree &tree, const UINode &n, Poco::JSON::Object &o) {
    o.set("type", nodeTypeName(n.type));
    if (!n.id.empty()) o.set("id", n.id);
    if (!n.key.empty()) o.set("key", n.key);
    if (!n.text.empty()) o.set("text", n.text);
    if (!n.valueText.empty()) o.set("valueText", n.valueText);
    if (!n.tooltip.empty()) o.set("tooltip", n.tooltip);
    if (!n.visible) o.set("visible", false);
    if (n.checked) o.set("checked", true);
    if (!n.open) o.set("open", false);
    if (n.value != 0.f) o.set("value", n.value);
    if (n.minValue != 0.f) o.set("minValue", n.minValue);
    if (n.maxValue != 1.f) o.set("maxValue", n.maxValue);
    if (n.sizeX != 0.f) o.set("sizeX", n.sizeX);
    if (n.sizeY != 0.f) o.set("sizeY", n.sizeY);
    if (n.marginL != 0.f || n.marginT != 0.f || n.marginR != 0.f || n.marginB != 0.f)
        o.set("margin", Poco::Dynamic::Array({n.marginL, n.marginT, n.marginR, n.marginB}));
    if (n.paddingL != 0.f || n.paddingT != 0.f || n.paddingR != 0.f || n.paddingB != 0.f)
        o.set("padding", Poco::Dynamic::Array({n.paddingL, n.paddingT, n.paddingR, n.paddingB}));
    if (n.minSizeX != 0.f) o.set("minSizeX", n.minSizeX);
    if (n.minSizeY != 0.f) o.set("minSizeY", n.minSizeY);
    if (n.maxSizeX != 0.f) o.set("maxSizeX", n.maxSizeX);
    if (n.maxSizeY != 0.f) o.set("maxSizeY", n.maxSizeY);
    if (n.percentW != 0.f) o.set("percentW", n.percentW);
    if (n.percentH != 0.f) o.set("percentH", n.percentH);
    if (n.absolute) {
        o.set("absolute", true);
        o.set("anchorX", n.anchorX);
        o.set("anchorY", n.anchorY);
        o.set("posX", n.posX);
        o.set("posY", n.posY);
    }
    if (n.wrapWidth != 0.f) o.set("wrapWidth", n.wrapWidth);
    if (n.flexDirection != FlexDirection::Row)
        o.set("flexDirection", n.flexDirection == FlexDirection::Column ? "column" : "row");
    if (n.alignItems != FlexAlign::Start) o.set("alignItems", int(n.alignItems));
    if (n.justifyContent != FlexJustify::Start) o.set("justifyContent", int(n.justifyContent));
    if (n.gap >= 0.f) o.set("gap", n.gap);
    if (n.flexGrow != 0.f) o.set("flexGrow", n.flexGrow);
    if (n.tintR != 1.f || n.tintG != 1.f || n.tintB != 1.f || n.tintA != 1.f)
        o.set("tint", Poco::Dynamic::Array({n.tintR, n.tintG, n.tintB, n.tintA}));
    if (n.borderL != 0.f || n.borderT != 0.f || n.borderR != 0.f || n.borderB != 0.f)
        o.set("border", Poco::Dynamic::Array({n.borderL, n.borderT, n.borderR, n.borderB}));
    if (n.cornerRadius != 0.f) o.set("cornerRadius", n.cornerRadius);
    if (n.uv0x != 0.f || n.uv0y != 0.f || n.uv1x != 1.f || n.uv1y != 1.f)
        o.set("uv", Poco::Dynamic::Array({n.uv0x, n.uv0y, n.uv1x, n.uv1y}));

    Poco::JSON::Array children;
    for (int c = n.firstChild; c >= 0; c = tree.nodes[size_t(c)].nextSibling) {
        Poco::JSON::Object child;
        nodeToJson(tree, tree.nodes[size_t(c)], child);
        children.add(child);
    }
    if (children.size() > 0) o.set("children", children);
}

float fnum(const Poco::Dynamic::Var &v, float def = 0.f) {
    try {
        return float(v.convert<double>());
    } catch (...) {
        return def;
    }
}

void applyCommonFields(WidgetDesc &d, const Poco::JSON::Object &o) {
    if (o.has("id")) d.id = o.getValue<std::string>("id");
    if (o.has("key")) d.key = o.getValue<std::string>("key");
    if (o.has("text")) d.text = o.getValue<std::string>("text");
    if (o.has("valueText")) d.valueText = o.getValue<std::string>("valueText");
    if (o.has("tooltip")) d.tooltip = o.getValue<std::string>("tooltip");
    if (o.has("visible")) d.visible = o.getValue<bool>("visible");
    if (o.has("checked")) d.checked = o.getValue<bool>("checked");
    if (o.has("open")) d.open = o.getValue<bool>("open");
    if (o.has("value")) d.value = fnum(o.get("value"));
    if (o.has("minValue")) d.minValue = fnum(o.get("minValue"));
    if (o.has("maxValue")) d.maxValue = fnum(o.get("maxValue"));
    if (o.has("sizeX")) d.sizeX = fnum(o.get("sizeX"));
    if (o.has("sizeY")) d.sizeY = fnum(o.get("sizeY"));
    if (o.has("margin")) {
        const Poco::Dynamic::Array a = o.get("margin").extract<Poco::Dynamic::Array>();
        if (a.size() >= 4) {
            d.marginL = fnum(a[0]);
            d.marginT = fnum(a[1]);
            d.marginR = fnum(a[2]);
            d.marginB = fnum(a[3]);
        }
    }
    if (o.has("padding")) {
        const Poco::Dynamic::Array a = o.get("padding").extract<Poco::Dynamic::Array>();
        if (a.size() >= 4) {
            d.paddingL = fnum(a[0]);
            d.paddingT = fnum(a[1]);
            d.paddingR = fnum(a[2]);
            d.paddingB = fnum(a[3]);
        }
    }
    if (o.has("minSizeX")) d.minSizeX = fnum(o.get("minSizeX"));
    if (o.has("minSizeY")) d.minSizeY = fnum(o.get("minSizeY"));
    if (o.has("maxSizeX")) d.maxSizeX = fnum(o.get("maxSizeX"));
    if (o.has("maxSizeY")) d.maxSizeY = fnum(o.get("maxSizeY"));
    if (o.has("percentW")) d.percentW = fnum(o.get("percentW"));
    if (o.has("percentH")) d.percentH = fnum(o.get("percentH"));
    if (o.has("absolute")) d.absolute = o.getValue<bool>("absolute");
    if (o.has("anchorX")) d.anchorX = fnum(o.get("anchorX"));
    if (o.has("anchorY")) d.anchorY = fnum(o.get("anchorY"));
    if (o.has("posX")) d.posX = fnum(o.get("posX"));
    if (o.has("posY")) d.posY = fnum(o.get("posY"));
    if (o.has("wrapWidth")) d.wrapWidth = fnum(o.get("wrapWidth"));
    if (o.has("flexDirection")) d.flexDirection =
        o.getValue<std::string>("flexDirection") == "column" ? FlexDirection::Column
                                                             : FlexDirection::Row;
    if (o.has("alignItems")) d.alignItems = FlexAlign(int(fnum(o.get("alignItems"))));
    if (o.has("justifyContent"))
        d.justifyContent = FlexJustify(int(fnum(o.get("justifyContent"))));
    if (o.has("gap")) d.gap = fnum(o.get("gap"), -1.f);
    if (o.has("flexGrow")) d.flexGrow = fnum(o.get("flexGrow"));
    if (o.has("tint")) {
        const Poco::Dynamic::Array a = o.get("tint").extract<Poco::Dynamic::Array>();
        if (a.size() >= 4) {
            d.tintR = fnum(a[0], 1.f);
            d.tintG = fnum(a[1], 1.f);
            d.tintB = fnum(a[2], 1.f);
            d.tintA = fnum(a[3], 1.f);
        }
    }
    if (o.has("border")) {
        const Poco::Dynamic::Array a = o.get("border").extract<Poco::Dynamic::Array>();
        if (a.size() >= 4) {
            d.borderL = fnum(a[0]);
            d.borderT = fnum(a[1]);
            d.borderR = fnum(a[2]);
            d.borderB = fnum(a[3]);
        }
    }
    if (o.has("cornerRadius")) d.cornerRadius = fnum(o.get("cornerRadius"));
    if (o.has("uv")) {
        const Poco::Dynamic::Array a = o.get("uv").extract<Poco::Dynamic::Array>();
        if (a.size() >= 4) {
            d.uv0x = fnum(a[0]);
            d.uv0y = fnum(a[1]);
            d.uv1x = fnum(a[2], 1.f);
            d.uv1y = fnum(a[3], 1.f);
        }
    }
}

WidgetDesc descFromJson(const Poco::JSON::Object &o) {
    WidgetDesc d;
    d.type = nodeTypeFromName(o.optValue<std::string>("type", "text"));
    applyCommonFields(d, o);
    if (o.has("children")) {
        const Poco::JSON::Array::Ptr children = o.getArray("children");
        for (size_t i = 0; i < children->size(); ++i) {
            const Poco::JSON::Object::Ptr child =
                children->getObject(static_cast<unsigned int>(i));
            if (child) d.children.push_back(descFromJson(*child));
        }
    }
    return d;
}

}  // namespace
#endif

void UI::mountSimple(const std::string &title, const std::string &labelText,
                     const std::string &buttonText) {
    mountAs("default", window(title, {text(labelText, "label"), button(buttonText, "btn")}, "root"));
}

bool UI::inspectOpen() {
    if (!inspector_) inspector_ = std::make_unique<Inspector>();
    inspector_->setPickScene([this]() { return callPickHandler(); });
    inspector_->open();
    return inspector_->isOpen();
}

void UI::inspectClose() {
    if (inspector_) inspector_->close();
}

bool UI::inspectRefresh() {
    if (!inspector_) inspector_ = std::make_unique<Inspector>();
    inspector_->refresh();
    return inspector_->instanceCount() > 0;
}

bool UI::inspectSelectClass(const std::string &name) {
    if (!inspector_) inspector_ = std::make_unique<Inspector>();
    inspector_->setPickScene([this]() { return callPickHandler(); });
    inspector_->open();  // scans classes and mounts the panel if not open yet
    return inspector_->selectClass(name);
}

bool UI::inspectObject(ssq::Object object) {
    if (!inspector_) inspector_ = std::make_unique<Inspector>();
    inspector_->setPickScene([this]() { return callPickHandler(); });
    inspector_->open();  // scans classes and mounts the panel if not open yet
    return inspector_->inspectObject(object);
}

bool UI::inspectSetPickHandler(ssq::Function fn) {
    Runtime *rt = ModuleManager::runtime();
    if (!rt) return false;
    HSQUIRRELVM squirrel = rt->handle();
    const SQInteger top = sq_gettop(squirrel);
    sq_pushroottable(squirrel);
    sq_pushstring(squirrel, "eve", -1);
    if (SQ_FAILED(sq_get(squirrel, -2)) ||
        sq_gettype(squirrel, -1) != OT_TABLE) {
        sq_settop(squirrel, top);
        return false;
    }
    sq_pushstring(squirrel, "_inspectorPickHandler", -1);
    sq_pushobject(squirrel, fn.getRaw());
    sq_newslot(squirrel, -3, SQFalse);
    sq_settop(squirrel, top);
    return true;
}

ssq::Object UI::callPickHandler() {
    Runtime *rt = ModuleManager::runtime();
    if (!rt) return {};
    HSQUIRRELVM squirrel = rt->handle();
    const SQInteger top = sq_gettop(squirrel);
    sq_pushroottable(squirrel);
    sq_pushstring(squirrel, "eve", -1);
    if (SQ_FAILED(sq_get(squirrel, -2)) ||
        sq_gettype(squirrel, -1) != OT_TABLE) {
        sq_settop(squirrel, top);
        return {};
    }
    sq_pushstring(squirrel, "_inspectorPickHandler", -1);
    if (SQ_FAILED(sq_get(squirrel, -2)) ||
        (sq_gettype(squirrel, -1) != OT_CLOSURE &&
         sq_gettype(squirrel, -1) != OT_NATIVECLOSURE)) {
        sq_settop(squirrel, top);
        return {};
    }
    sq_pushroottable(squirrel);  // environment
    if (SQ_FAILED(sq_call(squirrel, 1, SQTrue, SQTrue))) {
        sq_settop(squirrel, top);
        return {};
    }
    if (sq_gettype(squirrel, -1) != OT_INSTANCE) {
        sq_settop(squirrel, top);
        return {};
    }
    ssq::Object out(squirrel);
    sq_getstackobj(squirrel, -1, &out.getRaw());
    sq_addref(squirrel, &out.getRaw());
    sq_settop(squirrel, top);
    return out;
}

bool UI::inspectPickScene() {
    if (!inspector_) inspector_ = std::make_unique<Inspector>();
    inspector_->setPickScene([this]() { return callPickHandler(); });
    inspector_->open();
    const ssq::Object picked = callPickHandler();
    if (picked.getType() != ssq::Type::INSTANCE) return false;
    return inspector_->inspectObject(picked);
}

bool UI::inspectAddInstance() {
    return inspector_ && inspector_->addInstance();
}

bool UI::dbOpen() {
    if (!databasePanel_) databasePanel_ = std::make_unique<DatabasePanel>();
    databasePanel_->open();
    return databasePanel_->isOpen();
}

void UI::dbClose() {
    if (databasePanel_) databasePanel_->close();
}

bool UI::dbRefresh() {
    if (!databasePanel_) databasePanel_ = std::make_unique<DatabasePanel>();
    databasePanel_->refresh();
    return databasePanel_->isOpen();
}

bool UI::dbSelectClass(const std::string &name) {
    if (!databasePanel_) databasePanel_ = std::make_unique<DatabasePanel>();
    databasePanel_->refresh();
    return databasePanel_->selectClass(name);
}

uint64_t UI::dbRegister(ssq::Object object, const std::string &label) {
    if (!databasePanel_) databasePanel_ = std::make_unique<DatabasePanel>();
    databasePanel_->open();  // mount the panel so the entry becomes visible
    return databasePanel_->registerObject(object, label);
}

uint64_t UI::dbCreateInstance() {
    if (!databasePanel_) databasePanel_ = std::make_unique<DatabasePanel>();
    databasePanel_->open();
    return databasePanel_->createInstance();
}

bool UI::dbUnregister(uint64_t id) {
    return databasePanel_ && databasePanel_->unregister(id);
}

bool UI::editorOpen() {
    if (!inspector_) inspector_ = std::make_unique<Inspector>();
    inspector_->setPickScene([this]() { return callPickHandler(); });
    inspector_->open();
    if (!databasePanel_) databasePanel_ = std::make_unique<DatabasePanel>();
    databasePanel_->open();
    if (!scenePanel_) scenePanel_ = std::make_unique<ScenePanel>();
    scenePanel_->setPickHandler([this](const std::string &nodeId) {
        callScenePickHandler(nodeId);
    });
    scenePanel_->open();
    if (!editorShell_) editorShell_ = std::make_unique<EditorShell>();
    editorShell_->open(inspector_->host(), databasePanel_->host(),
                       scenePanel_->host());
    editorShell_->selectPanel("inspector");
    return editorShell_->isOpen();
}

void UI::editorClose() {
    if (editorShell_) editorShell_->close();
}

bool UI::editorSelectPanel(const std::string &name) {
    return editorShell_ && editorShell_->selectPanel(name);
}

bool UI::sceneOpen() {
    if (!scenePanel_) scenePanel_ = std::make_unique<ScenePanel>();
    scenePanel_->setPickHandler([this](const std::string &nodeId) {
        callScenePickHandler(nodeId);
    });
    scenePanel_->open();
    return scenePanel_->isOpen();
}

void UI::sceneClose() {
    if (scenePanel_) scenePanel_->close();
}

bool UI::sceneSelectNode(const std::string &id) {
    return scenePanel_ && scenePanel_->selectNode(id);
}

bool UI::sceneSetPickHandler(ssq::Function fn) {
    Runtime *rt = ModuleManager::runtime();
    if (!rt) return false;
    HSQUIRRELVM squirrel = rt->handle();
    const SQInteger top = sq_gettop(squirrel);
    sq_pushroottable(squirrel);
    sq_pushstring(squirrel, "eve", -1);
    if (SQ_FAILED(sq_get(squirrel, -2)) ||
        sq_gettype(squirrel, -1) != OT_TABLE) {
        sq_settop(squirrel, top);
        return false;
    }
    sq_pushstring(squirrel, "_scenePickHandler", -1);
    sq_pushobject(squirrel, fn.getRaw());
    sq_newslot(squirrel, -3, SQFalse);
    sq_settop(squirrel, top);
    return true;
}

void UI::callScenePickHandler(const std::string &nodeId) {
    Runtime *rt = ModuleManager::runtime();
    if (!rt) return;
    HSQUIRRELVM squirrel = rt->handle();
    const SQInteger top = sq_gettop(squirrel);
    sq_pushroottable(squirrel);
    sq_pushstring(squirrel, "eve", -1);
    if (SQ_FAILED(sq_get(squirrel, -2)) ||
        sq_gettype(squirrel, -1) != OT_TABLE) {
        sq_settop(squirrel, top);
        return;
    }
    sq_pushstring(squirrel, "_scenePickHandler", -1);
    if (SQ_FAILED(sq_get(squirrel, -2)) ||
        (sq_gettype(squirrel, -1) != OT_CLOSURE &&
         sq_gettype(squirrel, -1) != OT_NATIVECLOSURE)) {
        sq_settop(squirrel, top);
        return;
    }
    sq_pushroottable(squirrel);                    // environment
    sq_pushstring(squirrel, nodeId.c_str(), -1);   // node id argument
    sq_call(squirrel, 2, SQFalse, SQTrue);
    sq_settop(squirrel, top);
}

void UI::expose(ssq::Table &table) {
    auto cls = table.addClass(name, UI::create, false);
    expose(cls);
    injectUIComponentClass(table);
}

void UI::expose(ssq::Class &cls) {
    cls.addFunc("getName", &UI::getName);
    cls.addFunc("initBackend", &UI::initBackend);
    cls.addFunc("isBackendReady", &UI::isBackendReady);
    cls.addFunc("beginFrameAndRender", &UI::beginFrameAndRender);
    cls.addFunc("dispatchEvents", &UI::dispatchEvents);
    cls.addFunc("wantCaptureMouse", &UI::wantCaptureMouse);
    cls.addFunc("wantCaptureKeyboard", &UI::wantCaptureKeyboard);

    cls.addFunc("select", &UI::select);
    cls.addFunc("bindOwner", &UI::bindOwner);
    cls.addFunc("mountBuildAs", &UI::mountBuildAs);
    cls.addFunc("remountBuildAs", &UI::remountBuildAs);

    cls.addFunc("beginBuild", &UI::beginBuild);
    cls.addFunc("beginWindow", &UI::beginWindow);
    cls.addFunc("beginGroup", &UI::beginGroup);
    cls.addFunc("beginList", &UI::beginList);
    cls.addFunc("beginCollapsing", &UI::beginCollapsing);
    cls.addFunc("beginChild", &UI::beginChild);
    cls.addFunc("beginCard", &UI::beginCard);
    cls.addFunc("beginMenuBar", &UI::beginMenuBar);
    cls.addFunc("beginMenu", &UI::beginMenu);
    cls.addFunc("beginToolbar", &UI::beginToolbar);
    cls.addFunc("beginToolbox", &UI::beginToolbox);
    cls.addFunc("beginSidebar", &UI::beginSidebar);
    cls.addFunc("beginStatusBar", &UI::beginStatusBar);
    cls.addFunc("beginSplitPane", &UI::beginSplitPane);
    cls.addFunc("beginScrollList", &UI::beginScrollList);
    cls.addFunc("beginFlex", &UI::beginFlex);
    cls.addFunc("beginRow", &UI::beginRow);
    cls.addFunc("beginColumn", &UI::beginColumn);
    cls.addFunc("end", &UI::end);
    cls.addFunc("text", &UI::addText);
    cls.addFunc("textWrapped", &UI::addTextWrapped);
    cls.addFunc("button", &UI::addButton);
    cls.addFunc("icon", &UI::addIcon);
    cls.addFunc("iconButton", &UI::addIconButton);
    cls.addFunc("sameLine", &UI::addSameLine);
    cls.addFunc("separator", &UI::addSeparator);
    cls.addFunc("checkbox", &UI::addCheckbox);
    cls.addFunc("slider", &UI::addSlider);
    cls.addFunc("progress", &UI::addProgress);
    cls.addFunc("image", &UI::addImage);
    cls.addFunc("imageButton", &UI::addImageButton);
    cls.addFunc("viewport", &UI::addViewport);
    cls.addFunc("combo", &UI::addCombo);
    cls.addFunc("inputText", &UI::addInputText);
    cls.addFunc("searchField", &UI::addSearchField);
    cls.addFunc("switch", &UI::addSwitch);
    cls.addFunc("badge", &UI::addBadge);
    cls.addFunc("sectionHeader", &UI::addSectionHeader);
    cls.addFunc("menuItem", &UI::addMenuItem);
    cls.addFunc("spacer", &UI::addSpacer);
    cls.addFunc("setItemFlexGrow", &UI::setItemFlexGrow);
    cls.addFunc("setItemSize", &UI::setItemSize);
    cls.addFunc("setItemMargin", &UI::setItemMargin);
    cls.addFunc("setItemPadding", &UI::setItemPadding);
    cls.addFunc("setItemMinSize", &UI::setItemMinSize);
    cls.addFunc("setItemMaxSize", &UI::setItemMaxSize);
    cls.addFunc("setItemPercent", &UI::setItemPercent);
    cls.addFunc("setItemAbsolute", &UI::setItemAbsolute);
    cls.addFunc("setItemTooltip", &UI::setItemTooltip);
    cls.addFunc("setFlexAlign", &UI::setFlexAlign);
    cls.addFunc("setFlexJustify", &UI::setFlexJustify);
    cls.addFunc("listItem", &UI::addListItem);
    cls.addFunc("mountBuild", &UI::mountBuild);

    cls.addFunc("setText", &UI::setText);
    cls.addFunc("setTextWrap", &UI::setTextWrap);
    cls.addFunc("setVisible", &UI::setVisible);
    cls.addFunc("setChecked", &UI::setChecked);
    cls.addFunc("setValue", &UI::setValue);
    cls.addFunc("setValueText", &UI::setValueText);
    cls.addFunc("setImageTint", &UI::setImageTint);
    cls.addFunc("setImageUv", &UI::setImageUv);
    cls.addFunc("setImageNinePatch", &UI::setImageNinePatch);
    cls.addFunc("setImageCornerRadius", &UI::setImageCornerRadius);
    cls.addFunc("getValue", &UI::getValue);
    cls.addFunc("getValueText", &UI::getValueText);
    cls.addFunc("getChecked", &UI::getChecked);
    cls.addFunc("setHostVisible", &UI::setHostVisible);
    cls.addFunc("setHostLayer", &UI::setHostLayer);
    cls.addFunc("setHostModal", &UI::setHostModal);
    cls.addFunc("setHostOverlay", &UI::setHostOverlay);
    cls.addFunc("setHostPos", &UI::setHostPos);
    cls.addFunc("setHostAnchor", &UI::setHostAnchor);
    cls.addFunc("setHostSize", &UI::setHostSize);
    cls.addFunc("setHostPercent", &UI::setHostPercent);
    cls.addFunc("animateHostPos", &UI::animateHostPos);
    cls.addFunc("consumeClick", &UI::consumeClick);
    cls.addFunc("consumeChange", &UI::consumeChange);
    cls.addFunc("onClick", &UI::onClick);
    cls.addFunc("onChange", &UI::onChange);

    cls.addFunc("setThemeDark", &UI::setThemeDark);
    cls.addFunc("setThemeLight", &UI::setThemeLight);
    cls.addFunc("setTheme", &UI::setTheme);
    cls.addFunc("getTheme", &UI::getTheme);
    cls.addFunc("setNavKeyboard", &UI::setNavKeyboard);
    cls.addFunc("setNavGamepad", &UI::setNavGamepad);
    cls.addFunc("setScale", &UI::setScale);
    cls.addFunc("getScale", &UI::getScale);
    cls.addFunc("getStats", &UI::getStats);
    cls.addFunc("saveTreeJson", &UI::saveTreeJson);
    cls.addFunc("loadTreeJson", &UI::loadTreeJson);
    cls.addFunc("viewportCanvas", &UI::viewportCanvas);
    cls.addFunc("viewportHovered", &UI::viewportHovered);
    cls.addFunc("viewportActive", &UI::viewportActive);
    cls.addFunc("viewportMouseX", &UI::viewportMouseX);
    cls.addFunc("viewportMouseY", &UI::viewportMouseY);
    cls.addFunc("viewportDragDX", &UI::viewportDragDX);
    cls.addFunc("viewportDragDY", &UI::viewportDragDY);
    cls.addFunc("viewportWheel", &UI::viewportWheel);

    cls.addFunc("mountSimple", &UI::mountSimple);

    cls.addFunc("inspect", &UI::inspectOpen);
    cls.addFunc("inspectClose", &UI::inspectClose);
    cls.addFunc("inspectRefresh", &UI::inspectRefresh);
    cls.addFunc("inspectSelectClass", &UI::inspectSelectClass);
    cls.addFunc("inspectObject", &UI::inspectObject);
    cls.addFunc("inspectSetPickHandler", &UI::inspectSetPickHandler);
    cls.addFunc("inspectPickScene", &UI::inspectPickScene);
    cls.addFunc("inspectAddInstance", &UI::inspectAddInstance);

    cls.addFunc("dbOpen", &UI::dbOpen);
    cls.addFunc("dbClose", &UI::dbClose);
    cls.addFunc("dbRefresh", &UI::dbRefresh);
    cls.addFunc("dbSelectClass", &UI::dbSelectClass);
    cls.addFunc("dbRegister", &UI::dbRegister);
    cls.addFunc("dbCreateInstance", &UI::dbCreateInstance);
    cls.addFunc("dbUnregister", &UI::dbUnregister);

    cls.addFunc("editorOpen", &UI::editorOpen);
    cls.addFunc("editorClose", &UI::editorClose);
    cls.addFunc("editorSelectPanel", &UI::editorSelectPanel);

    cls.addFunc("sceneOpen", &UI::sceneOpen);
    cls.addFunc("sceneClose", &UI::sceneClose);
    cls.addFunc("sceneSelectNode", &UI::sceneSelectNode);
    cls.addFunc("sceneSetPickHandler", &UI::sceneSetPickHandler);
}

}  // namespace eve::ui
