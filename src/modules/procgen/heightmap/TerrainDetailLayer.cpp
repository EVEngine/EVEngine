#include "procgen/heightmap/TerrainDetailLayer.h"
#include <algorithm>
#include <climits>
#include <cmath>
#include <limits>
#include <map>
#include <unordered_set>
#include <vector>
#include "procgen/heightmap/Heightmap.h"
#include "procgen/heightmap/TerrainMultiTile.h"
#include "procgen/heightmap/TerrainStamp.h"

namespace eve::procgen {
namespace {
Result<int> invalid() {
    return Result<int>::failure(
        Diagnostic::error(DiagnosticCode::InvalidArgument,
                          "terrain.detail: initialized matching finite input and valid settings required"));
}
class DetailThinningStream {
    uint64_t a_, b_;

public:
    explicit DetailThinningStream(int32_t seed) {
        const auto bits = static_cast<uint32_t>(seed == 0 ? 1 : seed);
        a_              = uint64_t(181353) * bits;
        b_              = uint64_t(7) * bits;
    }
    float next() {
        auto x = a_, y = b_;
        a_ = y;
        x ^= x << 23;
        x ^= x >> 17;
        x ^= y ^ (y >> 26);
        b_ = x;
        return static_cast<float>(x + y) / static_cast<float>(UINT64_MAX);
    }
};
float inverseLerp(float a, float b, float value) {
    return a == b ? 0.0F : std::clamp((value - a) / (b - a), 0.0F, 1.0F);
}
int nearestEven(float value) {
    const double lower = std::floor(double(value)), fraction = double(value) - lower;
    return int(lower + (fraction > 0.5 || (fraction == 0.5 && std::fmod(lower, 2.0) != 0)));
}
bool validSettings(const TerrainDetailSettings& settings) {
    return std::isfinite(settings.minimumFitness) && settings.minimumFitness >= 0 && settings.minimumFitness <= 1 &&
           std::isfinite(settings.fadeStart) && settings.fadeStart >= 0 && settings.fadeStart <= 1 &&
           std::isfinite(settings.density) && settings.density >= 0 && double(settings.density) < double(INT_MAX) &&
           (settings.mode == TerrainDetailMode::Replace || settings.mode == TerrainDetailMode::Add ||
            settings.mode == TerrainDetailMode::Remove);
}
}  // namespace
struct TerrainDetailLayer::Impl {
    int              width = 0, height = 0;
    std::vector<int> counts;
    std::map<std::uint64_t, std::vector<int>> resources;
    Result<int> rebuildTotals() {
        std::vector<int> totals(counts.size(), 0);
        for (const auto& [namespaceId, resourceCounts] : resources) {
            (void)namespaceId;
            if (resourceCounts.size() != totals.size()) return invalid();
            for (std::size_t i = 0; i < totals.size(); ++i) {
                if (resourceCounts[i] > INT_MAX - totals[i]) return invalid();
                totals[i] += resourceCounts[i];
            }
        }
        counts.swap(totals);
        return Result<int>::success(static_cast<int>(counts.size()));
    }
};
TerrainDetailLayer::TerrainDetailLayer()                                         = default;
TerrainDetailLayer::~TerrainDetailLayer()                                        = default;
TerrainDetailLayer::TerrainDetailLayer(TerrainDetailLayer&&) noexcept            = default;
TerrainDetailLayer& TerrainDetailLayer::operator=(TerrainDetailLayer&&) noexcept = default;
TerrainDetailLayer::TerrainDetailLayer(const TerrainDetailLayer& other)
    : impl_(other.impl_ ? std::make_unique<Impl>(*other.impl_) : nullptr) {}
TerrainDetailLayer& TerrainDetailLayer::operator=(const TerrainDetailLayer& other) {
    if (this != &other) impl_ = other.impl_ ? std::make_unique<Impl>(*other.impl_) : nullptr;
    return *this;
}
int                 TerrainDetailLayer::getWidth() const noexcept { return impl_ ? impl_->width : 0; }
int                 TerrainDetailLayer::getHeight() const noexcept { return impl_ ? impl_->height : 0; }
Result<int>         TerrainDetailLayer::reset(int width, int height, int count) {
    if (width <= 0 || height <= 0 || width > INT_MAX / height || count < 0) return invalid();
    auto candidate    = std::make_unique<Impl>();
    candidate->width  = width;
    candidate->height = height;
    candidate->counts.assign(size_t(width) * height, count);
    candidate->resources.emplace(0, candidate->counts);
    impl_.swap(candidate);
    return Result<int>::success(width * height);
}
Result<int> TerrainDetailLayer::sample(int x, int z) const {
    if (!impl_ || x < 0 || z < 0 || x >= impl_->width || z >= impl_->height) return invalid();
    return Result<int>::success(impl_->counts[size_t(z) * impl_->width + x]);
}
Result<int> TerrainDetailLayer::sampleResource(std::uint64_t namespaceId, int x, int z) const {
    if (!impl_ || x < 0 || z < 0 || x >= impl_->width || z >= impl_->height) return invalid();
    const auto found = impl_->resources.find(namespaceId);
    if (found == impl_->resources.end()) return Result<int>::success(0);
    return Result<int>::success(found->second[size_t(z) * impl_->width + x]);
}
Result<int> TerrainDetailLayer::clearResource(std::uint64_t namespaceId) {
    if (!impl_) return invalid();
    auto candidate = std::make_unique<Impl>(*impl_);
    const auto found = candidate->resources.find(namespaceId);
    if (found == candidate->resources.end()) return Result<int>::success(0);
    int changed = 0;
    for (int count : found->second) changed += count != 0;
    candidate->resources.erase(found);
    auto rebuilt = candidate->rebuildTotals();
    if (!rebuilt.ok()) return rebuilt;
    impl_.swap(candidate);
    return Result<int>::success(changed);
}
Result<int> TerrainDetailLayer::apply(const Heightmap& fitness, const TerrainDetailSettings& settings, int32_t seed) {
    if (!impl_ || fitness.getWidth() != impl_->width || fitness.getHeight() != impl_->height ||
        fitness.data().size() != impl_->counts.size() || !validSettings(settings))
        return invalid();
    for (float value : fitness.data())
        if (!std::isfinite(value)) return invalid();
    auto next = std::make_unique<Impl>(*impl_);
    auto [resource, inserted] = next->resources.try_emplace(settings.namespaceId, impl_->counts.size(), 0);
    (void)inserted;
    auto& target = resource->second;
    if (settings.mode == TerrainDetailMode::Replace) std::fill(target.begin(), target.end(), 0);
    DetailThinningStream random(seed);
    const float          sign = settings.mode == TerrainDetailMode::Remove ? -1.0F : 1.0F;
    for (int x = 0; x < impl_->width; ++x)
        for (int z = 0; z < impl_->height; ++z) {
            const size_t index = size_t(z) * impl_->width + x;
            const float  value = fitness.data()[index];
            if (value <= settings.minimumFitness) continue;
            if (value < settings.fadeStart &&
                random.next() > inverseLerp(settings.minimumFitness, settings.fadeStart, value))
                continue;
            const float amount = inverseLerp(settings.minimumFitness, 1.0F, value) * settings.density;
            target[index] = nearestEven(std::clamp(float(target[index]) + sign * amount, 0.0F, settings.density));
        }
    auto rebuilt = next->rebuildTotals();
    if (!rebuilt.ok()) return rebuilt;
    int changed = 0;
    for (size_t i = 0; i < next->counts.size(); ++i) changed += next->counts[i] != impl_->counts[i];
    impl_.swap(next);
    return Result<int>::success(changed);
}

Result<TerrainMultiTileReport> applyTerrainDetailMultiTile(
    const std::vector<TerrainDetailTile>& tiles, const Heightmap& operationFitness,
    const TerrainDetailSettings& settings, const TerrainStampSettings& operationSettings, int32_t seed,
    bool worldMapOperation, const std::vector<std::string>& validTerrainNames) {
    if (tiles.empty() || !validSettings(settings))
        return Result<TerrainMultiTileReport>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "terrain.detail.multitile: valid tiles and settings required"));
    std::vector<TerrainOperationTile> operationTiles;
    operationTiles.reserve(tiles.size());
    std::unordered_set<TerrainDetailLayer*> owners;
    for (const auto& tile : tiles) {
        if (!tile.layer || !tile.layer->impl_ || !owners.insert(tile.layer).second)
            return Result<TerrainMultiTileReport>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "terrain.detail.multitile: distinct initialized layers required"));
        operationTiles.push_back({tile.name, tile.originX, tile.originZ, tile.width, tile.depth,
                                  tile.layer->impl_->width, tile.layer->impl_->height, tile.worldMap});
    }
    auto mapped = mapTerrainOperationMultiTile(operationTiles, operationSettings,
                                                TerrainOperationDomain::TerrainDetail, worldMapOperation,
                                                validTerrainNames);
    if (!mapped.ok()) return mapped;
    auto report = std::move(mapped.value());
    if (operationFitness.getWidth() != report.operationWidth ||
        operationFitness.getHeight() != report.operationHeight ||
        operationFitness.data().size() != size_t(report.operationWidth) * report.operationHeight)
        return Result<TerrainMultiTileReport>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "terrain.detail.multitile: operation fitness dimensions must match window"));
    for (float value : operationFitness.data())
        if (!std::isfinite(value))
            return Result<TerrainMultiTileReport>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "terrain.detail.multitile: finite operation fitness required"));

    struct Candidate {
        TerrainDetailLayer* target;
        std::unique_ptr<TerrainDetailLayer::Impl> value;
    };
    std::vector<Candidate> candidates;
    candidates.reserve(report.mappings.size());
    DetailThinningStream random(seed);
    const float sign = settings.mode == TerrainDetailMode::Remove ? -1.0F : 1.0F;
    for (const auto& mapping : report.mappings) {
        const auto tile = std::find_if(tiles.begin(), tiles.end(), [&](const auto& item) {
            return item.name == mapping.terrainName;
        });
        if (tile == tiles.end())
            return Result<TerrainMultiTileReport>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "terrain.detail.multitile: internal mapping has no tile"));
        auto next = std::make_unique<TerrainDetailLayer::Impl>(*tile->layer->impl_);
        auto [resource, inserted] = next->resources.try_emplace(settings.namespaceId, next->counts.size(), 0);
        (void)inserted;
        auto& targetCounts = resource->second;
        if (settings.mode == TerrainDetailMode::Replace) std::fill(targetCounts.begin(), targetCounts.end(), 0);
        const auto& before = tile->layer->impl_->counts;
        for (int x = 0; x < mapping.width; ++x)
            for (int z = 0; z < mapping.height; ++z) {
                const int localX = mapping.localX + x, localZ = mapping.localY + z;
                const int operationX = mapping.operationX + x, operationZ = mapping.operationY + z;
                const size_t localIndex = size_t(localZ) * next->width + localX;
                const float fitness = operationFitness.data()[size_t(operationZ) * report.operationWidth + operationX];
                if (fitness <= settings.minimumFitness) continue;
                if (fitness < settings.fadeStart &&
                    random.next() > inverseLerp(settings.minimumFitness, settings.fadeStart, fitness))
                    continue;
                const float amount = inverseLerp(settings.minimumFitness, 1.0F, fitness) * settings.density;
                targetCounts[localIndex] = nearestEven(
                    std::clamp(float(targetCounts[localIndex]) + sign * amount, 0.0F, settings.density));
            }
        auto rebuilt = next->rebuildTotals();
        if (!rebuilt.ok()) return Result<TerrainMultiTileReport>::failure(rebuilt.status());
        for (size_t i = 0; i < before.size(); ++i) {
            if (before[i] == next->counts[i]) continue;
            if (report.changedSamples == std::numeric_limits<int>::max())
                return Result<TerrainMultiTileReport>::failure(Diagnostic::error(
                    DiagnosticCode::InvalidArgument, "terrain.detail.multitile: changed cell count exceeds integer range"));
            ++report.changedSamples;
        }
        candidates.push_back({tile->layer, std::move(next)});
    }
    for (auto& candidate : candidates) candidate.target->impl_.swap(candidate.value);
    return Result<TerrainMultiTileReport>::success(std::move(report));
}

