#pragma once

#include <cstdint>
#include <map>
#include <string>

namespace eve::map {

/**
 * @brief A named, typed rectangle object from a Tiled objectgroup layer.
 * Used by Map::setObjects / getObject* for gameplay spawn points, triggers, etc.
 */
struct MapObject {
    std::string name;
    std::string type;
    /** @brief Top-left corner in pixels. */
    float x = 0.f;
    float y = 0.f;
    /** @brief Extent in pixels. */
    float width = 0.f;
    float height = 0.f;
    /** @brief Optional tileset GID (tile objects); 0 = none. */
    uint32_t gid = 0;
    /** @brief Owning canonical text projection of Tiled custom properties, sorted by name. */
    std::map<std::string, std::string> properties;
};

}  // namespace eve::map
