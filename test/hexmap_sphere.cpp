#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "hexmap/HexSphereGenerator.h"
#include "hexmap/HexSphereMap.h"
#include "hexmap/HexSphereMesh.h"
#include "hexmap/HexSphereTopology.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <utility>
#include <vector>

using namespace eve::hexmap;

namespace {

/** @brief Builds a topology the test depends on, failing the case when it cannot. */
[[nodiscard]] HexSphereTopology makeTopology(std::int32_t subdivision) {
    auto built = HexSphereTopology::build(subdivision);
    REQUIRE(built.ok());
    return built.value();
}

/** @brief Dot product, so the tests do not depend on module internals. */
[[nodiscard]] float dot(HexVec3 a, HexVec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

/** @brief Length of a vector. */
[[nodiscard]] float magnitude(HexVec3 a) { return std::sqrt(dot(a, a)); }

/** @brief Whether two directions denote the same point within float tolerance. */
[[nodiscard]] bool sameDirection(HexVec3 a, HexVec3 b) { return magnitude(a - b) < 1e-5f; }

/**
 * @brief A deterministic, low-discrepancy sample of the sphere.
 *
 * A golden-angle spiral is used instead of a PRNG so a failure always reproduces
 * and the samples do not clump at the poles, where a naive uniform latitude
 * sampling would over-weight the twelve pentagons.
 */
[[nodiscard]] HexVec3 sampleDirection(std::uint32_t index, std::uint32_t count) {
    constexpr float kGoldenAngle = 2.399963229728653f;
    const float     fraction     = (static_cast<float>(index) + 0.5f) / static_cast<float>(count);
    const float     y            = 1.f - 2.f * fraction;
    const float     radius       = std::sqrt(std::max(0.f, 1.f - y * y));
    const float     angle        = kGoldenAngle * static_cast<float>(index);
    return HexVec3{radius * std::cos(angle), y, radius * std::sin(angle)};
}

}  // namespace

TEST_CASE("hexmap.sphere.countsMatchTheGoldbergFormula") {
    for (std::int32_t subdivision = 0; subdivision <= 4; ++subdivision) {
        const HexSphereTopology topology  = makeTopology(subdivision);
        const std::int32_t     frequency  = std::int32_t{1} << subdivision;
        const std::int32_t     cellCount  = 10 * frequency * frequency + 2;

        CHECK_EQ(topology.subdivision(), subdivision);
        CHECK_EQ(topology.frequency(), frequency);
        CHECK_EQ(topology.cellCount(), cellCount);
        CHECK_EQ(topology.pentagonCount(), 12);
        CHECK_EQ(topology.edgeCount(), 30 * frequency * frequency);
        CHECK_EQ(topology.cornerCount(), 20 * frequency * frequency);
        // Euler: a closed surface of genus zero.
        CHECK_EQ(topology.cellCount() - topology.edgeCount() + topology.cornerCount(), 2);
    }
}

TEST_CASE("hexmap.sphere.pentagonsAreTheTwelveIcosahedronVertices") {
    const HexSphereTopology topology = makeTopology(3);
    CHECK_EQ(topology.pentagonCount(), 12);

    for (HexSphereCell cell = 0; cell < topology.cellCount(); ++cell) {
        const bool pentagon = cell < 12;
        CHECK_EQ(topology.isPentagon(cell), pentagon);
        CHECK_EQ(topology.neighborCount(cell), pentagon ? 5 : 6);
        CHECK_EQ(topology.cornerCountOf(cell), topology.neighborCount(cell));
    }

    // Ids are stable across levels, so the twelve pentagons are the same cells at
    // every subdivision and their directions are the icosahedron's vertices.
    const HexSphereTopology coarse = makeTopology(0);
    const HexSphereTopology fine   = makeTopology(4);
    for (HexSphereCell cell = 0; cell < 12; ++cell) {
        CHECK(sameDirection(coarse.direction(cell), fine.direction(cell)));
        CHECK(fine.isPentagon(cell));
    }
}

TEST_CASE("hexmap.sphere.neighboursAreMutualAndShareBothCorners") {
    const HexSphereTopology topology = makeTopology(3);

    for (HexSphereCell cell = 0; cell < topology.cellCount(); ++cell) {
        const std::int32_t degree = topology.neighborCount(cell);
        // Kept out of the assertion expression: zeroerr decomposes `a || b` and
        // would collide with the built-in boolean operator.
        const bool degreeIsHexOrPentagon = degree == 5 || degree == 6;
        REQUIRE(degreeIsHexOrPentagon);

        for (std::int32_t direction = 0; direction < degree; ++direction) {
            const HexSphereCell other = topology.neighbor(cell, direction);
            REQUIRE(other != kNoHexSphereCell);
            CHECK(topology.contains(other));

            const std::int32_t back = topology.directionOf(other, cell);
            REQUIRE(back >= 0);
            CHECK_EQ(back, topology.oppositeDirection(cell, direction));

            // The shared edge must expose the same two corners from both sides,
            // in reversed order; that is what lets a mesh builder walk a boundary
            // from either cell.
            const std::int32_t otherDegree = topology.neighborCount(other);
            const HexVec3      here        = topology.corner(cell, direction);
            const HexVec3      hereNext    = topology.corner(cell, (direction + 1) % degree);
            const HexVec3      there       = topology.corner(other, back);
            const HexVec3      thereNext   = topology.corner(other, (back + 1) % otherDegree);

            CHECK(sameDirection(here, thereNext));
            CHECK(sameDirection(hereNext, there));
            CHECK(!sameDirection(here, hereNext));

            // Adjacency is symmetric in the other direction too: `other` reports
            // `cell` exactly once.
            std::int32_t matches = 0;
            for (std::int32_t candidate = 0; candidate < otherDegree; ++candidate) {
                if (topology.neighbor(other, candidate) == cell) ++matches;
            }
            CHECK_EQ(matches, 1);
        }
    }
}

TEST_CASE("hexmap.sphere.directionsAndCornersAreUnitVectors") {
    const HexSphereTopology topology = makeTopology(3);

    for (HexSphereCell cell = 0; cell < topology.cellCount(); ++cell) {
        CHECK(std::fabs(magnitude(topology.direction(cell)) - 1.f) < 1e-5f);
        const std::int32_t degree = topology.neighborCount(cell);
        for (std::int32_t corner = 0; corner < degree; ++corner) {
            CHECK(std::fabs(magnitude(topology.corner(cell, corner)) - 1.f) < 1e-5f);
        }
    }
}

TEST_CASE("hexmap.sphere.everyCornerIsSharedByExactlyThreeCells") {
    const HexSphereTopology topology = makeTopology(2);

    std::vector<HexVec3>      groups;
    std::vector<std::int32_t> members;

    for (HexSphereCell cell = 0; cell < topology.cellCount(); ++cell) {
        const std::int32_t degree = topology.neighborCount(cell);
        for (std::int32_t corner = 0; corner < degree; ++corner) {
            const HexVec3 position = topology.corner(cell, corner);
            std::size_t   group    = groups.size();
            for (std::size_t candidate = 0; candidate < groups.size(); ++candidate) {
                if (sameDirection(groups[candidate], position)) {
                    group = candidate;
                    break;
                }
            }
            if (group == groups.size()) {
                groups.push_back(position);
                members.push_back(1);
            } else {
                ++members[group];
            }
        }
    }

    CHECK_EQ(static_cast<std::int32_t>(groups.size()), topology.cornerCount());
    for (const std::int32_t count : members) CHECK_EQ(count, 3);
}

TEST_CASE("hexmap.sphere.edgeLengthsAreNearUniform") {
    const HexSphereTopology topology = makeTopology(3);

    float shortest = 10.f;
    float longest  = 0.f;
    for (HexSphereCell cell = 0; cell < topology.cellCount(); ++cell) {
        const std::int32_t degree = topology.neighborCount(cell);
        for (std::int32_t direction = 0; direction < degree; ++direction) {
            const float length = HexSphereTopology::angularDistance(
                topology.direction(cell), topology.direction(topology.neighbor(cell, direction)));
            shortest = std::min(shortest, length);
            longest  = std::max(longest, length);
        }
    }

    CHECK(shortest > 0.f);
    // Hexagon-hexagon edges are slightly longer than the edges around a pentagon;
    // the ratio stays close to one because both are governed by the same level.
    CHECK(longest / shortest < 1.3f);
}

TEST_CASE("hexmap.sphere.cellAtMatchesBruteForceNearest") {
    const HexSphereTopology topology = makeTopology(3);

    constexpr std::uint32_t kSamples = 400;
    for (std::uint32_t index = 0; index < kSamples; ++index) {
        const HexVec3 sample = sampleDirection(index, kSamples);

        HexSphereCell best       = 0;
        float         bestScore  = -2.f;
        for (HexSphereCell cell = 0; cell < topology.cellCount(); ++cell) {
            const float score = dot(topology.direction(cell), sample);
            if (score > bestScore) {
                bestScore = score;
                best      = cell;
            }
        }

        const HexSphereCell found = topology.cellAt(sample);
        REQUIRE(found != kNoHexSphereCell);
        CHECK_EQ(found, best);

        // The reported cell must actually contain the sample: the sample is closer
        // to its own centre than to every neighbour's.
        for (std::int32_t direction = 0; direction < topology.neighborCount(found); ++direction) {
            CHECK(dot(topology.direction(found), sample) >
                  dot(topology.direction(topology.neighbor(found, direction)), sample));
        }
    }
}

TEST_CASE("hexmap.sphere.subdivisionOutOfRangeIsRejected") {
    auto tooLow = HexSphereTopology::build(-1);
    CHECK(!tooLow.ok());

    auto tooHigh = HexSphereTopology::build(kMaxHexSphereSubdivision + 1);
    CHECK(!tooHigh.ok());

    auto base = HexSphereTopology::build(0);
    REQUIRE(base.ok());
    CHECK_EQ(base.value().cellCount(), 12);
    CHECK_EQ(base.value().pentagonCount(), 12);
    CHECK_EQ(base.value().cornerCount(), 20);
    CHECK_EQ(base.value().edgeCount(), 30);
    for (HexSphereCell cell = 0; cell < 12; ++cell) {
        CHECK(base.value().isPentagon(cell));
        CHECK_EQ(base.value().neighborCount(cell), 5);
    }
}

TEST_CASE("hexmap.sphere.buildIsDeterministic") {
    const HexSphereTopology first  = makeTopology(2);
    const HexSphereTopology second = makeTopology(2);

    REQUIRE(first.cellCount() == second.cellCount());
    for (HexSphereCell cell = 0; cell < first.cellCount(); ++cell) {
        CHECK(sameDirection(first.direction(cell), second.direction(cell)));
        const std::int32_t degree = first.neighborCount(cell);
        CHECK_EQ(degree, second.neighborCount(cell));
        for (std::int32_t direction = 0; direction < degree; ++direction) {
            CHECK_EQ(first.neighbor(cell, direction), second.neighbor(cell, direction));
            CHECK(sameDirection(first.corner(cell, direction), second.corner(cell, direction)));
        }
    }
}

TEST_CASE("hexmap.sphere.outOfRangeQueriesAreTotal") {
    const HexSphereTopology topology = makeTopology(1);

    CHECK_EQ(topology.neighborCount(-1), 0);
    CHECK_EQ(topology.neighborCount(topology.cellCount()), 0);
    CHECK_EQ(topology.neighbor(-1, 0), kNoHexSphereCell);
    CHECK_EQ(topology.neighbor(0, -1), kNoHexSphereCell);
    CHECK_EQ(topology.neighbor(0, topology.neighborCount(0)), kNoHexSphereCell);
    CHECK_EQ(topology.directionOf(0, topology.cellCount()), kNoHexSphereCell);
    CHECK_EQ(topology.oppositeDirection(-1, 0), kNoHexSphereCell);

    // `directionOf` must report exactly the cell's own degree, no more and no less.
    std::int32_t adjacent = 0;
    for (HexSphereCell cell = 0; cell < topology.cellCount(); ++cell) {
        if (topology.directionOf(0, cell) >= 0) ++adjacent;
    }
    CHECK_EQ(adjacent, topology.neighborCount(0));

    const HexSphereTopology empty;
    CHECK(empty.empty());
    CHECK_EQ(empty.cellCount(), 0);
    CHECK_EQ(empty.pentagonCount(), 0);
    CHECK_EQ(empty.edgeCount(), 0);
    CHECK_EQ(empty.cornerCount(), 0);
    CHECK_EQ(empty.cellAt(HexVec3{0.f, 1.f, 0.f}), kNoHexSphereCell);
}

// --- HexSphereMap -----------------------------------------------------------

namespace {

/** @brief Builds a map the test depends on, failing the case when it cannot. */
[[nodiscard]] HexSphereMap makeMap(std::int32_t subdivision, float radius = 100.f, std::uint32_t seed = 7u) {
    HexSphereMap map;
    auto         built = map.reset(subdivision, radius, seed);
    REQUIRE(built.ok());
    return map;
}

/** @brief Drains the dirty queue into a vector of cell ids. */
[[nodiscard]] std::vector<HexSphereCell> drainDirty(HexSphereMap& map) {
    std::vector<HexSphereCell> dirty;
    for (HexSphereCell cell = map.takeDirtyCell(); cell != kNoHexSphereCell; cell = map.takeDirtyCell())
        dirty.push_back(cell);
    return dirty;
}

[[nodiscard]] bool holds(const std::vector<HexSphereCell>& cells, HexSphereCell cell) {
    return std::find(cells.begin(), cells.end(), cell) != cells.end();
}

}  // namespace

TEST_CASE("hexmap.sphereMap.resetBuildsEveryCell") {
    const HexSphereMap map = makeMap(2, 250.f, 11u);

    CHECK(!map.empty());
    CHECK_EQ(map.subdivision(), 2);
    CHECK_EQ(map.frequency(), 4);
    CHECK_EQ(map.cellCount(), 10 * 4 * 4 + 2);
    CHECK_EQ(map.pentagonCount(), 12);
    CHECK_EQ(map.topology().cellCount(), map.cellCount());
    CHECK_EQ(map.seed(), 11u);
    CHECK_EQ(map.sphereRadius(), 250.f);
    CHECK(map.elevationStep() > 0.f);

    for (HexSphereCell cell = 0; cell < map.cellCount(); ++cell) {
        CHECK_EQ(map.elevation(cell), 0);
        CHECK_EQ(map.waterLevel(cell), 0);
        CHECK_EQ(map.terrainType(cell), 0);
        CHECK(!map.hasRiver(cell));
        CHECK(!map.hasRoad(cell));
        CHECK(!map.isUnderwater(cell));
        CHECK(!map.isWalled(cell));
    }

    // A fresh map owes a rebuild of every cell, exactly once.
    CHECK_EQ(map.dirtyCellCount(), map.cellCount());
}

TEST_CASE("hexmap.sphereMap.elevationIsRadial") {
    HexSphereMap map  = makeMap(1, 100.f, 3u);
    const float  step = map.elevationStep();

    for (const HexSphereCell cell : std::vector<HexSphereCell>{0, 13, 40}) {
        for (const std::int32_t level : std::vector<std::int32_t>{-4, -1, 0, 3, 8}) {
            CHECK(map.setElevation(cell, level).ok());
            CHECK_EQ(map.elevation(cell), level);

            const float   radius    = 100.f + static_cast<float>(level) * step;
            const HexVec3 direction = map.direction(cell);
            CHECK(std::fabs(map.surfaceRadius(cell) - radius) < 1e-3f);
            CHECK(std::fabs(map.cellPosition(cell).x - direction.x * radius) < 1e-3f);
            CHECK(std::fabs(map.cellPosition(cell).y - direction.y * radius) < 1e-3f);
            CHECK(std::fabs(map.cellPosition(cell).z - direction.z * radius) < 1e-3f);

            // The ground position is the same point at the sphere radius.
            CHECK(std::fabs(map.cellGroundPosition(cell).x - direction.x * 100.f) < 1e-3f);
            CHECK(std::fabs(map.cellGroundPosition(cell).y - direction.y * 100.f) < 1e-3f);
            CHECK(std::fabs(map.cellGroundPosition(cell).z - direction.z * 100.f) < 1e-3f);
        }
    }
}

TEST_CASE("hexmap.sphereMap.authoringClampsAndMarksDirty") {
    HexSphereMap map = makeMap(1);
    // A fresh map owes a rebuild of every cell, so draining it must yield them all.
    CHECK_EQ(static_cast<std::int32_t>(drainDirty(map).size()), map.cellCount());
    CHECK_EQ(map.dirtyCellCount(), 0);

    const HexSphereCell cell = 20;
    CHECK(map.setElevation(cell, 99).ok());
    CHECK_EQ(map.elevation(cell), HexMetrics::kMaxElevation);
    CHECK(map.setElevation(cell, -99).ok());
    CHECK_EQ(map.elevation(cell), HexMetrics::kMinElevation);
    CHECK(map.setWaterLevel(cell, 99).ok());
    CHECK_EQ(map.waterLevel(cell), HexMetrics::kMaxElevation);
    CHECK(map.setTerrainType(cell, 99).ok());
    CHECK_EQ(map.terrainType(cell), kHexTerrainTypeCount - 1);
    CHECK_EQ(map.terrainType(cell), static_cast<std::int32_t>(HexTerrainType::Snow));

    // Every edit owed a rebuild of the cell and of all of its neighbours.
    const std::vector<HexSphereCell> dirty = drainDirty(map);
    CHECK(holds(dirty, cell));
    for (std::int32_t d = 0; d < map.neighborCount(cell); ++d) CHECK(holds(dirty, map.neighbor(cell, d)));

    CHECK_EQ(map.dirtyCellCount(), 0);
    CHECK_EQ(map.takeDirtyCell(), kNoHexSphereCell);
}

TEST_CASE("hexmap.sphereMap.dirtyQueueDrainsEachCellOnce") {
    HexSphereMap map      = makeMap(2);
    const auto   expected = static_cast<std::size_t>(map.cellCount());

    std::vector<std::uint8_t> seen(expected, 0u);
    std::size_t               count = 0;
    for (std::int32_t cell = map.takeDirtyCell(); cell != kNoHexSphereCell; cell = map.takeDirtyCell()) {
        REQUIRE(cell >= 0);
        REQUIRE(static_cast<std::size_t>(cell) < expected);
        CHECK_EQ(seen[static_cast<std::size_t>(cell)], 0);
        seen[static_cast<std::size_t>(cell)] = 1u;
        ++count;
    }
    CHECK_EQ(count, expected);
    CHECK_EQ(map.dirtyCellCount(), 0);
    CHECK_EQ(map.takeDirtyCell(), kNoHexSphereCell);
}

TEST_CASE("hexmap.sphereMap.distanceIsSymmetricAndMatchesNeighbours") {
    const HexSphereMap map = makeMap(3);
    CHECK_EQ(map.distance(13, 13), 0);
    CHECK_EQ(map.distance(-1, 0), kNoHexSphereCell);
    CHECK_EQ(map.distance(0, map.cellCount()), kNoHexSphereCell);

    for (const HexSphereCell cell : std::vector<HexSphereCell>{0, 13, 100, 641}) {
        for (std::int32_t d = 0; d < map.neighborCount(cell); ++d) {
            const HexSphereCell neighbour = map.neighbor(cell, d);
            CHECK_EQ(map.distance(cell, neighbour), 1);
            CHECK_EQ(map.distance(neighbour, cell), 1);
        }
    }

    for (const HexSphereCell a : std::vector<HexSphereCell>{0, 13, 100}) {
        for (const HexSphereCell b : std::vector<HexSphereCell>{200, 400, 641}) {
            CHECK_EQ(map.distance(a, b), map.distance(b, a));
            CHECK(map.distance(a, b) > 1);
        }
    }
}

TEST_CASE("hexmap.sphereMap.brushIsExactlyTheBreadthFirstBall") {
    const HexSphereMap         map = makeMap(2);
    std::vector<HexSphereCell> cells;

    map.collectBrush(0, 0, cells);
    CHECK_EQ(static_cast<std::int32_t>(cells.size()), 1);
    CHECK_EQ(cells.front(), 0);

    // The centre is pentagonal, so its one-step brush is smaller than a hexagon's.
    map.collectBrush(0, 1, cells);
    CHECK_EQ(static_cast<std::int32_t>(cells.size()), 1 + map.neighborCount(0));
    CHECK_EQ(map.neighborCount(0), 5);
    CHECK_EQ(map.neighborCount(13), 6);
    map.collectBrush(13, 1, cells);
    CHECK_EQ(static_cast<std::int32_t>(cells.size()), 7);

    // The brush must be exactly the set of cells within `radius` steps: nothing
    // farther than the radius, and nothing missing.
    for (std::int32_t radius = 0; radius <= 3; ++radius) {
        map.collectBrush(13, radius, cells);
        std::vector<std::uint8_t> inBrush(static_cast<std::size_t>(map.cellCount()), 0u);
        for (const HexSphereCell cell : cells) inBrush[static_cast<std::size_t>(cell)] = 1u;
        for (HexSphereCell cell = 0; cell < map.cellCount(); ++cell) {
            const bool within = map.distance(13, cell) <= radius;
            CHECK_EQ(static_cast<bool>(inBrush[static_cast<std::size_t>(cell)]), within);
        }
    }

    // Breadth-first order is deterministic for a given topology.
    std::vector<HexSphereCell> again;
    map.collectBrush(13, 2, cells);
    map.collectBrush(13, 2, again);
    CHECK_EQ(cells.size(), again.size());
    for (std::size_t i = 0; i < cells.size(); ++i) CHECK_EQ(cells[i], again[i]);

    // An invalid centre or a negative radius yields an empty brush.
    map.collectBrush(-1, 2, cells);
    CHECK(cells.empty());
    map.collectBrush(13, -1, cells);
    CHECK(cells.empty());
}

TEST_CASE("hexmap.sphereMap.riversMirrorAcrossTheSharedEdge") {
    HexSphereMap map = makeMap(2);

    // A pentagon's edges have no arithmetic opposite, so this is precisely the
    // case a planar `opposite(direction)` would get wrong.
    const HexSphereCell pentagon  = 0;
    const HexSphereCell neighbour = map.neighbor(pentagon, 2);
    const std::int32_t  back      = map.oppositeDirection(pentagon, 2);
    REQUIRE(neighbour != kNoHexSphereCell);
    REQUIRE(back >= 0);
    CHECK_EQ(map.neighbor(neighbour, back), pentagon);

    CHECK(map.setOutgoingRiver(pentagon, 2).ok());
    CHECK(map.hasRiverThrough(pentagon, 2));
    CHECK(map.hasRiverThrough(neighbour, back));

    CHECK(map.removeRiver(pentagon).ok());
    CHECK(!map.hasRiver(pentagon));
    CHECK(!map.hasRiver(neighbour));

    // A river may not be carved into an edge the cell does not have.
    CHECK(!map.setOutgoingRiver(pentagon, map.neighborCount(pentagon)).ok());
    CHECK(!map.setOutgoingRiver(pentagon, -1).ok());
}

TEST_CASE("hexmap.sphereMap.roadsMirrorAndRejectSteepEdges") {
    HexSphereMap       map  = makeMap(2);
    const HexSphereCell a   = 13;
    const std::int32_t  dir = 1;
    const HexSphereCell b   = map.neighbor(a, dir);
    const std::int32_t  back = map.oppositeDirection(a, dir);
    REQUIRE(b != kNoHexSphereCell);
    REQUIRE(back >= 0);

    CHECK(map.addRoad(a, dir).ok());
    CHECK(map.hasRoad(a));
    CHECK(map.hasRoad(b));

    // Raising the neighbour by more than one step must drop the road again.
    CHECK(map.setElevation(b, map.elevation(a) + 3).ok());
    CHECK(!map.hasRoad(a));
    CHECK(!map.hasRoad(b));

    // A road may only be added across an existing edge, and only over a slope.
    CHECK(!map.addRoad(a, map.neighborCount(a)).ok());
    CHECK(map.setElevation(b, map.elevation(a) + 2).ok());
    CHECK(map.addRoad(a, dir).ok());
    CHECK(!map.hasRoad(a));
}

TEST_CASE("hexmap.sphereMap.pickCellFindsTheFacingCell") {
    HexSphereMap map = makeMap(2, 100.f, 5u);

    for (const HexSphereCell expected : std::vector<HexSphereCell>{0, 5, 11, 13, 100, 161}) {
        const HexVec3 direction = map.direction(expected);
        const HexVec3 origin    = direction * 400.f;
        auto          picked    = map.pickCell(origin, HexVec3{-direction.x, -direction.y, -direction.z});
        REQUIRE(picked.ok());
        CHECK_EQ(picked.value(), expected);
    }

    // Elevation must not be ignored: a cell raised well above the sphere still
    // answers for the ray aimed at it, and its neighbour does not.
    CHECK(map.setElevation(13, HexMetrics::kMaxElevation).ok());
    {
        const HexVec3 direction = map.direction(13);
        auto          picked    = map.pickCell(direction * 400.f, HexVec3{-direction.x, -direction.y, -direction.z});
        REQUIRE(picked.ok());
        CHECK_EQ(picked.value(), 13);
    }

    const HexVec3 away = map.direction(13) * 400.f;
    auto          missed = map.pickCell(away, map.direction(13));
    CHECK(!missed.ok());
    CHECK(missed.code() == eve::StatusCode::NotFound);

    auto degenerate = map.pickCell(HexVec3{0.f, 0.f, 300.f}, HexVec3{});
    CHECK(!degenerate.ok());
    CHECK(degenerate.code() == eve::StatusCode::Rejected);

    const HexSphereMap empty;
    auto               nothing = empty.pickCell(HexVec3{0.f, 0.f, 300.f}, HexVec3{0.f, 0.f, -1.f});
    CHECK(!nothing.ok());
    CHECK(nothing.code() == eve::StatusCode::NotFound);
}

TEST_CASE("hexmap.sphereMap.rejectsBadShapeArguments") {
    HexSphereMap map;

    CHECK(!map.reset(-1, 100.f, 1u).ok());
    CHECK(!map.reset(kMaxHexSphereSubdivision + 1, 100.f, 1u).ok());
    CHECK(!map.reset(1, 0.f, 1u).ok());
    CHECK(!map.reset(1, -5.f, 1u).ok());
    CHECK(!map.reset(1, std::numeric_limits<float>::quiet_NaN(), 1u).ok());
    CHECK(map.empty());
    CHECK_EQ(map.cellCount(), 0);
    CHECK_EQ(map.takeDirtyCell(), kNoHexSphereCell);

    CHECK(map.reset(1, 100.f, 1u).ok());
    CHECK(!map.empty());
    CHECK_EQ(map.cellCount(), 42);
}

TEST_CASE("hexmap.sphereMap.isDeterministicForTheSameShape") {
    const HexSphereMap first  = makeMap(2, 100.f, 9u);
    const HexSphereMap second = makeMap(2, 100.f, 9u);

    CHECK_EQ(first.cellCount(), second.cellCount());
    CHECK_EQ(first.elevationStep(), second.elevationStep());
    for (HexSphereCell cell = 0; cell < first.cellCount(); ++cell) {
        CHECK(sameDirection(first.direction(cell), second.direction(cell)));
        CHECK_EQ(first.neighborCount(cell), second.neighborCount(cell));
        for (std::int32_t d = 0; d < first.neighborCount(cell); ++d) {
            CHECK_EQ(first.neighbor(cell, d), second.neighbor(cell, d));
            CHECK(sameDirection(first.cornerDirection(cell, d), second.cornerDirection(cell, d)));
        }
    }
}

TEST_CASE("hexmap.sphereMap.invalidCellQueriesAreTotal") {
    const HexSphereMap map = makeMap(1);

    CHECK(!map.contains(-1));
    CHECK(!map.contains(map.cellCount()));
    CHECK_EQ(map.cellAt(-1), nullptr);
    CHECK_EQ(map.cellAt(map.cellCount()), nullptr);
    CHECK_EQ(map.neighborCount(-1), 0);
    CHECK_EQ(map.elevation(-1), 0);
    CHECK_EQ(map.waterLevel(-1), 0);
    CHECK(!map.hasRiver(-1));
    CHECK(!map.hasRoad(-1));
    CHECK(!map.hasRiverThrough(-1, 0));
    CHECK(!map.isUnderwater(-1));
    CHECK_EQ(map.edgeTypeTo(-1, 0), HexEdgeType::Flat);
    CHECK_EQ(map.surfaceRadius(-1), map.sphereRadius());

    const HexSphereMap empty;
    CHECK_EQ(empty.surfaceRadius(0), 0.f);
}

// --- spherical mesh ---------------------------------------------------------

namespace {

/** @brief Exact vector equality; a shared topological corner must be bit-identical. */
[[nodiscard]] bool identical(HexVec3 a, HexVec3 b) { return a.x == b.x && a.y == b.y && a.z == b.z; }

/** @brief Quantised position key used to weld the deliberately unshared vertex stream. */
struct WeldKey {
    int x = 0;
    int y = 0;
    int z = 0;
    [[nodiscard]] bool operator<(const WeldKey& other) const {
        if (x != other.x) return x < other.x;
        if (y != other.y) return y < other.y;
        return z < other.z;
    }
};

/**
 * @brief Welds a position to a 1e-3 grid.
 *
 * The mesher never shares a vertex between triangles, so two triangles only "meet" if
 * their positions are equal - and shared corners are emitted as an exactly equal pure
 * function of the topological position, so a coarse quantum is enough to weld them
 * while staying far below the distance between two distinct corners.
 */
[[nodiscard]] WeldKey weldKey(HexVec3 p) {
    constexpr double kQuantum = 1000.0;
    return WeldKey{static_cast<int>(std::llround(static_cast<double>(p.x) * kQuantum)),
                   static_cast<int>(std::llround(static_cast<double>(p.y) * kQuantum)),
                   static_cast<int>(std::llround(static_cast<double>(p.z) * kQuantum))};
}

/** @brief Edge-use census of a welded mesh. */
struct WeldReport {
    std::size_t vertices      = 0;
    std::size_t edges         = 0;
    std::size_t boundaryEdges = 0;
    std::size_t nonManifold   = 0;
    std::size_t triangles     = 0;
};

/**
 * @brief Welds the mesh by position and counts how many triangles meet at every edge.
 *
 * A closed triangulated sphere has no boundary: every welded edge is shared by exactly
 * two triangles. A count of one is a crack and a count above two is a duplicated or
 * overlapping sheet. Both are invisible to a triangle count, and both are exactly what
 * a wrong winding, a wrong edge-span or a wrong corner index produces.
 */
[[nodiscard]] WeldReport analyseWeld(const HexMeshData& mesh) {
    WeldReport report;
    report.triangles = mesh.triangleCount();

    const auto&                positions = mesh.positions();
    const std::size_t          count     = positions.size() / 3u;
    std::map<WeldKey, std::uint32_t> welded;
    std::vector<std::uint32_t> ids;
    ids.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        const HexVec3 position{positions[i * 3u], positions[i * 3u + 1u], positions[i * 3u + 2u]};
        const auto    inserted = welded.emplace(weldKey(position), static_cast<std::uint32_t>(welded.size()));
        ids.push_back(inserted.first->second);
    }