struct TerrainMultiDetailWorkspace::Impl {
    struct Tile {
        std::string name;
        TerrainDetailLayer layer;
        double originX = 0, originZ = 0, width = 1, depth = 1;
        bool worldMap = false;
    };
    struct Snapshot {
        std::vector<TerrainDetailLayer> layers;
        TerrainMultiTileReport report;
    };
    std::vector<Tile> tiles;
    TerrainMultiTileReport last;
    std::vector<Snapshot> history;
    int cursor = 0;

    Impl() = default;
    Impl(const Impl& other) : last(other.last), history(other.history), cursor(other.cursor) {
        tiles.reserve(other.tiles.size());
        for (const auto& source : other.tiles) {
            TerrainDetailLayer layer;
            layer.impl_ = std::make_unique<TerrainDetailLayer::Impl>(*source.layer.impl_);
            tiles.push_back({source.name, std::move(layer), source.originX, source.originZ, source.width,
                             source.depth, source.worldMap});
        }
    }
    Snapshot snapshot() const {
        Snapshot result;
        result.layers.reserve(tiles.size());
        for (const auto& tile : tiles) result.layers.push_back(tile.layer);
        result.report = last;
        return result;
    }
    void restore(const Snapshot& snapshot) {
        for (size_t i = 0; i < tiles.size(); ++i) tiles[i].layer = snapshot.layers[i];
        last = snapshot.report;
    }
};

