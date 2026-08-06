#include "ui/UISystem.h"

#include "ui/Theme.h"

#include <imgui.h>

#include <algorithm>
#include <cstring>
#include <vector>

namespace eve::ui {
namespace {

std::vector<UIEvent> g_pending;
std::vector<UIClick> g_clicks;
std::vector<UIChange> g_changes;

void pushPending(UIHost *host, const UINode &n, const char *kind, uint32_t handlerIndex,
                 bool toggleValue = false, float floatValue = 0.f, std::string textValue = {}) {
    UIEvent ev;
    ev.host = host;
    ev.hostName = host ? host->meta()->name : "";
    ev.nodeId = n.id;
    ev.kind = kind;
    ev.handlerIndex = handlerIndex;
    ev.toggleValue = toggleValue;
    ev.floatValue = floatValue;
    ev.textValue = std::move(textValue);
    g_pending.push_back(std::move(ev));
}

void walk(UIHost *host, UIHost::Tree *tree, int index) {
    if (index < 0 || index >= int(tree->nodes.size())) return;
    UINode &n = tree->nodes[size_t(index)];
    if (!n.visible) {
        if (n.nextSibling >= 0) walk(host, tree, n.nextSibling);
        return;
    }

    switch (n.type) {
    case NodeType::Window: {
        std::string title = n.text.empty() ? "Window" : n.text;
        if (host && !host->meta()->name.empty()) title = host->meta()->name + "/" + title;
        const bool modal = host && host->meta()->modal;
        ImGuiWindowFlags flags = 0;
        if (host && host->meta()->hasPos) {
            ImGui::SetNextWindowPos(ImVec2(host->meta()->posX, host->meta()->posY), ImGuiCond_Always,
                                    ImVec2(host->meta()->pivotX, host->meta()->pivotY));
            flags |= ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize;
        }
        if (host && host->meta()->overlay) {
            flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_AlwaysAutoResize;
            ImGui::SetNextWindowBgAlpha(0.4f);
        }
        if (modal) {
            ImGui::OpenPopup(title.c_str());
            if (ImGui::BeginPopupModal(title.c_str(), nullptr,
                                       ImGuiWindowFlags_AlwaysAutoResize | flags)) {
                if (n.firstChild >= 0) walk(host, tree, n.firstChild);
                ImGui::EndPopup();
            }
        } else {
            ImGui::Begin(title.c_str(), nullptr, flags);
            if (n.firstChild >= 0) walk(host, tree, n.firstChild);
            ImGui::End();
        }
        break;
    }
    case NodeType::Text:
        ImGui::TextUnformatted(n.text.c_str());
        break;
    case NodeType::Button:
        if (ImGui::Button(n.text.empty() ? "Button" : n.text.c_str())) {
            pushPending(host, n, "click", n.handlerClick);
        }
        break;
    case NodeType::SameLine:
        ImGui::SameLine();
        break;
    case NodeType::Group:
        ImGui::BeginGroup();
        if (n.firstChild >= 0) walk(host, tree, n.firstChild);
        ImGui::EndGroup();
        break;
    case NodeType::Separator:
        ImGui::Separator();
        break;
    case NodeType::Checkbox: {
        bool checked = n.checked;
        if (ImGui::Checkbox(n.text.empty() ? "Check" : n.text.c_str(), &checked)) {
            n.checked = checked;
            pushPending(host, n, "toggle", n.handlerToggle, checked);
        }
        break;
    }
    case NodeType::Slider: {
        float v = n.value;
        const char *label = n.text.empty() ? "Slider" : n.text.c_str();
        if (ImGui::SliderFloat(label, &v, n.minValue, n.maxValue)) {
            n.value = v;
            pushPending(host, n, "value", n.handlerValue, false, v);
        }
        break;
    }
    case NodeType::Progress: {
        const char *overlay = n.text.empty() ? nullptr : n.text.c_str();
        ImGui::ProgressBar(n.value, ImVec2(-1.f, 0.f), overlay);
        break;
    }
    case NodeType::InputText: {
        char buf[256];
        std::memset(buf, 0, sizeof(buf));
        if (!n.valueText.empty()) {
            std::strncpy(buf, n.valueText.c_str(), sizeof(buf) - 1);
        }
        const char *label = n.text.empty() ? "Input" : n.text.c_str();
        if (ImGui::InputText(label, buf, sizeof(buf))) {
            n.valueText = buf;
            pushPending(host, n, "text", n.handlerText, false, 0.f, n.valueText);
        }
        break;
    }
    case NodeType::CollapsingHeader: {
        ImGuiTreeNodeFlags flags = n.open ? ImGuiTreeNodeFlags_DefaultOpen : 0;
        const char *label = n.text.empty() ? "Section" : n.text.c_str();
        if (ImGui::CollapsingHeader(label, flags)) {
            if (n.firstChild >= 0) walk(host, tree, n.firstChild);
        }
        break;
    }
    case NodeType::Child: {
        ImVec2 size(n.sizeX, n.sizeY);
        const char *cid = n.id.empty() ? "child" : n.id.c_str();
        if (ImGui::BeginChild(cid, size, true)) {
            if (n.firstChild >= 0) walk(host, tree, n.firstChild);
        }
        ImGui::EndChild();
        break;
    }
    }

    if (n.nextSibling >= 0) walk(host, tree, n.nextSibling);
}

}  // namespace

std::vector<UIEvent> &UISystem::pendingEvents() { return g_pending; }
std::vector<UIClick> &UISystem::clickQueue() { return g_clicks; }
std::vector<UIChange> &UISystem::changeQueue() { return g_changes; }

UIHost *UISystem::findHost(const std::string &name) {
    if (name.empty()) return nullptr;
    if (ecs::ComponentManager<UIHost>::inst().registy == nullptr) return nullptr;
    auto view = ecs::View<UIHost, UIHost::Meta, UIHost::Tree>();
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto [meta, tree] = *it;
        (void)tree;
        if (meta->name == name) return meta->entity;
    }
    return nullptr;
}

