#include "editor/EditorDock.h"

#include "common/Exception.h"

namespace eve::editor {

EditorDock::EditorDock() {
    sizes_["left"] = 200.f;
    sizes_["right"] = 280.f;
    sizes_["top"] = 40.f;
    sizes_["bottom"] = 24.f;
}

void EditorDock::setRegionSize(const std::string &region, float pixels) {
    if (region != "left" && region != "right" && region != "top" && region != "bottom") {
        throw Exception("EditorDock::setRegionSize: expected left|right|top|bottom");
    }
    if (pixels < 0.f) throw Exception("EditorDock::setRegionSize: size must be >= 0");
    sizes_[region] = pixels;
}

float EditorDock::getRegionSize(const std::string &region) const { return sizeOf(region); }

float EditorDock::sizeOf(const std::string &region) const {
    auto it = sizes_.find(region);
    return it == sizes_.end() ? 0.f : it->second;
}

void EditorDock::layout(float screenW, float screenH) {
    if (screenW <= 0.f || screenH <= 0.f) {
        throw Exception("EditorDock::layout: screen size must be > 0");
    }
    float left = sizeOf("left");
    float right = sizeOf("right");
    float top = sizeOf("top");
    float bottom = sizeOf("bottom");
    // Clamp so center remains non-negative
    if (left + right > screenW) {
        float s = screenW / (left + right);
        left *= s;
        right *= s;
    }
    if (top + bottom > screenH) {
        float s = screenH / (top + bottom);
        top *= s;
        bottom *= s;
    }

    rects_["top"] = {0.f, 0.f, screenW, top};
    rects_["bottom"] = {0.f, screenH - bottom, screenW, bottom};
    rects_["left"] = {0.f, top, left, screenH - top - bottom};
    rects_["right"] = {screenW - right, top, right, screenH - top - bottom};
    rects_["center"] = {left, top, screenW - left - right, screenH - top - bottom};
}

const EditorDock::Rect *EditorDock::rectOf(const std::string &region) const {
    auto it = rects_.find(region);
    return it == rects_.end() ? nullptr : &it->second;
}

float EditorDock::getRegionX(const std::string &region) const {
    const Rect *r = rectOf(region);
    if (!r) throw Exception("EditorDock::getRegionX: call layout() first / unknown region");
    return r->x;
}
float EditorDock::getRegionY(const std::string &region) const {
    const Rect *r = rectOf(region);
    if (!r) throw Exception("EditorDock::getRegionY: call layout() first / unknown region");
    return r->y;
}
float EditorDock::getRegionW(const std::string &region) const {
    const Rect *r = rectOf(region);
    if (!r) throw Exception("EditorDock::getRegionW: call layout() first / unknown region");
    return r->w;
}
float EditorDock::getRegionH(const std::string &region) const {
    const Rect *r = rectOf(region);
    if (!r) throw Exception("EditorDock::getRegionH: call layout() first / unknown region");
    return r->h;
}

}  // namespace eve::editor
