#include "zeroerr/assert.h"

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_gts_mesh_splitter)
#include "PrivateTestScope.h"
#include <zeroerr/unittest.h>
#include <limits>
#include "asset/AssetCooker.h"
#include "asset/CanonicalMesh.h"
#include "asset/EvpackResourceReader.h"
#include "asset/procgen/GtsTerrainLodPackage.h"
#include "image/ImageData.h"
#include "procgen/GtsMeshSimplifier.h"
#include "procgen/GtsMeshSplitter.h"
#include "procgen/GtsTerrainExportSettings.h"
#include "procgen/GtsTerrainLod.h"
#include "procgen/heightmap/Heightmap.h"
// gts_mesh_splitter.cpp
namespace test_gts_mesh_splitter {
using namespace eve::procgen;

namespace {
MeshBuild crossingQuad() {
    MeshBuild m;
    m.setActiveGroup("terrain");
    m.addVertex(0, 0, 0, 0, 1, 0, 0, 0);
    m.addVertex(2, 0, 0, 0, 1, 0, 1, 0);
    m.addVertex(2, 0, 2, 0, 1, 0, 1, 1);
    m.addVertex(0, 0, 2, 0, 1, 0, 0, 1);
    m.addTriangle(0, 1, 2);
    m.addTriangle(0, 2, 3);
    return m;
}
}  // namespace
TEST_CASE("procgen.gtsTerrainBaseMesh.matchesPcgSaveResolutionSampling") {
    Heightmap heightmap(17, 17);
    for (int z = 0; z < 17; ++z)
        for (int x = 0; x < 17; ++x) heightmap.setHeight(x, z, float(x + z * 2) / 48.f);
    const int expectedVertices[] = {289, 81, 25, 9, 4};
    const int expectedIndices[]  = {1536, 384, 96, 24, 6};
    for (int resolution = 0; resolution <= 4; ++resolution) {
        MeshBuild mesh;
        auto result = buildGtsTerrainBaseMesh(mesh, heightmap, static_cast<GtsTerrainSaveResolution>(resolution), 160.f,
                                              48.f, 80.f);
        REQUIRE(result.ok());
        CHECK_EQ(mesh.getVertexCount(), expectedVertices[resolution]);
        CHECK_EQ(mesh.getIndexCount(), expectedIndices[resolution]);
        CHECK_EQ(mesh.getPositionX(mesh.getVertexCount() - 1), 160.f);
        CHECK_EQ(mesh.getPositionY(mesh.getVertexCount() - 1), 48.f);
        CHECK_EQ(mesh.getPositionZ(mesh.getVertexCount() - 1), 80.f);
        CHECK_EQ(mesh.getUvU(mesh.getVertexCount() - 1), 1.f);
        CHECK_EQ(mesh.getUvV(mesh.getVertexCount() - 1), 1.f);
        for (int vertex = 0; vertex < mesh.getVertexCount(); ++vertex) {
            const float length = std::sqrt(mesh.getNormalX(vertex) * mesh.getNormalX(vertex) +
                                           mesh.getNormalY(vertex) * mesh.getNormalY(vertex) +
                                           mesh.getNormalZ(vertex) * mesh.getNormalZ(vertex));
            CHECK(std::abs(length - 1.f) < .0001f);
            CHECK(mesh.getNormalY(vertex) > 0.f);
        }
        CHECK_EQ(mesh.getMeta("saveResolution", ""), std::to_string(resolution));
    }
}

TEST_CASE("procgen.gtsTerrainBaseMesh.rejectsInvalidInputAtomically") {
    Heightmap valid(17, 17);
    MeshBuild output = crossingQuad();
    const int before = output.getIndexCount();
    CHECK(!buildGtsTerrainBaseMesh(output, valid, static_cast<GtsTerrainSaveResolution>(5), 1.f, 1.f, 1.f).ok());
    CHECK_EQ(output.getIndexCount(), before);
    Heightmap incompatible(6, 6);
    CHECK(!buildGtsTerrainBaseMesh(output, incompatible, GtsTerrainSaveResolution::Quarter, 1.f, 1.f, 1.f).ok());
    CHECK_EQ(output.getIndexCount(), before);
    valid.setHeight(3, 2, std::numeric_limits<float>::quiet_NaN());
    CHECK(!buildGtsTerrainBaseMesh(output, valid, GtsTerrainSaveResolution::Full, 1.f, 1.f, 1.f).ok());
    CHECK_EQ(output.getIndexCount(), before);
}

TEST_CASE("procgen.gtsTerrainLod.convertsHeightmapThroughTheCompletePcgPipeline") {
    Heightmap heightmap(65, 65);
    for (int z = 0; z < 65; ++z)
        for (int x = 0; x < 65; ++x) heightmap.setHeight(x, z, float(x + z) / 128.f);
    GtsTerrainLodSet lods;
    auto converted = buildDefaultGtsTerrainLodsFromHeightmapInto(lods, heightmap, GtsTerrainSaveResolution::Quarter,
                                                                 160.f, 32.f, 80.f, 3, GtsMeshPivot::None);
    if (!converted) throw std::runtime_error(converted.error()->message());
    CHECK_EQ(lods.getColumnCount(), 4);
    CHECK_EQ(lods.getRowCount(), 4);
    CHECK_EQ(lods.getTileCount(), 16);
    CHECK_EQ(lods.getLevelCount(), 4);
    for (int tile = 0; tile < lods.getTileCount(); ++tile) {
        const auto* value = lods.tileAt(tile);
        REQUIRE(value);
        CHECK_EQ(value->levels.size(), 4u);
        CHECK(!value->levels.front().empty());
    }
    auto snapshot = lods.snapshotJson();
    REQUIRE(snapshot.ok());
    CHECK(!buildDefaultGtsTerrainLodsFromHeightmapInto(lods, heightmap, GtsTerrainSaveResolution::Quarter, 160.f, 32.f,
                                                       80.f, 6, GtsMeshPivot::None)
               .ok());
    auto unchanged = lods.snapshotJson();
    REQUIRE(unchanged.ok());
    CHECK_EQ(unchanged.value(), snapshot.value());
}

TEST_CASE("procgen.gtsTerrainMeshSettings.persistAndDriveCustomLodCount") {
    GtsTerrainMeshSettings settings;
    CHECK_EQ(settings.getSaveResolution(), 0);
    CHECK_EQ(settings.getLodCount(), 4);
    CHECK_EQ(settings.getSubTiles(), 3);
    CHECK_EQ(settings.getLodQuality(3), 12.5f);
    REQUIRE(settings.setSaveResolution(2).ok());
    REQUIRE(settings.setLodCount(2).ok());
    REQUIRE(settings.setSubTiles(1).ok());
    REQUIRE(settings.setLodQuality(0, 75.f).ok());
    REQUIRE(settings.setLodQuality(1, 0.f).ok());
    CHECK_EQ(settings.getLodTransitionHeight(0), .95f);
    CHECK_EQ(settings.getLodTransitionHeight(1), .02f);
    auto json = settings.snapshotJson();
    REQUIRE(json.ok());
    GtsTerrainMeshSettings restored;
    REQUIRE(restored.restoreJson(json.value()).ok());
    auto again = restored.snapshotJson();
    REQUIRE(again.ok());
    CHECK_EQ(again.value(), json.value());
    Heightmap heightmap(17, 17);
    for (int z = 0; z < 17; ++z)
        for (int x = 0; x < 17; ++x) heightmap.setHeight(x, z, float(x + z) / 32.f);
    GtsTerrainLodSet lods;
    REQUIRE(buildGtsTerrainLodsFromHeightmapInto(lods, heightmap, restored, 16.f, 8.f, 16.f).ok());
    CHECK_EQ(lods.getTileCount(), 4);
    CHECK_EQ(lods.getLevelCount(), 2);
    for (int tile = 0; tile < 4; ++tile) CHECK(!lods.tileAt(tile)->levels[1].empty());
    std::string unknown = json.value();
    unknown.insert(1, "\"unknown\":1,");
    CHECK(!restored.restoreJson(unknown).ok());
    again = restored.snapshotJson();
    REQUIRE(again.ok());
    CHECK_EQ(again.value(), json.value());
    CHECK(!restored.setLodCount(0).ok());
    CHECK(!restored.setLodQuality(4, 50.f).ok());
    CHECK(!restored.setSubTiles(6).ok());
}
TEST_CASE("procgen.gtsMeshSplitter.clipsAcrossCellsAndInterpolatesAttributes") {
    auto result = splitGtsMesh(crossingQuad(), 1, 1, GtsMeshPivot::None);
    REQUIRE(result.ok());
    CHECK_EQ(result.value().getColumnCount(), 2);
    CHECK_EQ(result.value().getRowCount(), 2);
    CHECK_EQ(result.value().getTileCount(), 4);
    for (int i = 0; i < 4; ++i) {
        auto* tile = result.value().tileAt(i);
        REQUIRE(tile);
        CHECK(!tile->mesh.empty());
        for (int v = 0; v < tile->mesh.getVertexCount(); ++v) {
            CHECK(tile->mesh.getUvU(v) >= 0.f);
            CHECK(tile->mesh.getUvU(v) <= 1.f);
            CHECK(tile->mesh.getUvV(v) >= 0.f);
            CHECK(tile->mesh.getUvV(v) <= 1.f);
            CHECK_EQ(tile->mesh.getNormalY(v), 1.f);
        }
    }
}
TEST_CASE("procgen.gtsMeshSplitter.translatesPivotAndPreservesWorldBounds") {
    auto result = splitGtsMesh(crossingQuad(), 1, 0, GtsMeshPivot::CenterXZ);
    REQUIRE(result.ok());
    auto* right = result.value().tileAt(1);
    REQUIRE(right);
    CHECK_EQ(right->offsetX, 1.5f);
    CHECK_EQ(right->offsetZ, 1.f);
    float min = 99, max = -99;
    for (int i = 0; i < right->mesh.getVertexCount(); ++i) {
        float world = right->mesh.getPositionX(i) + right->offsetX;
        min         = std::min(min, world);
        max         = std::max(max, world);
    }
    CHECK_EQ(min, 1.f);
    CHECK_EQ(max, 2.f);
}
TEST_CASE("procgen.gtsMeshSplitter.rejectsInvalidInput") {
    MeshBuild invalid      = crossingQuad();
    invalid.positions()[0] = std::numeric_limits<float>::quiet_NaN();
    CHECK(!splitGtsMesh(invalid, 1, 1).ok());
    CHECK(!splitGtsMesh(crossingQuad(), -1, 1).ok());
    MeshBuild empty;
    CHECK(!splitGtsMesh(empty, 0, 0).ok());
}
TEST_CASE("procgen.gtsMeshSimplifier.reducesInteriorAndPreservesBorder") {
    MeshBuild grid;
    grid.setActiveGroup("terrain");
    for (int z = 0; z < 3; ++z)
        for (int x = 0; x < 3; ++x)
            grid.addVertex(float(x), x == 1 && z == 1 ? .2f : 0.f, float(z), 0, 1, 0, x / 2.f, z / 2.f);
    for (int z = 0; z < 2; ++z)
        for (int x = 0; x < 2; ++x) {
            uint32_t a = z * 3 + x, b = a + 1, c = a + 3, d = c + 1;
            grid.addTriangle(a, b, d);
            grid.addTriangle(a, d, c);
        }
    MeshBuild                    simplified;
    GtsMeshSimplificationOptions options;
    options.preserveBorderEdges = false;
    auto result                 = simplifyGtsMesh(simplified, grid, .5f, options);
    REQUIRE(result.ok());
    CHECK(result.value() <= 4);
    CHECK(result.value() > 0);
    CHECK_EQ(simplified.getGroupCount(), 1);
    CHECK_EQ(simplified.getGroupName(0), "terrain");
    MeshBuild border;
    options.preserveBorderEdges = true;
    REQUIRE(simplifyGtsMesh(border, grid, .1f, options).ok());
    CHECK(border.getIndexCount() / 3 >= 4);
}
TEST_CASE("procgen.gtsMeshSimplifier.aliasAndFailureAreAtomic") {
    MeshBuild mesh = crossingQuad();
    auto      ok   = simplifyGtsMesh(mesh, mesh, .5f, {});
    REQUIRE(ok.ok());
    CHECK(!mesh.empty());
    MeshBuild                    output = crossingQuad();
    const int                    before = output.getIndexCount();
    GtsMeshSimplificationOptions invalid;
    invalid.maxIterationCount = 0;
    CHECK(!simplifyGtsMesh(output, crossingQuad(), .5f, invalid).ok());
    CHECK_EQ(output.getIndexCount(), before);
}

TEST_CASE("procgen.gtsMeshSimplifier.smartLinksSplitAttributesWithoutMergingThem") {
    MeshBuild split;
    split.setActiveGroup("terrain");
    split.addVertex(0, 0, 0, 0, 1, 0, 0, 0);
    split.addVertex(1, 0, 0, 0, 1, 0, 1, 0);
    split.addVertex(1, 0, 1, 0, 1, 0, 1, 1);
    split.addVertex(0, 0, 1, 0, 1, 0, 0, 1);
    split.addVertex(1, 0, 0, 0, 1, 0, 0, 0);
    split.addVertex(2, 0, 0, 0, 1, 0, 1, 0);
    split.addVertex(2, 0, 1, 0, 1, 0, 1, 1);
    split.addVertex(1, 0, 1, 0, 1, 0, 0, 1);
    split.addTriangle(0, 1, 2);
    split.addTriangle(0, 2, 3);
    split.addTriangle(4, 5, 6);
    split.addTriangle(4, 6, 7);
    GtsMeshSimplificationOptions options;
    options.preserveBorderEdges = true;
    options.enableSmartLink     = false;
    MeshBuild disconnected;
    REQUIRE(simplifyGtsMesh(disconnected, split, .5f, options).ok());
    CHECK_EQ(disconnected.getIndexCount() / 3, 4);
    options.enableSmartLink = true;
    MeshBuild linked;
    REQUIRE(simplifyGtsMesh(linked, split, .5f, options).ok());
    CHECK_EQ(linked.getIndexCount() / 3, 2);
    bool retainedSplitAttributes = false;
    for (int a = 0; a < linked.getVertexCount(); ++a)
        for (int b = a + 1; b < linked.getVertexCount(); ++b)
            if (linked.getPositionX(a) == linked.getPositionX(b) && linked.getPositionZ(a) == linked.getPositionZ(b) &&
                (linked.getUvU(a) != linked.getUvU(b) || linked.getUvV(a) != linked.getUvV(b)))
                retainedSplitAttributes = true;
    CHECK(retainedSplitAttributes);
    options.preserveUvSeamEdges = true;
    MeshBuild seam;
    REQUIRE(simplifyGtsMesh(seam, split, .5f, options).ok());
    CHECK_EQ(seam.getIndexCount() / 3, 4);
    MeshBuild folded                = split;
    folded.uvs()[8]                 = 1.f;
    folded.uvs()[9]                 = 0.f;
    folded.uvs()[14]                = 1.f;
    folded.uvs()[15]                = 1.f;
    options.preserveUvSeamEdges     = false;
    options.preserveUvFoldoverEdges = true;
    MeshBuild foldover;
    REQUIRE(simplifyGtsMesh(foldover, folded, .5f, options).ok());
    CHECK_EQ(foldover.getIndexCount() / 3, 4);
}

TEST_CASE("procgen.gtsTerrainLod.buildsSequentialLevelsForEveryTile") {
    auto levels = defaultGtsTerrainLodLevels();
    auto result = buildGtsTerrainLods(crossingQuad(), 1, 0, GtsMeshPivot::CenterXZ, levels);
    REQUIRE(result.ok());
    CHECK_EQ(result.value().getColumnCount(), 2);
    CHECK_EQ(result.value().getTileCount(), 2);
    CHECK_EQ(result.value().getLevelCount(), 4);
    for (int tileIndex = 0; tileIndex < result.value().getTileCount(); ++tileIndex) {
        auto* tile = result.value().tileAt(tileIndex);
        REQUIRE(tile);
        CHECK_EQ(tile->levels.size(), 4u);
        for (size_t level = 1; level < tile->levels.size(); ++level)
            CHECK(tile->levels[level].getIndexCount() <= tile->levels[level - 1].getIndexCount());
    }
    CHECK_EQ(result.value().levelAt(0)->quality, 1.f);
    CHECK_EQ(result.value().levelAt(3)->screenRelativeTransitionHeight, .02f);
}

TEST_CASE("procgen.gtsTerrainLod.rejectsInvalidProfileWithoutPartialResult") {
    auto levels                              = defaultGtsTerrainLodLevels();
    levels[1].screenRelativeTransitionHeight = .96f;
    CHECK(!buildGtsTerrainLods(crossingQuad(), 0, 0, GtsMeshPivot::None, levels).ok());
    levels            = defaultGtsTerrainLodLevels();
    levels[2].quality = 0.f;
    CHECK(!buildGtsTerrainLods(crossingQuad(), 0, 0, GtsMeshPivot::None, levels).ok());
}

TEST_CASE("procgen.gtsTerrainLod.selectsUnityScreenThresholdsAndRenderableDistances") {
    auto result = buildGtsTerrainLods(crossingQuad(), 1, 0, GtsMeshPivot::None, defaultGtsTerrainLodLevels());
    REQUIRE(result.ok());
    const auto& lod = result.value();
    CHECK_EQ(lod.selectLevel(.96f), 0);
    CHECK_EQ(lod.selectLevel(.8f), 1);
    CHECK_EQ(lod.selectLevel(.65f), 2);
    CHECK_EQ(lod.selectLevel(.1f), 3);
    CHECK_EQ(lod.selectLevel(.01f), -1);
    const float switch1 = lod.getLevelSwitchDistance(1, 2.f, 90.f);
    CHECK(std::abs(switch1 - (1.f / .7f)) < .0001f);
    CHECK_EQ(lod.selectLevelForCamera(switch1 * .99f, 2.f, 90.f), 1);
    CHECK_EQ(lod.selectLevelForCamera(switch1 * 1.01f, 2.f, 90.f), 2);
    CHECK_EQ(lod.selectLevelForCamera(-1.f, 2.f, 90.f), -2);
    CHECK_EQ(lod.getLevelSwitchDistance(9, 2.f, 90.f), -1.f);
}

TEST_CASE("procgen.gtsTerrainLod.codecRoundTripsAndRejectsAtomically") {
    auto built = buildGtsTerrainLods(crossingQuad(), 1, 0, GtsMeshPivot::CenterXZ, defaultGtsTerrainLodLevels());
    REQUIRE(built.ok());
    auto json = built.value().snapshotJson();
    REQUIRE(json.ok());
    GtsTerrainLodSet restored;
    REQUIRE(restored.restoreJson(json.value()).ok());
    auto again = restored.snapshotJson();
    REQUIRE(again.ok());
    CHECK_EQ(again.value(), json.value());
    auto* tile = restored.tileAt(0);
    REQUIRE(tile);
    CHECK_EQ(tile->levels.size(), 4u);
    CHECK_EQ(tile->levels[0].getGroupName(0), "terrain");
    std::string unknown = json.value();
    unknown.insert(1, "\"extra\":1,");
    CHECK(!restored.restoreJson(unknown).ok());
    auto unchanged = restored.snapshotJson();
    REQUIRE(unchanged.ok());
    CHECK_EQ(unchanged.value(), json.value());
    std::string wrong = json.value();
    auto        pos   = wrong.find("eve.procgen.gts-terrain-lod");
    REQUIRE(pos != std::string::npos);
    wrong.replace(pos, 28, "eve.procgen.wrong-terrain-lod");
    CHECK(!restored.restoreJson(wrong).ok());
    unchanged = restored.snapshotJson();
    REQUIRE(unchanged.ok());
    CHECK_EQ(unchanged.value(), json.value());
}

TEST_CASE("procgen.gtsTerrainLod.plansDeterministicNativeAssetNames") {
    auto lods = buildGtsTerrainLods(crossingQuad(), 1, 0, GtsMeshPivot::None, defaultGtsTerrainLodLevels());
    REQUIRE(lods.ok());
    auto entries = planGtsTerrainLodAssets(lods.value(), "Alpine");
    REQUIRE(entries.ok());
    CHECK_EQ(entries.value().size(), 8u);
    CHECK_EQ(entries.value()[0].objectName, "Alpine SubTile0_LOD_0");
    CHECK_EQ(entries.value()[0].meshName, "Alpine_SubTile_00");
    CHECK_EQ(entries.value()[0].relativePath, "Meshes/Alpine_SubTile_00.eve.mesh.json");
    CHECK_EQ(entries.value()[7].relativePath, "Meshes/Alpine_SubTile_13.eve.mesh.json");
    GtsTerrainLodAssetPlan plan;
    REQUIRE(planGtsTerrainLodAssetsInto(plan, lods.value(), "Alpine", "Generated/Meshes").ok());
    CHECK_EQ(plan.getEntryCount(), 8);
    CHECK_EQ(plan.getTileIndex(7), 1);
    CHECK_EQ(plan.getLevelIndex(7), 3);
    const auto before = plan.getRelativePath(0);
    CHECK(!planGtsTerrainLodAssetsInto(plan, lods.value(), "../bad", "Meshes").ok());
    CHECK_EQ(plan.getRelativePath(0), before);
}

TEST_CASE("asset.procgen.gtsTerrainLod.packagesCanonicalMeshesEndToEnd") {
    auto lods = buildGtsTerrainLods(crossingQuad(), 1, 0, GtsMeshPivot::None, defaultGtsTerrainLodLevels());
    REQUIRE(lods.ok());
    eve::asset_import::ImportPackageIdentity identity{
        *eve::PersistentId::parse("3c615db5-e833-4dd3-a537-e60e9d79e259"), "pcg.gts.terrain", "1.0.0", {}};
    auto prepared = eve::asset_procgen::prepareGtsTerrainLodPackage(identity, lods.value(), "Alpine");
    REQUIRE(prepared.ok());
    CHECK_EQ(prepared.value().manifest.assets.size(), 8u);
    CHECK_EQ(prepared.value().manifest.entrypoints.size(), 9u);
    CHECK_EQ(prepared.value().sourceMappings.front().sourceObject, "Alpine SubTile0_LOD_0");
    auto archiveBytes = eve::asset::buildEvaArchive(prepared.value().manifest, prepared.value().entries);
    REQUIRE(archiveBytes.ok());
    auto archive = eve::asset::parseEvaArchive(archiveBytes.value());
    REQUIRE(archive.ok());
    auto profile = eve::asset::assetCookProfileForTarget("windows-x86_64-vulkan");
    REQUIRE(profile.ok());
    auto cooked = eve::asset::cookEvaToEvpack(archive.value(), profile.value());
    REQUIRE(cooked.ok());
    auto pack = eve::asset::parseEvpack(cooked.value().bytes);
    REQUIRE(pack.ok());
    eve::asset::EvpackResourceReader reader(std::make_shared<const eve::asset::Evpack>(std::move(pack).takeValue()));
    eve::asset::EvpackCapabilities   caps{"windows", "x86_64", "vulkan", {"rgba8"}, {"spirv-1.6"}, {"high"}, {}};
    auto                             payload =
        reader.read(prepared.value().manifest.entrypoints.at("tile-1-lod-3"), "eve.mesh/3", caps, 1024 * 1024);
    REQUIRE(payload.ok());
    bool decoded = false;
    for (const auto& chunk : payload.value().chunks)
        if (chunk.kind == eve::asset::EvpackChunkKind::Bulk) {
            auto mesh = eve::asset::decodeCanonicalMesh(chunk.bytes);
            REQUIRE(mesh.ok());
            CHECK_EQ(mesh.value().positions, lods.value().tileAt(1)->levels[3].positions());
            decoded = true;
        }
    REQUIRE(decoded);
    auto                                 limited = identity;
    eve::asset_import::AssetImportLimits limits;
    limits.maximumAssets = 1;
    REQUIRE(!eve::asset_procgen::prepareGtsTerrainLodPackage(limited, lods.value(), "Alpine", limits).ok());
}

TEST_CASE("procgen.gtsTerrainExportSettings.matchesOriginalPresetsAndRoundTrips") {
    auto impostor0 = makeGtsTerrainImpostorLod(0), impostor2 = makeGtsTerrainImpostorLod(2);
    auto low0 = makeGtsTerrainLowPolyLod(0), low1 = makeGtsTerrainLowPolyLod(1);
    REQUIRE(impostor0.ok());
    REQUIRE(impostor2.ok());
    REQUIRE(low0.ok());
    REQUIRE(low1.ok());
    CHECK_EQ(int(impostor0.value().saveResolution), 1);
    CHECK_EQ(impostor0.value().textureExportResolution, 2048);
    CHECK_EQ(int(impostor2.value().saveResolution), 3);
    CHECK_EQ(impostor2.value().textureExportResolution, 512);
    CHECK(!impostor0.value().bakeVertexColors);
    CHECK(impostor0.value().exportNormalMaps);
    CHECK_EQ(int(low0.value().saveResolution), 3);
    CHECK_EQ(int(low1.value().saveResolution), 4);
    CHECK_EQ(int(low0.value().normalEdgeMode), 1);
    CHECK(!low0.value().exportNormalMaps);
    CHECK_EQ(int(low0.value().materialShader), 1);
    CHECK_EQ(low0.value().namePrefix, "LOD0_");
    auto first                            = impostor0.value();
    first.screenRelativeTransitionHeight  = .8f;
    auto second                           = impostor2.value();
    second.screenRelativeTransitionHeight = .2f;
    second.simplifyQuality                = .5f;
    GtsTerrainExportSettings settings;
    REQUIRE(settings.setSourceLods({first, second}).ok());
    REQUIRE(settings.setImpostorLods({low0.value()}).ok());
    CHECK_EQ(settings.getSourceLodCount(), 2);
    auto compiled = settings.compileSourceLevels();
    REQUIRE(compiled.ok());
    CHECK_EQ(compiled.value().size(), 2u);
    CHECK_EQ(compiled.value()[1].quality, .5f);
    CHECK_EQ(compiled.value()[1].screenRelativeTransitionHeight, .2f);
    auto json = settings.snapshotJson();
    REQUIRE(json.ok());
    GtsTerrainExportSettings restored;
    REQUIRE(restored.restoreJson(json.value()).ok());
    auto again = restored.snapshotJson();
    REQUIRE(again.ok());
    CHECK_EQ(again.value(), json.value());
    CHECK_EQ(restored.impostorLodAt(0)->vertexColorSmoothing, 3);
    std::string unknown = json.value();
    unknown.insert(1, "\"unknown\":1,");
    CHECK(!restored.restoreJson(unknown).ok());
    again = restored.snapshotJson();
    REQUIRE(again.ok());
    CHECK_EQ(again.value(), json.value());
    second.screenRelativeTransitionHeight = .9f;
    CHECK(!restored.setSourceLods({first, second}).ok());
    CHECK_EQ(restored.getSourceLodCount(), 2);
    CHECK(!makeGtsTerrainImpostorLod(-1).ok());
    GtsTerrainExportWorkflow workflow;
    workflow.action                  = GtsTerrainConversionAction::ColliderOnly;
    workflow.colliderType            = GtsTerrainColliderType::Mesh;
    workflow.colliderResolution      = GtsTerrainSaveResolution::Quarter;
    workflow.colliderSimplifyQuality = .35f;
    workflow.copyPcgObjects          = false;
    workflow.addTreeColliders        = false;
    workflow.impostorRange           = 5000.0;
    REQUIRE(restored.configureWorkflow(workflow).ok());
    auto workflowJson = restored.snapshotJson();
    REQUIRE(workflowJson.ok());
    GtsTerrainExportSettings workflowRestored;
    REQUIRE(workflowRestored.restoreJson(workflowJson.value()).ok());
    auto decoded = workflowRestored.getWorkflow();
    CHECK_EQ(int(decoded.action), 1);
    CHECK_EQ(int(decoded.colliderType), 0);
    CHECK_EQ(int(decoded.colliderResolution), 2);
    CHECK_EQ(decoded.colliderSimplifyQuality, .35f);
    CHECK(!decoded.copyPcgObjects);
    CHECK_EQ(decoded.impostorRange, 5000.0);
    auto before = workflowRestored.snapshotJson();
    REQUIRE(before.ok());
    workflow.impostorRange = -1.0;
    CHECK(!workflowRestored.configureWorkflow(workflow).ok());
    auto after = workflowRestored.snapshotJson();
    REQUIRE(after.ok());
    CHECK_EQ(after.value(), before.value());
    std::string version2 = workflowJson.value();
    auto        face     = version2.find("\"objFaceMode\":0,");
    REQUIRE(face != std::string::npos);
    version2.erase(face, std::string("\"objFaceMode\":0,").size());
    auto version = version2.find("\"version\":3");
    REQUIRE(version != std::string::npos);
    version2.replace(version, std::string("\"version\":3").size(), "\"version\":2");
    GtsTerrainExportSettings migrated;
    REQUIRE(migrated.restoreJson(version2).ok());
    CHECK_EQ(int(migrated.getWorkflow().objFaceMode), 0);
    auto migratedJson = migrated.snapshotJson();
    REQUIRE(migratedJson.ok());
    CHECK(migratedJson.value().find("\"version\":3") != std::string::npos);
}

TEST_CASE("procgen.gtsTerrainExportSettings.drivesSourceAndColliderGeometry") {
    Heightmap heightmap(17, 17);
    for (int z = 0; z < 17; ++z)
        for (int x = 0; x < 17; ++x) heightmap.setHeight(x, z, float(x + 2 * z) / 48.f);
    auto source = makeGtsTerrainImpostorLod(1);
    REQUIRE(source.ok());
    source.value().screenRelativeTransitionHeight = .02f;
    source.value().simplifyQuality                = 1.f;
    GtsTerrainExportSettings settings;
    REQUIRE(settings.setSourceLods({source.value()}).ok());
    GtsTerrainExportWorkflow workflow;
    workflow.colliderType            = GtsTerrainColliderType::Mesh;
    workflow.colliderResolution      = GtsTerrainSaveResolution::Quarter;
    workflow.colliderSimplifyQuality = .5f;
    REQUIRE(settings.configureWorkflow(workflow).ok());
    GtsTerrainLodSet lods;
    REQUIRE(buildGtsTerrainExportLodsFromHeightmapInto(lods, heightmap, settings, 16.f, 8.f, 16.f, 1,
                                                       GtsMeshPivot::CenterXZ)
                .ok());
    CHECK_EQ(lods.getTileCount(), 4);
    CHECK_EQ(lods.getLevelCount(), 1);
    for (int tile = 0; tile < 4; ++tile) {
        REQUIRE(lods.tileAt(tile));
        CHECK(!lods.tileAt(tile)->levels[0].empty());
    }
    MeshBuild collider;
    REQUIRE(buildGtsTerrainColliderMeshFromHeightmapInto(collider, heightmap, workflow, 16.f, 8.f, 16.f).ok());
    CHECK(!collider.empty());
    CHECK(collider.getVertexCount() <= 25);
    const int before      = collider.getIndexCount();
    workflow.colliderType = GtsTerrainColliderType::Heightfield;
    CHECK(!buildGtsTerrainColliderMeshFromHeightmapInto(collider, heightmap, workflow, 16.f, 8.f, 16.f).ok());
    CHECK_EQ(collider.getIndexCount(), before);
}

TEST_CASE("procgen.gtsTerrainObj.matchesPcgCoordinatesUvsAndFaces") {
    Heightmap heightmap(3, 3);
    heightmap.setHeight(2, 2, 1.f);
    auto triangles =
        encodeGtsTerrainObj(heightmap, GtsTerrainSaveResolution::Full, 4.f, 3.f, 2.f, GtsTerrainObjFaceMode::Triangles);
    REQUIRE(triangles.ok());
    const auto& tri = triangles.value();
    CHECK(tri.starts_with("# Unity terrain OBJ File\n"));
    auto count = [](const std::string& text, const std::string& token) {
        int         total = 0;
        std::size_t at    = 0;
        while ((at = text.find(token, at)) != std::string::npos) {
            ++total;
            at += token.size();
        }
        return total;
    };
    CHECK_EQ(count(tri, "\nv "), 9);
    CHECK_EQ(count(tri, "\nvt "), 9);
    CHECK_EQ(count(tri, "\nf "), 8);
    CHECK(tri.find("v -2 3 4\n") != std::string::npos);
    CHECK(tri.find("vt 1 1\n") != std::string::npos);
    CHECK(tri.find("f 1/1 4/4 2/2\n") != std::string::npos);
    auto quads =
        encodeGtsTerrainObj(heightmap, GtsTerrainSaveResolution::Full, 4.f, 3.f, 2.f, GtsTerrainObjFaceMode::Quads);
    REQUIRE(quads.ok());
    CHECK_EQ(count(quads.value(), "\nf "), 4);
    CHECK(quads.value().find("f 1/1 4/4 5/5 2/2\n") != std::string::npos);
    CHECK(!encodeGtsTerrainObj(heightmap, GtsTerrainSaveResolution::Full, 4.f, 3.f, 2.f,
                               static_cast<GtsTerrainObjFaceMode>(2))
               .ok());
}

TEST_CASE("procgen.gtsTerrainObj.maskedMatchesPcgCellClassificationAndWinding") {
    Heightmap terrain(3, 3);
    for (int z = 0; z < 3; ++z)
        for (int x = 0; x < 3; ++x) terrain.setHeight(x, z, float(x + z * 3) / 8.f);
    Heightmap mask(2, 2);
    mask.setHeight(0, 0, 0.f);
    mask.setHeight(1, 0, 1.f);
    mask.setHeight(0, 1, 1.f);
    mask.setHeight(1, 1, 1.f);
    auto inside  = encodeGtsMaskedTerrainObj(terrain, mask, GtsTerrainSaveResolution::Full, 2.f, 8.f, 2.f,
                                             GtsTerrainObjFaceMode::Triangles, .2f, false);
    auto outside = encodeGtsMaskedTerrainObj(terrain, mask, GtsTerrainSaveResolution::Full, 2.f, 8.f, 2.f,
                                             GtsTerrainObjFaceMode::Quads, .2f, true);
    REQUIRE(inside.ok());
    REQUIRE(outside.ok());
    auto count = [](const std::string& text, const std::string& token) {
        int         total = 0;
        std::size_t at    = 0;
        while ((at = text.find(token, at)) != std::string::npos) {
            ++total;
            at += token.size();
        }
        return total;
    };
    CHECK_EQ(count(inside.value(), "\nf "), 6);
    CHECK_EQ(count(outside.value(), "\nf "), 1);
    CHECK(outside.value().find("v 0 3 -0") != std::string::npos);
    CHECK(outside.value().find("vt 0 0.333333343") != std::string::npos);
    CHECK(outside.value().find("f 1/1 3/3 4/4 2/2\n") != std::string::npos);
    CHECK(!encodeGtsMaskedTerrainObj(terrain, mask, GtsTerrainSaveResolution::Full, 2.f, 8.f, 2.f,
                                     GtsTerrainObjFaceMode::Triangles, 1.1f, false)
               .ok());
    Heightmap empty;
    CHECK(!encodeGtsMaskedTerrainObj(terrain, empty, GtsTerrainSaveResolution::Full, 2.f, 8.f, 2.f,
                                     GtsTerrainObjFaceMode::Triangles, .2f, false)
               .ok());
}

TEST_CASE("procgen.gtsTerrainVertexColors.matchesPcgSmoothAndSharpBake") {
    MeshBuild             source = crossingQuad();
    eve::image::ImageData texture(2, 2, "RGBA32F");
    texture.setPixel(0, 0, {1, 0, 0, 1});
    texture.setPixel(1, 0, {0, 1, 0, 1});
    texture.setPixel(0, 1, {0, 0, 1, 1});
    texture.setPixel(1, 1, {1, 1, 1, 1});
    MeshBuild smooth;
    auto      smoothResult =
        bakeGtsTerrainVertexColorsInto(smooth, source, texture, GtsTerrainNormalEdgeMode::Smooth, 0, 2.f, 2.f, false);
    REQUIRE(smoothResult.ok());
    CHECK_EQ(smoothResult.value(), 4);
    CHECK(smooth.hasVertexColors());
    CHECK_EQ(smooth.getColor(0, 0), 1.f);
    CHECK_EQ(smooth.getColor(0, 1), 0.f);
    CHECK_EQ(smooth.getColor(2, 0), 1.f);
    CHECK_EQ(smooth.getColor(2, 1), 1.f);
    CHECK_EQ(smooth.getColor(2, 2), 1.f);
    MeshBuild sharp;
    auto      sharpResult =
        bakeGtsTerrainVertexColorsInto(sharp, source, texture, GtsTerrainNormalEdgeMode::Sharp, 0, 2.f, 2.f, false);
    REQUIRE(sharpResult.ok());
    CHECK_EQ(sharpResult.value(), 6);
    CHECK_EQ(sharp.getIndexCount(), 6);
    CHECK_EQ(sharp.getIndex(0), 0);
    CHECK_EQ(sharp.getIndex(5), 5);
    CHECK(sharp.hasVertexColors());
    CHECK(std::abs(sharp.getColor(0, 0) - 2.f / 3.f) < 1e-6f);
    CHECK(std::abs(sharp.getColor(0, 1) - 2.f / 3.f) < 1e-6f);
    CHECK(std::abs(sharp.getColor(0, 2) - 1.f / 3.f) < 1e-6f);
    CHECK_EQ(sharp.getColor(0, 0), sharp.getColor(2, 0));
    auto copied = sharp.copyGroup(0);
    REQUIRE(copied);
    CHECK(copied->hasVertexColors());
    auto split = splitGtsMesh(sharp, 1, 1, GtsMeshPivot::None);
    REQUIRE(split.ok());
    for (int tile = 0; tile < split.value().getTileCount(); ++tile) {
        const auto* part = split.value().tileAt(tile);
        REQUIRE(part);
        if (!part->mesh.empty()) CHECK(part->mesh.hasVertexColors());
    }
    MeshBuild simplified;
    REQUIRE(simplifyGtsMesh(simplified, sharp, 1.f).ok());
    CHECK(simplified.hasVertexColors());
    std::vector<GtsTerrainLodLevelSettings> levels{{1.f, .02f, {}}};
    auto                                    lods = buildGtsTerrainLods(sharp, 0, 0, GtsMeshPivot::None, levels);
    REQUIRE(lods.ok());
    auto encoded = lods.value().snapshotJson();
    REQUIRE(encoded.ok());
    CHECK(encoded.value().find("\"version\":2") != std::string::npos);
    GtsTerrainLodSet restored;
    REQUIRE(restored.restoreJson(encoded.value()).ok());
    const auto* restoredTile = restored.tileAt(0);
    REQUIRE(restoredTile);
    REQUIRE_EQ(restoredTile->levels.size(), 1);
    CHECK(restoredTile->levels[0].hasVertexColors());
    CHECK_EQ(restoredTile->levels[0].colors(), sharp.colors());
    const auto before = smooth.colors();
    CHECK(!bakeGtsTerrainVertexColorsInto(smooth, source, texture, static_cast<GtsTerrainNormalEdgeMode>(2), 0, 2.f,
                                          2.f, false)
               .ok());
    CHECK_EQ(smooth.colors(), before);
}
}  // namespace test_gts_mesh_splitter
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_procgen_lsystem)
#include "PrivateTestScope.h"
#include <memory>
#include <set>
#include "RenderImageAudit.h"
#include "graphics/Graphics.h"
#include "graphics/Mesh.h"
#include "graphics/RenderSystem.h"
#include "graphics/RenderSystem3D.h"
#include "graphics/Texture.h"
#include "image/Image.h"
#include "procgen/PointSet.h"
#include "procgen/Procgen.h"
#include "procgen/algorithms/LSystem.h"
#include "window/Window.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// procgen_lsystem.cpp
namespace test_procgen_lsystem_unit {
using namespace eve::procgen;
using namespace eve::graphics;


TEST_CASE("procgen.lsystem.deterministicExpansion") {
    LSystem ls;
    ls.setAxiom("F");
    ls.addRule('F', "F[+F]F[-F]F");
    ls.setIterations(3);
    ls.setSeed(42);

    const std::string a = ls.derive();
    const std::string b = ls.derive();
    CHECK(!a.empty());
    CHECK_EQ(a, b);

    // More iterations must grow the word monotonically.
    ls.setIterations(4);
    const std::string c = ls.derive();
    CHECK(c.size() > a.size());
}

TEST_CASE("procgen.lsystem.stochasticRulesUseSeed") {
    LSystem ls;
    ls.setAxiom("A");
    // Branching grammar so different seeds produce a rich, distinct outcome space.
    ls.addRules('A', {"F[+A]A", "F[-A]A", "FA"}, {1.f, 1.f, 1.f});
    ls.setIterations(4);

    // Determinism for a fixed seed.
    ls.setSeed(7);
    const std::string a = ls.derive();
    ls.setSeed(7);
    const std::string b = ls.derive();
    CHECK_EQ(a, b);
    CHECK(!a.empty());

    // Across several seeds the stochastic grammar should not all collapse to one.
    std::set<std::string> seen;
    for (int s = 1; s <= 8; ++s) {
        ls.setSeed(s);
        seen.insert(ls.derive());
    }
    CHECK(seen.size() >= 2);
}

TEST_CASE("procgen.lsystem.turtleProducesSegments") {
    LSystem ls;
    ls.setAxiom("F[+F]F[-F]F");
    ls.addRule('F', "F");
    ls.setIterations(1);
    ls.setAngle(25.f);
    ls.setStep(1.f);
    ls.setSeed(1);

    LSystemResult result;
    ls.generate(result);
    CHECK(!result.segments.empty());
    CHECK(!result.derivation.empty());

    // Bracketed depth increases then returns: trunk segment is at depth 0.
    bool sawDepthZero = false;
    bool sawDeep      = false;
    for (const auto& seg : result.segments) {
        if (seg.depth == 0) sawDepthZero = true;
        if (seg.depth > 0) sawDeep = true;
        if (seg.leaf) CHECK(seg.leafSize > 0.f);
    }
    CHECK(sawDepthZero);
    CHECK(sawDeep);
}

TEST_CASE("procgen.lsystem.meshRecipeIsDeterministic") {
    Procgen proc;

    Params p1;
    p1.setSeed(123);
    p1.setString("style", "tree");
    p1.setInt("iterations", 4);
    p1.setString("leafMode", "cards");

    Params p2 = p1;

    auto p1Result = Procgen::newParamsHandle();
    auto p2Result = Procgen::newParamsHandle();
    REQUIRE(p1Result.ok());
    REQUIRE(p2Result.ok());
    auto p1Handle               = std::move(p1Result).takeValue();
    auto p2Handle               = std::move(p2Result).takeValue();
    *Procgen::resolve(p1Handle) = p1;
    *Procgen::resolve(p2Handle) = p2;
    auto m1Result               = proc.buildMeshHandle("mesh.lsystem", p1Handle);
    auto m2Result               = proc.buildMeshHandle("mesh.lsystem", p2Handle);
    REQUIRE(m1Result.ok());
    REQUIRE(m2Result.ok());
    auto m1Handle = std::move(m1Result).takeValue();
    auto m2Handle = std::move(m2Result).takeValue();
    auto m1       = proc.resolveMeshBuild(m1Handle);
    auto m2       = proc.resolveMeshBuild(m2Handle);
    REQUIRE(m1.isBound());
    REQUIRE(m2.isBound());
    CHECK(m1->getVertexCount() > 0);
    CHECK_EQ(m1->getVertexCount(), m2->getVertexCount());
    CHECK_EQ(m1->getIndexCount(), m2->getIndexCount());
    CHECK_EQ(m1->getMeta("recipe", ""), std::string("mesh.lsystem"));
    REQUIRE(proc.releaseMeshBuild(m2Handle).ok());
    REQUIRE(proc.releaseMeshBuild(m1Handle).ok());
    REQUIRE(Procgen::release(p2Handle).ok());
    REQUIRE(Procgen::release(p1Handle).ok());
}

TEST_CASE("procgen.lsystem.meshRecipeReportsErrors") {
    Procgen proc;
    Params  params;
    params.setSeed(1);
    params.setString("style", "bogus");
    auto paramsResult = Procgen::newParamsHandle();
    REQUIRE(paramsResult.ok());
    auto paramsHandle               = std::move(paramsResult).takeValue();
    *Procgen::resolve(paramsHandle) = params;
    auto meshResult                 = proc.buildMeshHandle("mesh.lsystem", paramsHandle);
    REQUIRE(!meshResult.ok());
    CHECK(meshResult.status().describe().find("unknown style") != std::string::npos);
    REQUIRE(Procgen::release(paramsHandle).ok());
}

TEST_CASE("procgen.lsystem.scriptFacade") {
    Procgen proc;
    auto    result = proc.newLSystemHandle();
    REQUIRE(result.ok());
    auto handle = std::move(result).takeValue();
    auto ls     = proc.resolveLSystem(handle);
    REQUIRE(ls.isBound());
    ls->setAxiom("F");
    ls->addRule('F', "F[+F]F[-F]F");
    ls->setIterations(2);
    ls->setSeed(5);

    PointSet points;
    ls->toPointSet(points);
    CHECK(points.getCount() > 0);
    REQUIRE(proc.release(handle).ok());
}
}  // namespace test_procgen_lsystem_unit
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_procgen_mesh_boolean)
#include "PrivateTestScope.h"
#include <zeroerr/unittest.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>
#include "common/Capability.h"
#include "common/MeshDeformationCompute.h"
#include "procgen/Params.h"
#include "procgen/algorithms/MarchingCubes.h"
#include "procgen/mesh/MeshAdhereLive.h"
#include "procgen/mesh/MeshBoolean.h"
#include "procgen/mesh/MeshContactBlend.h"
#include "procgen/mesh/MeshDeformationSession.h"
#include "procgen/mesh/MeshMerge.h"
#include "procgen/mesh/MeshModifierGraph.h"
#include "zeroerr/unittest.h"
// procgen_mesh_boolean.cpp
namespace test_procgen_mesh_boolean {
// procgen_mesh_boolean.cpp
namespace {

eve::procgen::MeshBuild cube(float centerX) {
    eve::procgen::MeshBuild mesh;
    const float             x0 = centerX - 1.f, x1 = centerX + 1.f;
    const float             positions[8][3] = {{x0, -1, -1}, {x1, -1, -1}, {x1, 1, -1}, {x0, 1, -1},
                                               {x0, -1, 1},  {x1, -1, 1},  {x1, 1, 1},  {x0, 1, 1}};
    for (const auto& p : positions) mesh.addVertex(p[0], p[1], p[2], 0.f, 1.f, 0.f, 0.f, 0.f);
    const int triangles[12][3] = {{0, 2, 1}, {0, 3, 2}, {4, 5, 6}, {4, 6, 7}, {0, 1, 5}, {0, 5, 4},
                                  {3, 7, 6}, {3, 6, 2}, {0, 4, 7}, {0, 7, 3}, {1, 2, 6}, {1, 6, 5}};
    mesh.setActiveGroup("shell");
    for (const auto& triangle : triangles) mesh.addTriangle(triangle[0], triangle[1], triangle[2]);
    return mesh;
}

double signedVolume(const eve::procgen::MeshBuild& mesh) {
    double volume = 0.0;
    for (int i = 0; i < mesh.getIndexCount(); i += 3) {
        const int    a = mesh.getIndex(i), b = mesh.getIndex(i + 1), c = mesh.getIndex(i + 2);
        const double ax = mesh.getPositionX(a), ay = mesh.getPositionY(a), az = mesh.getPositionZ(a);
        const double bx = mesh.getPositionX(b), by = mesh.getPositionY(b), bz = mesh.getPositionZ(b);
        const double cx = mesh.getPositionX(c), cy = mesh.getPositionY(c), cz = mesh.getPositionZ(c);
        volume += ax * (by * cz - bz * cy) + ay * (bz * cx - bx * cz) + az * (bx * cy - by * cx);
    }
    return volume / 6.0;
}

}  // namespace

TEST_CASE("procgen.meshBoolean.closesUnionDifferenceAndIntersection") {
    const auto left         = cube(0.f);
    const auto right        = cube(1.f);
    auto       unionMesh    = eve::procgen::meshBooleanResult(left, right, "union");
    auto       difference   = eve::procgen::meshBooleanResult(left, right, "difference");
    auto       intersection = eve::procgen::meshBooleanResult(left, right, "intersection");
    REQUIRE(unionMesh.ok());
    REQUIRE(difference.ok());
    REQUIRE(intersection.ok());
    CHECK(std::abs(std::abs(signedVolume(unionMesh.value())) - 12.0) < 0.01);
    CHECK(std::abs(std::abs(signedVolume(difference.value())) - 4.0) < 0.01);
    CHECK(std::abs(std::abs(signedVolume(intersection.value())) - 4.0) < 0.01);
    CHECK_EQ(difference.value().getMeta("boolean.operation", ""), std::string("difference"));
    CHECK(difference.value().getGroupCount() >= 2);
}

TEST_CASE("procgen.meshBoolean.rejectsInvalidInputsWithoutMutation") {
    const auto              left = cube(0.f);
    eve::procgen::MeshBuild empty;
    CHECK(!eve::procgen::meshBooleanResult(left, empty, "difference").ok());
    CHECK(!eve::procgen::meshBooleanResult(left, left, "xor").ok());
    CHECK_EQ(left.getVertexCount(), 8);
    CHECK_EQ(left.getIndexCount(), 36);
}

TEST_CASE("procgen.meshBoolean.executesThroughModifierGraph") {
    eve::procgen::MeshModifierGraph graph;
    REQUIRE(graph.addNode("left", "mesh.input").ok());
    REQUIRE(graph.addNode("right", "mesh.input").ok());
    REQUIRE(graph.addNode("cut", "mesh.boolean").ok());
    REQUIRE(graph.addNode("out", "mesh.output").ok());
    REQUIRE(graph.connect("left", "cut", 0).ok());
    REQUIRE(graph.connect("right", "cut", 1).ok());
    REQUIRE(graph.connect("cut", "out", 0).ok());
    REQUIRE(graph.setNodeString("cut", "operation", "difference").ok());
    REQUIRE(graph.setNodeMesh("left", cube(0.f)).ok());
    REQUIRE(graph.setNodeMesh("right", cube(1.f)).ok());
    auto result = graph.executeResult("out");
    REQUIRE(result.ok());
    CHECK(std::abs(std::abs(signedVolume(result.value())) - 4.0) < 0.01);
    CHECK_EQ(graph.compiledSegmentCount(), 4);
}

// procgen_mesh_deformation_geometry.cpp
using namespace eve::procgen;

TEST_CASE("procgen.meshDeformationGeometry.extendedPlaneSupportsCurvatureAndSharpNormals") {
    auto& registry = MeshRecipeRegistry::instance();
    registry.registerBuiltins();
    Params smooth;
    smooth.setInt("segmentsX", 4);
    smooth.setInt("segmentsZ", 3);
    smooth.setFloat("width", 4.f);
    smooth.setFloat("depth", 3.f);
    smooth.setFloat("curvature", 1.f);
    MeshBuild   smoothMesh;
    std::string error;
    REQUIRE(registry.generate("mesh.extendedPlane", smooth, smoothMesh, error));
    CHECK_EQ(smoothMesh.getVertexCount(), 20);
    CHECK_EQ(smoothMesh.getIndexCount(), 72);
    float maximumY = 0.f;
    for (int index = 0; index < smoothMesh.getVertexCount(); ++index)
        maximumY = std::max(maximumY, smoothMesh.getPositionY(index));
    CHECK(maximumY > 0.99f);

    Params sharp = smooth;
    sharp.setBool("sharpNormals", true);
    MeshBuild sharpMesh;
    REQUIRE(registry.generate("mesh.extendedPlane", sharp, sharpMesh, error));
    CHECK_EQ(sharpMesh.getVertexCount(), 48);
    CHECK_EQ(sharpMesh.getMeta("normalMode", ""), "sharp");
}

TEST_CASE("procgen.meshDeformationGeometry.hexGridIsDeterministicAndConfigurable") {
    auto& registry = MeshRecipeRegistry::instance();
    registry.registerBuiltins();
    Params params;
    params.setInt("columns", 3);
    params.setInt("rows", 2);
    params.setFloat("radius", 0.5f);
    params.setFloat("height", 0.2f);
    params.setFloat("randomHeight", 1.f);
    params.setSeed(77);
    MeshBuild   a, b;
    std::string error;
    REQUIRE(registry.generate("mesh.hexGrid", params, a, error));
    REQUIRE(registry.generate("mesh.hexGrid", params, b, error));
    CHECK_EQ(a.positions(), b.positions());
    CHECK_EQ(a.getVertexCount(), 42);
    CHECK_EQ(a.getIndexCount(), 108);

    params.setBool("planar", true);
    params.setBool("flipFaces", true);
    MeshBuild planar;
    REQUIRE(registry.generate("mesh.hexGrid", params, planar, error));
    for (int index = 0; index < planar.getVertexCount(); ++index) CHECK(std::abs(planar.getPositionY(index)) < 1e-6f);
    CHECK(planar.getNormalY(0) < 0.f);
}

// procgen_mesh_deformation_session.cpp
using namespace eve;
using namespace eve::procgen;

namespace {

MeshBuild sculptPlane() {
    MeshBuild mesh;
    mesh.addVertex(-1.f, 0.f, -1.f, 0.f, 1.f, 0.f, 0.f, 0.f);
    mesh.addVertex(1.f, 0.f, -1.f, 0.f, 1.f, 0.f, 1.f, 0.f);
    mesh.addVertex(0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.5f, 0.5f);
    mesh.addVertex(-1.f, 0.f, 1.f, 0.f, 1.f, 0.f, 0.f, 1.f);
    mesh.addVertex(1.f, 0.f, 1.f, 0.f, 1.f, 0.f, 1.f, 1.f);
    mesh.addTriangle(0, 2, 1);
    mesh.addTriangle(0, 3, 2);
    mesh.addTriangle(2, 3, 4);
    mesh.addTriangle(1, 2, 4);
    return mesh;
}

class FakeMeshCompute final : public IMeshDeformationCompute {
public:
    Result<std::vector<float>> deform(MeshDeformationComputeRequest request) override {
        ++calls;
        lastOperation = request.operation;
        for (std::size_t i = 1; i < request.positions.size(); i += 3u) request.positions[i] += 0.25f;
        return Result<std::vector<float>>::success(std::move(request.positions));
    }
    int                             calls         = 0;
    MeshDeformationComputeOperation lastOperation = MeshDeformationComputeOperation::Inflate;
};

}  // namespace

TEST_CASE("procgen.meshDeformationSession.gpuProviderIsExplicitAndCommitsAtomically") {
    auto* previous = cap::query<IMeshDeformationCompute>();
    if (previous) cap::revoke<IMeshDeformationCompute>(previous);
    MeshDeformationSession session;
    REQUIRE(session.initializeResult(sculptPlane()).ok());
    const auto revision = session.revision();
    auto       absent   = session.applyBrushGpuResult("inflate", 0.f, 0.f, 0.f, 1.f, 1.f, 1.f);
    REQUIRE(!absent.ok());
    CHECK_EQ(session.revision(), revision);

    FakeMeshCompute provider;
    cap::provide<IMeshDeformationCompute>(&provider);
    REQUIRE(session.applyBrushGpuResult("smooth", 0.f, 0.f, 0.f, 1.f, 1.f, 1.f).ok());
    CHECK_EQ(provider.calls, 1);
    CHECK(provider.lastOperation == MeshDeformationComputeOperation::Smooth);
    CHECK(std::abs(session.currentMeshResult().value().getPositionY(2) - 0.25f) < 1e-6f);
    REQUIRE(session.applyImpactGpuResult(0.f, 0.f, 0.f, 0.f, -1.f, 0.f, 1.f, 1.f, 1.f, 1.f).ok());
    CHECK(provider.lastOperation == MeshDeformationComputeOperation::Impact);
    CHECK_EQ(session.undoCount(), 2);
    cap::revoke<IMeshDeformationCompute>(&provider);
    if (previous) cap::provide<IMeshDeformationCompute>(previous);
}

TEST_CASE("procgen.meshDeformationSession.sculptsUndoesRestoresAndBakes") {
    MeshDeformationSession session;
    REQUIRE(session.initializeResult(sculptPlane()).ok());
    CHECK(session.isInitialized());
    const auto initialRevision = session.revision();

    REQUIRE(session.applyBrushResult("inflate", 0.f, 0.f, 0.f, 0.75f, 1.f, 1.f).ok());
    auto sculpted = session.currentMeshResult();
    REQUIRE(sculpted.ok());
    CHECK(sculpted.value().getPositionY(2) > 0.99f);
    CHECK_EQ(session.undoCount(), 1);
    CHECK(session.revision() > initialRevision);

    REQUIRE(session.undoResult().ok());
    auto undone = session.currentMeshResult();
    REQUIRE(undone.ok());
    CHECK(std::abs(undone.value().getPositionY(2)) < 1e-6f);

    REQUIRE(session.applyBrushResult("dent", 0.f, 0.f, 0.f, 0.75f, 0.5f, 1.f).ok());
    REQUIRE(session.bakeResult().ok());
    CHECK_EQ(session.undoCount(), 0);
    REQUIRE(session.applyBrushResult("directional", 0.f, 0.f, 0.f, 0.75f, 0.5f, 1.f, 1.f, 0.f, 0.f).ok());
    REQUIRE(session.restoreResult().ok());
    auto restored = session.currentMeshResult();
    REQUIRE(restored.ok());
    CHECK(restored.value().getPositionY(2) < -0.49f);
    CHECK(std::abs(restored.value().getPositionX(2)) < 1e-6f);
}

TEST_CASE("procgen.meshDeformationSession.rejectsInvalidEditsAtomically") {
    MeshDeformationSession session;
    auto                   beforeInit = session.applyBrushResult("inflate", 0.f, 0.f, 0.f, 1.f, 1.f, 1.f);
    REQUIRE(!beforeInit.ok());
    REQUIRE(session.initializeResult(sculptPlane()).ok());
    const auto revision = session.revision();
    auto       invalid  = session.applyBrushResult("unknown", 0.f, 0.f, 0.f, 1.f, 1.f, 1.f);
    REQUIRE(!invalid.ok());
    CHECK_EQ(session.revision(), revision);
    CHECK_EQ(session.undoCount(), 0);
}

TEST_CASE("procgen.meshDeformationSession.accumulatesBoundedPlasticImpacts") {
    MeshDeformationSession session;
    REQUIRE(session.initializeResult(sculptPlane()).ok());
    REQUIRE(session.applyImpactResult(0.f, 0.f, 0.f, 0.f, -4.f, 0.f, 0.8f, 0.5f, 1.5f, 0.3f).ok());
    auto first = session.currentMeshResult();
    REQUIRE(first.ok());
    CHECK(std::abs(first.value().getPositionY(2) + 0.3f) < 1e-6f);
    REQUIRE(session.applyImpactResult(0.f, -0.3f, 0.f, 0.f, -4.f, 0.f, 0.8f, 0.5f, 1.5f, 0.3f).ok());
    auto second = session.currentMeshResult();
    REQUIRE(second.ok());
    CHECK(std::abs(second.value().getPositionY(2) + 0.3f) < 1e-6f);
    CHECK_EQ(session.undoCount(), 2);
    REQUIRE(session.recoverSurfaceResult(1.f, 100.f).ok());
    auto afterRecovery = session.currentMeshResult();
    REQUIRE(afterRecovery.ok());
    CHECK(std::abs(afterRecovery.value().getPositionY(2) + 0.3f) < 1e-6f);
}

TEST_CASE("procgen.meshDeformationSession.vertexBlocksMatchFullImpactAndInvalidateOnUnindexedEdits") {
    MeshDeformationSession fullScan;
    MeshDeformationSession blocked;
    REQUIRE(fullScan.initializeResult(sculptPlane()).ok());
    REQUIRE(blocked.initializeResult(sculptPlane()).ok());
    REQUIRE(blocked.prepareImpactVertexBlocksResult(4).ok());
    CHECK(blocked.hasImpactVertexBlocks());
    CHECK(blocked.impactVertexBlockCount() > 0);
    REQUIRE(fullScan.applyImpactResult(0.f, 0.f, 0.f, 0.f, -2.f, 0.f, 0.8f, 0.25f, 1.5f, 0.4f).ok());
    REQUIRE(blocked.applyImpactResult(0.f, 0.f, 0.f, 0.f, -2.f, 0.f, 0.8f, 0.25f, 1.5f, 0.4f).ok());
    auto expected = fullScan.currentMeshResult();
    auto actual   = blocked.currentMeshResult();
    REQUIRE(expected.ok());
    REQUIRE(actual.ok());
    CHECK_EQ(expected.value().positions(), actual.value().positions());
    CHECK(blocked.hasImpactVertexBlocks());

    REQUIRE(blocked.applyBrushResult("inflate", 0.f, 0.f, 0.f, 0.8f, 0.1f, 1.f).ok());
    CHECK(!blocked.hasImpactVertexBlocks());
    const auto revision = blocked.revision();
    CHECK(!blocked.prepareImpactVertexBlocksResult(0).ok());
    CHECK_EQ(blocked.revision(), revision);
}

TEST_CASE("procgen.meshDeformationSession.interactiveSurfaceRecoversElasticAndPreservesPlasticDisplacement") {
    MeshDeformationSession session;
    REQUIRE(session.initializeResult(sculptPlane()).ok());
    REQUIRE(session.applySurfaceContactResult(0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 1.f, 0.f, 0.f, 0.8f, 1.f, 0.5f, 1.f, 0.25f)
                .ok());
    auto contacted = session.currentMeshResult();
    REQUIRE(contacted.ok());
    CHECK(std::abs(contacted.value().getPositionX(2) - 0.5f) < 1e-6f);
    CHECK(std::abs(contacted.value().getPositionY(2) + 1.f) < 1e-6f);
    const auto revision = session.revision();
    REQUIRE(session.recoverSurfaceResult(1.f, 100.f).ok());
    auto recovered = session.currentMeshResult();
    REQUIRE(recovered.ok());
    CHECK(std::abs(recovered.value().getPositionX(2) - 0.125f) < 1e-5f);
    CHECK(std::abs(recovered.value().getPositionY(2) + 0.25f) < 1e-5f);
    CHECK(session.revision() > revision);
    CHECK_EQ(session.undoCount(), 1);
    REQUIRE(session.undoResult().ok());
    auto undone = session.currentMeshResult();
    REQUIRE(undone.ok());
    CHECK(std::abs(undone.value().getPositionX(2)) < 1e-6f);
    CHECK(std::abs(undone.value().getPositionY(2)) < 1e-6f);
}

TEST_CASE("procgen.meshDeformationSession.interactiveSurfaceRejectsInvalidContactAtomically") {
    MeshDeformationSession session;
    REQUIRE(session.initializeResult(sculptPlane()).ok());
    const auto revision = session.revision();
    CHECK(
        !session.applySurfaceContactResult(0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 1.f, 1.f, 0.f, 1.f, 0.f).ok());
    CHECK(!session.recoverSurfaceResult(-1.f, 1.f).ok());
    CHECK_EQ(session.revision(), revision);
    CHECK_EQ(session.undoCount(), 0);
}

TEST_CASE("procgen.meshDeformationSession.meshSlimeUsesBoundedDeterministicSpringDynamics") {
    MeshDeformationSession first;
    MeshDeformationSession second;
    REQUIRE(first.initializeResult(sculptPlane()).ok());
    REQUIRE(second.initializeResult(sculptPlane()).ok());
    for (auto* session : {&first, &second}) {
        REQUIRE(session->applySlimeImpulseResult(0.f, 0.f, 0.f, 1.f, 2.f, 0.f, 0.8f, 1.f).ok());
        for (int step = 0; step < 20; ++step) REQUIRE(session->stepSlimeResult(0.01f, 12.f, 1.5f, 3.f).ok());
    }
    auto a = first.currentMeshResult();
    auto b = second.currentMeshResult();
    REQUIRE(a.ok());
    REQUIRE(b.ok());
    CHECK(a.value().positions() == b.value().positions());
    CHECK(a.value().getPositionX(2) > 0.f);
    CHECK(a.value().getPositionY(2) > 0.f);
    CHECK(a.value().getPositionY(2) < 0.6f);
    CHECK_EQ(first.undoCount(), 1);
    REQUIRE(first.undoResult().ok());
    CHECK(std::abs(first.currentMeshResult().value().getPositionY(2)) < 1e-6f);
}

TEST_CASE("procgen.meshDeformationSession.colliderRefreshSupportsAllModesAndOwningOffsetSnapshot") {
    MeshDeformationSession session;
    REQUIRE(session.initializeResult(sculptPlane()).ok());
    REQUIRE(session.configureColliderRefreshResult("once", 0.f, 1.f, -2.f, 3.f).ok());
    CHECK(session.updateColliderRefreshResult(0.f).value());
    CHECK(!session.updateColliderRefreshResult(0.f).value());
    auto collider = session.colliderMeshResult();
    REQUIRE(collider.ok());
    CHECK(std::abs(collider.value().getPositionX(2) - 1.f) < 1e-6f);
    CHECK(std::abs(collider.value().getPositionY(2) + 2.f) < 1e-6f);
    CHECK(std::abs(collider.value().getPositionZ(2) - 3.f) < 1e-6f);
    CHECK_EQ(collider.value().getMeta("purpose", ""), "dynamicCollider");

    REQUIRE(session.configureColliderRefreshResult("everyFrame", 0.f).ok());
    CHECK(session.updateColliderRefreshResult(0.f).value());
    CHECK(session.updateColliderRefreshResult(0.016f).value());
    REQUIRE(session.configureColliderRefreshResult("interval", 0.1f).ok());
    const float scriptLikeA = static_cast<float>(0.04);
    const float scriptLikeB = static_cast<float>(0.06);
    CHECK(!session.updateColliderRefreshResult(scriptLikeA).value());
    CHECK(session.updateColliderRefreshResult(scriptLikeB).value());
    CHECK(!session.updateColliderRefreshResult(0.01f).value());
    REQUIRE(session.configureColliderRefreshResult("manual", 0.f).ok());
    CHECK(!session.updateColliderRefreshResult(1.f).value());
    REQUIRE(session.requestColliderRefreshResult().ok());
    CHECK(session.updateColliderRefreshResult(0.f).value());
    CHECK(!session.updateColliderRefreshResult(0.f).value());
}

TEST_CASE("procgen.meshDeformationSession.colliderRefreshRejectsInvalidConfigurationAtomically") {
    MeshDeformationSession session;
    REQUIRE(session.initializeResult(sculptPlane()).ok());
    const auto revision = session.revision();
    CHECK(!session.configureColliderRefreshResult("interval", 0.f).ok());
    CHECK(!session.configureColliderRefreshResult("unknown", 1.f).ok());
    CHECK(!session.requestColliderRefreshResult().ok());
    CHECK_EQ(session.revision(), revision);
}

TEST_CASE("procgen.meshDeformationSession.runtimeVertexEditorSelectsMovesAndManipulatesUndoably") {
    MeshDeformationSession session;
    REQUIRE(session.initializeResult(sculptPlane()).ok());
    auto sphere = session.selectVerticesSphereResult(0.f, 0.f, 0.f, 0.1f, true);
    REQUIRE(sphere.ok());
    CHECK_EQ(sphere.value(), 1);
    REQUIRE(session.moveSelectedVerticesResult(1.f, 0.5f, 0.f).ok());
    CHECK(std::abs(session.currentMeshResult().value().getPositionX(2) - 1.f) < 1e-6f);
    CHECK(std::abs(session.currentMeshResult().value().getPositionY(2) - 0.5f) < 1e-6f);
    REQUIRE(session.undoResult().ok());
    CHECK(std::abs(session.currentMeshResult().value().getPositionX(2)) < 1e-6f);

    auto box = session.selectVerticesBoxResult(-1.1f, -0.1f, -1.1f, 0.1f, 0.1f, 1.1f, true);
    REQUIRE(box.ok());
    CHECK_EQ(box.value(), 3);
    REQUIRE(session.manipulateSelectedVerticesResult("pull", 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.25f).ok());
    CHECK(session.currentMeshResult().value().getPositionX(0) < -1.f);
    REQUIRE(session.manipulateSelectedVerticesResult("grab", 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.5f).ok());
    CHECK(session.currentMeshResult().value().getPositionY(2) > 0.49f);
    session.clearVertexSelection();
    CHECK_EQ(session.selectedVertexCount(), 0);
    CHECK(!session.moveSelectedVerticesResult(1.f, 0.f, 0.f).ok());
}

TEST_CASE("procgen.meshDeformationSession.runtimeVertexEditorRejectsInvalidSelectionAtomically") {
    MeshDeformationSession session;
    REQUIRE(session.initializeResult(sculptPlane()).ok());
    const auto revision = session.revision();
    CHECK(!session.selectVerticesSphereResult(0.f, 0.f, 0.f, -1.f).ok());
    CHECK(!session.selectVerticesBoxResult(1.f, 0.f, 0.f, -1.f, 1.f, 1.f).ok());
    CHECK(!session.manipulateSelectedVerticesResult("warp", 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 1.f).ok());
    CHECK_EQ(session.revision(), revision);
}

// procgen_mesh_merge_adhere.cpp
namespace {

eve::procgen::MeshBuild unitCube(float cx, float cy, float cz, float half = 0.5f) {
    eve::procgen::MeshBuild mesh;
    const float             x0 = cx - half, x1 = cx + half;
    const float             y0 = cy - half, y1 = cy + half;
    const float             z0 = cz - half, z1 = cz + half;
    const float             positions[8][3] = {{x0, y0, z0}, {x1, y0, z0}, {x1, y1, z0}, {x0, y1, z0},
                                               {x0, y0, z1}, {x1, y0, z1}, {x1, y1, z1}, {x0, y1, z1}};
    for (const auto& p : positions) mesh.addVertex(p[0], p[1], p[2], 0.f, 1.f, 0.f, 0.f, 0.f);
    const int triangles[12][3] = {{0, 2, 1}, {0, 3, 2}, {4, 5, 6}, {4, 6, 7}, {0, 1, 5}, {0, 5, 4},
                                  {3, 7, 6}, {3, 6, 2}, {0, 4, 7}, {0, 7, 3}, {1, 2, 6}, {1, 6, 5}};
    mesh.setActiveGroup("shell");
    for (const auto& triangle : triangles) mesh.addTriangle(triangle[0], triangle[1], triangle[2]);
    return mesh;
}

eve::procgen::MeshBuild flatPlaneY0(float half = 2.f) {
    eve::procgen::MeshBuild mesh;
    mesh.addVertex(-half, 0.f, -half, 0.f, 1.f, 0.f, 0.f, 0.f);
    mesh.addVertex(half, 0.f, -half, 0.f, 1.f, 0.f, 1.f, 0.f);
    mesh.addVertex(half, 0.f, half, 0.f, 1.f, 0.f, 1.f, 1.f);
    mesh.addVertex(-half, 0.f, half, 0.f, 1.f, 0.f, 0.f, 1.f);
    mesh.setActiveGroup("ground");
    mesh.addTriangle(0, 1, 2);
    mesh.addTriangle(0, 2, 3);
    return mesh;
}

}  // namespace

TEST_CASE("procgen.meshMerge.defaultSkipsContactBlend") {
    eve::procgen::MeshMergePlan plan;
    REQUIRE(plan.appendSource(unitCube(0.f, 0.f, 0.f), 0.f, 0.f, 0.f, 0.f, 1.f, 1.f, 1.f, "a").ok());
    REQUIRE(plan.appendSource(unitCube(1.2f, 0.f, 0.f), 0.f, 0.f, 0.f, 0.f, 1.f, 1.f, 1.f, "b").ok());
    CHECK(!plan.getEnableContactBlend());
    auto merged = eve::procgen::mergeStaticMeshes(plan);
    REQUIRE(merged.ok());
    CHECK_EQ(merged.value().getMeta("contactBlend.enabled", ""), std::string("0"));
    CHECK(!merged.value().hasVertexColors());
    CHECK_EQ(merged.value().getVertexCount(), 16);
}

TEST_CASE("procgen.meshMerge.emptyPlanFails") {
    eve::procgen::MeshMergePlan plan;
    CHECK(!eve::procgen::mergeStaticMeshes(plan).ok());
}

TEST_CASE("procgen.meshMerge.optInContactBlendWritesWeights") {
    eve::procgen::MeshMergePlan plan;
    REQUIRE(plan.appendSource(unitCube(0.f, 0.f, 0.f), 0.f, 0.f, 0.f, 0.f, 1.f, 1.f, 1.f, "a").ok());
    REQUIRE(plan.appendSource(unitCube(0.9f, 0.f, 0.f), 0.f, 0.f, 0.f, 0.f, 1.f, 1.f, 1.f, "b").ok());
    REQUIRE(plan.setEnableContactBlend(true).ok());
    eve::procgen::MeshContactBlendParams params;
    params.edgeRadius     = 0.75f;
    params.materialRadius = 0.75f;
    params.strength       = 1.f;
    params.normalsBlend   = 1.f;
    params.materialBlend  = 1.f;
    params.falloff        = "linear";
    REQUIRE(plan.setContactBlendParams(params).ok());
    // Keep world positions for contact distance (skip first-source pivot).
    REQUIRE(plan.setPivotMode(eve::procgen::MeshMergePlan::PivotMode::WorldOrigin).ok());
    auto merged = eve::procgen::mergeStaticMeshes(plan);
    REQUIRE(merged.ok());
    CHECK_EQ(merged.value().getMeta("contactBlend.enabled", ""), std::string("1"));
    REQUIRE(merged.value().hasVertexColors());
    int positive = 0;
    for (int v = 0; v < merged.value().getVertexCount(); ++v) {
        const float w = merged.value().getColor(v, 3);
        CHECK(w >= 0.f);
        CHECK(w <= 1.f);
        if (w > 0.f) ++positive;
    }
    CHECK(positive > 0);
}

TEST_CASE("procgen.meshContactBlend.againstSurfaceSoftSnap") {
    const auto                           cube   = unitCube(0.f, 0.4f, 0.f, 0.25f);
    const auto                           ground = flatPlaneY0();
    eve::procgen::MeshContactBlendParams params;
    params.edgeRadius        = 1.f;
    params.materialRadius    = 1.f;
    params.strength          = 1.f;
    params.normalsBlend      = 1.f;
    params.materialBlend     = 1.f;
    params.softSnapPositions = true;
    params.falloff           = "smooth";
    auto adhered             = eve::procgen::meshContactBlendAgainstSurfaceResult(cube, ground, params);
    REQUIRE(adhered.ok());
    float minY = 1e9f;
    for (int v = 0; v < adhered.value().getVertexCount(); ++v) minY = std::min(minY, adhered.value().getPositionY(v));
    CHECK(minY < 0.2f);
    REQUIRE(adhered.value().hasVertexColors());
    CHECK_EQ(adhered.value().getMeta("contactBlend.normals", ""), std::string("recalculated+smoothed"));
}

TEST_CASE("procgen.meshMerge.softSnapThenWeldSharesContactVerts") {
    eve::procgen::MeshMergePlan plan;
    REQUIRE(plan.appendSource(unitCube(-0.55f, 0.f, 0.f), 0.f, 0.f, 0.f, 0.f, 1.f, 1.f, 1.f, "a").ok());
    REQUIRE(plan.appendSource(unitCube(0.55f, 0.f, 0.f), 0.f, 0.f, 0.f, 0.f, 1.f, 1.f, 1.f, "b").ok());
    REQUIRE(plan.setEnableContactBlend(true).ok());
    eve::procgen::MeshContactBlendParams params;
    params.edgeRadius        = 1.2f;
    params.materialRadius    = 1.2f;
    params.strength          = 1.f;
    params.normalsBlend      = 1.f;
    params.materialBlend     = 1.f;
    params.softSnapPositions = true;
    params.falloff           = "smooth";
    REQUIRE(plan.setContactBlendParams(params).ok());
    REQUIRE(plan.setWeldTolerance(0.08f).ok());
    REQUIRE(plan.setPivotMode(eve::procgen::MeshMergePlan::PivotMode::WorldOrigin).ok());
    auto merged = eve::procgen::mergeStaticMeshes(plan);
    REQUIRE(merged.ok());
    CHECK_EQ(merged.value().getMeta("contactBlend.normals", ""), std::string("recalculated+smoothed"));
    // Two unit cubes start with 16 verts; post-blend weld must collapse some contact verts.
    CHECK(merged.value().getVertexCount() < 16);
}

TEST_CASE("procgen.meshAdhereLive.paramsChangeRevisionAndRemoveRestores") {
    eve::procgen::MeshAdhereLive live;
    const auto                   cube   = unitCube(0.f, 0.5f, 0.f, 0.25f);
    const auto                   ground = flatPlaneY0();
    REQUIRE(live.activateResult(cube, ground).ok());
    REQUIRE(live.evaluateResult(false).ok());
    const auto rev1 = live.revision();
    REQUIRE(live.derivedMesh() != nullptr);
    const float y1 = live.derivedMesh()->getPositionY(0);

    eve::procgen::MeshContactBlendParams strong = live.params();
    strong.strength                             = 1.f;
    strong.edgeRadius                           = 2.f;
    strong.materialRadius                       = 2.f;
    strong.softSnapPositions                    = true;
    REQUIRE(live.setParamsResult(strong).ok());
    CHECK(live.isDirty());
    REQUIRE(live.evaluateResult(false).ok());
    CHECK(live.revision() > rev1);
    REQUIRE(live.derivedMesh() != nullptr);
    const float y2 = live.derivedMesh()->getPositionY(0);
    CHECK(live.revision() > rev1);
    (void)y1;
    (void)y2;

    auto snapshot = live.derivedMeshResult();
    REQUIRE(snapshot.ok());
    CHECK_EQ(snapshot.value().getVertexCount(), cube.getVertexCount());
    CHECK(live.isActive());

    const int sourceVerts = cube.getVertexCount();
    live.removeSetup();
    CHECK(!live.isActive());
    CHECK(live.derivedMesh() == nullptr);
    CHECK(!live.derivedMeshResult().ok());
    CHECK_EQ(cube.getVertexCount(), sourceVerts);
}

TEST_CASE("procgen.meshAdhere.executesThroughModifierGraph") {
    eve::procgen::MeshModifierGraph graph;
    REQUIRE(graph.addNode("src", "mesh.input").ok());
    REQUIRE(graph.addNode("surf", "mesh.input").ok());
    REQUIRE(graph.addNode("adhere", "deform.meshAdhere").ok());
    REQUIRE(graph.addNode("out", "mesh.output").ok());
    REQUIRE(graph.connect("src", "adhere", 0).ok());
    REQUIRE(graph.connect("surf", "adhere", 1).ok());
    REQUIRE(graph.connect("adhere", "out", 0).ok());
    REQUIRE(graph.setNodeMesh("src", unitCube(0.f, 0.4f, 0.f, 0.25f)).ok());
    REQUIRE(graph.setNodeMesh("surf", flatPlaneY0()).ok());
    REQUIRE(graph.setNodeFloat("adhere", "edgeRadius", 1.f).ok());
    REQUIRE(graph.setNodeFloat("adhere", "materialRadius", 1.f).ok());
    REQUIRE(graph.setNodeFloat("adhere", "strength", 1.f).ok());
    auto result = graph.executeResult("out");
    REQUIRE(result.ok());
    CHECK_EQ(result.value().getMeta("deformer", ""), std::string("deform.meshAdhere"));
}

TEST_CASE("procgen.meshContactBlend.rejectsMismatchedSourceIds") {
    const auto                           mesh = unitCube(0.f, 0.f, 0.f);
    std::vector<std::int32_t>            ids(4, 0);
    eve::procgen::MeshContactBlendParams params;
    params.edgeRadius = 1.f;
    CHECK(!eve::procgen::meshContactBlendResult(mesh, ids, params).ok());
}
}  // namespace test_procgen_mesh_boolean
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_procgen_mesh_modifier_graph)
#include "PrivateTestScope.h"
#include <cmath>
#include "procgen/mesh/MeshModifierGraph.h"
#include "zeroerr/unittest.h"
// procgen_mesh_modifier_graph.cpp
namespace test_procgen_mesh_modifier_graph {
using namespace eve;
using namespace eve::procgen;

namespace {

MeshBuild triangle(float offsetX = 0.f) {
    MeshBuild mesh;
    mesh.addVertex(offsetX + 0.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f);
    mesh.addVertex(offsetX + 1.f, 0.f, 0.f, 0.f, 0.f, 1.f, 1.f, 0.f);
    mesh.addVertex(offsetX + 0.f, 1.f, 0.f, 0.f, 0.f, 1.f, 0.f, 1.f);
    mesh.addTriangle(0, 1, 2);
    return mesh;
}

MeshBuild tetrahedron(float tipOffset = 0.f) {
    MeshBuild mesh;
    mesh.addVertex(0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f);
    mesh.addVertex(1.f, 0.f, 0.f, 0.f, 1.f, 0.f, 1.f, 0.f);
    mesh.addVertex(0.f, 1.f, 0.f, 0.f, 1.f, 0.f, 0.f, 1.f);
    mesh.addVertex(tipOffset, 0.f, 1.f, 0.f, 1.f, 0.f, 1.f, 1.f);
    mesh.addTriangle(0, 2, 1);
    mesh.addTriangle(0, 1, 3);
    mesh.addTriangle(1, 2, 3);
    mesh.addTriangle(2, 0, 3);
    return mesh;
}

MeshBuild fitSurface() {
    MeshBuild mesh;
    mesh.addVertex(-2.f, -2.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f);
    mesh.addVertex(2.f, -2.f, 0.f, 0.f, 0.f, 1.f, 1.f, 0.f);
    mesh.addVertex(2.f, 2.f, 0.f, 0.f, 0.f, 1.f, 1.f, 1.f);
    mesh.addVertex(-2.f, 2.f, 0.f, 0.f, 0.f, 1.f, 0.f, 1.f);
    mesh.addTriangle(0, 1, 2);
    mesh.addTriangle(0, 2, 3);
    return mesh;
}

void requireOk(Result<void> result) { REQUIRE(result.ok()); }

}  // namespace

TEST_CASE("procgen.meshModifierGraph.fusesConsecutiveVertexOperations") {
    MeshModifierGraph graph;
    requireOk(graph.addNode("source", "mesh.input"));
    requireOk(graph.addNode("move", "deform.transform"));
    requireOk(graph.addNode("twist", "deform.twist"));
    requireOk(graph.addNode("out", "mesh.output"));
    requireOk(graph.connect("source", "move"));
    requireOk(graph.connect("move", "twist"));
    requireOk(graph.connect("twist", "out"));
    requireOk(graph.setNodeFloat("move", "x", 2.f));
    requireOk(graph.setNodeFloat("move", "maskRadius", 0.25f));
    requireOk(graph.setNodeFloat("twist", "angle", 0.f));
    requireOk(graph.setNodeMesh("source", triangle()));

    auto result = graph.executeResult("out");
    REQUIRE(result.ok());
    const auto output = std::move(result).takeValue();
    CHECK_EQ(output.getVertexCount(), 3);
    CHECK(std::abs(output.getPositionX(0) - 2.f) < 1e-6f);
    CHECK(std::abs(output.getPositionX(1) - 1.f) < 1e-6f);
    CHECK_EQ(graph.compiledSegmentCount(), 3);
    CHECK_EQ(graph.fusedOperationCount(), 2);

    auto cached = graph.executeResult("out");
    REQUIRE(cached.ok());
    CHECK_EQ(graph.metrics().size(), std::size_t(1));
    CHECK(graph.metrics().front().cacheHit);
}

TEST_CASE("procgen.meshModifierGraph.appendAndWeldAreFusionBarriers") {
    MeshModifierGraph graph;
    for (const auto* node : {"left", "right"}) requireOk(graph.addNode(node, "mesh.input"));
    requireOk(graph.addNode("append", "mesh.append"));
    requireOk(graph.addNode("weld", "mesh.weld"));
    requireOk(graph.addNode("out", "mesh.output"));
    requireOk(graph.connect("left", "append", 0));
    requireOk(graph.connect("right", "append", 1));
    requireOk(graph.connect("append", "weld"));
    requireOk(graph.connect("weld", "out"));
    requireOk(graph.setNodeFloat("weld", "tolerance", 0.001f));
    requireOk(graph.setNodeMesh("left", triangle()));
    requireOk(graph.setNodeMesh("right", triangle()));

    auto result = graph.executeResult("out");
    REQUIRE(result.ok());
    const auto output = std::move(result).takeValue();
    CHECK_EQ(output.getVertexCount(), 3);
    CHECK_EQ(output.getIndexCount(), 6);
    CHECK_EQ(graph.fusedOperationCount(), 0);
}

TEST_CASE("procgen.meshModifierGraph.rejectsCyclesAndInvalidParameters") {
    MeshModifierGraph graph;
    requireOk(graph.addNode("source", "mesh.input"));
    requireOk(graph.addNode("a", "deform.bend"));
    requireOk(graph.addNode("b", "deform.twist"));
    requireOk(graph.connect("b", "a"));
    requireOk(graph.connect("a", "b"));
    auto cycle = graph.executeResult("b");
    REQUIRE(!cycle.ok());
    const auto* diagnostic = cycle.status().primaryDiagnostic();
    REQUIRE(diagnostic != nullptr);
    CHECK(diagnostic->code() == DiagnosticCode::Conflict);

    auto invalid = graph.setNodeString("a", "axis", "diagonal");
    REQUIRE(invalid.ok());
    requireOk(graph.disconnect("a"));
    requireOk(graph.connect("source", "a"));
    requireOk(graph.setNodeMesh("source", triangle()));
    auto badAxis = graph.executeResult("a");
    REQUIRE(!badAxis.ok());
    diagnostic = badAxis.status().primaryDiagnostic();
    REQUIRE(diagnostic != nullptr);
    CHECK(diagnostic->code() == DiagnosticCode::InvalidArgument);
}

TEST_CASE("procgen.meshModifierGraph.supportsFfdMorphAndSpherify") {
    MeshModifierGraph graph;
    requireOk(graph.addNode("source", "mesh.input"));
    requireOk(graph.addNode("ffd", "deform.ffd"));
    requireOk(graph.addNode("sphere", "deform.spherify"));
    requireOk(graph.connect("source", "ffd"));
    requireOk(graph.connect("ffd", "sphere"));
    requireOk(graph.setNodeFloat("ffd", "p001x", 0.5f));
    requireOk(graph.setNodeFloat("sphere", "weight", 0.f));
    requireOk(graph.setNodeMesh("source", tetrahedron()));
    auto deformed = graph.executeResult("sphere");
    REQUIRE(deformed.ok());
    CHECK(std::abs(deformed.value().getPositionX(3) - 0.5f) < 1e-6f);

    MeshModifierGraph morph;
    requireOk(morph.addNode("a", "mesh.input"));
    requireOk(morph.addNode("b", "mesh.input"));
    requireOk(morph.addNode("blend", "deform.morph"));
    requireOk(morph.connect("a", "blend", 0));
    requireOk(morph.connect("b", "blend", 1));
    requireOk(morph.setNodeFloat("blend", "weight", 0.5f));
    requireOk(morph.setNodeMesh("a", tetrahedron()));
    requireOk(morph.setNodeMesh("b", tetrahedron(1.f)));
    auto blended = morph.executeResult("blend");
    REQUIRE(blended.ok());
    CHECK(std::abs(blended.value().getPositionX(3) - 0.5f) < 1e-6f);
}

TEST_CASE("procgen.meshModifierGraph.meshEffectorSupportsOneToFourWeightedNodesAndFusion") {
    MeshModifierGraph graph;
    requireOk(graph.addNode("source", "mesh.input"));
    requireOk(graph.addNode("effector", "deform.effector"));
    requireOk(graph.addNode("move", "deform.transform"));
    requireOk(graph.addNode("out", "mesh.output"));
    requireOk(graph.connect("source", "effector"));
    requireOk(graph.connect("effector", "move"));
    requireOk(graph.connect("move", "out"));
    requireOk(graph.setNodeMesh("source", triangle()));
    requireOk(graph.setNodeInt("effector", "pointCount", 2));
    requireOk(graph.setNodeFloat("effector", "p0radius", 0.4f));
    requireOk(graph.setNodeFloat("effector", "p0dy", 2.f));
    requireOk(graph.setNodeFloat("effector", "p1x", 1.f));
    requireOk(graph.setNodeFloat("effector", "p1radius", 0.4f));
    requireOk(graph.setNodeFloat("effector", "p1dx", 1.f));
    requireOk(graph.setNodeFloat("effector", "p1dy", 0.f));
    auto result = graph.executeResult("out");
    REQUIRE(result.ok());
    CHECK(std::abs(result.value().getPositionY(0) - 2.f) < 1e-6f);
    CHECK(std::abs(result.value().getPositionX(1) - 2.f) < 1e-6f);
    CHECK(std::abs(result.value().getPositionY(2) - 1.f) < 1e-6f);
    CHECK_EQ(graph.fusedOperationCount(), 2);

    MeshModifierGraph invalid;
    requireOk(invalid.addNode("source", "mesh.input"));
    requireOk(invalid.addNode("effector", "deform.effector"));
    requireOk(invalid.connect("source", "effector"));
    requireOk(invalid.setNodeMesh("source", triangle()));
    requireOk(invalid.setNodeInt("effector", "pointCount", 5));
    CHECK(!invalid.executeResult("effector").ok());
}

TEST_CASE("procgen.meshModifierGraph.meshFitProjectsOntoSecondInputWithoutPhysicsDependency") {
    MeshBuild source = triangle();
    for (std::size_t index = 2; index < source.positions().size(); index += 3u) source.positions()[index] = 1.f;
    MeshModifierGraph graph;
    requireOk(graph.addNode("source", "mesh.input"));
    requireOk(graph.addNode("surface", "mesh.input"));
    requireOk(graph.addNode("fit", "deform.meshFit"));
    requireOk(graph.connect("source", "fit", 0));
    requireOk(graph.connect("surface", "fit", 1));
    requireOk(graph.setNodeMesh("source", source));
    requireOk(graph.setNodeMesh("surface", fitSurface()));
    requireOk(graph.setNodeFloat("fit", "directionY", 0.f));
    requireOk(graph.setNodeFloat("fit", "directionZ", -1.f));
    requireOk(graph.setNodeFloat("fit", "surfaceOffset", 0.2f));
    auto fitted = graph.executeResult("fit");
    REQUIRE(fitted.ok());
    for (int vertex = 0; vertex < fitted.value().getVertexCount(); ++vertex)
        CHECK(std::abs(fitted.value().getPositionZ(vertex) - 0.2f) < 1e-6f);
    CHECK_EQ(fitted.value().getMeta("deformer", ""), "deform.meshFit");

    const auto revision = graph.revision();
    requireOk(graph.setNodeFloat("fit", "directionX", 0.f));
    requireOk(graph.setNodeFloat("fit", "directionZ", 0.f));
    CHECK(!graph.executeResult("fit").ok());
    CHECK(graph.revision() > revision);
}

TEST_CASE("procgen.meshModifierGraph.supportsSubdivisionAndPlaneCut") {
    MeshModifierGraph graph;
    requireOk(graph.addNode("source", "mesh.input"));
    requireOk(graph.addNode("subdivide", "mesh.subdivide"));
    requireOk(graph.addNode("cut", "mesh.cutPlane"));
    requireOk(graph.connect("source", "subdivide"));
    requireOk(graph.connect("subdivide", "cut"));
    requireOk(graph.setNodeInt("subdivide", "levels", 1));
    requireOk(graph.setNodeFloat("cut", "normalX", 1.f));
    requireOk(graph.setNodeFloat("cut", "normalY", 0.f));
    requireOk(graph.setNodeFloat("cut", "distance", 0.25f));
    requireOk(graph.setNodeInt("cut", "cap", 1));
    requireOk(graph.setNodeMesh("source", tetrahedron()));
    auto result = graph.executeResult("cut");
    REQUIRE(result.ok());
    CHECK(result.value().getIndexCount() > 0);
    CHECK_EQ(result.value().getGroupName(result.value().getGroupCount() - 1), "__cut_cap");
    for (int i = 0; i < result.value().getVertexCount(); ++i) CHECK(result.value().getPositionX(i) >= 0.25f - 1e-6f);

    MeshModifierGraph subdivision;
    requireOk(subdivision.addNode("source", "mesh.input"));
    requireOk(subdivision.addNode("subdivide", "mesh.subdivide"));
    requireOk(subdivision.connect("source", "subdivide"));
    requireOk(subdivision.setNodeMesh("source", triangle()));
    auto subdivided = subdivision.executeResult("subdivide");
    REQUIRE(subdivided.ok());
    CHECK_EQ(subdivided.value().getIndexCount(), 12);
}

TEST_CASE("procgen.meshModifierGraph.deformsAlongCubicSplineOffsets") {
    MeshModifierGraph graph;
    requireOk(graph.addNode("source", "mesh.input"));
    requireOk(graph.addNode("spline", "deform.spline"));
    requireOk(graph.connect("source", "spline"));
    requireOk(graph.setNodeFloat("spline", "c3x", 2.f));
    requireOk(graph.setNodeMesh("source", triangle()));
    auto result = graph.executeResult("spline");
    REQUIRE(result.ok());
    CHECK(std::abs(result.value().getPositionX(0)) < 1e-6f);
    CHECK(std::abs(result.value().getPositionX(2) - 2.f) < 1e-6f);
}

TEST_CASE("procgen.meshModifierGraph.deformsAlongOwningSplinePath") {
    SplinePath path;
    requireOk(path.setKindResult("linear"));
    requireOk(path.addPointResult({0.f, 0.f, 0.f}));
    requireOk(path.addPointResult({0.f, 1.f, 0.f}));
    requireOk(path.addPointResult({2.f, 2.f, 0.f}));

    MeshModifierGraph graph;
    requireOk(graph.addNode("source", "mesh.input"));
    requireOk(graph.addNode("path", "deform.splinePath"));
    requireOk(graph.connect("source", "path"));
    requireOk(graph.setNodeMesh("source", triangle()));
    requireOk(graph.setNodeSplinePath("path", path));

    auto result = graph.executeResult("path");
    REQUIRE(result.ok());
    CHECK(std::abs(result.value().getPositionX(0)) < 1e-6f);
    CHECK(std::abs(result.value().getPositionY(0)) < 1e-6f);
    CHECK(std::abs(result.value().getPositionX(2) - 2.f) < 1e-5f);
    CHECK(std::abs(result.value().getPositionY(2) - 2.f) < 1e-5f);
}

TEST_CASE("procgen.meshModifierGraph.requiresReadySplinePathBinding") {
    MeshModifierGraph graph;
    requireOk(graph.addNode("source", "mesh.input"));
    requireOk(graph.addNode("path", "deform.splinePath"));
    requireOk(graph.connect("source", "path"));
    requireOk(graph.setNodeMesh("source", triangle()));

    auto missing = graph.executeResult("path");
    REQUIRE(!missing.ok());
    REQUIRE(missing.status().primaryDiagnostic() != nullptr);
    CHECK(missing.status().primaryDiagnostic()->code() == DiagnosticCode::PreconditionViolation);

    SplinePath incomplete;
    requireOk(incomplete.addPointResult({0.f, 0.f, 0.f}));
    auto rejected = graph.setNodeSplinePath("path", incomplete);
    REQUIRE(!rejected.ok());
    REQUIRE(rejected.status().primaryDiagnostic() != nullptr);
    CHECK(rejected.status().primaryDiagnostic()->code() == DiagnosticCode::PreconditionViolation);
}

TEST_CASE("procgen.meshModifierGraph.splineFrameStaysContinuousNearVerticalTangents") {
    SplinePath path;
    requireOk(path.setKindResult("bezier"));
    requireOk(path.addPointResult({0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 1.2f, 1.f, 0.f}));
    requireOk(path.addPointResult({0.f, 2.f, 0.f, -0.2f, -1.f, 0.f, 0.f, 0.f, 0.f}));

    MeshBuild strip;
    for (float y : {0.f, 0.49f, 0.51f, 1.f}) strip.addVertex(1.f, y, 0.f, 0.f, 0.f, 1.f, 0.f, y);
    strip.addTriangle(0, 1, 2);
    strip.addTriangle(1, 3, 2);

    MeshModifierGraph graph;
    requireOk(graph.addNode("source", "mesh.input"));
    requireOk(graph.addNode("path", "deform.splinePath"));
    requireOk(graph.connect("source", "path"));
    requireOk(graph.setNodeMesh("source", strip));
    requireOk(graph.setNodeSplinePath("path", path));
    auto result = graph.executeResult("path");
    REQUIRE(result.ok());
    const float dx = result.value().getPositionX(2) - result.value().getPositionX(1);
    const float dy = result.value().getPositionY(2) - result.value().getPositionY(1);
    const float dz = result.value().getPositionZ(2) - result.value().getPositionZ(1);
    CHECK(std::sqrt(dx * dx + dy * dy + dz * dz) < 0.2f);
}

TEST_CASE("procgen.meshModifierGraph.generatesCappedSplineTubeWithoutMeshInput") {
    SplinePath path;
    requireOk(path.setKindResult("linear"));
    requireOk(path.addPointResult({0.f, 0.f, 0.f}));
    requireOk(path.addPointResult({0.f, 2.f, 0.f}));

    MeshModifierGraph graph;
    requireOk(graph.addNode("tube", "mesh.splineTube"));
    requireOk(graph.setNodeSplinePath("tube", path));
    requireOk(graph.setNodeFloat("tube", "radius", 0.5f));
    requireOk(graph.setNodeInt("tube", "pathSegments", 4));
    requireOk(graph.setNodeInt("tube", "radialSegments", 8));
    requireOk(graph.setNodeInt("tube", "cap", 1));

    auto result = graph.executeResult("tube");
    REQUIRE(result.ok());
    const auto& mesh = result.value();
    CHECK_EQ(mesh.getVertexCount(), 65);
    CHECK_EQ(mesh.getIndexCount(), 240);
    CHECK_EQ(mesh.getGroupCount(), 2);
    CHECK_EQ(mesh.getGroupName(0), "tube");
    CHECK_EQ(mesh.getGroupName(1), "caps");
    CHECK_EQ(mesh.getMeta("generator", ""), "mesh.splineTube");
    for (int vertex = 45; vertex < 55; ++vertex) CHECK(std::abs(mesh.getNormalY(vertex) + 1.f) < 1e-6f);
    CHECK(std::abs(mesh.getPositionY(0)) < 1e-6f);
    CHECK(
        std::abs(std::sqrt(mesh.getPositionX(0) * mesh.getPositionX(0) + mesh.getPositionZ(0) * mesh.getPositionZ(0)) -
                 0.5f) < 1e-6f);
}

TEST_CASE("procgen.meshModifierGraph.closesSplineTubeSeamAndOmitsCaps") {
    SplinePath path;
    requireOk(path.setKindResult("catmullRom"));
    path.setClosed(true);
    requireOk(path.addPointResult({-1.f, 0.f, -1.f}));
    requireOk(path.addPointResult({1.f, 0.f, -1.f}));
    requireOk(path.addPointResult({1.f, 0.f, 1.f}));
    requireOk(path.addPointResult({-1.f, 0.f, 1.f}));

    MeshModifierGraph graph;
    requireOk(graph.addNode("tube", "mesh.splineTube"));
    requireOk(graph.setNodeSplinePath("tube", path));
    requireOk(graph.setNodeInt("tube", "pathSegments", 8));
    requireOk(graph.setNodeInt("tube", "radialSegments", 6));
    auto result = graph.executeResult("tube");
    REQUIRE(result.ok());
    const auto& mesh = result.value();
    CHECK_EQ(mesh.getVertexCount(), 63);
    CHECK_EQ(mesh.getIndexCount(), 288);
    CHECK_EQ(mesh.getGroupCount(), 1);
    for (int radial = 0; radial <= 6; ++radial) {
        const int seam = 8 * 7 + radial;
        CHECK(std::abs(mesh.getPositionX(radial) - mesh.getPositionX(seam)) < 1e-6f);
        CHECK(std::abs(mesh.getPositionY(radial) - mesh.getPositionY(seam)) < 1e-6f);
        CHECK(std::abs(mesh.getPositionZ(radial) - mesh.getPositionZ(seam)) < 1e-6f);
        CHECK(std::abs(mesh.getNormalX(radial) - mesh.getNormalX(seam)) < 1e-6f);
        CHECK(std::abs(mesh.getUvU(seam) - 1.f) < 1e-6f);
    }
}

TEST_CASE("procgen.meshModifierGraph.rejectsInvalidSplineTubeBudgets") {
    MeshModifierGraph graph;
    requireOk(graph.addNode("tube", "mesh.splineTube"));
    auto missing = graph.executeResult("tube");
    REQUIRE(!missing.ok());
    CHECK(missing.status().primaryDiagnostic()->code() == DiagnosticCode::PreconditionViolation);

    SplinePath path;
    requireOk(path.addPointResult({0.f, 0.f, 0.f}));
    requireOk(path.addPointResult({0.f, 1.f, 0.f}));
    requireOk(graph.setNodeSplinePath("tube", path));
    requireOk(graph.setNodeInt("tube", "radialSegments", 2));
    auto invalid = graph.executeResult("tube");
    REQUIRE(!invalid.ok());
    CHECK(invalid.status().primaryDiagnostic()->code() == DiagnosticCode::InvalidArgument);
}

TEST_CASE("procgen.meshModifierGraph.generatesSplineRibbonAndSolidRoadGroups") {
    SplinePath path;
    requireOk(path.setKindResult("linear"));
    requireOk(path.addPointResult({0.f, 0.f, 0.f}));
    requireOk(path.addPointResult({0.f, 0.f, 4.f}));

    MeshModifierGraph ribbon;
    requireOk(ribbon.addNode("road", "mesh.splineRibbon"));
    requireOk(ribbon.setNodeSplinePath("road", path));
    requireOk(ribbon.setNodeFloat("road", "width", 2.f));
    requireOk(ribbon.setNodeInt("road", "pathSegments", 4));
    auto flat = ribbon.executeResult("road");
    REQUIRE(flat.ok());
    CHECK_EQ(flat.value().getVertexCount(), 10);
    CHECK_EQ(flat.value().getIndexCount(), 24);
    CHECK_EQ(flat.value().getGroupCount(), 1);
    CHECK_EQ(flat.value().getGroupName(0), "surface");
    CHECK(std::abs(flat.value().getPositionX(0) + 1.f) < 1e-6f);
    CHECK(std::abs(flat.value().getPositionX(1) - 1.f) < 1e-6f);

    MeshModifierGraph solid;
    requireOk(solid.addNode("road", "mesh.splineRibbon"));
    requireOk(solid.setNodeSplinePath("road", path));
    requireOk(solid.setNodeFloat("road", "width", 2.f));
    requireOk(solid.setNodeFloat("road", "thickness", 0.25f));
    requireOk(solid.setNodeInt("road", "pathSegments", 4));
    auto road = solid.executeResult("road");
    REQUIRE(road.ok());
    CHECK_EQ(road.value().getVertexCount(), 48);
    CHECK_EQ(road.value().getIndexCount(), 108);
    CHECK_EQ(road.value().getGroupCount(), 4);
    CHECK_EQ(road.value().getGroupName(0), "surface");
    CHECK_EQ(road.value().getGroupName(1), "bottom");
    CHECK_EQ(road.value().getGroupName(2), "sides");
    CHECK_EQ(road.value().getGroupName(3), "caps");
    CHECK_EQ(road.value().getMeta("generator", ""), "mesh.splineRibbon");
}

TEST_CASE("procgen.meshModifierGraph.splineGeneratorsConsumePointProfile") {
    SplinePath path;
    requireOk(path.setKindResult("linear"));
    requireOk(path.addPointResult({0.f, 0.f, 0.f}));
    requireOk(path.addPointResult({0.f, 0.f, 2.f}));
    requireOk(path.setPointProfileResult(1, 90.f, 2.f, 0.5f));
    MeshModifierGraph graph;
    requireOk(graph.addNode("ribbon", "mesh.splineRibbon"));
    requireOk(graph.setNodeSplinePath("ribbon", path));
    requireOk(graph.setNodeInt("ribbon", "pathSegments", 2));
    auto result = graph.executeResult("ribbon");
    REQUIRE(result.ok());
    const int   lastLeft  = 4;
    const int   lastRight = 5;
    const float dx        = result.value().getPositionX(lastRight) - result.value().getPositionX(lastLeft);
    const float dy        = result.value().getPositionY(lastRight) - result.value().getPositionY(lastLeft);
    const float dz        = result.value().getPositionZ(lastRight) - result.value().getPositionZ(lastLeft);
    CHECK(std::abs(std::sqrt(dx * dx + dy * dy + dz * dz) - 4.f) < 1e-5f);
    CHECK(std::abs(dy) > 3.9f);
}

TEST_CASE("procgen.meshModifierGraph.extrudesOwningClosedProfileWithIndependentCaps") {
    SplinePath path;
    requireOk(path.setKindResult("linear"));
    requireOk(path.addPointResult({0.f, 0.f, 0.f}));
    requireOk(path.addPointResult({0.f, 0.f, 2.f}));
    requireOk(path.setPointProfileResult(1, 45.f, 2.f, 0.5f));
    SplineProfile square{{{-0.5f, -0.5f}, {0.5f, -0.5f}, {0.5f, 0.5f}, {-0.5f, 0.5f}}, true};

    MeshModifierGraph graph;
    requireOk(graph.addNode("extrude", "mesh.splineExtrude"));
    requireOk(graph.setNodeSplinePath("extrude", path));
    requireOk(graph.setNodeSplineProfile("extrude", square));
    requireOk(graph.setNodeInt("extrude", "pathSegments", 2));
    auto result = graph.executeResult("extrude");
    REQUIRE(result.ok());
    CHECK_EQ(result.value().getVertexCount(), 23);
    CHECK_EQ(result.value().getIndexCount(), 60);
    CHECK_EQ(result.value().getGroupCount(), 2);
    CHECK_EQ(result.value().getGroupName(0), "profile");
    CHECK_EQ(result.value().getGroupName(1), "caps");
    CHECK_EQ(result.value().getMeta("generator", ""), "mesh.splineExtrude");
    for (int vertex = 15; vertex < 19; ++vertex) CHECK(result.value().getNormalZ(vertex) < -0.999f);
    for (int vertex = 19; vertex < 23; ++vertex) CHECK(result.value().getNormalZ(vertex) > 0.999f);
}

TEST_CASE("procgen.meshModifierGraph.extrudesOpenUProfileWithoutImplicitCaps") {
    SplinePath path;
    requireOk(path.addPointResult({0.f, 0.f, 0.f}));
    requireOk(path.addPointResult({0.f, 1.f, 0.f}));
    SplineProfile     channel{{{-1.f, 1.f}, {-1.f, 0.f}, {1.f, 0.f}, {1.f, 1.f}}, false};
    MeshModifierGraph graph;
    requireOk(graph.addNode("channel", "mesh.splineExtrude"));
    requireOk(graph.setNodeSplinePath("channel", path));
    requireOk(graph.setNodeSplineProfile("channel", channel));
    requireOk(graph.setNodeInt("channel", "pathSegments", 1));
    auto result = graph.executeResult("channel");
    REQUIRE(result.ok());
    CHECK_EQ(result.value().getVertexCount(), 8);
    CHECK_EQ(result.value().getIndexCount(), 18);
    CHECK_EQ(result.value().getGroupCount(), 1);
    CHECK_EQ(result.value().getMeta("profileClosed", ""), "0");
}

TEST_CASE("procgen.meshModifierGraph.rejectsInvalidProfileAtomically") {
    MeshModifierGraph graph;
    requireOk(graph.addNode("extrude", "mesh.splineExtrude"));
    const auto    revision = graph.revision();
    SplineProfile invalid{{{0.f, 0.f}, {1.f, 0.f}, {1.f, 0.f}}, true};
    auto          rejected = graph.setNodeSplineProfile("extrude", invalid);
    REQUIRE(!rejected.ok());
    CHECK_EQ(graph.revision(), revision);
    CHECK(rejected.status().primaryDiagnostic()->code() == DiagnosticCode::InvalidArgument);
}

TEST_CASE("procgen.meshModifierGraph.generatesDisconnectedSplineChunksWithoutBridgeTriangles") {
    SplinePath path;
    requireOk(path.setKindResult("linear"));
    requireOk(path.addPointResult({0.f, 0.f, 0.f}));
    requireOk(path.addPointResult({0.f, 1.f, 0.f}));
    requireOk(path.addPointResult({8.f, 0.f, 0.f}));
    requireOk(path.addPointResult({8.f, 1.f, 0.f}));
    requireOk(path.setPointChunkBreakResult(2, true));
    MeshModifierGraph graph;
    requireOk(graph.addNode("tube", "mesh.splineTube"));
    requireOk(graph.setNodeSplinePath("tube", path));
    requireOk(graph.setNodeInt("tube", "pathSegments", 2));
    requireOk(graph.setNodeInt("tube", "radialSegments", 4));
    auto result = graph.executeResult("tube");
    REQUIRE(result.ok());
    CHECK_EQ(result.value().getVertexCount(), 54);
    CHECK_EQ(result.value().getIndexCount(), 144);
    CHECK_EQ(result.value().getGroupCount(), 2);
    CHECK_EQ(result.value().getMeta("chunks", ""), "2");
    for (int index = 0; index < result.value().getIndexCount(); index += 3) {
        const bool firstChunk = result.value().getIndex(index) < 27;
        CHECK_EQ(result.value().getIndex(index + 1) < 27, firstChunk);
        CHECK_EQ(result.value().getIndex(index + 2) < 27, firstChunk);
    }
}

TEST_CASE("procgen.meshModifierGraph.noiseKeepsHardEdgeSeamsClosed") {
    MeshBuild seam;
    seam.addVertex(0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f);
    seam.addVertex(1.f, 0.f, 0.f, 1.f, 0.f, 0.f, 1.f, 0.f);
    seam.addVertex(0.f, 1.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f);
    seam.addVertex(0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f);
    seam.addVertex(0.f, 1.f, 0.f, 0.f, 1.f, 0.f, 1.f, 0.f);
    seam.addVertex(0.f, 0.f, 1.f, 0.f, 1.f, 0.f, 0.f, 1.f);
    seam.addTriangle(0, 1, 2);
    seam.addTriangle(3, 4, 5);
    MeshModifierGraph graph;
    requireOk(graph.addNode("source", "mesh.input"));
    requireOk(graph.addNode("noise", "deform.noise"));
    requireOk(graph.connect("source", "noise"));
    requireOk(graph.setNodeFloat("noise", "amplitude", 0.4f));
    requireOk(graph.setNodeMesh("source", seam));
    auto result = graph.executeResult("noise");
    REQUIRE(result.ok());
    CHECK(std::abs(result.value().getPositionX(0) - result.value().getPositionX(3)) < 1e-6f);
    CHECK(std::abs(result.value().getPositionY(0) - result.value().getPositionY(3)) < 1e-6f);
    CHECK(std::abs(result.value().getPositionZ(0) - result.value().getPositionZ(3)) < 1e-6f);
}

TEST_CASE("procgen.meshModifierGraph.soundReactConsumesAnalyzedLevelAndFuses") {
    MeshModifierGraph graph;
    requireOk(graph.addNode("source", "mesh.input"));
    requireOk(graph.addNode("sound", "deform.soundReact"));
    requireOk(graph.addNode("move", "deform.transform"));
    requireOk(graph.connect("source", "sound"));
    requireOk(graph.connect("sound", "move"));
    requireOk(graph.setNodeMesh("source", triangle()));
    requireOk(graph.setNodeFloat("sound", "level", 0.75f));
    requireOk(graph.setNodeFloat("sound", "threshold", 0.25f));
    requireOk(graph.setNodeFloat("sound", "strength", 2.f));
    requireOk(graph.setNodeFloat("sound", "frequency", 0.f));
    requireOk(graph.setNodeFloat("sound", "phase", 1.5707963268f));
    requireOk(graph.setNodeString("sound", "axis", "y"));
    auto result = graph.executeResult("move");
    REQUIRE(result.ok());
    CHECK(std::abs(result.value().getPositionX(0)) < 1e-5f);
    CHECK(std::abs(result.value().getPositionX(1) - 2.f) < 1e-5f);
    CHECK_EQ(graph.fusedOperationCount(), 2);

    requireOk(graph.setNodeFloat("sound", "level", 1.1f));
    auto invalid = graph.executeResult("sound");
    REQUIRE(!invalid.ok());
    CHECK(invalid.status().primaryDiagnostic()->code() == DiagnosticCode::InvalidArgument);
}
}  // namespace test_procgen_mesh_modifier_graph
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_procgen_mesh_uv_projection)
#include "PrivateTestScope.h"
#include <cmath>
#include "procgen/mesh/MeshModifierGraph.h"
#include "procgen/mesh/MeshUvProjection.h"
#include "zeroerr/unittest.h"
// procgen_mesh_uv_projection.cpp
namespace test_procgen_mesh_uv_projection {
using namespace eve::procgen;

namespace {
MeshBuild triangle() {
    MeshBuild mesh;
    mesh.addVertex(0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f);
    mesh.addVertex(1.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f);
    mesh.addVertex(0.f, 0.f, 1.f, 0.f, 1.f, 0.f, 0.f, 0.f);
    mesh.addTriangle(0, 1, 2);
    return mesh;
}
}  // namespace

TEST_CASE("procgen.meshUvProjection.projectsAndMapsDynamicSurface") {
    auto projected = projectMeshUvResult(triangle(), "planar", 1.f, 0.f, 0.f);
    REQUIRE(projected.ok());
    auto uv = mapMeshSurfacePointToUvResult(projected.value(), 0, 0.25f, 0.f, 0.25f);
    REQUIRE(uv.ok());
    CHECK(std::abs(uv.value().u - 0.25f) < 1e-6f);
    CHECK(std::abs(uv.value().v - 0.25f) < 1e-6f);
    CHECK_EQ(projected.value().getMeta("uvProjection", ""), "planar");
}

TEST_CASE("procgen.meshUvProjection.executesThroughModifierGraph") {
    MeshModifierGraph graph;
    REQUIRE(graph.addNode("source", "mesh.input").ok());
    REQUIRE(graph.addNode("uv", "mesh.projectUv").ok());
    REQUIRE(graph.addNode("output", "mesh.output").ok());
    REQUIRE(graph.connect("source", "uv", 0).ok());
    REQUIRE(graph.connect("uv", "output", 0).ok());
    REQUIRE(graph.setNodeMesh("source", triangle()).ok());
    REQUIRE(graph.setNodeString("uv", "mode", "box").ok());
    auto built = graph.executeResult("output");
    REQUIRE(built.ok());
    CHECK_EQ(built.value().getMeta("uvProjection", ""), "box");
}

TEST_CASE("procgen.meshUvProjection.rejectsInvalidMeshesAndTriangles") {
    MeshBuild empty;
    CHECK(!projectMeshUvResult(empty, "planar").ok());
    CHECK(!projectMeshUvResult(triangle(), "invalid").ok());
    CHECK(!mapMeshSurfacePointToUvResult(triangle(), 3, 0.f, 0.f, 0.f).ok());
}
}  // namespace test_procgen_mesh_uv_projection
#endif
