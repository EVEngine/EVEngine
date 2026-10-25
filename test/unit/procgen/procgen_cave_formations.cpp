#include "zeroerr/unittest.h"

#include "CaveFixtures.h"
#include "procgen/MeshBuild.h"
#include "procgen/algorithms/MarchingCubes.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

Params caveParams() {
    Params params;
    params.setSeed(20260830);
    params.setString("style", "mixed");
    params.setInt("resolution", 24);
    params.setInt("chambers", 4);
    params.setInt("branches", 2);
    return params;
}

MeshBuild generate(const Params& params) {
    MeshRecipeRegistry::instance().registerBuiltins();
    MeshBuild   mesh;
    std::string error;
    if (!MeshRecipeRegistry::instance().generate("mesh.cave", params, mesh, error))
        throw std::runtime_error("mesh.cave generation failed: " + error);
    return mesh;
}

std::vector<float> sphereField(int side, float radius) {
    std::vector<float> field(std::size_t(side * side * side));
    for (int z = 0; z < side; ++z)
        for (int y = 0; y < side; ++y)
            for (int x = 0; x < side; ++x) {
                const float px                                = float(x) / float(side - 1) * 2.f - 1.f;
                const float py                                = float(y) / float(side - 1) * 2.f - 1.f;
                const float pz                                = float(z) / float(side - 1) * 2.f - 1.f;
                field[std::size_t(x + side * (y + side * z))] = std::sqrt(px * px + py * py + pz * pz) - radius;
            }
    return field;
}

}  // namespace

TEST_CASE("Cave recipe: seeded styles produce valid and repeatable meshes with observable configuration") {
    SUB_CASE("every documented style produces a deterministic indexed surface") {
        for (const char* style : {"cavern", "tunnels", "vertical", "labyrinth", "mixed"}) {
            auto params = caveParams();
            params.setString("style", style);
            const auto first    = generate(params);
            const auto repeated = generate(params);
            REQUIRE(first.getVertexCount() > 100);
            CHECK_EQ(first.getIndexCount() % 3, 0);
            CHECK(first.positions() == repeated.positions());
            CHECK(first.indices() == repeated.indices());
            CHECK_EQ(first.getMeta("style", ""), std::string(style));
            CHECK_EQ(first.getMeta("determinism", ""), std::string("bit-exact-cpu"));
        }
    };
    SUB_CASE("world dimensions bound vertices; branch count changes topology") {
        auto params = caveParams();
        params.setFloat("width", 42.f);
        params.setFloat("height", 14.f);
        params.setFloat("depth", 26.f);
        params.setInt("branches", 1);
        const auto sparse = generate(params);
        params.setInt("branches", 8);
        const auto branched = generate(params);
        CHECK(sparse.positions() != branched.positions());
        CHECK_EQ(branched.getMeta("branches", ""), std::string("8"));
        for (int vertex = 0; vertex < branched.getVertexCount(); ++vertex) {
            CHECK(std::fabs(branched.getPositionX(vertex)) <= 21.01f);
            CHECK(std::fabs(branched.getPositionY(vertex)) <= 7.01f);
            CHECK(std::fabs(branched.getPositionZ(vertex)) <= 13.01f);
            const float nx = branched.getNormalX(vertex);
            const float ny = branched.getNormalY(vertex);
            const float nz = branched.getNormalZ(vertex);
            CHECK(std::fabs(std::sqrt(nx * nx + ny * ny + nz * nz) - 1.f) < 1e-3f);
        }
    };
}

TEST_CASE("Cave recipe: invalid enums and dimensions and normalized controls reject before mesh publication") {
    MeshRecipeRegistry::instance().registerBuiltins();
    const auto rejected = [&](Params params) {
        MeshBuild   mesh;
        std::string error;
        REQUIRE(!MeshRecipeRegistry::instance().generate("mesh.cave", params, mesh, error));
        CHECK(!error.empty());
        CHECK_EQ(mesh.getVertexCount(), 0);
    };
    SUB_CASE("enum and grid parameters") {
        for (const char* style : {"lava-tube-ish", "", "Cavern"}) {
            auto params = caveParams();
            params.setString("style", style);
            rejected(params);
        }
        auto params = caveParams();
        params.setString("genesis", "volcanic");
        rejected(params);
        params = caveParams();
        params.setInt("resolution", 4);
        rejected(params);
        params = caveParams();
        params.setInt("surfaceRefinement", 3);
        rejected(params);
        params = caveParams();
        params.setInt("isosurfaceSampling", 3);
        rejected(params);
        params = caveParams();
        params.setInt("wetnessRefinement", 2);
        rejected(params);
        params = caveParams();
        params.setString("surfaceNormalMode", "invented");
        rejected(params);
    };
    SUB_CASE("strength parameters above one") {
        for (const char* name :
             {"boundaryClosure", "permeabilityContrast", "bendUndercut", "scallopMaturity", "fragmentDetachment",
              "curvatureDissolution", "reactiveSurfaceCoupling", "biogenicCorrosion", "condensationCorrosion"}) {
            auto params = caveParams();
            params.setFloat(name, 1.1f);
            rejected(params);
        }
    };
}

