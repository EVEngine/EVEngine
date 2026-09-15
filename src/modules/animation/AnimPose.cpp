#include "animation/AnimPose.h"
#include <array>
#include "animation/AnimSkeleton.h"
#include "animation/AnimTransformInternal.h"

#include "common/Exception.h"

#if defined(__SSE2__) || defined(_M_X64) || defined(_M_IX86_FP)
#include <immintrin.h>
#endif

namespace eve::animation {

namespace {

struct Quat {
    float x = 0.f, y = 0.f, z = 0.f, w = 1.f;
};

Quat quatMul(const Quat& a, const Quat& b) {
    return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}

Quat quatInverse(const Quat& q) { return {-q.x, -q.y, -q.z, q.w}; }

bool fromTo(float ax, float ay, float az, float bx, float by, float bz, Quat& out) {
    const float al = std::sqrt(ax * ax + ay * ay + az * az);
    const float bl = std::sqrt(bx * bx + by * by + bz * bz);
    if (al < 1e-6f || bl < 1e-6f) return false;
    ax /= al;
    ay /= al;
    az /= al;
    bx /= bl;
    by /= bl;
    bz /= bl;
    const float dot = clampf(ax * bx + ay * by + az * bz, -1.f, 1.f);
    if (dot < -0.9999f) {
        float       ox = std::fabs(ax) < 0.8f ? 1.f : 0.f;
        float       oy = std::fabs(ax) < 0.8f ? 0.f : 1.f;
        float       oz = 0.f;
        const float cx = ay * oz - az * oy;
        const float cy = az * ox - ax * oz;
        const float cz = ax * oy - ay * ox;
        const float cl = std::sqrt(cx * cx + cy * cy + cz * cz);
        out            = {cx / cl, cy / cl, cz / cl, 0.f};
        return true;
    }
    const float cx = ay * bz - az * by;
    const float cy = az * bx - ax * bz;
    const float cz = ax * by - ay * bx;
    out            = {cx, cy, cz, 1.f + dot};
    const float ql = std::sqrt(out.x * out.x + out.y * out.y + out.z * out.z + out.w * out.w);
    out.x /= ql;
    out.y /= ql;
    out.z /= ql;
    out.w /= ql;
    return true;
}

Quat worldToLocalRotation(const AnimSkeleton* skeleton, const AnimPose* pose, int bone, const Quat& world) {
    const int parent = skeleton->getParent(bone);
    if (parent < 0) return world;
    const auto& p = pose->world(parent);
    return quatMul(quatInverse({p.qx, p.qy, p.qz, p.qw}), world);
}

void blendLocalRotation(TransformTRS& local, const Quat& target, float weight) {
    slerpQuat(local.qx, local.qy, local.qz, local.qw, target.x, target.y, target.z, target.w, clampf(weight, 0.f, 1.f),
              local.qx, local.qy, local.qz, local.qw);
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
        const TransformTRS& av = a->locals_[static_cast<size_t>(i)];
        const TransformTRS& bv = b->locals_[static_cast<size_t>(i)];
        TransformTRS& out = locals_[static_cast<size_t>(i)];
#if defined(__SSE2__) || defined(_M_X64) || defined(_M_IX86_FP)
        const __m128 weight = _mm_set1_ps(clampf(t, 0.f, 1.f));
        const __m128 oneMinus = _mm_sub_ps(_mm_set1_ps(1.f), weight);
        alignas(16) float values[4];
        _mm_store_ps(values, _mm_add_ps(_mm_mul_ps(_mm_setr_ps(av.px, av.py, av.pz, 0.f), oneMinus),
                                        _mm_mul_ps(_mm_setr_ps(bv.px, bv.py, bv.pz, 0.f), weight)));
        out.px = values[0]; out.py = values[1]; out.pz = values[2];
        _mm_store_ps(values, _mm_add_ps(_mm_mul_ps(_mm_setr_ps(av.sx, av.sy, av.sz, 0.f), oneMinus),
                                        _mm_mul_ps(_mm_setr_ps(bv.sx, bv.sy, bv.sz, 0.f), weight)));
        out.sx = values[0]; out.sy = values[1]; out.sz = values[2];
#else
        out.px = lerpf(av.px, bv.px, t); out.py = lerpf(av.py, bv.py, t); out.pz = lerpf(av.pz, bv.pz, t);
        out.sx = lerpf(av.sx, bv.sx, t); out.sy = lerpf(av.sy, bv.sy, t); out.sz = lerpf(av.sz, bv.sz, t);
#endif
        slerpQuat(av.qx, av.qy, av.qz, av.qw, bv.qx, bv.qy, bv.qz, bv.qw, clampf(t, 0.f, 1.f),
                  out.qx, out.qy, out.qz, out.qw);
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
                detail::mulTRS(worlds_[static_cast<size_t>(parent)], locals_[static_cast<size_t>(i)]);
        }
    }
}

