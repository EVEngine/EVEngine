#include "procgen/heightmap/TerrainSplatmap.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <unordered_set>
#include <vector>

#include "procgen/heightmap/Heightmap.h"
#include "procgen/heightmap/TerrainRasterInternal.h"
#include "procgen/heightmap/TerrainMultiTile.h"
#include "procgen/heightmap/TerrainStamp.h"

namespace eve::procgen {
namespace {
template <class T> Result<T> invalid(const char* message) {
    return Result<T>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, message));
}

float fade(float value) { return value * value * value * (value * (value * 6.f - 15.f) + 10.f); }
float seamHash(int x, int y) {
    std::uint32_t value = std::uint32_t(x) * 0x8da6b343u ^ std::uint32_t(y) * 0xd8163841u;
    value ^= value >> 13;
    value *= 0xcb1ab31fu;
    value ^= value >> 16;
    return float(value & 0x00ffffffu) / float(0x00ffffffu);
}
float seamNoise(float x, float y) {
    const int x0 = static_cast<int>(std::floor(x)), y0 = static_cast<int>(std::floor(y));
    const float tx = fade(x - float(x0)), ty = fade(y - float(y0));
    const float a = std::lerp(seamHash(x0, y0), seamHash(x0 + 1, y0), tx);
    const float b = std::lerp(seamHash(x0, y0 + 1), seamHash(x0 + 1, y0 + 1), tx);
    return std::lerp(a, b, ty);
}
}
struct TerrainSplatmap::Impl {
    int width = 0, height = 0, layers = 0, lastChanged = 0;
    std::vector<float> weights;
    std::size_t index(int layer, int x, int y) const {
        return (static_cast<std::size_t>(layer) * height + y) * width + x;
    }
};
struct GtsHeightBlendSet::Impl {
    struct Layer { Heightmap height; float contrast, brightness, increase; };
    std::vector<Layer> layers;
};
GtsHeightBlendSet::GtsHeightBlendSet() : impl_(std::make_unique<Impl>()) {}
GtsHeightBlendSet::~GtsHeightBlendSet() = default;
GtsHeightBlendSet::GtsHeightBlendSet(GtsHeightBlendSet&&) noexcept = default;
GtsHeightBlendSet& GtsHeightBlendSet::operator=(GtsHeightBlendSet&&) noexcept = default;
Result<int> GtsHeightBlendSet::addLayer(const Heightmap& height, float contrast, float brightness, float increase) {
    using namespace raster_detail;
    if (!validRaster(height) || !std::isfinite(contrast) || contrast < 0 || !std::isfinite(brightness) ||
        !std::isfinite(increase)) return invalid<int>("terrain.gtsHeightBlend: finite layer transform required");
    if (!impl_) impl_ = std::make_unique<Impl>();
    if (impl_->layers.size() >= 8) return invalid<int>("terrain.gtsHeightBlend: at most eight layers supported");
    if (!impl_->layers.empty() && (height.getWidth() != impl_->layers[0].height.getWidth() ||
                                   height.getHeight() != impl_->layers[0].height.getHeight()))
        return invalid<int>("terrain.gtsHeightBlend: all height rasters must match");
    impl_->layers.push_back({height, contrast, brightness, increase});
    return Result<int>::success(static_cast<int>(impl_->layers.size()));
}
int GtsHeightBlendSet::getLayerCount() const noexcept { return impl_ ? static_cast<int>(impl_->layers.size()) : 0; }
TerrainSplatmap::TerrainSplatmap() : impl_(std::make_unique<Impl>()) {}
TerrainSplatmap::~TerrainSplatmap() = default;
TerrainSplatmap::TerrainSplatmap(TerrainSplatmap&&) noexcept = default;
TerrainSplatmap& TerrainSplatmap::operator=(TerrainSplatmap&&) noexcept = default;
TerrainSplatmap::TerrainSplatmap(const TerrainSplatmap& other)
    : impl_(other.impl_ ? std::make_unique<Impl>(*other.impl_) : std::make_unique<Impl>()) {}
