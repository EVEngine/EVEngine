#pragma once

#include <string>
#include <unordered_map>

namespace eve::editor {

/** @brief Simple editor chrome regions: left / right / top / bottom / center. */
class EditorDock {
public:
    EditorDock();

    void setRegionSize(const std::string &region, float pixels);
    float getRegionSize(const std::string &region) const;

    void layout(float screenW, float screenH);

    float getRegionX(const std::string &region) const;
    float getRegionY(const std::string &region) const;
    float getRegionW(const std::string &region) const;
    float getRegionH(const std::string &region) const;

private:
    struct Rect {
        float x = 0.f, y = 0.f, w = 0.f, h = 0.f;
    };

    float sizeOf(const std::string &region) const;
    const Rect *rectOf(const std::string &region) const;

    std::unordered_map<std::string, float> sizes_;
    std::unordered_map<std::string, Rect> rects_;
};

}  // namespace eve::editor