bool AnimPose::aimBone(const AnimSkeleton* skeleton, int boneIndex, float targetX, float targetY, float targetZ,
                       float weight) {
    if (!skeleton) throw Exception("AnimPose.aimBone: skeleton is null");
    requireBone(boneIndex);
    computeWorld(skeleton);
    const auto& bone = world(boneIndex);
    const Mat4  mat  = Mat4::fromTRS(bone);
    Quat        delta;
    if (!fromTo(mat.m[8], mat.m[9], mat.m[10], targetX - bone.px, targetY - bone.py, targetZ - bone.pz, delta)) {
        return false;
    }
    const Quat desiredWorld = quatMul(delta, {bone.qx, bone.qy, bone.qz, bone.qw});
    blendLocalRotation(local(boneIndex), worldToLocalRotation(skeleton, this, boneIndex, desiredWorld), weight);
    computeWorld(skeleton);
    return true;
}

bool AnimPose::solveTwoBoneIK(const AnimSkeleton* skeleton, int rootBone, int midBone, int tipBone, float targetX,
                              float targetY, float targetZ, float weight) {
    if (!skeleton) throw Exception("AnimPose.solveTwoBoneIK: skeleton is null");
    requireBone(midBone);
    computeWorld(skeleton);
    const auto pole = world(midBone);
    return solveTwoBoneIKPoleImpl(skeleton, rootBone, midBone, tipBone, targetX, targetY, targetZ, pole.px, pole.py,
                                  pole.pz, weight);
}

