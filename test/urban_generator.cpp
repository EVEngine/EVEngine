#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "procgen/GeneratorRegistry.h"
#include "procgen/Grid2D.h"
#include "procgen/MeshBuild.h"
#include "procgen/Params.h"
#include "procgen/Semantic.h"
#include "procgen/algorithms/MarchingCubes.h"
#include "procgen/urban/UrbanGenerator.h"
#include "procgen/urban/UrbanGeometry.h"
#include "procgen/urban/UrbanOutput.h"
#include "procgen/urban/UrbanTypes.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <set>
#include <string>
#include <vector>

using namespace eve::procgen;
using namespace eve::procgen::urban;

namespace {

Params makeParams(uint32_t seed, const std::string& land, int targetParcels = 120) {
    Params p;
    p.setSeed(seed);
    p.setString("land", land);
    p.setFloat("landWidth", 100.f);
    p.setFloat("landHeight", 60.f);
    p.setFloat("minParcelArea", 4.f);
    p.setInt("targetParcels", targetParcels);
    return p;
}

bool layoutAllParcelsReachable(const UrbanLayout& l) {
    std::set<std::pair<int, int>> street;
    for (const auto& e : l.streetSegments) {
        const int a = e.first, b = e.second;
        street.insert({std::min(a, b), std::max(a, b)});
    }
    for (const Parcel& parcel : l.parcels) {
        const size_t m         = parcel.ring.size();
        bool         reachable = false;
        for (size_t i = 0; i < m; ++i) {
            const int a = parcel.ring[i];
            const int b = parcel.ring[(i + 1) % m];
            if (street.count({std::min(a, b), std::max(a, b)})) {
                reachable = true;
                break;
            }
        }
        if (!reachable) return false;
    }
    return true;
}

bool streetNetworkConnected(const UrbanLayout& l) {
    if (l.streetSegments.empty()) return false;
    std::vector<std::vector<int>> adj(l.corners.size());
    for (const auto& e : l.streetSegments) {
        adj[size_t(e.first)].push_back(e.second);
        adj[size_t(e.second)].push_back(e.first);
    }
    int               start = l.streetSegments[0].first;
    std::vector<char> seen(l.corners.size(), 0);
    std::vector<int>  q{start};
    seen[size_t(start)] = 1;
    int reached         = 0;
    for (size_t qi = 0; qi < q.size(); ++qi) {
        const int v = q[qi];
        ++reached;
        for (const int u : adj[size_t(v)]) {
            if (!seen[size_t(u)]) {
                seen[size_t(u)] = 1;
                q.push_back(u);
            }
        }
    }
    for (const auto& e : l.streetSegments) {
        if (!seen[size_t(e.first)] || !seen[size_t(e.second)]) return false;
    }
    return reached >= 2;
}

double layoutCoveredArea(const UrbanLayout& l) {
    double sum = 0.0;
    for (const Parcel& parcel : l.parcels) {
        Polygon poly;
        for (const int c : parcel.ring) poly.push_back(l.corners[size_t(c)]);
        sum += area(poly);
    }
    return sum;
}

int countQuads(const UrbanLayout& l) {
    int n = 0;
    for (const Parcel& parcel : l.parcels) {
        Polygon poly;
        for (const int c : parcel.ring) poly.push_back(l.corners[size_t(c)]);
        if (approximatePolygon(poly).size() == 4) ++n;
    }
    return n;
}

bool meshValid(const MeshBuild& m) {
    if (m.getVertexCount() <= 0 || m.getIndexCount() <= 0) return false;
    for (int i = 0; i < m.getIndexCount(); ++i) {
        if (m.getIndex(i) < 0 || m.getIndex(i) >= m.getVertexCount()) return false;
    }
    for (int i = 0; i < m.getVertexCount(); ++i) {
        if (!std::isfinite(m.getPositionX(i)) || !std::isfinite(m.getPositionY(i)) || !std::isfinite(m.getPositionZ(i)))
            return false;
    }
    return true;
}

}  // namespace

TEST_CASE("urban.geometry.irregularityMetric") {
    const Polygon square  = {{0, 0}, {10, 0}, {10, 10}, {0, 10}};
    const Polygon rect    = {{0, 0}, {20, 0}, {20, 5}, {0, 5}};
    const Polygon tri     = {{0, 0}, {10, 0}, {0, 10}};
    const double  iSquare = shapeIrregularity(square);
    const double  iRect   = shapeIrregularity(rect);
    const double  iTri    = shapeIrregularity(tri);
    CHECK(iSquare < 1e-6);
    CHECK(iRect > iSquare);
    CHECK(iTri > iRect);
}

