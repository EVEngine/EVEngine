#include "animation/MotionDatabase.h"
#include "animation/AnimClip.h"
#include "animation/AnimSkeleton.h"

#include "common/Exception.h"

#include <cmath>

namespace eve::animation {

MotionDatabase::MotionDatabase(AnimSkeleton *skeleton) : skeleton_(skeleton) {
    if (!skeleton_) throw Exception("MotionDatabase: skeleton is null");
    scratchPose_.resize(skeleton_->getBoneCount());
    // Default: use every non-root bone if none specified before bake.
}

void MotionDatabase::addFeatureBone(int boneIndex) {
    if (boneIndex < 0 || boneIndex >= skeleton_->getBoneCount()) {
        throw Exception("MotionDatabase.addFeatureBone: invalid bone %d", boneIndex);
    }
    for (int b : featureBones_) {
        if (b == boneIndex) return;
    }
    featureBones_.push_back(boneIndex);
    baked_ = false;
}

void MotionDatabase::addFeatureBoneByName(const std::string &name) {
    const int id = skeleton_->findBone(name);
    if (id < 0) throw Exception("MotionDatabase.addFeatureBoneByName: unknown '%s'", name.c_str());
    addFeatureBone(id);
}

void MotionDatabase::addClip(AnimClip *clip) {
    if (!clip) throw Exception("MotionDatabase.addClip: clip is null");
    clips_.push_back(clip);
    baked_ = false;
}

AnimClip *MotionDatabase::getClip(int clipIndex) const {
    if (clipIndex < 0 || clipIndex >= getClipCount()) {
        throw Exception("MotionDatabase.getClip: invalid index %d", clipIndex);
    }
    return clips_[static_cast<size_t>(clipIndex)];
}

void MotionDatabase::computeFeatureSize() {
    // vel(2) + trajPos*3(6) + trajFacing(2) + bones*3
    featureSize_ = 2 + 6 + 2 + static_cast<int>(featureBones_.size()) * 3;
}

float MotionDatabase::yawFromQuat(float /*x*/, float y, float /*z*/, float w) {
    // Yaw from quaternion (Y-up), assuming mostly planar rotation.
    return std::atan2(2.f * (w * y), 1.f - 2.f * (y * y));
}

int MotionDatabase::getFeatureBone(int index) const {
    if (index < 0 || index >= getFeatureBoneCount()) {
        throw Exception("MotionDatabase.getFeatureBone: invalid index %d", index);
    }
    return featureBones_[static_cast<size_t>(index)];
}

void MotionDatabase::extractFeature(AnimClip *clip, float time, float dtSample,
                                    std::vector<float> &out, float &rootX, float &rootZ,
                                    float &rootYaw, float &velX, float &velZ) const {
    out.assign(static_cast<size_t>(featureSize_), 0.f);
    clip->sample(time, &scratchPose_, skeleton_);
    scratchPose_.computeWorld(skeleton_);

    const int root = 0;
    rootX          = scratchPose_.getWorldPositionX(root);
    rootZ          = scratchPose_.getWorldPositionZ(root);
    rootYaw        = yawFromQuat(scratchPose_.getWorldRotationX(root),
                          scratchPose_.getWorldRotationY(root),
                          scratchPose_.getWorldRotationZ(root),
                          scratchPose_.getWorldRotationW(root));

    // Velocity from nearby sample.
    const float t1 = time + std::max(dtSample, 1e-3f);
    AnimPose next;
    clip->sample(t1, &next, skeleton_);
    next.computeWorld(skeleton_);
    const float nX = next.getWorldPositionX(root);
    const float nZ = next.getWorldPositionZ(root);
    const float dtt = std::max(dtSample, 1e-3f);
    velX            = (nX - rootX) / dtt;
    velZ            = (nZ - rootZ) / dtt;

    float cs = std::cos(rootYaw);
    float sn = std::sin(rootYaw);
    // Character-space: rotate world xz by -yaw
    auto toLocal = [&](float wx, float wz, float &lx, float &lz) {
        const float dx = wx - rootX;
        const float dz = wz - rootZ;
        lx             = dx * cs + dz * sn;
        lz             = -dx * sn + dz * cs;
    };

    out[0] = velX * cs + velZ * sn;
    out[1] = -velX * sn + velZ * cs;

    const float horizons[3] = {0.33f, 0.66f, 1.0f};
    for (int h = 0; h < 3; ++h) {
        AnimPose fut;
        clip->sample(time + horizons[h], &fut, skeleton_);
        fut.computeWorld(skeleton_);
        float lx, lz;
        toLocal(fut.getWorldPositionX(root), fut.getWorldPositionZ(root), lx, lz);
        out[2 + h * 2]     = lx;
        out[2 + h * 2 + 1] = lz;
    }

    {
        AnimPose fut;
        clip->sample(time + 1.0f, &fut, skeleton_);
        fut.computeWorld(skeleton_);
        const float fyaw = yawFromQuat(fut.getWorldRotationX(root), fut.getWorldRotationY(root),
                                       fut.getWorldRotationZ(root), fut.getWorldRotationW(root));
        float fx, fz;
        yawToForward(fyaw - rootYaw, fx, fz);
        out[8] = fx;
        out[9] = fz;
    }

    int base = 10;
    for (int bone : featureBones_) {
        float lx, lz;
        const float wy = scratchPose_.getWorldPositionY(bone);
        toLocal(scratchPose_.getWorldPositionX(bone), scratchPose_.getWorldPositionZ(bone), lx,
                lz);
        out[static_cast<size_t>(base)]     = lx;
        out[static_cast<size_t>(base + 1)] = wy;
        out[static_cast<size_t>(base + 2)] = lz;
        base += 3;
    }
}

void MotionDatabase::bake() {
    if (clips_.empty()) throw Exception("MotionDatabase.bake: no clips");
    if (featureBones_.empty()) {
        // Default: all bones except root.
        for (int i = 1; i < skeleton_->getBoneCount(); ++i) addFeatureBone(i);
        if (featureBones_.empty() && skeleton_->getBoneCount() > 0) addFeatureBone(0);
    }
    computeFeatureSize();
    frames_.clear();

    for (int ci = 0; ci < getClipCount(); ++ci) {
        AnimClip *clip = clips_[static_cast<size_t>(ci)];
        const float rate = clip->getSampleRate() > 0.f ? clip->getSampleRate() : 30.f;
        const float dt   = 1.f / rate;
        const float dur  = clip->getDuration();
        if (dur <= 0.f) {
            Frame f;
            f.clipIndex = ci;
            f.time      = 0.f;
            extractFeature(clip, 0.f, dt, f.feature, f.rootX, f.rootZ, f.rootYaw, f.velX, f.velZ);
            frames_.push_back(std::move(f));
            continue;
        }
        for (float t = 0.f; t < dur - 1e-5f; t += dt) {
            Frame f;
            f.clipIndex = ci;
            f.time      = t;
            extractFeature(clip, t, dt, f.feature, f.rootX, f.rootZ, f.rootYaw, f.velX, f.velZ);
            frames_.push_back(std::move(f));
        }
    }
    baked_ = true;
}

void MotionDatabase::requireBaked() const {
    if (!baked_) throw Exception("MotionDatabase: not baked; call bake() first");
}

const MotionDatabase::Frame &MotionDatabase::frameAt(int index) const {
    requireBaked();
    if (index < 0 || index >= getFrameCount()) {
        throw Exception("MotionDatabase: invalid frame %d", index);
    }
    return frames_[static_cast<size_t>(index)];
}

float MotionDatabase::getFrameTime(int frameIndex) const { return frameAt(frameIndex).time; }

int MotionDatabase::getFrameClipIndex(int frameIndex) const {
    return frameAt(frameIndex).clipIndex;
}

void MotionDatabase::getFeature(int frameIndex, float *out, int outCount) const {
    const Frame &f = frameAt(frameIndex);
    if (!out || outCount < featureSize_) {
        throw Exception("MotionDatabase.getFeature: buffer too small");
    }
    for (int i = 0; i < featureSize_; ++i) out[i] = f.feature[static_cast<size_t>(i)];
}

}  // namespace eve::animation
