#include "ui/UIAutomationCapabilities.h"

#include "common/config.h"

#if defined(EVENGINE_WEBGPU) && defined(__EMSCRIPTEN__)

namespace eve::ui {

void registerUIAutomationCapabilities() {}

}  // namespace eve::ui

#else

#include "common/Capability.h"
#include "common/ECS.h"
#include "common/UIAutomation.h"
#include "ui/UIHost.h"
#include "ui/UISystem.h"

#include <Poco/Dynamic/Var.h>
#include <Poco/JSON/Array.h>
#include <Poco/JSON/Object.h>
#include <Poco/JSON/Stringifier.h>

#include <sstream>
#include <string>
#include <vector>

namespace eve::ui {
namespace {

const char* nodeTypeName(NodeType type) {
    switch (type) {
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
        case NodeType::NinePatchPanel: return "ninePatchPanel";
        case NodeType::ColorPalette: return "colorPalette";
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
    return "unknown";
}

bool isClickable(const UINode& node) {
    if (!node.enabled || node.mouseFilter == MouseFilter::Ignore) return false;
    return node.type == NodeType::Button || node.type == NodeType::ImageButton ||
           node.type == NodeType::Switch || node.type == NodeType::MenuItem ||
           node.handlerClick != 0;
}

const char* focusModeName(FocusMode mode) {
    switch (mode) {
        case FocusMode::None: return "none";
        case FocusMode::Click: return "click";
        case FocusMode::All: return "all";
    }
    return "none";
}

const char* mouseFilterName(MouseFilter filter) {
    switch (filter) {
        case MouseFilter::Stop: return "stop";
        case MouseFilter::Pass: return "pass";
        case MouseFilter::Ignore: return "ignore";
    }
    return "ignore";
}

const char* themePresetName(ThemePreset preset) {
    switch (preset) {
        case ThemePreset::Inherit: return "inherit";
        case ThemePreset::Dark: return "dark";
        case ThemePreset::Light: return "light";
    }
    return "inherit";
}

const char* accessibilityRoleName(AccessibilityRole role) {
    switch (role) {
        case AccessibilityRole::Auto: return "auto";
        case AccessibilityRole::Button: return "button";
        case AccessibilityRole::Checkbox: return "checkbox";
        case AccessibilityRole::Slider: return "slider";
        case AccessibilityRole::Text: return "text";
        case AccessibilityRole::TextInput: return "textInput";
        case AccessibilityRole::List: return "list";
        case AccessibilityRole::ListItem: return "listItem";
        case AccessibilityRole::Menu: return "menu";
        case AccessibilityRole::MenuItem: return "menuItem";
        case AccessibilityRole::Progress: return "progress";
        case AccessibilityRole::Region: return "region";
        case AccessibilityRole::Tab: return "tab";
        case AccessibilityRole::Window: return "window";
    }
    return "auto";
}

std::string stringify(const Poco::Dynamic::Var& value) {
    std::ostringstream out;
    Poco::JSON::Stringifier::stringify(value, out, 0, 0);
    return out.str();
}

Poco::JSON::Object::Ptr nodeJson(const UIHost::Tree& tree, int index) {
    Poco::JSON::Object::Ptr out(new Poco::JSON::Object());
    if (index < 0 || index >= static_cast<int>(tree.nodes.size())) return out;

    const UINode& node = tree.nodes[static_cast<size_t>(index)];
    out->set("type", nodeTypeName(node.type));
    out->set("id", node.id);
    if (!node.key.empty()) out->set("key", node.key);
    out->set("text", node.text);
    out->set("visible", node.visible);
    out->set("enabled", node.enabled);
    out->set("clickable", isClickable(node));
    out->set("focusMode", focusModeName(node.focusMode));
    out->set("focused", node.focused);
    out->set("tabIndex", node.tabIndex);
    out->set("mouseFilter", mouseFilterName(node.mouseFilter));
    out->set("theme", themePresetName(node.themePreset));
    if (!node.focusPrevious.empty()) out->set("focusPrevious", node.focusPrevious);
    if (!node.focusNext.empty()) out->set("focusNext", node.focusNext);
    if (!node.focusLeft.empty()) out->set("focusLeft", node.focusLeft);
    if (!node.focusRight.empty()) out->set("focusRight", node.focusRight);
    if (!node.focusUp.empty()) out->set("focusUp", node.focusUp);
    if (!node.focusDown.empty()) out->set("focusDown", node.focusDown);
    Poco::JSON::Object::Ptr accessibility(new Poco::JSON::Object());
    accessibility->set("role", accessibilityRoleName(node.accessibilityRole));
    accessibility->set("name", node.accessibilityName.empty() ? node.text
                                                              : node.accessibilityName);
    if (!node.accessibilityDescription.empty())
        accessibility->set("description", node.accessibilityDescription);
    out->set("accessibility", accessibility);
    if (node.type == NodeType::Checkbox || node.type == NodeType::Switch ||
        node.type == NodeType::MenuItem)
        out->set("checked", node.checked);
    if (node.type == NodeType::Slider || node.type == NodeType::Progress ||
        node.type == NodeType::Combo || node.type == NodeType::SplitPane) {
        out->set("value", node.value);
        out->set("min", node.minValue);
        out->set("max", node.maxValue);
    }
    if (node.type == NodeType::ColorPalette) {
        Poco::JSON::Array::Ptr color(new Poco::JSON::Array());
        color->add(static_cast<double>(node.tintR));
        color->add(static_cast<double>(node.tintG));
        color->add(static_cast<double>(node.tintB));
        color->add(static_cast<double>(node.tintA));
        out->set("color", color);
    }
    if (node.type == NodeType::InputText || node.type == NodeType::SearchField ||
        node.type == NodeType::Combo || node.type == NodeType::MenuItem)
        out->set("valueText", node.valueText);
    if (!node.tooltip.empty()) out->set("tooltip", node.tooltip);

    Poco::JSON::Array::Ptr children(new Poco::JSON::Array());
    for (int child = node.firstChild; child >= 0; child = tree.nodes[static_cast<size_t>(child)].nextSibling) {
        if (child >= static_cast<int>(tree.nodes.size())) break;
        children->add(nodeJson(tree, child));
    }
    if (children->size() != 0) out->set("children", children);
    return out;
}

Poco::JSON::Object::Ptr hostJson(UIHost& host) {
    Poco::JSON::Object::Ptr out(new Poco::JSON::Object());
    const auto              meta = host.meta();
    const auto              tree = host.tree();
    out->set("name", meta->name);
    out->set("visible", meta->visible);
    out->set("layer", meta->layer);
    out->set("modal", meta->modal);
    out->set("overlay", meta->overlay);
    out->set("nodeCount", static_cast<int>(tree->nodes.size()));
    if (tree->root >= 0) out->set("root", nodeJson(*tree, tree->root));
    return out;
}

std::vector<UIHostHandle> hosts() {
    std::vector<UIHostHandle> result;
    if (ecs::current()->getManager<UIHost>() == nullptr) return result;
    auto view = ecs::View<UIHost, UIHost::Meta, UIHost::Tree>();
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto [meta, tree] = *it;
        (void)tree;
        if (UIHost::resolve(meta->entity)) result.push_back(meta->entity);
    }
    return result;
}

struct WidgetMatch {
    UIHostHandle host{};
    int          nodeIndex = -1;
};

std::vector<WidgetMatch> findWidgets(const std::string& hostName, const std::string& widgetId) {
    std::vector<WidgetMatch> result;
    for (UIHostHandle handle : hosts()) {
        auto host = UIHost::resolve(handle);
        if (!host || (!hostName.empty() && host->get().getName() != hostName)) continue;
        auto node = host->get().findById(widgetId);
        if (!node) continue;
        const auto tree = host->get().tree();
        result.push_back({handle, static_cast<int>(&node->get() - tree->nodes.data())});
    }
    return result;
}

std::string matchError(const std::string& host, const std::string& widget, size_t matchCount) {
    if (widget.empty()) return "error: missing widget";
    if (matchCount == 0) {
        if (!host.empty()) return "error: widget not found: " + host + "/" + widget;
        return "error: widget not found: " + widget;
    }
    return "error: widget id is ambiguous; specify host: " + widget;
}

class UIAutomationImpl final : public eve::IUIAutomation {
public:
    std::string tree(const std::string& hostName) const override {
        Poco::JSON::Array::Ptr items(new Poco::JSON::Array());
        for (UIHostHandle handle : hosts()) {
            auto host = UIHost::resolve(handle);
            if (!host || (!hostName.empty() && host->get().getName() != hostName)) continue;
            items->add(hostJson(host->get()));
        }
        if (!hostName.empty() && items->size() == 0) return "error: UI host not found: " + hostName;

        Poco::JSON::Object::Ptr out(new Poco::JSON::Object());
        out->set("hostCount", static_cast<int>(items->size()));
        out->set("hosts", items);
        return stringify(Poco::Dynamic::Var(out));
    }