    std::map<std::pair<std::uint32_t, std::uint32_t>, std::int32_t> edgeUse;
    const auto&                                                     indices = mesh.indices();
    for (std::size_t t = 0; t + 2u < indices.size(); t += 3u) {
        const std::uint32_t triangle[3] = {ids[indices[t]], ids[indices[t + 1u]], ids[indices[t + 2u]]};
        for (std::int32_t e = 0; e < 3; ++e) {
            std::uint32_t a = triangle[e];
            std::uint32_t b = triangle[(e + 1) % 3];
            if (a > b) std::swap(a, b);
            ++edgeUse[std::make_pair(a, b)];
        }
    }

    report.edges = edgeUse.size();
    report.vertices = welded.size();
    for (const auto& entry : edgeUse) {
        if (entry.second == 1) ++report.boundaryEdges;
        else if (entry.second > 2) ++report.nonManifold;
    }
    return report;
}

/**
 * @brief Asserts the welded surface is closed, manifold and of genus zero.
 *
 * `V - E + F = 2` is what turns "no boundary edges were found" into "this really is a
 * closed sphere": a weld that silently merged or missed vertices would still show no
 * boundary edges, but it could not also satisfy the Euler characteristic.
 */
void checkClosedSphere(const WeldReport& report) {
    // REQUIRE, not CHECK: in this suite a failed CHECK is only a warning and the test
    // still exits zero, which is how a mesh with 118 non-manifold edges and 241 boundary
    // edges passed this test unnoticed.
    REQUIRE(report.triangles > 0u);
    REQUIRE(report.vertices > 0u);
    REQUIRE(report.edges > 0u);
    REQUIRE_EQ(report.boundaryEdges, static_cast<std::size_t>(0));
    REQUIRE_EQ(report.nonManifold, static_cast<std::size_t>(0));
    REQUIRE_EQ(report.vertices + report.triangles - report.edges, static_cast<std::size_t>(2));
}

/** @brief Number of triangles whose stored normal points back towards the sphere centre. */
[[nodiscard]] std::size_t countInwardFacingTriangles(const HexMeshData& mesh) {
    const auto& positions = mesh.positions();
    const auto& normals   = mesh.normals();
    const auto& indices   = mesh.indices();

    std::size_t inward = 0;
    for (std::size_t t = 0; t + 2u < indices.size(); t += 3u) {
        const std::size_t normalBase = static_cast<std::size_t>(indices[t]) * 3u;
        const HexVec3     normal{normals[normalBase], normals[normalBase + 1u], normals[normalBase + 2u]};

        HexVec3 centroid{};
        for (std::int32_t k = 0; k < 3; ++k) {
            const std::size_t base = static_cast<std::size_t>(indices[t + static_cast<std::size_t>(k)]) * 3u;
            centroid.x += positions[base];
            centroid.y += positions[base + 1u];
            centroid.z += positions[base + 2u];
        }
        centroid = centroid * (1.f / 3.f);

        if (normal.x * centroid.x + normal.y * centroid.y + normal.z * centroid.z <= 0.f) ++inward;
    }
    return inward;
}

/** @brief Raises a scattered subset of cells so that slopes and cliffs both occur. */
void scatterElevation(HexSphereMap& map) {
    for (HexSphereCell cell = 0; cell < map.cellCount(); ++cell) {
        const std::int32_t level = (cell % 5 == 0) ? 3 : ((cell % 7 == 0) ? 1 : 0);
        REQUIRE(map.setElevation(cell, level).ok());
    }
}

}  // namespace

