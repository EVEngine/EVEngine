#pragma once
// Private scene ownership/cache; no mutable domain positions or retained Map/layer pointers.
#include "common/Revision.h"
#include "map/PreparedMapLoad.h"
#include "map/TileLayer.h"
#include "map/spawn/MapSpawnLoad.h"
namespace eve::map_spawn {
struct MapSpawnSession::NavigationBinding {
    enum class Phase { Ready, Closing, Closed };
    struct Observation {
        std::reference_wrapper<map::TileLayer> layer;  // Borrowed only within observe/capture call scope.
        ecs::EntityHandle                      handle;
        Revision                               revision;
    };
    map::MapLease                         owner;
    std::shared_ptr<const MapSpawnAssets> assets;
    std::size_t                           index;
    map::MapGridGeometry                  geometry;
    std::string                           asset;
    std::optional<ecs::EntityHandle>      lastLayer;
    Revision                              lastRevision;
    Phase                                 phase     = Phase::Ready;
    bool                                  advancing = false;
    NavigationBinding(map::MapLease lease, std::size_t layer, map::MapGridGeometry grid, std::string path)
        : owner(std::move(lease)), index(layer), geometry(grid), asset(std::move(path)) {}
    [[nodiscard]] Result<Observation> observe(map::Map& target) const;
};
}  // namespace eve::map_spawn
