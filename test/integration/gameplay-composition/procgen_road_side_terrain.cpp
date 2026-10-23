#include <cmath>
#include <limits>
#include "common/Capability.h"
#include "common/ProcgenAssetBounds.h"
#include "common/ProcgenWorldQuery.h"
#include "procgen/editing/RoadPresetDocument.h"
#include "procgen/road/RoadNetworkCodec.h"
#include "procgen/road/RoadRecipes.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
using namespace eve::procgen;
using namespace eve::procgen::road;
namespace {
struct PlaneQuery : eve::IProcgenWorldQuery {
    int                                 calls = 0, failAt = 0;
    bool                                miss = false, bad = false;
    bool                                slopedNormal = false, invalidNormal = false;
    std::uint64_t                       lastMask = 0;
    float                               lastMin = 0.f, lastMax = 0.f;
    eve::Result<eve::ProcgenSurfaceHit> projectDown(float x, float z, float maxY, float minY,
                                                    std::uint64_t mask) override {
        ++calls;
        lastMask = mask;
        lastMin  = minY;
        lastMax  = maxY;
        if (failAt && calls == failAt)
            return eve::Result<eve::ProcgenSurfaceHit>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::Failed, "injected query failure"));
        eve::ProcgenSurfaceHit h;
        h.hit = !miss;
        h.x   = x;
        h.z   = z;
        h.y   = bad ? std::numeric_limits<float>::quiet_NaN() : x + 2.f * z;
        if (slopedNormal) {
            h.normalX = -1.f;
            h.normalY = 1.f;
            h.normalZ = -2.f;
        }
        if (invalidNormal) h.normalX = h.normalY = h.normalZ = 0.f;
        return eve::Result<eve::ProcgenSurfaceHit>::success(h);
    }
};
struct Registration {
    eve::IProcgenWorldQuery* prior = eve::cap::query<eve::IProcgenWorldQuery>();
    explicit Registration(eve::IProcgenWorldQuery* q) { eve::cap::provide<eve::IProcgenWorldQuery>(q); }
    ~Registration() { eve::cap::provide<eve::IProcgenWorldQuery>(prior); }
};
struct AssetBoundsQuery final : eve::IProcgenAssetBounds {
    mutable int                       calls = 0;
    bool                              bad   = false;
    eve::Result<std::array<float, 6>> localBounds(const eve::AssetRef&, const eve::PersistentId& build,
                                                  eve::PrefabRootPose = eve::PrefabRootPose::Authored) const override {
        ++calls;
        if (build.format() != "018f0b7e-6e50-7a10-8c22-2c8f8e3dd192")
            return eve::Result<std::array<float, 6>>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::Conflict, "stale test asset"));
        return eve::Result<std::array<float, 6>>::success(bad ? std::array<float, 6>{2, 0, -4, -2, 3, -2}
                                                              : std::array<float, 6>{-2, 0, -4, 2, 3, -2});
    }
};
RoadNetwork source(RoadSideTerrainMode mode) {
    auto made = RoadNetwork::makeStraight(40.f, 2);
    if (!made.ok()) throw std::runtime_error(made.status().describe());
    auto n          = std::move(made).takeValue();
    auto style      = n.edges()[0].style;
    style.curbWidth = style.sidewalkWidth = 0.f;
    style.sideObjectsRight                = false;
    style.sideTerrain                     = {mode, -1, 1, -2, 2, 3};
    auto changed                          = n.setEdgeStyle(1, style);
    if (!changed.ok()) throw std::runtime_error(changed.status().describe());
    return n;
}
TEST_CASE("procgen.road.authoring.sideAlignmentProducesOwnedEulerPoseAndRejectsInvalidNormals") {
    auto       n = source(RoadSideTerrainMode::None);
    PlaneQuery query;
    query.slopedNormal = true;
    RoadBakeOptions options;
    options.placementWorldQuery = &query;
    auto style                  = n.edges()[0].style;
    style.sideTerrain.alignment = RoadSideAlignment::TerrainNormal;
    REQUIRE(n.setEdgeStyle(1, style).ok());
    auto snapped = bakeRoadNetwork(n, options);
    REQUIRE(snapped.ok());
    int anchors = 0;
    for (int i = 0; i < snapped.value().placements.getCount(); ++i) {
        const auto& points = snapped.value().placements;
        if (points.getStringAttribute(i, "road_role", "") != "side.left") continue;
        ++anchors;
        const auto& p = points.points()[i];
        REQUIRE(std::fabs(p.normalX + 1.f / std::sqrt(6.f)) < 1e-5f);
        REQUIRE(std::fabs(p.normalY - 1.f / std::sqrt(6.f)) < 1e-5f);
        REQUIRE(std::fabs(p.normalZ + 2.f / std::sqrt(6.f)) < 1e-5f);
        REQUIRE(std::fabs(p.pitch) > 1.f);
        constexpr float r  = .017453292519943295f;
        const float     sy = std::sin(p.yaw * r), cy = std::cos(p.yaw * r);
        const float     sp = std::sin(p.pitch * r), cp = std::cos(p.pitch * r);
        const float     sr = std::sin(p.roll * r), cr = std::cos(p.roll * r);
        REQUIRE(std::fabs((-cy * sr + sy * sp * cr) - p.normalX) < 1e-5f);
        REQUIRE(std::fabs(cp * cr - p.normalY) < 1e-5f);
        REQUIRE(std::fabs((sy * sr + cy * sp * cr) - p.normalZ) < 1e-5f);
    }
    REQUIRE(anchors > 0);
    REQUIRE(query.calls == anchors);
    query.invalidNormal = true;
    REQUIRE(!bakeRoadNetwork(n, options).ok());
    query.invalidNormal = false;
    for (auto alignment : {RoadSideAlignment::PathForward, RoadSideAlignment::PathNormal}) {
        style.sideTerrain.alignment = alignment;
        REQUIRE(n.setEdgeStyle(1, style).ok());
        auto controls = n.edges()[0].controlPoints;
        for (auto& p : controls) p.rollDegrees = 20.f;
        REQUIRE(n.setEdgeControlPoints(1, controls).ok());
        options.placementWorldQuery = nullptr;
        Registration absent(nullptr);
        auto         baked = bakeRoadNetwork(n, options);
        REQUIRE(baked.ok());
        for (int i = 0; i < baked.value().placements.getCount(); ++i) {
            const auto& points = baked.value().placements;
            if (points.getStringAttribute(i, "road_role", "") != "side.left") continue;
            const auto& p = points.points()[i];
            REQUIRE(std::fabs(p.pitch) < 1e-4f);
            if (alignment == RoadSideAlignment::PathForward)
                REQUIRE(std::fabs(p.roll) < 1e-4f);
            else
                REQUIRE(std::fabs(std::fabs(p.roll) - 20.f) < 1e-4f);
        }
    }
}
}  // namespace
TEST_CASE("procgen.road.authoring.sideTerrainBoundsModesConsumeProviderAndStandardRecipe") {
    for (auto mode : {RoadSideTerrainMode::Center, RoadSideTerrainMode::Lowest, RoadSideTerrainMode::Highest,
                      RoadSideTerrainMode::Average}) {
        auto            n = source(mode);
        PlaneQuery      query;
        RoadBakeOptions options;
        options.placementWorldQuery = &query;
        options.placementQueryMask  = 0x42;
        options.placementQueryMinY  = -1000.f;
        options.placementQueryMaxY  = 1000.f;
        auto baked                  = bakeRoadNetwork(n, options);
        REQUIRE(baked.ok());
        int sides = 0;
        for (int i = 0; i < baked.value().placements.getCount(); ++i) {
            const auto& points = baked.value().placements;
            if (points.getStringAttribute(i, "road_role", "") != "side.left") continue;
            ++sides;
            const float offset = mode == RoadSideTerrainMode::Lowest    ? -4.f
                                 : mode == RoadSideTerrainMode::Highest ? 4.f
                                                                        : 0.f;
            REQUIRE(std::fabs(points.getY(i) - (points.getX(i) + 2.f * points.getZ(i) + offset)) < 1e-4f);
            REQUIRE(points.getNormalY(i) == 1.f);
        }
        REQUIRE(sides > 0);
        REQUIRE(query.calls == sides * (mode == RoadSideTerrainMode::Center ? 1 : 9));
        REQUIRE(query.lastMask == 0x42);
        REQUIRE(query.lastMin == -1000.f);
        REQUIRE(query.lastMax == 1000.f);
        Params params;
        REQUIRE(setRoadRecipeNetwork(params, n).ok());
        Registration registered(&query);
        REQUIRE(!bakeRoadNetworkRecipe(params).ok());
        params.setString("placementWorldRevision", "test-plane/v1");
        auto recipe = bakeRoadNetworkRecipe(params);
        REQUIRE(recipe.ok());
        REQUIRE(recipe.value().placements.points().size() == baked.value().placements.points().size());
    }
}
TEST_CASE("procgen.road.authoring.sideAlignmentVerticalFramesAndSharedProjectionBudget") {
    Registration absent(nullptr);
    auto         vertical = source(RoadSideTerrainMode::None);
    REQUIRE(vertical.setNodePosition(1, 0.f, 0.f, 0.f).ok());
    REQUIRE(vertical.setNodePosition(2, 0.f, 40.f, 0.f).ok());
    auto verticalControls = vertical.edges()[0].controlPoints;
    for (std::size_t i = 0; i < verticalControls.size(); ++i) {
        verticalControls[i].x = verticalControls[i].z = 0.f;
        verticalControls[i].y = 40.f * static_cast<float>(i) / static_cast<float>(verticalControls.size() - 1);
    }
    REQUIRE(vertical.setEdgeControlPoints(1, std::move(verticalControls)).ok());
    auto style                  = vertical.edges()[0].style;
    style.sideTerrain.alignment = RoadSideAlignment::PathNormal;
    REQUIRE(vertical.setEdgeStyle(1, style).ok());
    auto baked = bakeRoadNetwork(vertical);
    REQUIRE(baked.ok());
    int sides = 0;
    for (int i = 0; i < baked.value().placements.getCount(); ++i) {
        const auto& points = baked.value().placements;
        if (points.getStringAttribute(i, "road_role", "") != "side.left") continue;
        ++sides;
        const auto& p = points.points()[i];
        REQUIRE(std::isfinite(p.pitch));
        REQUIRE(std::isfinite(p.yaw));
        REQUIRE(std::isfinite(p.roll));
        REQUIRE(std::fabs(std::fabs(p.pitch) - 90.f) < 1e-3f);
        REQUIRE(std::fabs(p.roll) < 1e-3f);
        constexpr float r = .017453292519943295f;
        REQUIRE(std::fabs(std::sin(p.yaw * r) * std::sin(p.pitch * r) - p.normalX) < 1e-4f);
        REQUIRE(std::fabs(std::cos(p.pitch * r) - p.normalY) < 1e-4f);
        REQUIRE(std::fabs(std::cos(p.yaw * r) * std::sin(p.pitch * r) - p.normalZ) < 1e-4f);
    }
    REQUIRE(sides > 0);
    auto terrain                = source(RoadSideTerrainMode::Average);
    style                       = terrain.edges()[0].style;
    style.sideTerrain.alignment = RoadSideAlignment::TerrainNormal;
    REQUIRE(terrain.setEdgeStyle(1, style).ok());
    REQUIRE(!bakeRoadNetwork(terrain).ok());
    PlaneQuery      query;
    RoadBakeOptions options;
    options.placementWorldQuery     = &query;
    options.maximumPlacementQueries = 9;
    REQUIRE(!bakeRoadNetwork(terrain, options).ok());
    REQUIRE(query.calls == 9);
}
TEST_CASE("procgen.road.authoring.sideTerrainAbsenceMissFailureAndSharedBudgetPreserveAuthor") {
    Registration absent(nullptr);
    auto         n      = source(RoadSideTerrainMode::Lowest);
    auto         before = captureRoadNetwork(n);
    REQUIRE(before.ok());
    REQUIRE(!bakeRoadNetwork(n).ok());
    auto legacy = source(RoadSideTerrainMode::None);
    REQUIRE(bakeRoadNetwork(legacy).ok());
    PlaneQuery      q;
    RoadBakeOptions options;
    options.placementWorldQuery     = &q;
    options.maximumPlacementQueries = 10;
    REQUIRE(!bakeRoadNetwork(n, options).ok());
    REQUIRE(q.calls == 10);
    options.maximumPlacementQueries = 65536;
    q.calls                         = 0;
    q.failAt                        = 11;
    REQUIRE(!bakeRoadNetwork(n, options).ok());
    q.failAt = 0;
    q.miss   = true;
    REQUIRE(!bakeRoadNetwork(n, options).ok());
    q.miss = false;
    q.bad  = true;
    REQUIRE(!bakeRoadNetwork(n, options).ok());
    q.bad                      = false;
    q.calls                    = 0;
    options.placementQueryMinY = 1.f;
    options.placementQueryMaxY = -1.f;
    REQUIRE(!bakeRoadNetwork(n, options).ok());
    REQUIRE(q.calls == 0);
    options.includePlacements   = false;
    options.placementWorldQuery = nullptr;
    REQUIRE(bakeRoadNetwork(n, options).ok());
    REQUIRE(captureRoadNetwork(n).value() == before.value());
}
TEST_CASE("procgen.road.authoring.sideTerrainSnapshotPresetUndoAndLegacyMigration") {
    auto n                              = source(RoadSideTerrainMode::Average);
    auto orientedStyle                  = n.edges()[0].style;
    orientedStyle.sideTerrain.alignment = RoadSideAlignment::PathNormal;
    auto boundAsset                     = eve::AssetRef::parse("asset://018f0b7e-6e50-7a10-8c22-2c8f8e3dd191");
    auto boundBuild                     = eve::PersistentId::parse("018f0b7e-6e50-7a10-8c22-2c8f8e3dd192");
    REQUIRE(boundAsset.ok());
    REQUIRE(boundBuild.has_value());
    orientedStyle.sideTerrain.asset = RoadSideAssetBinding{boundAsset.value(), *boundBuild};
    REQUIRE(n.setEdgeStyle(1, orientedStyle).ok());
    auto captured = captureRoadNetwork(n);
    REQUIRE(captured.ok());
    auto restored = restoreRoadNetwork(captured.value());
    REQUIRE(restored.ok());
    REQUIRE(captureRoadNetwork(restored.value()).value() == captured.value());
    auto  malformed     = captured.value();
    auto* malformedRule = malformed.getIf<eve::Value::Object>()
                              ->at("edges")
                              .getIf<eve::Value::Array>()
                              ->at(0)
                              .getIf<eve::Value::Object>()
                              ->at("style")
                              .getIf<eve::Value::Object>()
                              ->at("sideTerrain")
                              .getIf<eve::Value::Object>();
    malformedRule->erase("samplesPerAxis");
    REQUIRE(!restoreRoadNetwork(malformed).ok());
    REQUIRE(captureRoadNetwork(n).value() == captured.value());
    auto  legacy = captured.value();
    auto& style  = legacy.getIf<eve::Value::Object>()
                      ->at("edges")
                      .getIf<eve::Value::Array>()
                      ->at(0)
                      .getIf<eve::Value::Object>()
                      ->at("style");
    style.getIf<eve::Value::Object>()->erase("sideTerrain");
    REQUIRE(restoreRoadNetwork(legacy).value().edges()[0].style.sideTerrain.mode == RoadSideTerrainMode::None);
    using namespace eve::procgen_editing;
    RoadPresetDraft draft;
    draft.source                         = n;
    draft.types                          = {{"root", "", {}}};
    draft.bindings                       = {{1, "root", {}}};
    draft.types[0].overrides.sideTerrain = n.edges()[0].style.sideTerrain;
    auto doc                             = RoadPresetDocument::create(draft);
    REQUIRE(doc.ok());
    auto edit                                            = doc.value()->beginEdit();
    edit.types[0].overrides.sideTerrain->mode            = RoadSideTerrainMode::Highest;
    edit.types[0].overrides.sideTerrain->asset->rootPose = eve::PrefabRootPose::Authored;
    auto token                                           = doc.value()->prepare(std::move(edit));
    REQUIRE(token.ok());
    REQUIRE(doc.value()->commit(std::move(token).takeValue()).ok());
    REQUIRE(doc.value()->undo().ok());
    REQUIRE(doc.value()->network().edges()[0].style.sideTerrain.asset->rootPose ==
            eve::PrefabRootPose::ReplaceTranslationRotation);
    auto saved = doc.value()->capture();
    REQUIRE(saved.ok());
    auto loaded = doc.value()->prepareRestore(saved.value());
    REQUIRE(loaded.ok());
    REQUIRE(doc.value()->commit(std::move(loaded).takeValue()).ok());
    REQUIRE(doc.value()->redo().ok());
    REQUIRE(doc.value()->network().edges()[0].style.sideTerrain.mode == RoadSideTerrainMode::Highest);
    REQUIRE(doc.value()->network().edges()[0].style.sideTerrain.alignment == RoadSideAlignment::PathNormal);
    REQUIRE(doc.value()->network().edges()[0].style.sideTerrain.asset.has_value());
    REQUIRE(doc.value()->network().edges()[0].style.sideTerrain.asset->asset == boundAsset.value());
    REQUIRE(doc.value()->network().edges()[0].style.sideTerrain.asset->buildId == *boundBuild);
    REQUIRE(doc.value()->network().edges()[0].style.sideTerrain.asset->rootPose == eve::PrefabRootPose::Authored);
    auto invalid                = n.edges()[0].style;
    invalid.sideTerrain.minSide = 2;
    REQUIRE(!n.setEdgeStyle(1, invalid).ok());
    REQUIRE(captureRoadNetwork(n).value() == captured.value());
}
TEST_CASE("procgen.road.authoring.assetBoundsDriveFootprintAndRetainAdmittedIdentity") {
    auto n     = source(RoadSideTerrainMode::Average);
    auto style = n.edges()[0].style;
    auto asset = eve::AssetRef::parse("asset://018f0b7e-6e50-7a10-8c22-2c8f8e3dd191");
    auto build = eve::PersistentId::parse("018f0b7e-6e50-7a10-8c22-2c8f8e3dd192");
    REQUIRE(asset.ok());
    REQUIRE(build.has_value());
    style.sideTerrain.asset = RoadSideAssetBinding{asset.value(), *build};
    REQUIRE(n.setEdgeStyle(1, style).ok());
    auto snapshot = captureRoadNetwork(n);
    REQUIRE(snapshot.ok());
    auto restored = restoreRoadNetwork(snapshot.value());
    REQUIRE(restored.ok());
    REQUIRE(captureRoadNetwork(restored.value()).value() == snapshot.value());
    REQUIRE(restored.value().edges()[0].style.sideTerrain.asset->rootPose ==
            eve::PrefabRootPose::ReplaceTranslationRotation);
    auto  legacyPose = snapshot.value();
    auto* legacyRule = legacyPose.getIf<eve::Value::Object>()
                           ->at("edges")
                           .getIf<eve::Value::Array>()
                           ->at(0)
                           .getIf<eve::Value::Object>()
                           ->at("style")
                           .getIf<eve::Value::Object>()
                           ->at("sideTerrain")
                           .getIf<eve::Value::Object>();
    legacyRule->erase("assetRootPose");
    auto migrated = restoreRoadNetwork(legacyPose);
    REQUIRE(migrated.ok());
    REQUIRE(migrated.value().edges()[0].style.sideTerrain.asset->rootPose == eve::PrefabRootPose::Authored);
    legacyRule->emplace("assetRootPose", eve::Value(std::int64_t{2}));
    REQUIRE(!restoreRoadNetwork(legacyPose).ok());
    auto invalidPose                        = style;
    invalidPose.sideTerrain.asset->rootPose = static_cast<eve::PrefabRootPose>(255);
    REQUIRE(!n.setEdgeStyle(1, invalidPose).ok());
    PlaneQuery       terrain;
    AssetBoundsQuery assets;
    RoadBakeOptions  options;
    options.placementWorldQuery  = &terrain;
    options.placementAssetBounds = &assets;
    {
        struct RestoreAssetProvider {
            eve::IProcgenAssetBounds* prior = eve::cap::query<eve::IProcgenAssetBounds>();
            ~RestoreAssetProvider() { eve::cap::provide<eve::IProcgenAssetBounds>(prior); }
        } restore;
        eve::cap::provide<eve::IProcgenAssetBounds>(nullptr);
        auto missing                 = options;
        missing.placementAssetBounds = nullptr;
        REQUIRE(!bakeRoadNetwork(n, missing).ok());
    }
    auto baked = bakeRoadNetwork(n, options);
    REQUIRE(baked.ok());
    REQUIRE(assets.calls == 1);
    int sides = 0;
    for (int i = 0; i < baked.value().placements.getCount(); ++i) {
        const auto& points = baked.value().placements;
        if (points.getStringAttribute(i, "road_role", "") != "side.left") continue;
        ++sides;
        const auto& p = points.points()[i];
        REQUIRE(std::fabs(p.y - (p.x + 2.f * p.z + 3.f)) < 1e-4f);
        REQUIRE(points.getStringAttribute(i, "asset", "") == style.sideTerrain.asset->asset.format());
        REQUIRE(points.getStringAttribute(i, "asset_build_id", "") == style.sideTerrain.asset->buildId.format());
        REQUIRE(p.boundsMinX == -2.f);
        REQUIRE(p.boundsMaxZ == -2.f);
        REQUIRE(std::fabs(p.yaw + 90.f) < 1e-4f);
    }
    REQUIRE(sides > 0);
    assets.bad                  = true;
    assets.bad                  = false;
    style.sideTerrain.alignment = RoadSideAlignment::PathNormal;
    REQUIRE(n.setEdgeStyle(1, style).ok());
    auto rolledControls = n.edges()[0].controlPoints;
    for (auto& p : rolledControls) p.rollDegrees = 90.f;
    REQUIRE(n.setEdgeControlPoints(1, std::move(rolledControls)).ok());
    terrain.calls = 0;
    auto rolled   = bakeRoadNetwork(n, options);
    REQUIRE(rolled.ok());
    int rolledSides = 0;
    for (int i = 0; i < rolled.value().placements.getCount(); ++i) {
        const auto& points = rolled.value().placements;
        if (points.getStringAttribute(i, "road_role", "") != "side.left") continue;
        ++rolledSides;
        const auto&     p  = points.points()[i];
        constexpr float r  = .017453292519943295f;
        const float     sy = std::sin(p.yaw * r), cy = std::cos(p.yaw * r);
        const float     sp = std::sin(p.pitch * r), cp = std::cos(p.pitch * r);
        const float     sr = std::sin(p.roll * r), cr = std::cos(p.roll * r);
        const float     rx = -sr * 1.5f, ry = cr * 1.5f, rz = sp * ry - cp * 3.f;
        const float     dx = cy * rx + sy * rz, dz = -sy * rx + cy * rz;
        REQUIRE(std::fabs(p.y - (p.x + 2.f * p.z + dx + 2.f * dz)) < 1e-4f);
    }
    REQUIRE(rolledSides > 0);
    REQUIRE(terrain.calls == rolledSides * 18);
    assets.bad = true;
    REQUIRE(!bakeRoadNetwork(n, options).ok());
    assets.bad                       = false;
    style.sideTerrain.asset->buildId = *eve::PersistentId::parse("018f0b7e-6e50-7a10-8c22-2c8f8e3dd193");
    REQUIRE(n.setEdgeStyle(1, style).ok());
    REQUIRE(!bakeRoadNetwork(n, options).ok());
}
