#include "animation/AnimSkeleton.h"
#include "animation/AnimPose.h"

#include "common/Exception.h"

namespace eve::animation {

void AnimSkeleton::requireBone(int boneIndex) const {
    if (boneIndex < 0 || boneIndex >= getBoneCount()) {
        throw Exception("AnimSkeleton: invalid bone index %d", boneIndex);
    }
}

int AnimSkeleton::addBone(const std::string &name, int parentIndex) {
    if (parentIndex < -1 || parentIndex >= getBoneCount()) {
        throw Exception("AnimSkeleton.addBone: invalid parent index %d", parentIndex);
    }
    if (name.empty()) {
        throw Exception("AnimSkeleton.addBone: name must not be empty");
    }
    if (findBone(name) >= 0) {
        throw Exception("AnimSkeleton.addBone: duplicate bone name '%s'", name.c_str());
    }
    Bone b;
    b.name   = name;
    b.parent = parentIndex;
    b.bind   = TransformTRS::identity();
    bones_.push_back(b);
    return static_cast<int>(bones_.size()) - 1;
}

std::string AnimSkeleton::getBoneName(int boneIndex) const {
    requireBone(boneIndex);
    return bones_[static_cast<size_t>(boneIndex)].name;
}

int AnimSkeleton::findBone(const std::string &name) const {
    for (int i = 0; i < getBoneCount(); ++i) {
        if (bones_[static_cast<size_t>(i)].name == name) return i;
    }
    return -1;
}

int AnimSkeleton::getParent(int boneIndex) const {
    requireBone(boneIndex);
    return bones_[static_cast<size_t>(boneIndex)].parent;
}

void AnimSkeleton::setBindPosition(int boneIndex, float x, float y, float z) {
    requireBone(boneIndex);
    auto &t = bones_[static_cast<size_t>(boneIndex)].bind;
    t.px = x;
    t.py = y;
    t.pz = z;
}

void AnimSkeleton::setBindRotation(int boneIndex, float x, float y, float z, float w) {
    requireBone(boneIndex);
    auto &t = bones_[static_cast<size_t>(boneIndex)].bind;
    t.qx = x;
    t.qy = y;
    t.qz = z;
    t.qw = w;
    t.normalizeRotation();
}

void AnimSkeleton::setBindScale(int boneIndex, float x, float y, float z) {
    requireBone(boneIndex);
    auto &t = bones_[static_cast<size_t>(boneIndex)].bind;
    t.sx = x;
    t.sy = y;
    t.sz = z;
}

float AnimSkeleton::getBindPositionX(int boneIndex) const {
    requireBone(boneIndex);
    return bones_[static_cast<size_t>(boneIndex)].bind.px;
}
float AnimSkeleton::getBindPositionY(int boneIndex) const {
    requireBone(boneIndex);
    return bones_[static_cast<size_t>(boneIndex)].bind.py;
}
float AnimSkeleton::getBindPositionZ(int boneIndex) const {
    requireBone(boneIndex);
    return bones_[static_cast<size_t>(boneIndex)].bind.pz;
}
float AnimSkeleton::getBindRotationX(int boneIndex) const {
    requireBone(boneIndex);
    return bones_[static_cast<size_t>(boneIndex)].bind.qx;
}
float AnimSkeleton::getBindRotationY(int boneIndex) const {
    requireBone(boneIndex);
    return bones_[static_cast<size_t>(boneIndex)].bind.qy;
}
float AnimSkeleton::getBindRotationZ(int boneIndex) const {
    requireBone(boneIndex);
    return bones_[static_cast<size_t>(boneIndex)].bind.qz;
}
float AnimSkeleton::getBindRotationW(int boneIndex) const {
    requireBone(boneIndex);
    return bones_[static_cast<size_t>(boneIndex)].bind.qw;
}
float AnimSkeleton::getBindScaleX(int boneIndex) const {
    requireBone(boneIndex);
    return bones_[static_cast<size_t>(boneIndex)].bind.sx;
}
float AnimSkeleton::getBindScaleY(int boneIndex) const {
    requireBone(boneIndex);
    return bones_[static_cast<size_t>(boneIndex)].bind.sy;
}
float AnimSkeleton::getBindScaleZ(int boneIndex) const {
    requireBone(boneIndex);
    return bones_[static_cast<size_t>(boneIndex)].bind.sz;
}

const TransformTRS &AnimSkeleton::bindLocal(int boneIndex) const {
    requireBone(boneIndex);
    return bones_[static_cast<size_t>(boneIndex)].bind;
}

void AnimSkeleton::applyBindPose(AnimPose *pose) const {
    if (!pose) throw Exception("AnimSkeleton.applyBindPose: pose is null");
    pose->resize(getBoneCount());
    for (int i = 0; i < getBoneCount(); ++i) {
        pose->local(i) = bones_[static_cast<size_t>(i)].bind;
    }
}

}  // namespace eve::animation