TEST_CASE("urban.geometry.splitRect") {
    const Polygon    rect  = {{0, 0}, {20, 0}, {20, 10}, {0, 10}};
    const Polyline   split = {{10, 0}, {10, 10}};
    BoundaryPosition pa, pb;
    closestPointOnBoundary(rect, split.front(), &pa);
    closestPointOnBoundary(rect, split.back(), &pb);
    Polygon a, b;
    REQUIRE(splitPolygonByPolyline(rect, split, pa, pb, a, b));
    CHECK(std::fabs(area(a) + area(b) - area(rect)) < 1e-6);
    CHECK(area(a) > 0.0);
    CHECK(area(b) > 0.0);
    CHECK(polygonIsSimple(a));
    CHECK(polygonIsSimple(b));
}

TEST_CASE("urban.generator.rect.reachableAndRegular") {
    UrbanOptions opts;
    opts.land          = {{0, 0}, {100, 0}, {100, 60}, {0, 60}};
    opts.seed          = 42;
    opts.targetParcels = 120;
    UrbanGenerator gen(opts);
    std::string    err;
    REQUIRE(gen.generate(&err));
    const UrbanLayout& l = gen.layout();
    REQUIRE(l.parcels.size() >= 32);
    REQUIRE(layoutAllParcelsReachable(l));
    REQUIRE(streetNetworkConnected(l));
    REQUIRE(std::fabs(layoutCoveredArea(l) - area(opts.land)) < 0.03 * area(opts.land));
    REQUIRE(double(countQuads(l)) / double(l.parcels.size()) > 0.5);
    CHECK(l.avgIrregularity < 0.4);
    CHECK(l.streetJunctions >= 0);
    CHECK(l.totalStreetLength > 0.0);
}

TEST_CASE("urban.generator.deterministic") {
    UrbanOptions a;
    a.land           = {{0, 0}, {100, 0}, {100, 60}, {0, 60}};
    a.seed           = 7;
    a.targetParcels  = 64;
    UrbanOptions   b = a;
    UrbanGenerator g1(a), g2(b);
    std::string    err;
    REQUIRE(g1.generate(&err));
    REQUIRE(g2.generate(&err));
    const UrbanLayout& l1 = g1.layout();
    const UrbanLayout& l2 = g2.layout();
    CHECK_EQ(l1.parcels.size(), l2.parcels.size());
    CHECK_EQ(l1.streets.size(), l2.streets.size());
    REQUIRE(l1.corners.size() == l2.corners.size());
    for (size_t i = 0; i < l1.corners.size(); ++i) CHECK(l1.corners[i] == l2.corners[i]);
}

TEST_CASE("urban.generator.streetPatterns") {
    auto run = [&](int pattern, int boundaryMode) {
        UrbanOptions opts;
        opts.land               = {{0, 0}, {100, 0}, {100, 60}, {0, 60}};
        opts.seed               = 11;
        opts.targetParcels      = 64;
        opts.streetPattern      = pattern;
        opts.boundaryStreetMode = boundaryMode;
        UrbanGenerator gen(opts);
        std::string    err;
        REQUIRE(gen.generate(&err));
        return gen.layout();
    };
    const UrbanLayout loop = run(1, 0);
    const UrbanLayout tree = run(3, 1);
    CHECK_EQ(loop.streetEnds, 0);
    CHECK(tree.streetEnds >= 1);
    CHECK(layoutAllParcelsReachable(tree));
    CHECK(streetNetworkConnected(tree));
}

TEST_CASE("urban.generator.irregularLand") {
    const std::vector<Polygon> lands = {
        {{0, 0}, {100, 0}, {50, 60}},                                // triangle
        {{0, 0}, {100, 0}, {100, 33}, {45, 33}, {45, 60}, {0, 60}},  // L
    };
    for (const Polygon& land : lands) {
        UrbanOptions opts;
        opts.land          = land;
        opts.seed          = 3;
        opts.targetParcels = 80;
        UrbanGenerator gen(opts);
        std::string    err;
        REQUIRE(gen.generate(&err));
        const UrbanLayout& l = gen.layout();
        CHECK(l.parcels.size() >= 16);
        CHECK(layoutAllParcelsReachable(l));
        CHECK(streetNetworkConnected(l));
    }
}

TEST_CASE("urban.generator.weightsChangeLayout") {
    auto run = [&](float lr, float la) {
        UrbanOptions opts;
        opts.land          = {{0, 0}, {100, 0}, {100, 60}, {0, 60}};
        opts.seed          = 9;
        opts.targetParcels = 64;
        opts.lambdaRegu    = lr;
        opts.lambdaAcce    = la;
        UrbanGenerator gen(opts);
        std::string    err;
        REQUIRE(gen.generate(&err));
        return gen.layout();
    };
    const UrbanLayout regular = run(0.9f, 0.05f);
    const UrbanLayout access  = run(0.1f, 0.9f);
    CHECK(regular.corners != access.corners);
}

