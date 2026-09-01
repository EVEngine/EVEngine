#pragma once

#include "common/BorrowedRef.h"
#include "level_editing/TileBuffer.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace eve::level_editing {

/** @brief Whether a requested level mutation changed authoritative document state. */
enum class LevelChange { Changed, Unchanged };

/** @brief One freely extensible object placed in a top-down level. */
struct LevelObject {
    std::string id;
    std::string type;
    std::string name;
    float       x = 0.f, y = 0.f, width = 0.f, height = 0.f, rotation = 0.f;
    bool        visible = true;

    std::unordered_map<std::string, std::string> properties;
};

/** @brief A tile or object layer in a LevelDocument. */
struct LevelLayer {
    enum class Kind { Tiles, Objects };

    std::string id;
    std::string name;
    Kind        kind    = Kind::Tiles;
    bool        visible = true;
    bool        locked  = false;
    float       opacity = 1.f;
    float       offsetX = 0.f, offsetY = 0.f;

    std::unique_ptr<TileBuffer> tiles;
    std::vector<LevelObject>    objects;

    std::unordered_map<std::string, std::string> properties;
};

/**
 * @brief Format-neutral, editable top-down level model.
 *
 * Coordinates are world pixels, tile GID 0 means empty, and orientation is one
 * of orthogonal, isometric, staggered or hexagonal. Unknown developer data can
 * be preserved in string properties at document, layer and object scope.
 */
class LevelDocument {
public:
    LevelDocument(int width = 1, int height = 1, float tileWidth = 32.f, float tileHeight = 32.f);

    /** @brief Changes map dimensions and resizes all tile layers. */
    void               resize(int width, int height);
    int                getWidth() const { return width_; }
    int                getHeight() const { return height_; }
    float              getTileWidth() const { return tileWidth_; }
    float              getTileHeight() const { return tileHeight_; }
    void               setTileSize(float width, float height);
    void               setOrientation(const std::string& orientation);
    const std::string& getOrientation() const { return orientation_; }

    int                       addTileLayer(const std::string& name);
    int                       addObjectLayer(const std::string& name);
    [[nodiscard]] LevelChange removeLayer(int index);
    [[nodiscard]] LevelChange moveLayer(int from, int to);
    int                       getLayerCount() const { return static_cast<int>(layers_.size()); }
    const std::string&        getLayerName(int index) const;
    void                      setLayerName(int index, const std::string& name);
    const std::string         getLayerKind(int index) const;

    [[nodiscard]] eve::OptionalRef<TileBuffer>       getTileLayer(int index);
    [[nodiscard]] eve::OptionalRef<const TileBuffer> getTileLayer(int index) const;
    [[nodiscard]] eve::OptionalRef<LevelLayer>       layer(int index);
    [[nodiscard]] eve::OptionalRef<const LevelLayer> layer(int index) const;

    int addObject(int layerIndex, const std::string& type, float x, float y);
    int getObjectCount(int layerIndex) const;

    [[nodiscard]] eve::OptionalRef<LevelObject>       object(int layerIndex, int objectIndex);
    [[nodiscard]] eve::OptionalRef<const LevelObject> object(int layerIndex, int objectIndex) const;
    [[nodiscard]] LevelChange                         removeObject(int layerIndex, int objectIndex);

    void        setProperty(const std::string& key, const std::string& value);
    std::string getProperty(const std::string& key, const std::string& fallback = {}) const;

    std::unordered_map<std::string, std::string>&       properties() { return properties_; }
    const std::unordered_map<std::string, std::string>& properties() const { return properties_; }

    std::vector<LevelLayer>&       layers() { return layers_; }
    const std::vector<LevelLayer>& layers() const { return layers_; }

private:
    std::string nextId(const char* prefix);
    int         width_, height_;
    float       tileWidth_, tileHeight_;
    std::string orientation_ = "orthogonal";
    unsigned    nextId_      = 1;

    std::vector<LevelLayer>                      layers_;
    std::unordered_map<std::string, std::string> properties_;
};

}  // namespace eve::level_editing
