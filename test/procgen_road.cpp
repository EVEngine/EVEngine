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
#include <vector>

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

    bool sawAsphalt = false, sawPier = false, sawNav = false, sawMarking = false, sawCurb = false;
    for (int i = 0; i < baked.value().mesh.getGroupCount(); ++i) {
        const auto name = baked.value().mesh.getGroupName(i);
        if (name == "asphalt") sawAsphalt = true;
        if (name == "pier") sawPier = true;
        if (name == "nav") sawNav = true;
        if (name == "marking" || name == "markingYellow") sawMarking = true;
        if (name == "curb") sawCurb = true;
    }
    CHECK(sawAsphalt);
    CHECK(sawPier);
    CHECK(sawNav);
    CHECK(sawMarking);
    CHECK(sawCurb);
    CHECK(baked.value().mesh.hasVertexColors());
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

TEST_CASE("procgen.road.scenes.fourSimpleBakeClean") {
    RoadBakeOptions options;
    options.pathSegmentsPerEdge = 20;
    options.turnSamples         = 6;
    options.includeNavigation   = false;

    auto straight = RoadNetwork::makeStraight(28.f, 2);
    REQUIRE(straight.ok());
    CHECK_EQ(straight.value().edgeCount(), 1);
    options.includeJunctions = false;
    auto bakedStraight = bakeRoadNetwork(straight.value(), options);
    REQUIRE(bakedStraight.ok());
    CHECK_GT(bakedStraight.value().mesh.getVertexCount(), 50);

    auto curve = RoadNetwork::makeCurve(16.f, 2);
    REQUIRE(curve.ok());
    CHECK_EQ(curve.value().edgeCount(), 1);
    auto bakedCurve = bakeRoadNetwork(curve.value(), options);
    REQUIRE(bakedCurve.ok());
    CHECK_GT(bakedCurve.value().mesh.getVertexCount(), 50);

    auto bridge = RoadNetwork::makeBridge(30.f, 5.f, 2);
    REQUIRE(bridge.ok());
    options.includePiers = true;
    auto bakedBridge = bakeRoadNetwork(bridge.value(), options);
    REQUIRE(bakedBridge.ok());
    bool sawPier = false;
    for (int i = 0; i < bakedBridge.value().mesh.getGroupCount(); ++i) {
        if (bakedBridge.value().mesh.getGroupName(i) == "pier") sawPier = true;
    }
    CHECK(sawPier);

    auto cross = RoadNetwork::makeCross(28.f, 2);
    REQUIRE(cross.ok());
    CHECK_EQ(cross.value().edgeCount(), 4);
    CHECK_GT(cross.value().laneLinkCount(), 0);
    options.includeJunctions  = true;
    options.includeNavigation = false;
    auto bakedCross = bakeRoadNetwork(cross.value(), options);
    REQUIRE(bakedCross.ok());
    CHECK_GT(bakedCross.value().mesh.getVertexCount(), 100);

    MeshRecipeRegistry::instance().registerBuiltins();
    Params params;
    params.setString("scene", "straight");
    params.setFloat("span", 24.f);
    params.setInt("lanes", 2);
    params.setInt("pathSegments", 16);
    params.setBool("navigation", false);
    params.setBool("junctions", false);
    MeshBuild mesh;
    std::string error;
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.roadNetwork", params, mesh, error));
    CHECK_GT(mesh.getVertexCount(), 40);
    CHECK(!RoadNetwork::makeScene("nope", 20.f, 4.f, 2, 1).ok());
}


