#include "animation/AnimGraph.h"

#include "animation/AnimClip.h"
#include "animation/AnimSkeleton.h"
#include "common/Exception.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace eve::animation {

AnimGraph::AnimGraph(AnimSkeleton* skeleton) : skeleton_(skeleton) {
    if (!skeleton_) throw Exception("AnimGraph: skeleton is null");
    output_.resize(skeleton_->getBoneCount());
    scratch_.resize(skeleton_->getBoneCount());
}

int AnimGraph::addNode(Kind kind) {
    nodes_.emplace_back();
    Node& node = nodes_.back();
    node.kind  = kind;
    node.mask.assign(static_cast<size_t>(skeleton_->getBoneCount()), 1.f);
    node.cache.resize(skeleton_->getBoneCount());
    return static_cast<int>(nodes_.size()) - 1;
}

AnimGraph::Node& AnimGraph::requireNode(int node) {
    if (node < 0 || node >= getNodeCount()) throw Exception("AnimGraph: invalid node %d", node);
    return nodes_[static_cast<size_t>(node)];
}
const AnimGraph::Node& AnimGraph::requireNode(int node) const {
    if (node < 0 || node >= getNodeCount()) throw Exception("AnimGraph: invalid node %d", node);
    return nodes_[static_cast<size_t>(node)];
}
void AnimGraph::requireChild(int child) const { (void)requireNode(child); }

int AnimGraph::addClip(AnimClip* clip) {
    if (!clip) throw Exception("AnimGraph.addClip: clip is null");
    const int id                         = addNode(Kind::Clip);
    nodes_[static_cast<size_t>(id)].clip = clip;
    return id;
}
int AnimGraph::addBlend(int a, int b, float weight) {
    requireChild(a);
    requireChild(b);
    const int id = addNode(Kind::Blend);
    Node&     n  = nodes_[static_cast<size_t>(id)];
    n.a          = a;
    n.b          = b;
    n.weight     = clampf(weight, 0.f, 1.f);
    return id;
}
int AnimGraph::addAdditive(int base, int additive, float weight) {
    requireChild(base);
    requireChild(additive);
    const int id = addNode(Kind::Additive);
    Node&     n  = nodes_[static_cast<size_t>(id)];
    n.a          = base;
    n.b          = additive;
    n.weight     = clampf(weight, 0.f, 1.f);
    return id;
}
int AnimGraph::addLayer(int base, int overlay, float weight) {
    requireChild(base);
    requireChild(overlay);
    const int id = addNode(Kind::Layer);
    Node&     n  = nodes_[static_cast<size_t>(id)];
    n.a          = base;
    n.b          = overlay;
    n.weight     = clampf(weight, 0.f, 1.f);
    std::fill(n.mask.begin(), n.mask.end(), 0.f);
    return id;
}
int AnimGraph::addOneShot(int base, int shot, float fadeIn, float fadeOut) {
    requireChild(base);
    requireChild(shot);
    if (fadeIn < 0.f || fadeOut < 0.f) throw Exception("AnimGraph.addOneShot: fades must be >= 0");
    const int id = addNode(Kind::OneShot);
    Node&     n  = nodes_[static_cast<size_t>(id)];
    n.a          = base;
    n.b          = shot;
    n.fadeIn     = fadeIn;
    n.fadeOut    = fadeOut;
    return id;
}
int AnimGraph::addBlendSpace1D() { return addNode(Kind::BlendSpace1D); }
int AnimGraph::addBlendSpace2D() { return addNode(Kind::BlendSpace2D); }

void AnimGraph::addBlendSpace1DPoint(int node, float x, int child) {
    Node& n = requireNode(node);
    requireChild(child);
    if (n.kind != Kind::BlendSpace1D) throw Exception("AnimGraph: node is not BlendSpace1D");
    n.points.push_back({x, 0.f, child});
    std::sort(n.points.begin(), n.points.end(), [](const Point& a, const Point& b) { return a.x < b.x; });
}
void AnimGraph::addBlendSpace2DPoint(int node, float x, float y, int child) {
    Node& n = requireNode(node);
    requireChild(child);
    if (n.kind != Kind::BlendSpace2D) throw Exception("AnimGraph: node is not BlendSpace2D");
    n.points.push_back({x, y, child});
}