TEST_CASE("urban.grid.rasterAndSemantics") {
    Params      p = makeParams(42, "rect", 64);
    Grid2D      grid;
    std::string err;
    REQUIRE(generateUrbanGrid(p, grid, err));
    CHECK(grid.getWidth() > 10);
    CHECK(grid.getHeight() > 10);
    int           floor = 0, road = 0, wall = 0;
    std::set<int> details;
    for (int y = 0; y < grid.getHeight(); ++y) {
        for (int x = 0; x < grid.getWidth(); ++x) {
            const int c = grid.getCell(x, y);
            if (c == int(Semantic::Floor))
                ++floor;
            else if (c == int(Semantic::Road))
                ++road;
            else if (c == int(Semantic::Wall))
                ++wall;
            if (grid.getDetail(x, y) > 0) details.insert(grid.getDetail(x, y));
        }
    }
    CHECK(floor > 200);
    CHECK(road > 10);
    CHECK(wall > 0);
    CHECK(details.size() >= 32);
    CHECK(grid.getObjectCount() >= 32);
    CHECK_EQ(grid.getMeta("algorithm", ""), "urban.parcels");

    // Determinism through the public registry.
    Grid2D g2;
    REQUIRE(generateUrbanGrid(p, g2, err));
    CHECK(g2.cells() == grid.cells());
    GeneratorRegistry::instance().registerBuiltins();
    CHECK(GeneratorRegistry::instance().has("urban.parcels"));
}

TEST_CASE("urban.mesh.flatAndExtruded") {
    Params      p = makeParams(42, "rect", 48);
    MeshBuild   flat;
    std::string err;
    REQUIRE(generateUrbanMesh(p, flat, err));
    CHECK(meshValid(flat));
    CHECK(flat.getVertexCount() > 100);
    CHECK(flat.getIndexCount() % 3 == 0);
    CHECK_EQ(flat.getMeta("algorithm", ""), "mesh.urban");

    p.setFloat("extrude", 5.f);
    MeshBuild extruded;
    REQUIRE(generateUrbanMesh(p, extruded, err));
    CHECK(meshValid(extruded));
    CHECK(extruded.getVertexCount() > flat.getVertexCount());

    MeshBuild again;
    p.setFloat("extrude", 0.f);
    REQUIRE(generateUrbanMesh(p, again, err));
    CHECK(again.positions() == flat.positions());
    CHECK(again.indices() == flat.indices());
    MeshRecipeRegistry::instance().registerBuiltins();
    CHECK(MeshRecipeRegistry::instance().has("mesh.urban"));
}

TEST_CASE("urban.generator.orientation") {
    auto run = [&](int orientation) {
        UrbanOptions opts;
        opts.land          = {{0, 0}, {100, 0}, {100, 60}, {0, 60}};
        opts.seed          = 5;
        opts.targetParcels = 48;
        opts.orientation   = orientation;
        opts.lambdaOrient  = 1.0;
        UrbanGenerator gen(opts);
        std::string    err;
        REQUIRE(gen.generate(&err));
        return gen.layout();
    };
    const UrbanLayout ew = run(1);
    const UrbanLayout ns = run(2);
    CHECK(ew.corners != ns.corners);
    CHECK(layoutAllParcelsReachable(ew));
    CHECK(layoutAllParcelsReachable(ns));
}

TEST_CASE("urban.generator.optimizeCanBeDisabled") {
    UrbanOptions on;
    on.land          = {{0, 0}, {100, 0}, {100, 60}, {0, 60}};
    on.seed          = 13;
    on.targetParcels = 64;
    on.optimize      = true;
    UrbanOptions off = on;
    off.optimize     = false;
    UrbanGenerator g1(on), g2(off);
    std::string    err;
    REQUIRE(g1.generate(&err));
    REQUIRE(g2.generate(&err));
    CHECK(layoutAllParcelsReachable(g1.layout()));
    CHECK(layoutAllParcelsReachable(g2.layout()));
    CHECK(streetNetworkConnected(g1.layout()));
}

TEST_CASE("urban.grid.badParams") {
    Params p = makeParams(1, "rect");
    p.setFloat("minParcelArea", 0.f);
    Grid2D      grid;
    std::string err;
    CHECK(!generateUrbanGrid(p, grid, err));
    CHECK(!err.empty());
}
