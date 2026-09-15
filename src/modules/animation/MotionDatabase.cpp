#include "animation/MotionDatabase.h"
#include "animation/AnimClip.h"
#include "animation/AnimParallelInternal.h"
#include "animation/AnimSkeleton.h"
#include "animation/AnimTransformInternal.h"
#include "animation/MotionSchemaInternal.h"

#include "common/Exception.h"

#include <cmath>

namespace eve::animation {

MotionDatabase::MotionDatabase(AnimSkeleton* skeleton) : skeleton_(skeleton) {
    if (!skeleton_) throw Exception("MotionDatabase: skeleton is null");
    scratchPose_.resize(skeleton_->getBoneCount());
    // Default: use every non-root bone if none specified before bake.
}

void MotionDatabase::addFeatureBone(int boneIndex) {
    if (locomotionFeatures_ || schema_) throw Exception("MotionDatabase: configured layout owns its feature bones");
    if (boneIndex < 0 || boneIndex >= skeleton_->getBoneCount()) {
        throw Exception("MotionDatabase.addFeatureBone: invalid bone %d", boneIndex);
    }
    for (int b : featureBones_) {
        if (b == boneIndex) return;
    }
    featureBones_.push_back(boneIndex);
    baked_ = false;
}

void MotionDatabase::addFeatureBoneByName(const std::string& name) {
    const int id = skeleton_->findBone(name);
    if (id < 0) throw Exception("MotionDatabase.addFeatureBoneByName: unknown '%s'", name.c_str());
    addFeatureBone(id);
}

void MotionDatabase::setRootBone(int boneIndex) {
    if (schema_ && baked_) throw Exception("MotionDatabase: baked feature layout is immutable");
    if (boneIndex < 0 || boneIndex >= skeleton_->getBoneCount()) {
        throw Exception("MotionDatabase.setRootBone: invalid bone %d", boneIndex);
    }
    rootBone_ = boneIndex;
    baked_    = false;
}

void MotionDatabase::setRootBoneByName(const std::string& name) {
    const int id = skeleton_->findBone(name);
    if (id < 0) throw Exception("MotionDatabase.setRootBoneByName: unknown '%s'", name.c_str());
    setRootBone(id);
}

void MotionDatabase::addClip(AnimClip* clip) {
    if (schema_ && baked_) throw Exception("MotionDatabase: baked feature layout is immutable");
    if (!clip) throw Exception("MotionDatabase.addClip: clip is null");
    clips_.push_back(clip);
    baked_ = false;
}

AnimClip* MotionDatabase::getClip(int clipIndex) const {
    if (clipIndex < 0 || clipIndex >= getClipCount()) {
        throw Exception("MotionDatabase.getClip: invalid index %d", clipIndex);
    }
    return clips_[static_cast<size_t>(clipIndex)];
}

void MotionDatabase::computeFeatureSize() {
    // vel(2) + trajPos*3(6) + trajFacing(2) + bones*3
    featureSize_ = schema_ ? schema_->dimension : (locomotionFeatures_ ? 30 : 2 + 6 + 2 + static_cast<int>(featureBones_.size()) * 3);
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

void MotionDatabase::extractFeature(AnimClip* clip, float time, float dtSample, std::vector<float>& out, float& rootX,
                                    float& rootZ, float& rootYaw, float& velX, float& velZ) const {
    if (schema_) {
        extractSchemaFeature(clip, time, out, rootX, rootZ, rootYaw, velX, velZ);
        return;
    }
    if (locomotionFeatures_) {
        extractLocomotionFeature(clip, time, out, rootX, rootZ, rootYaw, velX, velZ);
        return;
    }
    out.assign(static_cast<size_t>(featureSize_), 0.f);
    // Trajectory horizons need only the root's ancestor chain; pose features
    // need only their own chains. Sampling every unrelated finger/face bone at
    // all six horizons makes full-corpus baking unnecessarily expensive.
    auto worldBone = [&](auto&& self, int bone, float sampleTime) -> TransformTRS {
        const auto local  = clip->sampleBone(bone, sampleTime, skeleton_->bindLocal(bone));
        const int  parent = skeleton_->getParent(bone);
        return parent < 0 ? local : detail::mulTRS(self(self, parent, sampleTime), local);
    };
    auto worldAt = [&](int bone, float sampleTime, bool wrap = true) {
        const float t = clampf(wrap ? clip->wrapTime(sampleTime) : sampleTime, 0.f, clip->getDuration());
        return worldBone(worldBone, bone, t);
    };
    const int root = rootBone_;
    const auto current = worldAt(root, time);
    rootX              = current.px;
    rootZ              = current.pz;
    rootYaw            = yawFromQuat(current.qx, current.qy, current.qz, current.qw);

    // Sampling a looping pose wraps its root back to the origin. Trajectories
    // must instead accumulate the displacement of every completed cycle.
    float       cycleX = 0.f, cycleZ = 0.f;
    const float duration = clip->getDuration();
    if (clip->getLoop() && duration > 1e-8f) {
        const auto start = worldAt(root, 0.f, false);
        const auto end   = worldAt(root, duration, false);
        cycleX           = end.px - start.px;
        cycleZ           = end.pz - start.pz;
    }
    auto cyclesAt = [&](float t) { return clip->getLoop() && duration > 1e-8f ? std::floor(t / duration) : 0.f; };

    // Velocity from nearby sample.
    const float t1 = time + std::max(dtSample, 1e-3f);
    const auto  next = worldAt(root, t1);
    const float nX   = next.px + cyclesAt(t1) * cycleX;
    const float nZ   = next.pz + cyclesAt(t1) * cycleZ;
    const float dtt = std::max(dtSample, 1e-3f);
    velX            = (nX - rootX) / dtt;
    velZ            = (nZ - rootZ) / dtt;

    float cs = std::cos(rootYaw);
    float sn = std::sin(rootYaw);
    // Character-space: rotate world xz by -yaw
    auto toLocal = [&](float wx, float wz, float& lx, float& lz) {
        const float dx = wx - rootX;
        const float dz = wz - rootZ;
        lx             = dx * cs - dz * sn;
        lz             = dx * sn + dz * cs;
    };

    out[0] = velX * cs - velZ * sn;
    out[1] = velX * sn + velZ * cs;

    const float horizons[3] = {0.33f, 0.66f, 1.0f};
    for (int h = 0; h < 3; ++h) {
        const auto  fut = worldAt(root, time + horizons[h]);
        float lx, lz;
        const float cycles = cyclesAt(time + horizons[h]);
        toLocal(fut.px + cycles * cycleX, fut.pz + cycles * cycleZ, lx, lz);
        out[2 + h * 2]     = lx;
        out[2 + h * 2 + 1] = lz;
    }

    {
        const auto  fut  = worldAt(root, time + 1.0f);
        const float fyaw = yawFromQuat(fut.qx, fut.qy, fut.qz, fut.qw);
        float       fx, fz;
        yawToForward(fyaw - rootYaw, fx, fz);
        out[8] = fx;
        out[9] = fz;
    }

    int base = 10;
    for (int bone : featureBones_) {
        float       lx, lz;
        const auto  feature = worldAt(bone, time);
        const float wy      = feature.py;
        toLocal(feature.px, feature.pz, lx, lz);
        out[static_cast<size_t>(base)]     = lx;
        out[static_cast<size_t>(base + 1)] = wy;
        out[static_cast<size_t>(base + 2)] = lz;
        base += 3;
    }
}

void MotionDatabase::bake() {
    if (schema_) for (const auto& channel : schema_->layout.channels) {
        if (channel.kind != MotionFeatureKind::Curve) continue;
        if (!schema_->curves) throw Exception("MotionDatabase: scalar curve data must be configured before bake");
        for (const auto* clip : clips_) if (!schema_->curveSources.contains(clip))
            throw Exception("MotionDatabase: added clip has no scalar curve source");
    }
    if (clips_.empty()) throw Exception("MotionDatabase.bake: no clips");
    if (featureBones_.empty() && !schema_) {
        // Default: all bones except root.
        for (int i = 1; i < skeleton_->getBoneCount(); ++i) addFeatureBone(i);
        if (featureBones_.empty() && skeleton_->getBoneCount() > 0) addFeatureBone(0);
    }
    computeFeatureSize();
    baked_ = false;
    std::vector<std::vector<Frame>> clipFrames(clips_.size());

    detail::parallelAnimationItems(clips_.size(), clips_.size() < 16 ? 1 : 0, [&](std::size_t index) {
        const int   ci     = static_cast<int>(index);
        auto&       output = clipFrames[index];
        AnimClip*   clip = clips_[static_cast<size_t>(ci)];
        const float rate = schema_ ? static_cast<float>(schema_->layout.sampleRate) : (clip->getSampleRate() > 0.f ? clip->getSampleRate() : 30.f);
        const float dt   = 1.f / rate;
        const float dur  = clip->getDuration();
        if (dur <= 0.f) {
            Frame f;
            f.clipIndex = ci;
            f.time      = 0.f;
            extractFeature(clip, 0.f, dt, f.feature, f.rootX, f.rootZ, f.rootYaw, f.velX, f.velZ);
            if (schema_) f.trajectorySpeed = schemaTrajectorySpeed(f.feature);
            output.push_back(std::move(f));
            return;
        }
        const int schemaLast = schema_ ? static_cast<int>(std::floor(dur * rate)) : 0;
        float t = 0.f;
        for (int sample = 0; ; ++sample, t = schema_ ? sample / rate : t + dt) {
            if (schema_ ? (sample > schemaLast || (clip->getLoop() && t >= dur - 1e-5f)) : t >= dur - 1e-5f) break;
            Frame f;
            f.clipIndex = ci;
            f.time      = t;
            extractFeature(clip, t, dt, f.feature, f.rootX, f.rootZ, f.rootYaw, f.velX, f.velZ);
            if (schema_) f.trajectorySpeed = schemaTrajectorySpeed(f.feature);
            output.push_back(std::move(f));
        }
    });
    // Preserve frame IDs and floating-point reduction order exactly.
    std::size_t total = 0;
    for (const auto& output : clipFrames) total += output.size();
    frames_.clear();
    frames_.reserve(total);
    clipFrameOffsets_.clear();
    clipFrameOffsets_.reserve(clipFrames.size() + 1);
    for (auto& output : clipFrames) {
        clipFrameOffsets_.push_back(static_cast<int>(frames_.size()));
        for (auto& frame : output) frames_.push_back(std::move(frame));
    }
    clipFrameOffsets_.push_back(static_cast<int>(frames_.size()));
    if (schema_) {
        normalizeSchemaFeatures();
        baked_ = true;
        return;
    }
    if (locomotionFeatures_) {
        normalizeLocomotionFeatures();
        baked_ = true;
        return;
    }
    featureMean_.assign(static_cast<size_t>(featureSize_), 0.f);
    featureInvStd_.assign(static_cast<size_t>(featureSize_), 0.f);
    for (const Frame& frame : frames_)
        for (int i = 0; i < featureSize_; ++i)
            featureMean_[static_cast<size_t>(i)] += frame.feature[static_cast<size_t>(i)];
    const float invCount = 1.f / static_cast<float>(frames_.size());
    for (float& mean : featureMean_) mean *= invCount;
    for (const Frame& frame : frames_)
        for (int i = 0; i < featureSize_; ++i) {
            const float d = frame.feature[static_cast<size_t>(i)] - featureMean_[static_cast<size_t>(i)];
            featureInvStd_[static_cast<size_t>(i)] += d * d;
        }
    for (float& invStd : featureInvStd_) invStd = 1.f / std::sqrt(invStd * invCount + 1e-6f);
    for (Frame& frame : frames_) normalizeFeature(frame.feature);
    baked_ = true;
}

void MotionDatabase::normalizeFeature(std::vector<float>& feature) const {
    if (static_cast<int>(feature.size()) != featureSize_ || static_cast<int>(featureMean_.size()) != featureSize_)
        return;
    for (int i = 0; i < featureSize_; ++i)
        feature[static_cast<size_t>(i)] = (feature[static_cast<size_t>(i)] - featureMean_[static_cast<size_t>(i)]) *
                                          featureInvStd_[static_cast<size_t>(i)];
}

void MotionDatabase::requireBaked() const {
    if (!baked_) throw Exception("MotionDatabase: not baked; call bake() first");
}

const MotionDatabase::Frame& MotionDatabase::frameAt(int index) const {
    requireBaked();
    if (index < 0 || index >= getFrameCount()) {
        throw Exception("MotionDatabase: invalid frame %d", index);
    }
    return frames_[static_cast<size_t>(index)];
}

float MotionDatabase::getFrameTime(int frameIndex) const { return frameAt(frameIndex).time; }

int MotionDatabase::getFrameClipIndex(int frameIndex) const { return frameAt(frameIndex).clipIndex; }

void MotionDatabase::getFeature(int frameIndex, float* out, int outCount) const {
    const Frame& f = frameAt(frameIndex);
    if (!out || outCount < featureSize_) {
        throw Exception("MotionDatabase.getFeature: buffer too small");
    }
    for (int i = 0; i < featureSize_; ++i) out[i] = f.feature[static_cast<size_t>(i)];
}

}  // namespace eve::animation
