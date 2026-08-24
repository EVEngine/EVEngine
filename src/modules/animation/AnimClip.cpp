#include "animation/AnimClip.h"
#include "animation/AnimClipRegistry.h"
#include "animation/AnimPose.h"
#include "animation/AnimSkeleton.h"

#include "common/Exception.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <utility>

namespace eve::animation {

AnimClip::AnimClip(std::string name) : name_(std::move(name)) {}

AnimClip::~AnimClip() { AnimClipRegistry::unregister(this); }

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
    auto& keys = tracks_[static_cast<size_t>(boneIndex)].positions;
    keys.push_back({time, x, y, z});
    std::sort(keys.begin(), keys.end(), [](const Vec3Key& a, const Vec3Key& b) { return a.t < b.t; });
    if (time > duration_) duration_ = time;
}

void AnimClip::addRotationKey(int boneIndex, float time, float x, float y, float z, float w) {
    if (time < 0.f) throw Exception("AnimClip.addRotationKey: time must be >= 0");
    ensureBone(boneIndex);
    QuatKey      k{time, x, y, z, w};
    TransformTRS tmp;
    tmp.qx = x;
    tmp.qy = y;
    tmp.qz = z;
    tmp.qw = w;
    tmp.normalizeRotation();
    k.x        = tmp.qx;
    k.y        = tmp.qy;
    k.z        = tmp.qz;
    k.w        = tmp.qw;
    auto& keys = tracks_[static_cast<size_t>(boneIndex)].rotations;
    keys.push_back(k);
    std::sort(keys.begin(), keys.end(), [](const QuatKey& a, const QuatKey& b) { return a.t < b.t; });
    if (time > duration_) duration_ = time;
}

void AnimClip::addScaleKey(int boneIndex, float time, float x, float y, float z) {
    if (time < 0.f) throw Exception("AnimClip.addScaleKey: time must be >= 0");
    ensureBone(boneIndex);
    auto& keys = tracks_[static_cast<size_t>(boneIndex)].scales;
    keys.push_back({time, x, y, z});
    std::sort(keys.begin(), keys.end(), [](const Vec3Key& a, const Vec3Key& b) { return a.t < b.t; });
    if (time > duration_) duration_ = time;
}

void AnimClip::addEvent(float time, const std::string& name) {
    if (time < 0.f || time > duration_) throw Exception("AnimClip.addEvent: time out of range");
    if (name.empty()) throw Exception("AnimClip.addEvent: name is empty");
    events_.push_back({time, name});
    std::stable_sort(events_.begin(), events_.end(), [](const EventKey& a, const EventKey& b) { return a.t < b.t; });
}

float AnimClip::getEventTime(int eventIndex) const {
    if (eventIndex < 0 || eventIndex >= getEventCount())
        throw Exception("AnimClip: invalid event index %d", eventIndex);
    return events_[static_cast<size_t>(eventIndex)].t;
}

std::string AnimClip::getEventName(int eventIndex) const {
    if (eventIndex < 0 || eventIndex >= getEventCount())
        throw Exception("AnimClip: invalid event index %d", eventIndex);
    return events_[static_cast<size_t>(eventIndex)].name;
}

