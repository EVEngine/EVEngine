#include "procgen/heightmap/TerrainDetailLayerInternal.h"
#include "procgen/heightmap/TerrainSplatmapInternal.h"
#include "procgen/heightmap/TerrainWorldWorkspaceInternal.h"
#include "procgen/road/RoadTerrainWorkspace.h"

#include <algorithm>
#include <limits>
#include <set>

namespace eve::procgen {
Result<int> TerrainWorldWorkspace::conformRoad(const MeshBuild& mesh, const road::RoadTerrainWriteOptions& options) {
    const road::RoadTerrainWrite write{std::cref(mesh), options};
    return conformRoadBatch(std::span<const road::RoadTerrainWrite>(&write, 1), options.maximumWork);
}
Result<int> TerrainWorldWorkspace::conformRoadBatch(std::span<const road::RoadTerrainWrite> writes,
                                                    std::size_t                             maximumWork) {
    if (!impl_ || impl_->tiles.empty() || impl_->runStatus == TerrainWorldRunStatus::Pending)
        return Result<int>::failure(Diagnostic::error(
            DiagnosticCode::PreconditionViolation, "terrain.world.road: created world without pending spawn required"));
    // The native world remains the sole observable owner. This projection is owned only by the private transaction.
    std::vector<road::RoadTerrainTile> input;
    for (const auto& tile : impl_->tiles) {
        road::RoadTerrainTile data;
        data.id      = tile.name;
        data.originX = static_cast<float>(tile.originX);
        data.originZ = static_cast<float>(tile.originZ);
        data.originY = impl_->settings.originY;
        data.sizeX = data.sizeZ = impl_->settings.tileSize;
        data.heightScale        = impl_->settings.tileHeight;
        data.heights            = tile.heights;
        data.holes              = tile.holes;
        for (int l = 0; l < tile.splat.getLayerCount(); ++l) {
            Heightmap layer(tile.splat.getWidth(), tile.splat.getHeight());
            auto      copied = tile.splat.copyLayer(l, layer);
            if (!copied.ok()) return Result<int>::failure(copied.status());
            data.splatLayers.push_back(std::move(layer));
        }
        for (const auto& [id, counts] : tile.detail.impl_->resources)
            data.details.push_back({std::to_string(id), tile.detail.getWidth(), tile.detail.getHeight(), counts});
        for (int row = 0; row < tile.trees.getCount(); ++row) {
            const auto& point = tile.trees.points()[static_cast<std::size_t>(row)];
            data.trees.push_back(
                {std::to_string(row), "native.tree", point.x, point.y, point.z, point.yaw, point.scaleX, point.scaleY});
        }
        input.push_back(std::move(data));
    }
    road::RoadTerrainWorkspace transaction;
    auto                       initialized = transaction.initialize(std::move(input));
    if (!initialized.ok()) return Result<int>::failure(initialized.status());
    auto applied = transaction.applyBatch(writes, maximumWork);
    if (!applied.ok()) return Result<int>::failure(applied.status());
    auto candidate = std::make_unique<Impl>(*impl_);
    int  affected  = 0;
    for (auto& tile : candidate->tiles) {
        auto output = transaction.copyTile(tile.name);
        if (!output.ok()) return Result<int>::failure(output.status());
        auto data    = std::move(output).takeValue();
        bool changed = tile.heights.data() != data.heights.data() || tile.holes.data() != data.holes.data();
        tile.heights = std::move(data.heights);
        tile.holes   = std::move(data.holes);
        tile.splat.impl_->lastChanged = 0;
        for (int y = 0; y < tile.splat.getHeight(); ++y)
            for (int x = 0; x < tile.splat.getWidth(); ++x) {
                bool texelChanged = false;
                for (int l = 0; l < tile.splat.getLayerCount(); ++l) {
                    auto&       value = tile.splat.impl_->weights[tile.splat.impl_->index(l, x, y)];
                    const float next  = data.splatLayers[static_cast<std::size_t>(l)].height(x, y);
                    texelChanged      = texelChanged || value != next;
                    value             = next;
                }
                tile.splat.impl_->lastChanged += texelChanged;
            }
        changed = changed || tile.splat.impl_->lastChanged != 0;
        for (auto& [id, counts] : tile.detail.impl_->resources) {
            const auto layer = std::find_if(data.details.begin(), data.details.end(),
                                            [&](const auto& d) { return d.resourceId == std::to_string(id); });
            if (layer == data.details.end())
                return Result<int>::failure(
                    Diagnostic::error(DiagnosticCode::InvariantViolation,
                                      "terrain.world.road: projection lost detail resource identity"));
            changed = changed || counts != layer->density;
            counts  = std::move(layer->density);
        }
        auto totals = tile.detail.impl_->rebuildTotals();
        if (!totals.ok()) return Result<int>::failure(totals.status());
        std::set<std::string> keptRows;
        for (const auto& tree : data.trees) keptRows.insert(tree.id);
        if (keptRows.size() != static_cast<std::size_t>(tile.trees.getCount())) {
            PointSet kept;
            kept.mutableDataAttributes() = tile.trees.dataAttributes();
            for (int row = 0; row < tile.trees.getCount(); ++row)
                if (keptRows.contains(std::to_string(row))) {
                    auto appended = kept.appendPointFrom(tile.trees, static_cast<std::size_t>(row));
                    if (!appended.ok()) return Result<int>::failure(appended.status());
                }
            tile.trees = std::move(kept);
            changed    = true;
        }
        affected += changed;
    }
    candidate->last                = {};
    candidate->last.changedSamples = applied.value();
    candidate->last.affectedTiles  = affected;
    candidate->record();
    impl_.swap(candidate);
    return Result<int>::success(applied.value());
}
Result<Heightmap> TerrainWorldWorkspace::copyHoles(int index) const {
    if (!impl_ || index < 0 || static_cast<std::size_t>(index) >= impl_->tiles.size())
        return Result<Heightmap>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "terrain.world: invalid hole tile index"));
    return Result<Heightmap>::success(impl_->tiles[static_cast<std::size_t>(index)].holes);
}
}  // namespace eve::procgen