TerrainMultiDetailWorkspace::TerrainMultiDetailWorkspace() : impl_(std::make_unique<Impl>()) {}
TerrainMultiDetailWorkspace::~TerrainMultiDetailWorkspace() = default;
TerrainMultiDetailWorkspace::TerrainMultiDetailWorkspace(TerrainMultiDetailWorkspace&&) noexcept = default;
TerrainMultiDetailWorkspace& TerrainMultiDetailWorkspace::operator=(TerrainMultiDetailWorkspace&&) noexcept = default;

Result<int> TerrainMultiDetailWorkspace::addTile(const std::string& name, const TerrainDetailLayer& layer,
                                                  double originX, double originZ, double width, double depth,
                                                  bool worldMap) {
    if (!impl_ || !layer.impl_ || name.empty())
        return Result<int>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "terrain.detail.workspace: initialized named tile required"));
    if (std::any_of(impl_->tiles.begin(), impl_->tiles.end(), [&](const auto& tile) { return tile.name == name; }))
        return Result<int>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "terrain.detail.workspace: duplicate tile name"));
    if (impl_->history.size() > 1)
        return Result<int>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "terrain.detail.workspace: topology is fixed after applying"));
    auto candidate = std::make_unique<Impl>(*impl_);
    TerrainDetailLayer owned;
    owned.impl_ = std::make_unique<TerrainDetailLayer::Impl>(*layer.impl_);
    candidate->tiles.push_back({name, std::move(owned), originX, originZ, width, depth, worldMap});
    std::vector<TerrainOperationTile> descriptors;
    for (const auto& tile : candidate->tiles)
        descriptors.push_back({tile.name, tile.originX, tile.originZ, tile.width, tile.depth,
                               tile.layer.impl_->width, tile.layer.impl_->height, tile.worldMap});
    TerrainStampSettings bounds;
    bounds.centerX = originX + width * 0.5;
    bounds.centerZ = originZ + depth * 0.5;
    bounds.width = width;
    bounds.depth = depth;
    auto checked = mapTerrainOperationMultiTile(descriptors, bounds, TerrainOperationDomain::TerrainDetail,
                                                 worldMap, {name});
    if (!checked.ok()) return Result<int>::failure(checked.status());
    candidate->history.clear();
    candidate->history.push_back(candidate->snapshot());
    candidate->cursor = 0;
    impl_.swap(candidate);
    return Result<int>::success(int(impl_->tiles.size()));
}

