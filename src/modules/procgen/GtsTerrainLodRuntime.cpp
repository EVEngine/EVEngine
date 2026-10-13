#include "procgen/GtsTerrainLodRuntime.h"

#include "common/ECS.h"
#include "graphics/RenderSystem3D.h"
#include "procgen/GtsTerrainLod.h"
#include "procgen/Procgen.h"

#include <vector>

namespace eve::procgen {
struct GtsTerrainLodRuntime::Impl {
    std::vector<ecs::EntityHandle> tiles;
    ecs::EntityHandle              sourceTerrain{};
    bool                           sourceWasVisible = false;
    bool                           hasSource        = false;
    std::uint64_t                  revision         = 0;
};
namespace {
graphics::Renderable3D* resolve(const ecs::EntityHandle& handle) {
    return dynamic_cast<graphics::Renderable3D*>(ecs::try_get(handle));
}
void destroy(std::vector<ecs::EntityHandle>& handles) {
    for (auto& h : handles)
        if (auto* entity = resolve(h)) ecs::DestroyEntity(entity);
    handles.clear();
}
void restoreSource(ecs::EntityHandle& handle, bool wasVisible, bool& hasSource) {
    if (hasSource)
        if (auto* source = resolve(handle)) source->setVisible(wasVisible);
    handle    = {};
    hasSource = false;
}
}  // namespace
GtsTerrainLodRuntime::GtsTerrainLodRuntime() : impl_(std::make_unique<Impl>()) {}
GtsTerrainLodRuntime::~GtsTerrainLodRuntime() {
    destroy(impl_->tiles);
    restoreSource(impl_->sourceTerrain, impl_->sourceWasVisible, impl_->hasSource);
}
Result<std::uint64_t> GtsTerrainLodRuntime::replace(const GtsTerrainLodSet& lods, Procgen& procgen,
                                                    graphics::Graphics& gfx, float diameter, float fov, float ox,
                                                    float oy, float oz) {
    std::vector<ecs::EntityHandle> candidate(size_t(lods.getTileCount()));
    for (int i = 0; i < lods.getTileCount(); ++i) {
        auto* tile = lods.tileAt(i);
        if (!tile || tile->levels.empty() || tile->levels.front().empty()) continue;
        auto* entity = graphics::Renderable3D::create();
        if (!entity) {
            destroy(candidate);
            return Result<std::uint64_t>::failure(Diagnostic::error(DiagnosticCode::Failed,
                                                                    "failed to create GTS terrain tile renderable",
                                                                    "procgen.gtsTerrainLodRuntime"));
        }
        entity->setVisible(false);
        candidate[size_t(i)] = ecs::handle_of(entity);
        auto configured      = procgen.configureGtsTerrainTileLods(lods, i, *entity, gfx, diameter, fov, ox, oy, oz);
        if (!configured) {
            destroy(candidate);
            return Result<std::uint64_t>::failure(*configured.error());
        }
    }
    for (auto& h : impl_->tiles)
        if (auto* entity = resolve(h)) entity->setVisible(false);
    for (auto& h : candidate)
        if (auto* entity = resolve(h)) entity->setVisible(true);
    destroy(impl_->tiles);
    impl_->tiles = std::move(candidate);
    ++impl_->revision;
    if (impl_->revision == 0) ++impl_->revision;
    return Result<std::uint64_t>::success(impl_->revision);
}
Result<std::uint64_t> GtsTerrainLodRuntime::replaceAndHideSource(const GtsTerrainLodSet& lods, Procgen& procgen,
                                                                 graphics::Graphics&     gfx,
                                                                 graphics::Renderable3D& source, float diameter,
                                                                 float fov, float ox, float oy, float oz) {
    for (const auto& handle : impl_->tiles)
        if (resolve(handle) == &source)
            return Result<std::uint64_t>::failure(
                Diagnostic::error(DiagnosticCode::InvalidArgument,
                                  "GTS source terrain cannot be one of the managed LOD tiles", "sourceTerrain"));
    const auto sourceHandle       = ecs::handle_of(&source);
    const bool sameSource         = impl_->hasSource && resolve(impl_->sourceTerrain) == &source;
    const bool enteringVisibility = sameSource ? impl_->sourceWasVisible : source.getVisible();
    auto       replaced           = replace(lods, procgen, gfx, diameter, fov, ox, oy, oz);
    if (!replaced) return replaced;
    if (!sameSource) restoreSource(impl_->sourceTerrain, impl_->sourceWasVisible, impl_->hasSource);
    impl_->sourceTerrain    = sourceHandle;
    impl_->sourceWasVisible = enteringVisibility;
    impl_->hasSource        = true;
    source.setVisible(false);
    return replaced;
}
graphics::Renderable3D* GtsTerrainLodRuntime::getSourceTerrain() const {
    return impl_->hasSource ? resolve(impl_->sourceTerrain) : nullptr;
}
Result<int> GtsTerrainLodRuntime::clear() {
    int count = 0;
    for (auto& h : impl_->tiles)
        if (resolve(h)) ++count;
    destroy(impl_->tiles);
    restoreSource(impl_->sourceTerrain, impl_->sourceWasVisible, impl_->hasSource);
    ++impl_->revision;
    if (impl_->revision == 0) ++impl_->revision;
    return Result<int>::success(count);
}
Result<int> GtsTerrainLodRuntime::applyMaterial(graphics::Material& material) {
    int count = 0;
    for (auto& h : impl_->tiles)
        if (auto* entity = resolve(h)) {
            entity->setMaterial(&material);
            ++count;
        }
    return Result<int>::success(count);
}
int                     GtsTerrainLodRuntime::getTileCount() const { return int(impl_->tiles.size()); }
std::uint64_t           GtsTerrainLodRuntime::getRevision() const { return impl_->revision; }
graphics::Renderable3D* GtsTerrainLodRuntime::getRenderable(int index) const {
    return index >= 0 && index < int(impl_->tiles.size()) ? resolve(impl_->tiles[size_t(index)]) : nullptr;
}
}  // namespace eve::procgen

#include "procgen/GtsTerrainLod.h"
#include "procgen/heightmap/Heightmap.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string_view>

namespace eve::procgen {
namespace {}  // namespace

Result<void> buildGtsTerrainBaseMesh(MeshBuild& output, const Heightmap& heightmap, GtsTerrainSaveResolution resolution,
                                     float sizeX, float sizeY, float sizeZ) {
    const int exponent    = static_cast<int>(resolution);
    const int sourceWidth = heightmap.getWidth(), sourceHeight = heightmap.getHeight();
    if (exponent < 0 || exponent > 4 || sourceWidth < 2 || sourceHeight < 2 || !std::isfinite(sizeX) || sizeX <= 0.f ||
        !std::isfinite(sizeY) || sizeY <= 0.f || !std::isfinite(sizeZ) || sizeZ <= 0.f)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "GTS terrain heightmap or export dimensions are invalid",
                                                       "procgen.gtsTerrainLod"));
    const int step = 1 << exponent;
    if ((sourceWidth - 1) % step != 0 || (sourceHeight - 1) % step != 0)
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument,
                              "GTS terrain heightmap intervals must be divisible by the save-resolution stride",
                              "procgen.gtsTerrainLod"));
    const int           columns = (sourceWidth - 1) / step + 1, rows = (sourceHeight - 1) / step + 1;
    const std::uint64_t vertexCount = std::uint64_t(columns) * std::uint64_t(rows);
    const std::uint64_t indexCount  = std::uint64_t(columns - 1) * std::uint64_t(rows - 1) * 6u;
    if (vertexCount > 8u * 1024u * 1024u || indexCount > 48u * 1024u * 1024u ||
        vertexCount > std::uint64_t(std::numeric_limits<int>::max()) ||
        indexCount > std::uint64_t(std::numeric_limits<int>::max()))
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "GTS terrain base mesh exceeds the native export budget",
                                                       "procgen.gtsTerrainLod"));
    for (float value : heightmap.data())
        if (!std::isfinite(value))
            return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                           "GTS terrain heightmap contains a non-finite sample",
                                                           "procgen.gtsTerrainLod"));
    MeshBuild candidate;
    candidate.reserve(static_cast<int>(vertexCount), static_cast<int>(indexCount));
    candidate.setActiveGroup("terrain");
    for (int z = 0; z < rows; ++z)
        for (int x = 0; x < columns; ++x) {
            const int gx = x * step, gz = z * step;
            candidate.addVertex(sizeX * float(gx) / float(sourceWidth - 1), heightmap.height(gx, gz) * sizeY,
                                sizeZ * float(gz) / float(sourceHeight - 1), 0.f, 0.f, 0.f,
                                float(gx) / float(sourceWidth - 1), float(gz) / float(sourceHeight - 1));
        }
    for (int z = 0; z + 1 < rows; ++z)
        for (int x = 0; x + 1 < columns; ++x) {
            const std::uint32_t a = std::uint32_t(z * columns + x), b = a + 1, c = a + std::uint32_t(columns),
                                d = c + 1;
            candidate.addTriangle(a, c, b);
            candidate.addTriangle(b, c, d);
        }
    auto&       normals   = candidate.normals();
    const auto& positions = candidate.positions();
    const auto& indices   = candidate.indices();
    for (size_t i = 0; i < indices.size(); i += 3) {
        const auto  a = indices[i], b = indices[i + 1], c = indices[i + 2];
        const float abx = positions[b * 3] - positions[a * 3], aby = positions[b * 3 + 1] - positions[a * 3 + 1],
                    abz = positions[b * 3 + 2] - positions[a * 3 + 2];
        const float acx = positions[c * 3] - positions[a * 3], acy = positions[c * 3 + 1] - positions[a * 3 + 1],
                    acz = positions[c * 3 + 2] - positions[a * 3 + 2];
        const float nx = aby * acz - abz * acy, ny = abz * acx - abx * acz, nz = abx * acy - aby * acx;
        for (auto vertex : {a, b, c}) {
            normals[vertex * 3] += nx;
            normals[vertex * 3 + 1] += ny;
            normals[vertex * 3 + 2] += nz;
        }
    }
    for (size_t i = 0; i < normals.size(); i += 3) {
        const float length =
            std::sqrt(normals[i] * normals[i] + normals[i + 1] * normals[i + 1] + normals[i + 2] * normals[i + 2]);
        if (!(length > 0.f) || !std::isfinite(length))
            return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                           "GTS terrain base mesh contains a degenerate vertex normal",
                                                           "procgen.gtsTerrainLod"));
        normals[i] /= length;
        normals[i + 1] /= length;
        normals[i + 2] /= length;
    }
    candidate.setMeta("kind", "gts.terrain.base");
    candidate.setMeta("saveResolution", std::to_string(exponent));
    candidate.setMeta("sourceWidth", std::to_string(sourceWidth));
    candidate.setMeta("sourceHeight", std::to_string(sourceHeight));
    output = std::move(candidate);
    return Result<void>::success();
}

