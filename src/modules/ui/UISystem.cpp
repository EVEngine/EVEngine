#include "ui/UISystem.h"

#include "ui/Layout.h"
#include "ui/Icons.h"
#include "ui/Theme.h"
#include "ui/UIBackend.h"

#include "common/Module.h"
#include "graphics/Graphics.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <map>
#include <vector>

namespace eve::ui {
namespace {

std::vector<UIEvent> g_pending;
std::vector<UIClick> g_clicks;
std::vector<UIChange> g_changes;
UIBackend *g_backend = nullptr;
UIStats g_stats;
std::map<std::string, ViewportState> g_viewports;

std::string viewportKey(UIHost *host, const UINode &n) {
    const std::string hostName =
        host && !host->meta()->name.empty() ? host->meta()->name : "?";
    return hostName + "/" + (n.id.empty() ? "viewport" : n.id);
}

void pushPending(UIHost *host, const UINode &n, const char *kind, uint32_t handlerIndex,
                 bool toggleValue = false, float floatValue = 0.f, std::string textValue = {}) {
    if (!n.enabled || n.mouseFilter == MouseFilter::Ignore) return;
    UIEvent ev;
    if (!host) return;
    ev.host = host->handle();
    ev.hostName = host ? host->meta()->name : "";
    ev.nodeId = n.id;
    if (host) {
        auto tree = host->tree();
        if (!tree->nodes.empty()) ev.nodeIndex = int(&n - tree->nodes.data());
    }
    ev.kind = kind;
    ev.handlerIndex = handlerIndex;
    ev.toggleValue = toggleValue;
    ev.floatValue = floatValue;
    ev.textValue = std::move(textValue);
    g_pending.push_back(std::move(ev));
}

std::string nodeLabel(const UINode &node, const char *fallback) {
    std::string label = node.text.empty() ? fallback : node.text;
    if (!node.id.empty()) label += "###" + node.id;
    return label;
}

bool privateUseGlyph(const std::string &text, ImWchar *out) {
    if (text.size() != 3) return false;
    const auto b0 = static_cast<unsigned char>(text[0]);
    const auto b1 = static_cast<unsigned char>(text[1]);
    const auto b2 = static_cast<unsigned char>(text[2]);
    if ((b0 & 0xf0u) != 0xe0u || (b1 & 0xc0u) != 0x80u || (b2 & 0xc0u) != 0x80u)
        return false;
    const unsigned int codepoint =
        ((b0 & 0x0fu) << 12u) | ((b1 & 0x3fu) << 6u) | (b2 & 0x3fu);
    if (codepoint < 0xe000u || codepoint > 0xf8ffu) return false;
    if (out) *out = static_cast<ImWchar>(codepoint);
    return true;
}

bool isInteractive(NodeType type) {
    switch (type) {
        case NodeType::Button:
        case NodeType::Checkbox:
        case NodeType::Slider:
        case NodeType::InputText:
        case NodeType::ImageButton:
        case NodeType::Combo:
        case NodeType::Viewport:
        case NodeType::SearchField:
        case NodeType::Switch:
        case NodeType::MenuItem: return true;
        default: return false;
    }
}

bool keyPressed(ImGuiKey key) {
    const int index = ImGui::GetKeyIndex(key);
    return index >= 0 && ImGui::IsKeyPressed(index, false);
}

void routeExplicitFocusInput(UIHost *host, const UINode &node) {
    if (!host || !node.focused || node.focusMode != FocusMode::All) return;
    if (keyPressed(ImGuiKey_Tab)) {
        const bool backwards = ImGui::GetIO().KeyShift;
        const std::string &target = backwards ? node.focusPrevious : node.focusNext;
        if (!target.empty())
            host->moveFocus(backwards ? FocusDirection::Previous : FocusDirection::Next);
        return;
    }
    if (ImGui::IsItemActive()) return;
    if (!node.focusLeft.empty() && keyPressed(ImGuiKey_LeftArrow))
        host->moveFocus(FocusDirection::Left);
    else if (!node.focusRight.empty() && keyPressed(ImGuiKey_RightArrow))
        host->moveFocus(FocusDirection::Right);
    else if (!node.focusUp.empty() && keyPressed(ImGuiKey_UpArrow))
        host->moveFocus(FocusDirection::Up);
    else if (!node.focusDown.empty() && keyPressed(ImGuiKey_DownArrow))
        host->moveFocus(FocusDirection::Down);
}

void walkNode(UIHost *host, UIHost::Tree *tree, int index);

void walkSiblings(UIHost *host, UIHost::Tree *tree, int index) {
    while (index >= 0 && index < int(tree->nodes.size())) {
        const int next = tree->nodes[size_t(index)].nextSibling;
        UINode &node = tree->nodes[size_t(index)];
        // Desktop composition behaves like CSS grid rows: menu/toolbar and
        // status bar keep their intrinsic height while the workspace consumes
        // the remaining block size. An explicit split height still wins.
        if (node.type == NodeType::SplitPane && node.sizeY <= 0.f && next >= 0 &&
            next < int(tree->nodes.size()) &&
            tree->nodes[size_t(next)].type == NodeType::StatusBar) {
            const float oldY = node.sizeY;
            const float reserve = tree->nodes[size_t(next)].measuredH +
                                  ImGui::GetStyle().ItemSpacing.y;
            node.sizeY = std::max(1.f, ImGui::GetContentRegionAvail().y - reserve);
            walkNode(host, tree, index);
            node.sizeY = oldY;
        } else {
            walkNode(host, tree, index);
        }
        index = next;
    }
}

void walkPaneChild(UIHost *host, UIHost::Tree *tree, int index) {
    if (index < 0 || index >= int(tree->nodes.size())) return;
    UINode &child = tree->nodes[size_t(index)];
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float oldX = child.sizeX;
    const float oldY = child.sizeY;
    // A direct SplitPane child fills its allocated pane. Descendants can still
    // specialize their own dimensions through normal widget layout props.
    child.sizeX = std::max(1.f, avail.x);
    child.sizeY = std::max(1.f, avail.y);
    child.measuredW = child.sizeX;
    child.measuredH = child.sizeY;
    walkNode(host, tree, index);
    child.sizeX = oldX;
    child.sizeY = oldY;
}

bool hasDirectChildType(const UIHost::Tree &tree, const UINode &parent, NodeType type) {
    for (int child = parent.firstChild; child >= 0;
         child = tree.nodes[size_t(child)].nextSibling) {
        if (child >= int(tree.nodes.size())) break;
        if (tree.nodes[size_t(child)].type == type) return true;
    }
    return false;
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

/** Draw a texture with optional nine-patch borders (ImGui 1.83 has no AddImageNineSlice). */
void drawNinePatch(void *handle, const ImVec2 &pos, const ImVec2 &size, const ImVec2 &uv0,
                   const ImVec2 &uv1, float bL, float bT, float bR, float bB, int texW, int texH,
                   ImU32 tint) {
    ImDrawList *dl = ImGui::GetWindowDrawList();
    const auto draw = [&](float x0, float y0, float x1, float y1, float u0, float v0, float u1,
                          float v1) {
        dl->AddImage(static_cast<ImTextureID>(handle), ImVec2(pos.x + x0, pos.y + y0),
                     ImVec2(pos.x + x1, pos.y + y1), ImVec2(u0, v0), ImVec2(u1, v1), tint);
    };
    if (bL <= 0.f && bT <= 0.f && bR <= 0.f && bB <= 0.f) {
        draw(0.f, 0.f, size.x, size.y, uv0.x, uv0.y, uv1.x, uv1.y);
        return;
    }
    if (texW <= 0 || texH <= 0) {
        draw(0.f, 0.f, size.x, size.y, uv0.x, uv0.y, uv1.x, uv1.y);
        return;
    }
    const float uPerPx = (uv1.x - uv0.x) / float(texW);
    const float vPerPx = (uv1.y - uv0.y) / float(texH);
    const float uL = uv0.x + std::min(bL, size.x * 0.5f) * uPerPx;
    const float uR = uv1.x - std::min(bR, size.x * 0.5f) * uPerPx;
    const float vT = uv0.y + std::min(bT, size.y * 0.5f) * vPerPx;
    const float vB = uv1.y - std::min(bB, size.y * 0.5f) * vPerPx;
    const float bl = std::min(bL, size.x * 0.5f);
    const float br = std::min(bR, size.x * 0.5f);
    const float bt = std::min(bT, size.y * 0.5f);
    const float bb = std::min(bB, size.y * 0.5f);
    // Corners
    draw(0.f, 0.f, bl, bt, uv0.x, uv0.y, uL, vT);
    draw(size.x - br, 0.f, size.x, bt, uR, uv0.y, uv1.x, vT);
    draw(0.f, size.y - bb, bl, size.y, uv0.x, vB, uL, uv1.y);
    draw(size.x - br, size.y - bb, size.x, size.y, uR, vB, uv1.x, uv1.y);
    // Edges
    draw(bl, 0.f, size.x - br, bt, uL, uv0.y, uR, vT);
    draw(bl, size.y - bb, size.x - br, size.y, uL, vB, uR, uv1.y);
    draw(0.f, bt, bl, size.y - bb, uv0.x, vT, uL, vB);
    draw(size.x - br, bt, size.x, size.y - bb, uR, vT, uv1.x, vB);
    // Center
    draw(bl, bt, size.x - br, size.y - bb, uL, vT, uR, vB);
}

/** Queue the same nine-patch geometry for backends without per-command texture handles. */
void queueNinePatch(uint64_t textureId, const ImVec2 &pos, const ImVec2 &size,
                    const ImVec2 &uv0, const ImVec2 &uv1, float bL, float bT, float bR,
                    float bB, int texW, int texH, float r, float g, float b, float a,
                    bool opaque = false) {
    if (!g_backend) return;
    const auto draw = [&](float x0, float y0, float x1, float y1, float u0, float v0,
                          float u1, float v1) {
        if (x1 <= x0 || y1 <= y0) return;
        g_backend->queueTextureDraw(textureId, pos.x + x0, pos.y + y0, x1 - x0, y1 - y0,
                                    u0, v0, u1, v1, r, g, b, a, opaque);
    };
    if ((bL <= 0.f && bT <= 0.f && bR <= 0.f && bB <= 0.f) || texW <= 0 || texH <= 0) {
        draw(0.f, 0.f, size.x, size.y, uv0.x, uv0.y, uv1.x, uv1.y);
        return;
    }
    const float bl = std::min(bL, size.x * 0.5f);
    const float br = std::min(bR, size.x * 0.5f);
    const float bt = std::min(bT, size.y * 0.5f);
    const float bb = std::min(bB, size.y * 0.5f);
    const float uPerPx = (uv1.x - uv0.x) / float(texW);
    const float vPerPx = (uv1.y - uv0.y) / float(texH);
    const float uL = uv0.x + bl * uPerPx;
    const float uR = uv1.x - br * uPerPx;
    const float vT = uv0.y + bt * vPerPx;
    const float vB = uv1.y - bb * vPerPx;
    draw(0.f, 0.f, bl, bt, uv0.x, uv0.y, uL, vT);
    draw(size.x - br, 0.f, size.x, bt, uR, uv0.y, uv1.x, vT);
    draw(0.f, size.y - bb, bl, size.y, uv0.x, vB, uL, uv1.y);
    draw(size.x - br, size.y - bb, size.x, size.y, uR, vB, uv1.x, uv1.y);
    draw(bl, 0.f, size.x - br, bt, uL, uv0.y, uR, vT);
    draw(bl, size.y - bb, size.x - br, size.y, uL, vB, uR, uv1.y);
    draw(0.f, bt, bl, size.y - bb, uv0.x, vT, uL, vB);
    draw(size.x - br, bt, size.x, size.y - bb, uR, vT, uv1.x, vB);
    draw(bl, bt, size.x - br, size.y - bb, uL, vT, uR, vB);
}

void walkFlex(UIHost *host, UIHost::Tree *tree, UINode &flex) {
    const bool row = flex.flexDirection == FlexDirection::Row;
    const ImGuiStyle &style = ImGui::GetStyle();
    const float gap = flex.gap >= 0.f ? flex.gap : (row ? style.ItemSpacing.x : style.ItemSpacing.y);

    // Own content box: an explicit size (set by the parent's arrange) wins;
    // otherwise use the available region (root flex inside a window/child).
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const bool fillsContainer =
        flex.type == NodeType::Toolbar || flex.type == NodeType::StatusBar;
    const float flexW = flex.sizeX > 0.f ? flex.sizeX : avail.x;
    const float flexH =
        flex.sizeY > 0.f ? flex.sizeY : (fillsContainer ? avail.y : flex.measuredH);
    const float padMain = row ? flex.paddingL + flex.paddingR : flex.paddingT + flex.paddingB;
    const float padCross = row ? flex.paddingT + flex.paddingB : flex.paddingL + flex.paddingR;
    const float availMain = (row ? flexW : flexH) - padMain;
    const float availCross = (row ? flexH : flexW) - padCross;

    std::vector<int> kids;
    std::vector<int> absKids;
    std::vector<FlexItemSpec> specs;
    for (int c = flex.firstChild; c >= 0; c = tree->nodes[size_t(c)].nextSibling) {
        if (c >= int(tree->nodes.size())) break;
        UINode &child = tree->nodes[size_t(c)];
        if (!child.visible) continue;
        if (child.absolute) {
            absKids.push_back(c);
            continue;
        }
        kids.push_back(c);
        FlexItemSpec s;
        s.basisMain = row ? child.measuredW : child.measuredH;
        s.basisCross = row ? child.measuredH : child.measuredW;
        s.marginBefore = row ? child.marginL : child.marginT;
        s.marginAfter = row ? child.marginR : child.marginB;
        s.marginCrossBefore = row ? child.marginT : child.marginL;
        s.marginCrossAfter = row ? child.marginB : child.marginR;
        s.flexGrow = child.flexGrow;
        s.isSpacer = child.type == NodeType::Spacer;
        s.minMain = row ? child.minSizeX : child.minSizeY;
        s.maxMain = row ? child.maxSizeX : child.maxSizeY;
        s.minCross = row ? child.minSizeY : child.minSizeX;
        s.maxCross = row ? child.maxSizeY : child.maxSizeX;
        s.percentMain = row ? child.percentW : child.percentH;
        s.percentCross = row ? child.percentH : child.percentW;
        s.explicitMain = row ? child.sizeX : child.sizeY;
        s.explicitCross = row ? child.sizeY : child.sizeX;
        specs.push_back(s);
    }

    const FlexResult res =
        flexArrange(row, gap, std::max(0.f, availMain), std::max(0.f, availCross),
                    flex.alignItems, flex.justifyContent, specs);
    const ImVec2 cursor = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(std::max(0.f, flexW), std::max(0.f, flexH)));
    const ImVec2 flowEnd = ImGui::GetCursorScreenPos();
    const ImVec2 origin = ImVec2(cursor.x + flex.paddingL, cursor.y + flex.paddingT);

    for (size_t i = 0; i < kids.size(); ++i) {
        UINode &child = tree->nodes[size_t(kids[i])];
        const FlexRect &r = res.items[i];
        ImGui::SetCursorScreenPos(ImVec2(origin.x + r.x, origin.y + r.y));
        const float oldX = child.sizeX;
        const float oldY = child.sizeY;
        bool pushedWidth = false;
        if (r.w > 0.f) child.sizeX = r.w;
        if (r.h > 0.f) child.sizeY = r.h;
        if (row || r.w > 0.f) {
            ImGui::PushItemWidth(r.w > 0.f ? r.w : 1.f);
            pushedWidth = true;
        }
        walkNode(host, tree, kids[i]);
        child.sizeX = oldX;
        child.sizeY = oldY;
        if (pushedWidth) ImGui::PopItemWidth();
    }

    // Absolutely placed children are excluded from flow; draw on top of it.
    for (int c : absKids) {
        UINode &child = tree->nodes[size_t(c)];
        float w = child.percentW > 0.f ? child.percentW * flexW : child.measuredW;
        float h = child.percentH > 0.f ? child.percentH * flexH : child.measuredH;
        if (child.sizeX > 0.f) w = child.sizeX;
        if (child.sizeY > 0.f) h = child.sizeY;
        if (child.minSizeX > 0.f) w = std::max(w, child.minSizeX);
        if (child.minSizeY > 0.f) h = std::max(h, child.minSizeY);
        if (child.maxSizeX > 0.f) w = std::min(w, child.maxSizeX);
        if (child.maxSizeY > 0.f) h = std::min(h, child.maxSizeY);
        const float x = child.anchorX * flexW + child.posX - child.anchorX * w;
        const float y = child.anchorY * flexH + child.posY - child.anchorY * h;
        ImGui::SetCursorScreenPos(ImVec2(origin.x + x, origin.y + y));
        const float oldX = child.sizeX;
        const float oldY = child.sizeY;
        child.sizeX = w;
        child.sizeY = h;
        if (w > 0.f) ImGui::PushItemWidth(w);
        walkNode(host, tree, c);
        child.sizeX = oldX;
        child.sizeY = oldY;
        if (w > 0.f) ImGui::PopItemWidth();
    }
    ImGui::SetCursorScreenPos(flowEnd);
}

void walkNode(UIHost *host, UIHost::Tree *tree, int index) {
    if (index < 0 || index >= int(tree->nodes.size())) return;
    UINode &n = tree->nodes[size_t(index)];
    if (!n.visible) return;

    const bool scopedTheme = n.themePreset != ThemePreset::Inherit;
    ImGuiStyle previousStyle;
    float previousFontScale = 1.f;
    ImGuiConfigFlags previousConfigFlags = ImGuiConfigFlags_None;
    if (scopedTheme) {
        previousStyle = ImGui::GetStyle();
        previousFontScale = ImGui::GetIO().FontGlobalScale;
        previousConfigFlags = ImGui::GetIO().ConfigFlags;
        applyThemeToImGui(n.themePreset == ThemePreset::Dark ? Theme::dark() : Theme::light());
    }

    const bool interactive = isInteractive(n.type);
    const bool disableItem = interactive && !n.enabled;
    const bool disableNavigation = interactive && n.focusMode != FocusMode::All;
    if (disableItem) {
        ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.6f);
    }
    if (disableNavigation) ImGui::PushItemFlag(ImGuiItemFlags_NoNav, true);
    if (interactive && n.focusRequested && n.enabled && n.focusMode != FocusMode::None)
        ImGui::SetKeyboardFocusHere();

