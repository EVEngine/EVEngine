#include "ik/Skeleton2D.h"

#include "common/Exception.h"

namespace eve::ik {

Skeleton2D::Skeleton2D() {
    ensureSorted();
    ensureStateSize();
}

void Skeleton2D::ensureSorted() {
    if (sk_.bones().empty()) {
        sk_.topologicalSort();
    }
}

void Skeleton2D::ensureStateSize() {
    ensureSorted();
    const unsigned n = static_cast<unsigned>(sk_.bones().size());
    if (state_.size() < n) {
        // Grow in place so existing joint poses survive createBone().
        state_.position_.resize(n);
        state_.orientation_.resize(n);
        state_.scale_.resize(n);
        state_.rotation_.resize(n);
    }
}

::ik::bone2d *Skeleton2D::boneAt(int boneId) const {
    if (boneId < 0) return nullptr;
    const auto &bones = sk_.bones();
    if (static_cast<size_t>(boneId) >= bones.size()) return nullptr;
    return bones[static_cast<size_t>(boneId)];
}

void Skeleton2D::requireBone(int boneId) const {
    if (!boneAt(boneId)) {
        throw Exception("Skeleton2D: invalid bone id %d", boneId);
    }
}

int Skeleton2D::createBone(int parentId, float length) {
    ensureSorted();
    requireBone(parentId);
    if (length <= 0.f) {
        throw Exception("Skeleton2D.createBone: length must be > 0");
    }
    ::ik::bone2d *parent = boneAt(parentId);
    ::ik::bone2d *child  = parent->createBone(length);
    sk_.topologicalSort();
    ensureStateSize();
    return static_cast<int>(child->id);
}

int Skeleton2D::getBoneCount() const {
    const_cast<Skeleton2D *>(this)->ensureSorted();
    return static_cast<int>(sk_.bones().size());
}

int Skeleton2D::getParent(int boneId) const {
    requireBone(boneId);
    ::ik::bone2d *b = boneAt(boneId);
    if (!b->parent) return -1;
    return static_cast<int>(b->parent->id);
}

int Skeleton2D::getChildCount(int boneId) const {
    requireBone(boneId);
    return static_cast<int>(boneAt(boneId)->children.size());
}

int Skeleton2D::getChild(int boneId, int childIndex) const {
    requireBone(boneId);
    ::ik::bone2d *b = boneAt(boneId);
    if (childIndex < 0 || static_cast<size_t>(childIndex) >= b->children.size()) {
        throw Exception("Skeleton2D.getChild: childIndex out of range");
    }
    return static_cast<int>(b->children[static_cast<size_t>(childIndex)]->id);
}

float Skeleton2D::getLength(int boneId) const {
    requireBone(boneId);
    return boneAt(boneId)->length;
}

void Skeleton2D::setLength(int boneId, float length) {
    requireBone(boneId);
    if (boneId == 0) {
        throw Exception("Skeleton2D.setLength: root length is always 0");
    }
    if (length <= 0.f) {
        throw Exception("Skeleton2D.setLength: length must be > 0");
    }
    boneAt(boneId)->setLength(length);
}

void Skeleton2D::setPosition(int boneId, float x, float y) {
    requireBone(boneId);
    ensureStateSize();
    boneAt(boneId)->position(state_) = {x, y};
}

float Skeleton2D::getX(int boneId) const {
    requireBone(boneId);
    const_cast<Skeleton2D *>(this)->ensureStateSize();
    return boneAt(boneId)->position(state_)[0];
}

float Skeleton2D::getY(int boneId) const {
    requireBone(boneId);
    const_cast<Skeleton2D *>(this)->ensureStateSize();
    return boneAt(boneId)->position(state_)[1];
}

void Skeleton2D::setOrientation(int boneId, float x, float y) {
    requireBone(boneId);
    ensureStateSize();
    boneAt(boneId)->orientation(state_) = {x, y};
}

float Skeleton2D::getOrientationX(int boneId) const {
    requireBone(boneId);
    const_cast<Skeleton2D *>(this)->ensureStateSize();
    return boneAt(boneId)->orientation(state_)[0];
}

float Skeleton2D::getOrientationY(int boneId) const {
    requireBone(boneId);
    const_cast<Skeleton2D *>(this)->ensureStateSize();
    return boneAt(boneId)->orientation(state_)[1];
}

void Skeleton2D::setRotation(int boneId, float angle) {
    requireBone(boneId);
    ensureStateSize();
    boneAt(boneId)->rotation(state_) = {angle};
}

float Skeleton2D::getRotation(int boneId) const {
    requireBone(boneId);
    const_cast<Skeleton2D *>(this)->ensureStateSize();
    return boneAt(boneId)->rotation(state_)[0];
}

void Skeleton2D::setConstraints(int boneId, float minAngle, float maxAngle) {
    requireBone(boneId);
    if (minAngle > maxAngle) {
        throw Exception("Skeleton2D.setConstraints: minAngle must be <= maxAngle");
    }
    boneAt(boneId)->setConstraints(::ik::vec<float, 1>{minAngle},
                                   ::ik::vec<float, 1>{maxAngle});
}

void Skeleton2D::clearConstraints(int boneId) {
    requireBone(boneId);
    boneAt(boneId)->clearConstraints();
}

bool Skeleton2D::hasConstraints(int boneId) const {
    requireBone(boneId);
    return boneAt(boneId)->constrained;
}

void Skeleton2D::initStraightPose(float rootX, float rootY) {
    ensureSorted();
    ensureStateSize();
    sk_.init_straight_pose(state_, ::ik::vec2{rootX, rootY});
}

void Skeleton2D::bind() {
    ensureSorted();
    ensureStateSize();
    sk_.bind(state_);
}

void Skeleton2D::forwardKinematics() {
    ensureSorted();
    ensureStateSize();
    sk_.forward_kinematics(state_);
}

void Skeleton2D::updateRotations() {
    ensureSorted();
    ensureStateSize();
    sk_.update_rotations(state_);
}

float Skeleton2D::totalLengthTo(int boneId) const {
    requireBone(boneId);
    return sk_.total_length_to(boneAt(boneId));
}

}  // namespace eve::ik