TerrainSplatmap& TerrainSplatmap::operator=(const TerrainSplatmap& other) {
    if (this != &other) {
        if (!impl_) impl_ = std::make_unique<Impl>();
        *impl_ = other.impl_ ? *other.impl_ : Impl{};
    }
    return *this;
}
Result<int> TerrainSplatmap::initialize(int width, int height, int layers, int defaultLayer) {
    if (width <= 0 || height <= 0 || layers <= 0 || defaultLayer < 0 || defaultLayer >= layers)
        return invalid<int>("terrain.splatmap: positive topology and valid default layer required");
    const auto texels = std::uint64_t(width) * std::uint64_t(height);
    if (texels > std::numeric_limits<std::size_t>::max() / std::uint64_t(layers) ||
        texels * std::uint64_t(layers) > std::numeric_limits<int>::max())
        return invalid<int>("terrain.splatmap: topology exceeds supported size");
    if (!impl_) impl_ = std::make_unique<Impl>();
    Impl next;
    next.width = width; next.height = height; next.layers = layers;
    next.weights.assign(static_cast<std::size_t>(texels) * layers, 0);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) next.weights[next.index(defaultLayer, x, y)] = 1;
    *impl_ = std::move(next);
    return Result<int>::success(static_cast<int>(texels));
}
Result<float> TerrainSplatmap::sample(int layer, int x, int y) const {
    if (!impl_ || impl_->layers <= 0) return invalid<float>("terrain.splatmap: initialize before sampling");
    if (layer < 0 || layer >= impl_->layers || x < 0 || x >= impl_->width || y < 0 || y >= impl_->height)
        return invalid<float>("terrain.splatmap: sample index out of range");
    return Result<float>::success(impl_->weights[impl_->index(layer, x, y)]);
}

Result<int> TerrainSplatmap::copyLayer(int layer, Heightmap& output) const {
    if (!impl_ || impl_->width <= 0 || layer < 0 || layer >= impl_->layers || output.getWidth() != impl_->width ||
        output.getHeight() != impl_->height)
        return invalid<int>("terrain.splat.copyLayer: initialized source, valid layer and matching output required");
    std::vector<float> candidate(size_t(impl_->width) * impl_->height);
    for (int y = 0; y < impl_->height; ++y)
        for (int x = 0; x < impl_->width; ++x)
            candidate[size_t(y) * impl_->width + x] = impl_->weights[impl_->index(layer, x, y)];
    int changed = 0;
    for (size_t i = 0; i < candidate.size(); ++i) changed += candidate[i] != output.data()[i];
    output.data().swap(candidate);
    return Result<int>::success(changed);
}
int TerrainSplatmap::getLastChangedSamples() const noexcept { return impl_ ? impl_->lastChanged : 0; }
int TerrainSplatmap::getWidth() const noexcept { return impl_ ? impl_->width : 0; }
int TerrainSplatmap::getHeight() const noexcept { return impl_ ? impl_->height : 0; }
int TerrainSplatmap::getLayerCount() const noexcept { return impl_ ? impl_->layers : 0; }

