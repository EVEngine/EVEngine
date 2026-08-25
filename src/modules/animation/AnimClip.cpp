#include "animation/AnimClip.h"
#include "animation/AnimClipRegistry.h"
#include "animation/AnimPose.h"
#include "animation/AnimSkeleton.h"

#include "common/Exception.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <functional>
#include <memory>
#include <utility>

namespace eve::animation {

AnimClip::AnimClip(std::string name) : name_(std::move(name)) {}

AnimClip::~AnimClip() { AnimClipRegistry::unregister(this); }

void AnimClip::setDuration(float seconds) {
    if (seconds < 0.f) throw Exception("AnimClip.setDuration: duration must be >= 0");
    for (const auto& event : events_)
        if (event.t > seconds)
            throw Exception("AnimClip.setDuration: event '%s' lies past duration", event.name.c_str());
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

bool AnimClip::setPositionKey(int boneIndex, int keyIndex, float time, float x, float y, float z) {
    if (time < 0.f) throw Exception("AnimClip.setPositionKey: time must be >= 0");
    if (boneIndex < 0 || boneIndex >= getTrackCount()) return false;
    auto& keys = tracks_[static_cast<size_t>(boneIndex)].positions;
    if (keyIndex < 0 || keyIndex >= static_cast<int>(keys.size())) return false;
    keys[static_cast<size_t>(keyIndex)] = {time, x, y, z};
    std::stable_sort(keys.begin(), keys.end(), [](const Vec3Key& a, const Vec3Key& b) { return a.t < b.t; });
    if (time > duration_) duration_ = time;
    return true;
}

bool AnimClip::setRotationKey(int boneIndex, int keyIndex, float time, float x, float y, float z,
                              float w) {
    if (time < 0.f) throw Exception("AnimClip.setRotationKey: time must be >= 0");
    if (boneIndex < 0 || boneIndex >= getTrackCount()) return false;
    auto& keys = tracks_[static_cast<size_t>(boneIndex)].rotations;
    if (keyIndex < 0 || keyIndex >= static_cast<int>(keys.size())) return false;
    TransformTRS normalized;
    normalized.qx = x;
    normalized.qy = y;
    normalized.qz = z;
    normalized.qw = w;
    normalized.normalizeRotation();
    keys[static_cast<size_t>(keyIndex)] =
        {time, normalized.qx, normalized.qy, normalized.qz, normalized.qw};
    std::stable_sort(keys.begin(), keys.end(), [](const QuatKey& a, const QuatKey& b) { return a.t < b.t; });
    if (time > duration_) duration_ = time;
    return true;
}

bool AnimClip::setScaleKey(int boneIndex, int keyIndex, float time, float x, float y, float z) {
    if (time < 0.f) throw Exception("AnimClip.setScaleKey: time must be >= 0");
    if (boneIndex < 0 || boneIndex >= getTrackCount()) return false;
    auto& keys = tracks_[static_cast<size_t>(boneIndex)].scales;
    if (keyIndex < 0 || keyIndex >= static_cast<int>(keys.size())) return false;
    keys[static_cast<size_t>(keyIndex)] = {time, x, y, z};
    std::stable_sort(keys.begin(), keys.end(), [](const Vec3Key& a, const Vec3Key& b) { return a.t < b.t; });
    if (time > duration_) duration_ = time;
    return true;
}

bool AnimClip::removePositionKey(int boneIndex, int keyIndex) {
    if (boneIndex < 0 || boneIndex >= getTrackCount()) return false;
    auto& keys = tracks_[static_cast<size_t>(boneIndex)].positions;
    if (keyIndex < 0 || keyIndex >= static_cast<int>(keys.size())) return false;
    keys.erase(keys.begin() + keyIndex);
    return true;
}

bool AnimClip::removeRotationKey(int boneIndex, int keyIndex) {
    if (boneIndex < 0 || boneIndex >= getTrackCount()) return false;
    auto& keys = tracks_[static_cast<size_t>(boneIndex)].rotations;
    if (keyIndex < 0 || keyIndex >= static_cast<int>(keys.size())) return false;
    keys.erase(keys.begin() + keyIndex);
    return true;
}

bool AnimClip::removeScaleKey(int boneIndex, int keyIndex) {
    if (boneIndex < 0 || boneIndex >= getTrackCount()) return false;
    auto& keys = tracks_[static_cast<size_t>(boneIndex)].scales;
    if (keyIndex < 0 || keyIndex >= static_cast<int>(keys.size())) return false;
    keys.erase(keys.begin() + keyIndex);
    return true;
}

bool AnimClip::clearTrack(int boneIndex) {
    if (boneIndex < 0 || boneIndex >= getTrackCount()) return false;
    tracks_[static_cast<size_t>(boneIndex)] = {};
    return true;
}

void AnimClip::addEvent(float time, const std::string& name, const std::string& payload) {
    if (time < 0.f) throw Exception("AnimClip.addEvent: time must be >= 0");
    if (duration_ > 0.f && time > duration_) throw Exception("AnimClip.addEvent: time lies past clip duration");
    if (name.empty()) throw Exception("AnimClip.addEvent: name is empty");
    events_.push_back({time, name, payload});
    std::stable_sort(events_.begin(), events_.end(),
                     [](const EventMarker& a, const EventMarker& b) { return a.t < b.t; });
}

bool AnimClip::setEvent(int index, float time, const std::string& name, const std::string& payload) {
    if (index < 0 || index >= getEventCount()) return false;
    if (time < 0.f) throw Exception("AnimClip.setEvent: time must be >= 0");
    if (duration_ > 0.f && time > duration_) throw Exception("AnimClip.setEvent: time lies past clip duration");
    if (name.empty()) throw Exception("AnimClip.setEvent: name is empty");
    events_[static_cast<size_t>(index)] = {time, name, payload};
    std::stable_sort(events_.begin(), events_.end(),
                     [](const EventMarker& a, const EventMarker& b) { return a.t < b.t; });
    return true;
}

bool AnimClip::removeEvent(int index) {
    if (index < 0 || index >= getEventCount()) return false;
    events_.erase(events_.begin() + index);
    return true;
}

float AnimClip::getEventTime(int index) const {
    if (index < 0 || index >= getEventCount()) return 0.f;
    return events_[static_cast<size_t>(index)].t;
}

std::string AnimClip::getEventName(int index) const {
    if (index < 0 || index >= getEventCount()) return {};
    return events_[static_cast<size_t>(index)].name;
}

std::string AnimClip::getEventPayload(int index) const {
    if (index < 0 || index >= getEventCount()) return {};
    return events_[static_cast<size_t>(index)].payload;
}

void AnimClip::collectEvents(float previousTime, float currentTime, bool loop,
                             std::vector<std::string>& out) const {
    if (events_.empty() || duration_ <= 0.f || currentTime == previousTime) return;
    const float from        = wrapTime(previousTime);
    const float to          = wrapTime(currentTime);
    auto        appendRange = [&](float lo, float hi, bool includeLo) {
        for (const EventMarker& event : events_) {
            if ((includeLo ? event.t >= lo : event.t > lo) && event.t <= hi)
                out.push_back(event.name);
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

void AnimClip::sampleLod(float time, AnimPose* out, const AnimSkeleton* skeleton, int lodLevel) const {
    if (!out || !skeleton) throw Exception("AnimClip.sampleLod: pose or skeleton is null");
    if (lodLevel < 0) throw Exception("AnimClip.sampleLod: lodLevel must be >= 0");
    const int boneCount = skeleton->getBoneCount();
    out->resize(boneCount);
    time = clampf(wrapTime(time), 0.f, duration_);
    for (int i = 0; i < boneCount; ++i) {
        const TransformTRS& bind = skeleton->bindLocal(i);
        out->local(i) = lodLevel <= skeleton->getBoneLodLimit(i) ? sampleBone(i, time, bind) : bind;
    }
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

void AnimRetargetProfile::addBoneMapping(const std::string& sourceBone, const std::string& targetBone) {
    if (sourceBone.empty() || targetBone.empty())
        throw Exception("AnimRetargetProfile.addBoneMapping: bone name must not be empty");
    for (Mapping& mapping : mappings_) {
        if (mapping.target == targetBone) {
            mapping.source = sourceBone;
            return;
        }
    }
    mappings_.push_back({sourceBone, targetBone});
}

void AnimRetargetProfile::clearBoneMappings() { mappings_.clear(); }

void AnimRetargetProfile::setRootBones(const std::string& sourceBone, const std::string& targetBone) {
    sourceRoot_ = sourceBone;
    targetRoot_ = targetBone;
}

void AnimRetargetProfile::setRootTranslationScale(float horizontal, float vertical) {
    if (horizontal < 0.f || vertical < 0.f)
        throw Exception("AnimRetargetProfile.setRootTranslationScale: scales must be >= 0");
    rootHorizontalScale_ = horizontal;
    rootVerticalScale_ = vertical;
}

std::string AnimRetargetProfile::getUnmatchedTargetBone(int index) const {
    if (index < 0 || index >= getUnmatchedBoneCount()) return {};
    return unmatchedTargetBones_[static_cast<size_t>(index)];
}

namespace {

struct RetargetQuat { float x = 0.f, y = 0.f, z = 0.f, w = 1.f; };

RetargetQuat retargetMul(const RetargetQuat& a, const RetargetQuat& b) {
    return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
            a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
            a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}

RetargetQuat retargetInverse(const RetargetQuat& q) { return {-q.x, -q.y, -q.z, q.w}; }

RetargetQuat rotationOf(const TransformTRS& transform) {
    return {transform.qx, transform.qy, transform.qz, transform.qw};
}

std::string normalizedBoneName(const std::string& name) {
    const size_t separator = name.find_last_of(":|/");
    const size_t begin = separator == std::string::npos ? 0 : separator + 1;
    std::string out;
    for (size_t i = begin; i < name.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(name[i]);
        if (std::isalnum(c)) out.push_back(static_cast<char>(std::tolower(c)));
    }
    return out;
}

int findNormalizedBone(const AnimSkeleton* skeleton, const std::string& name) {
    const std::string normalized = normalizedBoneName(name);
    int match = -1;
    for (int bone = 0; bone < skeleton->getBoneCount(); ++bone) {
        if (normalizedBoneName(skeleton->getBoneName(bone)) != normalized) continue;
        if (match >= 0) return -1;  // Ambiguous normalized names must be mapped explicitly.
        match = bone;
    }
    return match;
}

float skeletonExtent(const AnimSkeleton* skeleton) {
    AnimPose bind;
    skeleton->applyBindPose(&bind);
    bind.computeWorld(skeleton);
    if (skeleton->getBoneCount() == 0) return 1.f;
    const TransformTRS& root = bind.world(0);
    float extent = 0.f;
    for (int bone = 1; bone < skeleton->getBoneCount(); ++bone) {
        const TransformTRS& world = bind.world(bone);
        const float dx = world.px - root.px;
        const float dy = world.py - root.py;
        const float dz = world.pz - root.pz;
        extent = std::max(extent, std::sqrt(dx * dx + dy * dy + dz * dz));
    }
    return std::max(extent, 1e-6f);
}

float retargetRootMeasure(const AnimSkeleton* skeleton, const AnimPose& bindPose, int retargetRoot) {
    if (retargetRoot < 0) return skeletonExtent(skeleton);
    int hierarchyRoot = retargetRoot;
    while (skeleton->getParent(hierarchyRoot) >= 0) hierarchyRoot = skeleton->getParent(hierarchyRoot);
    const TransformTRS& root = bindPose.world(hierarchyRoot);
    const TransformTRS& pelvis = bindPose.world(retargetRoot);
    const float dx = pelvis.px - root.px;
    const float dy = pelvis.py - root.py;
    const float dz = pelvis.pz - root.pz;
    const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);
    return distance > 1e-6f ? distance : skeletonExtent(skeleton);
}

}  // namespace

AnimClip* AnimClip::retarget(const AnimSkeleton* sourceSkeleton, const AnimSkeleton* targetSkeleton) const {
    AnimRetargetProfile profile;
    return retargetWithProfile(sourceSkeleton, targetSkeleton, &profile);
}

AnimClip* AnimClip::retargetWithProfile(const AnimSkeleton* sourceSkeleton, const AnimSkeleton* targetSkeleton,
                                        AnimRetargetProfile* profile) const {
    if (!sourceSkeleton || !targetSkeleton) throw Exception("AnimClip.retargetWithProfile: skeleton is null");
    if (!profile) throw Exception("AnimClip.retargetWithProfile: profile is null");

    auto out = std::make_unique<AnimClip>(name_ + "_retargeted");
    out->duration_ = duration_;
    out->loop_ = loop_;
    out->sampleRate_ = sampleRate_;
    out->events_ = events_;
    out->tracks_.resize(static_cast<size_t>(targetSkeleton->getBoneCount()));

    std::vector<int> sourceForTarget(static_cast<size_t>(targetSkeleton->getBoneCount()), -1);
    profile->matchedBoneCount_ = 0;
    profile->unmatchedTargetBones_.clear();
    for (int targetBone = 0; targetBone < targetSkeleton->getBoneCount(); ++targetBone) {
        const std::string targetName = targetSkeleton->getBoneName(targetBone);
        int sourceBone = -1;
        bool explicitlyMapped = false;
        for (const auto& mapping : profile->mappings_) {
            if (mapping.target == targetName) {
                explicitlyMapped = true;
                sourceBone = sourceSkeleton->findBone(mapping.source);
                break;
            }
        }
        if (!explicitlyMapped) sourceBone = sourceSkeleton->findBone(targetName);
        if (!explicitlyMapped && sourceBone < 0 && profile->normalizedNameMatching_)
            sourceBone = findNormalizedBone(sourceSkeleton, targetName);
        if (sourceBone < 0) {
            profile->unmatchedTargetBones_.push_back(targetName);
            continue;
        }
        sourceForTarget[static_cast<size_t>(targetBone)] = sourceBone;
        ++profile->matchedBoneCount_;
    }

    int targetRoot = profile->targetRoot_.empty() ? -1 : targetSkeleton->findBone(profile->targetRoot_);
    int sourceRoot = profile->sourceRoot_.empty() ? -1 : sourceSkeleton->findBone(profile->sourceRoot_);
    if ((!profile->targetRoot_.empty() && targetRoot < 0) || (!profile->sourceRoot_.empty() && sourceRoot < 0))
        throw Exception("AnimClip.retargetWithProfile: configured root bone was not found");
    if (targetRoot < 0 || sourceRoot < 0) {
        for (int targetBone = 0; targetBone < targetSkeleton->getBoneCount(); ++targetBone) {
            const int candidate = sourceForTarget[static_cast<size_t>(targetBone)];
            if (candidate < 0 || candidate >= static_cast<int>(tracks_.size()) ||
                tracks_[static_cast<size_t>(candidate)].positions.empty()) continue;
            const std::string name = normalizedBoneName(targetSkeleton->getBoneName(targetBone));
            if (name.find("hips") != std::string::npos || name.find("pelvis") != std::string::npos) {
                targetRoot = targetBone;
                sourceRoot = candidate;
                break;
            }
        }
    }
    if (targetRoot < 0 || sourceRoot < 0) {
        for (int targetBone = 0; targetBone < targetSkeleton->getBoneCount(); ++targetBone) {
            if (targetSkeleton->getParent(targetBone) == -1 && sourceForTarget[static_cast<size_t>(targetBone)] >= 0) {
                targetRoot = targetBone;
                sourceRoot = sourceForTarget[static_cast<size_t>(targetBone)];
                break;
            }
        }
    }

    AnimPose sourceBindPose, targetBindPose, sourcePose;
    sourceSkeleton->applyBindPose(&sourceBindPose);
    targetSkeleton->applyBindPose(&targetBindPose);
    sourceBindPose.computeWorld(sourceSkeleton);
    targetBindPose.computeWorld(targetSkeleton);
    const float automaticScale = profile->autoRootScale_
        ? retargetRootMeasure(targetSkeleton, targetBindPose, targetRoot) /
              retargetRootMeasure(sourceSkeleton, sourceBindPose, sourceRoot)
        : 1.f;

    const int frameCount = duration_ <= 0.f ? 1 : std::max(1, static_cast<int>(std::ceil(duration_ * sampleRate_)));
    std::vector<TransformTRS> targetWorld(static_cast<size_t>(targetSkeleton->getBoneCount()));
    for (int frame = 0; frame <= frameCount; ++frame) {
        if (duration_ <= 0.f && frame > 0) break;
        const float time = duration_ <= 0.f ? 0.f : duration_ * static_cast<float>(frame) / frameCount;
        sample(time, &sourcePose, sourceSkeleton);
        sourcePose.computeWorld(sourceSkeleton);

        for (int targetBone = 0; targetBone < targetSkeleton->getBoneCount(); ++targetBone) {
            const int sourceBone = sourceForTarget[static_cast<size_t>(targetBone)];
            TransformTRS local = targetSkeleton->bindLocal(targetBone);
            if (sourceBone >= 0 && sourceBone < static_cast<int>(tracks_.size())) {
                const BoneTrack& sourceTrack = tracks_[static_cast<size_t>(sourceBone)];
                const TransformTRS& sourceLocal = sourcePose.local(sourceBone);
                const TransformTRS& sourceBind = sourceSkeleton->bindLocal(sourceBone);
                const TransformTRS& targetBind = targetSkeleton->bindLocal(targetBone);
                const float sourceLength = std::sqrt(sourceBind.px * sourceBind.px + sourceBind.py * sourceBind.py +
                                                     sourceBind.pz * sourceBind.pz);
                const float targetLength = std::sqrt(targetBind.px * targetBind.px + targetBind.py * targetBind.py +
                                                     targetBind.pz * targetBind.pz);
                const float boneScale = sourceLength > 1e-6f && targetLength > 1e-6f ? targetLength / sourceLength : 1.f;
                if (!sourceTrack.positions.empty()) {
                    if (targetBone == targetRoot && sourceBone == sourceRoot) {
                        const float horizontal = automaticScale * profile->rootHorizontalScale_;
                        const float vertical = automaticScale * profile->rootVerticalScale_;
                        local.px = targetBind.px + (sourceLocal.px - sourceBind.px) * horizontal;
                        local.py = targetBind.py + (sourceLocal.py - sourceBind.py) * vertical;
                        local.pz = targetBind.pz + (sourceLocal.pz - sourceBind.pz) * horizontal;
                    } else {
                        local.px = targetBind.px + (sourceLocal.px - sourceBind.px) * boneScale;
                        local.py = targetBind.py + (sourceLocal.py - sourceBind.py) * boneScale;
                        local.pz = targetBind.pz + (sourceLocal.pz - sourceBind.pz) * boneScale;
                    }
                    out->tracks_[static_cast<size_t>(targetBone)].positions.push_back(
                        {time, local.px, local.py, local.pz});
                }
                if (!sourceTrack.rotations.empty()) {
                    RetargetQuat desired;
                    if (profile->skeletonSpaceRotation_) {
                        desired = retargetMul(rotationOf(targetBindPose.world(targetBone)),
                                  retargetMul(retargetInverse(rotationOf(sourceBindPose.world(sourceBone))),
                                              rotationOf(sourcePose.world(sourceBone))));
                        const int parent = targetSkeleton->getParent(targetBone);
                        if (parent >= 0)
                            desired = retargetMul(retargetInverse(rotationOf(targetWorld[static_cast<size_t>(parent)])), desired);
                    } else {
                        desired = retargetMul(rotationOf(targetBind),
                                  retargetMul(retargetInverse(rotationOf(sourceBind)), rotationOf(sourceLocal)));
                    }
                    local.qx = desired.x; local.qy = desired.y; local.qz = desired.z; local.qw = desired.w;
                    local.normalizeRotation();
                    out->tracks_[static_cast<size_t>(targetBone)].rotations.push_back(
                        {time, local.qx, local.qy, local.qz, local.qw});
                }
                if (!sourceTrack.scales.empty()) {
                    local.sx = targetBind.sx * (std::abs(sourceBind.sx) > 1e-6f ? sourceLocal.sx / sourceBind.sx : sourceLocal.sx);
                    local.sy = targetBind.sy * (std::abs(sourceBind.sy) > 1e-6f ? sourceLocal.sy / sourceBind.sy : sourceLocal.sy);
                    local.sz = targetBind.sz * (std::abs(sourceBind.sz) > 1e-6f ? sourceLocal.sz / sourceBind.sz : sourceLocal.sz);
                    out->tracks_[static_cast<size_t>(targetBone)].scales.push_back({time, local.sx, local.sy, local.sz});
                }
            }
            const int parent = targetSkeleton->getParent(targetBone);
            if (parent < 0) {
                targetWorld[static_cast<size_t>(targetBone)] = local;
            } else {
                const TransformTRS& parentWorld = targetWorld[static_cast<size_t>(parent)];
                TransformTRS& world = targetWorld[static_cast<size_t>(targetBone)];
                const RetargetQuat worldRotation = retargetMul(rotationOf(parentWorld), rotationOf(local));
                world = local;
                world.qx = worldRotation.x; world.qy = worldRotation.y; world.qz = worldRotation.z; world.qw = worldRotation.w;
            }
        }
    }
    return out.release();
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
