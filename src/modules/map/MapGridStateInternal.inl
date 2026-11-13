#pragma once
#include "map/MapGridState.h"
#include "map/TileLayer.h"
namespace eve::map {
struct MapGridState::Impl {
    struct Profile {
        int           gid = 0, footprintW = 1, footprintH = 1;
        bool          walkable = true, opaque = false;
        float         cost  = 1;
        std::uint8_t  enter = 255, exit = 255;
        std::uint32_t flags = 0;
    };
    struct Layer {
        std::string                                    path;
        TileLayer::Config                              config;  // entity is always null in owned checkpoint facts.
        std::vector<std::uint32_t>                     gids;
        std::vector<int>                               terrain;
        std::uint64_t                                  revision = 0;
        std::vector<Profile>                           profiles;
        std::vector<TileLayer::Tileset::TerrainRule>   rules;
        std::vector<TileLayer::Tileset::TerrainFamily> families;
        std::vector<TileLayer::Tileset::CustomData>    custom;
    };
    std::vector<Layer> layers;
};
}  // namespace eve::map