Result<int> alignTerrainSplatTextures(TerrainSplatmap& terrainA, TerrainSplatmap& terrainB,
                                      const TerrainTextureAlignSettings& settings) {
    if (&terrainA == &terrainB || !terrainA.impl_ || !terrainB.impl_ || terrainA.impl_->layers <= 0 ||
        terrainB.impl_->layers <= 0 || terrainA.impl_->width <= 0 || terrainA.impl_->height <= 0 ||
        terrainB.impl_->width <= 0 || terrainB.impl_->height <= 0 ||
        terrainA.impl_->width != terrainA.impl_->height || terrainB.impl_->width != terrainB.impl_->height ||
        !std::isfinite(settings.terrainAOriginX) || !std::isfinite(settings.terrainAOriginZ) ||
        !std::isfinite(settings.terrainAWidth) || !std::isfinite(settings.terrainADepth) ||
        !std::isfinite(settings.terrainBOriginX) || !std::isfinite(settings.terrainBOriginZ) ||
        !std::isfinite(settings.terrainBWidth) || !std::isfinite(settings.terrainBDepth) ||
        !std::isfinite(settings.blendStrength) || !std::isfinite(settings.adjacencyTolerance) ||
        settings.terrainAWidth <= 0 || settings.terrainADepth <= 0 || settings.terrainBWidth <= 0 ||
        settings.terrainBDepth <= 0 || settings.blendStrength < 0 || settings.blendStrength > 1 ||
        settings.blendWidth <= 0 || settings.adjacencyTolerance <= 0)
        return invalid<int>("terrain.textureAlign: distinct initialized square maps and valid finite settings required");

    enum class Edge { None, Left, Right, Top, Bottom };
    Edge edgeA = Edge::None, edgeB = Edge::None;
    const double aMinX = settings.terrainAOriginX, aMaxX = aMinX + settings.terrainAWidth;
    const double aMinZ = settings.terrainAOriginZ, aMaxZ = aMinZ + settings.terrainADepth;
    const double bMinX = settings.terrainBOriginX, bMaxX = bMinX + settings.terrainBWidth;
    const double bMinZ = settings.terrainBOriginZ, bMaxZ = bMinZ + settings.terrainBDepth;
    const auto near = [&](double a, double b) { return std::abs(a - b) < settings.adjacencyTolerance; };
    const bool xOverlaps = (bMaxX > aMinX && bMinX < aMaxX) || near(bMaxX, aMinX) ||
                           near(bMinX, aMaxX) || near(aMinX, bMinX) || near(aMaxX, bMaxX);
    if (xOverlaps && near(aMaxZ, bMinZ)) edgeA = Edge::Right, edgeB = Edge::Left;
    else if (xOverlaps && near(aMinZ, bMaxZ)) edgeA = Edge::Left, edgeB = Edge::Right;
    else {
        const bool zOverlaps = (bMaxZ > aMinZ && bMinZ < aMaxZ) || near(bMaxZ, aMinZ) ||
                               near(bMinZ, aMaxZ) || near(aMinZ, bMinZ) || near(aMaxZ, bMaxZ);
        if (zOverlaps && near(aMaxX, bMinX)) edgeA = Edge::Top, edgeB = Edge::Bottom;
        else if (zOverlaps && near(aMinX, bMaxX)) edgeA = Edge::Bottom, edgeB = Edge::Top;
    }
    if (edgeA == Edge::None)
        return invalid<int>("terrain.textureAlign: terrain bounds are not adjacent");

    auto nextA = *terrainA.impl_, nextB = *terrainB.impl_;
    const int resolution = std::min(nextA.width, nextB.width);
    const int layers = std::min(nextA.layers, nextB.layers);
    const bool horizontal = edgeA == Edge::Left || edgeA == Edge::Right;
    int changedA = 0, changedB = 0;
    for (int i = 0; i < settings.blendWidth; ++i) {
        const float t = std::clamp(float(i) / float(settings.blendWidth), 0.f, 1.f);
        const float blend = settings.blendStrength * (1.f - t * t * (3.f - 2.f * t));
        for (int j = 0; j < resolution; ++j) {
            const int depthA = std::min(i, resolution - 1), depthB = std::min(i, resolution - 1);
            const int xA = horizontal ? (edgeA == Edge::Left ? depthA : resolution - 1 - depthA) : j;
            const int yA = horizontal ? j : (edgeA == Edge::Bottom ? depthA : resolution - 1 - depthA);
            const int xB = horizontal ? (edgeB == Edge::Left ? depthB : resolution - 1 - depthB) : j;
            const int yB = horizontal ? j : (edgeB == Edge::Bottom ? depthB : resolution - 1 - depthB);
            const float noise = seamNoise(horizontal ? j * 0.1f : i * 0.1f,
                                           horizontal ? i * 0.1f : j * 0.1f) * 0.5f + 0.5f;
            const float weightA = noise * blend, weightB = (1.f - noise) * blend;
            std::vector<float> oldA(static_cast<std::size_t>(layers));
            std::vector<float> oldB(static_cast<std::size_t>(layers));
            for (int layer = 0; layer < layers; ++layer) {
                oldA[static_cast<std::size_t>(layer)] = terrainA.impl_->weights[terrainA.impl_->index(layer, xA, yA)];
                oldB[static_cast<std::size_t>(layer)] = terrainB.impl_->weights[terrainB.impl_->index(layer, xB, yB)];
            }
            float sumA = 0, sumB = 0;
            for (int layer = 0; layer < layers; ++layer) {
                const float a = oldA[static_cast<std::size_t>(layer)], b = oldB[static_cast<std::size_t>(layer)];
                nextA.weights[nextA.index(layer, xA, yA)] = std::lerp(a, b, weightA);
                nextB.weights[nextB.index(layer, xB, yB)] = std::lerp(b, a, weightB);
                sumA += nextA.weights[nextA.index(layer, xA, yA)];
                sumB += nextB.weights[nextB.index(layer, xB, yB)];
            }
            if (sumA > 0 && sumB > 0) for (int layer = 0; layer < layers; ++layer) {
                nextA.weights[nextA.index(layer, xA, yA)] /= sumA;
                nextB.weights[nextB.index(layer, xB, yB)] /= sumB;
            }
            bool aChanged = false, bChanged = false;
            for (int layer = 0; layer < layers; ++layer) {
                aChanged |= nextA.weights[nextA.index(layer, xA, yA)] != oldA[static_cast<std::size_t>(layer)];
                bChanged |= nextB.weights[nextB.index(layer, xB, yB)] != oldB[static_cast<std::size_t>(layer)];
            }
            changedA += aChanged;
            changedB += bChanged;
        }
    }
    nextA.lastChanged = changedA;
    nextB.lastChanged = changedB;
    *terrainA.impl_ = std::move(nextA);
    *terrainB.impl_ = std::move(nextB);
    return Result<int>::success(changedA + changedB);
}