void AnimGraph::setBoneMask(int node, int boneIndex, float weight, bool includeChildren) {
    Node& n = requireNode(node);
    if (boneIndex < 0 || boneIndex >= skeleton_->getBoneCount())
        throw Exception("AnimGraph: invalid bone %d", boneIndex);
    n.mask[static_cast<size_t>(boneIndex)] = clampf(weight, 0.f, 1.f);
    if (includeChildren) {
        for (int i = boneIndex + 1; i < skeleton_->getBoneCount(); ++i) {
            int p = skeleton_->getParent(i);
            while (p >= 0 && p != boneIndex) p = skeleton_->getParent(p);
            if (p == boneIndex) n.mask[static_cast<size_t>(i)] = clampf(weight, 0.f, 1.f);
        }
    }
}
void AnimGraph::clearBoneMask(int node) {
    std::fill(requireNode(node).mask.begin(), requireNode(node).mask.end(), 0.f);
}
void AnimGraph::setRoot(int node) {
    requireChild(node);
    root_ = node;
}
void AnimGraph::setWeight(int node, float weight) { requireNode(node).weight = clampf(weight, 0.f, 1.f); }
void AnimGraph::setPosition1D(int node, float x) { requireNode(node).x = x; }
void AnimGraph::setPosition2D(int node, float x, float y) {
    Node& n = requireNode(node);
    n.x     = x;
    n.y     = y;
}
void AnimGraph::setSpeed(int node, float speed) {
    Node& n = requireNode(node);
    if (n.kind != Kind::Clip) throw Exception("AnimGraph.setSpeed: node is not a clip");
    n.speed = speed;
}
void AnimGraph::trigger(int node) {
    Node& n = requireNode(node);
    if (n.kind != Kind::OneShot) throw Exception("AnimGraph.trigger: node is not one-shot");
    n.active              = true;
    n.time                = 0.f;
    requireNode(n.b).time = 0.f;
}
bool AnimGraph::isOneShotActive(int node) const { return requireNode(node).active; }

void AnimGraph::blendMasked(AnimPose& out, const AnimPose& base, const AnimPose& overlay, float weight,
                            const std::vector<float>* mask) const {
    out.resize(base.getBoneCount());
    for (int i = 0; i < base.getBoneCount(); ++i) {
        const float w = clampf(weight * (mask ? (*mask)[static_cast<size_t>(i)] : 1.f), 0.f, 1.f);
        out.local(i)  = blendTRS(base.local(i), overlay.local(i), w);
    }
}

void AnimGraph::applyAdditive(AnimPose& out, const AnimPose& base, const AnimPose& delta, float weight,
                              const std::vector<float>* mask) const {
    out.resize(base.getBoneCount());
    for (int i = 0; i < base.getBoneCount(); ++i) {
        const float         w = clampf(weight * (mask ? (*mask)[static_cast<size_t>(i)] : 1.f), 0.f, 1.f);
        const TransformTRS& a = base.local(i);
        const TransformTRS& d = delta.local(i);
        TransformTRS        r = a;
        r.px += d.px * w;
        r.py += d.py * w;
        r.pz += d.pz * w;
        r.sx *= lerpf(1.f, d.sx, w);
        r.sy *= lerpf(1.f, d.sy, w);
        r.sz *= lerpf(1.f, d.sz, w);
        float qx, qy, qz, qw;
        slerpQuat(0.f, 0.f, 0.f, 1.f, d.qx, d.qy, d.qz, d.qw, w, qx, qy, qz, qw);
        r.qx = a.qw * qx + a.qx * qw + a.qy * qz - a.qz * qy;
        r.qy = a.qw * qy - a.qx * qz + a.qy * qw + a.qz * qx;
        r.qz = a.qw * qz + a.qx * qy - a.qy * qx + a.qz * qw;
        r.qw = a.qw * qw - a.qx * qx - a.qy * qy - a.qz * qz;
        r.normalizeRotation();
        out.local(i) = r;
    }
}

