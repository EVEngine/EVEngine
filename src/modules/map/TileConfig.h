#pragma once

#include "common/Result.h"
#include "map/MapObject.h"
#include "map/TileLayer.h"

#include <string>
#include <vector>

namespace eve::data {
class JsonDocument;
}

namespace eve::map {

/** @brief Transactional RPG Maker MV/MZ import receipt. */
struct RpgMakerImportReceipt {
    std::vector<TileLayer *> layers;
    TileLayer               *navigationLayer = nullptr;
    TileLayer               *shadowLayer     = nullptr;
    std::string              sourceEngine;
    int                      tilesetId = 0;
};

/**
 * @brief Apply map/layer JSON onto an existing TileLayer (Config + Tiles + Tileset + Draw).
 * Multi-layer documents only apply the first tile layer when called on one entity.
 */
bool applyConfigDocument(TileLayer *layer, data::JsonDocument *doc);

/** @brief Parse JSON text and apply onto one layer. */
bool applyConfigText(TileLayer *layer, const std::string &json, std::string *error = nullptr);

/**
 * @brief Read path via Filesystem, apply onto layer, bind Resource.path + modtime for hot reload.
 * If the document has multiple tile layers, only the first is applied to `layer`.
 */
bool loadConfigFile(TileLayer *layer, const std::string &path, std::string *error = nullptr);

bool reloadConfigFile(TileLayer *layer, std::string *error = nullptr);

/**
 * @brief Load a composable TileSet manifest onto an existing layer.
 *
 * The manifest only changes atlas/visual metadata; map dimensions and GIDs stay intact.
 */
bool loadTilesetManifestFile(TileLayer *layer, const std::string &path,
                             std::string *error = nullptr);

/**
 * @brief Load a (possibly multi-layer) map JSON. Creates one TileLayer per tile layer.
 * Optionally fills `objects` from objectgroup layers.
 * Returns created layers (empty on failure). All share the same Resource.path for reload.
 */
std::vector<TileLayer *> loadMapFile(const std::string &path, std::string *error = nullptr);
std::vector<TileLayer *> loadMapFile(const std::string &path, std::vector<MapObject> *objects,
                                     std::string *error = nullptr);

/** @brief Parse map JSON text (no filesystem). Same semantics as loadMapFile. */
std::vector<TileLayer *> loadMapText(const std::string &json, std::vector<MapObject> *objects,
                                     std::string *error = nullptr);

/**
 * @brief Imports RPG Maker MV/MZ MapXXX.json plus Tilesets.json without modifying the project.
 * @ownership Returned layer entities are owned by the ECS world, as with loadMapFile.
 * @thread Main-thread affine and non-reentrant.
 */
[[nodiscard]] eve::Result<RpgMakerImportReceipt> importRpgMakerMap(const std::string &mapPath,
                                                                   const std::string &tilesetsPath,
                                                                   const std::string &sourceEngine = "RPG Maker MV/MZ");

/** @brief Decodes one MV/MZ tile id into normal or quarter-tile atlas projections. */
[[nodiscard]] TileLayer::Tileset::Visual decodeRpgMakerTileVisual(int tileId);

}  // namespace eve::map