const GtsTerrainLodTile* GtsTerrainLodSet::tileAt(int index) const {
    return index >= 0 && index < static_cast<int>(tiles_.size()) ? &tiles_[index] : nullptr;
}

const GtsTerrainLodLevelSettings* GtsTerrainLodSet::levelAt(int index) const {
    return index >= 0 && index < static_cast<int>(settings_.size()) ? &settings_[index] : nullptr;
}

Result<void> GtsTerrainMeshSettings::setSaveResolution(int value) {
    if (value < 0 || value > 4)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "GTS save resolution must be in the range 0 through 4",
                                                       "procgen.gtsTerrainLod"));
    saveResolution_ = value;
    return Result<void>::success();
}
Result<void> GtsTerrainMeshSettings::setLodCount(int value) {
    if (value < 1 || value > 4)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "GTS LOD count must be in the range 1 through 4",
                                                       "procgen.gtsTerrainLod"));
    lodCount_ = value;
    return Result<void>::success();
}
Result<void> GtsTerrainMeshSettings::setSubTiles(int value) {
    if (value < 0 || value > 5)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "GTS sub-tile split count must be in the range 0 through 5",
                                                       "procgen.gtsTerrainLod"));
    subTiles_ = value;
    return Result<void>::success();
}
float GtsTerrainMeshSettings::getLodQuality(int index) const noexcept {
    return index >= 0 && index < 4 ? lodQuality_[index] : -1.f;
}
Result<void> GtsTerrainMeshSettings::setLodQuality(int index, float percent) {
    if (index < 0 || index >= 4 || !std::isfinite(percent) || percent < 0.f || percent > 100.f)
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument,
                              "GTS LOD quality requires a slot from 0 through 3 and percentage from 0 through 100",
                              "procgen.gtsTerrainLod"));
    lodQuality_[index] = percent;
    return Result<void>::success();
}
float GtsTerrainMeshSettings::getLodTransitionHeight(int index) const noexcept {
    if (index < 0 || index >= lodCount_) return -1.f;
    constexpr float transitions[]{.95f, .7f, .6f, .02f};
    return index == lodCount_ - 1 ? .02f : transitions[index];
}
Result<std::vector<GtsTerrainLodLevelSettings>> GtsTerrainMeshSettings::compileLevels() const {
    std::vector<GtsTerrainLodLevelSettings> levels;
    levels.reserve(size_t(lodCount_));
    for (int i = 0; i < lodCount_; ++i) {
        GtsTerrainLodLevelSettings level;
        level.quality                        = lodQuality_[i] / 100.f;
        level.screenRelativeTransitionHeight = getLodTransitionHeight(i);
        levels.push_back(level);
    }
    return Result<std::vector<GtsTerrainLodLevelSettings>>::success(std::move(levels));
}

int GtsTerrainLodSet::selectLevel(float relativeHeight) const {
    if (!std::isfinite(relativeHeight) || relativeHeight < 0.0f || settings_.empty()) return -2;
    for (int i = 0; i < static_cast<int>(settings_.size()); ++i)
        if (relativeHeight >= settings_[i].screenRelativeTransitionHeight) return i;
    return -1;
}

float GtsTerrainLodSet::getLevelSwitchDistance(int level, float worldDiameter, float verticalFovDegrees) const {
    if (level < 0 || level >= static_cast<int>(settings_.size()) || !std::isfinite(worldDiameter) ||
        worldDiameter <= 0.0f || !std::isfinite(verticalFovDegrees) || verticalFovDegrees <= 0.0f ||
        verticalFovDegrees >= 180.0f)
        return -1.0f;
    constexpr float radiansPerDegree = 0.01745329251994329577f;
    const float     tangent          = std::tan(verticalFovDegrees * 0.5f * radiansPerDegree);
    return worldDiameter / (2.0f * tangent * settings_[level].screenRelativeTransitionHeight);
}

int GtsTerrainLodSet::selectLevelForCamera(float distance, float worldDiameter, float verticalFovDegrees) const {
    if (!std::isfinite(distance) || distance <= 0.0f || !std::isfinite(worldDiameter) || worldDiameter <= 0.0f ||
        !std::isfinite(verticalFovDegrees) || verticalFovDegrees <= 0.0f || verticalFovDegrees >= 180.0f)
        return -2;
    constexpr float radiansPerDegree = 0.01745329251994329577f;
    const float     relativeHeight =
        worldDiameter / (2.0f * distance * std::tan(verticalFovDegrees * 0.5f * radiansPerDegree));
    return selectLevel(relativeHeight);
}

std::vector<GtsTerrainLodLevelSettings> defaultGtsTerrainLodLevels() {
    std::vector<GtsTerrainLodLevelSettings> levels(4);
    levels[0].quality                        = 1.0f;
    levels[1].quality                        = 0.5f;
    levels[2].quality                        = 0.25f;
    levels[3].quality                        = 0.125f;
    levels[0].screenRelativeTransitionHeight = 0.95f;
    levels[1].screenRelativeTransitionHeight = 0.7f;
    levels[2].screenRelativeTransitionHeight = 0.6f;
    levels[3].screenRelativeTransitionHeight = 0.02f;
    return levels;
}

Result<std::vector<GtsTerrainLodAssetEntry>> planGtsTerrainLodAssets(const GtsTerrainLodSet& lods,
                                                                     const std::string&      terrainName,
                                                                     const std::string&      meshFolder) {
    auto safe = [](const std::string& v, bool folder) {
        if (v.empty() || v == "." || v == ".." || v.find("..") != std::string::npos) return false;
        constexpr std::string_view invalid = "\\:*?\"<>|";
        if (v.find_first_of(invalid) != std::string::npos) return false;
        return folder || v.find('/') == std::string::npos;
    };
    if (!safe(terrainName, false) || !safe(meshFolder, true) || meshFolder.front() == '/' || meshFolder.back() == '/')
        return Result<std::vector<GtsTerrainLodAssetEntry>>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "terrain name or mesh folder is not a safe relative export name",
            "procgen.gtsTerrainLod"));
    std::vector<GtsTerrainLodAssetEntry> entries;
    for (int tileIndex = 0; tileIndex < lods.getTileCount(); ++tileIndex) {
        auto* tile = lods.tileAt(tileIndex);
        if (!tile) continue;
        for (int level = 0; level < static_cast<int>(tile->levels.size()); ++level) {
            if (tile->levels[size_t(level)].empty()) continue;
            GtsTerrainLodAssetEntry e;
            e.tileIndex    = tileIndex;
            e.levelIndex   = level;
            e.objectName   = terrainName + " SubTile" + std::to_string(tileIndex) + "_LOD_" + std::to_string(level);
            e.meshName     = terrainName + "_SubTile_" + std::to_string(tileIndex) + std::to_string(level);
            e.relativePath = meshFolder + "/" + e.meshName + ".eve.mesh.json";
            entries.push_back(std::move(e));
        }
    }
    return Result<std::vector<GtsTerrainLodAssetEntry>>::success(std::move(entries));
}
int GtsTerrainLodAssetPlan::getEntryCount() const { return static_cast<int>(entries_.size()); }
int GtsTerrainLodAssetPlan::getTileIndex(int i) const {
    return i >= 0 && i < int(entries_.size()) ? entries_[size_t(i)].tileIndex : -1;
}
int GtsTerrainLodAssetPlan::getLevelIndex(int i) const {
    return i >= 0 && i < int(entries_.size()) ? entries_[size_t(i)].levelIndex : -1;
}
std::string GtsTerrainLodAssetPlan::getObjectName(int i) const {
    return i >= 0 && i < int(entries_.size()) ? entries_[size_t(i)].objectName : std::string{};
}
std::string GtsTerrainLodAssetPlan::getMeshName(int i) const {
    return i >= 0 && i < int(entries_.size()) ? entries_[size_t(i)].meshName : std::string{};
}
std::string GtsTerrainLodAssetPlan::getRelativePath(int i) const {
    return i >= 0 && i < int(entries_.size()) ? entries_[size_t(i)].relativePath : std::string{};
}
Result<void> planGtsTerrainLodAssetsInto(GtsTerrainLodAssetPlan& output, const GtsTerrainLodSet& lods,
                                         const std::string& name, const std::string& folder) {
    auto plan = planGtsTerrainLodAssets(lods, name, folder);
    if (!plan) return Result<void>::failure(*plan.error());
    GtsTerrainLodAssetPlan candidate;
    candidate.entries_ = std::move(plan).takeValue();
    output             = std::move(candidate);
    return Result<void>::success();
}

Result<GtsTerrainLodSet> buildGtsTerrainLods(const MeshBuild& source, int xSplits, int zSplits, GtsMeshPivot pivot,
                                             const std::vector<GtsTerrainLodLevelSettings>& levels) {
    if (levels.empty())
        return Result<GtsTerrainLodSet>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "at least one terrain LOD level is required", "procgen.gtsTerrainLod"));
    float previousTransition = 1.0f;
    for (const auto& level : levels) {
        if (!std::isfinite(level.quality) || level.quality < 0.0f || level.quality > 1.0f ||
            !std::isfinite(level.screenRelativeTransitionHeight) || level.screenRelativeTransitionHeight < 0.0f ||
            level.screenRelativeTransitionHeight > 1.0f || level.screenRelativeTransitionHeight >= previousTransition) {
            return Result<GtsTerrainLodSet>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                                       "LOD quality or transition sequence is invalid",
                                                                       "procgen.gtsTerrainLod"));
        }
        previousTransition = level.screenRelativeTransitionHeight;
    }

    auto split = splitGtsMesh(source, xSplits, zSplits, pivot);
    if (!split) return Result<GtsTerrainLodSet>::failure(*split.error());

    GtsTerrainLodSet result;
    result.columns_  = split.value().getColumnCount();
    result.rows_     = split.value().getRowCount();
    result.settings_ = levels;
    result.tiles_.reserve(split.value().getTileCount());
    for (int tileIndex = 0; tileIndex < split.value().getTileCount(); ++tileIndex) {
        const auto*       sourceTile = split.value().tileAt(tileIndex);
        GtsTerrainLodTile tile;
        tile.offsetX = sourceTile->offsetX;
        tile.offsetZ = sourceTile->offsetZ;
        tile.levels.reserve(levels.size());
        MeshBuild previous = sourceTile->mesh;
        for (const auto& level : levels) {
            if (previous.empty()) {
                tile.levels.emplace_back();
                continue;
            }
            MeshBuild simplified;
            auto      simplification = simplifyGtsMesh(simplified, previous, level.quality, level.simplification);
            if (!simplification) return Result<GtsTerrainLodSet>::failure(*simplification.error());
            tile.levels.push_back(simplified);
            previous = std::move(simplified);
        }
        result.tiles_.push_back(std::move(tile));
    }
    return Result<GtsTerrainLodSet>::success(std::move(result));
}