TEST_CASE("hexmap.sphereMesh.isAWatertightSurface") {
    // Flat: only fans and blend strips are exercised.
    {
        HexSphereMap map = makeMap(1, 100.f, 4u);
        HexMeshData  mesh;
        buildSphereTerrainMesh(map, mesh);
        REQUIRE(!mesh.empty());

        const WeldReport report = analyseWeld(mesh);
        checkClosedSphere(report);
    }

    // Scattered elevations: terraces and radial cliff walls are exercised as well.
    {
        HexSphereMap map = makeMap(1, 100.f, 4u);
        scatterElevation(map);

        HexMeshData mesh;
        buildSphereTerrainMesh(map, mesh);
        checkClosedSphere(analyseWeld(mesh));
    }

    // A finer level, to catch a crack that only appears when a pentagon meets hexagons.
    {
        HexSphereMap map = makeMap(2, 100.f, 4u);
        scatterElevation(map);

        HexMeshData mesh;
        buildSphereTerrainMesh(map, mesh);
        const WeldReport report = analyseWeld(mesh);
        checkClosedSphere(report);
        // Guard against a vacuously small mesh passing the census.
        CHECK(report.triangles > 1000u);
    }
}

TEST_CASE("hexmap.sphereMesh.facesPointAwayFromTheCentre") {
    HexSphereMap map = makeMap(2, 100.f, 6u);
    scatterElevation(map);

    HexMeshData mesh;
    buildSphereTerrainMesh(map, mesh);
    REQUIRE(!mesh.empty());
    REQUIRE(mesh.hasNormals());

    // A clockwise fan or a reversed strip would light the planet from inside out.
    CHECK_EQ(countInwardFacingTriangles(mesh), static_cast<std::size_t>(0));
}