Result<int> TerrainMultiDetailWorkspace::apply(const Heightmap& operationFitness,
                                                const TerrainDetailSettings& settings,
                                                const TerrainStampSettings& operationSettings, int32_t seed,
                                                bool worldMapOperation) {
    if (!impl_)
        return Result<int>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "terrain.detail.workspace: moved-from workspace"));
    auto candidate = std::make_unique<Impl>(*impl_);
    std::vector<TerrainDetailTile> descriptors;
    for (auto& tile : candidate->tiles)
        descriptors.push_back({tile.name, &tile.layer, tile.originX, tile.originZ, tile.width, tile.depth,
                               tile.worldMap});
    auto applied = applyTerrainDetailMultiTile(descriptors, operationFitness, settings, operationSettings, seed,
                                                worldMapOperation);
    if (!applied.ok()) return Result<int>::failure(applied.status());
    candidate->last = std::move(applied.value());
    candidate->history.resize(size_t(candidate->cursor + 1));
    candidate->history.push_back(candidate->snapshot());
    ++candidate->cursor;
    const int changed = candidate->last.changedSamples;
    impl_.swap(candidate);
    return Result<int>::success(changed);
}

Result<int> TerrainMultiDetailWorkspace::copyTile(const std::string& name, TerrainDetailLayer& output) const {
    if (!impl_)
        return Result<int>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "terrain.detail.workspace: moved-from workspace"));
    const auto found = std::find_if(impl_->tiles.begin(), impl_->tiles.end(),
                                    [&](const auto& tile) { return tile.name == name; });
    if (found == impl_->tiles.end())
        return Result<int>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "terrain.detail.workspace: tile not found"));
    auto copy = std::make_unique<TerrainDetailLayer::Impl>(*found->layer.impl_);
    output.impl_.swap(copy);
    return Result<int>::success(found->layer.impl_->width * found->layer.impl_->height);
}