Result<int> paintTerrainSplatLayer(TerrainSplatmap& target, const Heightmap& paint, int targetLayer) {
    using namespace raster_detail;
    if (!target.impl_ || target.impl_->layers <= 0 || !validRaster(paint) ||
        paint.getWidth() != target.impl_->width || paint.getHeight() != target.impl_->height ||
        targetLayer < 0 || targetLayer >= target.impl_->layers ||
        !std::all_of(paint.data().begin(), paint.data().end(), [](float value) {
            return std::isfinite(value) && value >= 0 && value <= 1;
        }) || (target.impl_->layers == 1 && std::any_of(paint.data().begin(), paint.data().end(),
                                                       [](float value) { return value != 1; })))
        return invalid<int>("terrain.paintSplat: initialized matching topology, layer and normalized paint required");
    auto next = *target.impl_;
    int changed = 0;
    for (int y = 0; y < next.height; ++y) {
        for (int x = 0; x < next.width; ++x) {
            const auto targetIndex = next.index(targetLayer, x, y);
            const float oldTarget = next.weights[targetIndex], replacement = paint.height(x, y);
            if (oldTarget != replacement) ++changed;
            const double oldOther = 1.0 - oldTarget;
            if (oldOther > 1e-7) {
                const double factor = (1.0 - replacement) / oldOther;
                for (int layer = 0; layer < next.layers; ++layer)
                    if (layer != targetLayer) next.weights[next.index(layer, x, y)] =
                        static_cast<float>(next.weights[next.index(layer, x, y)] * factor);
            } else {
                const float share = next.layers > 1 ? (1 - replacement) / float(next.layers - 1) : 0;
                for (int layer = 0; layer < next.layers; ++layer)
                    if (layer != targetLayer) next.weights[next.index(layer, x, y)] = share;
            }
            next.weights[targetIndex] = replacement;
        }
    }
    next.lastChanged = changed;
    *target.impl_ = std::move(next);
    return Result<int>::success(changed);
}

