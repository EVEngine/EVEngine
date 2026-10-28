#include "animation/MotionBuilder.h"
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <tuple>
#include "animation/AnimClip.h"
#include "animation/AnimParallelInternal.h"
#include "animation/AnimSkeleton.h"
#include "animation/AnimTransformInternal.h"
#include "animation/MotionDatabase.h"
#include "animation/MotionFeatureInternal.h"
#include "animation/MotionSchemaInternal.h"
#include "common/Diagnostic.h"
#include "common/Exception.h"

// MotionBuilder.cpp
namespace eve::animation {
namespace {

[[nodiscard]] eve::Diagnostic consumedDiag(const char* api) {
    return eve::Diagnostic::error(eve::DiagnosticCode::PreconditionViolation,
                                  std::string(api) + ": builder already consumed");
}

}  // namespace

MotionBuilder::MotionBuilder(MotionRuntime& runtime, float from, float to, float duration) : runtime_(runtime) {
    desc_.from     = from;
    desc_.to       = to;
    desc_.duration = duration;
}

MotionBuilder& MotionBuilder::ease(std::string kind) {
    desc_.ease = std::move(kind);
    return *this;
}

MotionBuilder& MotionBuilder::delay(float seconds) {
    desc_.delay = seconds;
    return *this;
}

MotionBuilder& MotionBuilder::loops(int count, MotionLoopMode mode) {
    desc_.loops    = count;
    desc_.loopMode = mode;
    return *this;
}

MotionBuilder& MotionBuilder::onUpdate(MotionRuntime::FloatCallback cb) {
    desc_.onUpdate = std::move(cb);
    return *this;
}

MotionBuilder& MotionBuilder::onComplete(MotionRuntime::VoidCallback cb) {
    desc_.onComplete = std::move(cb);
    return *this;
}

MotionBuilder& MotionBuilder::onCancel(MotionRuntime::VoidCallback cb) {
    desc_.onCancel = std::move(cb);
    return *this;
}

MotionBuilder& MotionBuilder::cancelOnError(bool enabled) {
    desc_.cancelOnError = enabled;
    return *this;
}

MotionBuilder& MotionBuilder::to(IMotionFloatSink& sink) {
    desc_.sink = &sink;
    return *this;
}

eve::Result<MotionHandle> MotionBuilder::run() {
    if (consumed_) return eve::Result<MotionHandle>::failure(consumedDiag("MotionBuilder.run"));
    consumed_  = true;
    desc_.sink = nullptr;
    return runtime_.spawnFloat(desc_);
}

eve::Result<MotionHandle> MotionBuilder::bind(IMotionFloatSink& sink) {
    if (consumed_) return eve::Result<MotionHandle>::failure(consumedDiag("MotionBuilder.bind"));
    consumed_  = true;
    desc_.sink = &sink;
    return runtime_.spawnFloat(desc_);
}

eve::Result<MotionRuntime::FloatDesc> MotionBuilder::takeDesc() {
    if (consumed_) return eve::Result<MotionRuntime::FloatDesc>::failure(consumedDiag("MotionBuilder.takeDesc"));
    consumed_ = true;
    return eve::Result<MotionRuntime::FloatDesc>::success(std::move(desc_));
}

MotionVec2Builder::MotionVec2Builder(MotionRuntime& runtime, MotionVec2 from, MotionVec2 to, float duration)
    : runtime_(runtime) {
    desc_.from     = from;
    desc_.to       = to;
    desc_.duration = duration;
}

MotionVec2Builder& MotionVec2Builder::ease(std::string kind) {
    desc_.ease = std::move(kind);
    return *this;
}

MotionVec2Builder& MotionVec2Builder::delay(float seconds) {
    desc_.delay = seconds;
    return *this;
}

MotionVec2Builder& MotionVec2Builder::loops(int count, MotionLoopMode mode) {
    desc_.loops    = count;
    desc_.loopMode = mode;
    return *this;
}

MotionVec2Builder& MotionVec2Builder::onUpdate(MotionRuntime::Vec2Callback cb) {
    desc_.onUpdate = std::move(cb);
    return *this;
}

MotionVec2Builder& MotionVec2Builder::onComplete(MotionRuntime::VoidCallback cb) {
    desc_.onComplete = std::move(cb);
    return *this;
}

MotionVec2Builder& MotionVec2Builder::onCancel(MotionRuntime::VoidCallback cb) {
    desc_.onCancel = std::move(cb);
    return *this;
}

MotionVec2Builder& MotionVec2Builder::cancelOnError(bool enabled) {
    desc_.cancelOnError = enabled;
    return *this;
}

MotionVec2Builder& MotionVec2Builder::to(IMotionVec2Sink& sink) {
    desc_.sink = &sink;
    return *this;
}

eve::Result<MotionHandle> MotionVec2Builder::run() {
    if (consumed_) return eve::Result<MotionHandle>::failure(consumedDiag("MotionVec2Builder.run"));
    consumed_  = true;
    desc_.sink = nullptr;
    return runtime_.spawnVec2(desc_);
}

eve::Result<MotionHandle> MotionVec2Builder::bind(IMotionVec2Sink& sink) {
    if (consumed_) return eve::Result<MotionHandle>::failure(consumedDiag("MotionVec2Builder.bind"));
    consumed_  = true;
    desc_.sink = &sink;
    return runtime_.spawnVec2(desc_);
}

eve::Result<MotionRuntime::Vec2Desc> MotionVec2Builder::takeDesc() {
    if (consumed_) return eve::Result<MotionRuntime::Vec2Desc>::failure(consumedDiag("MotionVec2Builder.takeDesc"));
    consumed_ = true;
    return eve::Result<MotionRuntime::Vec2Desc>::success(std::move(desc_));
}

MotionVec3Builder::MotionVec3Builder(MotionRuntime& runtime, MotionVec3 from, MotionVec3 to, float duration)
    : runtime_(runtime) {
    desc_.from     = from;
    desc_.to       = to;
    desc_.duration = duration;
}

MotionVec3Builder& MotionVec3Builder::ease(std::string kind) {
    desc_.ease = std::move(kind);
    return *this;
}

MotionVec3Builder& MotionVec3Builder::delay(float seconds) {
    desc_.delay = seconds;
    return *this;
}

MotionVec3Builder& MotionVec3Builder::loops(int count, MotionLoopMode mode) {
    desc_.loops    = count;
    desc_.loopMode = mode;
    return *this;
}

MotionVec3Builder& MotionVec3Builder::onUpdate(MotionRuntime::Vec3Callback cb) {
    desc_.onUpdate = std::move(cb);
    return *this;
}

MotionVec3Builder& MotionVec3Builder::onComplete(MotionRuntime::VoidCallback cb) {
    desc_.onComplete = std::move(cb);
    return *this;
}

MotionVec3Builder& MotionVec3Builder::onCancel(MotionRuntime::VoidCallback cb) {
    desc_.onCancel = std::move(cb);
    return *this;
}

MotionVec3Builder& MotionVec3Builder::cancelOnError(bool enabled) {
    desc_.cancelOnError = enabled;
    return *this;
}

MotionVec3Builder& MotionVec3Builder::to(IMotionVec3Sink& sink) {
    desc_.sink = &sink;
    return *this;
}

eve::Result<MotionHandle> MotionVec3Builder::run() {
    if (consumed_) return eve::Result<MotionHandle>::failure(consumedDiag("MotionVec3Builder.run"));
    consumed_  = true;
    desc_.sink = nullptr;
    return runtime_.spawnVec3(desc_);
}

eve::Result<MotionHandle> MotionVec3Builder::bind(IMotionVec3Sink& sink) {
    if (consumed_) return eve::Result<MotionHandle>::failure(consumedDiag("MotionVec3Builder.bind"));
    consumed_  = true;
    desc_.sink = &sink;
    return runtime_.spawnVec3(desc_);
}

eve::Result<MotionRuntime::Vec3Desc> MotionVec3Builder::takeDesc() {
    if (consumed_) return eve::Result<MotionRuntime::Vec3Desc>::failure(consumedDiag("MotionVec3Builder.takeDesc"));
    consumed_ = true;
    return eve::Result<MotionRuntime::Vec3Desc>::success(std::move(desc_));
}


MotionBuilder& MotionBuilder::style(MotionStyle style) {
    desc_.style = style;
    return *this;
}
MotionBuilder& MotionBuilder::frequency(int count) {
    desc_.frequency = count;
    return *this;
}
MotionBuilder& MotionBuilder::dampingRatio(float ratio) {
    desc_.dampingRatio = ratio;
    return *this;
}
MotionBuilder& MotionBuilder::seed(std::uint32_t value) {
    desc_.seed = value;
    return *this;
}

MotionVec2Builder& MotionVec2Builder::style(MotionStyle style) {
    desc_.style = style;
    return *this;
}
MotionVec2Builder& MotionVec2Builder::frequency(int count) {
    desc_.frequency = count;
    return *this;
}
MotionVec2Builder& MotionVec2Builder::dampingRatio(float ratio) {
    desc_.dampingRatio = ratio;
    return *this;
}
MotionVec2Builder& MotionVec2Builder::seed(std::uint32_t value) {
    desc_.seed = value;
    return *this;
}

MotionVec3Builder& MotionVec3Builder::style(MotionStyle style) {
    desc_.style = style;
    return *this;
}
MotionVec3Builder& MotionVec3Builder::frequency(int count) {
    desc_.frequency = count;
    return *this;
}
MotionVec3Builder& MotionVec3Builder::dampingRatio(float ratio) {
    desc_.dampingRatio = ratio;
    return *this;
}
MotionVec3Builder& MotionVec3Builder::seed(std::uint32_t value) {
    desc_.seed = value;
    return *this;
}

MotionColorBuilder::MotionColorBuilder(MotionRuntime& runtime, MotionColor from, MotionColor to, float duration)
    : runtime_(runtime) {
    desc_.from     = from;
    desc_.to       = to;
    desc_.duration = duration;
}
MotionColorBuilder& MotionColorBuilder::ease(std::string kind) {
    desc_.ease = std::move(kind);
    return *this;
}
MotionColorBuilder& MotionColorBuilder::delay(float seconds) {
    desc_.delay = seconds;
    return *this;
}
MotionColorBuilder& MotionColorBuilder::loops(int count, MotionLoopMode mode) {
    desc_.loops    = count;
    desc_.loopMode = mode;
    return *this;
}
MotionColorBuilder& MotionColorBuilder::onUpdate(MotionRuntime::ColorCallback cb) {
    desc_.onUpdate = std::move(cb);
    return *this;
}
MotionColorBuilder& MotionColorBuilder::onComplete(MotionRuntime::VoidCallback cb) {
    desc_.onComplete = std::move(cb);
    return *this;
}
MotionColorBuilder& MotionColorBuilder::onCancel(MotionRuntime::VoidCallback cb) {
    desc_.onCancel = std::move(cb);
    return *this;
}
MotionColorBuilder& MotionColorBuilder::cancelOnError(bool enabled) {
    desc_.cancelOnError = enabled;
    return *this;
}
MotionColorBuilder& MotionColorBuilder::to(IMotionColorSink& sink) {
    desc_.sink = &sink;
    return *this;
}
eve::Result<MotionHandle> MotionColorBuilder::run() {
    if (consumed_) return eve::Result<MotionHandle>::failure(consumedDiag("MotionColorBuilder.run"));
    consumed_  = true;
    desc_.sink = nullptr;
    return runtime_.spawnColor(desc_);
}
eve::Result<MotionHandle> MotionColorBuilder::bind(IMotionColorSink& sink) {
    if (consumed_) return eve::Result<MotionHandle>::failure(consumedDiag("MotionColorBuilder.bind"));
    consumed_  = true;
    desc_.sink = &sink;
    return runtime_.spawnColor(desc_);
}
eve::Result<MotionRuntime::ColorDesc> MotionColorBuilder::takeDesc() {
    if (consumed_) return eve::Result<MotionRuntime::ColorDesc>::failure(consumedDiag("MotionColorBuilder.takeDesc"));
    consumed_ = true;
    return eve::Result<MotionRuntime::ColorDesc>::success(std::move(desc_));
}

MotionQuatBuilder::MotionQuatBuilder(MotionRuntime& runtime, MotionQuat from, MotionQuat to, float duration)
    : runtime_(runtime) {
    desc_.from     = from;
    desc_.to       = to;
    desc_.duration = duration;
}
MotionQuatBuilder& MotionQuatBuilder::ease(std::string kind) {
    desc_.ease = std::move(kind);
    return *this;
}
MotionQuatBuilder& MotionQuatBuilder::delay(float seconds) {
    desc_.delay = seconds;
    return *this;
}
MotionQuatBuilder& MotionQuatBuilder::loops(int count, MotionLoopMode mode) {
    desc_.loops    = count;
    desc_.loopMode = mode;
    return *this;
}
MotionQuatBuilder& MotionQuatBuilder::onUpdate(MotionRuntime::QuatCallback cb) {
    desc_.onUpdate = std::move(cb);
    return *this;
}
MotionQuatBuilder& MotionQuatBuilder::onComplete(MotionRuntime::VoidCallback cb) {
    desc_.onComplete = std::move(cb);
    return *this;
}
MotionQuatBuilder& MotionQuatBuilder::onCancel(MotionRuntime::VoidCallback cb) {
    desc_.onCancel = std::move(cb);
    return *this;
}
MotionQuatBuilder& MotionQuatBuilder::cancelOnError(bool enabled) {
    desc_.cancelOnError = enabled;
    return *this;
}
MotionQuatBuilder& MotionQuatBuilder::to(IMotionQuatSink& sink) {
    desc_.sink = &sink;
    return *this;
}
eve::Result<MotionHandle> MotionQuatBuilder::run() {
    if (consumed_) return eve::Result<MotionHandle>::failure(consumedDiag("MotionQuatBuilder.run"));
    consumed_  = true;
    desc_.sink = nullptr;
    return runtime_.spawnQuat(desc_);
}
eve::Result<MotionHandle> MotionQuatBuilder::bind(IMotionQuatSink& sink) {
    if (consumed_) return eve::Result<MotionHandle>::failure(consumedDiag("MotionQuatBuilder.bind"));
    consumed_  = true;
    desc_.sink = &sink;
    return runtime_.spawnQuat(desc_);
}
eve::Result<MotionRuntime::QuatDesc> MotionQuatBuilder::takeDesc() {
    if (consumed_) return eve::Result<MotionRuntime::QuatDesc>::failure(consumedDiag("MotionQuatBuilder.takeDesc"));
    consumed_ = true;
    return eve::Result<MotionRuntime::QuatDesc>::success(std::move(desc_));
}

}  // namespace eve::animation

