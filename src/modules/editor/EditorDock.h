#pragma once
#include "common/Export.h"


#include <string>
#include <unordered_map>

namespace eve::editor {

/** @brief Simple editor chrome regions: left / right / top / bottom / center. */
class EVENGINE_API_ORCHESTRATION EditorDock {
public:
    /** @brief Editor dock. */
    EditorDock();

    /** @brief Sets the region size. */
    void setRegionSize(const std::string &region, float pixels);
    /** @brief Returns the region size. */
    float getRegionSize(const std::string &region) const;

    /** @brief Layout. */
    void layout(float screenW, float screenH);

    /** @brief Returns the region x. */
    float getRegionX(const std::string &region) const;
    /** @brief Returns the region y. */
    float getRegionY(const std::string &region) const;
    /** @brief Returns the region w. */
    float getRegionW(const std::string &region) const;
    /** @brief Returns the region h. */
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
