#include "ui/WorldAnchorProjection.h"

#include <glm/geometric.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <algorithm>
#include <cmath>

namespace eve::ui {
namespace {

struct AnchorRect {
    float left;
    float top;
    float right;
    float bottom;
};

AnchorRect anchorRect(const WorldAnchorLayoutItem &item, float x, float y) {
    const float padding = std::max(0.f, item.padding) * 0.5f;
    const float width = std::max(1.f, item.width);
    const float height = std::max(1.f, item.height);
    const float left = x - item.pivotX * width;
    const float top = y - item.pivotY * height;
    return {left - padding, top - padding, left + width + padding,
            top + height + padding};
}

bool intersects(const AnchorRect &a, const AnchorRect &b) {
    return a.left < b.right && a.right > b.left && a.top < b.bottom &&
           a.bottom > b.top;
}

bool insideViewport(const AnchorRect &rect, float margin, float width, float height) {
    margin = std::max(0.f, margin);
    return rect.left >= margin && rect.top >= margin && rect.right <= width - margin &&
           rect.bottom <= height - margin;
}

}  // namespace

WorldAnchorProjection projectWorldAnchor(const UIHost::WorldAnchor &anchor,
                                         const glm::mat4 &viewProjection, float cameraX,
                                         float cameraY, float cameraZ, float viewportWidth,
                                         float viewportHeight) {
    WorldAnchorProjection result;
    if (!anchor.enabled) return result;
    if (viewportWidth <= 0.f || viewportHeight <= 0.f) {
        result.state = WorldAnchorState::NoCamera;
        return result;
    }
    const glm::vec4 clip = viewProjection * glm::vec4(anchor.worldX, anchor.worldY,
                                                       anchor.worldZ, 1.f);
    if (!std::isfinite(clip.w) || clip.w <= 0.f) {
        result.state = WorldAnchorState::BehindCamera;
        result.render = !anchor.hideBehindCamera;
        return result;
    }
    const glm::vec3 ndc = glm::vec3(clip) / clip.w;
    result.depth = ndc.z;
    result.screenX = (ndc.x * 0.5f + 0.5f) * viewportWidth + anchor.offsetX;
    result.screenY = (ndc.y * 0.5f + 0.5f) * viewportHeight + anchor.offsetY;
    const bool outside = ndc.x < -1.f || ndc.x > 1.f || ndc.y < -1.f || ndc.y > 1.f ||
                         ndc.z < 0.f || ndc.z > 1.f;
    if (outside && anchor.edgePolicy == WorldAnchorEdgePolicy::Hide) {
        result.state = WorldAnchorState::OutsideViewport;
        return result;
    }
    if (outside) {
        const float margin = std::max(0.f, anchor.safeMargin);
        result.screenX = std::clamp(result.screenX, margin,
                                    std::max(margin, viewportWidth - margin));
        result.screenY = std::clamp(result.screenY, margin,
                                    std::max(margin, viewportHeight - margin));
    }
    result.state = outside ? WorldAnchorState::OutsideViewport : WorldAnchorState::Visible;
    result.render = true;
    if (anchor.distanceScale) {
        const float distance = glm::length(glm::vec3(anchor.worldX - cameraX,
                                                      anchor.worldY - cameraY,
                                                      anchor.worldZ - cameraZ));
        const float reference = std::max(anchor.referenceDistance, 0.001f);
        result.scale = std::clamp(reference / std::max(distance, 0.001f),
                                  std::min(anchor.minScale, anchor.maxScale),
                                  std::max(anchor.minScale, anchor.maxScale));
    }
    return result;
}

std::vector<WorldAnchorLayoutResult> resolveWorldAnchorOverlaps(
    std::vector<WorldAnchorLayoutItem> items, float viewportWidth, float viewportHeight) {
    std::vector<WorldAnchorLayoutResult> results;
    results.reserve(items.size());
    for (const auto &item : items) {
        results.push_back({item.stableIndex, item.screenX, item.screenY, 0.f, 0.f, false});
    }
    std::sort(results.begin(), results.end(), [](const auto &a, const auto &b) {
        return a.stableIndex < b.stableIndex;
    });
    const auto resultFor = [&](std::size_t stableIndex) -> WorldAnchorLayoutResult & {
        return *std::lower_bound(results.begin(), results.end(), stableIndex,
                                 [](const auto &result, std::size_t value) {
                                     return result.stableIndex < value;
                                 });
    };
    if (viewportWidth <= 0.f || viewportHeight <= 0.f) return results;

    std::vector<AnchorRect> occupied;
    occupied.reserve(items.size());
    for (const auto &item : items) {
        if (item.avoidOverlap) continue;
        occupied.push_back(anchorRect(item, item.screenX, item.screenY));
        resultFor(item.stableIndex).render = true;
    }

    std::stable_sort(items.begin(), items.end(), [](const auto &a, const auto &b) {
        if (a.avoidOverlap != b.avoidOverlap) return a.avoidOverlap > b.avoidOverlap;
        if (a.priority != b.priority) return a.priority > b.priority;
        if (a.depth != b.depth) return a.depth < b.depth;
        return a.stableIndex < b.stableIndex;
    });

    for (const auto &item : items) {
        if (!item.avoidOverlap) continue;
        const float step = std::max(1.f, std::max(1.f, item.height) +
                                             std::max(0.f, item.padding));
        const int attempts = std::max(0, int(std::floor(std::max(0.f, item.maxDisplacement) /
                                                        step)));
        bool placed = false;
        for (int attempt = 0; attempt <= attempts * 2; ++attempt) {
            const int level = (attempt + 1) / 2;
            const float direction = attempt == 0 ? 0.f : (attempt % 2 == 1 ? -1.f : 1.f);
            const float displacementY = direction * float(level) * step;
            const float y = item.screenY + displacementY;
            const AnchorRect candidate = anchorRect(item, item.screenX, y);
            if (!insideViewport(candidate, item.safeMargin, viewportWidth, viewportHeight))
                continue;
            if (std::any_of(occupied.begin(), occupied.end(), [&](const AnchorRect &other) {
                    return intersects(candidate, other);
                }))
                continue;
            auto &result = resultFor(item.stableIndex);
            result.screenY = y;
            result.displacementY = displacementY;
            result.render = true;
            occupied.push_back(candidate);
            placed = true;
            break;
        }
        if (!placed) resultFor(item.stableIndex).render = false;
    }

    return results;
}

}  // namespace eve::ui
