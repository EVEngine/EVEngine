#include "ui/Layout.h"

#include <imgui.h>

#include <algorithm>

namespace eve::ui {
namespace {

float clampV(float v, float lo, float hi) {
    if (lo > 0.f) v = std::max(v, lo);
    if (hi > 0.f) v = std::min(v, hi);
    return v;
}

float effectiveGrow(const FlexItemSpec &s) {
    float g = s.flexGrow;
    if (s.isSpacer && g <= 0.f) g = 1.f;
    return std::max(0.f, g);
}

}  // namespace

FlexResult flexArrange(bool row, float gap, float availMain, float availCross,
                       FlexAlign containerAlign, FlexJustify justify,
                       const std::vector<FlexItemSpec> &items) {
    FlexResult res;
    res.items.resize(items.size());
    if (availMain < 0.f) availMain = 0.f;
    if (availCross < 0.f) availCross = 0.f;

    const int n = int(items.size());
    std::vector<float> mainSize(size_t(n), 0.f);
    std::vector<float> crossSize(size_t(n), 0.f);
    std::vector<float> mainPos(size_t(n), 0.f);
    std::vector<float> crossPos(size_t(n), 0.f);

    float flowTotal = 0.f;
    int flowCount = 0;
    float growSum = 0.f;

    for (int i = 0; i < n; ++i) {
        const FlexItemSpec &s = items[size_t(i)];
        float m = s.explicitMain > 0.f
                      ? s.explicitMain
                      : (s.percentMain > 0.f ? s.percentMain * availMain : s.basisMain);
        m = clampV(m, s.minMain, s.maxMain);
        float c = s.explicitCross > 0.f
                      ? s.explicitCross
                      : (s.percentCross > 0.f ? s.percentCross * availCross : s.basisCross);
        c = clampV(c, s.minCross, s.maxCross);
        mainSize[size_t(i)] = m;
        crossSize[size_t(i)] = c;

        if (s.absolute) continue;
        growSum += effectiveGrow(s);
        flowTotal += m + s.marginBefore + s.marginAfter;
        ++flowCount;
    }

    const int gapCount = std::max(0, flowCount - 1);
    float total = flowTotal + float(gapCount) * gap;
    float freeSpace = availMain - total;
    if (freeSpace < 0.f) freeSpace = 0.f;

    float leading = 0.f;
    float between = gap;
    if (growSum > 0.f) {
        for (int i = 0; i < n; ++i) {
            const FlexItemSpec &s = items[size_t(i)];
            if (s.absolute) continue;
            const float g = effectiveGrow(s);
            if (g > 0.f) mainSize[size_t(i)] += freeSpace * (g / growSum);
        }
    } else if (freeSpace > 0.f && flowCount > 0) {
        switch (justify) {
        case FlexJustify::Center:
            leading = freeSpace * 0.5f;
            break;
        case FlexJustify::End:
            leading = freeSpace;
            break;
        case FlexJustify::SpaceBetween:
            if (gapCount > 0) between = gap + freeSpace / float(gapCount);
            break;
        case FlexJustify::SpaceAround:
            if (flowCount > 0) {
                const float edge = freeSpace / float(flowCount * 2);
                leading = edge;
                between = gap + edge * 2.f;
            }
            break;
        case FlexJustify::Start:
        default:
            break;
        }
    }

    float cur = leading;
    for (int i = 0; i < n; ++i) {
        const FlexItemSpec &s = items[size_t(i)];
        if (s.absolute) {
            mainPos[size_t(i)] =
                s.anchorMain * availMain + s.posMain - s.anchorMain * mainSize[size_t(i)];
            continue;
        }
        mainPos[size_t(i)] = cur + s.marginBefore;
        cur += s.marginBefore + mainSize[size_t(i)] + s.marginAfter + between;
    }
    const float contentMain = flowCount > 0 ? cur - between : 0.f;

    float contentCross = 0.f;
    for (int i = 0; i < n; ++i) {
        const FlexItemSpec &s = items[size_t(i)];
        if (s.absolute) {
            crossPos[size_t(i)] =
                s.anchorCross * availCross + s.posCross - s.anchorCross * crossSize[size_t(i)];
            contentCross = std::max(contentCross,
                                    crossPos[size_t(i)] + crossSize[size_t(i)] +
                                        s.marginCrossAfter);
            continue;
        }
        const float cb = s.marginCrossBefore;
        const float ca = s.marginCrossAfter;
        const bool stretch = s.alignSelf >= 0
                                 ? s.alignSelf == int(FlexAlign::Stretch)
                                 : containerAlign == FlexAlign::Stretch;
        if (stretch && s.explicitCross <= 0.f && s.percentCross <= 0.f) {
            float c = availCross - cb - ca;
            if (c < 0.f) c = 0.f;
            crossSize[size_t(i)] = c;
        }
        switch (containerAlign) {
        case FlexAlign::Center:
            crossPos[size_t(i)] = (availCross - crossSize[size_t(i)] - cb - ca) * 0.5f + cb;
            break;
        case FlexAlign::End:
            crossPos[size_t(i)] = availCross - crossSize[size_t(i)] - ca;
            break;
        case FlexAlign::Start:
        case FlexAlign::Stretch:
        default:
            crossPos[size_t(i)] = cb;
            break;
        }
        contentCross = std::max(contentCross,
                                crossPos[size_t(i)] + crossSize[size_t(i)] + ca);
    }

    for (int i = 0; i < n; ++i) {
        FlexRect &r = res.items[size_t(i)];
        if (row) {
            r.x = mainPos[size_t(i)];
            r.y = crossPos[size_t(i)];
            r.w = mainSize[size_t(i)];
            r.h = crossSize[size_t(i)];
        } else {
            r.x = crossPos[size_t(i)];
            r.y = mainPos[size_t(i)];
            r.w = crossSize[size_t(i)];
            r.h = mainSize[size_t(i)];
        }
    }

    res.contentW = row ? contentMain : contentCross;
    res.contentH = row ? contentCross : contentMain;
    return res;
}

void measureFlowChildren(UIHost::Tree &tree, int firstChild, float *outW, float *outH) {
    float rowW = 0.f;
    float rowH = 0.f;
    float maxW = 0.f;
    float totalH = 0.f;
    bool sameRow = false;
    const ImGuiStyle &style = ImGui::GetStyle();
    int index = firstChild;
    while (index >= 0 && index < int(tree.nodes.size())) {
        UINode &n = tree.nodes[size_t(index)];
        const int next = n.nextSibling;
        if (n.visible) {
            if (n.type == NodeType::SameLine) {
                sameRow = true;
            } else {
                measureNode(tree, index);
                const float w = n.measuredW + n.marginL + n.marginR;
                const float h = n.measuredH + n.marginT + n.marginB;
                if (sameRow) {
                    rowW += style.ItemSpacing.x + w;
                } else {
                    if (rowH > 0.f) totalH += style.ItemSpacing.y;
                    totalH += rowH;
                    rowW = w;
                    rowH = 0.f;
                }
                rowH = std::max(rowH, h);
                maxW = std::max(maxW, rowW);
                sameRow = false;
            }
        }
        index = next;
    }
    if (rowH > 0.f) totalH += rowH;
    if (outW) *outW = maxW;
    if (outH) *outH = totalH;
}

void measureNode(UIHost::Tree &tree, int index) {
    if (index < 0 || index >= int(tree.nodes.size())) return;
    UINode &n = tree.nodes[size_t(index)];
    n.measuredW = 0.f;
    n.measuredH = 0.f;
    const ImGuiStyle &style = ImGui::GetStyle();

    switch (n.type) {
    case NodeType::Text: {
        const ImVec2 t = n.wrapWidth > 0.f
                             ? ImGui::CalcTextSize(n.text.c_str(), nullptr, false, n.wrapWidth)
                             : ImGui::CalcTextSize(n.text.c_str());
        n.measuredW = t.x;
        n.measuredH = t.y;
        break;
    }
    case NodeType::Combo:
    case NodeType::SearchField: {
        n.measuredW = 140.f;
        n.measuredH = ImGui::GetFrameHeight();
        break;
    }
    case NodeType::Button: {
        const char *label = n.text.empty() ? "Button" : n.text.c_str();
        const ImVec2 t = ImGui::CalcTextSize(label);
        n.measuredW = t.x + style.FramePadding.x * 2.f;
        n.measuredH = ImGui::GetFrameHeight();
        break;
    }
    case NodeType::Checkbox: {
        const char *label = n.text.empty() ? "Check" : n.text.c_str();
        const ImVec2 t = ImGui::CalcTextSize(label);
        n.measuredW = ImGui::GetFrameHeight() + style.ItemInnerSpacing.x + t.x;
        n.measuredH = ImGui::GetFrameHeight();
        break;
    }
    case NodeType::Switch: {
        const ImVec2 t = ImGui::CalcTextSize(n.text.c_str());
        const float trackW = ImGui::GetFrameHeight() * 1.7f;
        n.measuredW = trackW + (n.text.empty() ? 0.f : style.ItemInnerSpacing.x + t.x);
        n.measuredH = ImGui::GetFrameHeight();
        break;
    }
    case NodeType::Badge: {
        const ImVec2 t = ImGui::CalcTextSize(n.text.c_str());
        n.measuredW = t.x + style.FramePadding.x * 2.f;
        n.measuredH = t.y + 6.f;
        break;
    }
    case NodeType::Slider:
    case NodeType::InputText:
        n.measuredW = 120.f;
        n.measuredH = ImGui::GetFrameHeight();
        break;
    case NodeType::Progress:
        n.measuredW = 100.f;
        n.measuredH = ImGui::GetFrameHeight();
        break;
    case NodeType::Image:
    case NodeType::ImageButton:
        n.measuredW = n.sizeX > 0.f ? n.sizeX : 32.f;
        n.measuredH = n.sizeY > 0.f ? n.sizeY : 32.f;
        break;
    case NodeType::Separator:
        n.measuredW = style.ItemSpacing.x;
        n.measuredH = 1.f;
        break;
    case NodeType::Spacer:
        n.measuredW = n.sizeX > 0.f ? n.sizeX : 0.f;
        n.measuredH = n.sizeY > 0.f ? n.sizeY : 0.f;
        break;
    case NodeType::Child:
        n.measuredW = n.sizeX > 0.f ? n.sizeX : 80.f;
        n.measuredH = n.sizeY > 0.f ? n.sizeY : 120.f;
        break;
    case NodeType::ScrollList:
        n.measuredW = n.sizeX > 0.f ? n.sizeX : 0.f;
        n.measuredH = n.sizeY > 0.f ? n.sizeY : 120.f;
        break;
    case NodeType::Viewport:
        n.measuredW = n.sizeX > 0.f ? n.sizeX : 0.f;
        n.measuredH = n.sizeY > 0.f ? n.sizeY : 240.f;
        break;
    case NodeType::CollapsingHeader: {
        const char *label = n.text.empty() ? "Section" : n.text.c_str();
        const ImVec2 t = ImGui::CalcTextSize(label);
        n.measuredW = t.x + style.FramePadding.x * 2.f + 18.f;
        n.measuredH = ImGui::GetFrameHeight();
        if (n.open) {
            float w = 0.f, h = 0.f;
            measureFlowChildren(tree, n.firstChild, &w, &h);
            n.measuredW = std::max(n.measuredW, w);
            n.measuredH += h;
        }
        break;
    }
    case NodeType::SectionHeader: {
        const ImVec2 t = ImGui::CalcTextSize(n.text.c_str());
        n.measuredW = t.x + 10.f;
        n.measuredH = std::max(ImGui::GetFrameHeight(), t.y + style.ItemSpacing.y);
        break;
    }
    case NodeType::MenuItem: {
        const ImVec2 label = ImGui::CalcTextSize(n.text.c_str());
        const ImVec2 shortcut = ImGui::CalcTextSize(n.valueText.c_str());
        n.measuredW = label.x + shortcut.x + style.ItemSpacing.x * 4.f;
        n.measuredH = ImGui::GetFrameHeight();
        break;
    }
    case NodeType::Menu: {
        const ImVec2 t = ImGui::CalcTextSize(n.text.c_str());
        n.measuredW = t.x + style.FramePadding.x * 2.f;
        n.measuredH = ImGui::GetFrameHeight();
        break;
    }
    case NodeType::Toolbar:
    case NodeType::StatusBar: {
        float w = 0.f, h = 0.f;
        for (int c = n.firstChild; c >= 0; c = tree.nodes[size_t(c)].nextSibling) {
            UINode &child = tree.nodes[size_t(c)];
            if (!child.visible) continue;
            measureNode(tree, c);
            w += child.measuredW;
            h = std::max(h, child.measuredH);
        }
        n.measuredW = w;
        n.measuredH = std::max(ImGui::GetFrameHeight(), h) + style.WindowPadding.y * 2.f;
        break;
    }
    case NodeType::Toolbox: {
        int count = 0;
        for (int c = n.firstChild; c >= 0; c = tree.nodes[size_t(c)].nextSibling) {
            measureNode(tree, c);
            if (tree.nodes[size_t(c)].visible) ++count;
        }
        const float cell = n.itemHeight > 0.f ? n.itemHeight : 40.f;
        const int cols = int(n.value) > 0 ? int(n.value) : std::max(1, std::min(4, count));
        const int rows = cols > 0 ? (count + cols - 1) / cols : 0;
        n.measuredW = float(cols) * cell + float(std::max(0, cols - 1)) * style.ItemSpacing.x;
        n.measuredH = float(rows) * cell + float(std::max(0, rows - 1)) * style.ItemSpacing.y;
        break;
    }
    case NodeType::Sidebar: {
        float w = 0.f, h = 0.f;
        measureFlowChildren(tree, n.firstChild, &w, &h);
        n.measuredW = n.sizeX > 0.f ? n.sizeX : std::max(240.f, w);
        n.measuredH = n.sizeY > 0.f ? n.sizeY : h;
        break;
    }
    case NodeType::SplitPane: {
        int first = n.firstChild;
        int second = first >= 0 ? tree.nodes[size_t(first)].nextSibling : -1;
        if (first >= 0) measureNode(tree, first);
        if (second >= 0) measureNode(tree, second);
        if (n.flexDirection == FlexDirection::Row) {
            n.measuredW = (first >= 0 ? tree.nodes[size_t(first)].measuredW : 0.f) + 5.f +
                          (second >= 0 ? tree.nodes[size_t(second)].measuredW : 0.f);
            n.measuredH = std::max(first >= 0 ? tree.nodes[size_t(first)].measuredH : 0.f,
                                   second >= 0 ? tree.nodes[size_t(second)].measuredH : 0.f);
        } else {
            n.measuredW = std::max(first >= 0 ? tree.nodes[size_t(first)].measuredW : 0.f,
                                   second >= 0 ? tree.nodes[size_t(second)].measuredW : 0.f);
            n.measuredH = (first >= 0 ? tree.nodes[size_t(first)].measuredH : 0.f) + 5.f +
                          (second >= 0 ? tree.nodes[size_t(second)].measuredH : 0.f);
        }
        break;
    }
    case NodeType::Group:
    case NodeType::Card:
    case NodeType::MenuBar: {
        float w = 0.f, h = 0.f;
        measureFlowChildren(tree, n.firstChild, &w, &h);
        n.measuredW = w + n.paddingL + n.paddingR;
        n.measuredH = h + n.paddingT + n.paddingB;
        break;
    }
    case NodeType::Flex: {
        const bool row = n.flexDirection == FlexDirection::Row;
        float mainSum = 0.f;
        float crossMax = 0.f;
        int count = 0;
        for (int c = n.firstChild; c >= 0; c = tree.nodes[size_t(c)].nextSibling) {
            UINode &child = tree.nodes[size_t(c)];
            if (!child.visible) continue;
            measureNode(tree, c);
            if (child.absolute) continue;  // absolutely placed items don't size the flex
            const float m = row ? child.measuredW : child.measuredH;
            const float cm = row ? child.measuredH : child.measuredW;
            const float mb = row ? child.marginL : child.marginT;
            const float ma = row ? child.marginR : child.marginB;
            const float cb = row ? child.marginT : child.marginL;
            const float ca = row ? child.marginB : child.marginR;
            mainSum += m + mb + ma;
            crossMax = std::max(crossMax, cm + cb + ca);
            ++count;
        }
        const float gap = n.gap >= 0.f ? n.gap : (row ? style.ItemSpacing.x : style.ItemSpacing.y);
        const float padMain = row ? n.paddingL + n.paddingR : n.paddingT + n.paddingB;
        const float padCross = row ? n.paddingT + n.paddingB : n.paddingL + n.paddingR;
        const float mainSize = mainSum + float(std::max(0, count - 1)) * gap + padMain;
        const float crossSize = crossMax + padCross;
        n.measuredW = row ? mainSize : crossSize;
        n.measuredH = row ? crossSize : mainSize;
        break;
    }
    case NodeType::Window: {
        float w = 0.f, h = 0.f;
        measureFlowChildren(tree, n.firstChild, &w, &h);
        n.measuredW = w + n.paddingL + n.paddingR;
        n.measuredH = h + n.paddingT + n.paddingB;
        break;
    }
    case NodeType::SameLine:
    default:
        break;
    }

    if (n.type != NodeType::Child && n.type != NodeType::Window) {
        if (n.sizeX > 0.f) n.measuredW = n.sizeX;
        if (n.sizeY > 0.f) n.measuredH = n.sizeY;
    }
    if (n.minSizeX > 0.f) n.measuredW = std::max(n.measuredW, n.minSizeX);
    if (n.minSizeY > 0.f) n.measuredH = std::max(n.measuredH, n.minSizeY);
    if (n.maxSizeX > 0.f) n.measuredW = std::min(n.measuredW, n.maxSizeX);
    if (n.maxSizeY > 0.f) n.measuredH = std::min(n.measuredH, n.maxSizeY);
}

void measureTree(UIHost::Tree &tree) {
    if (tree.root >= 0) measureNode(tree, tree.root);
}

}  // namespace eve::ui
