#include <bit>
#include <cmath>
#include <cstdint>
#include <map>
#include <tuple>
#include "animation/AnimClip.h"
#include "animation/AnimSkeleton.h"
#include "animation/MotionDatabase.h"
#include "animation/MotionSchemaInternal.h"
#include "common/Exception.h"

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
        const int count = std::popcount(static_cast<unsigned>(channel.axes));
        float squared = 0.f;
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
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, message, "featureLayout", {}, "animation"));
    };
    if (baked_ || layout.sampleRate < 1 || layout.sampleRate > 240 || layout.channels.empty() || layout.channels.size() > 256 ||
        !std::isfinite(layout.normalizationLengthScale) || layout.normalizationLengthScale <= 0.f || layout.normalizationLengthScale > 1000000.f)
        return fail("feature layout requires an unbaked database, 1..240 Hz and 1..256 channels");
    auto next = std::make_unique<detail::MotionSchemaState>();
    next->layout = layout;
    double totalWeight = 0;
    for (const auto& c : layout.channels) {
        if (c.kind > MotionFeatureKind::Curve || c.source > MotionFeatureSource::Trajectory ||
            c.query > MotionFeatureQuery::Continuing || c.axes == 0 || c.axes > 7 ||
            c.headingAxis < 0 || c.headingAxis > 2 || !std::isfinite(c.weight) || c.weight < 0.f ||
            c.weight > 1000000.f || !std::isfinite(c.sampleTime) || std::abs(c.sampleTime) > 10.f ||
            c.normalizationGroup.size() > 4096)
            return fail("invalid feature channel operation, axes, weight, time or normalization group");
        if (c.kind == MotionFeatureKind::Curve && (c.source != MotionFeatureSource::Pose || c.axes != 1 ||
                c.curve.empty() || c.curve.size() > 4096 || c.curve.find('\0') != std::string::npos))
            return fail("curve channels require a named scalar pose source");
        if (c.kind != MotionFeatureKind::Curve && !c.curve.empty())
            return fail("vector channels cannot specify a scalar curve");
        if (c.kind != MotionFeatureKind::Curve && c.source == MotionFeatureSource::Pose && (c.sampleTime != 0.f || c.bone < 0 || c.origin < 0 ||
                c.bone >= skeleton_->getBoneCount() || c.origin >= skeleton_->getBoneCount()))
            return fail("pose channels require valid bones and zero sample offset");
        if (c.source == MotionFeatureSource::Trajectory && (c.query != MotionFeatureQuery::Character ||
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
            next->weights.push_back(c.weight); next->sources.push_back(c.source);
            next->kinds.push_back(c.kind); next->queries.push_back(c.query);
        }
    }
    if (next->dimension > 1024 || totalWeight <= 0 || !std::isfinite(totalWeight))
        return fail("feature layout requires bounded dimensions and positive finite total weight");
    schema_ = std::move(next);
    locomotionFeatures_ = false;
    featureBones_.clear();
    computeFeatureSize();
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> MotionDatabase::setFeatureCurves(std::span<const std::byte> bytes,
                                                   std::span<const std::string> sources) {
    auto fail = [](const char* message) {
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
            message, "featureCurves", {}, "animation"));
    };
    if (!schema_ || baked_ || clips_.empty() || sources.size() != clips_.size())
        return fail("feature curves require a configured unbaked database and one source per clip");
    auto library = std::make_unique<AnimCurveLibrary>();
    auto loaded = library->load(bytes);
    if (!loaded.ok()) return fail("invalid feature curve binary");
    std::map<const AnimClip*, std::string> names;
    for (std::size_t i = 0; i < sources.size(); ++i) {
        if (!library->contains(sources[i])) return fail("feature curve source is missing");
        auto [it, inserted] = names.emplace(clips_[i], sources[i]);
        if (!inserted && it->second != sources[i]) return fail("one clip cannot have conflicting curve sources");
    }
    schema_->curves = std::move(library);
    schema_->curveSources = std::move(names);
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<int> MotionDatabase::setFeatureNormalizationRanges(std::span<const MotionNormalizationRange> ranges) {
    auto fail = [](const char* message) {
        return eve::Result<int>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, message, "featureNormalizationRanges", {}, "animation"));
    };
    if (!schema_ || baked_ || clips_.empty() || ranges.empty() || ranges.size() > 100000)
        return fail("normalization ranges require a configured unbaked variable layout, clips and bounded nonempty input");
    std::uint64_t sampleCount = 0;
    const float rate = static_cast<float>(schema_->layout.sampleRate);
    for (const auto& range : ranges) {
        if (range.clipIndex < 0 || range.clipIndex >= static_cast<int>(clips_.size()) ||
            !std::isfinite(range.start) || !std::isfinite(range.end) || range.start < 0.f ||
            range.end < range.start || range.end > clips_[range.clipIndex]->getDuration() + .001f)
            return fail("invalid normalization clip or time interval");
        const auto* clip = clips_[range.clipIndex];
        int last = static_cast<int>(std::floor(clip->getDuration() * rate));
        if (clip->getLoop() && static_cast<float>(last) / rate >= clip->getDuration() - 1e-5f) --last;
        const int first = std::max(0, static_cast<int>(std::ceil((range.start - 1e-5f) * rate)));
        const int end = std::min(last, static_cast<int>(std::floor((range.end + 1e-5f) * rate)));
        if (first > end) return fail("normalization interval contains no layout-rate sample");
        sampleCount += static_cast<std::uint64_t>(end - first + 1);
        if (sampleCount > 100000000) return fail("normalization sample count exceeds the configured bound");
    }
    normalizationRanges_.assign(ranges.begin(), ranges.end());
    return eve::Result<int>::success(static_cast<int>(sampleCount),
                                     eve::Status::success(eve::StatusCode::Applied));
}

