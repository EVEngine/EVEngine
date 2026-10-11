#include "ui/Layout.h"
#include "ui/Theme.h"

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

FlexResult flexArrangeSingleLine(bool row, float gap, float availMain, float availCross,
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
                      : (s.percentMain > 0.f
                             ? s.percentMain * availMain
                             : (s.flexBasis >= 0.f ? s.flexBasis : s.basisMain));
        m = clampV(m, s.minMain, s.maxMain);
        float c = s.explicitCross > 0.f
                      ? s.explicitCross
                      : (s.percentCross > 0.f ? s.percentCross * availCross : s.basisCross);
        c = clampV(c, s.minCross, s.maxCross);
        if (s.aspectRatio > 0.f && s.explicitCross <= 0.f && s.percentCross <= 0.f)
            c = row ? m / s.aspectRatio : m * s.aspectRatio;
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
    if (freeSpace < 0.f) {
        const float deficit = -freeSpace;
        float shrinkWeight = 0.f;
        for (int i = 0; i < n; ++i) {
            const FlexItemSpec &s = items[size_t(i)];
            if (!s.absolute && s.flexShrink > 0.f)
                shrinkWeight += s.flexShrink * mainSize[size_t(i)];
        }
        if (shrinkWeight > 0.f) {
            for (int i = 0; i < n; ++i) {
                const FlexItemSpec &s = items[size_t(i)];
                if (s.absolute || s.flexShrink <= 0.f) continue;
                const float share = deficit *
                                    (s.flexShrink * mainSize[size_t(i)] / shrinkWeight);
                mainSize[size_t(i)] = clampV(std::max(0.f, mainSize[size_t(i)] - share),
                                             s.minMain, s.maxMain);
            }
        }
        float shrunkTotal = float(gapCount) * gap;
        for (int i = 0; i < n; ++i) {
            const FlexItemSpec &s = items[size_t(i)];
            if (!s.absolute)
                shrunkTotal += mainSize[size_t(i)] + s.marginBefore + s.marginAfter;
        }
        res.overflowMain = std::max(0.f, shrunkTotal - availMain);
        freeSpace = 0.f;
    }

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
        const FlexAlign align =
            s.alignSelf >= 0 ? FlexAlign(s.alignSelf) : containerAlign;
        const bool stretch = align == FlexAlign::Stretch;
        if (stretch && s.explicitCross <= 0.f && s.percentCross <= 0.f) {
            float c = availCross - cb - ca;
            if (c < 0.f) c = 0.f;
            crossSize[size_t(i)] = c;
        }
        switch (align) {
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

FlexResult flexArrange(bool row, float gap, float availMain, float availCross,
                       FlexAlign containerAlign, FlexJustify justify,
                       const std::vector<FlexItemSpec> &items, bool wrap, float crossGap) {
    if (!wrap)
        return flexArrangeSingleLine(row, gap, availMain, availCross, containerAlign, justify,
                                     items);

    FlexResult result;
    result.items.resize(items.size());
    if (crossGap < 0.f) crossGap = gap;
    std::vector<size_t> lineIndices;
    float lineOuterMain = 0.f;
    float crossOffset = 0.f;

    auto flushLine = [&]() {
        if (lineIndices.empty()) return;
        std::vector<FlexItemSpec> line;
        line.reserve(lineIndices.size());
        float lineCross = 0.f;
        for (size_t index : lineIndices) {
            line.push_back(items[index]);
            const auto &s = items[index];
            float cross = s.explicitCross > 0.f ? s.explicitCross : s.basisCross;
            float main = s.explicitMain > 0.f
                             ? s.explicitMain
                             : (s.flexBasis >= 0.f ? s.flexBasis : s.basisMain);
            if (s.aspectRatio > 0.f && s.explicitCross <= 0.f && s.percentCross <= 0.f)
                cross = row ? main / s.aspectRatio : main * s.aspectRatio;
            lineCross = std::max(lineCross,
                                 cross + s.marginCrossBefore + s.marginCrossAfter);
        }
        const FlexResult arranged = flexArrangeSingleLine(
            row, gap, availMain, lineCross, containerAlign, justify, line);
        for (size_t i = 0; i < lineIndices.size(); ++i) {
            FlexRect rect = arranged.items[i];
            if (row) rect.y += crossOffset;
            else rect.x += crossOffset;
            result.items[lineIndices[i]] = rect;
        }
        result.overflowMain = std::max(result.overflowMain, arranged.overflowMain);
        const float lineMain = row ? arranged.contentW : arranged.contentH;
        result.contentW = row ? std::max(result.contentW, lineMain)
                              : crossOffset + lineCross;
        result.contentH = row ? crossOffset + lineCross
                              : std::max(result.contentH, lineMain);
        crossOffset += lineCross + crossGap;
        lineIndices.clear();
        lineOuterMain = 0.f;
    };

    for (size_t i = 0; i < items.size(); ++i) {
        const FlexItemSpec &s = items[i];
        if (s.absolute) {
            const FlexResult absolute = flexArrangeSingleLine(
                row, 0.f, availMain, availCross, containerAlign, justify, {s});
            result.items[i] = absolute.items[0];
            continue;
        }
        float main = s.explicitMain > 0.f
                         ? s.explicitMain
                         : (s.percentMain > 0.f
                                ? s.percentMain * availMain
                                : (s.flexBasis >= 0.f ? s.flexBasis : s.basisMain));
        main = clampV(main, s.minMain, s.maxMain) + s.marginBefore + s.marginAfter;
        const float next = lineOuterMain + (lineIndices.empty() ? 0.f : gap) + main;
        if (!lineIndices.empty() && next > availMain) flushLine();
        lineOuterMain += (lineIndices.empty() ? 0.f : gap) + main;
        lineIndices.push_back(i);
    }
    flushLine();
    if (row) result.contentH = std::max(0.f, result.contentH);
    else result.contentW = std::max(0.f, result.contentW);
    return result;
}

GridResult gridArrange(int columns, float columnGap, float rowGap, float availWidth,
                       const std::vector<GridItemSpec> &items) {
    GridResult result;
    result.items.resize(items.size());
    columns = std::max(1, columns);
    columnGap = std::max(0.f, columnGap);
    rowGap = std::max(0.f, rowGap);
    const float gaps = float(columns - 1) * columnGap;
    const float cellWidth = std::max(0.f, (availWidth - gaps) / float(columns));
    struct Slot { int row = 0; int column = 0; int span = 1; };
    std::vector<Slot> slots(items.size());
    std::vector<float> rowHeights;
    int row = 0;
    int column = 0;
    for (size_t i = 0; i < items.size(); ++i) {
        const int span = std::clamp(items[i].columnSpan, 1, columns);
        if (column + span > columns) {
            ++row;
            column = 0;
        }
        if (row >= int(rowHeights.size())) rowHeights.resize(size_t(row + 1), 0.f);
        slots[i] = {row, column, span};
        const float width = cellWidth * float(span) + columnGap * float(span - 1) -
                            items[i].marginL - items[i].marginR;
        float height = items[i].basisH;
        if (items[i].aspectRatio > 0.f) height = std::max(0.f, width) / items[i].aspectRatio;
        rowHeights[size_t(row)] =
            std::max(rowHeights[size_t(row)], height + items[i].marginT + items[i].marginB);
        column += span;
        if (column >= columns) {
            ++row;
            column = 0;
        }
    }
    std::vector<float> rowY(rowHeights.size(), 0.f);
    for (size_t r = 1; r < rowHeights.size(); ++r)
        rowY[r] = rowY[r - 1] + rowHeights[r - 1] + rowGap;
    for (size_t i = 0; i < items.size(); ++i) {
        const Slot slot = slots[i];
        const float outerW = cellWidth * float(slot.span) + columnGap * float(slot.span - 1);
        FlexRect &rect = result.items[i];
        rect.x = float(slot.column) * (cellWidth + columnGap) + items[i].marginL;
        rect.y = rowY[size_t(slot.row)] + items[i].marginT;
        rect.w = std::max(0.f, outerW - items[i].marginL - items[i].marginR);
        rect.h = items[i].aspectRatio > 0.f
                     ? rect.w / items[i].aspectRatio
                     : std::max(0.f, items[i].basisH);
    }
    result.contentW = std::max(0.f, availWidth);
    if (!rowHeights.empty())
        result.contentH = rowY.back() + rowHeights.back();
    result.overflowX = std::max(0.f, gaps - availWidth);
    return result;
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
        n.measuredW = globalTheme().layout.searchMinWidth * themeUiScale();
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
    case NodeType::ColorPalette: {
        const float swatch = ImGui::GetFrameHeight();
        const float gap = 4.f;
        const int cols = 8;
        int count = 0;
        if (!n.valueText.empty()) {
            for (size_t i = 0; i < n.valueText.size();) {
                size_t end = n.valueText.find_first_of(";\n", i);
                if (end == std::string::npos) end = n.valueText.size();
                if (end > i) ++count;
                i = end + 1;
            }
        }
        if (count <= 0) count = 16;
        const int rows = (count + cols - 1) / cols;
        n.measuredW = n.sizeX > 0.f ? n.sizeX : 220.f;
        n.measuredH = ImGui::GetFrameHeight() + style.ItemSpacing.y +
                      float(rows) * swatch + float(std::max(0, rows - 1)) * gap;
        break;
    }
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
        n.measuredH = std::max(ImGui::GetFrameHeight(), t.y + style.ItemSpacing.y) +
                      globalTheme().layout.sectionSpacingY * themeUiScale();
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
        const float defaultHeight =
            (n.type == NodeType::StatusBar ? globalTheme().layout.statusBarHeight
                                           : globalTheme().layout.toolbarHeight) *
            themeUiScale();
        n.measuredH = n.sizeY > 0.f ? n.sizeY : defaultHeight;
        break;
    }
    case NodeType::Toolbox: {
        int count = 0;
        for (int c = n.firstChild; c >= 0; c = tree.nodes[size_t(c)].nextSibling) {
            measureNode(tree, c);
            if (tree.nodes[size_t(c)].visible) ++count;
        }
        const float cell = n.itemHeight > 0.f
                               ? n.itemHeight
                               : globalTheme().layout.toolboxCellSize * themeUiScale();
        const int cols = int(n.value) > 0 ? int(n.value) : std::max(1, std::min(4, count));
        const int rows = cols > 0 ? (count + cols - 1) / cols : 0;
        n.measuredW = float(cols) * cell + float(std::max(0, cols - 1)) * style.ItemSpacing.x;
        n.measuredH = float(rows) * cell + float(std::max(0, rows - 1)) * style.ItemSpacing.y;
        break;
    }
    case NodeType::Sidebar: {
        float w = 0.f, h = 0.f;
        measureFlowChildren(tree, n.firstChild, &w, &h);
        const float defaultWidth = globalTheme().layout.sidebarWidth * themeUiScale();
        n.measuredW = n.sizeX > 0.f ? n.sizeX : std::max(defaultWidth, w);
        n.measuredH = n.sizeY > 0.f ? n.sizeY : h;
        break;
    }
    case NodeType::SplitPane: {
        int first = n.firstChild;
        int second = first >= 0 ? tree.nodes[size_t(first)].nextSibling : -1;
        if (first >= 0) measureNode(tree, first);
        if (second >= 0) measureNode(tree, second);
        if (n.flexDirection == FlexDirection::Row) {
            n.measuredW = (first >= 0 ? tree.nodes[size_t(first)].measuredW : 0.f) +
                          globalTheme().layout.splitterSize * themeUiScale() +
                          (second >= 0 ? tree.nodes[size_t(second)].measuredW : 0.f);
            n.measuredH = std::max(first >= 0 ? tree.nodes[size_t(first)].measuredH : 0.f,
                                   second >= 0 ? tree.nodes[size_t(second)].measuredH : 0.f);
        } else {
            n.measuredW = std::max(first >= 0 ? tree.nodes[size_t(first)].measuredW : 0.f,
                                   second >= 0 ? tree.nodes[size_t(second)].measuredW : 0.f);
            n.measuredH = (first >= 0 ? tree.nodes[size_t(first)].measuredH : 0.f) +
                          globalTheme().layout.splitterSize * themeUiScale() +
                          (second >= 0 ? tree.nodes[size_t(second)].measuredH : 0.f);
        }
        break;
    }
    case NodeType::Group:
    case NodeType::Card:
    case NodeType::NinePatchPanel:
    case NodeType::MenuBar: {
        float w = 0.f, h = 0.f;
        measureFlowChildren(tree, n.firstChild, &w, &h);
        n.measuredW = w + n.paddingL + n.paddingR;
        n.measuredH = h + n.paddingT + n.paddingB;
        break;
    }
    case NodeType::Grid: {
        std::vector<GridItemSpec> items;
        float maxCellWidth = 0.f;
        for (int c = n.firstChild; c >= 0; c = tree.nodes[size_t(c)].nextSibling) {
            UINode &child = tree.nodes[size_t(c)];
            if (!child.visible || child.absolute) continue;
            measureNode(tree, c);
            GridItemSpec item;
            item.basisW = child.measuredW;
            item.basisH = child.measuredH;
            item.marginL = child.marginL;
            item.marginT = child.marginT;
            item.marginR = child.marginR;
            item.marginB = child.marginB;
            item.aspectRatio = child.aspectRatio;
            item.columnSpan = child.gridColumnSpan;
            items.push_back(item);
            maxCellWidth = std::max(maxCellWidth,
                                    (child.measuredW + child.marginL + child.marginR) /
                                        float(std::max(1, child.gridColumnSpan)));
        }
        const int columns = std::max(1, n.gridColumns);
        const float columnGap = n.columnGap >= 0.f
                                    ? n.columnGap
                                    : (n.gap >= 0.f ? n.gap : style.ItemSpacing.x);
        const float rowGap = n.rowGap >= 0.f
                                 ? n.rowGap
                                 : (n.gap >= 0.f ? n.gap : style.ItemSpacing.y);
        const float naturalWidth = maxCellWidth * float(columns) +
                                   columnGap * float(columns - 1);
        const GridResult arranged =
            gridArrange(columns, columnGap, rowGap, naturalWidth, items);
        n.measuredW = arranged.contentW + n.paddingL + n.paddingR;
        n.measuredH = arranged.contentH + n.paddingT + n.paddingB;
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
    if (n.aspectRatio > 0.f) {
        if (n.sizeX > 0.f && n.sizeY <= 0.f) n.measuredH = n.measuredW / n.aspectRatio;
        else if (n.sizeY > 0.f && n.sizeX <= 0.f) n.measuredW = n.measuredH * n.aspectRatio;
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
