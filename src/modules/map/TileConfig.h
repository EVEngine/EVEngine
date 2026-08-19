#pragma once

#include "map/TileLayer.h"
#include "map/MapObject.h"

#include <string>
#include <vector>

namespace eve::data {
class JsonDocument;
}

namespace eve::map {

/**
 * Apply map/layer JSON onto an existing TileLayer (Config + Tiles + Tileset + Draw).
 * Multi-layer documents only apply the first tile layer when called on one entity.
 */
bool applyConfigDocument(TileLayer *layer, data::JsonDocument *doc);

/** Parse JSON text and apply onto one layer. */
bool applyConfigText(TileLayer *layer, const std::string &json, std::string *error = nullptr);

/**
 * Read path via Filesystem, apply onto layer, bind Resource.path + modtime for hot reload.
 * If the document has multiple tile layers, only the first is applied to `layer`.
 */
bool loadConfigFile(TileLayer *layer, const std::string &path, std::string *error = nullptr);

bool reloadConfigFile(TileLayer *layer, std::string *error = nullptr);

/**
 * Load a (possibly multi-layer) map JSON. Creates one TileLayer per tile layer.
 * Optionally fills `objects` from objectgroup layers.
 * Returns created layers (empty on failure). All share the same Resource.path for reload.
 */
std::vector<TileLayer *> loadMapFile(const std::string &path, std::string *error = nullptr);
std::vector<TileLayer *> loadMapFile(const std::string &path, std::vector<MapObject> *objects,
                                     std::string *error = nullptr);

/** Parse map JSON text (no filesystem). Same semantics as loadMapFile. */
std::vector<TileLayer *> loadMapText(const std::string &json, std::vector<MapObject> *objects,
                                     std::string *error = nullptr);

}  // namespace eve::map