// MotionDatabase.cpp
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
    featureSize_ = schema_ ? schema_->dimension
                           : (locomotionFeatures_ ? 30 : 2 + 6 + 2 + static_cast<int>(featureBones_.size()) * 3);
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
    const int  root    = rootBone_;
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
    const float t1   = time + std::max(dtSample, 1e-3f);
    const auto  next = worldAt(root, t1);
    const float nX   = next.px + cyclesAt(t1) * cycleX;
    const float nZ   = next.pz + cyclesAt(t1) * cycleZ;
    const float dtt  = std::max(dtSample, 1e-3f);
    velX             = (nX - rootX) / dtt;
    velZ             = (nZ - rootZ) / dtt;

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
        float       lx, lz;
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
    if (schema_)
        for (const auto& channel : schema_->layout.channels) {
            if (channel.kind != MotionFeatureKind::Curve) continue;
            if (!schema_->curves) throw Exception("MotionDatabase: scalar curve data must be configured before bake");
            for (const auto* clip : clips_)
                if (!schema_->curveSources.contains(clip))
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
        AnimClip*   clip   = clips_[static_cast<size_t>(ci)];
        const float rate   = schema_ ? static_cast<float>(schema_->layout.sampleRate)
                                     : (clip->getSampleRate() > 0.f ? clip->getSampleRate() : 30.f);
        const float dt     = 1.f / rate;
        const float dur    = clip->getDuration();
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
        float     t          = 0.f;
        for (int sample = 0;; ++sample, t = schema_ ? sample / rate : t + dt) {
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

// MotionDatabaseLocomotion.cpp
namespace eve::animation {
eve::Result<void> MotionDatabase::setLocomotionFeatures(int leftFoot, int rightFoot, int pelvis) {
    const int count = skeleton_->getBoneCount();
    if (baked_ || leftFoot < 0 || rightFoot < 0 || pelvis < 0 || leftFoot >= count || rightFoot >= count ||
        pelvis >= count || leftFoot == rightFoot || leftFoot == pelvis || rightFoot == pelvis)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "locomotion features require three distinct valid bones before bake",
            "featureBones", {}, "animation"));
    std::vector<int> bones{leftFoot, rightFoot, pelvis};
    schema_.reset();
    featureBones_.swap(bones);
    locomotionFeatures_ = true;
    computeFeatureSize();
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

void MotionDatabase::extractLocomotionFeature(AnimClip* clip, float time, std::vector<float>& out, float& rootX,
                                              float& rootZ, float& rootYaw, float& velX, float& velZ) const {
    out.assign(detail::locomotionFeatureCount, 0.f);
    const float duration = clip->getDuration();
    const float delta    = 1.f / 60.f;
    auto        world    = [&](auto&& self, int bone, float t) -> TransformTRS {
        const auto local  = clip->sampleBone(bone, t, skeleton_->bindLocal(bone));
        const int  parent = skeleton_->getParent(bone);
        return parent < 0 ? local : detail::mulTRS(self(self, parent, t), local);
    };
    auto poseAt = [&](int bone, float t) { return world(world, bone, clampf(clip->wrapTime(t), 0.f, duration)); };
    // Accumulate complete root transforms at loop seams; turning cycles need
    // rotation composition, not repeated addition of an unrotated displacement.
    const auto  start = world(world, rootBone_, 0.f), end = world(world, rootBone_, duration);
    const float startYaw = detail::featureYaw(start);
    const float cycleYaw = std::remainder(detail::featureYaw(end) - startYaw, 6.28318530718f);
    auto        rootAt   = [&](float t) {
        auto root = poseAt(rootBone_, t);
        if (clip->getLoop() && duration > 1e-6f) {
            const int   cycles = static_cast<int>(std::floor(t / duration));
            float       x = root.px - start.px, z = root.pz - start.pz;
            const float dx = end.px - start.px, dz = end.pz - start.pz;
            const float cs = std::cos(cycleYaw), sn = std::sin(cycleYaw);
            for (int i = 0; i < std::abs(cycles); ++i) {
                if (cycles > 0) {
                    const float nextX = cs * x + sn * z + dx;
                    z                 = -sn * x + cs * z + dz;
                    x                 = nextX;
                } else {
                    x -= dx;
                    z -= dz;
                    const float nextX = cs * x - sn * z;
                    z                 = sn * x + cs * z;
                    x                 = nextX;
                }
            }
            root.px = start.px + x;
            root.pz = start.pz + z;
            root.py += cycles * (end.py - start.py);
            const float angle = detail::featureYaw(root) + cycles * cycleYaw;
            root.qx = root.qz = 0.f;
            root.qy           = std::sin(angle * .5f);
            root.qw           = std::cos(angle * .5f);
        } else if (duration > 1e-6f && (t < 0.f || t > duration)) {
            // Root trajectory extrapolation preserves velocity outside short
            // starts/stops, instead of inventing a stationary future endpoint.
            const float dt        = std::min(delta, duration);
            const auto  a         = world(world, rootBone_, t < 0.f ? 0.f : duration - dt);
            const auto  b         = world(world, rootBone_, t < 0.f ? dt : duration);
            const float extension = (t < 0.f ? t : t - duration) / dt;
            root.px += (b.px - a.px) * extension;
            root.py += (b.py - a.py) * extension;
            root.pz += (b.pz - a.pz) * extension;
            const float angle =
                detail::featureYaw(root) +
                std::remainder(detail::featureYaw(b) - detail::featureYaw(a), 6.28318530718f) * extension;
            root.qx = root.qz = 0.f;
            root.qy           = std::sin(angle * .5f);
            root.qw           = std::cos(angle * .5f);
        }
        return root;
    };
    struct Sample {
        float x, y, z, vx, vy, vz, yaw;
    };
    std::array<Sample, 5> samples{};
    const auto            current = rootAt(time);
    rootX                         = current.px;
    rootZ                         = current.pz;
    rootYaw                       = detail::featureYaw(current);
    for (int i = 0; i < 5; ++i) {
        const float t = time + detail::locomotionHorizons[i];
        const auto  r = rootAt(t), previous = rootAt(t - delta);
        samples[i] = {r.px - current.px,
                      r.py - current.py,
                      r.pz - current.pz,
                      (r.px - previous.px) / delta,
                      (r.py - previous.py) / delta,
                      (r.pz - previous.pz) / delta,
                      detail::featureYaw(r)};
    }
    velX = samples[1].vx;
    velZ = samples[1].vz;
    detail::encodeLocomotionTrajectory(std::span(out), samples, rootYaw);
    std::array<TransformTRS, 4> now{}, past{};
    now[0]  = poseAt(rootBone_, time);
    past[0] = poseAt(rootBone_, time - delta);
    for (int i = 0; i < 3; ++i) {
        now[i + 1]  = poseAt(featureBones_[i], time);
        past[i + 1] = poseAt(featureBones_[i], time - delta);
    }
    detail::encodeLocomotionPose(out, now, past, delta);
}

void MotionDatabase::normalizeLocomotionFeatures() {
    featureMean_.assign(detail::locomotionFeatureCount, 0.f);
    featureInvStd_.assign(detail::locomotionFeatureCount, 1.f);
    // Separate channels use vector mean deviation (isotropic XZ/XYZ); the two
    // feet share a centroid and scale, as the authored FeetVelZ group specifies.
    const std::array<std::array<int, 3>, 12> channels{{{0, 2, 1},
                                                       {2, 2, 1},
                                                       {4, 2, 1},
                                                       {6, 2, 1},
                                                       {8, 2, 1},
                                                       {10, 2, 1},
                                                       {12, 2, 1},
                                                       {14, 2, 1},
                                                       {16, 3, 1},
                                                       {19, 3, 1},
                                                       {22, 3, 2},
                                                       {28, 2, 1}}};
    for (const auto& channel : channels) {
        const int             base = channel[0], dimensions = channel[1], copies = channel[2];
        std::array<double, 3> mean{};
        const double          count = static_cast<double>(frames_.size()) * copies;
        for (const auto& frame : frames_)
            for (int copy = 0; copy < copies; ++copy)
                for (int axis = 0; axis < dimensions; ++axis)
                    mean[axis] += frame.feature[base + copy * dimensions + axis];
        for (double& value : mean) value /= count;
        double deviation = 0;
        for (const auto& frame : frames_)
            for (int copy = 0; copy < copies; ++copy) {
                double square = 0;
                for (int axis = 0; axis < dimensions; ++axis) {
                    const double d = frame.feature[base + copy * dimensions + axis] - mean[axis];
                    square += d * d;
                }
                deviation += std::sqrt(square);
            }
        deviation /= count;
        const bool   unitVector = base == 4 || base == 8 || base == 14 || base == 16 || base == 28;
        const double minimum    = unitVector ? 0.1 : 0.001;  // UE 0.1 cm, converted to metres.
        const float  inverse = static_cast<float>(deviation > minimum ? 1.0 / deviation : (unitVector ? 1.0 : 100.0));
        for (int copy = 0; copy < copies; ++copy)
            for (int axis = 0; axis < dimensions; ++axis) {
                const int i       = base + copy * dimensions + axis;
                featureMean_[i]   = static_cast<float>(mean[axis]);
                featureInvStd_[i] = inverse;
            }
    }
    for (auto& frame : frames_) normalizeFeature(frame.feature);
}
}  // namespace eve::animation

// MotionDatabaseSchema.cpp
namespace eve::animation {
MotionDatabase::~MotionDatabase() = default;
bool MotionDatabase::hasFeatureLayout() const { return schema_ != nullptr; }

float MotionDatabase::schemaTrajectorySpeed(std::span<const float> feature) const {
    if (!schema_ || feature.size() != static_cast<std::size_t>(schema_->dimension)) return 0.f;
    float total = 0.f;
    for (std::size_t i = 0; i < schema_->layout.channels.size(); ++i) {
        const auto& channel = schema_->layout.channels[i];
        if (channel.source != MotionFeatureSource::Trajectory || channel.kind != MotionFeatureKind::Velocity ||
            channel.normalizeVelocity)
            continue;
        const int count   = std::popcount(static_cast<unsigned>(channel.axes));
        float     squared = 0.f;
        for (int component = 0; component < count; ++component) {
            const float value = feature[schema_->offsets[i] + static_cast<std::size_t>(component)];
            squared += value * value;
        }
        total += std::sqrt(squared);
    }
    return total;
}

eve::Result<void> MotionDatabase::setFeatureLayout(const MotionFeatureLayout& layout) {
    auto fail = [](const char* message) {
        return eve::Result<void>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, message, "featureLayout", {}, "animation"));
    };
    if (baked_ || layout.sampleRate < 1 || layout.sampleRate > 240 || layout.channels.empty() ||
        layout.channels.size() > 256 || !std::isfinite(layout.normalizationLengthScale) ||
        layout.normalizationLengthScale <= 0.f || layout.normalizationLengthScale > 1000000.f)
        return fail("feature layout requires an unbaked database, 1..240 Hz and 1..256 channels");
    auto next          = std::make_unique<detail::MotionSchemaState>();
    next->layout       = layout;
    double totalWeight = 0;
    for (const auto& c : layout.channels) {
        if (c.kind > MotionFeatureKind::Curve || c.source > MotionFeatureSource::Trajectory ||
            c.query > MotionFeatureQuery::Continuing || c.axes == 0 || c.axes > 7 || c.headingAxis < 0 ||
            c.headingAxis > 2 || !std::isfinite(c.weight) || c.weight < 0.f || c.weight > 1000000.f ||
            !std::isfinite(c.sampleTime) || std::abs(c.sampleTime) > 10.f || c.normalizationGroup.size() > 4096)
            return fail("invalid feature channel operation, axes, weight, time or normalization group");
        if (c.kind == MotionFeatureKind::Curve &&
            (c.source != MotionFeatureSource::Pose || c.axes != 1 || c.curve.empty() || c.curve.size() > 4096 ||
             c.curve.find('\0') != std::string::npos))
            return fail("curve channels require a named scalar pose source");
        if (c.kind != MotionFeatureKind::Curve && !c.curve.empty())
            return fail("vector channels cannot specify a scalar curve");
        if (c.kind != MotionFeatureKind::Curve && c.source == MotionFeatureSource::Pose &&
            (c.sampleTime != 0.f || c.bone < 0 || c.origin < 0 || c.bone >= skeleton_->getBoneCount() ||
             c.origin >= skeleton_->getBoneCount()))
            return fail("pose channels require valid bones and zero sample offset");
        if (c.source == MotionFeatureSource::Trajectory &&
            (c.query != MotionFeatureQuery::Character ||
             (c.kind == MotionFeatureKind::Velocity && c.characterSpaceVelocity)))
            return fail("trajectory channels require character queries and world-space velocity");
        const int count = std::popcount(static_cast<unsigned>(c.axes));
        if (c.normalizeVelocity && c.kind != MotionFeatureKind::Velocity)
            return fail("velocity normalization requires a velocity channel");
        next->offsets.push_back(next->dimension);
        next->dimension += count;
        totalWeight += c.weight * count;
        const int group = c.source == MotionFeatureSource::Pose ? 0 : (c.kind == MotionFeatureKind::Velocity ? 1 : 2);
        next->weightSums[group] += c.weight * count;
        for (int i = 0; i < count; ++i) {
            next->weights.push_back(c.weight);
            next->sources.push_back(c.source);
            next->kinds.push_back(c.kind);
            next->queries.push_back(c.query);
        }
    }
    if (next->dimension > 1024 || totalWeight <= 0 || !std::isfinite(totalWeight))
        return fail("feature layout requires bounded dimensions and positive finite total weight");
    schema_             = std::move(next);
    locomotionFeatures_ = false;
    featureBones_.clear();
    computeFeatureSize();
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> MotionDatabase::setFeatureCurves(std::span<const std::byte>   bytes,
                                                   std::span<const std::string> sources) {
    auto fail = [](const char* message) {
        return eve::Result<void>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, message, "featureCurves", {}, "animation"));
    };
    if (!schema_ || baked_ || clips_.empty() || sources.size() != clips_.size())
        return fail("feature curves require a configured unbaked database and one source per clip");
    auto library = std::make_unique<AnimCurveLibrary>();
    auto loaded  = library->load(bytes);
    if (!loaded.ok()) return fail("invalid feature curve binary");
    std::map<const AnimClip*, std::string> names;
    for (std::size_t i = 0; i < sources.size(); ++i) {
        if (!library->contains(sources[i])) return fail("feature curve source is missing");
        auto [it, inserted] = names.emplace(clips_[i], sources[i]);
        if (!inserted && it->second != sources[i]) return fail("one clip cannot have conflicting curve sources");
    }
    schema_->curves       = std::move(library);
    schema_->curveSources = std::move(names);
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<int> MotionDatabase::setFeatureNormalizationRanges(std::span<const MotionNormalizationRange> ranges) {
    auto fail = [](const char* message) {
        return eve::Result<int>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, message,
                                                                "featureNormalizationRanges", {}, "animation"));
    };
    if (!schema_ || baked_ || clips_.empty() || ranges.empty() || ranges.size() > 100000)
        return fail(
            "normalization ranges require a configured unbaked variable layout, clips and bounded nonempty input");
    std::uint64_t sampleCount = 0;
    const float   rate        = static_cast<float>(schema_->layout.sampleRate);
    for (const auto& range : ranges) {
        if (range.clipIndex < 0 || range.clipIndex >= static_cast<int>(clips_.size()) || !std::isfinite(range.start) ||
            !std::isfinite(range.end) || range.start < 0.f || range.end < range.start ||
            range.end > clips_[range.clipIndex]->getDuration() + .001f)
            return fail("invalid normalization clip or time interval");
        const auto* clip = clips_[range.clipIndex];
        int         last = static_cast<int>(std::floor(clip->getDuration() * rate));
        if (clip->getLoop() && static_cast<float>(last) / rate >= clip->getDuration() - 1e-5f) --last;
        const int first = std::max(0, static_cast<int>(std::ceil((range.start - 1e-5f) * rate)));
        const int end   = std::min(last, static_cast<int>(std::floor((range.end + 1e-5f) * rate)));
        if (first > end) return fail("normalization interval contains no layout-rate sample");
        sampleCount += static_cast<std::uint64_t>(end - first + 1);
        if (sampleCount > 100000000) return fail("normalization sample count exceeds the configured bound");
    }
    normalizationRanges_.assign(ranges.begin(), ranges.end());
    return eve::Result<int>::success(static_cast<int>(sampleCount), eve::Status::success(eve::StatusCode::Applied));
}

