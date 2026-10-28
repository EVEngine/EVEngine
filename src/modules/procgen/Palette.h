#pragma once

#include "procgen/Grid2D.h"

#include <cstdint>
#include <string>
#include <unordered_map>

namespace eve::map {
class TileLayer;
}

namespace eve::procgen {

/** @brief Named palette: semantic name -> tile GID. Unmapped semantics become GID 0. */
class PaletteTable {
public:
    /** @brief Sets the gid. */
    void setGid(const std::string &palette, const std::string &semantic, int gid);
    /** @brief Returns the gid. */
    int  getGid(const std::string &palette, const std::string &semantic) const;
    /** @brief Returns the gid. */
    int  getGid(const std::string &palette, uint32_t semanticId) const;
    /** @brief Applies to layer. */
    bool applyToLayer(const Grid2D &grid, const std::string &palette, map::TileLayer *layer,
                      std::string *error) const;

private:
    std::unordered_map<std::string, std::unordered_map<std::string, int>> palettes_;
};

}  // namespace eve::procgen