const AnimPose& AnimGraph::evaluateBlendSpace(Node& node, bool twoDimensional) {
    if (node.points.empty()) {
        skeleton_->applyBindPose(&node.cache);
        return node.cache;
    }
    if (node.points.size() == 1) {
        node.cache.copyFrom(&evaluate(node.points.front().child));
        return node.cache;
    }
    if (!twoDimensional) {
        if (node.x <= node.points.front().x) {
            node.cache.copyFrom(&evaluate(node.points.front().child));
            return node.cache;
        }
        if (node.x >= node.points.back().x) {
            node.cache.copyFrom(&evaluate(node.points.back().child));
            return node.cache;
        }
        for (size_t i = 0; i + 1 < node.points.size(); ++i) {
            const Point &a = node.points[i], &b = node.points[i + 1];
            if (node.x >= a.x && node.x <= b.x) {
                blendMasked(node.cache, evaluate(a.child), evaluate(b.child), (node.x - a.x) / (b.x - a.x), nullptr);
                return node.cache;
            }
        }
    }
    // Inverse-distance weights are stable outside the convex hull and degrade
    // gracefully for authoring-time sparse 2D spaces.
    std::vector<float> weights(node.points.size());
    float              sum = 0.f;
    for (size_t i = 0; i < node.points.size(); ++i) {
        const float dx = node.x - node.points[i].x, dy = node.y - node.points[i].y;
        weights[i] = 1.f / std::max(1e-6f, dx * dx + dy * dy);
        sum += weights[i];
    }
    node.cache.copyFrom(&evaluate(node.points.front().child));
    float accumulated = weights[0] / sum;
    for (size_t i = 1; i < node.points.size(); ++i) {
        const float w        = weights[i] / sum;
        const float relative = w / (accumulated + w);
        scratch_.copyFrom(&node.cache);
        blendMasked(node.cache, scratch_, evaluate(node.points[i].child), relative, nullptr);
        accumulated += w;
    }
    return node.cache;
}

const AnimPose& AnimGraph::evaluate(int id) {
    Node& node = requireNode(id);
    if (node.cacheGeneration == generation_) return node.cache;
    node.cacheGeneration = generation_;
    switch (node.kind) {
        case Kind::Clip: node.clip->sample(node.time, &node.cache, skeleton_); break;
        case Kind::Blend: blendMasked(node.cache, evaluate(node.a), evaluate(node.b), node.weight, nullptr); break;
        case Kind::Layer: blendMasked(node.cache, evaluate(node.a), evaluate(node.b), node.weight, &node.mask); break;
        case Kind::Additive:
            applyAdditive(node.cache, evaluate(node.a), evaluate(node.b), node.weight, &node.mask);
            break;
        case Kind::OneShot: {
            const AnimPose& base = evaluate(node.a);
            if (!node.active)
                node.cache.copyFrom(&base);
            else {
                const float duration = requireNode(node.b).clip ? requireNode(node.b).clip->getDuration() : 0.f;
                float       w        = 1.f;
                if (node.fadeIn > 0.f) w = std::min(w, node.time / node.fadeIn);
                if (node.fadeOut > 0.f) w = std::min(w, (duration - node.time) / node.fadeOut);
                blendMasked(node.cache, base, evaluate(node.b), clampf(w, 0.f, 1.f), &node.mask);
            }
            break;
        }
        case Kind::BlendSpace1D: return evaluateBlendSpace(node, false);
        case Kind::BlendSpace2D: return evaluateBlendSpace(node, true);
    }
    return node.cache;
}

void AnimGraph::update(float dt) {
    if (dt < 0.f) throw Exception("AnimGraph.update: dt must be >= 0");
    if (root_ < 0) throw Exception("AnimGraph.update: root is not set");
    for (Node& node : nodes_) {
        if (node.kind == Kind::Clip) node.time = node.clip->wrapTime(node.time + dt * node.speed);
        if (node.kind == Kind::OneShot && node.active) {
            node.time += dt;
            const Node& shot = requireNode(node.b);
            if (shot.clip && node.time >= shot.clip->getDuration()) node.active = false;
        }
    }
    if (++generation_ == 0) {
        generation_ = 1;
        for (Node& n : nodes_) n.cacheGeneration = 0;
    }
    output_.copyFrom(&evaluate(root_));
}

}  // namespace eve::animation
