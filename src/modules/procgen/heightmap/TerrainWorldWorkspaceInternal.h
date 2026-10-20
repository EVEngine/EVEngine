#pragma once
#include "procgen/PointSet.h"
#include "procgen/heightmap/Heightmap.h"
#include "procgen/heightmap/TerrainDetailLayer.h"
#include "procgen/heightmap/TerrainMultiTile.h"
#include "procgen/heightmap/TerrainSpawnPlan.h"
#include "procgen/heightmap/TerrainSplatmap.h"
#include "procgen/heightmap/TerrainWorldWorkspace.h"
namespace eve::procgen {
struct TerrainWorldWorkspace::Impl {
    struct Tile {
        std::string        name;
        Heightmap          heights, holes;
        TerrainSplatmap    splat;
        TerrainDetailLayer detail;
        PointSet           trees, objects, probes;
        double             originX = 0, originZ = 0;
    };
    struct Snapshot {
        std::vector<Heightmap>          heights, holes;
        std::vector<TerrainSplatmap>    splats;
        std::vector<TerrainDetailLayer> details;
        std::vector<PointSet>           trees, objects, probes;
        TerrainMultiTileReport          report;
    };
    TerrainWorldCreationSettings settings;
    std::vector<Tile>            tiles;
    TerrainMultiTileReport       last;
    std::vector<Snapshot>        history;
    int                          cursor    = 0;
    TerrainWorldRunStatus        runStatus = TerrainWorldRunStatus::Idle;
    std::unique_ptr<Impl>        runCandidate;
    TerrainSpawnPlan             runPlan;
    std::size_t                  runNext = 0;
    TerrainMultiTileReport       runReport;

    Impl() = default;
    Impl(const Impl& other) : settings(other.settings), last(other.last), history(other.history), cursor(other.cursor) {
        tiles.reserve(other.tiles.size());
        for (const auto& source : other.tiles) {
            Tile tile;
            tile.name    = source.name;
            tile.heights = source.heights;
            tile.holes   = source.holes;
            tile.splat   = source.splat;
            tile.detail  = source.detail;
            tile.trees   = source.trees;
            tile.objects = source.objects;
            tile.probes  = source.probes;
            tile.originX = source.originX;
            tile.originZ = source.originZ;
            tiles.push_back(std::move(tile));
        }
    }
    Snapshot snapshot() const {
        Snapshot result;
        for (const auto& tile : tiles) {
            result.heights.push_back(tile.heights);
            result.holes.push_back(tile.holes);
            result.splats.push_back(tile.splat);
            result.details.push_back(tile.detail);
            result.trees.push_back(tile.trees);
            result.objects.push_back(tile.objects);
            result.probes.push_back(tile.probes);
        }
        result.report = last;
        return result;
    }
    void restore(const Snapshot& value) {
        for (std::size_t i = 0; i < tiles.size(); ++i) {
            tiles[i].heights = value.heights[i];
            tiles[i].holes   = value.holes[i];
            tiles[i].splat   = value.splats[i];
            tiles[i].detail  = value.details[i];
            tiles[i].trees   = value.trees[i];
            tiles[i].objects = value.objects[i];
            tiles[i].probes  = value.probes[i];
        }
        last = value.report;
    }
    void record() {
        history.resize(static_cast<std::size_t>(cursor + 1));
        history.push_back(snapshot());
        ++cursor;
    }
};

}  // namespace eve::procgen
