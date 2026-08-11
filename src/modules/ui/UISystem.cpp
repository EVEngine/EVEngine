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

void walkNode(UIHost *host, UIHost::Tree *tree, int index);

void walkSiblings(UIHost *host, UIHost::Tree *tree, int index) {
    while (index >= 0 && index < int(tree->nodes.size())) {
        const int next = tree->nodes[size_t(index)].nextSibling;
        walkNode(host, tree, index);
        index = next;
    }
}

float estimateItemMainSize(const UINode &n, bool row) {
    if (row) {
        if (n.sizeX > 0.f) return n.sizeX;
        if (n.type == NodeType::Spacer) return 0.f;
        const ImGuiStyle &style = ImGui::GetStyle();
        switch (n.type) {
        case NodeType::Text:
            return ImGui::CalcTextSize(n.text.c_str()).x;
        case NodeType::Button: {
            const char *label = n.text.empty() ? "Button" : n.text.c_str();
            return ImGui::CalcTextSize(label).x + style.FramePadding.x * 2.f;
        }
        case NodeType::Checkbox: {
            const char *label = n.text.empty() ? "Check" : n.text.c_str();
            return ImGui::GetFrameHeight() + style.ItemInnerSpacing.x + ImGui::CalcTextSize(label).x;
        }
        case NodeType::Separator:
            return style.ItemSpacing.x;
        case NodeType::Progress:
            return 100.f;
        case NodeType::Slider:
        case NodeType::InputText:
            return 120.f;
        case NodeType::Child:
            return n.sizeX > 0.f ? n.sizeX : 80.f;
        case NodeType::Group:
        case NodeType::Flex:
        case NodeType::CollapsingHeader:
            return 0.f;  // nested; treat as flexible/unknown
        default:
            return 0.f;
        }
    }
    if (n.sizeY > 0.f) return n.sizeY;
    if (n.type == NodeType::Spacer) return 0.f;
    const ImGuiStyle &style = ImGui::GetStyle();
    switch (n.type) {
    case NodeType::Separator:
        return 1.f;
    case NodeType::Child:
        return n.sizeY > 0.f ? n.sizeY : style.FramePadding.y * 2.f;
    case NodeType::Text:
        return ImGui::GetTextLineHeight();
    default:
        return ImGui::GetFrameHeight();
    }
}

void emitSpacer(float main, float cross, bool row) {
    if (main < 0.f) main = 0.f;
    if (cross < 0.f) cross = 0.f;
    if (row) {
        if (main <= 0.f && cross <= 0.f) {
            ImGui::Dummy(ImVec2(0.f, 0.f));
        } else {
            ImGui::Dummy(ImVec2(main, cross > 0.f ? cross : 1.f));
        }
    } else {
        if (main <= 0.f && cross <= 0.f) {
            ImGui::Dummy(ImVec2(0.f, 0.f));
        } else {
            ImGui::Dummy(ImVec2(cross > 0.f ? cross : 1.f, main));
        }
    }
}

void applyCrossAlign(FlexAlign align, float itemCross, float lineCross, bool row) {
    if (align == FlexAlign::Start || align == FlexAlign::Stretch) return;
    if (lineCross <= 0.f || itemCross <= 0.f) return;
    const float delta = lineCross - itemCross;
    if (delta <= 0.f) return;
    float offset = 0.f;
    if (align == FlexAlign::Center) offset = delta * 0.5f;
    else if (align == FlexAlign::End) offset = delta;
    if (offset <= 0.f) return;
    if (row) {
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + offset);
    } else {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offset);
    }
}