    switch (n.type) {
    case NodeType::Window: {
        const std::string visibleTitle = n.text.empty() ? "Window" : n.text;
        std::string title = visibleTitle;
        // Keep the host name in ImGui's stable ID without exposing internal
        // paths such as "inventory/Inspector" in the visible title bar.
        if (host && !host->meta()->name.empty())
            title += "###" + host->meta()->name + "/" + visibleTitle;
        const bool modal = host && host->meta()->modal;
        ImGuiWindowFlags flags = 0;
        if (hasDirectChildType(*tree, n, NodeType::MenuBar)) flags |= ImGuiWindowFlags_MenuBar;
        const bool desktopChrome = hasDirectChildType(*tree, n, NodeType::SplitPane) &&
                                   (hasDirectChildType(*tree, n, NodeType::Toolbar) ||
                                    hasDirectChildType(*tree, n, NodeType::StatusBar));
        if (desktopChrome)
            flags |= ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
        float winW = 0.f;
        float winH = 0.f;
        if (host && host->meta()->percentW > 0.f)
            winW = host->meta()->percentW * ImGui::GetIO().DisplaySize.x;
        else if (host && host->meta()->hasSize)
            winW = host->meta()->sizeX;
        if (host && host->meta()->percentH > 0.f)
            winH = host->meta()->percentH * ImGui::GetIO().DisplaySize.y;
        else if (host && host->meta()->hasSize)
            winH = host->meta()->sizeY;
        if (host && host->meta()->hasPos) {
            const ImVec2 display = ImGui::GetIO().DisplaySize;
            // SetNextWindowPos applies the pivot itself. Passing an already
            // pivot-adjusted top-left position shifts the window by its pivot
            // a second time (a centered window moves half its width left).
            const float x = host->meta()->anchorX * display.x + host->meta()->posX;
            const float y = host->meta()->anchorY * display.y + host->meta()->posY;
            ImGui::SetNextWindowPos(ImVec2(x, y),
                                    host->meta()->lockPos ? ImGuiCond_Always
                                                          : ImGuiCond_FirstUseEver,
                                    ImVec2(host->meta()->pivotX, host->meta()->pivotY));
            if (host->meta()->lockPos) flags |= ImGuiWindowFlags_NoMove;
            if (winW <= 0.f && winH <= 0.f) flags |= ImGuiWindowFlags_AlwaysAutoResize;
        }
        if (winW > 0.f || winH > 0.f) {
            ImGui::SetNextWindowSize(
                ImVec2(winW > 0.f ? winW : -1.f, winH > 0.f ? winH : -1.f),
                host && host->meta()->lockSize ? ImGuiCond_Always : ImGuiCond_FirstUseEver);
        } else if (n.measuredW > 0.f || n.measuredH > 0.f) {
            // Real measured size → stable auto-resize instead of ImGui's guess.
            ImGui::SetNextWindowContentSize(ImVec2(n.measuredW, n.measuredH));
        }
        if (host && host->meta()->overlay) {
            flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoCollapse;
            if (!host->meta()->hasSize && host->meta()->percentW <= 0.f &&
                host->meta()->percentH <= 0.f)
                flags |= ImGuiWindowFlags_AlwaysAutoResize;
            ImGui::SetNextWindowBgAlpha(host->meta()->overlayBgAlpha);
        }
        const bool flushOverlay = host && host->meta()->overlay && host->meta()->overlayFlush;
        if (flushOverlay) ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.f, 0.f));
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
        if (flushOverlay) ImGui::PopStyleVar();
        break;
    }
    case NodeType::Text:
        if (n.wrapWidth > 0.f) {
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + n.wrapWidth);
            ImGui::TextUnformatted(n.text.c_str());
            ImGui::PopTextWrapPos();
        } else {
            ImGui::TextUnformatted(n.text.c_str());
        }
        break;
    case NodeType::Button: {
        ImWchar iconCodepoint = 0;
        const bool iconOnly = privateUseGlyph(n.text, &iconCodepoint);
        const std::string label =
            iconOnly ? "##iconButton" + (n.id.empty() ? std::string("button") : n.id)
                     : nodeLabel(n, "Button");
        const bool sized = n.sizeX > 0.f || n.sizeY > 0.f;
        const float defaultSide = ImGui::GetFrameHeight();
        const ImVec2 buttonSize(iconOnly ? (n.sizeX > 0.f ? n.sizeX : defaultSide)
                                          : (n.sizeX > 0.f ? n.sizeX : 0.f),
                                iconOnly ? (n.sizeY > 0.f ? n.sizeY : defaultSide)
                                          : (n.sizeY > 0.f ? n.sizeY : 0.f));
        const bool clicked = (sized || iconOnly) ? ImGui::Button(label.c_str(), buttonSize)
                                                 : ImGui::Button(label.c_str());
        if (iconOnly) {
            const ImVec2 rectMin = ImGui::GetItemRectMin();
            const ImVec2 rectMax = ImGui::GetItemRectMax();
            ImFont *font = ImGui::GetFont();
            const ImFontGlyph *glyph = font ? font->FindGlyph(iconCodepoint) : nullptr;
            if (glyph) {
                const float fontSize = ImGui::GetFontSize();
                const float fontScale = fontSize / font->FontSize;
                const float glyphW = (glyph->X1 - glyph->X0) * fontScale;
                const float glyphH = (glyph->Y1 - glyph->Y0) * fontScale;
                const ImVec2 glyphOrigin(
                    (rectMin.x + rectMax.x - glyphW) * 0.5f - glyph->X0 * fontScale,
                    (rectMin.y + rectMax.y - glyphH) * 0.5f - glyph->Y0 * fontScale);
                ImGui::GetWindowDrawList()->AddText(font, fontSize, glyphOrigin,
                                                    ImGui::GetColorU32(ImGuiCol_Text),
                                                    n.text.c_str());
            }
        }
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
    case NodeType::Image: {
        const float w = n.sizeX > 0.f ? n.sizeX : 32.f;
        const float h = n.sizeY > 0.f ? n.sizeY : 32.f;
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const ImVec2 size(w, h);
        const ImU32 tint =
            ImGui::ColorConvertFloat4ToU32(ImVec4(n.tintR, n.tintG, n.tintB, n.tintA));
        if (n.textureId != 0 && g_backend) {
            int tw = 0, th = 0;
            g_backend->textureSize(n.textureId, &tw, &th);
            if (g_backend->usesQueuedTextureDraws()) {
                queueNinePatch(n.textureId, pos, size, ImVec2(n.uv0x, n.uv0y),
                               ImVec2(n.uv1x, n.uv1y), n.borderL, n.borderT, n.borderR,
                               n.borderB, tw, th, n.tintR, n.tintG, n.tintB, n.tintA);
            } else {
                void *handle = g_backend->textureHandle(n.textureId);
                if (handle) {
                    drawNinePatch(handle, pos, size, ImVec2(n.uv0x, n.uv0y),
                                  ImVec2(n.uv1x, n.uv1y), n.borderL, n.borderT,
                                  n.borderR, n.borderB, tw, th, tint);
                } else {
                    ImGui::GetWindowDrawList()->AddRectFilled(
                        pos, ImVec2(pos.x + w, pos.y + h), tint, n.cornerRadius);
                }
            }
        } else {
            ImGui::GetWindowDrawList()->AddRectFilled(pos, ImVec2(pos.x + w, pos.y + h), tint,
                                                      n.cornerRadius);
        }
        ImGui::Dummy(size);
        break;
    }
    case NodeType::ImageButton: {
        const float w = n.sizeX > 0.f ? n.sizeX : 32.f;
        const float h = n.sizeY > 0.f ? n.sizeY : 32.f;
        const ImVec2 size(w, h);
        const std::string sid = n.id.empty() ? "imgbtn" : n.id;
        ImGui::PushID(sid.c_str());
        bool clicked = false;
        bool itemCreated = false;
        if (n.textureId != 0 && g_backend) {
            if (g_backend->usesQueuedTextureDraws()) {
                clicked = ImGui::InvisibleButton(sid.c_str(), size);
                itemCreated = true;
                const ImVec2 pmin = ImGui::GetItemRectMin();
                const float hover = ImGui::IsItemHovered() ? 1.12f : 1.f;
                g_backend->queueTextureDraw(n.textureId, pmin.x, pmin.y, size.x, size.y,
                                            n.uv0x, n.uv0y, n.uv1x, n.uv1y,
                                            std::min(1.f, n.tintR * hover),
                                            std::min(1.f, n.tintG * hover),
                                            std::min(1.f, n.tintB * hover), n.tintA, false);
            } else {
                void *handle = g_backend->textureHandle(n.textureId);
                if (handle) {
                    clicked = ImGui::ImageButton(
                        static_cast<ImTextureID>(handle), size, ImVec2(n.uv0x, n.uv0y),
                        ImVec2(n.uv1x, n.uv1y), -1, ImVec4(0.f, 0.f, 0.f, 0.f),
                        ImVec4(n.tintR, n.tintG, n.tintB, n.tintA));
                    itemCreated = true;
                }
            }
        }
        if (!itemCreated) {
            clicked = ImGui::InvisibleButton(sid.c_str(), size);
            const ImVec2 pmin = ImGui::GetItemRectMin();
            const ImVec2 pmax = ImGui::GetItemRectMax();
            ImU32 col =
                ImGui::ColorConvertFloat4ToU32(ImVec4(n.tintR, n.tintG, n.tintB, n.tintA));
            if (ImGui::IsItemHovered()) col = ImGui::GetColorU32(ImGuiCol_ButtonHovered);
            ImGui::GetWindowDrawList()->AddRectFilled(pmin, pmax, col, n.cornerRadius);
        }
        ImGui::PopID();
        if (clicked) pushPending(host, n, "click", n.handlerClick);
        break;
    }
    case NodeType::Combo: {
        // Options are newline-separated in valueText; selected index in value.
        std::vector<std::string> storage;
        size_t start = 0;
        while (start <= n.valueText.size()) {
            const size_t end = n.valueText.find('\n', start);
            storage.push_back(n.valueText.substr(
                start, end == std::string::npos ? std::string::npos : end - start));
            if (end == std::string::npos) break;
            start = end + 1;
        }
        std::vector<const char *> items;
        items.reserve(storage.size());
        for (const auto &s : storage) items.push_back(s.c_str());
        int idx = int(n.value);
        const char *label = n.text.empty() ? "Combo" : n.text.c_str();
        if (ImGui::Combo(label, &idx, items.data(), int(items.size()))) {
            n.value = float(idx);
            pushPending(host, n, "value", n.handlerValue, false, float(idx));
        }
        break;
    }
    case NodeType::InputText: {
        char buf[1024];
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
    case NodeType::SearchField: {
        char buf[1024];
        std::memset(buf, 0, sizeof(buf));
        if (!n.valueText.empty()) std::strncpy(buf, n.valueText.c_str(), sizeof(buf) - 1);
        const std::string label = "##search" + (n.id.empty() ? std::string() : "###" + n.id);
        const ImGuiStyle &style = ImGui::GetStyle();
        const std::string hint = n.text.empty() ? "Search" : n.text;
        const char *glyph = iconGlyph(Icon::Search);
        const ImVec2 iconSize = ImGui::CalcTextSize(glyph);
        const float gap = globalTheme().layout.searchIconGap * themeUiScale();
        const float width = n.sizeX > 0.f ? n.sizeX : ImGui::GetContentRegionAvail().x;
        n.measuredW = std::max(1.f, width);
        ImGui::PushItemWidth(std::max(1.f, width));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                            ImVec2(style.FramePadding.x + iconSize.x + gap,
                                   style.FramePadding.y));
        if (ImGui::InputTextWithHint(label.c_str(), hint.c_str(), buf, sizeof(buf))) {
            n.valueText = buf;
            pushPending(host, n, "text", n.handlerText, false, 0.f, n.valueText);
        }
        ImGui::PopStyleVar();
        ImGui::PopItemWidth();
        const ImVec2 rectMin = ImGui::GetItemRectMin();
        const ImVec2 rectMax = ImGui::GetItemRectMax();
        ImGui::GetWindowDrawList()->AddText(
            ImVec2(rectMin.x + style.FramePadding.x,
                   rectMin.y + (rectMax.y - rectMin.y - iconSize.y) * 0.5f),
            ImGui::GetColorU32(ImGuiCol_TextDisabled), glyph);
        n.measuredH = rectMax.y - rectMin.y;
        break;
    }
    case NodeType::Switch: {
        const float height = ImGui::GetFrameHeight();
        const float trackH = std::max(16.f, height * 0.68f);
        const float trackW = height * 1.7f;
        const float radius = trackH * 0.5f;
        const std::string id = "##switch" + (n.id.empty() ? std::string() : "###" + n.id);
        ImGui::BeginGroup();
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        if (ImGui::InvisibleButton(id.c_str(), ImVec2(trackW, height))) {
            n.checked = !n.checked;
            pushPending(host, n, "toggle", n.handlerToggle, n.checked);
        }
        const bool hovered = ImGui::IsItemHovered();
        const ImU32 bg = ImGui::GetColorU32(
            n.checked ? ImGuiCol_CheckMark
                      : (hovered ? ImGuiCol_FrameBgHovered : ImGuiCol_FrameBg));
        const float y = pos.y + (height - trackH) * 0.5f;
        ImDrawList *draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(ImVec2(pos.x, y), ImVec2(pos.x + trackW, y + trackH), bg, radius);
        const float knobR = radius - 2.f;
        const float knobX = n.checked ? pos.x + trackW - radius : pos.x + radius;
        draw->AddCircleFilled(ImVec2(knobX, y + radius), knobR,
                              ImGui::GetColorU32(ImGuiCol_Text));
        if (!n.text.empty()) {
            ImGui::SameLine(0.f, ImGui::GetStyle().ItemInnerSpacing.x);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(n.text.c_str());
        }
        ImGui::EndGroup();
        break;
    }
    case NodeType::Badge: {
        const ImVec2 textSize = ImGui::CalcTextSize(n.text.c_str());
        const ImVec2 padding(ImGui::GetStyle().FramePadding.x, 3.f);
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const ImVec2 size(textSize.x + padding.x * 2.f, textSize.y + padding.y * 2.f);
        ImDrawList *draw = ImGui::GetWindowDrawList();
        const ImU32 bg = ImGui::ColorConvertFloat4ToU32(
            ImVec4(n.tintR, n.tintG, n.tintB, n.tintA));
        const ImU32 border = ImGui::ColorConvertFloat4ToU32(
            ImVec4(n.tintR, n.tintG, n.tintB, std::min(1.f, n.tintA + 0.35f)));
        draw->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), bg, size.y * 0.5f);
        draw->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y), border, size.y * 0.5f);
        draw->AddText(ImVec2(pos.x + padding.x, pos.y + padding.y),
                      ImGui::GetColorU32(ImGuiCol_Text), n.text.c_str());
        ImGui::Dummy(size);
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
    case NodeType::Card: {
        const ThemeLayout &layout = globalTheme().layout;
        const ImVec2 padding(layout.cardPaddingX * themeUiScale(),
                             layout.cardPaddingY * themeUiScale());
        const float height = n.sizeY > 0.f
                                 ? n.sizeY
                                 : std::max(ImGui::GetFrameHeight(),
                                            n.measuredH + padding.y * 2.f);
        const ImVec2 size(n.sizeX > 0.f ? n.sizeX : 0.f, height);
        const std::string id = n.id.empty() ? "card" : n.id;
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, padding);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
        if (ImGui::BeginChild(id.c_str(), size, true, ImGuiWindowFlags_AlwaysUseWindowPadding)) {
            if (n.firstChild >= 0) walkSiblings(host, tree, n.firstChild);
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
        break;
    }
    case NodeType::NinePatchPanel: {
        const float width = n.sizeX > 0.f ? n.sizeX : ImGui::GetContentRegionAvail().x;
        const float height = n.sizeY > 0.f
                                 ? n.sizeY
                                 : std::max(n.minSizeY,
                                            n.measuredH > 0.f ? n.measuredH
                                                              : ImGui::GetFrameHeight());
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const ImVec2 size(std::max(n.minSizeX, width), height);
        if (n.textureId != 0 && g_backend) {
            int tw = 0, th = 0;
            g_backend->textureSize(n.textureId, &tw, &th);
            if (g_backend->usesQueuedTextureDraws()) {
                queueNinePatch(n.textureId, pos, size, ImVec2(n.uv0x, n.uv0y),
                               ImVec2(n.uv1x, n.uv1y), n.borderL, n.borderT, n.borderR,
                               n.borderB, tw, th, n.tintR, n.tintG, n.tintB, n.tintA);
            } else if (void *handle = g_backend->textureHandle(n.textureId)) {
                const ImU32 tint = ImGui::ColorConvertFloat4ToU32(
                    ImVec4(n.tintR, n.tintG, n.tintB, n.tintA));
                drawNinePatch(handle, pos, size, ImVec2(n.uv0x, n.uv0y),
                              ImVec2(n.uv1x, n.uv1y), n.borderL, n.borderT, n.borderR,
                              n.borderB, tw, th, tint);
            }
        }
        const std::string id = n.id.empty() ? "ninePatchPanel" : n.id;
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.f, 0.f));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.f, 0.f, 0.f, 0.f));
        if (ImGui::BeginChild(id.c_str(), size, false,
                              ImGuiWindowFlags_AlwaysUseWindowPadding)) {
            ImGui::SetCursorPos(ImVec2(n.paddingL, n.paddingT));
            const ImVec2 contentSize(std::max(1.f, size.x - n.paddingL - n.paddingR),
                                     std::max(1.f, size.y - n.paddingT - n.paddingB));
            const std::string contentId = id + "##content";
            if (ImGui::BeginChild(contentId.c_str(), contentSize, false)) {
                if (n.firstChild >= 0) walkSiblings(host, tree, n.firstChild);
            }
            ImGui::EndChild();
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
        break;
    }
    case NodeType::SectionHeader: {
        const float sectionGap = globalTheme().layout.sectionSpacingY * themeUiScale();
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const float height = ImGui::GetFrameHeight();
        const float totalHeight = sectionGap + height;
        const float width = n.sizeX > 0.f ? n.sizeX : ImGui::GetContentRegionAvail().x;
        const float headerY = pos.y + sectionGap;
        ImDrawList *draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(ImVec2(pos.x, headerY + 4.f),
                            ImVec2(pos.x + 3.f, headerY + height - 4.f),
                            ImGui::GetColorU32(ImGuiCol_CheckMark), 1.f);
        draw->AddText(ImVec2(pos.x + 10.f,
                             headerY + (height - ImGui::GetFontSize()) * 0.5f),
                      ImGui::GetColorU32(ImGuiCol_Text), n.text.c_str());
        const float separatorY = pos.y + totalHeight - 1.f;
        draw->AddLine(ImVec2(pos.x, separatorY), ImVec2(pos.x + width, separatorY),
                      ImGui::GetColorU32(ImGuiCol_Separator));
        ImGui::Dummy(ImVec2(width, totalHeight));
        break;
    }
    case NodeType::MenuBar:
        if (ImGui::BeginMenuBar()) {
            if (n.firstChild >= 0) walkSiblings(host, tree, n.firstChild);
            ImGui::EndMenuBar();
        }
        break;
    case NodeType::Menu: {
        const std::string label = nodeLabel(n, "Menu");
        if (ImGui::BeginMenu(label.c_str())) {
            if (n.firstChild >= 0) walkSiblings(host, tree, n.firstChild);
            ImGui::EndMenu();
        }
        break;
    }
    case NodeType::MenuItem: {
        const std::string label = nodeLabel(n, "Command");
        const char *shortcut = n.valueText.empty() ? nullptr : n.valueText.c_str();
        if (ImGui::MenuItem(label.c_str(), shortcut, n.checked, true))
            pushPending(host, n, "click", n.handlerClick);
        break;
    }
    case NodeType::Toolbar:
    case NodeType::StatusBar: {
        const bool status = n.type == NodeType::StatusBar;
        const ThemeLayout &layout = globalTheme().layout;
        const float scale = themeUiScale();
        const float defaultHeight =
            (status ? layout.statusBarHeight : layout.toolbarHeight) * scale;
        const float height = n.sizeY > 0.f ? n.sizeY : defaultHeight;
        const ImVec2 size(n.sizeX > 0.f ? n.sizeX : 0.f, height);
        const std::string id = n.id.empty() ? (status ? "statusbar" : "toolbar") : n.id;
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                            ImVec2(layout.barPaddingX * scale, layout.barPaddingY * scale));
        ImGui::PushStyleColor(ImGuiCol_ChildBg,
                              ImGui::GetStyleColorVec4(status ? ImGuiCol_WindowBg
                                                             : ImGuiCol_MenuBarBg));
        if (ImGui::BeginChild(id.c_str(), size, false,
                              ImGuiWindowFlags_AlwaysUseWindowPadding |
                                  ImGuiWindowFlags_NoScrollbar)) {
            walkFlex(host, tree, n);
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
        break;
    }
    case NodeType::Toolbox: {
        const ThemeLayout &layout = globalTheme().layout;
        const float scale = themeUiScale();
        const float cell = n.itemHeight > 0.f ? n.itemHeight : layout.toolboxCellSize * scale;
        const ImVec2 padding(layout.panelPaddingX * scale, layout.panelPaddingY * scale);
        const float outerWidth = n.sizeX > 0.f ? n.sizeX : ImGui::GetContentRegionAvail().x;
        const float contentWidth = std::max(1.f, outerWidth - padding.x * 2.f);
        const float spacing = ImGui::GetStyle().ItemSpacing.x;
        const int capacity = std::max(1, int((contentWidth + spacing) / (cell + spacing)));
        const int requested = int(n.value) > 0 ? int(n.value) : capacity;
        const int columns = std::max(1, std::min(requested, capacity));
        int count = 0;
        for (int childIndex = n.firstChild; childIndex >= 0;
             childIndex = tree->nodes[size_t(childIndex)].nextSibling) {
            if (tree->nodes[size_t(childIndex)].visible) ++count;
        }
        const int rows = (count + columns - 1) / columns;
        const float autoHeight = std::max(
            cell, float(rows) * cell + float(std::max(0, rows - 1)) *
                                           ImGui::GetStyle().ItemSpacing.y + padding.y * 2.f);
        const ImVec2 size(n.sizeX > 0.f ? n.sizeX : 0.f, n.sizeY > 0.f ? n.sizeY : autoHeight);
        const std::string id = n.id.empty() ? "toolbox" : n.id;
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, padding);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyleColorVec4(ImGuiCol_ChildBg));
        if (ImGui::BeginChild(id.c_str(), size, false, ImGuiWindowFlags_AlwaysUseWindowPadding)) {
            int visibleIndex = 0;
            for (int childIndex = n.firstChild; childIndex >= 0;
                 childIndex = tree->nodes[size_t(childIndex)].nextSibling) {
                UINode &child = tree->nodes[size_t(childIndex)];
                if (!child.visible) continue;
                const float oldX = child.sizeX;
                const float oldY = child.sizeY;
                child.sizeX = cell;
                child.sizeY = cell;
                walkNode(host, tree, childIndex);
                child.sizeX = oldX;
                child.sizeY = oldY;
                ++visibleIndex;
                if (visibleIndex % columns != 0) ImGui::SameLine();
            }
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
        break;
    }
    case NodeType::Sidebar: {
        const ThemeLayout &layout = globalTheme().layout;
        const float scale = themeUiScale();
        const ImVec2 size(n.sizeX > 0.f ? n.sizeX : layout.sidebarWidth * scale,
                          n.sizeY > 0.f ? n.sizeY : 0.f);
        const std::string id = n.id.empty() ? "sidebar" : n.id;
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                            ImVec2(layout.panelPaddingX * scale, layout.panelPaddingY * scale));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyleColorVec4(ImGuiCol_ChildBg));
        if (ImGui::BeginChild(id.c_str(), size, true, ImGuiWindowFlags_AlwaysUseWindowPadding)) {
            if (n.firstChild >= 0) walkSiblings(host, tree, n.firstChild);
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
        break;
    }
    case NodeType::SplitPane: {
        const bool row = n.flexDirection == FlexDirection::Row;
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        const ImVec2 size(n.sizeX > 0.f ? n.sizeX : avail.x, n.sizeY > 0.f ? n.sizeY : avail.y);
        n.measuredW = size.x;
        n.measuredH = size.y;
        const ThemeLayout &layout = globalTheme().layout;
        const float splitter = layout.splitterSize * themeUiScale();
        const float mainSize = std::max(1.f, (row ? size.x : size.y) - splitter);
        const float minPane = std::min(layout.minPaneSize * themeUiScale(), mainSize * 0.5f);
        const float ratioLo = std::max(n.minValue, minPane / mainSize);
        const float ratioHi = std::min(n.maxValue, 1.f - minPane / mainSize);
        const std::string id = n.id.empty() ? "splitpane" : n.id;
        ImGui::PushID(id.c_str());
        const ImGuiID ratioStateId = ImGui::GetID("ratio");
        const float storedRatio = ImGui::GetStateStorage()->GetFloat(ratioStateId, n.value);
        const float ratio = std::max(ratioLo, std::min(ratioHi, storedRatio));
        ImGui::GetStateStorage()->SetFloat(ratioStateId, ratio);
        n.value = ratio;
        const float firstMain = std::floor(mainSize * ratio);
        const float secondMain = std::max(1.f, mainSize - firstMain);
        const int first = n.firstChild;
        const int second = first >= 0 ? tree->nodes[size_t(first)].nextSibling : -1;
        ImGui::BeginGroup();
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.f, 0.f));
        if (first >= 0) {
            const ImVec2 paneSize(row ? firstMain : size.x, row ? size.y : firstMain);
            if (ImGui::BeginChild("first", paneSize, false,
                                  ImGuiWindowFlags_NoScrollbar |
                                      ImGuiWindowFlags_NoScrollWithMouse))
                walkPaneChild(host, tree, first);
            ImGui::EndChild();
        }
        if (row) ImGui::SameLine(0.f, 0.f);
        const ImVec2 handleSize(row ? splitter : size.x, row ? size.y : splitter);
        ImGui::InvisibleButton("splitter", handleSize);
        const bool hovered = ImGui::IsItemHovered();
        const bool active = ImGui::IsItemActive();
        if (hovered || active)
            ImGui::SetMouseCursor(row ? ImGuiMouseCursor_ResizeEW : ImGuiMouseCursor_ResizeNS);
        ImGui::GetWindowDrawList()->AddRectFilled(
            ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
            ImGui::GetColorU32(active ? ImGuiCol_CheckMark
                                     : (hovered ? ImGuiCol_SeparatorHovered
                                                : ImGuiCol_Separator)));
        if (active) {
            const float delta = row ? ImGui::GetIO().MouseDelta.x : ImGui::GetIO().MouseDelta.y;
            const float next = std::max(ratioLo, std::min(ratioHi, ratio + delta / mainSize));
            if (next != ratio) {
                n.value = next;
                ImGui::GetStateStorage()->SetFloat(ratioStateId, next);
                pushPending(host, n, "value", n.handlerValue, false, next);
            }
        }
        if (row) ImGui::SameLine(0.f, 0.f);
        if (second >= 0) {
            const ImVec2 paneSize(row ? secondMain : size.x, row ? size.y : secondMain);
            if (ImGui::BeginChild("second", paneSize, false,
                                  ImGuiWindowFlags_NoScrollbar |
                                      ImGuiWindowFlags_NoScrollWithMouse))
                walkPaneChild(host, tree, second);
            ImGui::EndChild();
        }
        ImGui::PopStyleVar();
        ImGui::EndGroup();
        ImGui::PopID();
        break;
    }
    case NodeType::ScrollList: {
        // Virtualized list: only the rows intersecting the viewport are drawn.
        std::vector<int> kids;
        for (int c = n.firstChild; c >= 0; c = tree->nodes[size_t(c)].nextSibling) {
            if (c >= int(tree->nodes.size())) break;
            if (tree->nodes[size_t(c)].visible) kids.push_back(c);
        }
        const int total = int(kids.size());
        const char *cid = n.id.empty() ? "scrolllist" : n.id.c_str();
        const ImVec2 size(n.sizeX > 0.f ? n.sizeX : 0.f,
                          n.sizeY > 0.f ? n.sizeY : 120.f);
        ImGui::BeginChild(cid, size, true);
        const float itemH = n.itemHeight > 0.f ? n.itemHeight : ImGui::GetFrameHeight();
        const float contentW = ImGui::GetContentRegionAvail().x;
        const float scrollY = ImGui::GetScrollY();
        const float viewH = ImGui::GetWindowHeight();
        if (total > 0 && itemH > 0.f) {
            int first = int(scrollY / itemH);
            if (first < 0) first = 0;
            if (first > total) first = total;
            int last = int(std::ceil((scrollY + viewH) / itemH));
            if (last < first) last = first;
            if (last > total) last = total;
            for (int i = first; i < last; ++i) {
                UINode &child = tree->nodes[size_t(kids[size_t(i)])];
                ImGui::SetCursorPos(ImVec2(0.f, float(i) * itemH));
                const float oldX = child.sizeX;
                const float oldY = child.sizeY;
                if (contentW > 0.f) child.sizeX = contentW;
                if (contentW > 0.f) ImGui::PushItemWidth(contentW);
                walkNode(host, tree, kids[size_t(i)]);
                child.sizeX = oldX;
                child.sizeY = oldY;
                if (contentW > 0.f) ImGui::PopItemWidth();
                // Force the row to occupy exactly itemH so rows stay uniform.
                ImGui::SetCursorPosY(float(i) * itemH + itemH);
            }
        }
        // Establish the full scrollable extent (scrollbar reflects total rows).
        ImGui::SetCursorPosY(float(total) * itemH);
        ImGui::EndChild();
        break;
    }
    case NodeType::Viewport: {
        const float w = n.sizeX > 0.f ? n.sizeX : ImGui::GetContentRegionAvail().x;
        const float h = n.sizeY > 0.f ? n.sizeY : ImGui::GetContentRegionAvail().y;
        const ImVec2 size(std::max(1.f, w), std::max(1.f, h));
        const std::string key = viewportKey(host, n);
        auto viewport = UISystem::ensureViewport(key, int(size.x), int(size.y));

        const ImVec2 rectMin = ImGui::GetCursorScreenPos();
        const std::string label = "##vp" + (n.id.empty() ? std::string("viewport") : n.id);
        ImGui::InvisibleButton(label.c_str(), size);
        const bool hovered = ImGui::IsItemHovered();
        const bool active = ImGui::IsItemActive();
        const ImVec2 mouse = ImGui::GetMousePos();
        if (viewport.ok()) {
            ViewportState &vs = viewport.value().get();
            vs.hovered = hovered;
            vs.active = active;
            vs.mouseX = mouse.x - rectMin.x;
            vs.mouseY = mouse.y - rectMin.y;
            if (active) {
                vs.dragDX = ImGui::GetIO().MouseDelta.x;
                vs.dragDY = ImGui::GetIO().MouseDelta.y;
            } else {
                vs.dragDX = 0.f;
                vs.dragDY = 0.f;
            }
            vs.wheel = hovered ? ImGui::GetIO().MouseWheel : 0.f;

            if (vs.textureId && g_backend && g_backend->usesQueuedTextureDraws()) {
                g_backend->queueTextureDraw(vs.textureId, rectMin.x, rectMin.y, size.x, size.y,
                                        0.f, 0.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, true);
            } else if (vs.textureId && g_backend) {
                void *handle = g_backend->textureHandle(vs.textureId);
                if (handle) {
                    ImGui::SetCursorScreenPos(rectMin);
                    ImGui::Image(static_cast<ImTextureID>(handle), size);
                } else {
                    ImGui::GetWindowDrawList()->AddRectFilled(
                        rectMin, ImVec2(rectMin.x + size.x, rectMin.y + size.y),
                        IM_COL32(10, 12, 16, 255));
                }
            } else {
                ImGui::GetWindowDrawList()->AddRectFilled(
                    rectMin, ImVec2(rectMin.x + size.x, rectMin.y + size.y),
                    IM_COL32(10, 12, 16, 255));
            }
        } else {
            ImGui::GetWindowDrawList()->AddRectFilled(
                rectMin, ImVec2(rectMin.x + size.x, rectMin.y + size.y), IM_COL32(10, 12, 16, 255));
        }
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

    if (interactive) {
        n.focused = ImGui::IsItemFocused();
        routeExplicitFocusInput(host, n);
        n.focusRequested = false;
    }
    if (disableNavigation) ImGui::PopItemFlag();
    if (disableItem) {
        ImGui::PopStyleVar();
        ImGui::PopItemFlag();
    }

    if (!n.tooltip.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_None))
        ImGui::SetTooltip("%s", n.tooltip.c_str());

    if (scopedTheme) {
        ImGui::GetStyle() = previousStyle;
        ImGui::GetIO().FontGlobalScale = previousFontScale;
        ImGui::GetIO().ConfigFlags = previousConfigFlags;
    }
}

