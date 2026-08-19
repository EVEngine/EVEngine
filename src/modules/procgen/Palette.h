#pragma once

#include "procgen/Grid2D.h"
#include "map/TileLayer.h"

#include <cstdint>
#include <string>
#include <unordered_map>

namespace eve::procgen {

/** @brief Named palette: semantic name -> tile GID. Unmapped semantics become GID 0. */
class PaletteTable {
public:
    void setGid(const std::string &palette, const std::string &semantic, int gid);
    int  getGid(const std::string &palette, const std::string &semantic) const;
    int  getGid(const std::string &palette, uint32_t semanticId) const;
    bool applyToLayer(const Grid2D &grid, const std::string &palette, map::TileLayer *layer,
                      std::string *error) const;

private:
    std::unordered_map<std::string, std::unordered_map<std::string, int>> palettes_;
};

}  // namespace eve::procgen