TEST_CASE("Cave corrosion: dry biofilms retreat walls while flowing films protect old fluvial marks") {
    CaveBiogenicCorrosionInput dry;
    dry.along             = .37f;
    dry.angle             = 1.2f;
    dry.distance          = .17f;
    dry.radius            = .16f;
    dry.hydraulicExposure = .45f;
    dry.strength          = .9f;
    dry.seed              = 20260830u;
    const auto first      = sampleCaveBiogenicCorrosion(dry);
    REQUIRE(first.erosion > 0.f);
    CHECK(first.fluvialScallopRetention < .35f);
    CHECK_EQ(sampleCaveBiogenicCorrosion(dry).erosion, first.erosion);
    auto wet                    = dry;
    wet.hydraulicExposure       = 1.45f;
    const auto protectedSurface = sampleCaveBiogenicCorrosion(wet);
    CHECK(protectedSurface.erosion < first.erosion);
    CHECK(protectedSurface.fluvialScallopRetention > first.fluvialScallopRetention);
    auto disabled     = dry;
    disabled.strength = 0.f;
    CHECK_EQ(sampleCaveBiogenicCorrosion(disabled).erosion, 0.f);
    CHECK_EQ(sampleCaveBiogenicCorrosion(disabled).fluvialScallopRetention, 1.f);
}

TEST_CASE("Cave condensation: zero strength preserves field while enabled retreat is seeded and ceiling-biased") {
    constexpr int      side     = 17;
    const auto         original = sphereField(side, .62f);
    std::vector<float> exposure(original.size(), .35f);
    auto               disabled = original;
    const auto         noChange = erodeCaveByCondensation(disabled, exposure, side, side, side, 0.f, 73u);
    REQUIRE(disabled == original);
    CHECK_EQ(noChange.affectedVoxels, 0);
    auto       first = original, repeat = original;
    const auto result = erodeCaveByCondensation(first, exposure, side, side, side, 1.f, 73u);
    const auto replay = erodeCaveByCondensation(repeat, exposure, side, side, side, 1.f, 73u);
    CHECK(first == repeat);
    CHECK_EQ(result.affectedVoxels, replay.affectedVoxels);
    CHECK(result.affectedVoxels > 0);
    CHECK(result.maximumRetreat > 0.f);
    CHECK(result.maximumRetreat <= .032001f);
    int above = 0, below = 0;
    for (int z = 1; z < side - 1; ++z)
        for (int y = 1; y < side - 1; ++y)
            for (int x = 1; x < side - 1; ++x) {
                const auto index = std::size_t(x + side * (y + side * z));
                if (first[index] != original[index]) {
                    above += y > side / 2;
                    below += y < side / 2;
                }
            }
    CHECK(above > below * 4);
}

TEST_CASE("Cave recipe: optional corrosion is deterministic and visible in metadata and geometry") {
    SUB_CASE("biogenic corrosion") {
        auto params = caveParams();
        params.setFloat("erosion", 0.f);
        params.setFloat("scallopErosion", 0.f);
        const auto baseline = generate(params);
        params.setFloat("biogenicCorrosion", .9f);
        const auto changed = generate(params);
        const auto replay  = generate(params);
        REQUIRE(changed.positions() == replay.positions());
        CHECK(changed.positions() != baseline.positions());
        CHECK_EQ(changed.getMeta("biogenicCorrosionModel", ""), std::string("ammonia-nitrification-aero-speleogen-v1"));
        CHECK(std::stoi(changed.getMeta("biogenicAffectedVoxels", "0")) > 0);
    };
    SUB_CASE("condensation corrosion") {
        auto       params   = caveParams();
        const auto baseline = generate(params);
        params.setFloat("condensationCorrosion", .9f);
        const auto changed = generate(params);
        CHECK(changed.positions() != baseline.positions());
        CHECK(changed.positions() == generate(params).positions());
        CHECK_EQ(changed.getMeta("condensationCorrosionModel", ""), std::string("cool-wall-co2-film-pitting-v1"));
        CHECK(std::stoi(changed.getMeta("condensationAffectedVoxels", "0")) > 0);
    };
}