Result<int> applyGtsHeightBlend(TerrainSplatmap& output, const TerrainSplatmap& input,
                                const GtsHeightBlendSet& heights, float blendFactor) {
    if (!input.impl_ || input.impl_->layers <= 0 || !heights.impl_ ||
        heights.impl_->layers.size() != static_cast<std::size_t>(input.impl_->layers) ||
        !std::isfinite(blendFactor) || blendFactor < 0 || blendFactor > 1)
        return invalid<int>("terrain.gtsHeightBlend: initialized splat, matching heights and normalized factor required");
    for (const auto& layer : heights.impl_->layers)
        if (layer.height.getWidth() != input.impl_->width || layer.height.getHeight() != input.impl_->height)
            return invalid<int>("terrain.gtsHeightBlend: topology mismatch");
    auto next = *input.impl_;
    int changed = 0;
    std::vector<float> weighted(static_cast<std::size_t>(next.layers));
    for (int y = 0; y < next.height; ++y) for (int x = 0; x < next.width; ++x) {
        float maxHeight = 0;
        for (int layer = 0; layer < next.layers; ++layer) {
            const auto& source = heights.impl_->layers[static_cast<std::size_t>(layer)];
            const float transformed = std::pow(std::abs(source.height.height(x, y)), source.contrast) *
                                      source.brightness + source.increase;
            weighted[static_cast<std::size_t>(layer)] = transformed * input.impl_->weights[input.impl_->index(layer, x, y)];
            maxHeight = std::max(maxHeight, weighted[static_cast<std::size_t>(layer)]);
        }
        float sum = 0;
        const float transition = std::max(blendFactor, 1e-5F);
        for (int layer = 0; layer < next.layers; ++layer) {
            float& value = weighted[static_cast<std::size_t>(layer)];
            value = std::max(0.0F, value + transition - maxHeight);
            value = (value + 1e-6F) * input.impl_->weights[input.impl_->index(layer, x, y)];
            sum += value;
        }
        sum = std::max(sum, 1e-6F);
        bool texelChanged = false;
        for (int layer = 0; layer < next.layers; ++layer) {
            const auto index = next.index(layer, x, y);
            const float value = weighted[static_cast<std::size_t>(layer)] / sum;
            texelChanged |= value != next.weights[index]; next.weights[index] = value;
        }
        changed += texelChanged;
    }
    next.lastChanged = changed;
    if (!output.impl_) output.impl_ = std::make_unique<TerrainSplatmap::Impl>();
    *output.impl_ = std::move(next);
    return Result<int>::success(changed);
}

Result<TerrainMultiTileReport> paintTerrainSplatLayerMultiTile(
    const std::vector<TerrainSplatTile>& tiles, const Heightmap& operationPaint, int targetLayer,
    const TerrainStampSettings& operationSettings, bool worldMapOperation,
    const std::vector<std::string>& validTerrainNames) {
    using namespace raster_detail;
    if (tiles.empty() || !validRaster(operationPaint) ||
        !std::all_of(operationPaint.data().begin(), operationPaint.data().end(), [](float value) {
            return std::isfinite(value) && value >= 0 && value <= 1;
        }))
        return invalid<TerrainMultiTileReport>("terrain.paintSplat.multitile: tiles and normalized paint required");
    std::vector<TerrainOperationTile> descriptors;
    std::unordered_set<TerrainSplatmap*> owners;
    descriptors.reserve(tiles.size());
    for (const auto& tile : tiles) {
        if (!tile.splatmap || !tile.splatmap->impl_ || tile.splatmap->impl_->layers <= 0 ||
            !owners.insert(tile.splatmap).second || targetLayer < 0 || targetLayer >= tile.splatmap->impl_->layers)
            return invalid<TerrainMultiTileReport>(
                "terrain.paintSplat.multitile: distinct initialized owners and common target layer required");
        descriptors.push_back({tile.name, tile.originX, tile.originZ, tile.width, tile.depth,
                               tile.splatmap->impl_->width, tile.splatmap->impl_->height, tile.worldMap});
    }
    auto mapped = mapTerrainOperationMultiTile(descriptors, operationSettings, TerrainOperationDomain::Texture,
                                               worldMapOperation, validTerrainNames);
    if (!mapped.ok()) return mapped;
    auto report = std::move(mapped.value());
    if (operationPaint.getWidth() != report.operationWidth || operationPaint.getHeight() != report.operationHeight)
        return invalid<TerrainMultiTileReport>(
            "terrain.paintSplat.multitile: paint dimensions must match the shared operation window");
    struct Candidate { TerrainSplatmap* target; TerrainSplatmap::Impl value; };
    std::vector<Candidate> candidates;
    candidates.reserve(report.mappings.size());
    for (const auto& mapping : report.mappings) {
        const auto tile = std::find_if(tiles.begin(), tiles.end(),
                                       [&](const auto& value) { return value.name == mapping.terrainName; });
        if (tile == tiles.end())
            return Result<TerrainMultiTileReport>::failure(Diagnostic::error(
                DiagnosticCode::InvariantViolation, "terrain.paintSplat.multitile: mapped terrain was not supplied"));
        if (tile->splatmap->impl_->layers == 1) {
            for (int y = 0; y < mapping.height; ++y)
                for (int x = 0; x < mapping.width; ++x)
                    if (operationPaint.height(mapping.operationX + x, mapping.operationY + y) != 1)
                        return invalid<TerrainMultiTileReport>(
                            "terrain.paintSplat.multitile: a selected single-layer splatmap must remain one");
        }
        auto next = *tile->splatmap->impl_;
        int changed = 0;
        for (int y = 0; y < mapping.height; ++y) {
            for (int x = 0; x < mapping.width; ++x) {
                const int localX = mapping.localX + x, localY = mapping.localY + y;
                const float replacement = operationPaint.height(mapping.operationX + x, mapping.operationY + y);
                const auto index = next.index(targetLayer, localX, localY);
                const float oldTarget = next.weights[index];
                if (oldTarget != replacement) ++changed;
                const double oldOther = 1.0 - oldTarget;
                if (oldOther > 1e-7) {
                    const double factor = (1.0 - replacement) / oldOther;
                    for (int layer = 0; layer < next.layers; ++layer)
                        if (layer != targetLayer) next.weights[next.index(layer, localX, localY)] =
                            static_cast<float>(next.weights[next.index(layer, localX, localY)] * factor);
                } else {
                    const float share = next.layers > 1 ? (1 - replacement) / float(next.layers - 1) : 0;
                    for (int layer = 0; layer < next.layers; ++layer)
                        if (layer != targetLayer) next.weights[next.index(layer, localX, localY)] = share;
                }
                next.weights[index] = replacement;
            }
        }
        next.lastChanged = changed;
        if (report.changedSamples > std::numeric_limits<int>::max() - changed)
            return invalid<TerrainMultiTileReport>("terrain.paintSplat.multitile: changed texel count exceeds range");
        report.changedSamples += changed;
        candidates.push_back({tile->splatmap, std::move(next)});
    }
    for (auto& candidate : candidates) *candidate.target->impl_ = std::move(candidate.value);
    return Result<TerrainMultiTileReport>::success(std::move(report));
}