Result<GtsTerrainLodSet> buildGtsTerrainLodsFromHeightmap(const Heightmap&         heightmap,
                                                          GtsTerrainSaveResolution resolution, float sizeX, float sizeY,
                                                          float sizeZ, int subTileSplits, GtsMeshPivot pivot,
                                                          const std::vector<GtsTerrainLodLevelSettings>& levels) {
    if (subTileSplits < 0 || subTileSplits > 5)
        return Result<GtsTerrainLodSet>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "GTS sub-tile split count must be in the profile range 0 through 5",
            "procgen.gtsTerrainLod"));
    MeshBuild base;
    auto      built = buildGtsTerrainBaseMesh(base, heightmap, resolution, sizeX, sizeY, sizeZ);
    if (!built) return Result<GtsTerrainLodSet>::failure(*built.error());
    return buildGtsTerrainLods(base, subTileSplits, subTileSplits, pivot, levels);
}

Result<void> buildDefaultGtsTerrainLodsFromHeightmapInto(GtsTerrainLodSet& output, const Heightmap& heightmap,
                                                         GtsTerrainSaveResolution resolution, float sizeX, float sizeY,
                                                         float sizeZ, int subTileSplits, GtsMeshPivot pivot) {
    auto built = buildGtsTerrainLodsFromHeightmap(heightmap, resolution, sizeX, sizeY, sizeZ, subTileSplits, pivot,
                                                  defaultGtsTerrainLodLevels());
    if (!built) return Result<void>::failure(*built.error());
    output = std::move(built).takeValue();
    return Result<void>::success();
}

Result<void> buildGtsTerrainLodsFromHeightmapInto(GtsTerrainLodSet& output, const Heightmap& heightmap,
                                                  const GtsTerrainMeshSettings& settings, float sizeX, float sizeY,
                                                  float sizeZ, GtsMeshPivot pivot) {
    auto levels = settings.compileLevels();
    if (!levels) return Result<void>::failure(*levels.error());
    auto built =
        buildGtsTerrainLodsFromHeightmap(heightmap, static_cast<GtsTerrainSaveResolution>(settings.getSaveResolution()),
                                         sizeX, sizeY, sizeZ, settings.getSubTiles(), pivot, levels.value());
    if (!built) return Result<void>::failure(*built.error());
    output = std::move(built).takeValue();
    return Result<void>::success();
}

}  // namespace eve::procgen

#include "procgen/GtsTerrainLod.h"

#include "common/Value.h"

#include <cmath>
#include <cstdint>
#include <limits>
#include <set>

namespace eve::procgen {
namespace {
constexpr std::size_t kMaxJsonBytes = 256U * 1024U * 1024U, kMaxTiles = 65536, kMaxLevels = 32,
                      kMaxVertices = 8U * 1024U * 1024U;
bool exact(const Value::Object& o, std::initializer_list<const char*> names) {
    if (o.size() != names.size()) return false;
    for (auto* n : names)
        if (!o.contains(n)) return false;
    return true;
}
const double* num(const Value& v) { return v.getIf<double>(); }
Value         options(const GtsMeshSimplificationOptions& o) {
    return Value::Object{{"aggressiveness", o.aggressiveness},
                                 {"enableSmartLink", o.enableSmartLink},
                                 {"maxIterationCount", o.maxIterationCount},
                                 {"preserveBorderEdges", o.preserveBorderEdges},
                                 {"preserveSurfaceCurvature", o.preserveSurfaceCurvature},
                                 {"preserveUvFoldoverEdges", o.preserveUvFoldoverEdges},
                                 {"preserveUvSeamEdges", o.preserveUvSeamEdges},
                                 {"vertexLinkDistance", o.vertexLinkDistance}};
}
bool decodeOptions(const Value& v, GtsMeshSimplificationOptions& o) {
    auto* p = v.getIf<Value::Object>();
    if (!p || !exact(*p, {"aggressiveness", "enableSmartLink", "maxIterationCount", "preserveBorderEdges",
                          "preserveSurfaceCurvature", "preserveUvFoldoverEdges", "preserveUvSeamEdges",
                          "vertexLinkDistance"}))
        return false;
    auto ag = num(p->at("aggressiveness")), link = num(p->at("vertexLinkDistance"));
    auto count = p->at("maxIterationCount").getIf<int64_t>();
    auto smart = p->at("enableSmartLink").getIf<bool>(), border = p->at("preserveBorderEdges").getIf<bool>(),
         curve = p->at("preserveSurfaceCurvature").getIf<bool>(), fold = p->at("preserveUvFoldoverEdges").getIf<bool>(),
         seam = p->at("preserveUvSeamEdges").getIf<bool>();
    if (!ag || !link || !count || !smart || !border || !curve || !fold || !seam || !std::isfinite(*ag) || *ag <= 0 ||
        !std::isfinite(*link) || *link < 0 || *count <= 0 || *count > 100000)
        return false;
    o.aggressiveness           = *ag;
    o.vertexLinkDistance       = *link;
    o.maxIterationCount        = int(*count);
    o.enableSmartLink          = *smart;
    o.preserveBorderEdges      = *border;
    o.preserveSurfaceCurvature = *curve;
    o.preserveUvFoldoverEdges  = *fold;
    o.preserveUvSeamEdges      = *seam;
    return true;
}
Value encodeMesh(const MeshBuild& m) {
    Value::Array p, n, u, c, i, g;
    p.reserve(m.positions().size());
    n.reserve(m.normals().size());
    u.reserve(m.uvs().size());
    c.reserve(m.colors().size());
    i.reserve(m.indices().size());
    g.reserve(m.getIndexCount() / 3);
    for (float x : m.positions()) p.emplace_back(x);
    for (float x : m.normals()) n.emplace_back(x);
    for (float x : m.uvs()) u.emplace_back(x);
    for (float x : m.colors()) c.emplace_back(x);
    for (auto x : m.indices()) i.emplace_back(int64_t(x));
    for (int t = 0; t < m.getIndexCount() / 3; ++t) {
        int group = m.getTriangleGroup(t);
        g.emplace_back(group >= 0 ? m.getGroupName(group) : std::string{});
    }
    return Value::Object{{"colors", std::move(c)},  {"groups", std::move(g)},    {"indices", std::move(i)},
                         {"normals", std::move(n)}, {"positions", std::move(p)}, {"uvs", std::move(u)}};
}
bool decodeFloatArray(const Value& v, std::vector<float>& out, std::size_t max) {
    auto* a = v.getIf<Value::Array>();
    if (!a || a->size() > max) return false;
    out.reserve(a->size());
    for (auto& x : *a) {
        auto* d = num(x);
        if (!d || !std::isfinite(*d) || *d < -std::numeric_limits<float>::max() ||
            *d > std::numeric_limits<float>::max())
            return false;
        out.push_back(float(*d));
    }
    return true;
}
bool decodeMesh(const Value& v, MeshBuild& out, bool legacy) {
    auto* o = v.getIf<Value::Object>();
    if (!o || !(legacy ? exact(*o, {"groups", "indices", "normals", "positions", "uvs"})
                       : exact(*o, {"colors", "groups", "indices", "normals", "positions", "uvs"})))
        return false;
    std::vector<float> p, n, u, c;
    if (!decodeFloatArray(o->at("positions"), p, kMaxVertices * 3) ||
        !decodeFloatArray(o->at("normals"), n, kMaxVertices * 3) ||
        !decodeFloatArray(o->at("uvs"), u, kMaxVertices * 2) ||
        (!legacy && !decodeFloatArray(o->at("colors"), c, kMaxVertices * 4)) || p.size() % 3 || u.size() % 2 ||
        n.size() != p.size() || u.size() / 2 != p.size() / 3 || (!c.empty() && c.size() / 4 != p.size() / 3))
        return false;
    auto* indices = o->at("indices").getIf<Value::Array>();
    auto* groups  = o->at("groups").getIf<Value::Array>();
    if (!indices || indices->size() % 3 || indices->size() > kMaxVertices * 6 || !groups ||
        groups->size() != indices->size() / 3)
        return false;
    MeshBuild m;
    for (size_t x = 0; x < p.size() / 3; ++x)
        m.addVertex(p[x * 3], p[x * 3 + 1], p[x * 3 + 2], n[x * 3], n[x * 3 + 1], n[x * 3 + 2], u[x * 2], u[x * 2 + 1]);
    if (!c.empty() && !m.setVertexColors(std::move(c)).ok()) return false;
    for (size_t t = 0; t < groups->size(); ++t) {
        auto* name = (*groups)[t].getIf<std::string>();
        if (!name || name->size() > 1024) return false;
        uint32_t ids[3];
        for (int corner = 0; corner < 3; ++corner) {
            auto* id = (*indices)[t * 3 + corner].getIf<int64_t>();
            if (!id || *id < 0 || *id >= int64_t(p.size() / 3)) return false;
            ids[corner] = uint32_t(*id);
        }
        if (ids[0] == ids[1] || ids[1] == ids[2] || ids[0] == ids[2]) return false;
        m.setActiveGroup(*name);
        m.addTriangle(ids[0], ids[1], ids[2]);
    }
    out = std::move(m);
    return true;
}
}  // namespace

Result<std::string> GtsTerrainMeshSettings::snapshotJson() const {
    Value::Array qualities;
    for (float quality : lodQuality_) qualities.emplace_back(quality);
    return Value(Value::Object{{"lodCount", lodCount_},
                               {"lodQuality", std::move(qualities)},
                               {"saveResolution", saveResolution_},
                               {"schema", "eve.procgen.gts-terrain-mesh-settings"},
                               {"subTiles", subTiles_},
                               {"version", 1}})
        .toJson();
}

Result<void> GtsTerrainMeshSettings::restoreJson(const std::string& json) {
    if (json.size() > 64U * 1024U)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "GTS terrain mesh settings JSON exceeds size limit",
                                                       "procgen.gtsTerrainLod.codec"));
    auto parsed = Value::fromJson(json);
    if (!parsed) return Result<void>::failure(parsed.status());
    auto* root = parsed.value().getIf<Value::Object>();
    if (!root || !exact(*root, {"lodCount", "lodQuality", "saveResolution", "schema", "subTiles", "version"}))
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "GTS terrain mesh settings fields are invalid",
                                                       "procgen.gtsTerrainLod.codec"));
    auto* schema     = root->at("schema").getIf<std::string>();
    auto* version    = root->at("version").getIf<int64_t>();
    auto* resolution = root->at("saveResolution").getIf<int64_t>();
    auto* count      = root->at("lodCount").getIf<int64_t>();
    auto* subTiles   = root->at("subTiles").getIf<int64_t>();
    auto* qualities  = root->at("lodQuality").getIf<Value::Array>();
    if (!schema || *schema != "eve.procgen.gts-terrain-mesh-settings" || !version || *version != 1 || !resolution ||
        *resolution < 0 || *resolution > 4 || !count || *count < 1 || *count > 4 || !subTiles || *subTiles < 0 ||
        *subTiles > 5 || !qualities || qualities->size() != 4)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "GTS terrain mesh settings schema or values are invalid",
                                                       "procgen.gtsTerrainLod.codec"));
    GtsTerrainMeshSettings candidate;
    auto                   a = candidate.setSaveResolution(int(*resolution));
    if (!a) return a;
    auto b = candidate.setLodCount(int(*count));
    if (!b) return b;
    auto c = candidate.setSubTiles(int(*subTiles));
    if (!c) return c;
    for (int i = 0; i < 4; ++i) {
        auto* quality = num((*qualities)[size_t(i)]);
        if (!quality)
            return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                           "GTS terrain mesh LOD quality is invalid",
                                                           "procgen.gtsTerrainLod.codec"));
        auto set = candidate.setLodQuality(i, float(*quality));
        if (!set) return set;
    }
    *this = std::move(candidate);
    return Result<void>::success();
}