TEST_CASE("hexmap.sphereMesh.verticesStayOnTheirSurfaceRadius") {
    HexSphereMap map = makeMap(1, 100.f, 2u);
    CHECK(map.setElevation(0, HexMetrics::kMaxElevation).ok());
    CHECK(map.setElevation(1, HexMetrics::kMinElevation).ok());
    CHECK(map.setElevation(2, HexMetrics::kMaxElevation).ok());

    HexMeshData mesh;
    buildSphereTerrainMesh(map, mesh);
    REQUIRE(!mesh.empty());

    const float step    = map.elevationStep();
    const float lowest  = map.sphereRadius() + static_cast<float>(HexMetrics::kMinElevation) * step;
    const float highest = map.sphereRadius() + static_cast<float>(HexMetrics::kMaxElevation) * step;
    const float deepest = lowest + (HexMetrics::kStreamBedElevationOffset / HexMetrics::kElevationStep) * step;

    // The perturbation slides *along* the surface, so it must not move a vertex in or
    // out; every radius has to stay inside the relief the elevations can produce.
    const auto& positions = mesh.positions();
    for (std::size_t i = 0; i + 2u < positions.size(); i += 3u) {
        const HexVec3 point{positions[i], positions[i + 1u], positions[i + 2u]};
        const float   radius = std::sqrt(point.x * point.x + point.y * point.y + point.z * point.z);
        CHECK(radius >= deepest - 1e-2f);
        CHECK(radius <= highest + 1e-2f);
    }
}