struct TerrainMultiSplatWorkspace::Impl {
    struct Tile {
        std::string name;
        TerrainSplatmap splatmap;
        double originX = 0, originZ = 0, width = 1, depth = 1;
        bool worldMap = false;
    };
    struct Snapshot {
        std::vector<TerrainSplatmap> maps;
        TerrainMultiTileReport report;
    };
    std::vector<Tile> tiles;
    TerrainMultiTileReport last;
    std::vector<Snapshot> history;
    int cursor = 0;
    Snapshot snapshot() const {
        Snapshot value;
        value.maps.reserve(tiles.size());
        for (const auto& tile : tiles) value.maps.push_back(tile.splatmap);
        value.report = last;
        return value;
    }
    void restore(const Snapshot& value) {
        for (std::size_t i = 0; i < tiles.size(); ++i) tiles[i].splatmap = value.maps[i];
        last = value.report;
    }
};

TerrainMultiSplatWorkspace::TerrainMultiSplatWorkspace() : impl_(std::make_unique<Impl>()) {}
TerrainMultiSplatWorkspace::~TerrainMultiSplatWorkspace() = default;
TerrainMultiSplatWorkspace::TerrainMultiSplatWorkspace(TerrainMultiSplatWorkspace&&) noexcept = default;
TerrainMultiSplatWorkspace& TerrainMultiSplatWorkspace::operator=(TerrainMultiSplatWorkspace&&) noexcept = default;