void walk(UIHost *host, UIHost::Tree *tree, int index) { walkSiblings(host, tree, index); }

}  // namespace

std::vector<UIEvent> &UISystem::pendingEvents() { return g_pending; }
std::vector<UIClick> &UISystem::clickQueue() { return g_clicks; }
std::vector<UIChange> &UISystem::changeQueue() { return g_changes; }

void UISystem::setBackend(UIBackend& backend) { g_backend = &backend; }
void UISystem::clearBackend() noexcept { g_backend = nullptr; }
std::optional<std::reference_wrapper<UIBackend>> UISystem::backend() {
    if (!g_backend) return std::nullopt;
    return std::ref(*g_backend);
}
const UIStats &UISystem::stats() { return g_stats; }

eve::Result<std::reference_wrapper<ViewportState>> UISystem::ensureViewport(
    const std::string &key, int w, int h) {
    using Return = eve::Result<std::reference_wrapper<ViewportState>>;
    if (key.empty() || w <= 0 || h <= 0) {
        return Return::failure(eve::Status::failure(
            eve::StatusCode::Rejected,
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                   "viewport key and dimensions must be valid")));
    }
    auto *gfx = eve::ModuleManager::getInstance<eve::graphics::Graphics>("Graphics");
    if (!gfx) {
        return Return::failure(eve::Status::failure(
            eve::StatusCode::Unsupported,
            eve::Diagnostic::error(eve::DiagnosticCode::Unsupported,
                                   "viewport requires the Graphics module")));
    }
    ViewportState &vs = g_viewports[key];
    vs.key = key;
    if (!vs.canvas || vs.width != w || vs.height != h) {
        graphics::Canvas *canvas = gfx->newCanvas(w, h);
        if (!canvas) {
            return Return::failure(eve::Status::failure(
                eve::StatusCode::Failed,
                eve::Diagnostic::error(eve::DiagnosticCode::Failed,
                                       "Graphics could not create the viewport canvas")));
        }
        if (vs.textureId && g_backend) g_backend->unregisterTexture(vs.textureId);
        vs.textureId = 0;
        vs.canvas = canvas;
        vs.width = w;
        vs.height = h;
        if (vs.canvas && g_backend)
            vs.textureId = g_backend->registerTexture(vs.canvas->getTexture());
    }
    return Return::success(std::ref(vs));
}

