#include "spatial/BSPTree3D.h"

#include "common/Exception.h"

namespace eve::spatial {

BSPTree3D::BSPTree3D(float minX, float minY, float minZ, float maxX, float maxY, float maxZ,
                     int maxDepth, int maxPerNode)
    : rootBounds_(makeAABB3(minX, minY, minZ, maxX, maxY, maxZ)),
      maxDepth_(std::max(1, maxDepth)),
      maxPerNode_(std::max(1, maxPerNode)) {
    if (!rootBounds_.valid() || rootBounds_.width() <= 0.f || rootBounds_.height() <= 0.f ||
        rootBounds_.depth() <= 0.f) {
        throw Exception("BSPTree3D: bounds must have positive volume");
    }
    ensureRoot();
}

BSPTree3D::Node *BSPTree3D::ensureRoot() {
    if (!root_) {
        root_         = std::make_unique<Node>();
        root_->bounds = rootBounds_;
        root_->depth  = 0;
        root_->axis   = 0;
    }
    return root_.get();
}

void BSPTree3D::clear() {
    items_.clear();
    results_.clear();
    root_.reset();
    ensureRoot();
}

bool BSPTree3D::contains(int id) const { return items_.find(id) != items_.end(); }

bool BSPTree3D::insert(int id, float minX, float minY, float minZ, float maxX, float maxY,
                       float maxZ) {
    AABB3 b = makeAABB3(minX, minY, minZ, maxX, maxY, maxZ);
    if (!b.valid()) return false;
    if (contains(id)) remove(id);
    items_[id] = b;
    if (!insertInto(*ensureRoot(), id, b)) {
        ensureRoot()->itemIds.push_back(id);
    }
    return true;
}

bool BSPTree3D::remove(int id) {
    if (!contains(id)) return false;
    items_.erase(id);
    rebuild();
    return true;
}

bool BSPTree3D::update(int id, float minX, float minY, float minZ, float maxX, float maxY,
                       float maxZ) {
    if (!contains(id)) return false;
    return insert(id, minX, minY, minZ, maxX, maxY, maxZ);
}

void BSPTree3D::rebuild() {
    results_.clear();
    root_.reset();
    ensureRoot();
    for (const auto &kv : items_) {
        if (!insertInto(*root_, kv.first, kv.second)) {
            root_->itemIds.push_back(kv.first);
        }
    }
}

bool BSPTree3D::insertInto(Node &node, int id, const AABB3 &bounds) {
    if (!node.bounds.containsAABB(bounds) && node.depth == 0) {
        if (!node.bounds.intersectsAABB(bounds)) return false;
    } else if (node.depth > 0 && !node.bounds.containsAABB(bounds)) {
        return false;
    }

    if (node.isLeaf()) {
        node.itemIds.push_back(id);
        if (static_cast<int>(node.itemIds.size()) > maxPerNode_ && node.depth < maxDepth_) {
            split(node);
        }
        return true;
    }

    float bMin = 0.f, bMax = 0.f;
    if (node.axis == 0) {
        bMin = bounds.minX;
        bMax = bounds.maxX;
    } else if (node.axis == 1) {
        bMin = bounds.minY;
        bMax = bounds.maxY;
    } else {
        bMin = bounds.minZ;
        bMax = bounds.maxZ;
    }

    if (bMax <= node.split && node.left) {
        return insertInto(*node.left, id, bounds);
    }
    if (bMin >= node.split && node.right) {
        return insertInto(*node.right, id, bounds);
    }
    node.itemIds.push_back(id);
    return true;
}

void BSPTree3D::split(Node &node) {
    node.axis = node.depth % 3;
    if (node.axis == 0) {
        node.split = node.bounds.centerX();
    } else if (node.axis == 1) {
        node.split = node.bounds.centerY();
    } else {
        node.split = node.bounds.centerZ();
    }

    node.left         = std::make_unique<Node>();
    node.right        = std::make_unique<Node>();
    node.left->depth  = node.depth + 1;
    node.right->depth = node.depth + 1;
    node.left->axis   = node.left->depth % 3;
    node.right->axis  = node.right->depth % 3;

    const AABB3 &b = node.bounds;
    if (node.axis == 0) {
        node.left->bounds  = makeAABB3(b.minX, b.minY, b.minZ, node.split, b.maxY, b.maxZ);
        node.right->bounds = makeAABB3(node.split, b.minY, b.minZ, b.maxX, b.maxY, b.maxZ);
    } else if (node.axis == 1) {
        node.left->bounds  = makeAABB3(b.minX, b.minY, b.minZ, b.maxX, node.split, b.maxZ);
        node.right->bounds = makeAABB3(b.minX, node.split, b.minZ, b.maxX, b.maxY, b.maxZ);
    } else {
        node.left->bounds  = makeAABB3(b.minX, b.minY, b.minZ, b.maxX, b.maxY, node.split);
        node.right->bounds = makeAABB3(b.minX, b.minY, node.split, b.maxX, b.maxY, b.maxZ);
    }

    std::vector<int> remain;
    remain.reserve(node.itemIds.size());
    for (int id : node.itemIds) {
        auto it = items_.find(id);
        if (it == items_.end()) continue;
        const AABB3 &ib = it->second;
        float iMin = 0.f, iMax = 0.f;
        if (node.axis == 0) {
            iMin = ib.minX;
            iMax = ib.maxX;
        } else if (node.axis == 1) {
            iMin = ib.minY;
            iMax = ib.maxY;
        } else {
            iMin = ib.minZ;
            iMax = ib.maxZ;
        }
        if (iMax <= node.split) {
            node.left->itemIds.push_back(id);
        } else if (iMin >= node.split) {
            node.right->itemIds.push_back(id);
        } else {
            remain.push_back(id);
        }
    }
    node.itemIds.swap(remain);
}

void BSPTree3D::collect(Node &node, const AABB3 *box, float cx, float cy, float cz, float radius,
                        bool useSphere) {
    if (box && !node.bounds.intersectsAABB(*box)) return;
    if (useSphere && !node.bounds.intersectsSphere(cx, cy, cz, radius)) return;

    for (int id : node.itemIds) {
        auto it = items_.find(id);
        if (it == items_.end()) continue;
        const AABB3 &b = it->second;
        bool hit       = false;
        if (box) {
            hit = b.intersectsAABB(*box);
        } else if (useSphere) {
            hit = b.intersectsSphere(cx, cy, cz, radius);
        } else {
            hit = b.containsPoint(cx, cy, cz);
        }
        if (hit) results_.addUnique(id);
    }

    if (!node.isLeaf()) {
        if (node.left) collect(*node.left, box, cx, cy, cz, radius, useSphere);
        if (node.right) collect(*node.right, box, cx, cy, cz, radius, useSphere);
    }
}

int BSPTree3D::queryPoint(float x, float y, float z) {
    results_.clear();
    collect(*ensureRoot(), nullptr, x, y, z, 0.f, false);
    return results_.getCount();
}

int BSPTree3D::queryAABB(float minX, float minY, float minZ, float maxX, float maxY, float maxZ) {
    results_.clear();
    AABB3 box = makeAABB3(minX, minY, minZ, maxX, maxY, maxZ);
    collect(*ensureRoot(), &box, 0.f, 0.f, 0.f, 0.f, false);
    return results_.getCount();
}

int BSPTree3D::querySphere(float cx, float cy, float cz, float radius) {
    results_.clear();
    if (radius < 0.f) radius = 0.f;
    collect(*ensureRoot(), nullptr, cx, cy, cz, radius, true);
    return results_.getCount();
}

}  // namespace eve::spatial