TEST_CASE("hexmap.sphereMesh.isDeterministicAndReusable") {
    const HexSphereMap map = makeMap(1, 100.f, 8u);

    HexMeshData first;
    HexMeshData second;
    buildSphereTerrainMesh(map, first);
    buildSphereTerrainMesh(map, second);

    REQUIRE(!first.empty());
    CHECK_EQ(first.vertexCount(), second.vertexCount());
    CHECK_EQ(first.triangleCount(), second.triangleCount());
    CHECK(first.positions() == second.positions());
    CHECK(first.normals() == second.normals());
    CHECK(first.uvs() == second.uvs());
    CHECK(first.indices() == second.indices());

    // Rebuilding into a mesh that already holds data must replace it, not append.
    HexMeshData reused;
    buildSphereTerrainMesh(map, reused);
    buildSphereTerrainMesh(map, reused);
    CHECK_EQ(reused.vertexCount(), first.vertexCount());
    CHECK_EQ(reused.triangleCount(), first.triangleCount());
}

TEST_CASE("hexmap.sphereMesh.emptyMapProducesAnEmptyMesh") {
    const HexSphereMap map;
    HexMeshData        mesh;
    buildSphereTerrainMesh(map, mesh);

    CHECK(mesh.empty());
    CHECK_EQ(mesh.vertexCount(), static_cast<std::size_t>(0));
    CHECK_EQ(mesh.triangleCount(), static_cast<std::size_t>(0));
}

