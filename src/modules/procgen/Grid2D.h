#pragma once

#include "map/MapObject.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace eve::procgen {

/**
 * @brief Intermediate 2D generation result. `cells` store semantic ids (see Semantic.h),
 * not tile GIDs — convert via Palette when applying to a TileLayer.
 */
class Grid2D {
public:
    void resize(int width, int height);
    int  getWidth() const;
    int  getHeight() const;

    void setCell(int x, int y, int semantic);
    int  getCell(int x, int y) const;
    void fill(int semantic);

    /**
     * @brief Per-cell detail layer (0..255), parallel to `cells`. Semantics stay in
     * `cells` (palette/GID compatible); `detail` carries algorithm-specific
     * extras such as wall-autotile direction masks, floor-pattern variants and
     * decor tile indices. Semantics that have no detail use 0.
     */
    void setDetail(int x, int y, int value);
    int  getDetail(int x, int y) const;

    void        setMeta(const std::string &key, const std::string &value);
    std::string getMeta(const std::string &key, const std::string &defaultValue) const;

    void clearObjects();
    /** @brief Script-friendly: name/type + tile coords. */
    void addObjectAt(const std::string &name, const std::string &type, float x, float y);
    void addObject(const std::string &name, const std::string &type, float x, float y, float width,
                   float height, int gid);
    int         getObjectCount() const;
    std::string getObjectName(int i) const;
    std::string getObjectType(int i) const;
    float       getObjectX(int i) const;
    float       getObjectY(int i) const;
    float       getObjectWidth(int i) const;
    float       getObjectHeight(int i) const;
    int         getObjectGid(int i) const;

    const std::vector<uint32_t>     &cells() const { return cells_; }
    std::vector<uint32_t>           &cells() { return cells_; }
    const std::vector<uint8_t>      &detail() const { return detail_; }
    std::vector<uint8_t>            &detail() { return detail_; }
    const std::vector<map::MapObject> &objects() const { return objects_; }

private:
    bool inBounds(int x, int y) const;

    int                                      width_  = 0;
    int                                      height_ = 0;
    std::vector<uint32_t>                    cells_;
    std::vector<uint8_t>                     detail_;
    std::unordered_map<std::string, std::string> meta_;
    std::vector<map::MapObject>              objects_;
};

}  // namespace eve::procgen
