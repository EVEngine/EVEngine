#include "animation/AnimClip.h"
#include "animation/AnimPose.h"
#include "animation/AnimSkeleton.h"

#include "common/Exception.h"

#include <algorithm>
#include <cmath>

namespace eve::animation {

AnimClip::AnimClip(std::string name) : name_(std::move(name)) {}

void AnimClip::setDuration(float seconds) {
    if (seconds < 0.f) throw Exception("AnimClip.setDuration: duration must be >= 0");
    duration_ = seconds;
}

void AnimClip::setSampleRate(float hz) {
    if (hz <= 0.f) throw Exception("AnimClip.setSampleRate: hz must be > 0");
    sampleRate_ = hz;
}

void AnimClip::ensureBone(int boneIndex) {
    if (boneIndex < 0) throw Exception("AnimClip: invalid bone index %d", boneIndex);
    if (boneIndex >= static_cast<int>(tracks_.size())) {
        tracks_.resize(static_cast<size_t>(boneIndex) + 1);
    }
}

void AnimClip::addPositionKey(int boneIndex, float time, float x, float y, float z) {
    if (time < 0.f) throw Exception("AnimClip.addPositionKey: time must be >= 0");
    ensureBone(boneIndex);
    auto &keys = tracks_[static_cast<size_t>(boneIndex)].positions;
    keys.push_back({time, x, y, z});
    std::sort(keys.begin(), keys.end(),
              [](const Vec3Key &a, const Vec3Key &b) { return a.t < b.t; });
    if (time > duration_) duration_ = time;
}

void AnimClip::addRotationKey(int boneIndex, float time, float x, float y, float z, float w) {
    if (time < 0.f) throw Exception("AnimClip.addRotationKey: time must be >= 0");
    ensureBone(boneIndex);
    QuatKey k{time, x, y, z, w};
    TransformTRS tmp;
    tmp.qx = x;
    tmp.qy = y;
    tmp.qz = z;
    tmp.qw = w;
    tmp.normalizeRotation();
    k.x = tmp.qx;
    k.y = tmp.qy;
    k.z = tmp.qz;
    k.w = tmp.qw;
    auto &keys = tracks_[static_cast<size_t>(boneIndex)].rotations;
    keys.push_back(k);
    std::sort(keys.begin(), keys.end(),
              [](const QuatKey &a, const QuatKey &b) { return a.t < b.t; });
    if (time > duration_) duration_ = time;
}

void AnimClip::addScaleKey(int boneIndex, float time, float x, float y, float z) {
    if (time < 0.f) throw Exception("AnimClip.addScaleKey: time must be >= 0");
    ensureBone(boneIndex);
    auto &keys = tracks_[static_cast<size_t>(boneIndex)].scales;
    keys.push_back({time, x, y, z});
    std::sort(keys.begin(), keys.end(),
              [](const Vec3Key &a, const Vec3Key &b) { return a.t < b.t; });
    if (time > duration_) duration_ = time;
}

int AnimClip::getPositionKeyCount(int boneIndex) const {
    if (boneIndex < 0 || boneIndex >= static_cast<int>(tracks_.size())) return 0;
    return static_cast<int>(tracks_[static_cast<size_t>(boneIndex)].positions.size());
}

int AnimClip::getRotationKeyCount(int boneIndex) const {
    if (boneIndex < 0 || boneIndex >= static_cast<int>(tracks_.size())) return 0;
    return static_cast<int>(tracks_[static_cast<size_t>(boneIndex)].rotations.size());
}

int AnimClip::getScaleKeyCount(int boneIndex) const {
    if (boneIndex < 0 || boneIndex >= static_cast<int>(tracks_.size())) return 0;
    return static_cast<int>(tracks_[static_cast<size_t>(boneIndex)].scales.size());
}

float AnimClip::wrapTime(float time) const {
    if (duration_ <= 1e-8f) return 0.f;
    if (loop_) {
        time = std::fmod(time, duration_);
        if (time < 0.f) time += duration_;
        return time;
    }
    return clampf(time, 0.f, duration_);
}

void AnimClip::sampleVec3(const std::vector<Vec3Key> &keys, float time, float &x, float &y,
                          float &z, bool &ok) {
    ok = false;
    if (keys.empty()) return;
    ok = true;
    if (time <= keys.front().t || keys.size() == 1) {
        x = keys.front().x;
        y = keys.front().y;
        z = keys.front().z;
        return;
    }
    if (time >= keys.back().t) {
        x = keys.back().x;
        y = keys.back().y;
        z = keys.back().z;
        return;
    }
    for (size_t i = 0; i + 1 < keys.size(); ++i) {
        const auto &a = keys[i];
        const auto &b = keys[i + 1];
        if (time >= a.t && time <= b.t) {
            const float den = b.t - a.t;
            const float t   = den > 1e-8f ? (time - a.t) / den : 0.f;
            x               = lerpf(a.x, b.x, t);
            y               = lerpf(a.y, b.y, t);
            z               = lerpf(a.z, b.z, t);
            return;
        }
    }
    x = keys.back().x;
    y = keys.back().y;
    z = keys.back().z;
}

void AnimClip::sampleQuat(const std::vector<QuatKey> &keys, float time, float &x, float &y,
                          float &z, float &w, bool &ok) {
    ok = false;
    if (keys.empty()) return;
    ok = true;
    if (time <= keys.front().t || keys.size() == 1) {
        x = keys.front().x;
        y = keys.front().y;
        z = keys.front().z;
        w = keys.front().w;
        return;
    }
    if (time >= keys.back().t) {
        x = keys.back().x;
        y = keys.back().y;
        z = keys.back().z;
        w = keys.back().w;
        return;
    }
    for (size_t i = 0; i + 1 < keys.size(); ++i) {
        const auto &a = keys[i];
        const auto &b = keys[i + 1];
        if (time >= a.t && time <= b.t) {
            const float den = b.t - a.t;
            const float t   = den > 1e-8f ? (time - a.t) / den : 0.f;
            slerpQuat(a.x, a.y, a.z, a.w, b.x, b.y, b.z, b.w, t, x, y, z, w);
            return;
        }
    }
    x = keys.back().x;
    y = keys.back().y;
    z = keys.back().z;
    w = keys.back().w;
}

TransformTRS AnimClip::sampleBone(int boneIndex, float time, const TransformTRS &fallback) const {
    TransformTRS out = fallback;
    if (boneIndex < 0 || boneIndex >= static_cast<int>(tracks_.size())) return out;
    const BoneTrack &tr = tracks_[static_cast<size_t>(boneIndex)];
    bool ok             = false;
    float x, y, z, w;
    sampleVec3(tr.positions, time, x, y, z, ok);
    if (ok) {
        out.px = x;
        out.py = y;
        out.pz = z;
    }
    sampleQuat(tr.rotations, time, x, y, z, w, ok);
    if (ok) {
        out.qx = x;
        out.qy = y;
        out.qz = z;
        out.qw = w;
    }
    sampleVec3(tr.scales, time, x, y, z, ok);
    if (ok) {
        out.sx = x;
        out.sy = y;
        out.sz = z;
    }
    return out;
}

void AnimClip::sample(float time, AnimPose *out, const AnimSkeleton *skeleton) const {
    if (!out) throw Exception("AnimClip.sample: pose is null");
    const int boneCount = skeleton ? skeleton->getBoneCount()
                                   : static_cast<int>(tracks_.size());
    out->resize(boneCount);
    time = wrapTime(time);
    for (int i = 0; i < boneCount; ++i) {
        const TransformTRS &fb =
            skeleton ? skeleton->bindLocal(i) : TransformTRS::identity();
        out->local(i) = sampleBone(i, time, fb);
    }
}

}  // namespace eve::animation