Result<std::string> GtsTerrainLodSet::snapshotJson() const {
    if (tiles_.size() > kMaxTiles || settings_.empty() || settings_.size() > kMaxLevels)
        return Result<std::string>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                              "GTS terrain LOD set exceeds codec limits",
                                                              "procgen.gtsTerrainLod.codec"));
    Value::Array settings, tiles;
    for (auto& s : settings_)
        settings.emplace_back(Value::Object{{"options", options(s.simplification)},
                                            {"quality", s.quality},
                                            {"transitionHeight", s.screenRelativeTransitionHeight}});
    for (auto& t : tiles_) {
        Value::Array levels;
        for (auto& m : t.levels) levels.emplace_back(encodeMesh(m));
        tiles.emplace_back(
            Value::Object{{"levels", std::move(levels)}, {"offsetX", t.offsetX}, {"offsetZ", t.offsetZ}});
    }
    auto json = Value(Value::Object{{"columns", columns_},
                                    {"rows", rows_},
                                    {"schema", "eve.procgen.gts-terrain-lod"},
                                    {"settings", std::move(settings)},
                                    {"tiles", std::move(tiles)},
                                    {"version", 2}})
                    .toJson();
    if (!json) return json;
    if (json.value().size() > kMaxJsonBytes)
        return Result<std::string>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "GTS terrain LOD JSON exceeds size limit", "procgen.gtsTerrainLod.codec"));
    return json;
}

Result<void> GtsTerrainLodSet::restoreJson(const std::string& json) {
    if (json.size() > kMaxJsonBytes)
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "GTS terrain LOD JSON exceeds size limit", "procgen.gtsTerrainLod.codec"));
    auto parsed = Value::fromJson(json);
    if (!parsed) return Result<void>::failure(parsed.status());
    auto* root = parsed.value().getIf<Value::Object>();
    if (!root || !exact(*root, {"columns", "rows", "schema", "settings", "tiles", "version"}))
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "GTS terrain LOD root fields are invalid", "procgen.gtsTerrainLod.codec"));
    auto* schema   = root->at("schema").getIf<std::string>();
    auto* version  = root->at("version").getIf<int64_t>();
    auto* columns  = root->at("columns").getIf<int64_t>();
    auto* rows     = root->at("rows").getIf<int64_t>();
    auto* settings = root->at("settings").getIf<Value::Array>();
    auto* tiles    = root->at("tiles").getIf<Value::Array>();
    if (!schema || *schema != "eve.procgen.gts-terrain-lod" || !version || (*version != 1 && *version != 2) ||
        !columns || !rows || *columns <= 0 || *rows <= 0 || *columns > 65536 || *rows > 65536 || !settings ||
        settings->empty() || settings->size() > kMaxLevels || !tiles || tiles->size() > kMaxTiles ||
        uint64_t(*columns) * uint64_t(*rows) != tiles->size())
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "GTS terrain LOD schema, layout or counts are invalid",
                                                       "procgen.gtsTerrainLod.codec"));
    GtsTerrainLodSet candidate;
    candidate.columns_ = int(*columns);
    candidate.rows_    = int(*rows);
    float previous     = 1;
    for (auto& encoded : *settings) {
        auto* o = encoded.getIf<Value::Object>();
        if (!o || !exact(*o, {"options", "quality", "transitionHeight"}))
            return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                           "GTS terrain LOD setting fields are invalid",
                                                           "procgen.gtsTerrainLod.codec"));
        auto *                     q = num(o->at("quality")), *h = num(o->at("transitionHeight"));
        GtsTerrainLodLevelSettings s;
        if (!q || !h || !std::isfinite(*q) || *q < 0 || *q > 1 || !std::isfinite(*h) || *h < 0 || *h >= previous ||
            !decodeOptions(o->at("options"), s.simplification))
            return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                           "GTS terrain LOD setting values are invalid",
                                                           "procgen.gtsTerrainLod.codec"));
        s.quality                        = float(*q);
        s.screenRelativeTransitionHeight = float(*h);
        previous                         = s.screenRelativeTransitionHeight;
        candidate.settings_.push_back(s);
    }
    for (auto& encoded : *tiles) {
        auto* o = encoded.getIf<Value::Object>();
        if (!o || !exact(*o, {"levels", "offsetX", "offsetZ"}))
            return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                           "GTS terrain LOD tile fields are invalid",
                                                           "procgen.gtsTerrainLod.codec"));
        auto *x = num(o->at("offsetX")), *z = num(o->at("offsetZ"));
        auto* levels = o->at("levels").getIf<Value::Array>();
        if (!x || !z || !std::isfinite(*x) || !std::isfinite(*z) || !levels ||
            levels->size() != candidate.settings_.size())
            return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                           "GTS terrain LOD tile values are invalid",
                                                           "procgen.gtsTerrainLod.codec"));
        GtsTerrainLodTile tile;
        tile.offsetX = float(*x);
        tile.offsetZ = float(*z);
        for (auto& m : *levels) {
            MeshBuild decoded;
            if (!decodeMesh(m, decoded, *version == 1))
                return Result<void>::failure(Diagnostic::error(
                    DiagnosticCode::InvalidArgument, "GTS terrain LOD mesh is invalid", "procgen.gtsTerrainLod.codec"));
            tile.levels.push_back(std::move(decoded));
        }
        candidate.tiles_.push_back(std::move(tile));
    }
    *this = std::move(candidate);
    return Result<void>::success();
}
}  // namespace eve::procgen

#include "procgen/GtsTerrainExportSettings.h"

#include "common/Value.h"
#include "image/ImageData.h"
#include "procgen/heightmap/Heightmap.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <utility>

