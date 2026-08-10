#include "spatial/Octree.h"

#include "common/Exception.h"

namespace eve::spatial {

Octree::Octree(float minX, float minY, float minZ, float maxX, float maxY, float maxZ,
               int maxDepth, int maxPerNode)
    : rootBounds_(makeAABB3(minX, minY, minZ, maxX, maxY, maxZ)),
      maxDepth_(std::max(1, maxDepth)),
      maxPerNode_(std::max(1, maxPerNode)) {
    if (!rootBounds_.valid() || rootBounds_.width() <= 0.f || rootBounds_.height() <= 0.f ||
        rootBounds_.depth() <= 0.f) {
        throw Exception("Octree: bounds must have positive volume");
    }
    ensureRoot();
}

Octree::Node *Octree::ensureRoot() {
    if (!root_) {
        root_         = std::make_unique<Node>();
        root_->bounds = rootBounds_;
        root_->depth  = 0;
    }
    return root_.get();
}

void Octree::clear() {
    items_.clear();
    results_.clear();
    root_.reset();
    ensureRoot();
}

bool Octree::contains(int id) const { return items_.find(id) != items_.end(); }

bool Octree::insert(int id, float minX, float minY, float minZ, float maxX, float maxY,
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

bool Octree::remove(int id) {
    if (!contains(id)) return false;
    items_.erase(id);
    rebuild();
    return true;
}

bool Octree::update(int id, float minX, float minY, float minZ, float maxX, float maxY,
                    float maxZ) {
    if (!contains(id)) return false;
    return insert(id, minX, minY, minZ, maxX, maxY, maxZ);
}

void Octree::rebuild() {
    results_.clear();
    root_.reset();
    ensureRoot();
    for (const auto &kv : items_) {
        if (!insertInto(*root_, kv.first, kv.second)) {
            root_->itemIds.push_back(kv.first);
        }
    }
}

bool Octree::insertInto(Node &node, int id, const AABB3 &bounds) {
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

    for (int i = 0; i < 8; ++i) {
        if (node.children[i] && node.children[i]->bounds.containsAABB(bounds)) {
            return insertInto(*node.children[i], id, bounds);
        }
    }
    node.itemIds.push_back(id);
    return true;
}

void Octree::split(Node &node) {
    const float mx = node.bounds.centerX();
    const float my = node.bounds.centerY();
    const float mz = node.bounds.centerZ();
    const float x0 = node.bounds.minX, x1 = node.bounds.maxX;
    const float y0 = node.bounds.minY, y1 = node.bounds.maxY;
    const float z0 = node.bounds.minZ, z1 = node.bounds.maxZ;
    const AABB3 octs[8] = {
        makeAABB3(x0, y0, z0, mx, my, mz), makeAABB3(mx, y0, z0, x1, my, mz),
        makeAABB3(x0, my, z0, mx, y1, mz), makeAABB3(mx, my, z0, x1, y1, mz),
        makeAABB3(x0, y0, mz, mx, my, z1), makeAABB3(mx, y0, mz, x1, my, z1),
        makeAABB3(x0, my, mz, mx, y1, z1), makeAABB3(mx, my, mz, x1, y1, z1),
    };
    for (int i = 0; i < 8; ++i) {
        node.children[i]         = std::make_unique<Node>();
        node.children[i]->bounds = octs[i];
        node.children[i]->depth  = node.depth + 1;
    }

    std::vector<int> remain;
    remain.reserve(node.itemIds.size());
    for (int id : node.itemIds) {
        auto it = items_.find(id);
        if (it == items_.end()) continue;
        bool placed = false;
        for (int i = 0; i < 8; ++i) {
            if (node.children[i]->bounds.containsAABB(it->second)) {
                node.children[i]->itemIds.push_back(id);
                placed = true;
                break;
            }
        }
        if (!placed) remain.push_back(id);
    }
    node.itemIds.swap(remain);
}

void Octree::collect(Node &node, const AABB3 *box, float cx, float cy, float cz, float radius,
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
        for (int i = 0; i < 8; ++i) {
            if (node.children[i]) collect(*node.children[i], box, cx, cy, cz, radius, useSphere);
        }
    }
}

int Octree::queryPoint(float x, float y, float z) {
    results_.clear();
    collect(*ensureRoot(), nullptr, x, y, z, 0.f, false);
    return results_.getCount();
}

int Octree::queryAABB(float minX, float minY, float minZ, float maxX, float maxY, float maxZ) {
    results_.clear();
    AABB3 box = makeAABB3(minX, minY, minZ, maxX, maxY, maxZ);
    collect(*ensureRoot(), &box, 0.f, 0.f, 0.f, 0.f, false);
    return results_.getCount();
}

int Octree::querySphere(float cx, float cy, float cz, float radius) {
    results_.clear();
    if (radius < 0.f) radius = 0.f;
    collect(*ensureRoot(), nullptr, cx, cy, cz, radius, true);
    return results_.getCount();
}

}  // namespace eve::spatial
