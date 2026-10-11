#pragma once
#include "common/Export.h"


#include "common/BorrowedRef.h"
#include "common/Value.h"
#include "map/level/editing/TileBuffer.h"

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
    /** @brief Owned format fields not represented above; codec excludes authoritative fields. */
    eve::Value::Object extensions;
};

/** @brief A tile or object layer in a LevelDocument. */
struct LevelLayer {
    /** @brief Kind public API. */
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
    /** @brief Owned format fields not represented above; codec excludes authoritative fields. */
    eve::Value::Object extensions;
};

/**
 * @brief Format-neutral, editable top-down level model.
 *
 * Coordinates are world pixels, tile GID 0 means empty, and orientation is one
 * of orthogonal, isometric, staggered or hexagonal. Unknown developer data can
 * be preserved in string properties at document, layer and object scope.
 */
class EVENGINE_API_DOMAINS LevelDocument {
public:
    /** @brief Level document. */
    LevelDocument(int width = 1, int height = 1, float tileWidth = 32.f, float tileHeight = 32.f);

    // std::vector<LevelLayer> holds a unique_ptr member, so the implicit copy
    // operations were already deleted; a class-level dllexport would nonetheless
    // instantiate std::vector<LevelLayer>::operator= and hard-error (C2280).
    // Spell the four out so the export surface stays defined; semantics unchanged.
    LevelDocument(const LevelDocument&)            = delete;
    LevelDocument& operator=(const LevelDocument&) = delete;
    /** @brief Level document. */
    LevelDocument(LevelDocument&&)                 = default;
    /** @brief Operator =. */
    LevelDocument& operator=(LevelDocument&&)      = default;

    /** @brief Changes map dimensions and resizes all tile layers. */
    void               resize(int width, int height);
    /** @brief Returns the width. */
    int                getWidth() const { return width_; }
    /** @brief Returns the height. */
    int                getHeight() const { return height_; }
    /** @brief Returns the tile width. */
    float              getTileWidth() const { return tileWidth_; }
    /** @brief Returns the tile height. */
    float              getTileHeight() const { return tileHeight_; }
    /** @brief Sets the tile size. */
    void               setTileSize(float width, float height);
    /** @brief Sets the orientation. */
    void               setOrientation(const std::string& orientation);
    /** @brief Returns the orientation. */
    const std::string& getOrientation() const { return orientation_; }

    /** @brief Adds tile layer. */
    int                       addTileLayer(const std::string& name);
    /** @brief Adds object layer. */
    int                       addObjectLayer(const std::string& name);
    /** @brief Removes layer. */
    [[nodiscard]] LevelChange removeLayer(int index);
    /** @brief Moves layer. */
    [[nodiscard]] LevelChange moveLayer(int from, int to);
    /** @brief Returns the layer count. */
    int                       getLayerCount() const { return static_cast<int>(layers_.size()); }
    /** @brief Returns the layer name. */
    const std::string&        getLayerName(int index) const;
    /** @brief Sets the layer name. */
    void                      setLayerName(int index, const std::string& name);
    /** @brief Returns the layer kind. */
    const std::string         getLayerKind(int index) const;

    /** @brief Returns the tile layer. */
    [[nodiscard]] eve::OptionalRef<TileBuffer>       getTileLayer(int index);
    /** @brief Returns the tile layer. */
    [[nodiscard]] eve::OptionalRef<const TileBuffer> getTileLayer(int index) const;
    /** @brief Layer. */
    [[nodiscard]] eve::OptionalRef<LevelLayer>       layer(int index);
    /** @brief Layer. */
    [[nodiscard]] eve::OptionalRef<const LevelLayer> layer(int index) const;

    /** @brief Adds object. */
    int addObject(int layerIndex, const std::string& type, float x, float y);
    /** @brief Returns the object count. */
    int getObjectCount(int layerIndex) const;

    /** @brief Object. */
    [[nodiscard]] eve::OptionalRef<LevelObject>       object(int layerIndex, int objectIndex);
    /** @brief Object. */
    [[nodiscard]] eve::OptionalRef<const LevelObject> object(int layerIndex, int objectIndex) const;
    /** @brief Removes object. */
    [[nodiscard]] LevelChange                         removeObject(int layerIndex, int objectIndex);

    /** @brief Sets the property. */
    void        setProperty(const std::string& key, const std::string& value);
    /** @brief Returns the property. */
    std::string getProperty(const std::string& key, const std::string& fallback = {}) const;

    std::unordered_map<std::string, std::string>&       properties() { return properties_; }
    const std::unordered_map<std::string, std::string>& properties() const { return properties_; }

    /** @brief Layers. */
    std::vector<LevelLayer>&       layers() { return layers_; }
    /** @brief Layers. */
    const std::vector<LevelLayer>& layers() const { return layers_; }

private:
    friend class LevelFormatCodec;
    eve::Value::Object extensions_;
    std::string nextId(const char* prefix);
    int         width_, height_;
    float       tileWidth_, tileHeight_;
    std::string orientation_ = "orthogonal";
    unsigned    nextId_      = 1;

    std::vector<LevelLayer>                      layers_;
    std::unordered_map<std::string, std::string> properties_;
};

}  // namespace eve::level_editing
