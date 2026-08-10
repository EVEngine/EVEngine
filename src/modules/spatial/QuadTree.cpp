#include "spatial/QuadTree.h"

#include "common/Exception.h"

namespace eve::spatial {

QuadTree::QuadTree(float minX, float minY, float maxX, float maxY, int maxDepth, int maxPerNode)
    : rootBounds_(makeAABB2(minX, minY, maxX, maxY)),
      maxDepth_(std::max(1, maxDepth)),
      maxPerNode_(std::max(1, maxPerNode)) {
    if (!rootBounds_.valid() || rootBounds_.width() <= 0.f || rootBounds_.height() <= 0.f) {
        throw Exception("QuadTree: bounds must have positive area");
    }
    ensureRoot();
}

QuadTree::Node *QuadTree::ensureRoot() {
    if (!root_) {
        root_        = std::make_unique<Node>();
        root_->bounds = rootBounds_;
        root_->depth  = 0;
    }
    return root_.get();
}

void QuadTree::clear() {
    items_.clear();
    results_.clear();
    root_.reset();
    ensureRoot();
}

bool QuadTree::contains(int id) const { return items_.find(id) != items_.end(); }

bool QuadTree::insert(int id, float minX, float minY, float maxX, float maxY) {
    AABB2 b = makeAABB2(minX, minY, maxX, maxY);
    if (!b.valid()) return false;
    if (contains(id)) {
        remove(id);
    }
    items_[id] = b;
    if (!insertInto(*ensureRoot(), id, b)) {
        // Outside root or failed fit: keep in map but force root storage.
        ensureRoot()->itemIds.push_back(id);
    }
    return true;
}

bool QuadTree::remove(int id) {
    if (!contains(id)) return false;
    items_.erase(id);
    rebuild();
    return true;
}

bool QuadTree::update(int id, float minX, float minY, float maxX, float maxY) {
    if (!contains(id)) return false;
    return insert(id, minX, minY, maxX, maxY);
}

void QuadTree::rebuild() {
    results_.clear();
    root_.reset();
    ensureRoot();
    for (const auto &kv : items_) {
        if (!insertInto(*root_, kv.first, kv.second)) {
            root_->itemIds.push_back(kv.first);
        }
    }
}

bool QuadTree::insertInto(Node &node, int id, const AABB2 &bounds) {
    if (!node.bounds.containsAABB(bounds) && node.depth == 0) {
        // Allow root to hold items that extend slightly outside.
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

    for (int i = 0; i < 4; ++i) {
        if (node.children[i] && node.children[i]->bounds.containsAABB(bounds)) {
            return insertInto(*node.children[i], id, bounds);
        }
    }
    node.itemIds.push_back(id);
    return true;
}

void QuadTree::split(Node &node) {
    const float mx = node.bounds.centerX();
    const float my = node.bounds.centerY();
    const AABB2 quads[4] = {
        makeAABB2(node.bounds.minX, node.bounds.minY, mx, my),
        makeAABB2(mx, node.bounds.minY, node.bounds.maxX, my),
        makeAABB2(node.bounds.minX, my, mx, node.bounds.maxY),
        makeAABB2(mx, my, node.bounds.maxX, node.bounds.maxY),
    };
    for (int i = 0; i < 4; ++i) {
        node.children[i]         = std::make_unique<Node>();
        node.children[i]->bounds = quads[i];
        node.children[i]->depth  = node.depth + 1;
    }

    std::vector<int> remain;
    remain.reserve(node.itemIds.size());
    for (int id : node.itemIds) {
        auto it = items_.find(id);
        if (it == items_.end()) continue;
        bool placed = false;
        for (int i = 0; i < 4; ++i) {
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

void QuadTree::collect(Node &node, const AABB2 *rect, float cx, float cy, float radius,
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
        for (int i = 0; i < 4; ++i) {
            if (node.children[i]) collect(*node.children[i], rect, cx, cy, radius, useCircle);
        }
    }
}

int QuadTree::queryPoint(float x, float y) {
    results_.clear();
    collect(*ensureRoot(), nullptr, x, y, 0.f, false);
    return results_.getCount();
}

int QuadTree::queryRect(float minX, float minY, float maxX, float maxY) {
    results_.clear();
    AABB2 rect = makeAABB2(minX, minY, maxX, maxY);
    collect(*ensureRoot(), &rect, 0.f, 0.f, 0.f, false);
    return results_.getCount();
}

int QuadTree::queryCircle(float cx, float cy, float radius) {
    results_.clear();
    if (radius < 0.f) radius = 0.f;
    collect(*ensureRoot(), nullptr, cx, cy, radius, true);
    return results_.getCount();
}

}  // namespace eve::spatial
