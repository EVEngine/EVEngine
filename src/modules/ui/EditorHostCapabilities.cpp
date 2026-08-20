#include "common/Capability.h"
#include "common/EditorHost.h"
#include "ui/EditorHost.h"

#include <string>

namespace eve::ui {
namespace {

class EditorHostImpl final : public eve::IEditorHost {
public:
    std::string status() const override { return EditorHost::instance().status(); }
    std::string openWindow(const std::string &title, int width, int height) override {
        return EditorHost::instance().openWindow(title, width, height);
    }
    std::string closeWindow() override { return EditorHost::instance().closeWindow(); }
    std::string windowState() const override { return EditorHost::instance().windowState(); }
    std::string applyEditor(const std::string &json) override {
        return EditorHost::instance().applyEditor(json);
    }
    std::string removeEditor(const std::string &id) override {
        return EditorHost::instance().removeEditor(id);
    }
    std::string listEditors() const override { return EditorHost::instance().listEditors(); }
    std::string editorState(const std::string &id) const override {
        return EditorHost::instance().editorState(id);
    }
    std::string setEditorValue(const std::string &editorId, const std::string &widgetId,
                               const std::string &value) override {
        return EditorHost::instance().setEditorValue(editorId, widgetId, value);
    }
    std::string consumeEvents(const std::string &editorId) override {
        return EditorHost::instance().consumeEvents(editorId);
    }
    std::string widgetRect(const std::string &editorId,
                           const std::string &widgetId) const override {
        return EditorHost::instance().widgetRect(editorId, widgetId);
    }
    std::string registerVM(const std::string &name, const std::string &source) override {
        return EditorHost::instance().registerVM(name, source);
    }
    std::string unregisterVM(const std::string &name) override {
        return EditorHost::instance().unregisterVM(name);
    }
    std::string saveEditor(const std::string &id) override {
        return EditorHost::instance().saveEditor(id);
    }
    std::string unloadEditor(const std::string &id) override {
        return EditorHost::instance().unloadEditor(id);
    }
    std::string capture(const std::string &path) override {
        return EditorHost::instance().capture(path);
    }
    std::string runScript(const std::string &source) override {
        return EditorHost::instance().runScript(source);
    }
    void requestExit() override { EditorHost::instance().requestExit(); }
    bool exitRequested() override { return EditorHost::instance().exitRequested(); }
    void frame() override { EditorHost::instance().frame(); }
    void stop() override { EditorHost::instance().stop(); }
    void start(ssq::VM& vm, const std::string& gameRoot, bool allowWindow) override {
        EditorHost::instance().start(vm, gameRoot, allowWindow);
    }
    void exposeScriptApi(ssq::VM& vm) override { EditorHost::instance().exposeScriptApi(vm); }
};

}  // namespace

void registerEditorHostCapabilities() {
    static EditorHostImpl impl;
    eve::cap::provide<eve::IEditorHost>(&impl);
}

}  // namespace eve::ui