namespace eve::procgen {
namespace {
bool enumRange(int value, int last) { return value >= 0 && value <= last; }
bool textureResolutionValid(int value) {
    for (int candidate = 32; candidate <= 8192; candidate *= 2)
        if (value == candidate) return true;
    return false;
}
Result<void> validateList(const std::vector<GtsTerrainExportLodSettings>& levels) {
    if (levels.size() > 32)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "GTS terrain export supports at most 32 LOD records",
                                                       "procgen.gtsTerrainExportSettings"));
    float previous = 1.0f;
    for (const auto& level : levels) {
        auto valid = level.validate();
        if (!valid) return valid;
        if (level.screenRelativeTransitionHeight >= previous)
            return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                           "GTS terrain export transition heights must descend",
                                                           "procgen.gtsTerrainExportSettings"));
        previous = level.screenRelativeTransitionHeight;
    }
    return Result<void>::success();
}
Value encodeOptions(const GtsMeshSimplificationOptions& value) {
    return Value::Object{{"aggressiveness", value.aggressiveness},
                         {"enableSmartLink", value.enableSmartLink},
                         {"maxIterationCount", value.maxIterationCount},
                         {"preserveBorderEdges", value.preserveBorderEdges},
                         {"preserveSurfaceCurvature", value.preserveSurfaceCurvature},
                         {"preserveUvFoldoverEdges", value.preserveUvFoldoverEdges},
                         {"preserveUvSeamEdges", value.preserveUvSeamEdges},
                         {"vertexLinkDistance", value.vertexLinkDistance}};
}
Value encodeLod(const GtsTerrainExportLodSettings& value) {
    return Value::Object{{"alphaChannel", int(value.alphaChannel)},
                         {"bakeLayerMask", int64_t(value.bakeLayerMask)},
                         {"bakeLighting", int(value.bakeLighting)},
                         {"bakeVertexColors", value.bakeVertexColors},
                         {"captureBaseMapTextures", value.captureBaseMapTextures},
                         {"createMaterials", value.createMaterials},
                         {"exportNormalMaps", value.exportNormalMaps},
                         {"exportSplatmaps", value.exportSplatmaps},
                         {"exportTextures", value.exportTextures},
                         {"materialShader", int(value.materialShader)},
                         {"mode", int(value.mode)},
                         {"namePrefix", value.namePrefix},
                         {"normalEdgeMode", int(value.normalEdgeMode)},
                         {"saveResolution", int(value.saveResolution)},
                         {"screenRelativeTransitionHeight", value.screenRelativeTransitionHeight},
                         {"simplification", encodeOptions(value.simplification)},
                         {"simplifyQuality", value.simplifyQuality},
                         {"textureExportMethod", int(value.textureExportMethod)},
                         {"textureExportResolution", value.textureExportResolution},
                         {"vertexColorSmoothing", value.vertexColorSmoothing}};
}
Value encodeWorkflow(const GtsTerrainExportWorkflow& value) {
    return Value::Object{{"action", int(value.action)},
                         {"addMeshColliderToImpostor", value.addMeshColliderToImpostor},
                         {"addObjectColliders", value.addObjectColliders},
                         {"addTerrainCollider", value.addTerrainCollider},
                         {"addTreeColliders", value.addTreeColliders},
                         {"bakeCombinedCollisionMesh", value.bakeCombinedCollisionMesh},
                         {"colliderResolution", int(value.colliderResolution)},
                         {"colliderSimplifyQuality", value.colliderSimplifyQuality},
                         {"colliderType", int(value.colliderType)},
                         {"convertSourceTerrains", value.convertSourceTerrains},
                         {"convertTreesToObjects", value.convertTreesToObjects},
                         {"copyPcgObjects", value.copyPcgObjects},
                         {"copyPcgObjectsToImpostor", value.copyPcgObjectsToImpostor},
                         {"createColliderScenes", value.createColliderScenes},
                         {"createImpostorScenes", value.createImpostorScenes},
                         {"impostorRange", value.impostorRange},
                         {"invertExportMask", value.invertExportMask},
                         {"objFaceMode", int(value.objFaceMode)},
                         {"selection", int(value.selection)},
                         {"sourceTreatment", int(value.sourceTreatment)}};
}
bool exportSettingsExact(const Value::Object& object, std::initializer_list<const char*> names) {
    if (object.size() != names.size()) return false;
    for (auto* name : names)
        if (!object.contains(name)) return false;
    return true;
}
const double* number(const Value& value) { return value.getIf<double>(); }
bool          exportSettingsDecodeOptions(const Value& encoded, GtsMeshSimplificationOptions& output) {
    auto* object = encoded.getIf<Value::Object>();
    if (!object ||
        !exportSettingsExact(*object, {"aggressiveness", "enableSmartLink", "maxIterationCount", "preserveBorderEdges",
                                                "preserveSurfaceCurvature", "preserveUvFoldoverEdges", "preserveUvSeamEdges",
                                                "vertexLinkDistance"}))
        return false;
    auto* aggressiveness = number(object->at("aggressiveness"));
    auto* distance       = number(object->at("vertexLinkDistance"));
    auto* iterations     = object->at("maxIterationCount").getIf<int64_t>();
    auto* smart          = object->at("enableSmartLink").getIf<bool>();
    auto* border         = object->at("preserveBorderEdges").getIf<bool>();
    auto* curvature      = object->at("preserveSurfaceCurvature").getIf<bool>();
    auto* foldover       = object->at("preserveUvFoldoverEdges").getIf<bool>();
    auto* seam           = object->at("preserveUvSeamEdges").getIf<bool>();
    if (!aggressiveness || !distance || !iterations || !smart || !border || !curvature || !foldover || !seam)
        return false;
    output.aggressiveness           = *aggressiveness;
    output.vertexLinkDistance       = *distance;
    output.maxIterationCount        = int(*iterations);
    output.enableSmartLink          = *smart;
    output.preserveBorderEdges      = *border;
    output.preserveSurfaceCurvature = *curvature;
    output.preserveUvFoldoverEdges  = *foldover;
    output.preserveUvSeamEdges      = *seam;
    return true;
}
bool decodeLod(const Value& encoded, GtsTerrainExportLodSettings& output) {
    auto* object = encoded.getIf<Value::Object>();
    if (!object || !exportSettingsExact(*object, {"alphaChannel",
                                                  "bakeLayerMask",
                                                  "bakeLighting",
                                                  "bakeVertexColors",
                                                  "captureBaseMapTextures",
                                                  "createMaterials",
                                                  "exportNormalMaps",
                                                  "exportSplatmaps",
                                                  "exportTextures",
                                                  "materialShader",
                                                  "mode",
                                                  "namePrefix",
                                                  "normalEdgeMode",
                                                  "saveResolution",
                                                  "screenRelativeTransitionHeight",
                                                  "simplification",
                                                  "simplifyQuality",
                                                  "textureExportMethod",
                                                  "textureExportResolution",
                                                  "vertexColorSmoothing"}))
        return false;
    auto  integer           = [&](const char* name) { return object->at(name).getIf<int64_t>(); };
    auto  boolean           = [&](const char* name) { return object->at(name).getIf<bool>(); };
    auto* alpha             = integer("alphaChannel");
    auto* mask              = integer("bakeLayerMask");
    auto* lighting          = integer("bakeLighting");
    auto* shader            = integer("materialShader");
    auto* mode              = integer("mode");
    auto* edge              = integer("normalEdgeMode");
    auto* resolution        = integer("saveResolution");
    auto* textureMethod     = integer("textureExportMethod");
    auto* textureResolution = integer("textureExportResolution");
    auto* smoothing         = integer("vertexColorSmoothing");
    auto* transition        = number(object->at("screenRelativeTransitionHeight"));
    auto* quality           = number(object->at("simplifyQuality"));
    auto* prefix            = object->at("namePrefix").getIf<std::string>();
    auto* vertexColors      = boolean("bakeVertexColors");
    auto* capture           = boolean("captureBaseMapTextures");
    auto* materials         = boolean("createMaterials");
    auto* normals           = boolean("exportNormalMaps");
    auto* splats            = boolean("exportSplatmaps");
    auto* textures          = boolean("exportTextures");
    if (!alpha || *alpha < 0 || *alpha > 1 || !mask || *mask < 0 || *mask > std::numeric_limits<std::uint32_t>::max() ||
        !lighting || *lighting < 0 || *lighting > 1 || !shader || *shader < 0 || *shader > 1 || !mode || *mode < 0 ||
        *mode > 2 || !edge || *edge < 0 || *edge > 1 || !resolution || *resolution < 0 || *resolution > 4 ||
        !textureMethod || *textureMethod < 0 || *textureMethod > 1 || !textureResolution || *textureResolution < 32 ||
        *textureResolution > 8192 || !smoothing || *smoothing < 0 || *smoothing > 128 || !transition || !quality ||
        !prefix || prefix->size() > 256 || !vertexColors || !capture || !materials || !normals || !splats ||
        !textures || !exportSettingsDecodeOptions(object->at("simplification"), output.simplification))
        return false;
    output.alphaChannel                   = static_cast<GtsTerrainAlphaChannel>(*alpha);
    output.bakeLayerMask                  = std::uint32_t(*mask);
    output.bakeLighting                   = static_cast<GtsTerrainBakeLighting>(*lighting);
    output.materialShader                 = static_cast<GtsTerrainExportShader>(*shader);
    output.mode                           = static_cast<GtsTerrainLodMode>(*mode);
    output.normalEdgeMode                 = static_cast<GtsTerrainNormalEdgeMode>(*edge);
    output.saveResolution                 = static_cast<GtsTerrainSaveResolution>(*resolution);
    output.textureExportMethod            = static_cast<GtsTerrainTextureExportMethod>(*textureMethod);
    output.textureExportResolution        = int(*textureResolution);
    output.vertexColorSmoothing           = int(*smoothing);
    output.screenRelativeTransitionHeight = float(*transition);
    output.simplifyQuality                = float(*quality);
    output.namePrefix                     = *prefix;
    output.bakeVertexColors               = *vertexColors;
    output.captureBaseMapTextures         = *capture;
    output.createMaterials                = *materials;
    output.exportNormalMaps               = *normals;
    output.exportSplatmaps                = *splats;
    output.exportTextures                 = *textures;
    return output.validate().ok();
}
bool decodeWorkflow(const Value& encoded, GtsTerrainExportWorkflow& output, bool legacyV2 = false) {
    auto* object = encoded.getIf<Value::Object>();
    if (!object ||
        (!exportSettingsExact(*object, {"action",
                                        "addMeshColliderToImpostor",
                                        "addObjectColliders",
                                        "addTerrainCollider",
                                        "addTreeColliders",
                                        "bakeCombinedCollisionMesh",
                                        "colliderResolution",
                                        "colliderSimplifyQuality",
                                        "colliderType",
                                        "convertSourceTerrains",
                                        "convertTreesToObjects",
                                        "copyPcgObjects",
                                        "copyPcgObjectsToImpostor",
                                        "createColliderScenes",
                                        "createImpostorScenes",
                                        "impostorRange",
                                        "invertExportMask",
                                        "objFaceMode",
                                        "selection",
                                        "sourceTreatment"}) &&
         !(legacyV2 &&
           exportSettingsExact(
               *object, {"action", "addMeshColliderToImpostor", "addObjectColliders", "addTerrainCollider",
                         "addTreeColliders", "bakeCombinedCollisionMesh", "colliderResolution",
                         "colliderSimplifyQuality", "colliderType", "convertSourceTerrains", "convertTreesToObjects",
                         "copyPcgObjects", "copyPcgObjectsToImpostor", "createColliderScenes", "createImpostorScenes",
                         "impostorRange", "invertExportMask", "selection", "sourceTreatment"}))))
        return false;
    auto  integer = [&](const char* name) { return object->at(name).getIf<int64_t>(); };
    auto  boolean = [&](const char* name) { return object->at(name).getIf<bool>(); };
    auto *action = integer("action"), *resolution = integer("colliderResolution"), *type = integer("colliderType"),
         *selection = integer("selection"), *treatment = integer("sourceTreatment");
    const int64_t defaultFace = 0;
    auto*         face        = legacyV2 ? &defaultFace : integer("objFaceMode");
    auto*         quality     = number(object->at("colliderSimplifyQuality"));
    auto*         range       = number(object->at("impostorRange"));
    auto *        addImpostor = boolean("addMeshColliderToImpostor"), *addObjects = boolean("addObjectColliders"),
         *addTerrain = boolean("addTerrainCollider"), *addTrees = boolean("addTreeColliders"),
         *combined = boolean("bakeCombinedCollisionMesh"), *convert = boolean("convertSourceTerrains"),
         *convertTrees = boolean("convertTreesToObjects"), *copy = boolean("copyPcgObjects"),
         *copyImpostor = boolean("copyPcgObjectsToImpostor"), *colliderScenes = boolean("createColliderScenes"),
         *impostorScenes = boolean("createImpostorScenes"), *invert = boolean("invertExportMask");
    if (!action || *action < 0 || *action > 2 || !resolution || *resolution < 0 || *resolution > 4 || !type ||
        *type < 0 || *type > 1 || !selection || *selection < 0 || *selection > 1 || !treatment || *treatment < 0 ||
        *treatment > 3 || !face || *face < 0 || *face > 1 || !quality || !range || !std::isfinite(*quality) ||
        *quality < 0 || *quality > 1 || !std::isfinite(*range) || *range < 0 || !addImpostor || !addObjects ||
        !addTerrain || !addTrees || !combined || !convert || !convertTrees || !copy || !copyImpostor ||
        !colliderScenes || !impostorScenes || !invert)
        return false;
    output.action                    = static_cast<GtsTerrainConversionAction>(*action);
    output.colliderResolution        = static_cast<GtsTerrainSaveResolution>(*resolution);
    output.colliderType              = static_cast<GtsTerrainColliderType>(*type);
    output.selection                 = static_cast<GtsTerrainExportSelection>(*selection);
    output.objFaceMode               = static_cast<GtsTerrainObjFaceMode>(*face);
    output.sourceTreatment           = static_cast<GtsSourceTerrainTreatment>(*treatment);
    output.colliderSimplifyQuality   = float(*quality);
    output.impostorRange             = *range;
    output.addMeshColliderToImpostor = *addImpostor;
    output.addObjectColliders        = *addObjects;
    output.addTerrainCollider        = *addTerrain;
    output.addTreeColliders          = *addTrees;
    output.bakeCombinedCollisionMesh = *combined;
    output.convertSourceTerrains     = *convert;
    output.convertTreesToObjects     = *convertTrees;
    output.copyPcgObjects            = *copy;
    output.copyPcgObjectsToImpostor  = *copyImpostor;
    output.createColliderScenes      = *colliderScenes;
    output.createImpostorScenes      = *impostorScenes;
    output.invertExportMask          = *invert;
    return true;
}
}  // namespace