    std::string get(const std::string& hostName, const std::string& widgetId) const override {
        const std::vector<WidgetMatch> matches = findWidgets(hostName, widgetId);
        if (matches.size() != 1) return matchError(hostName, widgetId, matches.size());
        auto host = UIHost::resolve(matches[0].host);
        if (!host) return "error: UI host became stale";
        Poco::JSON::Object::Ptr out = nodeJson(*host->get().tree(), matches[0].nodeIndex);
        out->set("host", host->get().getName());
        out->set("hostVisible", host->get().meta()->visible);
        return stringify(Poco::Dynamic::Var(out));
    }

    std::string click(const std::string& hostName, const std::string& widgetId) override {
        const std::vector<WidgetMatch> matches = findWidgets(hostName, widgetId);
        if (matches.size() != 1) return matchError(hostName, widgetId, matches.size());
        auto host = UIHost::resolve(matches[0].host);
        if (!host) return "error: UI host became stale";
        UIHost&    resolvedHost = host->get();
        const auto tree         = resolvedHost.tree();
        if (matches[0].nodeIndex < 0 || matches[0].nodeIndex >= static_cast<int>(tree->nodes.size()))
            return "error: UI widget became stale";
        UINode& node = tree->nodes[static_cast<std::size_t>(matches[0].nodeIndex)];
        if (!resolvedHost.meta()->visible || !node.visible)
            return "error: widget is not visible: " + resolvedHost.getName() + "/" + widgetId;
        if (!isClickable(node)) return "error: widget is not clickable: " + resolvedHost.getName() + "/" + widgetId;

        UIEvent event;
        event.host         = matches[0].host;
        event.hostName     = resolvedHost.getName();
        event.nodeId       = node.id;
        event.nodeIndex    = matches[0].nodeIndex;
        event.kind         = "click";
        event.handlerIndex = node.handlerClick;
        UISystem::pendingEvents().push_back(std::move(event));

        Poco::JSON::Object::Ptr out(new Poco::JSON::Object());
        out->set("queued", true);
        out->set("host", resolvedHost.getName());
        out->set("widget", widgetId);
        out->set("event", "click");
        return stringify(Poco::Dynamic::Var(out));
    }

private:
    static int nodeIndex(const UIHost::Tree& tree, const UINode* node) {
        if (!node || tree.nodes.empty()) return -1;
        return static_cast<int>(node - tree.nodes.data());
    }
};

}  // namespace

void registerUIAutomationCapabilities() {
    static UIAutomationImpl impl;
    eve::cap::provide<eve::IUIAutomation>(&impl);
}

}  // namespace eve::ui

#endif