namespace {
TransformTRS inverseRigid(TransformTRS value) {
    value.qx = -value.qx; value.qy = -value.qy; value.qz = -value.qz;
    const auto p = detail::schemaRotate({-value.px, -value.py, -value.pz}, value);
    value.px = p[0]; value.py = p[1]; value.pz = p[2];
    value.sx = value.sy = value.sz = 1.f;
    return value;
}
TransformTRS rigidPower(TransformTRS delta, float count) {
    if (count < 0.f) { delta = inverseRigid(delta); count = -count; }
    auto whole = static_cast<unsigned>(std::floor(count));
    const float fraction = count - whole;
    TransformTRS result, power = delta;
    while (whole) {
        if (whole & 1) result = detail::mulTRS(result, power);
        power = detail::mulTRS(power, power); whole >>= 1;
    }
    TransformTRS partial;
    partial.px = delta.px * fraction; partial.py = delta.py * fraction; partial.pz = delta.pz * fraction;
    slerpQuat(0.f, 0.f, 0.f, 1.f, delta.qx, delta.qy, delta.qz, delta.qw, fraction,
              partial.qx, partial.qy, partial.qz, partial.qw);
    return detail::mulTRS(result, partial);
}
}  // namespace
struct MotionDatabase::SchemaSampler {
    const AnimSkeleton& skeleton;
    const AnimClip& clip;
    int rootBone;
    TransformTRS raw(int bone, float time) const {
        const auto local = clip.sampleBone(bone, time, skeleton.bindLocal(bone));
        const int parent = skeleton.getParent(bone);
        return parent < 0 ? local : detail::mulTRS(raw(parent, time), local);
    }
    TransformTRS root(float time) const {
        const float duration = clip.getDuration();
        if (duration <= 0.f) return raw(rootBone, 0.f);
        if (clip.getLoop()) {
            const float cycles = std::floor(time / duration);
            const auto start = raw(rootBone, 0.f), end = raw(rootBone, duration);
            const auto delta = detail::mulTRS(inverseRigid(start), end);
            const auto local = detail::mulTRS(inverseRigid(start), raw(rootBone, clip.wrapTime(time)));
            return detail::mulTRS(detail::mulTRS(start, rigidPower(delta, cycles)), local);
        }
        if (time >= 0.f && time <= duration) return raw(rootBone, time);
        const float step = std::min(1.f / 30.f, duration);
        const auto a = raw(rootBone, time < 0.f ? 0.f : duration - step);
        const auto b = raw(rootBone, time < 0.f ? step : duration);
        const auto delta = detail::mulTRS(inverseRigid(a), b);
        return detail::mulTRS(time < 0.f ? a : b, rigidPower(delta, (time < 0.f ? time : time - duration) / step));
    }
    TransformTRS bone(int index, float time) const {
        const float sample = clip.getLoop() ? clip.wrapTime(time) : clampf(time, 0.f, clip.getDuration());
        return detail::mulTRS(root(time), detail::mulTRS(inverseRigid(raw(rootBone, sample)), raw(index, sample)));
    }
};

