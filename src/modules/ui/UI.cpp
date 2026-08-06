#include "ui/UI.h"

#include "ui/Theme.h"
#include "ui/UISystem.h"
#include "ui/Widget.h"

#include "common/Module.h"
#include "graphics/Graphics.h"
#include "window/Window.h"
#include "window/sdl/Window.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <cstring>
#include <stdexcept>

namespace eve::ui {
namespace {

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
eve.Component <- eve.UIComponent
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

UI::UI() : backend_(createImGuiBackend()) {}
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
    return backend_->init(native, gfx);
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
    backend_->newFrame();
    UISystem::render();
}

void UI::dispatchEvents() { UISystem::dispatchEvents(); }

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

void UI::addButton(const std::string &label, const std::string &id) {
    currentParent().children.push_back(button(label, id));
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

void UI::addInputText(const std::string &label, const std::string &value, const std::string &id) {
    currentParent().children.push_back(inputText(label, value, id));
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

std::string UI::consumeClick() { return UISystem::consumeClick(); }

std::string UI::consumeChange() { return UISystem::consumeChange(); }

void UI::setThemeDark() {
    Theme t;
    setGlobalTheme(t);
}

void UI::setThemeLight() {
    Theme t;
    t.windowBg[0] = 0.94f;
    t.windowBg[1] = 0.94f;
    t.windowBg[2] = 0.94f;
    t.windowBg[3] = 1.f;
    t.text[0] = t.text[1] = t.text[2] = 0.f;
    t.text[3] = 1.f;
    t.button[0] = 0.26f;
    t.button[1] = 0.59f;
    t.button[2] = 0.98f;
    t.button[3] = 0.6f;
    setGlobalTheme(t);
}

void UI::setNavKeyboard(bool enabled) {
    globalTheme().navEnableKeyboard = enabled;
}

void UI::setScale(float scale) {
    if (!isBackendReady()) {
        if (!initBackend()) return;
    }
    if (backend_) backend_->setScale(scale);
}

float UI::getScale() const {
    return backend_ ? backend_->getScale() : 1.f;
}

void UI::mountSimple(const std::string &title, const std::string &labelText,
                     const std::string &buttonText) {
    mountAs("default", window(title, {text(labelText, "label"), button(buttonText, "btn")}, "root"));
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
    cls.addFunc("end", &UI::end);
    cls.addFunc("text", &UI::addText);
    cls.addFunc("button", &UI::addButton);
    cls.addFunc("sameLine", &UI::addSameLine);
    cls.addFunc("separator", &UI::addSeparator);
    cls.addFunc("checkbox", &UI::addCheckbox);
    cls.addFunc("slider", &UI::addSlider);
    cls.addFunc("progress", &UI::addProgress);
    cls.addFunc("inputText", &UI::addInputText);
    cls.addFunc("listItem", &UI::addListItem);
    cls.addFunc("mountBuild", &UI::mountBuild);

    cls.addFunc("setText", &UI::setText);
    cls.addFunc("setVisible", &UI::setVisible);
    cls.addFunc("setChecked", &UI::setChecked);
    cls.addFunc("setValue", &UI::setValue);
    cls.addFunc("setValueText", &UI::setValueText);
    cls.addFunc("getValue", &UI::getValue);
    cls.addFunc("getValueText", &UI::getValueText);
    cls.addFunc("getChecked", &UI::getChecked);
    cls.addFunc("setHostVisible", &UI::setHostVisible);
    cls.addFunc("setHostLayer", &UI::setHostLayer);
    cls.addFunc("setHostModal", &UI::setHostModal);
    cls.addFunc("setHostOverlay", &UI::setHostOverlay);
    cls.addFunc("setHostPos", &UI::setHostPos);
    cls.addFunc("consumeClick", &UI::consumeClick);
    cls.addFunc("consumeChange", &UI::consumeChange);

    cls.addFunc("setThemeDark", &UI::setThemeDark);
    cls.addFunc("setThemeLight", &UI::setThemeLight);
    cls.addFunc("setNavKeyboard", &UI::setNavKeyboard);
    cls.addFunc("setScale", &UI::setScale);
    cls.addFunc("getScale", &UI::getScale);

    cls.addFunc("mountSimple", &UI::mountSimple);
}

}  // namespace eve::ui
