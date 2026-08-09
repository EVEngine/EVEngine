#include "spatial/Spatial.h"

#include "spatial/BSPTree2D.h"
#include "spatial/BSPTree3D.h"
#include "spatial/Octree.h"
#include "spatial/QuadTree.h"
#include "spatial/SpatialHash2D.h"
#include "spatial/SpatialHash3D.h"

#include <simplesquirrel/simplesquirrel.hpp>

namespace eve::spatial {

Module_IMPL(Spatial, new Spatial());

QuadTree *Spatial::newQuadTree(float minX, float minY, float maxX, float maxY, int maxDepth,
                               int maxPerNode) {
    return new QuadTree(minX, minY, maxX, maxY, maxDepth, maxPerNode);
}

Octree *Spatial::newOctree(float minX, float minY, float minZ, float maxX, float maxY, float maxZ,
                           int maxDepth, int maxPerNode) {
    return new Octree(minX, minY, minZ, maxX, maxY, maxZ, maxDepth, maxPerNode);
}

SpatialHash2D *Spatial::newSpatialHash2D(float cellSize) { return new SpatialHash2D(cellSize); }

SpatialHash3D *Spatial::newSpatialHash3D(float cellSize) { return new SpatialHash3D(cellSize); }

BSPTree2D *Spatial::newBSPTree2D(float minX, float minY, float maxX, float maxY, int maxDepth,
                                 int maxPerNode) {
    return new BSPTree2D(minX, minY, maxX, maxY, maxDepth, maxPerNode);
}

BSPTree3D *Spatial::newBSPTree3D(float minX, float minY, float minZ, float maxX, float maxY,
                                 float maxZ, int maxDepth, int maxPerNode) {
    return new BSPTree3D(minX, minY, minZ, maxX, maxY, maxZ, maxDepth, maxPerNode);
}

void Spatial::expose(ssq::Table &table) {
    auto cls = table.addClass(name, Spatial::create, false);
    expose(cls);

    auto qt = table.addClass<QuadTree>(
        "QuadTree",
        std::function<QuadTree *()>([]() -> QuadTree * { return nullptr; }), true);
    qt.addFunc("clear", &QuadTree::clear);
    qt.addFunc("insert", &QuadTree::insert);
    qt.addFunc("remove", &QuadTree::remove);
    qt.addFunc("update", &QuadTree::update);
    qt.addFunc("contains", &QuadTree::contains);
    qt.addFunc("getCount", &QuadTree::getCount);
    qt.addFunc("queryPoint", &QuadTree::queryPoint);
    qt.addFunc("queryRect", &QuadTree::queryRect);
    qt.addFunc("queryCircle", &QuadTree::queryCircle);
    qt.addFunc("getResultCount", &QuadTree::getResultCount);
    qt.addFunc("getResultId", &QuadTree::getResultId);
    qt.addFunc("getMinX", &QuadTree::getMinX);
    qt.addFunc("getMinY", &QuadTree::getMinY);
    qt.addFunc("getMaxX", &QuadTree::getMaxX);
    qt.addFunc("getMaxY", &QuadTree::getMaxY);
    qt.addFunc("getMaxDepth", &QuadTree::getMaxDepth);
    qt.addFunc("getMaxPerNode", &QuadTree::getMaxPerNode);

    auto ot = table.addClass<Octree>(
        "Octree", std::function<Octree *()>([]() -> Octree * { return nullptr; }), true);
    ot.addFunc("clear", &Octree::clear);
    ot.addFunc("insert", &Octree::insert);
    ot.addFunc("remove", &Octree::remove);
    ot.addFunc("update", &Octree::update);
    ot.addFunc("contains", &Octree::contains);
    ot.addFunc("getCount", &Octree::getCount);
    ot.addFunc("queryPoint", &Octree::queryPoint);
    ot.addFunc("queryAABB", &Octree::queryAABB);
    ot.addFunc("querySphere", &Octree::querySphere);
    ot.addFunc("getResultCount", &Octree::getResultCount);
    ot.addFunc("getResultId", &Octree::getResultId);
    ot.addFunc("getMinX", &Octree::getMinX);
    ot.addFunc("getMinY", &Octree::getMinY);
    ot.addFunc("getMinZ", &Octree::getMinZ);
    ot.addFunc("getMaxX", &Octree::getMaxX);
    ot.addFunc("getMaxY", &Octree::getMaxY);
    ot.addFunc("getMaxZ", &Octree::getMaxZ);
    ot.addFunc("getMaxDepth", &Octree::getMaxDepth);
    ot.addFunc("getMaxPerNode", &Octree::getMaxPerNode);

    auto h2 = table.addClass<SpatialHash2D>(
        "SpatialHash2D",
        std::function<SpatialHash2D *()>([]() -> SpatialHash2D * { return nullptr; }), true);
    h2.addFunc("clear", &SpatialHash2D::clear);
    h2.addFunc("setCellSize", &SpatialHash2D::setCellSize);
    h2.addFunc("getCellSize", &SpatialHash2D::getCellSize);
    h2.addFunc("insert", &SpatialHash2D::insert);
    h2.addFunc("remove", &SpatialHash2D::remove);
    h2.addFunc("update", &SpatialHash2D::update);
    h2.addFunc("contains", &SpatialHash2D::contains);
    h2.addFunc("getCount", &SpatialHash2D::getCount);
    h2.addFunc("queryPoint", &SpatialHash2D::queryPoint);
    h2.addFunc("queryRect", &SpatialHash2D::queryRect);
    h2.addFunc("queryCircle", &SpatialHash2D::queryCircle);
    h2.addFunc("getResultCount", &SpatialHash2D::getResultCount);
    h2.addFunc("getResultId", &SpatialHash2D::getResultId);

    auto h3 = table.addClass<SpatialHash3D>(
        "SpatialHash3D",
        std::function<SpatialHash3D *()>([]() -> SpatialHash3D * { return nullptr; }), true);
    h3.addFunc("clear", &SpatialHash3D::clear);
    h3.addFunc("setCellSize", &SpatialHash3D::setCellSize);
    h3.addFunc("getCellSize", &SpatialHash3D::getCellSize);
    h3.addFunc("insert", &SpatialHash3D::insert);
    h3.addFunc("remove", &SpatialHash3D::remove);
    h3.addFunc("update", &SpatialHash3D::update);
    h3.addFunc("contains", &SpatialHash3D::contains);
    h3.addFunc("getCount", &SpatialHash3D::getCount);
    h3.addFunc("queryPoint", &SpatialHash3D::queryPoint);
    h3.addFunc("queryAABB", &SpatialHash3D::queryAABB);
    h3.addFunc("querySphere", &SpatialHash3D::querySphere);
    h3.addFunc("getResultCount", &SpatialHash3D::getResultCount);
    h3.addFunc("getResultId", &SpatialHash3D::getResultId);

    auto b2 = table.addClass<BSPTree2D>(
        "BSPTree2D",
        std::function<BSPTree2D *()>([]() -> BSPTree2D * { return nullptr; }), true);
    b2.addFunc("clear", &BSPTree2D::clear);
    b2.addFunc("insert", &BSPTree2D::insert);
    b2.addFunc("remove", &BSPTree2D::remove);
    b2.addFunc("update", &BSPTree2D::update);
    b2.addFunc("contains", &BSPTree2D::contains);
    b2.addFunc("getCount", &BSPTree2D::getCount);
    b2.addFunc("queryPoint", &BSPTree2D::queryPoint);
    b2.addFunc("queryRect", &BSPTree2D::queryRect);
    b2.addFunc("queryCircle", &BSPTree2D::queryCircle);
    b2.addFunc("getResultCount", &BSPTree2D::getResultCount);
    b2.addFunc("getResultId", &BSPTree2D::getResultId);
    b2.addFunc("getMinX", &BSPTree2D::getMinX);
    b2.addFunc("getMinY", &BSPTree2D::getMinY);
    b2.addFunc("getMaxX", &BSPTree2D::getMaxX);
    b2.addFunc("getMaxY", &BSPTree2D::getMaxY);
    b2.addFunc("getMaxDepth", &BSPTree2D::getMaxDepth);
    b2.addFunc("getMaxPerNode", &BSPTree2D::getMaxPerNode);

    auto b3 = table.addClass<BSPTree3D>(
        "BSPTree3D",
        std::function<BSPTree3D *()>([]() -> BSPTree3D * { return nullptr; }), true);
    b3.addFunc("clear", &BSPTree3D::clear);
    b3.addFunc("insert", &BSPTree3D::insert);
    b3.addFunc("remove", &BSPTree3D::remove);
    b3.addFunc("update", &BSPTree3D::update);
    b3.addFunc("contains", &BSPTree3D::contains);
    b3.addFunc("getCount", &BSPTree3D::getCount);
    b3.addFunc("queryPoint", &BSPTree3D::queryPoint);
    b3.addFunc("queryAABB", &BSPTree3D::queryAABB);
    b3.addFunc("querySphere", &BSPTree3D::querySphere);
    b3.addFunc("getResultCount", &BSPTree3D::getResultCount);
    b3.addFunc("getResultId", &BSPTree3D::getResultId);
    b3.addFunc("getMinX", &BSPTree3D::getMinX);
    b3.addFunc("getMinY", &BSPTree3D::getMinY);
    b3.addFunc("getMinZ", &BSPTree3D::getMinZ);
    b3.addFunc("getMaxX", &BSPTree3D::getMaxX);
    b3.addFunc("getMaxY", &BSPTree3D::getMaxY);
    b3.addFunc("getMaxZ", &BSPTree3D::getMaxZ);
    b3.addFunc("getMaxDepth", &BSPTree3D::getMaxDepth);
    b3.addFunc("getMaxPerNode", &BSPTree3D::getMaxPerNode);
}

void Spatial::expose(ssq::Class &cls) {
    cls.addFunc("getName", &Spatial::getName);
    cls.addFunc("newQuadTree", &Spatial::newQuadTree);
    cls.addFunc("newOctree", &Spatial::newOctree);
    cls.addFunc("newSpatialHash2D", &Spatial::newSpatialHash2D);
    cls.addFunc("newSpatialHash3D", &Spatial::newSpatialHash3D);
    cls.addFunc("newBSPTree2D", &Spatial::newBSPTree2D);
    cls.addFunc("newBSPTree3D", &Spatial::newBSPTree3D);
}

}  // namespace eve::spatial