Result<void> GtsTerrainExportLodSettings::validate() const {
    if (!enumRange(int(saveResolution), 4) || !enumRange(int(normalEdgeMode), 1) || !enumRange(int(mode), 2) ||
        !enumRange(int(materialShader), 1) || !enumRange(int(textureExportMethod), 1) ||
        !enumRange(int(alphaChannel), 1) || !enumRange(int(bakeLighting), 1))
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "GTS terrain export enum value is invalid",
                                                       "procgen.gtsTerrainExportSettings"));
    if (!std::isfinite(simplifyQuality) || simplifyQuality < 0.0f || simplifyQuality > 1.0f ||
        !std::isfinite(screenRelativeTransitionHeight) || screenRelativeTransitionHeight < 0.0f ||
        screenRelativeTransitionHeight >= 1.0f || vertexColorSmoothing < 0 || vertexColorSmoothing > 128 ||
        !textureResolutionValid(textureExportResolution) || namePrefix.size() > 256 ||
        !std::isfinite(simplification.vertexLinkDistance) || simplification.vertexLinkDistance < 0.0 ||
        !std::isfinite(simplification.aggressiveness) || simplification.aggressiveness <= 0.0 ||
        simplification.maxIterationCount <= 0 || simplification.maxIterationCount > 100000)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "GTS terrain export LOD value is outside its supported range",
                                                       "procgen.gtsTerrainExportSettings"));
    return Result<void>::success();
}
Result<GtsTerrainLodLevelSettings> GtsTerrainExportLodSettings::compileLevel() const {
    auto valid = validate();
    if (!valid) return Result<GtsTerrainLodLevelSettings>::failure(valid.status());
    return Result<GtsTerrainLodLevelSettings>::success(
        {simplifyQuality, screenRelativeTransitionHeight, simplification});
}
Result<GtsTerrainExportLodSettings> makeGtsTerrainImpostorLod(int level) {
    if (level < 0)
        return Result<GtsTerrainExportLodSettings>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "GTS impostor LOD index must be non-negative",
                              "procgen.gtsTerrainExportSettings"));
    GtsTerrainExportLodSettings value;
    value.namePrefix          = "LOD" + std::to_string(level) + "_";
    value.mode                = GtsTerrainLodMode::Impostor;
    value.normalEdgeMode      = GtsTerrainNormalEdgeMode::Smooth;
    value.textureExportMethod = GtsTerrainTextureExportMethod::OrthographicBake;
    if (level == 0) {
        value.saveResolution          = GtsTerrainSaveResolution::Half;
        value.textureExportResolution = 2048;
    } else if (level == 1) {
        value.saveResolution          = GtsTerrainSaveResolution::Quarter;
        value.textureExportResolution = 1024;
    } else if (level == 2) {
        value.saveResolution          = GtsTerrainSaveResolution::Eighth;
        value.textureExportResolution = 512;
    } else {
        value.saveResolution          = GtsTerrainSaveResolution::Sixteenth;
        value.textureExportResolution = 256;
    }
    value.bakeVertexColors = false;
    value.alphaChannel     = GtsTerrainAlphaChannel::None;
    value.exportSplatmaps  = false;
    value.materialShader   = GtsTerrainExportShader::Standard;
    return Result<GtsTerrainExportLodSettings>::success(std::move(value));
}
Result<GtsTerrainExportLodSettings> makeGtsTerrainLowPolyLod(int level) {
    if (level < 0)
        return Result<GtsTerrainExportLodSettings>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "GTS low-poly LOD index must be non-negative",
                              "procgen.gtsTerrainExportSettings"));
    GtsTerrainExportLodSettings value;
    value.namePrefix           = "LOD" + std::to_string(level) + "_";
    value.mode                 = GtsTerrainLodMode::LowPoly;
    value.saveResolution       = level == 0 ? GtsTerrainSaveResolution::Eighth : GtsTerrainSaveResolution::Sixteenth;
    value.normalEdgeMode       = GtsTerrainNormalEdgeMode::Sharp;
    value.textureExportMethod  = GtsTerrainTextureExportMethod::BaseMapExport;
    value.bakeVertexColors     = true;
    value.vertexColorSmoothing = 3;
    value.alphaChannel         = GtsTerrainAlphaChannel::None;
    value.exportNormalMaps     = false;
    value.exportSplatmaps      = false;
    value.materialShader       = GtsTerrainExportShader::VertexColor;
    return Result<GtsTerrainExportLodSettings>::success(std::move(value));
}
namespace {
Result<GtsTerrainExportLodSettings> preset(int mode, int level, float quality, float transition) {
    Result<GtsTerrainExportLodSettings> made =
        mode == int(GtsTerrainLodMode::Impostor) ? makeGtsTerrainImpostorLod(level)
        : mode == int(GtsTerrainLodMode::LowPoly)
            ? makeGtsTerrainLowPolyLod(level)
            : Result<GtsTerrainExportLodSettings>::failure(Diagnostic::error(
                  DiagnosticCode::InvalidArgument, "GTS terrain preset mode must be Impostor or LowPoly",
                  "procgen.gtsTerrainExportSettings"));
    if (!made) return made;
    made.value().simplifyQuality                = quality;
    made.value().screenRelativeTransitionHeight = transition;
    auto valid                                  = made.value().validate();
    if (!valid) return Result<GtsTerrainExportLodSettings>::failure(valid.status());
    return made;
}
}  // namespace
Result<void> GtsTerrainExportSettings::appendSourcePreset(int mode, int level, float quality, float transitionHeight) {
    auto made = preset(mode, level, quality, transitionHeight);
    if (!made) return Result<void>::failure(made.status());
    auto candidate = sourceLods_;
    candidate.push_back(std::move(made).takeValue());
    return setSourceLods(std::move(candidate));
}
Result<void> GtsTerrainExportSettings::appendImpostorPreset(int mode, int level, float quality,
                                                            float transitionHeight) {
    auto made = preset(mode, level, quality, transitionHeight);
    if (!made) return Result<void>::failure(made.status());
    auto candidate = impostorLods_;
    candidate.push_back(std::move(made).takeValue());
    return setImpostorLods(std::move(candidate));
}
Result<void> GtsTerrainExportSettings::setSourceLods(std::vector<GtsTerrainExportLodSettings> levels) {
    auto valid = validateList(levels);
    if (!valid) return valid;
    sourceLods_ = std::move(levels);
    return Result<void>::success();
}
Result<void> GtsTerrainExportSettings::configureWorkflow(const GtsTerrainExportWorkflow& workflow) {
    if (!enumRange(int(workflow.action), 2) || !enumRange(int(workflow.sourceTreatment), 3) ||
        !enumRange(int(workflow.selection), 1) || !enumRange(int(workflow.colliderType), 1) ||
        !enumRange(int(workflow.objFaceMode), 1) || !enumRange(int(workflow.colliderResolution), 4) ||
        !std::isfinite(workflow.colliderSimplifyQuality) || workflow.colliderSimplifyQuality < 0.f ||
        workflow.colliderSimplifyQuality > 1.f || !std::isfinite(workflow.impostorRange) ||
        workflow.impostorRange < 0.0)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "GTS terrain export workflow value is invalid",
                                                       "procgen.gtsTerrainExportSettings"));
    workflow_ = workflow;
    return Result<void>::success();
}
GtsTerrainExportWorkflow GtsTerrainExportSettings::getWorkflow() const noexcept { return workflow_; }
Result<void>             GtsTerrainExportSettings::setImpostorLods(std::vector<GtsTerrainExportLodSettings> levels) {
    auto valid = validateList(levels);
    if (!valid) return valid;
    impostorLods_ = std::move(levels);
    return Result<void>::success();
}
int GtsTerrainExportSettings::getSourceLodCount() const noexcept { return int(sourceLods_.size()); }
int GtsTerrainExportSettings::getImpostorLodCount() const noexcept { return int(impostorLods_.size()); }
const GtsTerrainExportLodSettings* GtsTerrainExportSettings::sourceLodAt(int index) const noexcept {
    return index >= 0 && index < int(sourceLods_.size()) ? &sourceLods_[std::size_t(index)] : nullptr;
}
const GtsTerrainExportLodSettings* GtsTerrainExportSettings::impostorLodAt(int index) const noexcept {
    return index >= 0 && index < int(impostorLods_.size()) ? &impostorLods_[std::size_t(index)] : nullptr;
}
Result<std::vector<GtsTerrainLodLevelSettings>> GtsTerrainExportSettings::compileSourceLevels() const {
    auto valid = validateList(sourceLods_);
    if (!valid) return Result<std::vector<GtsTerrainLodLevelSettings>>::failure(valid.status());
    if (sourceLods_.empty())
        return Result<std::vector<GtsTerrainLodLevelSettings>>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "GTS source export requires at least one LOD",
                              "procgen.gtsTerrainExportSettings"));
    std::vector<GtsTerrainLodLevelSettings> output;
    output.reserve(sourceLods_.size());
    for (const auto& source : sourceLods_) {
        auto level = source.compileLevel();
        if (!level) return Result<std::vector<GtsTerrainLodLevelSettings>>::failure(level.status());
        output.push_back(std::move(level).takeValue());
    }
    return Result<std::vector<GtsTerrainLodLevelSettings>>::success(std::move(output));
}
Result<std::string> GtsTerrainExportSettings::snapshotJson() const {
    auto sourceValid = validateList(sourceLods_);
    if (!sourceValid) return Result<std::string>::failure(sourceValid.status());
    auto impostorValid = validateList(impostorLods_);
    if (!impostorValid) return Result<std::string>::failure(impostorValid.status());
    Value::Array source, impostor;
    for (const auto& level : sourceLods_) source.emplace_back(encodeLod(level));
    for (const auto& level : impostorLods_) impostor.emplace_back(encodeLod(level));
    return Value(Value::Object{{"impostorLods", std::move(impostor)},
                               {"schema", "eve.procgen.gts-terrain-export-settings"},
                               {"sourceLods", std::move(source)},
                               {"version", 3},
                               {"workflow", encodeWorkflow(workflow_)}})
        .toJson();
}
Result<void> GtsTerrainExportSettings::restoreJson(const std::string& json) {
    if (json.size() > 1024U * 1024U)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "GTS terrain export settings JSON exceeds size limit",
                                                       "procgen.gtsTerrainExportSettings"));
    auto parsed = Value::fromJson(json);
    if (!parsed) return Result<void>::failure(parsed.status());
    auto* root = parsed.value().getIf<Value::Object>();
    if (!root || !root->contains("schema") || !root->contains("version") || !root->contains("sourceLods") ||
        !root->contains("impostorLods"))
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "GTS terrain export settings fields are invalid",
                                                       "procgen.gtsTerrainExportSettings"));
    auto* schema  = root->at("schema").getIf<std::string>();
    auto* version = root->at("version").getIf<int64_t>();
    if (!version || (*version < 1 || *version > 3) ||
        (*version == 1 && !exportSettingsExact(*root, {"impostorLods", "schema", "sourceLods", "version"})) ||
        (*version >= 2 && !exportSettingsExact(*root, {"impostorLods", "schema", "sourceLods", "version", "workflow"})))
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "GTS terrain export settings fields or version are invalid",
                                                       "procgen.gtsTerrainExportSettings"));
    auto* source   = root->at("sourceLods").getIf<Value::Array>();
    auto* impostor = root->at("impostorLods").getIf<Value::Array>();
    if (!schema || *schema != "eve.procgen.gts-terrain-export-settings" || !source || !impostor ||
        source->size() > 32 || impostor->size() > 32)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "GTS terrain export settings schema or counts are invalid",
                                                       "procgen.gtsTerrainExportSettings"));
    GtsTerrainExportWorkflow decodedWorkflow;
    if (*version >= 2 && !decodeWorkflow(root->at("workflow"), decodedWorkflow, *version == 2))
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "GTS terrain export workflow is invalid",
                                                       "procgen.gtsTerrainExportSettings"));
    std::vector<GtsTerrainExportLodSettings> decodedSource, decodedImpostor;
    for (const auto& encoded : *source) {
        GtsTerrainExportLodSettings value;
        if (!decodeLod(encoded, value))
            return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                           "GTS source export LOD is invalid",
                                                           "procgen.gtsTerrainExportSettings"));
        decodedSource.push_back(std::move(value));
    }
    for (const auto& encoded : *impostor) {
        GtsTerrainExportLodSettings value;
        if (!decodeLod(encoded, value))
            return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                           "GTS impostor export LOD is invalid",
                                                           "procgen.gtsTerrainExportSettings"));
        decodedImpostor.push_back(std::move(value));
    }
    auto sourceValid = validateList(decodedSource);
    if (!sourceValid) return sourceValid;
    auto impostorValid = validateList(decodedImpostor);
    if (!impostorValid) return impostorValid;
    auto workflowValid = configureWorkflow(decodedWorkflow);
    if (!workflowValid) return workflowValid;
    sourceLods_   = std::move(decodedSource);
    impostorLods_ = std::move(decodedImpostor);
    return Result<void>::success();
}


