#include "procgen/road/RoadTerrainWorkspaceInternal.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <set>

namespace eve::procgen::road {
namespace {
template <class T>
Result<T> invalid(const char* message) {
    return Result<T>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, message, "road.terrain"));
}
bool rasterValid(const Heightmap& map, bool normalized) {
    if (map.getWidth() < 1 || map.getHeight() < 1 || map.getWidth() > 4096 || map.getHeight() > 4096 ||
        map.data().size() != static_cast<std::size_t>(map.getWidth()) * map.getHeight())
        return false;
    for (float v : map.data())
        if (!std::isfinite(v) || (normalized && (v < 0.f || v > 1.f))) return false;
    return true;
}
std::size_t storage(const std::vector<RoadTerrainTile>& tiles) {
    std::size_t count = 0;
    for (const auto& tile : tiles) {
        count += tile.heights.data().size() + tile.holes.data().size() + tile.trees.size() * 8u;
        for (const auto& layer : tile.splatLayers) count += layer.data().size();
        for (const auto& layer : tile.details) count += layer.density.size();
    }
    return count;
}
struct Coverage {
    Heightmap ground, tunnel;
};
Result<Coverage> coverage(const MeshBuild& mesh, const RoadTerrainTile& tile, int width, int height,
                          const RoadTerrainWriteOptions& options, std::size_t& work) {
    Coverage          out{Heightmap(width, height), Heightmap(width, height)};
    std::vector<bool> protectedCells(static_cast<std::size_t>(width) * height, false);
    const float       dx = tile.sizeX / static_cast<float>(std::max(1, width - 1)),
                dz       = tile.sizeZ / static_cast<float>(std::max(1, height - 1));
    for (int t = 0; t < mesh.getIndexCount() / 3; ++t) {
        const auto group = mesh.getGroupName(mesh.getTriangleGroup(t));
        if (group != "asphalt" && group != "asphalt.bridge" && group != "asphalt.tunnel" &&
            group != "asphalt.tunnel.portal")
            continue;
        const int   a = mesh.getIndex(t * 3), b = mesh.getIndex(t * 3 + 1), c = mesh.getIndex(t * 3 + 2);
        const float ax = mesh.getPositionX(a), az = mesh.getPositionZ(a), bx = mesh.getPositionX(b),
                    bz = mesh.getPositionZ(b), cx = mesh.getPositionX(c), cz = mesh.getPositionZ(c);
        const float denominator = (bz - cz) * (ax - cx) + (cx - bx) * (az - cz);
        if (std::fabs(denominator) < 1e-7f) continue;
        const float minX = std::clamp((std::min({ax, bx, cx}) - tile.originX) / dx, 0.f, static_cast<float>(width - 1));
        const float maxX = std::clamp((std::max({ax, bx, cx}) - tile.originX) / dx, 0.f, static_cast<float>(width - 1));
        const float minZ =
            std::clamp((std::min({az, bz, cz}) - tile.originZ) / dz, 0.f, static_cast<float>(height - 1));
        const float maxZ =
            std::clamp((std::max({az, bz, cz}) - tile.originZ) / dz, 0.f, static_cast<float>(height - 1));
        for (int z = static_cast<int>(std::floor(minZ)); z <= static_cast<int>(std::ceil(maxZ)); ++z)
            for (int x = static_cast<int>(std::floor(minX)); x <= static_cast<int>(std::ceil(maxX)); ++x) {
                if (++work > options.maximumWork) return invalid<Coverage>("channel rasterization exceeds work budget");
                const float tx = width == 1 ? .5f : static_cast<float>(x) / static_cast<float>(width - 1);
                const float tz = height == 1 ? .5f : static_cast<float>(z) / static_cast<float>(height - 1);
                const float wx = tile.originX + tx * tile.sizeX, wz = tile.originZ + tz * tile.sizeZ;
                const float u = ((bz - cz) * (wx - cx) + (cx - bx) * (wz - cz)) / denominator;
                const float v = ((cz - az) * (wx - cx) + (ax - cx) * (wz - cz)) / denominator, w = 1.f - u - v;
                // Portal boundaries come from sampled arc interpolation. Use a world-space
                // 2 mm edge tolerance so a grid point on a portal border survives a road split.
                const bool  portal = group == "asphalt.tunnel.portal";
                const float tu     = portal ? .002f * std::hypot(bx - cx, bz - cz) / std::fabs(denominator) : 1e-4f;
                const float tv     = portal ? .002f * std::hypot(cx - ax, cz - az) / std::fabs(denominator) : 1e-4f;
                const float tw     = portal ? .002f * std::hypot(ax - bx, az - bz) / std::fabs(denominator) : 1e-4f;
                if (u < -tu || v < -tv || w < -tw) continue;
                const auto index = static_cast<std::size_t>(z) * width + x;
                if (group == "asphalt.tunnel" || group == "asphalt.tunnel.portal") {
                    if (!options.tunnelHolesAtPortalsOnly || group == "asphalt.tunnel.portal")
                        out.tunnel.data()[index] = 1.f;
                    protectedCells[index] = true;
                } else if (group == "asphalt.bridge")
                    protectedCells[index] = true;
                else {
                    const float roadY = u * mesh.getPositionY(a) + v * mesh.getPositionY(b) + w * mesh.getPositionY(c);
                    const float terrainY =
                        tile.originY + tile.heights.sampleBilinear(tx * (tile.heights.getWidth() - 1),
                                                                   tz * (tile.heights.getHeight() - 1)) *
                                           tile.heightScale;
                    if (std::fabs(roadY - terrainY) <= options.maxVerticalDelta)
                        out.ground.data()[index] = 1.f;
                    else
                        protectedCells[index] = true;
                }
            }
    }
    const auto original = out.ground.data();
    const int  radiusX  = static_cast<int>(std::ceil(options.blendDistance / dx)),
              radiusZ   = static_cast<int>(std::ceil(options.blendDistance / dz));
    if (radiusX > 256 || radiusZ > 256) return invalid<Coverage>("channel blend radius exceeds 256 cells");
    for (int z = 0; z < height; ++z)
        for (int x = 0; x < width; ++x) {
            if (original[static_cast<std::size_t>(z) * width + x] == 0.f) continue;
            for (int oz = -radiusZ; oz <= radiusZ; ++oz)
                for (int ox = -radiusX; ox <= radiusX; ++ox) {
                    if (++work > options.maximumWork) return invalid<Coverage>("channel blending exceeds work budget");
                    const int px = x + ox, pz = z + oz;
                    if (px < 0 || pz < 0 || px >= width || pz >= height) continue;
                    const auto index = static_cast<std::size_t>(pz) * width + px;
                    if (protectedCells[index] && original[index] == 0.f) continue;
                    const float distance = std::hypot(static_cast<float>(ox) * dx, static_cast<float>(oz) * dz);
                    const float weight =
                        options.blendDistance == 0.f ? 1.f : std::max(0.f, 1.f - distance / options.blendDistance);
                    out.ground.data()[index] = std::max(out.ground.data()[index], weight);
                }
        }
    return Result<Coverage>::success(std::move(out));
}
}  // namespace
namespace detail {
Result<void> validateTerrainTiles(const std::vector<RoadTerrainTile>& tiles) {
    if (tiles.empty() || tiles.size() > 256u) return invalid<void>("terrain requires 1..256 tiles");
    std::set<std::string> tileIds;
    std::size_t           samples = 0;
    for (const auto& tile : tiles) {
        if (tile.id.empty() || tile.id.size() > 256u || !tileIds.insert(tile.id).second ||
            !std::isfinite(tile.originX) || !std::isfinite(tile.originY) || !std::isfinite(tile.originZ) ||
            !std::isfinite(tile.sizeX) || !std::isfinite(tile.sizeZ) || !std::isfinite(tile.heightScale) ||
            tile.sizeX <= 0.f || tile.sizeZ <= 0.f || tile.heightScale <= 0.f || !rasterValid(tile.heights, false) ||
            tile.heights.getWidth() < 2 || tile.heights.getHeight() < 2 || !rasterValid(tile.holes, true) ||
            std::fabs(tile.sizeX / (tile.heights.getWidth() - 1) - tile.sizeZ / (tile.heights.getHeight() - 1)) >
                1e-4f ||
            tile.splatLayers.size() > 64u || tile.details.size() > 256u || tile.trees.size() > 1000000u)
            return invalid<void>("invalid terrain tile identity, geometry or channel topology");
        samples += tile.heights.data().size() + tile.holes.data().size() + tile.trees.size() * 8u;
        for (float v : tile.holes.data())
            if (v != 0.f && v != 1.f) return invalid<void>("holes must be binary");
        for (const auto& layer : tile.splatLayers) {
            if (!rasterValid(layer, true) || layer.getWidth() != tile.splatLayers.front().getWidth() ||
                layer.getHeight() != tile.splatLayers.front().getHeight())
                return invalid<void>("invalid splat topology");
            samples += layer.data().size();
        }
        if (!tile.splatLayers.empty())
            for (std::size_t i = 0; i < tile.splatLayers.front().data().size(); ++i) {
                float sum = 0.f;
                for (const auto& layer : tile.splatLayers) sum += layer.data()[i];
                if (std::fabs(sum - 1.f) > 1e-4f) return invalid<void>("splat weights must sum to one");
            }
        std::set<std::string> resources, trees;
        for (const auto& d : tile.details) {
            if (d.resourceId.empty() || d.resourceId.size() > 256u || !resources.insert(d.resourceId).second ||
                d.width < 1 || d.height < 1 || d.width > 4096 || d.height > 4096 ||
                d.density.size() != static_cast<std::size_t>(d.width) * d.height)
                return invalid<void>("invalid detail identity or topology");
            for (int v : d.density)
                if (v < 0) return invalid<void>("negative detail density");
            samples += d.density.size();
        }
        for (const auto& tree : tile.trees) {
            if (tree.id.empty() || tree.id.size() > 256u || tree.resourceId.empty() || tree.resourceId.size() > 256u ||
                !trees.insert(tree.id).second || !std::isfinite(tree.x) || !std::isfinite(tree.y) ||
                !std::isfinite(tree.z) || !std::isfinite(tree.yaw) || !std::isfinite(tree.width) ||
                !std::isfinite(tree.height) || tree.width <= 0.f || tree.height <= 0.f)
                return invalid<void>("invalid tree instance");
        }
        if (samples > 16u * 1024u * 1024u) return invalid<void>("terrain channel storage exceeds budget");
    }
    return Result<void>::success();
}
}  // namespace detail
RoadTerrainWorkspace::RoadTerrainWorkspace() : impl_(std::make_unique<Impl>()) {}
RoadTerrainWorkspace::~RoadTerrainWorkspace()                                          = default;
RoadTerrainWorkspace::RoadTerrainWorkspace(RoadTerrainWorkspace&&) noexcept            = default;
RoadTerrainWorkspace& RoadTerrainWorkspace::operator=(RoadTerrainWorkspace&&) noexcept = default;
Result<int>           RoadTerrainWorkspace::initialize(std::vector<RoadTerrainTile> tiles) {
    auto valid = detail::validateTerrainTiles(tiles);
    if (!valid.ok()) return Result<int>::failure(valid.status());
    auto      candidate = std::make_unique<Impl>();
    const int count     = static_cast<int>(tiles.size());
    candidate->history.push_back(std::move(tiles));
    impl_.swap(candidate);
    return Result<int>::success(count);
}
Result<int> RoadTerrainWorkspace::apply(const MeshBuild& mesh, const RoadTerrainWriteOptions& options) {
    const RoadTerrainWrite write{std::cref(mesh), options};
    return applyBatch(std::span<const RoadTerrainWrite>(&write, 1), options.maximumWork);
}
Result<int> RoadTerrainWorkspace::applyBatch(std::span<const RoadTerrainWrite> writes, std::size_t maximumWork) {
    if (!impl_ || impl_->history.empty() || writes.empty() || writes.size() > 4096u || maximumWork == 0u)
        return invalid<int>("invalid terrain batch or uninitialized owner");
    for (const auto& write : writes) {
        const auto& options = write.options;
        if (!std::isfinite(options.blendDistance) || options.blendDistance < 0.f ||
            !std::isfinite(options.maxVerticalDelta) || options.maxVerticalDelta < 0.f || options.splatLayer < -1 ||
            options.maximumWork == 0u || !std::isfinite(options.splatOpacity) || options.splatOpacity < 0.f ||
            options.splatOpacity > 1.f)
            return invalid<int>("invalid terrain write options");
    }
    std::size_t historyStorage = storage(impl_->history[impl_->cursor]);
    for (std::size_t i = 0; i <= impl_->cursor; ++i) historyStorage += storage(impl_->history[i]);
    if (historyStorage > 16u * 1024u * 1024u || impl_->cursor >= 4095u)
        return invalid<int>("terrain undo history exceeds save budget");
    auto         frame   = impl_->history[impl_->cursor];
    std::size_t  work    = 0;
    std::int64_t changed = 0;
    for (const auto& write : writes) {
        if (work >= maximumWork) return invalid<int>("terrain batch exceeds shared work budget");
        auto options        = write.options;
        options.maximumWork = work + std::min(maximumWork - work, options.maximumWork);
        const auto& mesh    = write.mesh.get();
        for (auto& tile : frame) {
            if (options.splatLayer >= static_cast<int>(tile.splatLayers.size()))
                return invalid<int>("selected splat layer absent on tile");
            RoadTerrainConformOptions conform;
            conform.originX          = tile.originX;
            conform.originY          = tile.originY;
            conform.originZ          = tile.originZ;
            conform.cellSize         = tile.sizeX / (tile.heights.getWidth() - 1);
            conform.heightScale      = tile.heightScale;
            conform.blendDistance    = options.blendDistance;
            conform.maxVerticalDelta = options.maxVerticalDelta;
            // Validate mesh through the canonical conformance path before any channel indexes its streams.
            if (work >= options.maximumWork) return invalid<int>("terrain write exceeds shared work budget");
            conform.maximumWork = options.maximumWork - work;
            const auto baseline = tile.heights;
            auto       height   = conformHeightmapToRoadWithReceipt(tile.heights, mesh, conform);
            if (!height.ok()) return Result<int>::failure(height.status());
            work += height.value().workPerformed;
            changed += static_cast<int>(height.value().sampleIndices.size());
            // Channel eligibility uses the original terrain height, so height writes cannot change bridge
            // classification.
            RoadTerrainTile mapping = tile;
            mapping.heights         = baseline;
            if (options.splatLayer >= 0) {
                auto mask = coverage(mesh, mapping, tile.splatLayers.front().getWidth(),
                                     tile.splatLayers.front().getHeight(), options, work);
                if (!mask.ok()) return Result<int>::failure(mask.status());
                for (std::size_t i = 0; i < mask.value().ground.data().size(); ++i) {
                    const float weight = mask.value().ground.data()[i] * options.splatOpacity;
                    const auto  target = static_cast<std::size_t>(options.splatLayer);
                    if (weight <= tile.splatLayers[target].data()[i]) continue;
                    float remainder = 0.f;
                    for (std::size_t l = 0; l < tile.splatLayers.size(); ++l)
                        if (l != target) remainder += tile.splatLayers[l].data()[i];
                    for (std::size_t l = 0; l < tile.splatLayers.size(); ++l) {
                        auto&       value = tile.splatLayers[l].data()[i];
                        const float next  = l == target ? (remainder > 0.f ? weight : 1.f)
                                                        : (remainder > 0.f ? value * ((1.f - weight) / remainder) : 0.f);
                        changed += value != next;
                        value = next;
                    }
                }
            }
            if (options.clearDetails)
                for (auto& d : tile.details) {
                    auto mask = coverage(mesh, mapping, d.width, d.height, options, work);
                    if (!mask.ok()) return Result<int>::failure(mask.status());
                    for (std::size_t i = 0; i < d.density.size(); ++i)
                        if (mask.value().ground.data()[i] > 0.f ||
                            (options.cutTunnelHoles && mask.value().tunnel.data()[i] > 0.f)) {
                            changed += d.density[i] != 0;
                            d.density[i] = 0;
                        }
                }
            if (options.clearTrees && !tile.trees.empty()) {
                auto mask = coverage(mesh, mapping, tile.heights.getWidth(), tile.heights.getHeight(), options, work);
                if (!mask.ok()) return Result<int>::failure(mask.status());
                const auto oldCount = tile.trees.size();
                std::erase_if(tile.trees, [&](const RoadTerrainTree& tree) {
                    const float x = (tree.x - tile.originX) / tile.sizeX, z = (tree.z - tile.originZ) / tile.sizeZ;
                    if (x < 0.f || z < 0.f || x > 1.f || z > 1.f) return false;
                    const float ground = mask.value().ground.sampleBilinear(x * (mask.value().ground.getWidth() - 1),
                                                                            z * (mask.value().ground.getHeight() - 1));
                    const float tunnel = mask.value().tunnel.sampleBilinear(x * (mask.value().tunnel.getWidth() - 1),
                                                                            z * (mask.value().tunnel.getHeight() - 1));
                    return ground > 0.f || (options.cutTunnelHoles && tunnel > 0.f);
                });
                changed += static_cast<int>(oldCount - tile.trees.size());
            }
            if (options.cutTunnelHoles) {
                auto mask = coverage(mesh, mapping, tile.holes.getWidth(), tile.holes.getHeight(), options, work);
                if (!mask.ok()) return Result<int>::failure(mask.status());
                for (std::size_t i = 0; i < tile.holes.data().size(); ++i)
                    if (mask.value().tunnel.data()[i] > 0.f && mask.value().ground.data()[i] == 0.f) {
                        changed += tile.holes.data()[i] != 1.f;
                        tile.holes.data()[i] = 1.f;
                    }
            }
        }
    }
    if (changed > std::numeric_limits<int>::max())
        return invalid<int>("terrain batch change count exceeds result range");
    auto candidate = std::make_unique<Impl>(*impl_);
    candidate->history.resize(candidate->cursor + 1u);
    candidate->history.push_back(std::move(frame));
    ++candidate->cursor;
    impl_.swap(candidate);
    return Result<int>::success(static_cast<int>(changed));
}
Result<int> RoadTerrainWorkspace::undo() {
    if (!impl_ || impl_->history.empty() || impl_->cursor == 0u) return invalid<int>("no terrain undo frame");
    --impl_->cursor;
    return Result<int>::success(static_cast<int>(impl_->cursor));
}
Result<int> RoadTerrainWorkspace::redo() {
    if (!impl_ || impl_->cursor + 1u >= impl_->history.size()) return invalid<int>("no terrain redo frame");
    ++impl_->cursor;
    return Result<int>::success(static_cast<int>(impl_->cursor));
}
Result<RoadTerrainTile> RoadTerrainWorkspace::copyTile(const std::string& id) const {
    if (impl_ && !impl_->history.empty())
        for (const auto& tile : impl_->history[impl_->cursor])
            if (tile.id == id) return Result<RoadTerrainTile>::success(tile);
    return invalid<RoadTerrainTile>("unknown terrain tile");
}
}  // namespace eve::procgen::road
