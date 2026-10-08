#pragma once
#include "common/Export.h"


#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace eve::procgen {

/**
 * @brief Named rectangle or asset placement emitted by a procedural generator.
 *
 * Coordinates and dimensions are expressed in tiles. The value owns all strings,
 * has no external lifetime requirements, and may be copied between threads when
 * the containing Grid2D is not being mutated concurrently.
 */
struct GridObject {
    std::string name;
    std::string type;
    float x = 0.f;
    float y = 0.f;
    float width = 0.f;
    float height = 0.f;
    uint32_t gid = 0;
    std::string asset;
    float rotationDegrees = 0.f;
    uint32_t placementFlags = 0;
};

/**
 * @brief Intermediate 2D generation result. `cells` store semantic ids (see Semantic.h),
 * not tile GIDs — convert via Palette when applying to a TileLayer.
 */
class EVENGINE_API_DOMAINS Grid2D {
public:
    /** @brief Resize. */
    void resize(int width, int height);
    /** @brief Returns the width. */
    int  getWidth() const;
    /** @brief Returns the height. */
    int  getHeight() const;

    /** @brief Sets the cell. */
    void setCell(int x, int y, int semantic);
    /** @brief Returns the cell. */
    int  getCell(int x, int y) const;
    /** @brief Fill. */
    void fill(int semantic);

    /**
     * @brief Per-cell detail layer (0..255), parallel to `cells`. Semantics stay in
     * `cells` (palette/GID compatible); `detail` carries algorithm-specific
     * extras such as wall-autotile direction masks, floor-pattern variants and
     * decor tile indices. Semantics that have no detail use 0.
     */
    void setDetail(int x, int y, int value);
    /** @brief Returns the detail. */
    int  getDetail(int x, int y) const;

    /** @brief Sets the meta. */
    void        setMeta(const std::string &key, const std::string &value);
    /** @brief Returns the meta. */
    std::string getMeta(const std::string &key, const std::string &defaultValue) const;
    /** @brief Return all metadata key/value pairs in deterministic map order. */
    const std::unordered_map<std::string, std::string> &metadata() const { return meta_; }

    /** @brief Clears objects. */
    void clearObjects();
    /** @brief Script-friendly: name/type + tile coords. */
    void addObjectAt(const std::string &name, const std::string &type, float x, float y);
    /** @brief Adds object. */
    void addObject(const std::string &name, const std::string &type, float x, float y, float width,
                   float height, int gid);
    /** @brief Add a placed asset while preserving its semantic role, asset id and orientation. */
    void addAssetObject(const std::string &name, const std::string &role,
                        const std::string &asset, float x, float y, float width, float height,
                        float rotationDegrees, int flags);
    /** @brief Returns the object count. */
    int         getObjectCount() const;
    /** @brief Returns the object name. */
    std::string getObjectName(int i) const;
    /** @brief Returns the object type. */
    std::string getObjectType(int i) const;
    /** @brief Returns the object x. */
    float       getObjectX(int i) const;
    /** @brief Returns the object y. */
    float       getObjectY(int i) const;
    /** @brief Returns the object width. */
    float       getObjectWidth(int i) const;
    /** @brief Returns the object height. */
    float       getObjectHeight(int i) const;
    /** @brief Returns the object gid. */
    int         getObjectGid(int i) const;
    /** @brief Returns the object asset. */
    std::string getObjectAsset(int i) const;
    /** @brief Returns the object rotation. */
    float       getObjectRotation(int i) const;
    /** @brief Returns the object flags. */
    int         getObjectFlags(int i) const;

    /** @brief Cells. */
    const std::vector<uint32_t>     &cells() const { return cells_; }
    /** @brief Cells. */
    std::vector<uint32_t>           &cells() { return cells_; }
    /** @brief Detail. */
    const std::vector<uint8_t>      &detail() const { return detail_; }
    /** @brief Detail. */
    std::vector<uint8_t>            &detail() { return detail_; }
    /** @brief Objects. */
    const std::vector<GridObject> &objects() const { return objects_; }

private:
    bool inBounds(int x, int y) const;

    int                                      width_  = 0;
    int                                      height_ = 0;
    std::vector<uint32_t>                    cells_;
    std::vector<uint8_t>                     detail_;
    std::unordered_map<std::string, std::string> meta_;
    std::vector<GridObject>                  objects_;
};

}  // namespace eve::procgen