std::optional<std::reference_wrapper<ViewportState>> UISystem::viewportState(
    const std::string &hostName, const std::string &nodeId) {
    auto it = g_viewports.find(hostName + "/" + nodeId);
    if (it == g_viewports.end()) return std::nullopt;
    return std::ref(it->second);
}

UIHostHandle UISystem::findHost(const std::string &name) {
    if (name.empty()) return {};
    if (ecs::current()->getManager<UIHost>() == nullptr) return {};
    auto view = ecs::View<UIHost, UIHost::Meta, UIHost::Tree>();
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto [meta, tree] = *it;
        (void)tree;
        if (meta->name == name && UIHost::resolve(meta->entity)) return meta->entity;
    }
    return {};
}

UIHostHandle UISystem::findHostByOwner(uint32_t ownerId) {
    if (ownerId == 0) return {};
    if (ecs::current()->getManager<UIHost>() == nullptr) return {};
    auto view = ecs::View<UIHost, UIHost::Meta, UIHost::Tree>();
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto [meta, tree] = *it;
        (void)tree;
        if (meta->ownerId == ownerId && UIHost::resolve(meta->entity)) return meta->entity;
    }
    return {};
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
        auto host = UIHost::resolve(meta->entity);
        if (!host) continue;
        items.push_back(Item{&host->get(), meta, tree});
    }

    std::stable_sort(items.begin(), items.end(),
                     [](const Item &a, const Item &b) { return a.meta->layer < b.meta->layer; });

    g_stats.hostCount = int(items.size());
    g_stats.nodeCount = 0;
    g_stats.measureMs = 0.0;
    g_stats.walkMs = 0.0;
    for (auto &item : items) {
        item.tree->dirty = false;
        // Real measure pass: nested containers, text metrics, margins/min/max.
        const auto m0 = std::chrono::steady_clock::now();
        measureTree(*item.tree);
        const auto m1 = std::chrono::steady_clock::now();
        g_stats.nodeCount += int(item.tree->nodes.size());
        if (item.tree->root >= 0) walk(item.host, item.tree, item.tree->root);
        const auto m2 = std::chrono::steady_clock::now();
        g_stats.measureMs +=
            std::chrono::duration<double, std::milli>(m1 - m0).count();
        g_stats.walkMs += std::chrono::duration<double, std::milli>(m2 - m1).count();
    }
}

