#include "spatial/BSPTree2D.h"

#include "common/Exception.h"

namespace eve::spatial {

BSPTree2D::BSPTree2D(float minX, float minY, float maxX, float maxY, int maxDepth, int maxPerNode)
    : rootBounds_(makeAABB2(minX, minY, maxX, maxY)),
      maxDepth_(std::max(1, maxDepth)),
      maxPerNode_(std::max(1, maxPerNode)) {
    if (!rootBounds_.valid() || rootBounds_.width() <= 0.f || rootBounds_.height() <= 0.f) {
        throw Exception("BSPTree2D: bounds must have positive area");
    }
    ensureRoot();
}

BSPTree2D::Node *BSPTree2D::ensureRoot() {
    if (!root_) {
        root_         = std::make_unique<Node>();
        root_->bounds = rootBounds_;
        root_->depth  = 0;
        root_->axis   = 0;
    }
    return root_.get();
}

void BSPTree2D::clear() {
    items_.clear();
    results_.clear();
    root_.reset();
    ensureRoot();
}

bool BSPTree2D::contains(int id) const { return items_.find(id) != items_.end(); }

bool BSPTree2D::insert(int id, float minX, float minY, float maxX, float maxY) {
    AABB2 b = makeAABB2(minX, minY, maxX, maxY);
    if (!b.valid()) return false;
    if (contains(id)) remove(id);
    items_[id] = b;
    if (!insertInto(*ensureRoot(), id, b)) {
        ensureRoot()->itemIds.push_back(id);
    }
    return true;
}

bool BSPTree2D::remove(int id) {
    if (!contains(id)) return false;
    items_.erase(id);
    rebuild();
    return true;
}

bool BSPTree2D::update(int id, float minX, float minY, float maxX, float maxY) {
    if (!contains(id)) return false;
    return insert(id, minX, minY, maxX, maxY);
}

void BSPTree2D::rebuild() {
    results_.clear();
    root_.reset();
    ensureRoot();
    for (const auto &kv : items_) {
        if (!insertInto(*root_, kv.first, kv.second)) {
            root_->itemIds.push_back(kv.first);
        }
    }
}

bool BSPTree2D::insertInto(Node &node, int id, const AABB2 &bounds) {
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

    // Fully on one side of the split plane?
    if (node.axis == 0) {
        if (bounds.maxX <= node.split && node.left) {
            return insertInto(*node.left, id, bounds);
        }
        if (bounds.minX >= node.split && node.right) {
            return insertInto(*node.right, id, bounds);
        }
    } else {
        if (bounds.maxY <= node.split && node.left) {
            return insertInto(*node.left, id, bounds);
        }
        if (bounds.minY >= node.split && node.right) {
            return insertInto(*node.right, id, bounds);
        }
    }
    node.itemIds.push_back(id);
    return true;
}

void BSPTree2D::split(Node &node) {
    node.axis  = node.depth % 2;
    node.split = (node.axis == 0) ? node.bounds.centerX() : node.bounds.centerY();

    node.left          = std::make_unique<Node>();
    node.right         = std::make_unique<Node>();
    node.left->depth   = node.depth + 1;
    node.right->depth  = node.depth + 1;
    node.left->axis    = node.left->depth % 2;
    node.right->axis   = node.right->depth % 2;

    if (node.axis == 0) {
        node.left->bounds =
            makeAABB2(node.bounds.minX, node.bounds.minY, node.split, node.bounds.maxY);
        node.right->bounds =
            makeAABB2(node.split, node.bounds.minY, node.bounds.maxX, node.bounds.maxY);
    } else {
        node.left->bounds =
            makeAABB2(node.bounds.minX, node.bounds.minY, node.bounds.maxX, node.split);
        node.right->bounds =
            makeAABB2(node.bounds.minX, node.split, node.bounds.maxX, node.bounds.maxY);
    }

    std::vector<int> remain;
    remain.reserve(node.itemIds.size());
    for (int id : node.itemIds) {
        auto it = items_.find(id);
        if (it == items_.end()) continue;
        const AABB2 &b = it->second;
        if (node.axis == 0) {
            if (b.maxX <= node.split) {
                node.left->itemIds.push_back(id);
            } else if (b.minX >= node.split) {
                node.right->itemIds.push_back(id);
            } else {
                remain.push_back(id);
            }
        } else {
            if (b.maxY <= node.split) {
                node.left->itemIds.push_back(id);
            } else if (b.minY >= node.split) {
                node.right->itemIds.push_back(id);
            } else {
                remain.push_back(id);
            }
        }
    }
    node.itemIds.swap(remain);
}

void BSPTree2D::collect(Node &node, const AABB2 *rect, float cx, float cy, float radius,
                        bool useCircle) {
    if (rect && !node.bounds.intersectsAABB(*rect)) return;
    if (useCircle && !node.bounds.intersectsCircle(cx, cy, radius)) return;

    for (int id : node.itemIds) {
        auto it = items_.find(id);
        if (it == items_.end()) continue;
        const AABB2 &b = it->second;
        bool hit       = false;
        if (rect) {
            hit = b.intersectsAABB(*rect);
        } else if (useCircle) {
            hit = b.intersectsCircle(cx, cy, radius);
        } else {
            hit = b.containsPoint(cx, cy);
        }
        if (hit) results_.addUnique(id);
    }

    if (!node.isLeaf()) {
        if (node.left) collect(*node.left, rect, cx, cy, radius, useCircle);
        if (node.right) collect(*node.right, rect, cx, cy, radius, useCircle);
    }
}

int BSPTree2D::queryPoint(float x, float y) {
    results_.clear();
    collect(*ensureRoot(), nullptr, x, y, 0.f, false);
    return results_.getCount();
}

int BSPTree2D::queryRect(float minX, float minY, float maxX, float maxY) {
    results_.clear();
    AABB2 rect = makeAABB2(minX, minY, maxX, maxY);
    collect(*ensureRoot(), &rect, 0.f, 0.f, 0.f, false);
    return results_.getCount();
}

int BSPTree2D::queryCircle(float cx, float cy, float radius) {
    results_.clear();
    if (radius < 0.f) radius = 0.f;
    collect(*ensureRoot(), nullptr, cx, cy, radius, true);
    return results_.getCount();
}

}  // namespace eve::spatial