Result<int> TerrainMultiSplatWorkspace::addTile(const std::string& name, const TerrainSplatmap& splatmap,
                                                 double originX, double originZ, double width, double depth,
                                                 bool worldMap) {
    if (!impl_ || name.empty() || splatmap.getWidth() <= 0 || splatmap.getHeight() <= 0)
        return invalid<int>("terrain.splat.workspace: initialized named tile required");
    if (std::any_of(impl_->tiles.begin(), impl_->tiles.end(), [&](const auto& tile) { return tile.name == name; }))
        return invalid<int>("terrain.splat.workspace: duplicate tile name");
    if (impl_->history.size() > 1)
        return invalid<int>("terrain.splat.workspace: topology is fixed after painting");
    auto candidate = std::make_unique<Impl>(*impl_);
    candidate->tiles.push_back({name, splatmap, originX, originZ, width, depth, worldMap});
    std::vector<TerrainOperationTile> descriptors;
    for (const auto& tile : candidate->tiles)
        descriptors.push_back({tile.name, tile.originX, tile.originZ, tile.width, tile.depth,
                               tile.splatmap.getWidth(), tile.splatmap.getHeight(), tile.worldMap});
    TerrainStampSettings bounds;
    bounds.centerX = originX + width * 0.5;
    bounds.centerZ = originZ + depth * 0.5;
    bounds.width = width;
    bounds.depth = depth;
    auto checked = mapTerrainOperationMultiTile(descriptors, bounds, TerrainOperationDomain::Texture, worldMap, {name});
    if (!checked.ok()) return Result<int>::failure(checked.status());
    candidate->history.clear();
    candidate->history.push_back(candidate->snapshot());
    candidate->cursor = 0;
    impl_.swap(candidate);
    return Result<int>::success(static_cast<int>(impl_->tiles.size()));
}

Result<int> TerrainMultiSplatWorkspace::paint(const Heightmap& operationPaint, int targetLayer,
                                               const TerrainStampSettings& operationSettings,
                                               bool worldMapOperation) {
    if (!impl_) return invalid<int>("terrain.splat.workspace: moved-from workspace");
    auto candidate = std::make_unique<Impl>(*impl_);
    std::vector<TerrainSplatTile> descriptors;
    for (auto& tile : candidate->tiles)
        descriptors.push_back({tile.name, &tile.splatmap, tile.originX, tile.originZ, tile.width, tile.depth,
                               tile.worldMap});
    auto painted = paintTerrainSplatLayerMultiTile(descriptors, operationPaint, targetLayer, operationSettings,
                                                   worldMapOperation);
    if (!painted.ok()) return Result<int>::failure(painted.status());
    candidate->last = std::move(painted.value());
    candidate->history.resize(static_cast<std::size_t>(candidate->cursor + 1));
    candidate->history.push_back(candidate->snapshot());
    ++candidate->cursor;
    const int changed = candidate->last.changedSamples;
    impl_.swap(candidate);
    return Result<int>::success(changed);
}

Result<int> TerrainMultiSplatWorkspace::copyTile(const std::string& name, TerrainSplatmap& output) const {
    if (!impl_) return invalid<int>("terrain.splat.workspace: moved-from workspace");
    const auto found = std::find_if(impl_->tiles.begin(), impl_->tiles.end(),
                                    [&](const auto& tile) { return tile.name == name; });
    if (found == impl_->tiles.end()) return invalid<int>("terrain.splat.workspace: tile not found");
    TerrainSplatmap copy = found->splatmap;
    output = std::move(copy);
    return Result<int>::success(output.getWidth() * output.getHeight());
}
Result<int> TerrainMultiSplatWorkspace::undo() {
    if (!impl_ || impl_->cursor <= 0) return invalid<int>("terrain.splat.workspace: no undo snapshot");
    --impl_->cursor;
    impl_->restore(impl_->history[static_cast<std::size_t>(impl_->cursor)]);
    return Result<int>::success(impl_->cursor);
}
Result<int> TerrainMultiSplatWorkspace::redo() {
    if (!impl_ || impl_->cursor + 1 >= static_cast<int>(impl_->history.size()))
        return invalid<int>("terrain.splat.workspace: no redo snapshot");
    ++impl_->cursor;
    impl_->restore(impl_->history[static_cast<std::size_t>(impl_->cursor)]);
    return Result<int>::success(impl_->cursor);
}
int TerrainMultiSplatWorkspace::getTileCount() const noexcept { return impl_ ? static_cast<int>(impl_->tiles.size()) : 0; }
int TerrainMultiSplatWorkspace::getLastChangedSamples() const noexcept { return impl_ ? impl_->last.changedSamples : 0; }
int TerrainMultiSplatWorkspace::getLastAffectedTiles() const noexcept { return impl_ ? impl_->last.affectedTiles : 0; }
int TerrainMultiSplatWorkspace::getOperationCount() const noexcept {
    return impl_ ? std::max(0, static_cast<int>(impl_->history.size()) - 1) : 0;
}
int TerrainMultiSplatWorkspace::getAppliedCount() const noexcept { return impl_ ? impl_->cursor : 0; }
}  // namespace eve::procgen