Result<void> buildGtsTerrainExportLodsFromHeightmapInto(GtsTerrainLodSet& output, const Heightmap& heightmap,
                                                        const GtsTerrainExportSettings& settings, float sizeX,
                                                        float sizeY, float sizeZ, int subTiles, GtsMeshPivot pivot) {
    auto levels = settings.compileSourceLevels();
    if (!levels) return Result<void>::failure(levels.status());
    const auto* first = settings.sourceLodAt(0);
    if (!first)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "GTS source export requires at least one LOD",
                                                       "procgen.gtsTerrainExportSettings"));
    MeshBuild base;
    auto      built = buildGtsTerrainBaseMesh(base, heightmap, first->saveResolution, sizeX, sizeY, sizeZ);
    if (!built) return built;
    auto lods = buildGtsTerrainLods(base, subTiles, subTiles, pivot, levels.value());
    if (!lods) return Result<void>::failure(lods.status());
    output = std::move(lods).takeValue();
    return Result<void>::success();
}
Result<void> buildGtsTerrainColliderMeshFromHeightmapInto(MeshBuild& output, const Heightmap& heightmap,
                                                          const GtsTerrainExportWorkflow& workflow, float sizeX,
                                                          float sizeY, float sizeZ) {
    GtsTerrainExportSettings validator;
    auto                     valid = validator.configureWorkflow(workflow);
    if (!valid) return valid;
    if (!workflow.addTerrainCollider || workflow.colliderType != GtsTerrainColliderType::Mesh)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "GTS workflow does not request a terrain MeshCollider",
                                                       "procgen.gtsTerrainExportSettings"));
    MeshBuild base;
    auto      built = buildGtsTerrainBaseMesh(base, heightmap, workflow.colliderResolution, sizeX, sizeY, sizeZ);
    if (!built) return built;
    MeshBuild simplified;
    auto      reduced = simplifyGtsMesh(simplified, base, workflow.colliderSimplifyQuality);
    if (!reduced) return Result<void>::failure(reduced.status());
    output = std::move(simplified);
    return Result<void>::success();
}

Result<std::string> encodeGtsTerrainObj(const Heightmap& heightmap, GtsTerrainSaveResolution resolution, float sizeX,
                                        float sizeY, float sizeZ, GtsTerrainObjFaceMode faceMode) {
    if (!enumRange(int(faceMode), 1))
        return Result<std::string>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                              "GTS terrain OBJ face mode is invalid",
                                                              "procgen.gtsTerrainExportSettings"));
    MeshBuild validated;
    auto      built = buildGtsTerrainBaseMesh(validated, heightmap, resolution, sizeX, sizeY, sizeZ);
    if (!built) return Result<std::string>::failure(built.status());
    const int stride = 1 << int(resolution), width = (heightmap.getWidth() - 1) / stride + 1,
              height = (heightmap.getHeight() - 1) / stride + 1;
    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream << std::setprecision(std::numeric_limits<float>::max_digits10);
    stream << "# Unity terrain OBJ File\n";
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            const float u = float(x * stride) / float(heightmap.getWidth() - 1),
                        v = float(y * stride) / float(heightmap.getHeight() - 1);
            stream << "v " << (-v * sizeZ) << ' ' << (heightmap.height(x * stride, y * stride) * sizeY) << ' '
                   << (u * sizeX) << '\n';
        }
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            const float u = float(x * stride) / float(heightmap.getWidth() - 1),
                        v = float(y * stride) / float(heightmap.getHeight() - 1);
            stream << "vt " << v << ' ' << u << '\n';
        }
    for (int y = 0; y < height - 1; ++y)
        for (int x = 0; x < width - 1; ++x) {
            const int a = y * width + x + 1, b = (y + 1) * width + x + 1, c = (y + 1) * width + x + 2,
                      d = y * width + x + 2;
            if (faceMode == GtsTerrainObjFaceMode::Triangles)
                stream << "f " << a << '/' << a << ' ' << b << '/' << b << ' ' << d << '/' << d << "\nf " << b << '/'
                       << b << ' ' << c << '/' << c << ' ' << d << '/' << d << '\n';
            else
                stream << "f " << a << '/' << a << ' ' << b << '/' << b << ' ' << c << '/' << c << ' ' << d << '/' << d
                       << '\n';
        }
    auto text = stream.str();
    if (text.size() > 512U * 1024U * 1024U)
        return Result<std::string>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "GTS terrain OBJ exceeds size limit", "procgen.gtsTerrainExportSettings"));
    return Result<std::string>::success(std::move(text));
}