void MotionDatabase::extractSchemaFeature(AnimClip* clip, float time, std::vector<float>& out, float& rootX,
                                           float& rootZ, float& rootYaw, float& velX, float& velZ) const {
    constexpr float dt = 1.f / 60.f;
    const SchemaSampler sampler{*skeleton_, *clip, rootBone_};
    const auto root = sampler.root(time), previousRoot = sampler.root(time - dt);
    rootX = root.px; rootZ = root.pz;
    rootYaw = std::atan2(2.f * (root.qw * root.qy + root.qx * root.qz), 1.f - 2.f * (root.qx * root.qx + root.qy * root.qy));
    velX = (root.px - previousRoot.px) / dt; velZ = (root.pz - previousRoot.pz) / dt;
    out.assign(schema_->dimension, 0.f);
    for (std::size_t i = 0; i < schema_->layout.channels.size(); ++i) {
        const auto& c = schema_->layout.channels[i];
        std::array<float, 3> value{};
        if (c.kind == MotionFeatureKind::Curve) {
            auto sampled = schema_->curves->sample(schema_->curveSources.at(clip), c.curve,
                                                   time + c.sampleTime, clip->getLoop());
            if (!sampled.ok()) throw std::runtime_error("configured motion curve cannot be sampled");
            value[0] = sampled.value().value_or(0.f);
        } else if (c.source == MotionFeatureSource::Trajectory) {
            const auto sample = sampler.root(time + c.sampleTime);
            if (c.kind == MotionFeatureKind::Position) value = detail::schemaDifference(sample, root, root);
            else if (c.kind == MotionFeatureKind::Heading) value = detail::schemaHeading(sample, root, c.headingAxis);
            else {
                value = detail::schemaDifference(sample, sampler.root(time + c.sampleTime - dt), root);
                for (float& v : value) v /= dt;
            }
        } else {
            const auto bone = sampler.bone(c.bone, time), origin = sampler.bone(c.origin, time);
            if (c.kind == MotionFeatureKind::Position) value = detail::schemaDifference(bone, origin, root);
            else if (c.kind == MotionFeatureKind::Heading) value = detail::schemaHeading(bone, root, c.headingAxis);
            else if (c.characterSpaceVelocity) {
                value = detail::schemaDifference(bone, origin, root);
                const auto past = detail::schemaDifference(sampler.bone(c.bone, time - dt), sampler.bone(c.origin, time - dt), previousRoot);
                for (int axis = 0; axis < 3; ++axis) value[axis] = (value[axis] - past[axis]) / dt;
            } else {
                value = detail::schemaDifference(bone, sampler.bone(c.bone, time - dt), root);
                for (float& v : value) v /= dt;
            }
        }
        detail::schemaEncode(std::span(out).subspan(schema_->offsets[i]), c, value, schema_->layout.normalizationLengthScale);
    }
}

void MotionDatabase::normalizeSchemaFeatures() {
    featureMean_.assign(schema_->dimension, 0.f); featureInvStd_.assign(schema_->dimension, 1.f);
    using Key = std::tuple<MotionFeatureKind, int, std::string, int>;
    std::map<Key, std::vector<int>> groups;
    for (std::size_t i = 0; i < schema_->layout.channels.size(); ++i) {
        const auto& c = schema_->layout.channels[i];
        groups[{c.kind, std::popcount(static_cast<unsigned>(c.axes)), c.normalizationGroup,
                c.normalizationGroup.empty() ? static_cast<int>(i) : -1}].push_back(static_cast<int>(i));
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
                if (frame->time + 1e-5f >= range.start && frame->time - 1e-5f <= range.end)
                    samples.push_back(frame);
    }
    if (samples.empty()) throw Exception("MotionDatabase: normalization ranges contain no baked samples");
    for (const auto& [key, channels] : groups) {
        const int count = std::get<1>(key);
        const double size = static_cast<double>(samples.size()) * channels.size();
        std::array<double, 3> mean{};
        for (const auto* frame : samples) for (int channel : channels)
            for (int axis = 0; axis < count; ++axis) mean[axis] += frame->feature[schema_->offsets[channel] + axis];
        for (double& v : mean) v /= size;
        double deviation = 0;
        for (const auto* frame : samples) for (int channel : channels) {
            double square = 0;
            for (int axis = 0; axis < count; ++axis) {
                const double delta = frame->feature[schema_->offsets[channel] + axis] - mean[axis]; square += delta * delta;
            }
            deviation += std::sqrt(square);
        }
        deviation /= size;
        const auto& first = schema_->layout.channels[channels[0]];
        const bool unit = first.kind == MotionFeatureKind::Heading || first.kind == MotionFeatureKind::Curve || first.normalizeVelocity;
        const float scale = schema_->layout.normalizationLengthScale;
        const float inverse = static_cast<float>(deviation > (unit ? .1 : .001 * scale) ? 1.0 / deviation : (unit ? 1.0 : 100.0 / scale));
        for (int channel : channels) for (int axis = 0; axis < count; ++axis) {
            featureMean_[schema_->offsets[channel] + axis] = static_cast<float>(mean[axis]);
            featureInvStd_[schema_->offsets[channel] + axis] = inverse;
        }
    }
    for (auto& frame : frames_) normalizeFeature(frame.feature);
}
}  // namespace eve::animation
