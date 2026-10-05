#include "procgen/road/RoadBake.h"
#include "procgen/road/RoadDecor.h"
#include "procgen/road/RoadNetwork.h"
#include "procgen/road/RoadRecipes.h"
#include "procgen/road/RoadTypes.h"
#include "procgen/Params.h"
#include "procgen/algorithms/MarchingCubes.h"
#include "procgen/texture/TextureRecipe.h"
#include "image/ImageData.h"

#include "zeroerr/unittest.h"

#include <cmath>
#include <limits>
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

TEST_CASE("procgen.road.scenes.interchangeGroundCrossAndPiers") {
    auto network = RoadNetwork::makeInterchange(48.f, 8.f, 2, 1);
    REQUIRE(network.ok());
    CHECK_GE(network.value().nodeCount(), 10);
    CHECK_EQ(network.value().edgeCount(), 12);

    float hubJr = 0.f;
    for (const auto& n : network.value().nodes()) {
        if (std::fabs(n.x) < 1e-3f && std::fabs(n.z) < 1e-3f && std::fabs(n.y) < 1e-3f) hubJr = n.junctionRadius;
    }
    // Same fillet sizing as makeCross: asphaltHalf + cornerR.
    CHECK_GT(hubJr, 5.5f);
    CHECK_LT(hubJr, 7.5f);

    RoadBakeOptions options;
    options.pathSegmentsPerEdge = 28;
    options.turnSamples         = 8;
    options.includeJunctions    = true;
    options.includeNavigation   = false;
    options.includePiers        = true;
    options.includeMarkings     = true;
    auto baked                  = bakeRoadNetwork(network.value(), options);
    REQUIRE(baked.ok());
    CHECK_GT(baked.value().mesh.getVertexCount(), 2000);

    bool sawPier = false, sawSidewalk = false, sawDeck = false;
    for (int i = 0; i < baked.value().mesh.getGroupCount(); ++i) {
        const auto name = baked.value().mesh.getGroupName(i);
        if (name == "pier") sawPier = true;
        if (name == "sidewalk") sawSidewalk = true;
        if (name == "deck") sawDeck = true;
    }
    CHECK(sawPier);
    CHECK(sawSidewalk);
    CHECK(sawDeck);

    // Ground-hub sidewalks should sit on outward-center curb returns (near jr,jr disk).
    int sidewalkGroup = -1;
    for (int g = 0; g < baked.value().mesh.getGroupCount(); ++g) {
        if (baked.value().mesh.getGroupName(g) == "sidewalk") sidewalkGroup = g;
    }
    REQUIRE(sidewalkGroup >= 0);
    auto sw = baked.value().mesh.copyGroup(sidewalkGroup);
    REQUIRE(sw);
    int arcHits = 0;
    for (int i = 0; i < sw->getVertexCount(); ++i) {
        if (std::fabs(sw->getPositionY(i)) > 0.5f) continue;  // ground level only
        const float x  = std::fabs(sw->getPositionX(i));
        const float z  = std::fabs(sw->getPositionZ(i));
        const float dx = hubJr - x;
        const float dz = hubJr - z;
        if (dx < 0.05f || dz < 0.05f) continue;
        const float r = std::sqrt(dx * dx + dz * dz);
        if (r > 0.9f && r < 2.9f) ++arcHits;
    }
    CHECK_GT(arcHits, 16);
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


TEST_CASE("procgen.road.scenes.crossJunctionCornerSidewalks") {
    auto cross = RoadNetwork::makeCross(28.f, 2);
    REQUIRE(cross.ok());
    // Arms trim past asphaltHalf so corner arcs own the ring (jr ≈ 3.5 + 2.8).
    float hubJr = 0.f;
    for (const auto& n : cross.value().nodes()) {
        if (std::fabs(n.x) < 1e-3f && std::fabs(n.z) < 1e-3f) hubJr = n.junctionRadius;
    }
    CHECK_GT(hubJr, 5.5f);
    CHECK_LT(hubJr, 7.5f);

    RoadBakeOptions options;
    options.pathSegmentsPerEdge = 20;
    options.includeJunctions    = true;
    options.includeNavigation   = false;
    options.includePiers        = false;
    auto baked                  = bakeRoadNetwork(cross.value(), options);
    REQUIRE(baked.ok());
    int sidewalkGroup = -1;
    for (int g = 0; g < baked.value().mesh.getGroupCount(); ++g) {
        if (baked.value().mesh.getGroupName(g) == "sidewalk") sidewalkGroup = g;
    }
    CHECK_GE(sidewalkGroup, 0);
    auto sw = baked.value().mesh.copyGroup(sidewalkGroup);
    REQUIRE(sw);
    // Outward-center curb returns: sidewalk ring around (jr,jr), no tip disk to the center.
    const float jr = hubJr;
    int         arcHits = 0;
    int         tipHits = 0;
    int         outerSkirtHits = 0;
    for (int i = 0; i < sw->getVertexCount(); ++i) {
        const float x  = std::fabs(sw->getPositionX(i));
        const float z  = std::fabs(sw->getPositionZ(i));
        const float dx = jr - x;
        const float dz = jr - z;
        if (dx < 0.05f || dz < 0.05f) continue;
        const float r = std::sqrt(dx * dx + dz * dz);
        // Sidewalk annulus between outer skirt and curb (no fan into r≈0 tip).
        if (r > 0.9f && r < 2.9f) ++arcHits;
        if (r < 0.55f) ++tipHits;
        const float nx = sw->getNormalX(i);
        const float nz = sw->getNormalZ(i);
        // Outer skirt faces the hub (away from property corner) so overhead views see it.
        if (r > 0.7f && r < 1.4f && (nx * dx + nz * dz) < -0.3f) ++outerSkirtHits;
    }
    CHECK_GT(arcHits, 16);
    CHECK_EQ(tipHits, 0);
    CHECK_GT(outerSkirtHits, 8);

    // Top faces must carry +Y normals (mesh3D CCW / lighting); flipped windings show dark.
    int upHits = 0, downHits = 0;
    for (int i = 0; i < sw->getVertexCount(); ++i) {
        const float ny = sw->getNormalY(i);
        if (ny > 0.5f) ++upHits;
        if (ny < -0.5f) ++downHits;
    }
    CHECK_GT(upHits, 16);
    CHECK_EQ(downHits, 0);
}



TEST_CASE("procgen.road.scenes.crossJunctionArmApronSeam") {
    auto cross = RoadNetwork::makeCross(28.f, 2);
    REQUIRE(cross.ok());
    float hubJr = 0.f;
    for (const auto& n : cross.value().nodes()) {
        if (std::fabs(n.x) < 1e-3f && std::fabs(n.z) < 1e-3f) hubJr = n.junctionRadius;
    }
    REQUIRE(hubJr > 5.f);

    RoadBakeOptions options;
    options.pathSegmentsPerEdge = 28;
    options.includeJunctions    = true;
    options.includeNavigation   = false;
    options.includePiers        = false;
    auto baked                  = bakeRoadNetwork(cross.value(), options);
    REQUIRE(baked.ok());
    int asphaltGroup = -1;
    for (int g = 0; g < baked.value().mesh.getGroupCount(); ++g) {
        if (baked.value().mesh.getGroupName(g) == "asphalt") asphaltGroup = g;
    }
    REQUIRE(asphaltGroup >= 0);
    auto asphalt = baked.value().mesh.copyGroup(asphaltGroup);
    REQUIRE(asphalt);

    // Along +Z arm centerline, asphalt should cover continuously across jr (no gap).
    const float ah = 3.5f;
    auto covers = [&](float x, float z) {
        for (int t = 0; t < asphalt->getIndexCount() / 3; ++t) {
            const int i0 = asphalt->getIndex(t * 3 + 0);
            const int i1 = asphalt->getIndex(t * 3 + 1);
            const int i2 = asphalt->getIndex(t * 3 + 2);
            const float x0 = asphalt->getPositionX(i0), z0 = asphalt->getPositionZ(i0);
            const float x1 = asphalt->getPositionX(i1), z1 = asphalt->getPositionZ(i1);
            const float x2 = asphalt->getPositionX(i2), z2 = asphalt->getPositionZ(i2);
            const float den = (z1 - z2) * (x0 - x2) + (x2 - x1) * (z0 - z2);
            if (std::fabs(den) < 1e-8f) continue;
            const float a = ((z1 - z2) * (x - x2) + (x2 - x1) * (z - z2)) / den;
            const float b = ((z2 - z0) * (x - x2) + (x0 - x2) * (z - z2)) / den;
            const float c = 1.f - a - b;
            if (a >= -1e-3f && b >= -1e-3f && c >= -1e-3f) return true;
        }
        return false;
    };
    // Probe just inside apron, on the seam, and just onto the arm.
    CHECK(covers(0.f, hubJr - 0.25f));
    CHECK(covers(0.f, hubJr));
    CHECK(covers(0.f, hubJr + 0.25f));
    CHECK(covers(0.f, -(hubJr)));
    CHECK(covers(hubJr, 0.f));
    CHECK(covers(-hubJr, 0.f));
    // Stay within asphalt half-width when probing the seam.
    CHECK(covers(ah * 0.5f, hubJr));
}

TEST_CASE("procgen.road.scenes.teeAndYHaveFilletedJunctions") {
    RoadBakeOptions options;
    options.pathSegmentsPerEdge = 24;
    options.includeJunctions    = true;
    options.includeNavigation   = false;
    options.includePiers        = false;

    auto tee = RoadNetwork::makeTee(28.f, 2);
    REQUIRE(tee.ok());
    CHECK_EQ(tee.value().edgeCount(), 3);
    auto bakedTee = bakeRoadNetwork(tee.value(), options);
    REQUIRE(bakedTee.ok());

    bool sawCurb = false, sawSidewalk = false, sawAsphalt = false;
    for (int i = 0; i < bakedTee.value().mesh.getGroupCount(); ++i) {
        const auto name = bakedTee.value().mesh.getGroupName(i);
        if (name == "curb") sawCurb = true;
        if (name == "sidewalk") sawSidewalk = true;
        if (name == "asphalt") sawAsphalt = true;
    }
    CHECK(sawCurb);
    CHECK(sawSidewalk);
    CHECK(sawAsphalt);

    // Hub asphalt must cover the origin and the wide northern back chord.
    int asphaltGroup = -1;
    for (int g = 0; g < bakedTee.value().mesh.getGroupCount(); ++g) {
        if (bakedTee.value().mesh.getGroupName(g) == "asphalt") asphaltGroup = g;
    }
    REQUIRE(asphaltGroup >= 0);
    auto asphalt = bakedTee.value().mesh.copyGroup(asphaltGroup);
    REQUIRE(asphalt);
    auto covers = [&](float x, float z) {
        for (int t = 0; t < asphalt->getIndexCount() / 3; ++t) {
            const int i0 = asphalt->getIndex(t * 3 + 0);
            const int i1 = asphalt->getIndex(t * 3 + 1);
            const int i2 = asphalt->getIndex(t * 3 + 2);
            const float x0 = asphalt->getPositionX(i0), z0 = asphalt->getPositionZ(i0);
            const float x1 = asphalt->getPositionX(i1), z1 = asphalt->getPositionZ(i1);
            const float x2 = asphalt->getPositionX(i2), z2 = asphalt->getPositionZ(i2);
            const float den = (z1 - z2) * (x0 - x2) + (x2 - x1) * (z0 - z2);
            if (std::fabs(den) < 1e-8f) continue;
            const float a = ((z1 - z2) * (x - x2) + (x2 - x1) * (z - z2)) / den;
            const float b = ((z2 - z0) * (x - x2) + (x0 - x2) * (z - z2)) / den;
            const float c = 1.f - a - b;
            if (a >= -1e-3f && b >= -1e-3f && c >= -1e-3f) return true;
        }
        return false;
    };
    CHECK(covers(0.f, 0.f));
    CHECK(covers(0.f, 2.f));   // toward stem
    CHECK(covers(-2.f, 0.f));  // through road
    CHECK(covers(2.f, 0.f));

    auto yj = RoadNetwork::makeY(28.f, 2);
    REQUIRE(yj.ok());
    CHECK_EQ(yj.value().edgeCount(), 3);
    auto bakedY = bakeRoadNetwork(yj.value(), options);
    REQUIRE(bakedY.ok());
    CHECK_GT(bakedY.value().mesh.getVertexCount(), 200);
    CHECK(RoadNetwork::makeScene("tee", 24.f, 4.f, 2, 1).ok());
    CHECK(RoadNetwork::makeScene("y", 24.f, 4.f, 2, 1).ok());
    CHECK(RoadNetwork::makeScene("fork", 24.f, 4.f, 2, 1).ok());
    CHECK(RoadNetwork::makeScene("skew", 24.f, 4.f, 2, 1).ok());
}

TEST_CASE("procgen.road.scenes.yJunctionArmFilletDocks") {
    // Y corners are 120°: curb-return tangency is closer to the hub than
    // junctionRadius. Arms must trim to that tangency so curb tips meet the
    // fillet without salmon gaps.
    auto yj = RoadNetwork::makeY(28.f, 2);
    REQUIRE(yj.ok());
    float hubJr = 0.f;
    for (const auto& n : yj.value().nodes()) {
        if (std::fabs(n.x) < 1e-3f && std::fabs(n.z) < 1e-3f) hubJr = n.junctionRadius;
    }
    REQUIRE(hubJr > 5.f);

    RoadBakeOptions options;
    options.pathSegmentsPerEdge = 28;
    options.includeJunctions    = true;
    options.includeNavigation   = false;
    options.includePiers        = false;
    auto baked                  = bakeRoadNetwork(yj.value(), options);
    REQUIRE(baked.ok());

    int curbGroup = -1, asphaltGroup = -1;
    for (int g = 0; g < baked.value().mesh.getGroupCount(); ++g) {
        const auto name = baked.value().mesh.getGroupName(g);
        if (name == "curb") curbGroup = g;
        if (name == "asphalt") asphaltGroup = g;
    }
    REQUIRE(curbGroup >= 0);
    REQUIRE(asphaltGroup >= 0);
    auto curb    = baked.value().mesh.copyGroup(curbGroup);
    auto asphalt = baked.value().mesh.copyGroup(asphaltGroup);
    REQUIRE(curb);
    REQUIRE(asphalt);

    // Expected dock ring: for 120° Y with jr=ah+2.8, tangency sits near ~0.58*jr.
    const float dockR = hubJr * 0.55f;
    const float band0 = dockR - 0.45f;
    const float band1 = dockR + 0.45f;
    int         curbDockHits = 0;
    for (int i = 0; i < curb->getVertexCount(); ++i) {
        if (std::fabs(curb->getPositionY(i)) > 1.0f) continue;
        const float x = curb->getPositionX(i);
        const float z = curb->getPositionZ(i);
        const float r = std::sqrt(x * x + z * z);
        if (r > band0 && r < band1) ++curbDockHits;
    }
    CHECK_GT(curbDockHits, 24);

    // No large empty annulus between arm tips and fillet: sample mid-angle rays
    // and require asphalt coverage near the dock radius (where the tip meets the arc).
    auto covers = [&](float x, float z) {
        for (int t = 0; t < asphalt->getIndexCount() / 3; ++t) {
            const int i0 = asphalt->getIndex(t * 3 + 0);
            const int i1 = asphalt->getIndex(t * 3 + 1);
            const int i2 = asphalt->getIndex(t * 3 + 2);
            const float x0 = asphalt->getPositionX(i0), z0 = asphalt->getPositionZ(i0);
            const float x1 = asphalt->getPositionX(i1), z1 = asphalt->getPositionZ(i1);
            const float x2 = asphalt->getPositionX(i2), z2 = asphalt->getPositionZ(i2);
            const float den = (z1 - z2) * (x0 - x2) + (x2 - x1) * (z0 - z2);
            if (std::fabs(den) < 1e-8f) continue;
            const float a = ((z1 - z2) * (x - x2) + (x2 - x1) * (z - z2)) / den;
            const float b = ((z2 - z0) * (x - x2) + (x0 - x2) * (z - z2)) / den;
            const float c = 1.f - a - b;
            if (a >= -1e-3f && b >= -1e-3f && c >= -1e-3f) return true;
        }
        return false;
    };
    // Probe along the three arm centerlines at the dock radius and slightly inside.
    const float dirs[3][2] = {{0.f, -1.f}, {0.8660254f, 0.5f}, {-0.8660254f, 0.5f}};
    for (const auto& d : dirs) {
        CHECK(covers(d[0] * dockR, d[1] * dockR));
        CHECK(covers(d[0] * (dockR - 0.35f), d[1] * (dockR - 0.35f)));
    }
    // Probe corner mid-angles (between arms) on the fillet asphalt ring.
    const float midAngles[3] = {-0.5235988f, 1.5707963f, 3.6651914f};  // approx mid of 120° gaps
    for (float ang : midAngles) {
        const float x = std::cos(ang) * dockR;
        const float z = std::sin(ang) * dockR;
        CHECK(covers(x, z));
    }
}

TEST_CASE("procgen.road.scenes.commonAnglesDock") {
    // Common corner gaps must bake a filleted (or chord) junction without empty mesh.
    const float gaps[] = {60.f, 90.f, 120.f, 135.f};
    RoadBakeOptions options;
    options.pathSegmentsPerEdge = 24;
    options.includeJunctions    = true;
    options.includeNavigation   = false;
    options.includePiers        = false;
    options.includeMarkings     = false;

    for (float gap : gaps) {
        // Three arms: 0°, gap, 180°+gap/2 — exercises one controlled acute/obtuse corner.
        const float third = 180.f + gap * 0.5f;
        auto        net   = RoadNetwork::makeFan(28.f, 2, {0.f, gap, third});
        REQUIRE(net.ok());
        CHECK_EQ(net.value().edgeCount(), 3);
        auto baked = bakeRoadNetwork(net.value(), options);
        REQUIRE(baked.ok());
        CHECK_GT(baked.value().mesh.getVertexCount(), 200);

        int curbGroup = -1, asphaltGroup = -1;
        for (int g = 0; g < baked.value().mesh.getGroupCount(); ++g) {
            const auto name = baked.value().mesh.getGroupName(g);
            if (name == "curb") curbGroup = g;
            if (name == "asphalt") asphaltGroup = g;
        }
        CHECK(curbGroup >= 0);
        CHECK(asphaltGroup >= 0);
        auto asphalt = baked.value().mesh.copyGroup(asphaltGroup);
        REQUIRE(asphalt);
        // Hub must stay covered for every common gap.
        bool hubCovered = false;
        for (int t = 0; t < asphalt->getIndexCount() / 3; ++t) {
            const int i0 = asphalt->getIndex(t * 3 + 0);
            const int i1 = asphalt->getIndex(t * 3 + 1);
            const int i2 = asphalt->getIndex(t * 3 + 2);
            const float x0 = asphalt->getPositionX(i0), z0 = asphalt->getPositionZ(i0);
            const float x1 = asphalt->getPositionX(i1), z1 = asphalt->getPositionZ(i1);
            const float x2 = asphalt->getPositionX(i2), z2 = asphalt->getPositionZ(i2);
            const float den = (z1 - z2) * (x0 - x2) + (x2 - x1) * (z0 - z2);
            if (std::fabs(den) < 1e-8f) continue;
            const float a = ((z1 - z2) * (0.f - x2) + (x2 - x1) * (0.f - z2)) / den;
            const float b = ((z2 - z0) * (0.f - x2) + (x0 - x2) * (0.f - z2)) / den;
            const float c = 1.f - a - b;
            if (a >= -1e-3f && b >= -1e-3f && c >= -1e-3f) {
                hubCovered = true;
                break;
            }
        }
        CHECK(hubCovered);
    }

    auto fork = RoadNetwork::makeFork(28.f, 2);
    REQUIRE(fork.ok());
    auto bakedFork = bakeRoadNetwork(fork.value(), options);
    REQUIRE(bakedFork.ok());

    auto skew = RoadNetwork::makeSkew(28.f, 2);
    REQUIRE(skew.ok());
    auto bakedSkew = bakeRoadNetwork(skew.value(), options);
    REQUIRE(bakedSkew.ok());
}

TEST_CASE("procgen.road.scenes.acuteBendToDock") {
    // 28° is below the reliable fillet range; bend-to-dock must still produce a
    // continuous asphalt apron (plan.ok via nudged outDirs).
    auto net = RoadNetwork::makeFan(28.f, 2, {0.f, 28.f, 200.f});
    REQUIRE(net.ok());
    RoadBakeOptions options;
    options.pathSegmentsPerEdge = 28;
    options.includeJunctions    = true;
    options.includeNavigation   = false;
    options.includePiers        = false;
    options.includeMarkings     = false;
    auto baked                  = bakeRoadNetwork(net.value(), options);
    REQUIRE(baked.ok());
    CHECK_GT(baked.value().mesh.getVertexCount(), 200);

    int asphaltGroup = -1;
    for (int g = 0; g < baked.value().mesh.getGroupCount(); ++g) {
        if (baked.value().mesh.getGroupName(g) == "asphalt") asphaltGroup = g;
    }
    REQUIRE(asphaltGroup >= 0);
    auto asphalt = baked.value().mesh.copyGroup(asphaltGroup);
    REQUIRE(asphalt);
    auto covers = [&](float x, float z) {
        for (int t = 0; t < asphalt->getIndexCount() / 3; ++t) {
            const int i0 = asphalt->getIndex(t * 3 + 0);
            const int i1 = asphalt->getIndex(t * 3 + 1);
            const int i2 = asphalt->getIndex(t * 3 + 2);
            const float x0 = asphalt->getPositionX(i0), z0 = asphalt->getPositionZ(i0);
            const float x1 = asphalt->getPositionX(i1), z1 = asphalt->getPositionZ(i1);
            const float x2 = asphalt->getPositionX(i2), z2 = asphalt->getPositionZ(i2);
            const float den = (z1 - z2) * (x0 - x2) + (x2 - x1) * (z0 - z2);
            if (std::fabs(den) < 1e-8f) continue;
            const float a = ((z1 - z2) * (x - x2) + (x2 - x1) * (z - z2)) / den;
            const float b = ((z2 - z0) * (x - x2) + (x0 - x2) * (z - z2)) / den;
            const float c = 1.f - a - b;
            if (a >= -1e-3f && b >= -1e-3f && c >= -1e-3f) return true;
        }
        return false;
    };
    CHECK(covers(0.f, 0.f));
    // Mid-ray of the acute gap should hit fillet asphalt after bend.
    const float mid = 14.f * 0.01745329252f;
    CHECK(covers(std::cos(mid) * 3.2f, std::sin(mid) * 3.2f));
}

TEST_CASE("procgen.road.bidirectional.markingsAndNav") {
    RoadNetwork network;
    auto        a = network.addNode(-18.f, 0.f, 0.f, 2.f);
    auto        b = network.addNode(18.f, 0.f, 0.f, 2.f);
    REQUIRE(a.ok());
    REQUIRE(b.ok());
    RoadStyle style;
    style.deckThickness = 0.2f;
    style.pierClearance = 100.f;
    auto edge = network.addEdge(a.value(), b.value(),
                                {RoadControlPoint{-18.f, 0.f, 0.f}, RoadControlPoint{0.f, 0.f, 0.f},
                                 RoadControlPoint{18.f, 0.f, 0.f}},
                                2, 2, style);
    REQUIRE(edge.ok());

    RoadBakeOptions options;
    options.pathSegmentsPerEdge = 24;
    options.includeJunctions    = false;
    options.includeNavigation   = true;
    options.includePiers        = false;
    options.includeMarkings     = true;
    auto baked                  = bakeRoadNetwork(network, options);
    REQUIRE(baked.ok());
    CHECK_EQ(baked.value().overlay.lanes.size(), 4u);  // 2 forward + 2 reverse

    bool sawYellow = false;
    for (int i = 0; i < baked.value().mesh.getGroupCount(); ++i) {
        if (baked.value().mesh.getGroupName(i) == "markingYellow") sawYellow = true;
    }
    CHECK(sawYellow);
}

TEST_CASE("procgen.road.decor.builtinsAndCustom") {
    auto straight = RoadNetwork::makeStraight(36.f, 2);
    REQUIRE(straight.ok());

    RoadBakeOptions options;
    options.pathSegmentsPerEdge   = 24;
    options.includeJunctions      = false;
    options.includeNavigation     = false;
    options.includePiers          = false;
    options.includeMarkings       = false;
    options.decor.trees           = true;
    options.decor.greenbelt       = true;
    options.decor.streetLights    = true;
    options.decor.utilityPoles    = true;
    options.decor.treeSpacing     = 6.f;
    options.decor.lightSpacing    = 12.f;
    options.decor.poleSpacing     = 15.f;
    options.decor.seed            = 7;

    RoadDecorSpec bench;
    bench.id         = "bench";
    bench.spacing    = 10.f;
    bench.roadside   = true;
    bench.bothSides  = false;
    bench.lateralGap = 1.4f;
    RoadDecorPrimitive seat;
    seat.shape = RoadDecorPrimitive::Shape::Box;
    seat.oy    = 0.35f;
    seat.sx    = 0.55f;
    seat.sy    = 0.08f;
    seat.sz    = 0.22f;
    seat.group = "decorCustom";
    RoadDecorPrimitive back;
    back.shape = RoadDecorPrimitive::Shape::Box;
    back.oz    = -0.18f;
    back.oy    = 0.55f;
    back.sx    = 0.55f;
    back.sy    = 0.28f;
    back.sz    = 0.05f;
    back.group = "decorCustom";
    bench.parts = {seat, back};
    REQUIRE(addCustomRoadDecor(options.decor, bench).ok());
    CHECK(!addCustomRoadDecor(options.decor, bench).ok());  // duplicate id

    auto baked = bakeRoadNetwork(straight.value(), options);
    REQUIRE(baked.ok());

    bool sawTrunk = false, sawFoliage = false, sawGrass = false, sawMetal = false, sawPole = false,
         sawCustom = false;
    for (int g = 0; g < baked.value().mesh.getGroupCount(); ++g) {
        const auto name = baked.value().mesh.getGroupName(g);
        if (name == "decorTrunk") sawTrunk = true;
        if (name == "decorFoliage") sawFoliage = true;
        if (name == "decorGrass") sawGrass = true;
        if (name == "decorMetal") sawMetal = true;
        if (name == "decorPole") sawPole = true;
        if (name == "decorCustom") sawCustom = true;
    }
    CHECK(sawTrunk);
    CHECK(sawFoliage);
    CHECK(sawGrass);
    CHECK(sawMetal);
    CHECK(sawPole);
    CHECK(sawCustom);
}

TEST_CASE("procgen.road.decor.medianOnBidirectional") {
    RoadNetwork network;
    auto        a = network.addNode(-20.f, 0.f, 0.f, 2.f);
    auto        b = network.addNode(20.f, 0.f, 0.f, 2.f);
    REQUIRE(a.ok());
    REQUIRE(b.ok());
    RoadStyle style;
    style.deckThickness = 0.15f;
    style.pierClearance = 100.f;
    auto edge = network.addEdge(a.value(), b.value(),
                                {RoadControlPoint{-20.f, 0.f, 0.f}, RoadControlPoint{0.f, 0.f, 0.f},
                                 RoadControlPoint{20.f, 0.f, 0.f}},
                                2, 2, style);
    REQUIRE(edge.ok());

    RoadBakeOptions options;
    options.pathSegmentsPerEdge = 20;
    options.includeJunctions    = false;
    options.includeNavigation   = false;
    options.includePiers        = false;
    options.includeMarkings     = false;
    options.decor.medianStrip   = true;
    options.decor.medianWidth   = 1.2f;
    auto baked                  = bakeRoadNetwork(network, options);
    REQUIRE(baked.ok());
    bool sawMedian = false;
    for (int g = 0; g < baked.value().mesh.getGroupCount(); ++g) {
        if (baked.value().mesh.getGroupName(g) == "decorMedian") sawMedian = true;
    }
    CHECK(sawMedian);
}

TEST_CASE("procgen.road.scenes.yJunctionLaneLinks") {
    // Classic Y topology: one north inbound arm, two outbound SE/SW exits.
    // Alternating fan directions used to drop the north→southeast turn.
    auto yj = RoadNetwork::makeY(28.f, 2);
    REQUIRE(yj.ok());
    CHECK_EQ(yj.value().edgeCount(), 3);

    std::uint32_t hubId = 0;
    std::uint32_t northIn = 0, seOut = 0, swOut = 0;
    int           inbound = 0, outbound = 0;
    for (const auto& n : yj.value().nodes()) {
        if (std::fabs(n.x) < 1e-3f && std::fabs(n.z) < 1e-3f) hubId = n.id;
    }
    REQUIRE(hubId != 0);
    for (const auto& e : yj.value().edges()) {
        if (e.to == hubId) {
            ++inbound;
            northIn = e.id;
        }
        if (e.from == hubId) {
            ++outbound;
            auto leaf = yj.value().nodeResult(e.to);
            REQUIRE(leaf.ok());
            if (leaf.value().x > 0.f) seOut = e.id;
            if (leaf.value().x < 0.f) swOut = e.id;
        }
    }
    CHECK_EQ(inbound, 1);
    CHECK_EQ(outbound, 2);
    REQUIRE(northIn != 0);
    REQUIRE(seOut != 0);
    REQUIRE(swOut != 0);

    // 1 inbound × 2 outbound × 2 forward lanes = 4 links covering both exits.
    CHECK_EQ(yj.value().laneLinkCount(), 4);
    bool northToSe = false, northToSw = false;
    for (const auto& link : yj.value().laneLinks()) {
        if (link.inEdge == northIn && link.outEdge == seOut) northToSe = true;
        if (link.inEdge == northIn && link.outEdge == swOut) northToSw = true;
    }
    CHECK(northToSe);
    CHECK(northToSw);
}

TEST_CASE("procgen.road.bidirectional.asymmetricLaneCenters") {
    RoadNetwork network;
    auto        a = network.addNode(-20.f, 0.f, 0.f, 2.f);
    auto        b = network.addNode(20.f, 0.f, 0.f, 2.f);
    REQUIRE(a.ok());
    REQUIRE(b.ok());
    RoadStyle style;
    style.laneWidth     = 3.5f;
    style.deckThickness = 0.2f;
    style.pierClearance = 100.f;
    auto edge = network.addEdge(a.value(), b.value(),
                                {RoadControlPoint{-20.f, 0.f, 0.f}, RoadControlPoint{20.f, 0.f, 0.f}}, 1, 2, style);
    REQUIRE(edge.ok());

    RoadBakeOptions options;
    options.pathSegmentsPerEdge = 16;
    options.includeJunctions    = false;
    options.includeNavigation   = true;
    options.includePiers        = false;
    options.includeMarkings     = true;
    auto baked                  = bakeRoadNetwork(network, options);
    REQUIRE(baked.ok());
    REQUIRE_EQ(baked.value().overlay.lanes.size(), 3u);

    // asphaltHalf = 5.25; boundary = -5.25 + 7 = 1.75
    // forward lane0 center = 3.5; reverse lane0 = -3.5 (edge along +X → lateral in +Z)
    float fwdLat = 0.f, revLat = 0.f;
    bool  foundFwd = false, foundRev = false;
    for (const auto& poly : baked.value().overlay.lanes) {
        for (std::size_t i = 0; i + 2 < poly.xyz.size(); i += 3) {
            const float x = poly.xyz[i];
            const float z = poly.xyz[i + 2];
            if (std::fabs(x) > 2.f) continue;
            if (!foundFwd && z > 1.f) {
                fwdLat   = z;
                foundFwd = true;
            }
            if (!foundRev && z < -1.f) {
                revLat   = z;
                foundRev = true;
            }
        }
    }
    CHECK(foundFwd);
    CHECK(foundRev);
    CHECK(std::fabs(fwdLat - 3.5f) < 0.25f);
    CHECK(std::fabs(revLat + 3.5f) < 0.25f);
}

TEST_CASE("procgen.road.decor.rejectsInvalidSpecsAndSpacing") {
    RoadDecorOptions options;
    RoadDecorSpec    bad;
    bad.id      = "bad";
    bad.spacing = -1.f;
    RoadDecorPrimitive part;
    part.shape = RoadDecorPrimitive::Shape::Box;
    bad.parts  = {part};
    CHECK(!addCustomRoadDecor(options, bad).ok());

    bad.spacing = 4.f;
    bad.startOffset = -2.f;
    CHECK(!addCustomRoadDecor(options, bad).ok());

    bad.startOffset = 1.f;
    part.ox         = std::numeric_limits<float>::quiet_NaN();
    bad.parts       = {part};
    CHECK(!addCustomRoadDecor(options, bad).ok());

    // Built-in negative spacing must fail the bake, not spin for thousands of instances.
    auto straight = RoadNetwork::makeStraight(40.f, 2);
    REQUIRE(straight.ok());
    RoadBakeOptions bakeOpts;
    bakeOpts.includeJunctions  = false;
    bakeOpts.includeNavigation = false;
    bakeOpts.includePiers      = false;
    bakeOpts.includeMarkings   = false;
    bakeOpts.decor.trees       = true;
    bakeOpts.decor.treeSpacing = -5.f;
    CHECK(!bakeRoadNetwork(straight.value(), bakeOpts).ok());
}

TEST_CASE("procgen.road.decor.recipeMedianAndSchemaWidths") {
    MeshRecipeRegistry::instance().registerBuiltins();
    REQUIRE(MeshRecipeRegistry::instance().has("mesh.roadNetwork"));
    const auto* schema = MeshRecipeRegistry::instance().descriptor("mesh.roadNetwork");
    REQUIRE(schema != nullptr);
    bool sawGreenbeltW = false, sawMedianW = false, sawLanesBack = false;
    for (const auto& p : schema->params) {
        if (p.key == "decorGreenbeltWidth") sawGreenbeltW = true;
        if (p.key == "decorMedianWidth") sawMedianW = true;
        if (p.key == "lanesBackward") sawLanesBack = true;
    }
    CHECK(sawGreenbeltW);
    CHECK(sawMedianW);
    CHECK(sawLanesBack);

    Params params;
    params.setString("scene", "straight");
    params.setFloat("span", 36.f);
    params.setInt("lanes", 2);
    params.setBool("decorMedian", true);
    params.setBool("piers", false);
    params.setBool("markings", false);
    params.setBool("navigation", false);
    params.setBool("junctions", false);
    MeshBuild mesh;
    std::string error;
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.roadNetwork", params, mesh, error));
    bool sawMedian = false;
    for (int g = 0; g < mesh.getGroupCount(); ++g) {
        if (mesh.getGroupName(g) == "decorMedian") sawMedian = true;
    }
    CHECK(sawMedian);
}

TEST_CASE("procgen.road.decor.boxWindingMatchesNormal") {
    // Bake a short median box and verify each triangle's geometric normal agrees
    // with the stored vertex normal (outward-facing winding).
    RoadNetwork network;
    auto        a = network.addNode(-12.f, 0.f, 0.f, 2.f);
    auto        b = network.addNode(12.f, 0.f, 0.f, 2.f);
    REQUIRE(a.ok());
    REQUIRE(b.ok());
    RoadStyle style;
    style.deckThickness = 0.15f;
    style.pierClearance = 100.f;
    auto edge = network.addEdge(a.value(), b.value(),
                                {RoadControlPoint{-12.f, 0.f, 0.f}, RoadControlPoint{12.f, 0.f, 0.f}}, 2, 2, style);
    REQUIRE(edge.ok());
    RoadBakeOptions options;
    options.pathSegmentsPerEdge = 12;
    options.includeJunctions    = false;
    options.includeNavigation   = false;
    options.includePiers        = false;
    options.includeMarkings     = false;
    options.decor.medianStrip   = true;
    options.decor.medianWidth   = 1.0f;
    auto baked                  = bakeRoadNetwork(network, options);
    REQUIRE(baked.ok());
    int medianGroup = -1;
    for (int g = 0; g < baked.value().mesh.getGroupCount(); ++g) {
        if (baked.value().mesh.getGroupName(g) == "decorMedian") medianGroup = g;
    }
    REQUIRE(medianGroup >= 0);
    auto median = baked.value().mesh.copyGroup(medianGroup);
    REQUIRE(median);
    int checked = 0;
    for (int t = 0; t < median->getIndexCount() / 3; ++t) {
        const int i0 = median->getIndex(t * 3 + 0);
        const int i1 = median->getIndex(t * 3 + 1);
        const int i2 = median->getIndex(t * 3 + 2);
        const float ax = median->getPositionX(i0), ay = median->getPositionY(i0), az = median->getPositionZ(i0);
        const float bx = median->getPositionX(i1), by = median->getPositionY(i1), bz = median->getPositionZ(i1);
        const float cx = median->getPositionX(i2), cy = median->getPositionY(i2), cz = median->getPositionZ(i2);
        const float ex = bx - ax, ey = by - ay, ez = bz - az;
        const float fx = cx - ax, fy = cy - ay, fz = cz - az;
        const float gx = ey * fz - ez * fy;
        const float gy = ez * fx - ex * fz;
        const float gz = ex * fy - ey * fx;
        const float nx = median->getNormalX(i0);
        const float ny = median->getNormalY(i0);
        const float nz = median->getNormalZ(i0);
        const float dot = gx * nx + gy * ny + gz * nz;
        CHECK(dot > 0.f);
        ++checked;
    }
    CHECK_GT(checked, 8);
}

