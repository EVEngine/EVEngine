#include "procgen/road/RoadBake.h"
#include "procgen/road/RoadNetwork.h"
#include "procgen/road/RoadRecipes.h"
#include "procgen/road/RoadTypes.h"
#include "procgen/Params.h"
#include "procgen/algorithms/MarchingCubes.h"
#include "procgen/texture/TextureRecipe.h"
#include "image/ImageData.h"

#include "zeroerr/unittest.h"

#include <cmath>
#include <string>

using namespace eve;
using namespace eve::procgen;
using namespace eve::procgen::road;

namespace {

}  // namespace

TEST_CASE("procgen.road.profile.laneWidths") {
    RoadStyle style;
    style.laneWidth     = 3.5f;
    style.curbWidth     = 0.4f;
    style.sidewalkWidth = 1.5f;
    auto profile        = makeRoadProfile(style, 2, 0);
    REQUIRE(profile.ok());
    CHECK(std::fabs(profile.value().halfWidth - (3.5f + 0.4f + 1.5f)) < 1e-4f);
    CHECK_GE(profile.value().points.size(), 8u);
    CHECK_EQ(std::string(roadMaterialGroup(RoadMaterial::Asphalt)), "asphalt");
}

TEST_CASE("procgen.road.network.interchangeBakeHasGroupsAndOverlay") {
    auto network = RoadNetwork::makeInterchange(40.f, 7.f, 2, 3);
    REQUIRE(network.ok());
    CHECK_GE(network.value().nodeCount(), 8);
    CHECK_GE(network.value().edgeCount(), 8);
    CHECK_GT(network.value().laneLinkCount(), 0);

    RoadBakeOptions options;
    options.pathSegmentsPerEdge = 24;
    options.turnSamples         = 8;
    auto baked                  = bakeRoadNetwork(network.value(), options);
    REQUIRE(baked.ok());
    CHECK_GT(baked.value().mesh.getVertexCount(), 100);
    CHECK_GT(baked.value().mesh.getIndexCount(), 100);
    CHECK_GT(baked.value().mesh.getGroupCount(), 3);
    CHECK_GT(baked.value().overlay.lanes.size(), 0u);
    CHECK_GT(baked.value().overlay.turns.size(), 0u);
    CHECK_EQ(baked.value().mesh.getMeta("schema", ""), "eve.procgen.roadNetwork");

    bool sawAsphalt = false, sawPier = false, sawNav = false, sawMarking = false;
    for (int i = 0; i < baked.value().mesh.getGroupCount(); ++i) {
        const auto name = baked.value().mesh.getGroupName(i);
        if (name == "asphalt") sawAsphalt = true;
        if (name == "pier") sawPier = true;
        if (name == "nav") sawNav = true;
        if (name == "marking") sawMarking = true;
    }
    CHECK(sawAsphalt);
    CHECK(sawPier);
    CHECK(sawNav);
    CHECK(sawMarking);
}

TEST_CASE("procgen.road.network.rejectsInvalidEdge") {
    RoadNetwork network;
    auto        a = network.addNode(0.f, 0.f, 0.f);
    auto        b = network.addNode(10.f, 0.f, 0.f);
    REQUIRE(a.ok());
    REQUIRE(b.ok());
    auto bad = network.addEdge(a.value(), b.value(), {RoadControlPoint{0.f, 0.f, 0.f}}, 2, 0);
    CHECK(!bad.ok());
}

TEST_CASE("procgen.road.recipe.meshAndTextureDeterministic") {
    MeshRecipeRegistry::instance().registerBuiltins();
    TextureRecipeRegistry::instance().registerBuiltins();
    REQUIRE(MeshRecipeRegistry::instance().has("mesh.roadNetwork"));
    REQUIRE(TextureRecipeRegistry::instance().has("tex.roadMarkings"));

    Params params;
    params.setSeed(11);
    params.setFloat("span", 36.f);
    params.setFloat("bridgeHeight", 6.f);
    params.setInt("lanes", 2);
    params.setInt("pathSegments", 20);

    MeshBuild first, second;
    std::string error;
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.roadNetwork", params, first, error));
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.roadNetwork", params, second, error));
    CHECK_EQ(first.getVertexCount(), second.getVertexCount());
    CHECK_EQ(first.getIndexCount(), second.getIndexCount());

    params.setSize(128, 32);
    auto tex = TextureRecipeRegistry::instance().generate("tex.roadMarkings", params, error);
    REQUIRE(tex);
    CHECK_EQ(tex->getWidth(), 128);
    CHECK_EQ(tex->getHeight(), 32);
}

TEST_CASE("procgen.road.overlay.fromParams") {
    auto network = RoadNetwork::makeInterchange(40.f, 7.f, 2, 3);
    REQUIRE(network.ok());
    RoadBakeOptions options;
    options.pathSegmentsPerEdge = 24;
    options.turnSamples         = 8;
    options.includeNavigation   = true;
    auto baked                  = bakeRoadNetwork(network.value(), options);
    REQUIRE(baked.ok());
    CHECK_GT(baked.value().overlay.lanes.size(), 0u);
    CHECK_GT(baked.value().overlay.turns.size(), 0u);
}

TEST_CASE("procgen.road.network.seedSevenNavigationTerminates") {
    // Seed 7 previously produced large frame steps that hung the nav arrow loop.
    auto network = RoadNetwork::makeInterchange(40.f, 7.f, 2, 7);
    REQUIRE(network.ok());
    RoadBakeOptions options;
    options.pathSegmentsPerEdge = 24;
    options.turnSamples         = 8;
    options.includeNavigation   = true;
    auto baked                  = bakeRoadNetwork(network.value(), options);
    REQUIRE(baked.ok());
    CHECK_GT(baked.value().mesh.getVertexCount(), 100);
    CHECK_GT(baked.value().overlay.lanes.size(), 0u);
}