bool AnimPose::solveTwoBoneIKPoleImpl(const AnimSkeleton* skeleton, int rootBone, int midBone, int tipBone,
                                      float targetX, float targetY, float targetZ, float poleX, float poleY,
                                      float poleZ, float weight) {
    if (!skeleton) throw Exception("AnimPose.solveTwoBoneIK: skeleton is null");
    requireBone(rootBone);
    requireBone(midBone);
    requireBone(tipBone);
    if (skeleton->getParent(midBone) != rootBone || skeleton->getParent(tipBone) != midBone) return false;

    if (!std::isfinite(targetX) || !std::isfinite(targetY) || !std::isfinite(targetZ) || !std::isfinite(poleX) ||
        !std::isfinite(poleY) || !std::isfinite(poleZ) || !std::isfinite(weight))
        throw Exception("AnimPose.solveTwoBoneIK: nonfinite target or weight");
    computeWorld(skeleton);
    using Vector  = std::array<double, 3>;
    auto position = [](const TransformTRS& t) -> Vector { return {t.px, t.py, t.pz}; };
    auto sub      = [](Vector a, Vector b) -> Vector { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; };
    auto dot      = [](Vector a, Vector b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; };
    auto cross    = [](Vector a, Vector b) -> Vector {
        return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
    };
    const Vector a = position(world(rootBone)), b = position(world(midBone)), c = position(world(tipBone));
    const Vector ab = sub(b, a), bc = sub(c, b);
    const double upper = std::sqrt(dot(ab, ab)), lower = std::sqrt(dot(bc, bc));
    if (upper < 1e-7 || lower < 1e-7) return false;
    Vector direction = sub(Vector{targetX, targetY, targetZ}, a);
    double distance  = std::sqrt(dot(direction, direction));
    if (distance < 1e-7) {
        direction = sub(c, a);
        distance  = std::sqrt(dot(direction, direction));
    }
    if (distance < 1e-7) {
        direction = ab;
        distance  = upper;
    }
    for (auto& value : direction) value /= distance;
    const double requested =
        std::sqrt(dot(sub(Vector{targetX, targetY, targetZ}, a), sub(Vector{targetX, targetY, targetZ}, a)));
    const double reach     = std::clamp(requested, std::max(std::abs(upper - lower), 1e-7), upper + lower);
    const double along     = (upper * upper - lower * lower + reach * reach) / (2 * reach);
    const double height    = std::sqrt(std::max(upper * upper - along * along, 0.0));
    Vector       pole      = sub(Vector{poleX, poleY, poleZ}, a);
    const double projected = dot(pole, direction);
    for (int i = 0; i < 3; ++i) pole[i] -= projected * direction[i];
    double poleLength = std::sqrt(dot(pole, pole));
    if (poleLength < 1e-7) {
        pole       = cross(cross(ab, bc), direction);
        poleLength = std::sqrt(dot(pole, pole));
    }
    if (poleLength < 1e-7) {
        // A fully straight chain has no bend plane. Choose the least parallel
        // axis deterministically; subsequent calls retain the resulting plane.
        int axis = 0;
        for (int i = 1; i < 3; ++i)
            if (std::abs(direction[i]) < std::abs(direction[axis])) axis = i;
        pole                    = {0, 0, 0};
        pole[axis]              = 1;
        const double projection = dot(pole, direction);
        for (int i = 0; i < 3; ++i) pole[i] -= projection * direction[i];
        poleLength = std::sqrt(dot(pole, pole));
    }
    Vector knee{}, tip{};
    for (int i = 0; i < 3; ++i) {
        knee[i] = a[i] + direction[i] * along + pole[i] * height / poleLength;
        tip[i]  = a[i] + direction[i] * reach;
    }
    const auto originalRoot = local(rootBone), originalMid = local(midBone);
    Quat       delta;
    if (!fromTo(float(ab[0]), float(ab[1]), float(ab[2]), float(knee[0] - a[0]), float(knee[1] - a[1]),
                float(knee[2] - a[2]), delta))
        return false;
    const auto root = world(rootBone);
    const Quat rootRotation =
        worldToLocalRotation(skeleton, this, rootBone, quatMul(delta, {root.qx, root.qy, root.qz, root.qw}));
    blendLocalRotation(local(rootBone), rootRotation, 1.f);
    computeWorld(skeleton);
    const auto mid = world(midBone), end = world(tipBone);
    if (fromTo(end.px - mid.px, end.py - mid.py, end.pz - mid.pz, float(tip[0] - mid.px), float(tip[1] - mid.py),
               float(tip[2] - mid.pz), delta)) {
        const Quat midRotation =
            worldToLocalRotation(skeleton, this, midBone, quatMul(delta, {mid.qx, mid.qy, mid.qz, mid.qw}));
        blendLocalRotation(local(midBone), midRotation, 1.f);
    }
    const auto solvedRoot = local(rootBone), solvedMid = local(midBone);
    local(rootBone) = originalRoot;
    local(midBone)  = originalMid;
    blendLocalRotation(local(rootBone), {solvedRoot.qx, solvedRoot.qy, solvedRoot.qz, solvedRoot.qw}, weight);
    blendLocalRotation(local(midBone), {solvedMid.qx, solvedMid.qy, solvedMid.qz, solvedMid.qw}, weight);
    computeWorld(skeleton);
    return true;
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