TEST_CASE("hexmap.sphere.cornerIndicesAgreeAcrossTheThreeCellsAtACorner") {
    // The mesher reaches a shared corner from three different cells by index arithmetic
    // alone; this is that arithmetic, checked against the topology's own corner table.
    for (std::int32_t subdivision = 0; subdivision <= 3; ++subdivision) {
        const HexSphereTopology topology = makeTopology(subdivision);
        for (HexSphereCell cell = 0; cell < topology.cellCount(); ++cell) {
            const std::int32_t edges = topology.neighborCount(cell);
            for (std::int32_t d = 0; d < edges; ++d) {
                const std::int32_t  nextD = (d + 1) % edges;
                const HexSphereCell left  = topology.neighbor(cell, d);
                const HexSphereCell right = topology.neighbor(cell, nextD);
                if (left == kNoHexSphereCell || right == kNoHexSphereCell) continue;

                const std::int32_t back  = topology.oppositeDirection(cell, d);
                const std::int32_t back2 = topology.oppositeDirection(cell, nextD);
                REQUIRE(back >= 0);
                REQUIRE(back2 >= 0);

                const HexVec3 shared = topology.corner(cell, nextD);
                CHECK(identical(topology.corner(left, back), shared));
                CHECK(identical(topology.corner(right, (back2 + 1) % topology.neighborCount(right)), shared));
            }
        }
    }
}