void AnimClip::collectEvents(float previousTime, float currentTime, bool loop, std::vector<std::string>& out) const {
    if (events_.empty() || duration_ <= 0.f || currentTime == previousTime) return;
    const float from        = wrapTime(previousTime);
    const float to          = wrapTime(currentTime);
    auto        appendRange = [&](float lo, float hi, bool includeLo) {
        for (const EventKey& event : events_) {
            if ((includeLo ? event.t >= lo : event.t > lo) && event.t <= hi) out.push_back(event.name);
        }
    };
    if (loop && (currentTime - previousTime >= duration_ || to < from)) {
        appendRange(from, duration_, false);
        appendRange(0.f, to, true);
    } else {
        appendRange(from, to, false);
    }
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

namespace {
void requireKey(int boneIndex, int keyIndex, int count, const char* what) {
    if (boneIndex < 0 || keyIndex < 0 || keyIndex >= count) {
        throw Exception("AnimClip: invalid %s key bone=%d index=%d", what, boneIndex, keyIndex);
    }
}
}  // namespace

float AnimClip::getPositionKeyTime(int boneIndex, int keyIndex) const {
    requireKey(boneIndex, keyIndex, getPositionKeyCount(boneIndex), "position");
    return tracks_[static_cast<size_t>(boneIndex)].positions[static_cast<size_t>(keyIndex)].t;
}
float AnimClip::getPositionKeyX(int boneIndex, int keyIndex) const {
    requireKey(boneIndex, keyIndex, getPositionKeyCount(boneIndex), "position");
    return tracks_[static_cast<size_t>(boneIndex)].positions[static_cast<size_t>(keyIndex)].x;
}
float AnimClip::getPositionKeyY(int boneIndex, int keyIndex) const {
    requireKey(boneIndex, keyIndex, getPositionKeyCount(boneIndex), "position");
    return tracks_[static_cast<size_t>(boneIndex)].positions[static_cast<size_t>(keyIndex)].y;
}
float AnimClip::getPositionKeyZ(int boneIndex, int keyIndex) const {
    requireKey(boneIndex, keyIndex, getPositionKeyCount(boneIndex), "position");
    return tracks_[static_cast<size_t>(boneIndex)].positions[static_cast<size_t>(keyIndex)].z;
}

float AnimClip::getRotationKeyTime(int boneIndex, int keyIndex) const {
    requireKey(boneIndex, keyIndex, getRotationKeyCount(boneIndex), "rotation");
    return tracks_[static_cast<size_t>(boneIndex)].rotations[static_cast<size_t>(keyIndex)].t;
}
float AnimClip::getRotationKeyX(int boneIndex, int keyIndex) const {
    requireKey(boneIndex, keyIndex, getRotationKeyCount(boneIndex), "rotation");
    return tracks_[static_cast<size_t>(boneIndex)].rotations[static_cast<size_t>(keyIndex)].x;
}
float AnimClip::getRotationKeyY(int boneIndex, int keyIndex) const {
    requireKey(boneIndex, keyIndex, getRotationKeyCount(boneIndex), "rotation");
    return tracks_[static_cast<size_t>(boneIndex)].rotations[static_cast<size_t>(keyIndex)].y;
}
float AnimClip::getRotationKeyZ(int boneIndex, int keyIndex) const {
    requireKey(boneIndex, keyIndex, getRotationKeyCount(boneIndex), "rotation");
    return tracks_[static_cast<size_t>(boneIndex)].rotations[static_cast<size_t>(keyIndex)].z;
}
float AnimClip::getRotationKeyW(int boneIndex, int keyIndex) const {
    requireKey(boneIndex, keyIndex, getRotationKeyCount(boneIndex), "rotation");
    return tracks_[static_cast<size_t>(boneIndex)].rotations[static_cast<size_t>(keyIndex)].w;
}

float AnimClip::getScaleKeyTime(int boneIndex, int keyIndex) const {
    requireKey(boneIndex, keyIndex, getScaleKeyCount(boneIndex), "scale");
    return tracks_[static_cast<size_t>(boneIndex)].scales[static_cast<size_t>(keyIndex)].t;
}
float AnimClip::getScaleKeyX(int boneIndex, int keyIndex) const {
    requireKey(boneIndex, keyIndex, getScaleKeyCount(boneIndex), "scale");
    return tracks_[static_cast<size_t>(boneIndex)].scales[static_cast<size_t>(keyIndex)].x;
}
float AnimClip::getScaleKeyY(int boneIndex, int keyIndex) const {
    requireKey(boneIndex, keyIndex, getScaleKeyCount(boneIndex), "scale");
    return tracks_[static_cast<size_t>(boneIndex)].scales[static_cast<size_t>(keyIndex)].y;
}
float AnimClip::getScaleKeyZ(int boneIndex, int keyIndex) const {
    requireKey(boneIndex, keyIndex, getScaleKeyCount(boneIndex), "scale");
    return tracks_[static_cast<size_t>(boneIndex)].scales[static_cast<size_t>(keyIndex)].z;
}

void AnimClip::applyPlanarRootMotion(int boneIndex, float speedX, float speedZ) {
    ensureBone(boneIndex);
    auto& keys = tracks_[static_cast<size_t>(boneIndex)].positions;
    if (keys.empty()) {
        // Create start/end keys from zero so motion matching sees a trajectory.
        keys.push_back({0.f, 0.f, 0.f, 0.f});
        keys.push_back({duration_, speedX * duration_, 0.f, speedZ * duration_});
        return;
    }
    for (auto& k : keys) {
        k.x += speedX * k.t;
        k.z += speedZ * k.t;
    }
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

void AnimClip::sampleVec3(const std::vector<Vec3Key>& keys, float time, float& x, float& y, float& z, bool& ok) {
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
    const auto upper =
        std::upper_bound(keys.begin(), keys.end(), time, [](float value, const Vec3Key& key) { return value < key.t; });
    const auto& b   = *upper;
    const auto& a   = *(upper - 1);
    const float den = b.t - a.t;
    const float t   = den > 1e-8f ? (time - a.t) / den : 0.f;
    x               = lerpf(a.x, b.x, t);
    y               = lerpf(a.y, b.y, t);
    z               = lerpf(a.z, b.z, t);
}

void AnimClip::sampleQuat(const std::vector<QuatKey>& keys, float time, float& x, float& y, float& z, float& w,
                          bool& ok) {
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
    const auto upper =
        std::upper_bound(keys.begin(), keys.end(), time, [](float value, const QuatKey& key) { return value < key.t; });
    const auto& b   = *upper;
    const auto& a   = *(upper - 1);
    const float den = b.t - a.t;
    const float t   = den > 1e-8f ? (time - a.t) / den : 0.f;
    slerpQuat(a.x, a.y, a.z, a.w, b.x, b.y, b.z, b.w, t, x, y, z, w);
}

TransformTRS AnimClip::sampleBone(int boneIndex, float time, const TransformTRS& fallback) const {
    TransformTRS out = fallback;
    if (boneIndex < 0 || boneIndex >= static_cast<int>(tracks_.size())) return out;
    const BoneTrack& tr = tracks_[static_cast<size_t>(boneIndex)];
    bool             ok = false;
    float            x, y, z, w;
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

void AnimClip::sample(float time, AnimPose* out, const AnimSkeleton* skeleton) const {
    sampleClamped(wrapTime(time), out, skeleton);
}

void AnimClip::sampleClamped(float time, AnimPose* out, const AnimSkeleton* skeleton) const {
    if (!out) throw Exception("AnimClip.sample: pose is null");
    const int boneCount = skeleton ? skeleton->getBoneCount() : static_cast<int>(tracks_.size());
    out->resize(boneCount);
    time = clampf(time, 0.f, duration_);
    for (int i = 0; i < boneCount; ++i) {
        const TransformTRS& fb = skeleton ? skeleton->bindLocal(i) : TransformTRS::identity();
        out->local(i)          = sampleBone(i, time, fb);
    }
}

int AnimClip::compress(float positionError, float rotationErrorDegrees, float scaleError) {
    if (positionError < 0.f || rotationErrorDegrees < 0.f || scaleError < 0.f)
        throw Exception("AnimClip.compress: tolerances must be >= 0");
    const float rotationError = rotationErrorDegrees * 0.01745329251994329577f;
    int removed = 0;
    for (BoneTrack& track : tracks_) {
        auto reduceVec3 = [&](std::vector<Vec3Key>& keys, float tolerance) {
            if (keys.size() <= 2) return;
            std::vector<unsigned char> keep(keys.size(), 0);
            keep.front() = keep.back() = 1;
            std::function<void(size_t, size_t)> split = [&](size_t first, size_t last) {
                if (last <= first + 1) return;
                const float span = keys[last].t - keys[first].t;
                float worst = -1.f;
                size_t worstIndex = first;
                for (size_t i = first + 1; i < last; ++i) {
                    const float alpha = span > 1e-8f ? (keys[i].t - keys[first].t) / span : 0.f;
                    const float dx = keys[i].x - lerpf(keys[first].x, keys[last].x, alpha);
                    const float dy = keys[i].y - lerpf(keys[first].y, keys[last].y, alpha);
                    const float dz = keys[i].z - lerpf(keys[first].z, keys[last].z, alpha);
                    const float error = std::sqrt(dx * dx + dy * dy + dz * dz);
                    if (error > worst) { worst = error; worstIndex = i; }
                }
                if (worst > tolerance) {
                    keep[worstIndex] = 1;
                    split(first, worstIndex);
                    split(worstIndex, last);
                }
            };
            split(0, keys.size() - 1);
            std::vector<Vec3Key> reduced;
            reduced.reserve(keys.size());
            for (size_t i = 0; i < keys.size(); ++i) if (keep[i]) reduced.push_back(keys[i]);
            removed += static_cast<int>(keys.size() - reduced.size());
            keys.swap(reduced);
        };
        auto reduceQuat = [&](std::vector<QuatKey>& keys) {
            if (keys.size() <= 2) return;
            std::vector<unsigned char> keep(keys.size(), 0);
            keep.front() = keep.back() = 1;
            std::function<void(size_t, size_t)> split = [&](size_t first, size_t last) {
                if (last <= first + 1) return;
                const float span = keys[last].t - keys[first].t;
                float worst = -1.f;
                size_t worstIndex = first;
                for (size_t i = first + 1; i < last; ++i) {
                    const float alpha = span > 1e-8f ? (keys[i].t - keys[first].t) / span : 0.f;
                    float x, y, z, w;
                    slerpQuat(keys[first].x, keys[first].y, keys[first].z, keys[first].w,
                              keys[last].x, keys[last].y, keys[last].z, keys[last].w,
                              alpha, x, y, z, w);
                    const float dot = std::abs(x * keys[i].x + y * keys[i].y + z * keys[i].z + w * keys[i].w);
                    const float error = 2.f * std::acos(clampf(dot, -1.f, 1.f));
                    if (error > worst) { worst = error; worstIndex = i; }
                }
                if (worst > rotationError) {
                    keep[worstIndex] = 1;
                    split(first, worstIndex);
                    split(worstIndex, last);
                }
            };
            split(0, keys.size() - 1);
            std::vector<QuatKey> reduced;
            reduced.reserve(keys.size());
            for (size_t i = 0; i < keys.size(); ++i) if (keep[i]) reduced.push_back(keys[i]);
            removed += static_cast<int>(keys.size() - reduced.size());
            keys.swap(reduced);
        };
        reduceVec3(track.positions, positionError);
        reduceQuat(track.rotations);
        reduceVec3(track.scales, scaleError);
    }
    return removed;
}

AnimClip* AnimClip::retarget(const AnimSkeleton* sourceSkeleton, const AnimSkeleton* targetSkeleton) const {
    if (!sourceSkeleton || !targetSkeleton) throw Exception("AnimClip.retarget: skeleton is null");
    auto* out = new AnimClip(name_ + "_retargeted");
    out->duration_ = duration_;
    out->loop_ = loop_;
    out->sampleRate_ = sampleRate_;
    out->events_ = events_;
    out->tracks_.resize(static_cast<size_t>(targetSkeleton->getBoneCount()));
    auto multiplyQuat = [](float ax, float ay, float az, float aw, float bx, float by, float bz, float bw,
                           float& x, float& y, float& z, float& w) {
        x = aw * bx + ax * bw + ay * bz - az * by;
        y = aw * by - ax * bz + ay * bw + az * bx;
        z = aw * bz + ax * by - ay * bx + az * bw;
        w = aw * bw - ax * bx - ay * by - az * bz;
    };
    for (int targetBone = 0; targetBone < targetSkeleton->getBoneCount(); ++targetBone) {
        const int sourceBone = sourceSkeleton->findBone(targetSkeleton->getBoneName(targetBone));
        if (sourceBone < 0 || sourceBone >= static_cast<int>(tracks_.size())) continue;
        const TransformTRS& sourceBind = sourceSkeleton->bindLocal(sourceBone);
        const TransformTRS& targetBind = targetSkeleton->bindLocal(targetBone);
        const float sourceLength = std::sqrt(sourceBind.px * sourceBind.px + sourceBind.py * sourceBind.py +
                                             sourceBind.pz * sourceBind.pz);
        const float targetLength = std::sqrt(targetBind.px * targetBind.px + targetBind.py * targetBind.py +
                                             targetBind.pz * targetBind.pz);
        const float translationScale = sourceLength > 1e-6f && targetLength > 1e-6f
                                           ? targetLength / sourceLength : 1.f;
        const BoneTrack& source = tracks_[static_cast<size_t>(sourceBone)];
        BoneTrack& target = out->tracks_[static_cast<size_t>(targetBone)];
        for (const Vec3Key& key : source.positions) {
            target.positions.push_back({key.t,
                targetBind.px + (key.x - sourceBind.px) * translationScale,
                targetBind.py + (key.y - sourceBind.py) * translationScale,
                targetBind.pz + (key.z - sourceBind.pz) * translationScale});
        }
        for (const QuatKey& key : source.rotations) {
            float dx, dy, dz, dw;
            multiplyQuat(-sourceBind.qx, -sourceBind.qy, -sourceBind.qz, sourceBind.qw,
                         key.x, key.y, key.z, key.w, dx, dy, dz, dw);
            QuatKey result{key.t};
            multiplyQuat(targetBind.qx, targetBind.qy, targetBind.qz, targetBind.qw,
                         dx, dy, dz, dw, result.x, result.y, result.z, result.w);
            TransformTRS normalized;
            normalized.qx = result.x; normalized.qy = result.y; normalized.qz = result.z; normalized.qw = result.w;
            normalized.normalizeRotation();
            result.x = normalized.qx; result.y = normalized.qy; result.z = normalized.qz; result.w = normalized.qw;
            target.rotations.push_back(result);
        }
        for (const Vec3Key& key : source.scales) {
            target.scales.push_back({key.t,
                targetBind.sx * (std::abs(sourceBind.sx) > 1e-6f ? key.x / sourceBind.sx : key.x),
                targetBind.sy * (std::abs(sourceBind.sy) > 1e-6f ? key.y / sourceBind.sy : key.y),
                targetBind.sz * (std::abs(sourceBind.sz) > 1e-6f ? key.z / sourceBind.sz : key.z)});
        }
    }
    return out;
}

void AnimClip::adopt(AnimClip& other) {
    std::swap(name_, other.name_);
    std::swap(duration_, other.duration_);
    std::swap(loop_, other.loop_);
    std::swap(sampleRate_, other.sampleRate_);
    std::swap(tracks_, other.tracks_);
    std::swap(events_, other.events_);
}

}  // namespace eve::animation