TEST_CASE("Cave field sampling: trilinear interpolation preserves source samples and tracks surface normals") {
    constexpr int      side = 7;
    std::vector<float> linear(std::size_t(side * side * side));
    for (int z = 0; z < side; ++z)
        for (int y = 0; y < side; ++y)
            for (int x = 0; x < side; ++x)
                linear[std::size_t(x + side * (y + side * z))] = float(x) / float(side - 1) * 2.f - 1.f;
    SUB_CASE("interpolation and clamping") {
        CHECK(std::fabs(sampleCaveDensity(linear, side, side, side, {.25f, .3f, -.4f}) - .25f) < 1e-6f);
        CHECK_EQ(sampleCaveDensity(linear, side, side, side, {2.f, 0.f, 0.f}), 1.f);
        CHECK_EQ(sampleCaveDensity(linear, side, side, side, {-2.f, 0.f, 0.f}), -1.f);
        const auto gradient = sampleCaveDensityGradient(linear, side, side, side, {0, 0, 0});
        CHECK(std::fabs(gradient.x - 1.f) < 1e-5f);
        CHECK(std::fabs(gradient.y) < 1e-5f);
        CHECK(std::fabs(gradient.z) < 1e-5f);
    };
    SUB_CASE("supersampling retains every original lattice point") {
        const auto refined = resampleCaveDensity(linear, side, side, side, 2);
        REQUIRE_EQ(refined.nx, 13);
        CHECK_EQ(refined.ny, 13);
        CHECK_EQ(refined.nz, 13);
        for (int z = 0; z < side; ++z)
            for (int y = 0; y < side; ++y)
                for (int x = 0; x < side; ++x) {
                    const auto original = std::size_t(x + side * (y + side * z));
                    const auto upscale  = std::size_t(2 * x + 13 * (2 * y + 13 * 2 * z));
                    CHECK(std::fabs(refined.density[upscale] - linear[original]) < 1e-6f);
                }
    };
}

TEST_CASE("Cave field boundary: closure seals exposed air and is seeded while leaving interior unchanged") {
    constexpr int      side = 11;
    std::vector<float> open(std::size_t(side * side * side), -.2f);
    auto               disabled = open;
    const auto         zero     = closeCaveDensityBoundary(disabled, side, side, side, 0.f, 19u);
    REQUIRE(disabled == open);
    CHECK_EQ(zero.changedVoxels, 0);
    auto       first = open, repeat = open;
    const auto closed = closeCaveDensityBoundary(first, side, side, side, 1.f, 19u);
    const auto replay = closeCaveDensityBoundary(repeat, side, side, side, 1.f, 19u);
    CHECK(first == repeat);
    CHECK_EQ(closed.changedVoxels, replay.changedVoxels);
    CHECK(closed.airSamplesBefore > 0);
    CHECK_EQ(closed.airSamplesAfter, 0);
    CHECK_EQ(first[std::size_t(5 + side * (5 + side * 5))], -.2f);
}

TEST_CASE("Cave fracture channelization: aperture and intersection control advective retreat") {
    CaveFractureChannelizationInput input;
    CHECK_EQ(sampleCaveFractureChannelization(input).erosion, 0.f);
    input.primaryMask               = .8f;
    input.primaryApertureMultiplier = 1.f;
    CHECK_EQ(sampleCaveFractureChannelization(input).erosion, 0.f);
    input.primaryApertureMultiplier = 1.8f;
    const auto single               = sampleCaveFractureChannelization(input);
    REQUIRE(single.erosion > 0.f);
    CHECK(single.flowConcentration > 0.f);
    CHECK_EQ(single.intersectionAmplification, 0.f);
    input.secondaryMask               = .8f;
    input.secondaryApertureMultiplier = 1.8f;
    const auto crossing               = sampleCaveFractureChannelization(input);
    CHECK(crossing.intersectionAmplification > 0.f);
    CHECK(crossing.erosion > single.erosion);
    input.distanceFromInlet = 20.f;
    const auto downstream   = sampleCaveFractureChannelization(input);
    CHECK(downstream.reactantAccess < crossing.reactantAccess);
    CHECK(downstream.erosion < crossing.erosion);
}