void walkFlex(UIHost *host, UIHost::Tree *tree, UINode &flex) {
    const bool row = flex.flexDirection == FlexDirection::Row;
    const ImGuiStyle &style = ImGui::GetStyle();
    const float themeGap = row ? style.ItemSpacing.x : style.ItemSpacing.y;
    const float gap = flex.gap >= 0.f ? flex.gap : themeGap;

    std::vector<int> kids;
    for (int c = flex.firstChild; c >= 0; c = tree->nodes[size_t(c)].nextSibling) {
        if (c >= int(tree->nodes.size())) break;
        if (tree->nodes[size_t(c)].visible) kids.push_back(c);
    }
    if (kids.empty()) {
        ImGui::BeginGroup();
        ImGui::EndGroup();
        return;
    }

    const int nKids = int(kids.size());
    const int gapCount = std::max(0, nKids - 1);

    float growSum = 0.f;
    float fixedSum = 0.f;
    std::vector<float> basis(size_t(nKids), 0.f);
    std::vector<float> grow(size_t(nKids), 0.f);
    float maxCross = 0.f;

    for (int i = 0; i < nKids; ++i) {
        const UINode &child = tree->nodes[size_t(kids[size_t(i)])];
        float g = child.flexGrow;
        if (child.type == NodeType::Spacer && g <= 0.f) g = 1.f;
        grow[size_t(i)] = std::max(0.f, g);
        growSum += grow[size_t(i)];

        const float b = estimateItemMainSize(child, row);
        basis[size_t(i)] = b;
        if (grow[size_t(i)] <= 0.f) fixedSum += b;

        const float cross = row ? (child.sizeY > 0.f ? child.sizeY : ImGui::GetFrameHeight())
                                : (child.sizeX > 0.f ? child.sizeX : 0.f);
        maxCross = std::max(maxCross, cross);
    }

    const float avail = row ? ImGui::GetContentRegionAvail().x : ImGui::GetContentRegionAvail().y;
    float freeSpace = avail - fixedSum - float(gapCount) * gap;
    // Natural-sized grow=0 items already counted in fixedSum; growing items start from basis.
    for (int i = 0; i < nKids; ++i) {
        if (grow[size_t(i)] > 0.f) freeSpace -= basis[size_t(i)];
    }
    if (freeSpace < 0.f) freeSpace = 0.f;

    // Justify consumes free space when nothing grows.
    float leading = 0.f;
    float betweenExtra = 0.f;
    float aroundEdge = 0.f;
    if (growSum <= 0.f && freeSpace > 0.f) {
        switch (flex.justifyContent) {
        case FlexJustify::Center:
            leading = freeSpace * 0.5f;
            break;
        case FlexJustify::End:
            leading = freeSpace;
            break;
        case FlexJustify::SpaceBetween:
            if (gapCount > 0) betweenExtra = freeSpace / float(gapCount);
            break;
        case FlexJustify::SpaceAround:
            aroundEdge = freeSpace / float(nKids * 2);
            betweenExtra = aroundEdge * 2.f;
            leading = aroundEdge;
            break;
        case FlexJustify::Start:
        default:
            break;
        }
        freeSpace = 0.f;
    }

    std::vector<float> mainSize(size_t(nKids), 0.f);
    for (int i = 0; i < nKids; ++i) {
        float extra = 0.f;
        if (growSum > 0.f && grow[size_t(i)] > 0.f)
            extra = freeSpace * (grow[size_t(i)] / growSum);
        mainSize[size_t(i)] = basis[size_t(i)] + extra;
    }

    ImGui::BeginGroup();
    if (leading > 0.f) emitSpacer(leading, maxCross > 0.f ? maxCross : 1.f, row);

    for (int i = 0; i < nKids; ++i) {
        if (i > 0) {
            const float spacing = gap + betweenExtra;
            if (row) {
                ImGui::SameLine(0.f, spacing);
            } else if (spacing > themeGap) {
                ImGui::Dummy(ImVec2(0.f, spacing - themeGap));
            } else if (spacing < themeGap && spacing >= 0.f) {
                ImGui::Dummy(ImVec2(0.f, 0.f));
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() - (themeGap - spacing));
            }
        }

        UINode &child = tree->nodes[size_t(kids[size_t(i)])];
        const float cross = row ? (child.sizeY > 0.f ? child.sizeY : ImGui::GetFrameHeight())
                                : (child.sizeX > 0.f ? child.sizeX : estimateItemMainSize(child, true));
        applyCrossAlign(flex.alignItems, cross, maxCross, row);

        if (child.type == NodeType::Spacer) {
            emitSpacer(mainSize[size_t(i)], child.sizeY > 0.f ? child.sizeY : (row ? 1.f : 0.f), row);
            continue;
        }

        bool pushedWidth = false;
        const float oldSizeX = child.sizeX;
        const float oldSizeY = child.sizeY;
        if (row && (grow[size_t(i)] > 0.f || child.sizeX > 0.f)) {
            child.sizeX = mainSize[size_t(i)];
            ImGui::PushItemWidth(mainSize[size_t(i)]);
            pushedWidth = true;
        } else if (!row && grow[size_t(i)] > 0.f) {
            child.sizeY = mainSize[size_t(i)];
        } else if (!row && flex.alignItems == FlexAlign::Stretch) {
            const float w = ImGui::GetContentRegionAvail().x;
            if (w > 0.f) {
                ImGui::PushItemWidth(w);
                pushedWidth = true;
            }
        } else if (!row && child.sizeX > 0.f) {
            ImGui::PushItemWidth(child.sizeX);
            pushedWidth = true;
        }

        walkNode(host, tree, kids[size_t(i)]);

        child.sizeX = oldSizeX;
        child.sizeY = oldSizeY;
        if (pushedWidth) ImGui::PopItemWidth();
    }

    if (aroundEdge > 0.f) {
        if (row) ImGui::SameLine(0.f, aroundEdge);
        emitSpacer(aroundEdge, maxCross > 0.f ? maxCross : 1.f, row);
    }
    ImGui::EndGroup();
}

