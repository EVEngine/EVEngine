#pragma once
#include "common/Export.h"


#include <string>
#include <unordered_map>
#include <vector>

namespace eve::animation {

/**
 * @brief Esoteric Spine `.atlas` text parser (region rectangles + page metadata).
 * Does not load image pixels — bind GPU textures by page name/index in SpineAnim.
 * Script type: `SpineAtlas`.
 */
class EVENGINE_API_WORLD SpineAtlas {
public:
    /** @brief Spine atlas. */
    SpineAtlas() = default;
    /** @brief Spine atlas. */
    ~SpineAtlas() = default;

    SpineAtlas(const SpineAtlas &)            = delete;
    SpineAtlas &operator=(const SpineAtlas &) = delete;

    /** @brief Loads from text. */
    bool loadFromText(const std::string &text, std::string *error = nullptr);
    /** @brief Loads from file. */
    bool loadFromFile(const std::string &path, std::string *error = nullptr);

    /** @brief Clears . */
    void clear();

    /** @brief Returns the page count. */
    int         getPageCount() const { return static_cast<int>(pages_.size()); }
    /** @brief Returns the page name. */
    std::string getPageName(int pageIndex) const;
    /** @brief Returns the page width. */
    int         getPageWidth(int pageIndex) const;
    /** @brief Returns the page height. */
    int         getPageHeight(int pageIndex) const;

    /** @brief Returns the region count. */
    int         getRegionCount() const { return static_cast<int>(regions_.size()); }
    /** @brief Finds region. */
    int         findRegion(const std::string &name) const;
    /** @brief Returns the region name. */
    std::string getRegionName(int index) const;
    /** @brief Returns the region page. */
    int         getRegionPage(int index) const;
    /** @brief Returns the region x. */
    int         getRegionX(int index) const;
    /** @brief Returns the region y. */
    int         getRegionY(int index) const;
    /** @brief Returns the region width. */
    int         getRegionWidth(int index) const;
    /** @brief Returns the region height. */
    int         getRegionHeight(int index) const;
    /** @brief Returns the region original width. */
    int         getRegionOriginalWidth(int index) const;
    /** @brief Returns the region original height. */
    int         getRegionOriginalHeight(int index) const;
    /** @brief Returns the region offset x. */
    int         getRegionOffsetX(int index) const;
    /** @brief Returns the region offset y. */
    int         getRegionOffsetY(int index) const;
    /** @brief Returns the region rotate. */
    bool        getRegionRotate(int index) const;

    /** @brief Normalized UVs for a texture of texW×texH (usually page size). */
    void getRegionUV(int index, int texW, int texH, float &u0, float &v0, float &u1,
                     float &v1) const;

private:
    struct Page {
        std::string name;
        int         width  = 0;
        int         height = 0;
    };

    struct Region {
        std::string name;
        int         pageIndex = 0;
        int         x = 0, y = 0, w = 0, h = 0;
        int         origW = 0, origH = 0;
        int         offsetX = 0, offsetY = 0;
        bool        rotate = false;
    };

    void checkPage(int index) const;
    void checkRegion(int index) const;

    std::vector<Page>                    pages_;
    std::vector<Region>                  regions_;
    std::unordered_map<std::string, int> regionByName_;
};

}  // namespace eve::animation