namespace {
TransformTRS inverseRigid(TransformTRS value) {
    value.qx     = -value.qx;
    value.qy     = -value.qy;
    value.qz     = -value.qz;
    const auto p = detail::schemaRotate({-value.px, -value.py, -value.pz}, value);
    value.px     = p[0];
    value.py     = p[1];
    value.pz     = p[2];
    value.sx = value.sy = value.sz = 1.f;
    return value;
}
TransformTRS rigidPower(TransformTRS delta, float count) {
    if (count < 0.f) {
        delta = inverseRigid(delta);
        count = -count;
    }
    auto         whole    = static_cast<unsigned>(std::floor(count));
    const float  fraction = count - whole;
    TransformTRS result, power = delta;
    while (whole) {
        if (whole & 1) result = detail::mulTRS(result, power);
        power = detail::mulTRS(power, power);
        whole >>= 1;
    }
    TransformTRS partial;
    partial.px = delta.px * fraction;
    partial.py = delta.py * fraction;
    partial.pz = delta.pz * fraction;
    slerpQuat(0.f, 0.f, 0.f, 1.f, delta.qx, delta.qy, delta.qz, delta.qw, fraction, partial.qx, partial.qy, partial.qz,
              partial.qw);
    return detail::mulTRS(result, partial);
}
}  // namespace
struct MotionDatabase::SchemaSampler {
    const AnimSkeleton& skeleton;
    const AnimClip&     clip;
    int                 rootBone;
    TransformTRS        raw(int bone, float time) const {
        const auto local  = clip.sampleBone(bone, time, skeleton.bindLocal(bone));
        const int  parent = skeleton.getParent(bone);
        return parent < 0 ? local : detail::mulTRS(raw(parent, time), local);
    }
    TransformTRS root(float time) const {
        const float duration = clip.getDuration();
        if (duration <= 0.f) return raw(rootBone, 0.f);
        if (clip.getLoop()) {
            const float cycles = std::floor(time / duration);
            const auto  start = raw(rootBone, 0.f), end = raw(rootBone, duration);
            const auto  delta = detail::mulTRS(inverseRigid(start), end);
            const auto  local = detail::mulTRS(inverseRigid(start), raw(rootBone, clip.wrapTime(time)));
            return detail::mulTRS(detail::mulTRS(start, rigidPower(delta, cycles)), local);
        }
        if (time >= 0.f && time <= duration) return raw(rootBone, time);
        const float step  = std::min(1.f / 30.f, duration);
        const auto  a     = raw(rootBone, time < 0.f ? 0.f : duration - step);
        const auto  b     = raw(rootBone, time < 0.f ? step : duration);
        const auto  delta = detail::mulTRS(inverseRigid(a), b);
        return detail::mulTRS(time < 0.f ? a : b, rigidPower(delta, (time < 0.f ? time : time - duration) / step));
    }
    TransformTRS bone(int index, float time) const {
        const float sample = clip.getLoop() ? clip.wrapTime(time) : clampf(time, 0.f, clip.getDuration());
        return detail::mulTRS(root(time), detail::mulTRS(inverseRigid(raw(rootBone, sample)), raw(index, sample)));
    }
};