UIHost *UISystem::findHostByOwner(uint32_t ownerId) {
    if (ownerId == 0) return nullptr;
    if (ecs::ComponentManager<UIHost>::inst().registy == nullptr) return nullptr;
    auto view = ecs::View<UIHost, UIHost::Meta, UIHost::Tree>();
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto [meta, tree] = *it;
        (void)tree;
        if (meta->ownerId == ownerId) return meta->entity;
    }
    return nullptr;
}

void UISystem::render() {
    if (ecs::ComponentManager<UIHost>::inst().registy == nullptr) return;

    applyThemeToImGui(globalTheme());

    struct Item {
        UIHost *host;
        UIHost::Meta *meta;
        UIHost::Tree *tree;
    };
    std::vector<Item> items;

    auto view = ecs::View<UIHost, UIHost::Meta, UIHost::Tree>();
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto [meta, tree] = *it;
        if (!meta->visible) continue;
        UIHost *host = meta->entity;
        if (!host) continue;
        items.push_back(Item{host, meta, tree});
    }

    std::stable_sort(items.begin(), items.end(),
                     [](const Item &a, const Item &b) { return a.meta->layer < b.meta->layer; });

    for (auto &item : items) {
        item.tree->dirty = false;
        if (item.tree->root >= 0) walk(item.host, item.tree, item.tree->root);
    }
}

void UISystem::dispatchEvents() {
    auto events = std::move(g_pending);
    g_pending.clear();
    for (auto &ev : events) {
        if (!ev.host) continue;

        if (ev.kind == "click" && !ev.nodeId.empty()) {
            UIClick c;
            c.hostName = ev.hostName.empty() ? ev.host->meta()->name : ev.hostName;
            c.nodeId = ev.nodeId;
            g_clicks.push_back(std::move(c));
        }

        if ((ev.kind == "toggle" || ev.kind == "value" || ev.kind == "text") && !ev.nodeId.empty()) {
            UIChange ch;
            ch.hostName = ev.hostName.empty() ? ev.host->meta()->name : ev.hostName;
            ch.nodeId = ev.nodeId;
            ch.kind = ev.kind;
            g_changes.push_back(std::move(ch));
        }

        if (ev.kind == "toggle" && ev.handlerIndex != 0) {
            auto t = ev.host->tree();
            size_t idx = size_t(ev.handlerIndex - 1);
            if (idx < t->toggleHandlers.size() && t->toggleHandlers[idx])
                t->toggleHandlers[idx](ev.toggleValue);
            continue;
        }
        if (ev.kind == "value" && ev.handlerIndex != 0) {
            auto t = ev.host->tree();
            size_t idx = size_t(ev.handlerIndex - 1);
            if (idx < t->valueHandlers.size() && t->valueHandlers[idx])
                t->valueHandlers[idx](ev.floatValue);
            continue;
        }
        if (ev.kind == "text" && ev.handlerIndex != 0) {
            auto t = ev.host->tree();
            size_t idx = size_t(ev.handlerIndex - 1);
            if (idx < t->textHandlers.size() && t->textHandlers[idx])
                t->textHandlers[idx](ev.textValue);
            continue;
        }
        if (ev.handlerIndex == 0) continue;
        auto t = ev.host->tree();
        size_t idx = size_t(ev.handlerIndex - 1);
        if (idx >= t->clickHandlers.size()) continue;
        if (t->clickHandlers[idx]) t->clickHandlers[idx]();
    }
}

std::string UISystem::consumeClick() {
    if (g_clicks.empty()) return {};
    UIClick c = std::move(g_clicks.front());
    g_clicks.erase(g_clicks.begin());
    if (c.hostName.empty()) return c.nodeId;
    return c.hostName + "/" + c.nodeId;
}

std::string UISystem::consumeClickFor(const std::string &hostName) {
    for (auto it = g_clicks.begin(); it != g_clicks.end(); ++it) {
        if (it->hostName != hostName) continue;
        std::string id = std::move(it->nodeId);
        g_clicks.erase(it);
        return id;
    }
    return {};
}

std::string UISystem::consumeChange() {
    if (g_changes.empty()) return {};
    UIChange c = std::move(g_changes.front());
    g_changes.erase(g_changes.begin());
    if (c.hostName.empty()) return c.nodeId;
    return c.hostName + "/" + c.nodeId;
}

}  // namespace eve::ui