void UISystem::dispatchEvents() {
    auto events = std::move(g_pending);
    g_pending.clear();
    for (auto &ev : events) {
        auto resolvedHost = UIHost::resolve(ev.host);
        if (!resolvedHost) continue;
        UIHost *host = &resolvedHost->get();

        if (ev.kind == "click" && !ev.nodeId.empty()) {
            UIClick c;
            c.hostName = ev.hostName.empty() ? host->meta()->name : ev.hostName;
            c.nodeId = ev.nodeId;
            g_clicks.push_back(std::move(c));
        }

        if ((ev.kind == "toggle" || ev.kind == "value" || ev.kind == "text") && !ev.nodeId.empty()) {
            UIChange ch;
            ch.hostName = ev.hostName.empty() ? host->meta()->name : ev.hostName;
            ch.nodeId = ev.nodeId;
            ch.kind = ev.kind;
            g_changes.push_back(std::move(ch));
        }

        if (ev.kind == "click") {
            auto tree = host->tree();
            int index = ev.nodeIndex;
            if (index < 0 || index >= int(tree->nodes.size()) ||
                tree->nodes[size_t(index)].id != ev.nodeId) {
                auto target = host->findById(ev.nodeId);
                index = target && !tree->nodes.empty()
                            ? int(&target->get() - tree->nodes.data())
                            : -1;
            }

            bool first = true;
            while (index >= 0 && index < int(tree->nodes.size())) {
                const UINode &node = tree->nodes[size_t(index)];
                if (!node.visible || !node.enabled) break;
                const uint32_t handler = first ? ev.handlerIndex : node.handlerClick;
                if (node.mouseFilter != MouseFilter::Ignore && handler != 0) {
                    const size_t handlerSlot = size_t(handler - 1);
                    if (handlerSlot < tree->clickHandlers.size() &&
                        tree->clickHandlers[handlerSlot])
                        tree->clickHandlers[handlerSlot]();
                }
                if (node.mouseFilter == MouseFilter::Stop) break;
                index = node.parent;
                first = false;
            }
            continue;
        }

        if (ev.kind == "toggle" && ev.handlerIndex != 0) {
            auto t = host->tree();
            size_t idx = size_t(ev.handlerIndex - 1);
            if (idx < t->toggleHandlers.size() && t->toggleHandlers[idx])
                t->toggleHandlers[idx](ev.toggleValue);
            continue;
        }
        if (ev.kind == "value" && ev.handlerIndex != 0) {
            auto t = host->tree();
            size_t idx = size_t(ev.handlerIndex - 1);
            if (idx < t->valueHandlers.size() && t->valueHandlers[idx])
                t->valueHandlers[idx](ev.floatValue);
            continue;
        }
        if (ev.kind == "text" && ev.handlerIndex != 0) {
            auto t = host->tree();
            size_t idx = size_t(ev.handlerIndex - 1);
            if (idx < t->textHandlers.size() && t->textHandlers[idx])
                t->textHandlers[idx](ev.textValue);
            continue;
        }
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