Result<int> TerrainMultiDetailWorkspace::undo() {
    if (!impl_ || impl_->cursor <= 0)
        return Result<int>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "terrain.detail.workspace: no undo snapshot"));
    auto candidate = std::make_unique<Impl>(*impl_);
    --candidate->cursor;
    candidate->restore(candidate->history[size_t(candidate->cursor)]);
    const int cursor = candidate->cursor;
    impl_.swap(candidate);
    return Result<int>::success(cursor);
}
Result<int> TerrainMultiDetailWorkspace::redo() {
    if (!impl_ || impl_->cursor + 1 >= int(impl_->history.size()))
        return Result<int>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "terrain.detail.workspace: no redo snapshot"));
    auto candidate = std::make_unique<Impl>(*impl_);
    ++candidate->cursor;
    candidate->restore(candidate->history[size_t(candidate->cursor)]);
    const int cursor = candidate->cursor;
    impl_.swap(candidate);
    return Result<int>::success(cursor);
}
int TerrainMultiDetailWorkspace::getTileCount() const noexcept { return impl_ ? int(impl_->tiles.size()) : 0; }
int TerrainMultiDetailWorkspace::getLastChangedSamples() const noexcept {
    return impl_ ? impl_->last.changedSamples : 0;
}
int TerrainMultiDetailWorkspace::getLastAffectedTiles() const noexcept {
    return impl_ ? impl_->last.affectedTiles : 0;
}
int TerrainMultiDetailWorkspace::getOperationCount() const noexcept {
    return impl_ ? std::max(0, int(impl_->history.size()) - 1) : 0;
}
int TerrainMultiDetailWorkspace::getAppliedCount() const noexcept { return impl_ ? impl_->cursor : 0; }
}  // namespace eve::procgen