void MotionDatabase::extractSchemaFeature(AnimClip* clip, float time, std::vector<float>& out, float& rootX,
                                          float& rootZ, float& rootYaw, float& velX, float& velZ) const {
    constexpr float     dt = 1.f / 60.f;
    const SchemaSampler sampler{*skeleton_, *clip, rootBone_};
    const auto          root = sampler.root(time), previousRoot = sampler.root(time - dt);
    rootX = root.px;
    rootZ = root.pz;
    rootYaw =
        std::atan2(2.f * (root.qw * root.qy + root.qx * root.qz), 1.f - 2.f * (root.qx * root.qx + root.qy * root.qy));
    velX = (root.px - previousRoot.px) / dt;
    velZ = (root.pz - previousRoot.pz) / dt;
    out.assign(schema_->dimension, 0.f);
    for (std::size_t i = 0; i < schema_->layout.channels.size(); ++i) {
        const auto&          c = schema_->layout.channels[i];
        std::array<float, 3> value{};
        if (c.kind == MotionFeatureKind::Curve) {
            auto sampled =
                schema_->curves->sample(schema_->curveSources.at(clip), c.curve, time + c.sampleTime, clip->getLoop());
            if (!sampled.ok()) throw std::runtime_error("configured motion curve cannot be sampled");
            value[0] = sampled.value().value_or(0.f);
        } else if (c.source == MotionFeatureSource::Trajectory) {
            const auto sample = sampler.root(time + c.sampleTime);
            if (c.kind == MotionFeatureKind::Position)
                value = detail::schemaDifference(sample, root, root);
            else if (c.kind == MotionFeatureKind::Heading)
                value = detail::schemaHeading(sample, root, c.headingAxis);
            else {
                value = detail::schemaDifference(sample, sampler.root(time + c.sampleTime - dt), root);
                for (float& v : value) v /= dt;
            }
        } else {
            const auto bone = sampler.bone(c.bone, time), origin = sampler.bone(c.origin, time);
            if (c.kind == MotionFeatureKind::Position)
                value = detail::schemaDifference(bone, origin, root);
            else if (c.kind == MotionFeatureKind::Heading)
                value = detail::schemaHeading(bone, root, c.headingAxis);
            else if (c.characterSpaceVelocity) {
                value           = detail::schemaDifference(bone, origin, root);
                const auto past = detail::schemaDifference(sampler.bone(c.bone, time - dt),
                                                           sampler.bone(c.origin, time - dt), previousRoot);
                for (int axis = 0; axis < 3; ++axis) value[axis] = (value[axis] - past[axis]) / dt;
            } else {
                value = detail::schemaDifference(bone, sampler.bone(c.bone, time - dt), root);
                for (float& v : value) v /= dt;
            }
        }
        detail::schemaEncode(std::span(out).subspan(schema_->offsets[i]), c, value,
                             schema_->layout.normalizationLengthScale);
    }
}

