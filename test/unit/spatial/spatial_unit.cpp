#include "common/Exception.h"
#include "spatial/BSPTree2D.h"
#include "spatial/BSPTree3D.h"
#include "spatial/Octree.h"
#include "spatial/QuadTree.h"
#include "spatial/Spatial.h"
#include "spatial/SpatialHash2D.h"
#include "spatial/SpatialHash3D.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <memory>
#include <set>
#include <string>

using namespace eve::spatial;

namespace {
template <class Index>
std::set<int> ids(const Index& index) {
    std::set<int> result;
    for (int i = 0; i < index.getResultCount(); ++i) result.insert(index.getResultId(i));
    return result;
}
}  // namespace

TEST_CASE("Spatial 2D index contract") {
    SUB_CASE("spatial contract: quadtree upserts IDs and queries replace their result buffer") {
        QuadTree tree(0, 0, 100, 100, 6, 2);
        REQUIRE(tree.insert(1, 10, 10, 20, 20));
        REQUIRE(tree.insert(2, 50, 50, 60, 60));
        REQUIRE(tree.insert(3, 80, 10, 90, 20));
        REQUIRE(tree.insert(1, 10, 10, 20, 20));
        REQUIRE_EQ(tree.getCount(), 3);
        REQUIRE(tree.insert(4, 101, 101, 102, 102));
        REQUIRE_EQ(tree.getCount(), 4);
        REQUIRE(tree.contains(4));
        REQUIRE_EQ(tree.queryRect(0, 0, 30, 30), 1);
        REQUIRE_EQ(ids(tree), std::set<int>{1});
        REQUIRE_EQ(tree.queryPoint(55, 55), 1);
        REQUIRE_EQ(ids(tree), std::set<int>{2});
        REQUIRE_EQ(tree.queryCircle(85, 15, 5), 1);
        REQUIRE_EQ(ids(tree), std::set<int>{3});
        REQUIRE_EQ(tree.getResultId(1), -1);
        REQUIRE(tree.update(2, 11, 11, 12, 12));
        REQUIRE_EQ(tree.queryRect(0, 0, 30, 30), 2);
        REQUIRE_EQ(ids(tree), (std::set<int>{1, 2}));
        REQUIRE(!tree.update(99, 0, 0, 1, 1));
        REQUIRE(tree.remove(1));
        REQUIRE(!tree.remove(1));
        REQUIRE(!tree.contains(1));
        tree.clear();
        REQUIRE_EQ(tree.getCount(), 0);
        REQUIRE_EQ(tree.queryRect(0, 0, 100, 100), 0);
    };

    SUB_CASE("spatial contract: 2D hash deduplicates queries and reindexes moves and cell-size changes") {
        SpatialHash2D hash(16.f);
        REQUIRE(hash.insert(10, 0, 0, 8, 8));
        REQUIRE(hash.insert(11, 40, 40, 48, 48));
        REQUIRE(hash.insert(12, 14, 14, 18, 18));
        REQUIRE_EQ(hash.queryRect(0, 0, 20, 20), 2);
        REQUIRE_EQ(ids(hash), (std::set<int>{10, 12}));
        REQUIRE_EQ(hash.queryPoint(16, 16), 1);
        REQUIRE_EQ(ids(hash), std::set<int>{12});
        REQUIRE(hash.update(11, 0, 0, 2, 2));
        REQUIRE_EQ(hash.queryCircle(44, 44, 5), 0);
        REQUIRE_EQ(hash.queryRect(0, 0, 20, 20), 3);
        REQUIRE_EQ(ids(hash), (std::set<int>{10, 11, 12}));
        REQUIRE(hash.remove(12));
        REQUIRE(!hash.contains(12));
        hash.setCellSize(8.f);
        REQUIRE_EQ(hash.getCellSize(), 8.f);
        REQUIRE_EQ(hash.getCount(), 2);
        REQUIRE_EQ(hash.queryRect(0, 0, 20, 20), 2);
        REQUIRE_EQ(ids(hash), (std::set<int>{10, 11}));
        hash.clear();
        REQUIRE_EQ(hash.getCount(), 0);
    };
}