TEST_CASE("Cave recipe: independent geology controls select an observable model and replay exactly") {
    struct Variant {
        const char* parameter;
        float       strength;
        const char* metadata;
        const char* model;
    };
    const std::array<Variant, 7> variants{{
        {"boundaryClosure", 1.f, "boundaryClosureModel", "rough-host-envelope-v1"},
        {"condensationFaceting", .8f, "condensationFacetModel", "local-convection-planar-envelope-v1"},
        {"differentialVeinErosion", .8f, "differentialVeinModel", "resistant-vein-host-retreat-v1"},
        {"breakdown", .8f, "breakdownModel", "paired-ceiling-spall-talus-v1"},
        {"sedimentDeposition", .8f, "sedimentModel", "longitudinal-bar-imbrication-v1"},
        {"multiscaleRoughness", .8f, "wallRoughnessSpectrum", "band-limited-three-scale-v1"},
        {"chamberHierarchy", .8f, "macroMorphologyModel", "hierarchical-primary-hall-v2"},
    }};
    for (const auto& variant : variants) {
        auto params = caveParams();
        params.setFloat(variant.parameter, variant.strength);
        const auto first    = generate(params);
        const auto repeated = generate(params);
        REQUIRE_EQ(first.getMeta(variant.metadata, ""), std::string(variant.model));
        CHECK(first.positions() == repeated.positions());
        CHECK(first.indices() == repeated.indices());
    }
}

TEST_CASE("Cave flood operators: rock joints and sediment tools are required inputs") {
    auto params = caveParams();
    params.setString("style", "tunnels");
    params.setString("genesis", "epigene");
    params.setInt("chambers", 5);
    params.setInt("branches", 4);
    params.setInt("dripstones", 0);
    params.setInt("flowstones", 0);
    params.setInt("curtains", 0);
    params.setFloat("roughness", 0.f);
    params.setFloat("erosion", 0.f);
    params.setFloat("vadoseIncision", 0.f);
    SUB_CASE("plucking retreats fractured rock but is inert without a fracture network") {
        params.setInt("fractureCount", 10);
        const auto baseline = generate(params);
        params.setFloat("floodPlucking", 1.f);
        params.setFloat("pluckingBlockScale", .14f);
        const auto plucked = generate(params);
        REQUIRE(plucked.positions() != baseline.positions());
        CHECK_EQ(plucked.getMeta("floodPluckingModel", ""), std::string("thresholded-fracture-block-release-v1"));
        CHECK(std::stoi(plucked.getMeta("pluckingAffectedVoxels", "0")) > 0);
        params.setInt("fractureCount", 0);
        const auto inert = generate(params);
        params.setFloat("floodPlucking", 0.f);
        const auto noJoints = generate(params);
        CHECK(inert.positions() == noJoints.positions());
        CHECK_EQ(inert.getMeta("floodPluckingModel", ""), std::string("inactive-no-fracture-network"));
    };
    SUB_CASE("abrasion retreats the wetted bed only when sediment tools are present") {
        params.setFloat("sedimentLoad", .48f);
        const auto baseline = generate(params);
        params.setFloat("floodAbrasion", 1.f);
        const auto abraded = generate(params);
        CHECK(abraded.positions() != baseline.positions());
        CHECK_EQ(abraded.getMeta("floodAbrasionModel", ""), std::string("near-bed-tools-cover-vortex-v1"));
        CHECK(std::stoi(abraded.getMeta("abrasionAffectedVoxels", "0")) > 0);
        params.setFloat("sedimentLoad", 0.f);
        const auto inert = generate(params);
        params.setFloat("floodAbrasion", 0.f);
        const auto noTools = generate(params);
        CHECK(inert.positions() == noTools.positions());
        CHECK_EQ(inert.getMeta("floodAbrasionModel", ""), std::string("inactive-no-tools"));
    };
}

