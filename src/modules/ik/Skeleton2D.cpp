#include "ik/Skeleton2D.h"
#include "ik/Skeleton3D.h"
#include "ik/detail/SkeletonImpl.h"

#include "common/Exception.h"

#include <utility>

namespace eve::ik {

Skeleton2D::Skeleton2D() : impl_(std::make_unique<Impl>()) {
    impl_->ensureSorted();
    impl_->ensureStateSize();
}

Skeleton2D::~Skeleton2D() = default;

void Skeleton2D::requireBone(int boneId) const {
    if (!impl_->boneAt(boneId)) {
        throw Exception("Skeleton2D: invalid bone id %d", boneId);
    }
}

int Skeleton2D::createBone(int parentId, float length) {
    impl_->ensureSorted();
    requireBone(parentId);
    if (length <= 0.f) {
        throw Exception("Skeleton2D.createBone: length must be > 0");
    }
    ::ik::bone2d *parent = impl_->boneAt(parentId);
    ::ik::bone2d *child  = parent->createBone(length);
    impl_->sk.topologicalSort();
    impl_->ensureStateSize();
    return static_cast<int>(child->id);
}

int Skeleton2D::getBoneCount() const {
    const_cast<Skeleton2D *>(this)->impl_->ensureSorted();
    return static_cast<int>(impl_->sk.bones().size());
}

int Skeleton2D::getParent(int boneId) const {
    requireBone(boneId);
    ::ik::bone2d *b = impl_->boneAt(boneId);
    if (!b->parent) return -1;
    return static_cast<int>(b->parent->id);
}

int Skeleton2D::getChildCount(int boneId) const {
    requireBone(boneId);
    return static_cast<int>(impl_->boneAt(boneId)->children.size());
}

int Skeleton2D::getChild(int boneId, int childIndex) const {
    requireBone(boneId);
    ::ik::bone2d *b = impl_->boneAt(boneId);
    if (childIndex < 0 || static_cast<size_t>(childIndex) >= b->children.size()) {
        throw Exception("Skeleton2D.getChild: childIndex out of range");
    }
    return static_cast<int>(b->children[static_cast<size_t>(childIndex)]->id);
}

float Skeleton2D::getLength(int boneId) const {
    requireBone(boneId);
    return impl_->boneAt(boneId)->length;
}

void Skeleton2D::setLength(int boneId, float length) {
    requireBone(boneId);
    if (boneId == 0) {
        throw Exception("Skeleton2D.setLength: root length is always 0");
    }
    if (length <= 0.f) {
        throw Exception("Skeleton2D.setLength: length must be > 0");
    }
    impl_->boneAt(boneId)->setLength(length);
}

void Skeleton2D::setPosition(int boneId, float x, float y) {
    requireBone(boneId);
    impl_->ensureStateSize();
    impl_->boneAt(boneId)->position(impl_->state) = {x, y};
}

float Skeleton2D::getX(int boneId) const {
    requireBone(boneId);
    const_cast<Skeleton2D *>(this)->impl_->ensureStateSize();
    return impl_->boneAt(boneId)->position(impl_->state)[0];
}

float Skeleton2D::getY(int boneId) const {
    requireBone(boneId);
    const_cast<Skeleton2D *>(this)->impl_->ensureStateSize();
    return impl_->boneAt(boneId)->position(impl_->state)[1];
}

void Skeleton2D::setOrientation(int boneId, float x, float y) {
    requireBone(boneId);
    impl_->ensureStateSize();
    impl_->boneAt(boneId)->orientation(impl_->state) = {x, y};
}

float Skeleton2D::getOrientationX(int boneId) const {
    requireBone(boneId);
    const_cast<Skeleton2D *>(this)->impl_->ensureStateSize();
    return impl_->boneAt(boneId)->orientation(impl_->state)[0];
}

float Skeleton2D::getOrientationY(int boneId) const {
    requireBone(boneId);
    const_cast<Skeleton2D *>(this)->impl_->ensureStateSize();
    return impl_->boneAt(boneId)->orientation(impl_->state)[1];
}

void Skeleton2D::setRotation(int boneId, float angle) {
    requireBone(boneId);
    impl_->ensureStateSize();
    impl_->boneAt(boneId)->rotation(impl_->state) = {angle};
}

float Skeleton2D::getRotation(int boneId) const {
    requireBone(boneId);
    const_cast<Skeleton2D *>(this)->impl_->ensureStateSize();
    return impl_->boneAt(boneId)->rotation(impl_->state)[0];
}

void Skeleton2D::setConstraints(int boneId, float minAngle, float maxAngle) {
    requireBone(boneId);
    if (minAngle > maxAngle) {
        throw Exception("Skeleton2D.setConstraints: minAngle must be <= maxAngle");
    }
    impl_->boneAt(boneId)->setConstraints(::ik::vec<float, 1>{minAngle}, ::ik::vec<float, 1>{maxAngle});
}

void Skeleton2D::clearConstraints(int boneId) {
    requireBone(boneId);
    impl_->boneAt(boneId)->clearConstraints();
}

bool Skeleton2D::hasConstraints(int boneId) const {
    requireBone(boneId);
    return impl_->boneAt(boneId)->constrained;
}

void Skeleton2D::initStraightPose(float rootX, float rootY) {
    impl_->ensureSorted();
    impl_->ensureStateSize();
    impl_->sk.init_straight_pose(impl_->state, ::ik::vec2{rootX, rootY});
}

void Skeleton2D::bind() {
    impl_->ensureSorted();
    impl_->ensureStateSize();
    impl_->sk.bind(impl_->state);
}

void Skeleton2D::forwardKinematics() {
    impl_->ensureSorted();
    impl_->ensureStateSize();
    impl_->sk.forward_kinematics(impl_->state);
}

void Skeleton2D::updateRotations() {
    impl_->ensureSorted();
    impl_->ensureStateSize();
    impl_->sk.update_rotations(impl_->state);
}

float Skeleton2D::totalLengthTo(int boneId) const {
    requireBone(boneId);
    return impl_->sk.total_length_to(impl_->boneAt(boneId));
}

// ---------------------------------------------------------------------------
// Skeleton3D
// ---------------------------------------------------------------------------

Skeleton3D::Skeleton3D() : impl_(std::make_unique<Impl>()) {
    impl_->ensureSorted();
    impl_->ensureStateSize();
}

Skeleton3D::~Skeleton3D() = default;

void Skeleton3D::requireBone(int boneId) const {
    if (!impl_->boneAt(boneId)) {
        throw Exception("Skeleton3D: invalid bone id %d", boneId);
    }
}

int Skeleton3D::createBone(int parentId, float length) {
    impl_->ensureSorted();
    requireBone(parentId);
    if (length <= 0.f) {
        throw Exception("Skeleton3D.createBone: length must be > 0");
    }
    ::ik::bone3d *parent = impl_->boneAt(parentId);
    ::ik::bone3d *child  = parent->createBone(length);
    impl_->sk.topologicalSort();
    impl_->ensureStateSize();
    return static_cast<int>(child->id);
}

int Skeleton3D::getBoneCount() const {
    const_cast<Skeleton3D *>(this)->impl_->ensureSorted();
    return static_cast<int>(impl_->sk.bones().size());
}

int Skeleton3D::getParent(int boneId) const {
    requireBone(boneId);
    ::ik::bone3d *b = impl_->boneAt(boneId);
    if (!b->parent) return -1;
    return static_cast<int>(b->parent->id);
}

int Skeleton3D::getChildCount(int boneId) const {
    requireBone(boneId);
    return static_cast<int>(impl_->boneAt(boneId)->children.size());
}

int Skeleton3D::getChild(int boneId, int childIndex) const {
    requireBone(boneId);
    ::ik::bone3d *b = impl_->boneAt(boneId);
    if (childIndex < 0 || static_cast<size_t>(childIndex) >= b->children.size()) {
        throw Exception("Skeleton3D.getChild: childIndex out of range");
    }
    return static_cast<int>(b->children[static_cast<size_t>(childIndex)]->id);
}

float Skeleton3D::getLength(int boneId) const {
    requireBone(boneId);
    return impl_->boneAt(boneId)->length;
}

void Skeleton3D::setLength(int boneId, float length) {
    requireBone(boneId);
    if (boneId == 0) {
        throw Exception("Skeleton3D.setLength: root length is always 0");
    }
    if (length <= 0.f) {
        throw Exception("Skeleton3D.setLength: length must be > 0");
    }
    impl_->boneAt(boneId)->setLength(length);
}

void Skeleton3D::setPosition(int boneId, float x, float y, float z) {
    requireBone(boneId);
    impl_->ensureStateSize();
    impl_->boneAt(boneId)->position(impl_->state) = {x, y, z};
}

float Skeleton3D::getX(int boneId) const {
    requireBone(boneId);
    const_cast<Skeleton3D *>(this)->impl_->ensureStateSize();
    return impl_->boneAt(boneId)->position(impl_->state)[0];
}

float Skeleton3D::getY(int boneId) const {
    requireBone(boneId);
    const_cast<Skeleton3D *>(this)->impl_->ensureStateSize();
    return impl_->boneAt(boneId)->position(impl_->state)[1];
}

float Skeleton3D::getZ(int boneId) const {
    requireBone(boneId);
    const_cast<Skeleton3D *>(this)->impl_->ensureStateSize();
    return impl_->boneAt(boneId)->position(impl_->state)[2];
}

void Skeleton3D::setOrientation(int boneId, float x, float y, float z) {
    requireBone(boneId);
    impl_->ensureStateSize();
    impl_->boneAt(boneId)->orientation(impl_->state) = {x, y, z};
}

float Skeleton3D::getOrientationX(int boneId) const {
    requireBone(boneId);
    const_cast<Skeleton3D *>(this)->impl_->ensureStateSize();
    return impl_->boneAt(boneId)->orientation(impl_->state)[0];
}

float Skeleton3D::getOrientationY(int boneId) const {
    requireBone(boneId);
    const_cast<Skeleton3D *>(this)->impl_->ensureStateSize();
    return impl_->boneAt(boneId)->orientation(impl_->state)[1];
}

float Skeleton3D::getOrientationZ(int boneId) const {
    requireBone(boneId);
    const_cast<Skeleton3D *>(this)->impl_->ensureStateSize();
    return impl_->boneAt(boneId)->orientation(impl_->state)[2];
}

void Skeleton3D::setRotation(int boneId, float yaw, float pitch) {
    requireBone(boneId);
    impl_->ensureStateSize();
    impl_->boneAt(boneId)->rotation(impl_->state) = {yaw, pitch};
}

float Skeleton3D::getRotationYaw(int boneId) const {
    requireBone(boneId);
    const_cast<Skeleton3D *>(this)->impl_->ensureStateSize();
    return impl_->boneAt(boneId)->rotation(impl_->state)[0];
}

float Skeleton3D::getRotationPitch(int boneId) const {
    requireBone(boneId);
    const_cast<Skeleton3D *>(this)->impl_->ensureStateSize();
    return impl_->boneAt(boneId)->rotation(impl_->state)[1];
}

void Skeleton3D::setConstraints(int boneId, float minYaw, float minPitch, float maxYaw, float maxPitch) {
    requireBone(boneId);
    if (minYaw > maxYaw || minPitch > maxPitch) {
        throw Exception("Skeleton3D.setConstraints: min must be <= max per axis");
    }
    impl_->boneAt(boneId)->setConstraints(::ik::vec2{minYaw, minPitch}, ::ik::vec2{maxYaw, maxPitch});
}

void Skeleton3D::clearConstraints(int boneId) {
    requireBone(boneId);
    impl_->boneAt(boneId)->clearConstraints();
}

bool Skeleton3D::hasConstraints(int boneId) const {
    requireBone(boneId);
    return impl_->boneAt(boneId)->constrained;
}

void Skeleton3D::initStraightPose(float rootX, float rootY, float rootZ) {
    impl_->ensureSorted();
    impl_->ensureStateSize();
    impl_->sk.init_straight_pose(impl_->state, ::ik::vec3{rootX, rootY, rootZ});
}

void Skeleton3D::bind() {
    impl_->ensureSorted();
    impl_->ensureStateSize();
    impl_->sk.bind(impl_->state);
}

void Skeleton3D::forwardKinematics() {
    impl_->ensureSorted();
    impl_->ensureStateSize();
    impl_->sk.forward_kinematics(impl_->state);
}

void Skeleton3D::updateRotations() {
    impl_->ensureSorted();
    impl_->ensureStateSize();
    impl_->sk.update_rotations(impl_->state);
}

float Skeleton3D::totalLengthTo(int boneId) const {
    requireBone(boneId);
    return impl_->sk.total_length_to(impl_->boneAt(boneId));
}

}  // namespace eve::ik