TEST_CASE("procgen.road.scenes.crossJunctionHasCenterAsphalt") {
    auto cross = RoadNetwork::makeCross(28.f, 2);
    REQUIRE(cross.ok());
    RoadBakeOptions options;
    options.pathSegmentsPerEdge = 20;
    options.includeJunctions = true;
    options.includeNavigation = false;
    options.includePiers = false;
    auto baked = bakeRoadNetwork(cross.value(), options);
    REQUIRE(baked.ok());
    const auto& m = baked.value().mesh;
    int asphaltGroup = -1;
    for (int g = 0; g < m.getGroupCount(); ++g) {
        if (m.getGroupName(g) == "asphalt") asphaltGroup = g;
    }
    CHECK_GE(asphaltGroup, 0);
    auto asphalt = m.copyGroup(asphaltGroup);
    REQUIRE(asphalt);
    int near = 0;
    float minX=1e9,maxX=-1e9,minZ=1e9,maxZ=-1e9;
    for (int i = 0; i < asphalt->getVertexCount(); ++i) {
        const float x = asphalt->getPositionX(i);
        const float z = asphalt->getPositionZ(i);
        if (std::fabs(x) < 3.f && std::fabs(z) < 3.f) {
            ++near;
            minX=std::min(minX,x); maxX=std::max(maxX,x);
            minZ=std::min(minZ,z); maxZ=std::max(maxZ,z);
        }
    }
    // Does any asphalt triangle cover the origin in XZ?
    int cover = 0;
    for (int t = 0; t < asphalt->getIndexCount() / 3; ++t) {
        const int i0 = asphalt->getIndex(t * 3 + 0);
        const int i1 = asphalt->getIndex(t * 3 + 1);
        const int i2 = asphalt->getIndex(t * 3 + 2);
        const float x0 = asphalt->getPositionX(i0), z0 = asphalt->getPositionZ(i0);
        const float x1 = asphalt->getPositionX(i1), z1 = asphalt->getPositionZ(i1);
        const float x2 = asphalt->getPositionX(i2), z2 = asphalt->getPositionZ(i2);
        // barycentric in XZ for (0,0)
        const float den = (z1 - z2) * (x0 - x2) + (x2 - x1) * (z0 - z2);
        if (std::fabs(den) < 1e-8f) continue;
        const float a = ((z1 - z2) * (0.f - x2) + (x2 - x1) * (0.f - z2)) / den;
        const float b = ((z2 - z0) * (0.f - x2) + (x0 - x2) * (0.f - z2)) / den;
        const float c = 1.f - a - b;
        if (a >= -1e-4f && b >= -1e-4f && c >= -1e-4f) {
            ++cover;
        }
    }
    CHECK_GT(cover, 0);
}




TEST_CASE("procgen.road.scenes.pierUprightFootprint") {
    auto bridge = RoadNetwork::makeBridge(30.f, 5.f, 2);
    REQUIRE(bridge.ok());
    RoadBakeOptions options;
    options.pathSegmentsPerEdge = 24;
    options.includePiers = true;
    options.includeJunctions = false;
    options.includeNavigation = false;
    auto baked = bakeRoadNetwork(bridge.value(), options);
    REQUIRE(baked.ok());
    int pierGroup = -1;
    for (int g = 0; g < baked.value().mesh.getGroupCount(); ++g)
        if (baked.value().mesh.getGroupName(g) == "pier") pierGroup = g;
    REQUIRE(pierGroup >= 0);
    auto pier = baked.value().mesh.copyGroup(pierGroup);
    REQUIRE(pier);
    // Cluster by X into piers; for each pier check top/bottom XZ extents match
    struct Vert { float x,y,z; };
    std::vector<Vert> verts;
    for (int i = 0; i < pier->getVertexCount(); ++i)
        verts.push_back({pier->getPositionX(i), pier->getPositionY(i), pier->getPositionZ(i)});
    // unique pier centers roughly by rounding X
    std::vector<float> centers;
    for (const auto& v : verts) {
        bool found = false;
        for (float c : centers) if (std::fabs(c - v.x) < 1.5f) { found = true; break; }
        if (!found) centers.push_back(v.x);
    }
    for (float cx : centers) {
        float minXb=1e9,maxXb=-1e9,minZb=1e9,maxZb=-1e9;
        float minXt=1e9,maxXt=-1e9,minZt=1e9,maxZt=-1e9;
        float minY=1e9,maxY=-1e9;
        for (const auto& v : verts) {
            if (std::fabs(v.x - cx) > 2.0f) continue;
            minY=std::min(minY,v.y); maxY=std::max(maxY,v.y);
        }
        const float yCut = minY + 0.15f * (maxY - minY);
        const float yTop = maxY - 0.15f * (maxY - minY);
        for (const auto& v : verts) {
            if (std::fabs(v.x - cx) > 2.0f) continue;
            if (v.y <= yCut) {
                minXb=std::min(minXb,v.x); maxXb=std::max(maxXb,v.x);
                minZb=std::min(minZb,v.z); maxZb=std::max(maxZb,v.z);
            }
            if (v.y >= yTop) {
                minXt=std::min(minXt,v.x); maxXt=std::max(maxXt,v.x);
                minZt=std::min(minZt,v.z); maxZt=std::max(maxZt,v.z);
            }
        }
        const float dx = std::fabs((minXb+maxXb)*0.5f - (minXt+maxXt)*0.5f);
        const float dz = std::fabs((minZb+maxZb)*0.5f - (minZt+maxZt)*0.5f);
        CHECK_LT(dx, 0.05f);
        CHECK_LT(dz, 0.05f);
    }
}

