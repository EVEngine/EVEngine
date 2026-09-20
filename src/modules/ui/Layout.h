#pragma once
#include "common/Export.h"


#include "ui/UIHost.h"

#include <vector>

namespace eve::ui {

/**
 * Two-phase retained layout:
 *   measure — `measureTree()` computes real desired sizes (recursive, includes
 *             nested containers, text metrics, margins/padding/min/max);
 *   arrange — `flexArrange()` is a pure function that distributes flex items
 *             into rects given measured sizes + layout props.
 *
 * Rendering consumes the arranged rects instead of guessing sizes from the
 * ImGui cursor, which fixes nested elastic layouts and enables anchors,
 * percentage sizing, margins and absolute placement.
 */

/** One flex item as seen by the arrange pass (already measured). */
struct FlexItemSpec {
    float basisMain = 0.f;        // measured content size on main axis
    float basisCross = 0.f;       // measured content size on cross axis
    float marginBefore = 0.f;     // leading main-axis margin
    float marginAfter = 0.f;      // trailing main-axis margin
    float marginCrossBefore = 0.f;
    float marginCrossAfter = 0.f;
    float flexGrow = 0.f;
    float flexShrink = 1.f;       // negative free space weight; 0 keeps basis fixed
    float flexBasis = -1.f;       // >=0 overrides measured basis before min/max
    float aspectRatio = 0.f;      // width / height; derives an automatic cross size
    bool isSpacer = false;        // default grow = 1
    bool absolute = false;        // excluded from flex flow
    float anchorMain = 0.f;       // 0..1 anchor point inside content box
    float anchorCross = 0.f;
    float posMain = 0.f;          // offset from the anchored edge
    float posCross = 0.f;
    float percentMain = 0.f;      // 0..1 of available main size; overrides basis
    float percentCross = 0.f;
    float minMain = 0.f;
    float maxMain = 0.f;
    float minCross = 0.f;
    float maxCross = 0.f;
    float explicitMain = 0.f;     // >0 overrides basisMain (fixed size)
    float explicitCross = 0.f;    // >0 overrides basisCross
    /** -1 = inherit container align; otherwise FlexAlign value (int). */
    int alignSelf = -1;
};

/** Content rect (margins excluded), relative to the container content box. */
struct FlexRect {
    float x = 0.f;
    float y = 0.f;
    float w = 0.f;
    float h = 0.f;
};

struct FlexResult {
    std::vector<FlexRect> items;  // one per non-absolute item, same order
    float contentW = 0.f;         // total outer extent on X
    float contentH = 0.f;         // total outer extent on Y
    float overflowMain = 0.f;      // unresolved main-axis deficit after shrink/min limits
};

/**
 * Distribute flex items along main/cross axis. Pure — no ImGui state touched,
 * so it can be unit-tested headlessly.
 */
EVENGINE_API_WORLD FlexResult flexArrange(bool row, float gap, float availMain, float availCross,
                       FlexAlign containerAlign, FlexJustify justify,
                       const std::vector<FlexItemSpec> &items, bool wrap = false,
                       float crossGap = -1.f);

struct GridItemSpec {
    float basisW = 0.f;
    float basisH = 0.f;
    float marginL = 0.f;
    float marginT = 0.f;
    float marginR = 0.f;
    float marginB = 0.f;
    float aspectRatio = 0.f;
    int columnSpan = 1;
};

struct GridResult {
    std::vector<FlexRect> items;
    float contentW = 0.f;
    float contentH = 0.f;
    float overflowX = 0.f;
};

/** @brief Places source-ordered items into equal-width fixed columns. */
EVENGINE_API_WORLD GridResult gridArrange(int columns, float columnGap, float rowGap, float availWidth,
                       const std::vector<GridItemSpec> &items);

/** Measure one node (recursively) and fill UINode::measuredW/H. */
void measureNode(UIHost::Tree &tree, int index);

/** Measure the whole tree from its root. */
EVENGINE_API_WORLD void measureTree(UIHost::Tree &tree);

/** Flow measure helpers for non-flex containers (Window/Group/Child/Header). */
void measureFlowChildren(UIHost::Tree &tree, int firstChild, float *outW, float *outH);

}  // namespace eve::ui