TEST_CASE("Cave local hydrology samplers: floor flow and staged water line and fracture supply") {
    SUB_CASE("sediment abrasion acts near the floor and needs both flow and tools") {
        CaveAbrasionInput floor{.42f, 3.1415926535f, 1.f, .45f, 3.1415926535f, .48f, 20260830u};
        const auto        active = sampleCaveFloodAbrasion(floor);
        CHECK(active.floorMask > .99f);
        REQUIRE(active.erosion > 0.f);
        CHECK(active.coverProtection > 0.f);
        CHECK_EQ(sampleCaveFloodAbrasion(floor).erosion, active.erosion);
        auto crown  = floor;
        crown.angle = 0.f;
        CHECK(sampleCaveFloodAbrasion(crown).erosion < active.erosion * .01f);
        auto slow               = floor;
        slow.hydraulicIntensity = .15f;
        CHECK(sampleCaveFloodAbrasion(slow).erosion < active.erosion);
        auto noTools         = floor;
        noTools.sedimentLoad = 0.f;
        CHECK_EQ(sampleCaveFloodAbrasion(noTools).erosion, 0.f);
    };
    SUB_CASE("water-table belts favour sidewalls and expose the dominant stage") {
        CaveWaterTableInput input;
        input.y            = 0.f;
        input.passageAngle = 1.57f;
        input.along        = .25f;
        input.level        = 0.f;
        input.stages       = 3;
        input.seed         = 20260830u;
        const auto side    = sampleCaveWaterTableCorrosion(input);
        CHECK(side.sidewallMask > 0.f);
        CHECK(side.erosion >= 0.f);
        CHECK(side.dominantStage >= 0);
        CHECK(side.dominantStage < input.stages);
        CHECK_EQ(sampleCaveWaterTableCorrosion(input).erosion, side.erosion);
        auto crown         = input;
        crown.passageAngle = 0.f;
        CHECK(sampleCaveWaterTableCorrosion(crown).sidewallMask < side.sidewallMask);
    };
}

TEST_CASE("Cave curvature operator: zero strength is identity while positive rate changes only a bounded surface") {
    constexpr int side     = 17;
    const auto    original = sphereField(side, .62f);
    auto          disabled = original;
    const auto    zero     = evolveCaveSurfaceByCurvature(disabled, side, side, side, 0.f);
    CHECK(disabled == original);
    CHECK_EQ(zero.affectedVoxels, 0);
    auto       first = original, repeat = original;
    const auto evolved = evolveCaveSurfaceByCurvature(first, side, side, side, 1.f);
    const auto replay  = evolveCaveSurfaceByCurvature(repeat, side, side, side, 1.f);
    REQUIRE(first == repeat);
    CHECK_EQ(evolved.affectedVoxels, replay.affectedVoxels);
    CHECK(evolved.affectedVoxels > 0);
    CHECK(evolved.maximumRetreat > 0.f);
    std::vector<float> rates(original.size(), .5f);
    auto               coupled = original;
    const auto         result  = evolveCaveSurfaceByCurvature(coupled, rates, side, side, side, 1.f);
    CHECK(result.affectedVoxels > 0);
    CHECK(result.maximumRateMultiplier <= 1.f);
    CHECK(coupled != first);
}

TEST_CASE("Cave recipe: staged geology declares absent prerequisites and activates when supplied") {
    SUB_CASE("paragenesis needs sediment deposits") {
        auto       params   = caveParams();
        const auto baseline = generate(params);
        params.setFloat("paragenesis", .8f);
        const auto noSediment = generate(params);
        CHECK(noSediment.positions() == baseline.positions());
        CHECK_EQ(noSediment.getMeta("paragenesisStatus", ""), std::string("inactive-no-sediment"));
        params.setFloat("sedimentDeposition", .9f);
        params.setInt("sedimentBars", 4);
        const auto applied = generate(params);
        REQUIRE_EQ(applied.getMeta("paragenesisStatus", ""), std::string("applied"));
        CHECK(std::stoi(applied.getMeta("parageneticChannels", "0")) > 0);
        CHECK_EQ(applied.getMeta("sedimentModel", ""), std::string("longitudinal-bar-imbrication-v1"));
        CHECK(std::stoi(applied.getMeta("sedimentClasts", "0")) >= 12);
    };
    SUB_CASE("water table corrosion applies in epigene passages and is inert in hypogene genesis") {
        auto params = caveParams();
        params.setString("genesis", "epigene");
        params.setFloat("waterTableCorrosion", 1.f);
        params.setFloat("waterTableLevel", .3f);
        params.setInt("waterTableStages", 3);
        params.setFloat("waterTableDrop", .2f);
        const auto epigene = generate(params);
        CHECK_EQ(epigene.getMeta("waterTableCorrosionModel", ""), std::string("descending-epiphreatic-belts-v1"));
        CHECK(std::stoi(epigene.getMeta("waterTableAffectedVoxels", "0")) > 0);
        params.setString("genesis", "hypogene");
        const auto inactive = generate(params);
        CHECK_EQ(inactive.getMeta("waterTableCorrosionModel", ""), std::string("inactive-hypogene"));
        params.setFloat("waterTableCorrosion", 0.f);
        CHECK(inactive.positions() == generate(params).positions());
    };
}