Result<std::string> encodeGtsMaskedTerrainObj(const Heightmap& heightmap, const Heightmap& maskmap,
                                              GtsTerrainSaveResolution resolution, float sizeX, float sizeY,
                                              float sizeZ, GtsTerrainObjFaceMode faceMode, float threshold,
                                              bool invert) {
    if (!enumRange(int(faceMode), 1) || !std::isfinite(threshold) || threshold < 0.f || threshold > 1.f)
        return Result<std::string>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                              "GTS masked terrain OBJ settings are invalid",
                                                              "procgen.gtsTerrainExportSettings"));
    MeshBuild validated;
    auto      built = buildGtsTerrainBaseMesh(validated, heightmap, resolution, sizeX, sizeY, sizeZ);
    if (!built) return Result<std::string>::failure(built.status());
    if (maskmap.getWidth() < 1 || maskmap.getHeight() < 1)
        return Result<std::string>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                              "GTS masked terrain OBJ mask is empty",
                                                              "procgen.gtsTerrainExportSettings"));
    const int        terrainResolution = heightmap.getWidth(), stride = 1 << int(resolution);
    std::vector<int> vertexIndices(std::size_t(terrainResolution) * std::size_t(terrainResolution), -1);
    struct Vertex {
        float x, y, z, u, v;
    };
    std::vector<Vertex> vertices;
    std::vector<int>    indices;
    auto                sampleNormalized = [](const Heightmap& map, float x, float z) {
        return map.sampleBilinear(x * float(map.getWidth()), z * float(map.getHeight()));
    };
    auto vertexIndex = [&](int x, int z, float height) {
        auto& index = vertexIndices[std::size_t(z) * std::size_t(terrainResolution) + std::size_t(x)];
        if (index < 0) {
            index = int(vertices.size());
            vertices.push_back({-float(x) * sizeX / float(terrainResolution - 1), height * sizeY,
                                float(z) * sizeZ / float(terrainResolution - 1), float(x) / float(terrainResolution),
                                float(z) / float(terrainResolution)});
        }
        return index;
    };
    auto addTriangle = [&](int a, int b, int c) {
        indices.push_back(c);
        indices.push_back(b);
        indices.push_back(a);
    };
    auto addQuad = [&](int a, int b, int c, int d) {
        indices.push_back(a);
        indices.push_back(c);
        indices.push_back(d);
        indices.push_back(b);
    };
    for (int z = 0; z < terrainResolution - stride; z += stride) {
        const float z0 = float(z) / float(terrainResolution), z1 = float(z + stride) / float(terrainResolution);
        for (int x = 0; x < terrainResolution - stride; x += stride) {
            const float x0 = float(x) / float(terrainResolution), x1 = float(x + stride) / float(terrainResolution);
            const bool  outside = sampleNormalized(maskmap, x0, z0) < threshold;
            if (outside != invert) continue;
            const int v1 = vertexIndex(x, z + stride, sampleNormalized(heightmap, z1, x0));
            const int v2 = vertexIndex(x + stride, z + stride, sampleNormalized(heightmap, z1, x1));
            const int v3 = vertexIndex(x, z, sampleNormalized(heightmap, z0, x0));
            const int v4 = vertexIndex(x + stride, z, sampleNormalized(heightmap, z0, x1));
            if (faceMode == GtsTerrainObjFaceMode::Triangles) {
                addTriangle(v3, v1, v2);
                addTriangle(v3, v2, v4);
            } else
                addQuad(v1, v2, v3, v4);
        }
    }
    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream << std::setprecision(std::numeric_limits<float>::max_digits10);
    stream << "# Unity terrain OBJ File\n";
    for (const auto& vertex : vertices) stream << "v " << vertex.x << ' ' << vertex.y << ' ' << vertex.z << '\n';
    for (const auto& vertex : vertices) stream << "vt " << vertex.u << ' ' << vertex.v << '\n';
    const int faceSize = faceMode == GtsTerrainObjFaceMode::Triangles ? 3 : 4;
    for (std::size_t i = 0; i < indices.size(); i += std::size_t(faceSize)) {
        stream << "f";
        for (int j = 0; j < faceSize; ++j) {
            const int index = indices[i + std::size_t(j)] + 1;
            stream << ' ' << index << '/' << index;
        }
        stream << '\n';
    }
    auto text = stream.str();
    if (text.size() > 512U * 1024U * 1024U)
        return Result<std::string>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                              "GTS masked terrain OBJ exceeds size limit",
                                                              "procgen.gtsTerrainExportSettings"));
    return Result<std::string>::success(std::move(text));
}

Result<int> bakeGtsTerrainVertexColorsInto(MeshBuild& output, const MeshBuild& source,
                                           const image::ImageData& bakedTexture, GtsTerrainNormalEdgeMode edgeMode,
                                           int smoothingIterations, float terrainSizeX, float terrainSizeZ,
                                           bool linearize) {
    if (!enumRange(int(edgeMode), 1) || smoothingIterations < 0 || smoothingIterations > 128 || source.empty() ||
        source.getIndexCount() % 3 || !std::isfinite(terrainSizeX) || terrainSizeX <= 0.f ||
        !std::isfinite(terrainSizeZ) || terrainSizeZ <= 0.f || bakedTexture.getWidth() < 1 ||
        bakedTexture.getHeight() < 1 || !bakedTexture.getData() || !bakedTexture.getPixelGetFunction())
        return Result<int>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                      "GTS terrain vertex-color bake input is invalid",
                                                      "procgen.gtsTerrainExportSettings"));
    const std::size_t pixelSize = bakedTexture.getPixelSize();
    if (!pixelSize || std::size_t(bakedTexture.getWidth()) * std::size_t(bakedTexture.getHeight()) >
                          bakedTexture.getSize() / pixelSize)
        return Result<int>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                      "GTS terrain vertex-color texture storage is invalid",
                                                      "procgen.gtsTerrainExportSettings"));
    using ExportColor              = image::ImageData::Colorf;
    const int                width = bakedTexture.getWidth(), height = bakedTexture.getHeight();
    std::vector<ExportColor> colors;
    colors.reserve(std::size_t(width) * std::size_t(height));
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) colors.push_back(bakedTexture.getPixel(x, y));
    auto safe = [&](const std::vector<ExportColor>& values, int x, int y) {
        x = std::clamp(x, 0, width - 1);
        y = std::clamp(y, 0, height - 1);
        return values[std::size_t(y) * std::size_t(width) + std::size_t(x)];
    };
    for (int iteration = 0; iteration < smoothingIterations; ++iteration) {
        auto next = colors;
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x) {
                const auto a = safe(colors, x, y - 1), b = safe(colors, x, y + 1), c = safe(colors, x - 1, y),
                           d                                               = safe(colors, x + 1, y);
                next[std::size_t(y) * std::size_t(width) + std::size_t(x)] = {
                    (a.r + b.r + c.r + d.r) * .25f, (a.g + b.g + c.g + d.g) * .25f, (a.b + b.b + c.b + d.b) * .25f,
                    (a.a + b.a + c.a + d.a) * .25f};
            }
        colors = std::move(next);
    }
    auto linear = [](float value) {
        return value <= .04045f ? value / 12.92f : std::pow((value + .055f) / 1.055f, 2.4f);
    };
    auto sample = [&](float x, float z) {
        const int   tx    = std::max(0, int(std::round(x / terrainSizeX * float(width))) - 1);
        const int   tz    = std::max(0, int(std::round(z / terrainSizeZ * float(width))) - 1);
        ExportColor color = safe(colors, tx, tz);
        if (linearize) {
            color.r = linear(color.r);
            color.g = linear(color.g);
            color.b = linear(color.b);
        }
        return color;
    };
    MeshBuild          candidate;
    std::vector<float> vertexColors;
    if (edgeMode == GtsTerrainNormalEdgeMode::Smooth) {
        candidate = source;
        vertexColors.reserve(std::size_t(source.getVertexCount()) * 4u);
        for (int vertex = 0; vertex < source.getVertexCount(); ++vertex) {
            const auto color = sample(source.getPositionX(vertex), source.getPositionZ(vertex));
            vertexColors.insert(vertexColors.end(), {color.r, color.g, color.b, color.a});
        }
    } else {
        candidate.reserve(source.getIndexCount(), source.getIndexCount());
        vertexColors.reserve(std::size_t(source.getIndexCount()) * 4u);
        for (int i = 0; i < source.getIndexCount(); i += 3) {
            const int group = source.getTriangleGroup(i / 3);
            candidate.setActiveGroup(group >= 0 ? source.getGroupName(group) : "");
            ExportColor triangle[3];
            for (int corner = 0; corner < 3; ++corner) {
                const int src    = source.getIndex(i + corner);
                triangle[corner] = sample(source.getPositionX(src), source.getPositionZ(src));
                candidate.addVertex(source.getPositionX(src), source.getPositionY(src), source.getPositionZ(src), 0, 0,
                                    0, source.getUvU(src), source.getUvV(src));
            }
            const ExportColor average{(triangle[0].r + triangle[1].r + triangle[2].r) / 3.f,
                                      (triangle[0].g + triangle[1].g + triangle[2].g) / 3.f,
                                      (triangle[0].b + triangle[1].b + triangle[2].b) / 3.f,
                                      (triangle[0].a + triangle[1].a + triangle[2].a) / 3.f};
            for (int corner = 0; corner < 3; ++corner)
                vertexColors.insert(vertexColors.end(), {average.r, average.g, average.b, average.a});
            const std::uint32_t base = std::uint32_t(i);
            candidate.addTriangle(base, base + 1, base + 2);
        }
        auto&       normals   = candidate.normals();
        const auto& positions = candidate.positions();
        for (int i = 0; i < candidate.getIndexCount(); i += 3) {
            const auto a = candidate.indices()[std::size_t(i)], b = candidate.indices()[std::size_t(i + 1)],
                       c    = candidate.indices()[std::size_t(i + 2)];
            const float abx = positions[b * 3] - positions[a * 3], aby = positions[b * 3 + 1] - positions[a * 3 + 1],
                        abz = positions[b * 3 + 2] - positions[a * 3 + 2];
            const float acx = positions[c * 3] - positions[a * 3], acy = positions[c * 3 + 1] - positions[a * 3 + 1],
                        acz = positions[c * 3 + 2] - positions[a * 3 + 2];
            float       nx = aby * acz - abz * acy, ny = abz * acx - abx * acz, nz = abx * acy - aby * acx;
            const float length = std::sqrt(nx * nx + ny * ny + nz * nz);
            if (!(length > 0.f) || !std::isfinite(length))
                return Result<int>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                              "GTS sharp terrain mesh contains a degenerate triangle",
                                                              "procgen.gtsTerrainExportSettings"));
            nx /= length;
            ny /= length;
            nz /= length;
            for (auto vertex : {a, b, c}) {
                normals[vertex * 3]     = nx;
                normals[vertex * 3 + 1] = ny;
                normals[vertex * 3 + 2] = nz;
            }
        }
        for (const auto& [key, value] : source.metadata()) candidate.setMeta(key, value);
    }
    auto applied = candidate.setVertexColors(std::move(vertexColors));
    if (!applied) return Result<int>::failure(applied.status());
    candidate.setMeta("gts.vertexColors", edgeMode == GtsTerrainNormalEdgeMode::Sharp ? "sharp" : "smooth");
    output = std::move(candidate);
    return Result<int>::success(output.getVertexCount());
}
}  // namespace eve::procgen