void MotionDatabase::normalizeSchemaFeatures() {
    featureMean_.assign(schema_->dimension, 0.f);
    featureInvStd_.assign(schema_->dimension, 1.f);
    using Key = std::tuple<MotionFeatureKind, int, std::string, int>;
    std::map<Key, std::vector<int>> groups;
    for (std::size_t i = 0; i < schema_->layout.channels.size(); ++i) {
        const auto& c = schema_->layout.channels[i];
        groups[{c.kind, std::popcount(static_cast<unsigned>(c.axes)), c.normalizationGroup,
                c.normalizationGroup.empty() ? static_cast<int>(i) : -1}]
            .push_back(static_cast<int>(i));
    }
    std::vector<const Frame*> samples;
    if (normalizationRanges_.empty()) {
        samples.reserve(frames_.size());
        for (const auto& frame : frames_) samples.push_back(&frame);
    } else {
        std::vector<std::vector<const Frame*>> perClip(clips_.size());
        for (const auto& frame : frames_) perClip[frame.clipIndex].push_back(&frame);
        for (const auto& range : normalizationRanges_)
            for (const auto* frame : perClip[range.clipIndex])
                if (frame->time + 1e-5f >= range.start && frame->time - 1e-5f <= range.end) samples.push_back(frame);
    }
    if (samples.empty()) throw Exception("MotionDatabase: normalization ranges contain no baked samples");
    for (const auto& [key, channels] : groups) {
        const int             count = std::get<1>(key);
        const double          size  = static_cast<double>(samples.size()) * channels.size();
        std::array<double, 3> mean{};
        for (const auto* frame : samples)
            for (int channel : channels)
                for (int axis = 0; axis < count; ++axis) mean[axis] += frame->feature[schema_->offsets[channel] + axis];
        for (double& v : mean) v /= size;
        double deviation = 0;
        for (const auto* frame : samples)
            for (int channel : channels) {
                double square = 0;
                for (int axis = 0; axis < count; ++axis) {
                    const double delta = frame->feature[schema_->offsets[channel] + axis] - mean[axis];
                    square += delta * delta;
                }
                deviation += std::sqrt(square);
            }
        deviation /= size;
        const auto& first = schema_->layout.channels[channels[0]];
        const bool  unit  = first.kind == MotionFeatureKind::Heading || first.kind == MotionFeatureKind::Curve ||
                          first.normalizeVelocity;
        const float scale = schema_->layout.normalizationLengthScale;
        const float inverse =
            static_cast<float>(deviation > (unit ? .1 : .001 * scale) ? 1.0 / deviation : (unit ? 1.0 : 100.0 / scale));
        for (int channel : channels)
            for (int axis = 0; axis < count; ++axis) {
                featureMean_[schema_->offsets[channel] + axis]   = static_cast<float>(mean[axis]);
                featureInvStd_[schema_->offsets[channel] + axis] = inverse;
            }
    }
    for (auto& frame : frames_) normalizeFeature(frame.feature);
}
}  // namespace eve::animation