// --- spherical generator ----------------------------------------------------

namespace {

/** @brief Generates a planet at `landPercentage` and returns it. */
[[nodiscard]] HexSphereMap generatePlanet(std::int32_t landPercentage) {
    HexSphereMap               map = makeMap(2, 100.f, 12u);
    HexSphereGeneratorSettings settings{};
    settings.seed           = 1234u;
    settings.landPercentage = landPercentage;
    settings.waterLevel     = 0;
    auto generated          = generateSphereMap(map, settings);
    REQUIRE(generated.ok());
    return map;
}

/** @brief Cells the generator left above their own water line. */
[[nodiscard]] std::int32_t landCells(const HexSphereMap& map) {
    std::int32_t land = 0;
    for (HexSphereCell cell = 0; cell < map.cellCount(); ++cell) {
        if (map.elevation(cell) > map.waterLevel(cell)) ++land;
    }
    return land;
}

}  // namespace

TEST_CASE("hexmap.sphereGenerator.matchesItsLandTargetAndIsDeterministic") {
    const HexSphereMap first  = generatePlanet(45);
    const HexSphereMap second = generatePlanet(45);

    for (HexSphereCell cell = 0; cell < first.cellCount(); ++cell) {
        CHECK_EQ(first.elevation(cell), second.elevation(cell));
        CHECK_EQ(first.waterLevel(cell), second.waterLevel(cell));
        CHECK_EQ(first.terrainType(cell), second.terrainType(cell));
    }

    for (HexSphereCell cell = 0; cell < first.cellCount(); ++cell) {
        CHECK(first.elevation(cell) >= HexMetrics::kMinElevation);
        CHECK(first.elevation(cell) <= HexMetrics::kMaxElevation);
        const std::int32_t terrain = first.terrainType(cell);
        CHECK(terrain >= 0);
        CHECK(terrain < kHexTerrainTypeCount);
    }

    // The sea level is picked from the noise's own distribution, so the share of land
    // has to follow the requested percentage rather than wherever the field fell.
    const std::int32_t expected = first.cellCount() * 45 / 100;
    const std::int32_t actual   = landCells(first);
    CHECK(actual > expected - first.cellCount() / 10);
    CHECK(actual < expected + first.cellCount() / 10);
}

TEST_CASE("hexmap.sphereGenerator.landShareFollowsTheSetting") {
    CHECK(landCells(generatePlanet(25)) < landCells(generatePlanet(70)));
}

TEST_CASE("hexmap.sphereGenerator.rejectsAnEmptyMap") {
    HexSphereMap               map;
    HexSphereGeneratorSettings settings{};
    auto                       generated = generateSphereMap(map, settings);

    CHECK(!generated.ok());
    CHECK(generated.code() == eve::StatusCode::Rejected);
}
