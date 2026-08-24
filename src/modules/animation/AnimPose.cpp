#include "animation/AnimPose.h"
#include "animation/AnimSkeleton.h"

#include "common/Exception.h"

namespace eve::animation {

namespace {

TransformTRS mulTRS(const TransformTRS& parent, const TransformTRS& local) {
    // world = parent * local (TRS, scale ignored in rotation path for FK positions)
    TransformTRS out;
    // Rotate local translation by parent rotation, then add parent translation * scales.
    const float qx = parent.qx, qy = parent.qy, qz = parent.qz, qw = parent.qw;
    const float lx = local.px * parent.sx;
    const float ly = local.py * parent.sy;
    const float lz = local.pz * parent.sz;
    // q * v
    const float ix = qw * lx + qy * lz - qz * ly;
    const float iy = qw * ly + qz * lx - qx * lz;
    const float iz = qw * lz + qx * ly - qy * lx;
    const float iw = -qx * lx - qy * ly - qz * lz;
    out.px         = parent.px + (ix * qw + iw * -qx + iy * -qz - iz * -qy);
    out.py         = parent.py + (iy * qw + iw * -qy + iz * -qx - ix * -qz);
    out.pz         = parent.pz + (iz * qw + iw * -qz + ix * -qy - iy * -qx);

    // Quaternion multiply parent * local
    out.qw = parent.qw * local.qw - parent.qx * local.qx - parent.qy * local.qy - parent.qz * local.qz;
    out.qx = parent.qw * local.qx + parent.qx * local.qw + parent.qy * local.qz - parent.qz * local.qy;
    out.qy = parent.qw * local.qy - parent.qx * local.qz + parent.qy * local.qw + parent.qz * local.qx;
    out.qz = parent.qw * local.qz + parent.qx * local.qy - parent.qy * local.qx + parent.qz * local.qw;
    out.normalizeRotation();

    out.sx = parent.sx * local.sx;
    out.sy = parent.sy * local.sy;
    out.sz = parent.sz * local.sz;
    return out;
}

}  // namespace

AnimPose::AnimPose(int boneCount) { resize(boneCount); }

void AnimPose::resize(int boneCount) {
    if (boneCount < 0) throw Exception("AnimPose.resize: boneCount must be >= 0");
    if (boneCount == getBoneCount()) return;
    locals_.assign(static_cast<size_t>(boneCount), TransformTRS::identity());
    worlds_.assign(static_cast<size_t>(boneCount), TransformTRS::identity());
}

void AnimPose::requireBone(int boneIndex) const {
    if (boneIndex < 0 || boneIndex >= getBoneCount()) {
        throw Exception("AnimPose: invalid bone index %d", boneIndex);
    }
}

void AnimPose::copyFrom(const AnimPose* other) {
    if (!other) throw Exception("AnimPose.copyFrom: other is null");
    locals_ = other->locals_;
    worlds_ = other->worlds_;
}

void AnimPose::blendFrom(const AnimPose* a, const AnimPose* b, float t) {
    if (!a || !b) throw Exception("AnimPose.blendFrom: pose is null");
    if (a->getBoneCount() != b->getBoneCount()) {
        throw Exception("AnimPose.blendFrom: bone count mismatch");
    }
    resize(a->getBoneCount());
    for (int i = 0; i < getBoneCount(); ++i) {
        locals_[static_cast<size_t>(i)] =
            blendTRS(a->locals_[static_cast<size_t>(i)], b->locals_[static_cast<size_t>(i)], t);
    }
}

TransformTRS& AnimPose::local(int boneIndex) {
    requireBone(boneIndex);
    return locals_[static_cast<size_t>(boneIndex)];
}

const TransformTRS& AnimPose::local(int boneIndex) const {
    requireBone(boneIndex);
    return locals_[static_cast<size_t>(boneIndex)];
}

void AnimPose::setLocalPosition(int boneIndex, float x, float y, float z) {
    requireBone(boneIndex);
    auto& t = locals_[static_cast<size_t>(boneIndex)];
    t.px    = x;
    t.py    = y;
    t.pz    = z;
}

void AnimPose::setLocalRotation(int boneIndex, float x, float y, float z, float w) {
    requireBone(boneIndex);
    auto& t = locals_[static_cast<size_t>(boneIndex)];
    t.qx    = x;
    t.qy    = y;
    t.qz    = z;
    t.qw    = w;
    t.normalizeRotation();
}

void AnimPose::setLocalScale(int boneIndex, float x, float y, float z) {
    requireBone(boneIndex);
    auto& t = locals_[static_cast<size_t>(boneIndex)];
    t.sx    = x;
    t.sy    = y;
    t.sz    = z;
}

float AnimPose::getLocalPositionX(int boneIndex) const {
    requireBone(boneIndex);
    return locals_[static_cast<size_t>(boneIndex)].px;
}
float AnimPose::getLocalPositionY(int boneIndex) const {
    requireBone(boneIndex);
    return locals_[static_cast<size_t>(boneIndex)].py;
}
float AnimPose::getLocalPositionZ(int boneIndex) const {
    requireBone(boneIndex);
    return locals_[static_cast<size_t>(boneIndex)].pz;
}
float AnimPose::getLocalRotationX(int boneIndex) const {
    requireBone(boneIndex);
    return locals_[static_cast<size_t>(boneIndex)].qx;
}
float AnimPose::getLocalRotationY(int boneIndex) const {
    requireBone(boneIndex);
    return locals_[static_cast<size_t>(boneIndex)].qy;
}
float AnimPose::getLocalRotationZ(int boneIndex) const {
    requireBone(boneIndex);
    return locals_[static_cast<size_t>(boneIndex)].qz;
}
float AnimPose::getLocalRotationW(int boneIndex) const {
    requireBone(boneIndex);
    return locals_[static_cast<size_t>(boneIndex)].qw;
}
float AnimPose::getLocalScaleX(int boneIndex) const {
    requireBone(boneIndex);
    return locals_[static_cast<size_t>(boneIndex)].sx;
}
float AnimPose::getLocalScaleY(int boneIndex) const {
    requireBone(boneIndex);
    return locals_[static_cast<size_t>(boneIndex)].sy;
}
float AnimPose::getLocalScaleZ(int boneIndex) const {
    requireBone(boneIndex);
    return locals_[static_cast<size_t>(boneIndex)].sz;
}

void AnimPose::computeWorld(const AnimSkeleton* skeleton) {
    if (!skeleton) throw Exception("AnimPose.computeWorld: skeleton is null");
    if (skeleton->getBoneCount() != getBoneCount()) {
        throw Exception("AnimPose.computeWorld: bone count mismatch");
    }
    worlds_.resize(locals_.size());
    for (int i = 0; i < getBoneCount(); ++i) {
        const int parent = skeleton->getParent(i);
        if (parent < 0) {
            worlds_[static_cast<size_t>(i)] = locals_[static_cast<size_t>(i)];
        } else {
            worlds_[static_cast<size_t>(i)] =
                mulTRS(worlds_[static_cast<size_t>(parent)], locals_[static_cast<size_t>(i)]);
        }
    }
}

float AnimPose::getWorldPositionX(int boneIndex) const {
    requireBone(boneIndex);
    return worlds_[static_cast<size_t>(boneIndex)].px;
}
float AnimPose::getWorldPositionY(int boneIndex) const {
    requireBone(boneIndex);
    return worlds_[static_cast<size_t>(boneIndex)].py;
}
float AnimPose::getWorldPositionZ(int boneIndex) const {
    requireBone(boneIndex);
    return worlds_[static_cast<size_t>(boneIndex)].pz;
}
float AnimPose::getWorldRotationX(int boneIndex) const {
    requireBone(boneIndex);
    return worlds_[static_cast<size_t>(boneIndex)].qx;
}
float AnimPose::getWorldRotationY(int boneIndex) const {
    requireBone(boneIndex);
    return worlds_[static_cast<size_t>(boneIndex)].qy;
}
float AnimPose::getWorldRotationZ(int boneIndex) const {
    requireBone(boneIndex);
    return worlds_[static_cast<size_t>(boneIndex)].qz;
}
float AnimPose::getWorldRotationW(int boneIndex) const {
    requireBone(boneIndex);
    return worlds_[static_cast<size_t>(boneIndex)].qw;
}

const TransformTRS& AnimPose::world(int boneIndex) const {
    requireBone(boneIndex);
    return worlds_[static_cast<size_t>(boneIndex)];
}

float AnimPose::getWorldMatrixElement(int boneIndex, int elementIndex) const {
    requireBone(boneIndex);
    if (elementIndex < 0 || elementIndex > 15) {
        throw Exception("AnimPose.getWorldMatrixElement: elementIndex must be 0..15");
    }
    const Mat4 mat = Mat4::fromTRS(worlds_[static_cast<size_t>(boneIndex)]);
    return mat.m[elementIndex];
}

void AnimPose::getWorldMatrix(int boneIndex, float* out16) const {
    requireBone(boneIndex);
    if (!out16) throw Exception("AnimPose.getWorldMatrix: out16 is null");
    const Mat4 mat = Mat4::fromTRS(worlds_[static_cast<size_t>(boneIndex)]);
    for (int i = 0; i < 16; ++i) out16[i] = mat.m[i];
}

}  // namespace eve::animation
