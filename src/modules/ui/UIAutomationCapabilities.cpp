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
    return node.type == NodeType::Button || node.type == NodeType::ImageButton ||
           node.type == NodeType::Switch || node.type == NodeType::MenuItem ||
           node.handlerClick != 0;
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
    out->set("clickable", isClickable(node));
    if (node.type == NodeType::Checkbox || node.type == NodeType::Switch ||
        node.type == NodeType::MenuItem)
        out->set("checked", node.checked);
    if (node.type == NodeType::Slider || node.type == NodeType::Progress ||
        node.type == NodeType::Combo || node.type == NodeType::SplitPane) {
        out->set("value", node.value);
        out->set("min", node.minValue);
        out->set("max", node.maxValue);
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

Poco::JSON::Object::Ptr hostJson(UIHost* host) {
    Poco::JSON::Object::Ptr out(new Poco::JSON::Object());
    const auto              meta = host->meta();
    const auto              tree = host->tree();
    out->set("name", meta->name);
    out->set("visible", meta->visible);
    out->set("layer", meta->layer);
    out->set("modal", meta->modal);
    out->set("overlay", meta->overlay);
    out->set("nodeCount", static_cast<int>(tree->nodes.size()));
    if (tree->root >= 0) out->set("root", nodeJson(*tree, tree->root));
    return out;
}

std::vector<UIHost*> hosts() {
    std::vector<UIHost*> result;
    if (ecs::current()->getManager<UIHost>() == nullptr) return result;
    auto view = ecs::View<UIHost, UIHost::Meta, UIHost::Tree>();
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto [meta, tree] = *it;
        (void)tree;
        if (meta->entity) result.push_back(meta->entity);
    }
    return result;
}

struct WidgetMatch {
    UIHost* host = nullptr;
    UINode* node = nullptr;
};

std::vector<WidgetMatch> findWidgets(const std::string& hostName, const std::string& widgetId) {
    std::vector<WidgetMatch> result;
    for (UIHost* host : hosts()) {
        if (!hostName.empty() && host->getName() != hostName) continue;
        if (UINode* node = host->findById(widgetId)) result.push_back({host, node});
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
        for (UIHost* host : hosts()) {
            if (!hostName.empty() && host->getName() != hostName) continue;
            items->add(hostJson(host));
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

        Poco::JSON::Object::Ptr out =
            nodeJson(*matches[0].host->tree(), nodeIndex(*matches[0].host->tree(), matches[0].node));
        out->set("host", matches[0].host->getName());
        out->set("hostVisible", matches[0].host->meta()->visible);
        return stringify(Poco::Dynamic::Var(out));
    }

    std::string click(const std::string& hostName, const std::string& widgetId) override {
        const std::vector<WidgetMatch> matches = findWidgets(hostName, widgetId);
        if (matches.size() != 1) return matchError(hostName, widgetId, matches.size());
        UIHost* host = matches[0].host;
        UINode* node = matches[0].node;
        if (!host->meta()->visible || !node->visible)
            return "error: widget is not visible: " + host->getName() + "/" + widgetId;
        if (!isClickable(*node)) return "error: widget is not clickable: " + host->getName() + "/" + widgetId;

        UIEvent event;
        event.host         = host;
        event.hostName     = host->getName();
        event.nodeId       = node->id;
        event.kind         = "click";
        event.handlerIndex = node->handlerClick;
        UISystem::pendingEvents().push_back(std::move(event));

        Poco::JSON::Object::Ptr out(new Poco::JSON::Object());
        out->set("queued", true);
        out->set("host", host->getName());
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
