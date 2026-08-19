#pragma once

#include <string>
#include <unordered_map>
#include <vector>

namespace eve::animation {

/**
 * @brief Esoteric Spine `.atlas` text parser (region rectangles + page metadata).
 * Does not load image pixels — bind GPU textures by page name/index in SpineAnim.
 * Script type: `SpineAtlas`.
 */
class SpineAtlas {
public:
    SpineAtlas() = default;
    ~SpineAtlas() = default;

    SpineAtlas(const SpineAtlas &)            = delete;
    SpineAtlas &operator=(const SpineAtlas &) = delete;

    bool loadFromText(const std::string &text, std::string *error = nullptr);
    bool loadFromFile(const std::string &path, std::string *error = nullptr);

    void clear();

    int         getPageCount() const { return static_cast<int>(pages_.size()); }
    std::string getPageName(int pageIndex) const;
    int         getPageWidth(int pageIndex) const;
    int         getPageHeight(int pageIndex) const;

    int         getRegionCount() const { return static_cast<int>(regions_.size()); }
    int         findRegion(const std::string &name) const;
    std::string getRegionName(int index) const;
    int         getRegionPage(int index) const;
    int         getRegionX(int index) const;
    int         getRegionY(int index) const;
    int         getRegionWidth(int index) const;
    int         getRegionHeight(int index) const;
    int         getRegionOriginalWidth(int index) const;
    int         getRegionOriginalHeight(int index) const;
    int         getRegionOffsetX(int index) const;
    int         getRegionOffsetY(int index) const;
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