void walkNode(UIHost *host, UIHost::Tree *tree, int index) {
    if (index < 0 || index >= int(tree->nodes.size())) return;
    UINode &n = tree->nodes[size_t(index)];
    if (!n.visible) return;

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
                if (n.firstChild >= 0) walkSiblings(host, tree, n.firstChild);
                ImGui::EndPopup();
            }
        } else {
            ImGui::Begin(title.c_str(), nullptr, flags);
            if (n.firstChild >= 0) walkSiblings(host, tree, n.firstChild);
            ImGui::End();
        }
        break;
    }
    case NodeType::Text:
        ImGui::TextUnformatted(n.text.c_str());
        break;
    case NodeType::Button: {
        const char *label = n.text.empty() ? "Button" : n.text.c_str();
        const bool sized = n.sizeX > 0.f || n.sizeY > 0.f;
        const bool clicked =
            sized ? ImGui::Button(label, ImVec2(n.sizeX > 0.f ? n.sizeX : 0.f,
                                                n.sizeY > 0.f ? n.sizeY : 0.f))
                  : ImGui::Button(label);
        if (clicked) pushPending(host, n, "click", n.handlerClick);
        break;
    }
    case NodeType::SameLine:
        ImGui::SameLine();
        break;
    case NodeType::Group:
        ImGui::BeginGroup();
        if (n.firstChild >= 0) walkSiblings(host, tree, n.firstChild);
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
            if (n.firstChild >= 0) walkSiblings(host, tree, n.firstChild);
        }
        break;
    }
    case NodeType::Child: {
        ImVec2 size(n.sizeX, n.sizeY);
        const char *cid = n.id.empty() ? "child" : n.id.c_str();
        if (ImGui::BeginChild(cid, size, true)) {
            if (n.firstChild >= 0) walkSiblings(host, tree, n.firstChild);
        }
        ImGui::EndChild();
        break;
    }
    case NodeType::Flex:
        walkFlex(host, tree, n);
        break;
    case NodeType::Spacer:
        // Outside Flex: honor explicit size, otherwise a tiny dummy.
        emitSpacer(n.sizeX > 0.f ? n.sizeX : 0.f, n.sizeY > 0.f ? n.sizeY : 0.f, true);
        break;
    }
}

void walk(UIHost *host, UIHost::Tree *tree, int index) { walkSiblings(host, tree, index); }

}  // namespace

std::vector<UIEvent> &UISystem::pendingEvents() { return g_pending; }
std::vector<UIClick> &UISystem::clickQueue() { return g_clicks; }
std::vector<UIChange> &UISystem::changeQueue() { return g_changes; }

UIHost *UISystem::findHost(const std::string &name) {
    if (name.empty()) return nullptr;
    if (ecs::current()->getManager<UIHost>() == nullptr) return nullptr;
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
    if (ecs::current()->getManager<UIHost>() == nullptr) return nullptr;
    auto view = ecs::View<UIHost, UIHost::Meta, UIHost::Tree>();
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto [meta, tree] = *it;
        (void)tree;
        if (meta->ownerId == ownerId) return meta->entity;
    }
    return nullptr;
}

void UISystem::render() {
    if (ecs::current()->getManager<UIHost>() == nullptr) return;

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