TEST_CASE("Spatial 3D index contract") {
    SUB_CASE("spatial contract: octree and 3D hash preserve volumetric point and range semantics") {
        Octree tree(0, 0, 0, 100, 100, 100, 6, 2);
        REQUIRE(tree.insert(1, 10, 10, 10, 20, 20, 20));
        REQUIRE(tree.insert(2, 70, 70, 70, 80, 80, 80));
        REQUIRE_EQ(tree.queryAABB(0, 0, 0, 30, 30, 30), 1);
        REQUIRE_EQ(ids(tree), std::set<int>{1});
        REQUIRE_EQ(tree.querySphere(75, 75, 75, 8), 1);
        REQUIRE_EQ(ids(tree), std::set<int>{2});
        REQUIRE(tree.update(1, 71, 71, 71, 72, 72, 72));
        REQUIRE_EQ(tree.queryAABB(0, 0, 0, 30, 30, 30), 0);
        REQUIRE_EQ(tree.querySphere(75, 75, 75, 10), 2);

        SpatialHash3D hash(16.f);
        REQUIRE(hash.insert(3, 0, 0, 0, 5, 5, 5));
        REQUIRE(hash.insert(4, 32, 32, 32, 40, 40, 40));
        REQUIRE_EQ(hash.queryPoint(2, 2, 2), 1);
        REQUIRE_EQ(ids(hash), std::set<int>{3});
        REQUIRE_EQ(hash.queryAABB(30, 30, 30, 50, 50, 50), 1);
        REQUIRE_EQ(ids(hash), std::set<int>{4});
        REQUIRE(hash.remove(4));
        REQUIRE_EQ(hash.querySphere(36, 36, 36, 4), 0);
    };

    SUB_CASE("spatial contract: BSP indexes survive subdivision and return only overlapping IDs") {
        BSPTree2D flat(0, 0, 200, 200, 10, 2);
        for (int i = 0; i < 20; ++i) {
            const float coordinate = float(i) * 8.f;
            REQUIRE(flat.insert(100 + i, coordinate, coordinate, coordinate + 4.f, coordinate + 4.f));
        }
        REQUIRE_EQ(flat.getCount(), 20);
        REQUIRE_EQ(flat.queryRect(0, 0, 20, 20), 3);
        REQUIRE_EQ(ids(flat), (std::set<int>{100, 101, 102}));
        REQUIRE_EQ(flat.queryCircle(1000, 1000, 1), 0);

        BSPTree3D volume(0, 0, 0, 100, 100, 100, 10, 2);
        REQUIRE(volume.insert(1, 5, 5, 5, 10, 10, 10));
        REQUIRE(volume.insert(2, 50, 50, 50, 55, 55, 55));
        REQUIRE_EQ(volume.queryAABB(0, 0, 0, 15, 15, 15), 1);
        REQUIRE_EQ(ids(volume), std::set<int>{1});
        REQUIRE_EQ(volume.querySphere(52, 52, 52, 5), 1);
        REQUIRE_EQ(ids(volume), std::set<int>{2});
    };
}

TEST_CASE("Spatial factory and invalid construction contract") {
    SUB_CASE("spatial contract: module factories return independent caller-owned indexes") {
        auto* module = Spatial::create();
        REQUIRE(module != nullptr);
        REQUIRE_EQ(module->getName(), "Spatial");
        REQUIRE(Spatial::create() == module);
        std::unique_ptr<QuadTree>      quad(module->newQuadTree(0, 0, 10, 10));
        std::unique_ptr<Octree>        oct(module->newOctree(0, 0, 0, 10, 10, 10));
        std::unique_ptr<SpatialHash2D> hash2(module->newSpatialHash2D(8.f));
        std::unique_ptr<SpatialHash3D> hash3(module->newSpatialHash3D(8.f));
        std::unique_ptr<BSPTree2D>     bsp2(module->newBSPTree2D(0, 0, 10, 10));
        std::unique_ptr<BSPTree3D>     bsp3(module->newBSPTree3D(0, 0, 0, 10, 10, 10));
        REQUIRE(quad.get() != nullptr);
        REQUIRE(oct.get() != nullptr);
        REQUIRE(hash2.get() != nullptr);
        REQUIRE(hash3.get() != nullptr);
        REQUIRE(bsp2.get() != nullptr);
        REQUIRE(bsp3.get() != nullptr);
        REQUIRE(quad->insert(1, 1, 1, 2, 2));
        REQUIRE_EQ(quad->queryPoint(1.5f, 1.5f), 1);
        REQUIRE_EQ(hash2->getCount(), 0);
    };

    SUB_CASE("spatial contract: invalid construction bounds reject before creating an index") {
        bool badBounds = false;
        try {
            QuadTree invalid(0, 0, 0, 10);
        } catch (const eve::Exception&) {
            badBounds = true;
        }
        REQUIRE(badBounds);
        bool badCell = false;
        try {
            SpatialHash2D invalid(0.f);
        } catch (const eve::Exception&) {
            badCell = true;
        }
        REQUIRE(badCell);
    };
}
