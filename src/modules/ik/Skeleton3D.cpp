#include "ik/Skeleton3D.h"

#include "common/Exception.h"

namespace eve::ik {

Skeleton3D::Skeleton3D() {
    ensureSorted();
    ensureStateSize();
}

void Skeleton3D::ensureSorted() {
    if (sk_.bones().empty()) {
        sk_.topologicalSort();
    }
}

void Skeleton3D::ensureStateSize() {
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

::ik::bone3d *Skeleton3D::boneAt(int boneId) const {
    if (boneId < 0) return nullptr;
    const auto &bones = sk_.bones();
    if (static_cast<size_t>(boneId) >= bones.size()) return nullptr;
    return bones[static_cast<size_t>(boneId)];
}

void Skeleton3D::requireBone(int boneId) const {
    if (!boneAt(boneId)) {
        throw Exception("Skeleton3D: invalid bone id %d", boneId);
    }
}

int Skeleton3D::createBone(int parentId, float length) {
    ensureSorted();
    requireBone(parentId);
    if (length <= 0.f) {
        throw Exception("Skeleton3D.createBone: length must be > 0");
    }
    ::ik::bone3d *parent = boneAt(parentId);
    ::ik::bone3d *child  = parent->createBone(length);
    sk_.topologicalSort();
    ensureStateSize();
    return static_cast<int>(child->id);
}

int Skeleton3D::getBoneCount() const {
    const_cast<Skeleton3D *>(this)->ensureSorted();
    return static_cast<int>(sk_.bones().size());
}

int Skeleton3D::getParent(int boneId) const {
    requireBone(boneId);
    ::ik::bone3d *b = boneAt(boneId);
    if (!b->parent) return -1;
    return static_cast<int>(b->parent->id);
}

int Skeleton3D::getChildCount(int boneId) const {
    requireBone(boneId);
    return static_cast<int>(boneAt(boneId)->children.size());
}

int Skeleton3D::getChild(int boneId, int childIndex) const {
    requireBone(boneId);
    ::ik::bone3d *b = boneAt(boneId);
    if (childIndex < 0 || static_cast<size_t>(childIndex) >= b->children.size()) {
        throw Exception("Skeleton3D.getChild: childIndex out of range");
    }
    return static_cast<int>(b->children[static_cast<size_t>(childIndex)]->id);
}

float Skeleton3D::getLength(int boneId) const {
    requireBone(boneId);
    return boneAt(boneId)->length;
}

void Skeleton3D::setLength(int boneId, float length) {
    requireBone(boneId);
    if (boneId == 0) {
        throw Exception("Skeleton3D.setLength: root length is always 0");
    }
    if (length <= 0.f) {
        throw Exception("Skeleton3D.setLength: length must be > 0");
    }
    boneAt(boneId)->setLength(length);
}

void Skeleton3D::setPosition(int boneId, float x, float y, float z) {
    requireBone(boneId);
    ensureStateSize();
    boneAt(boneId)->position(state_) = {x, y, z};
}

float Skeleton3D::getX(int boneId) const {
    requireBone(boneId);
    const_cast<Skeleton3D *>(this)->ensureStateSize();
    return boneAt(boneId)->position(state_)[0];
}

float Skeleton3D::getY(int boneId) const {
    requireBone(boneId);
    const_cast<Skeleton3D *>(this)->ensureStateSize();
    return boneAt(boneId)->position(state_)[1];
}

float Skeleton3D::getZ(int boneId) const {
    requireBone(boneId);
    const_cast<Skeleton3D *>(this)->ensureStateSize();
    return boneAt(boneId)->position(state_)[2];
}

void Skeleton3D::setOrientation(int boneId, float x, float y, float z) {
    requireBone(boneId);
    ensureStateSize();
    boneAt(boneId)->orientation(state_) = {x, y, z};
}

float Skeleton3D::getOrientationX(int boneId) const {
    requireBone(boneId);
    const_cast<Skeleton3D *>(this)->ensureStateSize();
    return boneAt(boneId)->orientation(state_)[0];
}

float Skeleton3D::getOrientationY(int boneId) const {
    requireBone(boneId);
    const_cast<Skeleton3D *>(this)->ensureStateSize();
    return boneAt(boneId)->orientation(state_)[1];
}

float Skeleton3D::getOrientationZ(int boneId) const {
    requireBone(boneId);
    const_cast<Skeleton3D *>(this)->ensureStateSize();
    return boneAt(boneId)->orientation(state_)[2];
}

void Skeleton3D::setRotation(int boneId, float yaw, float pitch) {
    requireBone(boneId);
    ensureStateSize();
    boneAt(boneId)->rotation(state_) = {yaw, pitch};
}

float Skeleton3D::getRotationYaw(int boneId) const {
    requireBone(boneId);
    const_cast<Skeleton3D *>(this)->ensureStateSize();
    return boneAt(boneId)->rotation(state_)[0];
}

float Skeleton3D::getRotationPitch(int boneId) const {
    requireBone(boneId);
    const_cast<Skeleton3D *>(this)->ensureStateSize();
    return boneAt(boneId)->rotation(state_)[1];
}

void Skeleton3D::setConstraints(int boneId, float minYaw, float minPitch, float maxYaw,
                                float maxPitch) {
    requireBone(boneId);
    if (minYaw > maxYaw || minPitch > maxPitch) {
        throw Exception("Skeleton3D.setConstraints: min must be <= max per axis");
    }
    boneAt(boneId)->setConstraints(::ik::vec2{minYaw, minPitch},
                                   ::ik::vec2{maxYaw, maxPitch});
}

void Skeleton3D::clearConstraints(int boneId) {
    requireBone(boneId);
    boneAt(boneId)->clearConstraints();
}

bool Skeleton3D::hasConstraints(int boneId) const {
    requireBone(boneId);
    return boneAt(boneId)->constrained;
}

void Skeleton3D::initStraightPose(float rootX, float rootY, float rootZ) {
    ensureSorted();
    ensureStateSize();
    sk_.init_straight_pose(state_, ::ik::vec3{rootX, rootY, rootZ});
}

void Skeleton3D::bind() {
    ensureSorted();
    ensureStateSize();
    sk_.bind(state_);
}

void Skeleton3D::forwardKinematics() {
    ensureSorted();
    ensureStateSize();
    sk_.forward_kinematics(state_);
}

void Skeleton3D::updateRotations() {
    ensureSorted();
    ensureStateSize();
    sk_.update_rotations(state_);
}

float Skeleton3D::totalLengthTo(int boneId) const {
    requireBone(boneId);
    return sk_.total_length_to(boneAt(boneId));
}

}  // namespace eve::ik
