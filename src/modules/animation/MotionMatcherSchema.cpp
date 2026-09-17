#include <algorithm>
#include "animation/AnimSkeleton.h"
#include "animation/MotionDatabase.h"
#include "animation/MotionMatcher.h"
#include "animation/MotionSchemaInternal.h"
#include "common/Exception.h"

namespace eve::animation {
eve::Result<void> MotionMatcher::setFeatureQuery(const AnimPose& current, const AnimPose& previous,
                                                  float dt, std::span<const MotionFeatureTrajectorySample> samples,
                                                  std::span<const MotionFeatureCurveSample> curves) {
    auto fail = [](const char* message) {
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, message, "featureQuery", {}, "animation"));
    };
    if (!database_->schema_ || !database_->isBaked() || !std::isfinite(dt) || dt <= 0.f || dt > 1.f ||
        current.getBoneCount() != skeleton_->getBoneCount() || previous.getBoneCount() != skeleton_->getBoneCount() || samples.size() > 256 || curves.size() > 256)
        return fail("feature query requires a baked variable layout, matching poses and finite dt in (0,1]");
    for (const auto* pose : {&current, &previous}) for (int i = 0; i < pose->getBoneCount(); ++i) {
        const auto& t = pose->local(i);
        for (float v : {t.px,t.py,t.pz,t.qx,t.qy,t.qz,t.qw,t.sx,t.sy,t.sz})
            if (!std::isfinite(v)) return fail("feature query poses must be finite");
    }
    for (std::size_t i = 0; i < samples.size(); ++i) {
        const auto& s = samples[i];
        for (float v : {s.seconds,s.x,s.y,s.z,s.vx,s.vy,s.vz,s.yaw})
            if (!std::isfinite(v)) return fail("trajectory samples must be finite");
        for (std::size_t j = 0; j < i; ++j)
            if (std::abs(s.seconds-samples[j].seconds) < 1e-6f) return fail("trajectory sample times must be unique");
    }
    const auto& schema = *database_->schema_;
    for (std::size_t i = 0; i < curves.size(); ++i) {
        const auto& c = curves[i];
        if (c.curve.empty() || c.curve.size() > 4096 || !std::isfinite(c.seconds) || !std::isfinite(c.value))
            return fail("curve queries require bounded names and finite times/values");
        for (std::size_t j = 0; j < i; ++j)
            if (c.curve == curves[j].curve && std::abs(c.seconds-curves[j].seconds) < 1e-6f)
                return fail("curve query names and times must be unique");
    }
    std::vector<float> scalarValues(schema.layout.channels.size(), 0.f);
    for (std::size_t i = 0; i < schema.layout.channels.size(); ++i) {
        const auto& c = schema.layout.channels[i];
        if (c.kind != MotionFeatureKind::Curve) continue;
        auto sample = std::find_if(curves.begin(), curves.end(), [&](const auto& s) {
            return s.curve == c.curve && std::abs(s.seconds-c.sampleTime) < 1e-6f;
        });
        if (sample == curves.end()) return fail("curve query does not cover a configured channel/time");
        scalarValues[i] = sample->value;
    }
    std::vector<const MotionFeatureTrajectorySample*> selected;
    for (const auto& c : schema.layout.channels) {
        const MotionFeatureTrajectorySample* sample = nullptr;
        if (c.source == MotionFeatureSource::Trajectory) {
            for (const auto& s : samples) if (std::abs(s.seconds-c.sampleTime) < 1e-6f) { sample = &s; break; }
            if (!sample) return fail("trajectory query does not cover a configured sample time");
        }
        selected.push_back(sample);
    }
    AnimPose now, past;
    now.copyFrom(&current); past.copyFrom(&previous);
    now.computeWorld(skeleton_); past.computeWorld(skeleton_);
    const auto& root = now.world(database_->getRootBone());
    const auto& previousRoot = past.world(database_->getRootBone());
    TransformTRS character;
    character.qy = std::sin(desiredYaw_ * .5f); character.qw = std::cos(desiredYaw_ * .5f);
    std::vector<float> query(schema.dimension, 0.f);
    for (std::size_t i = 0; i < schema.layout.channels.size(); ++i) {
        const auto& c = schema.layout.channels[i];
        std::array<float, 3> value{};
        if (c.kind == MotionFeatureKind::Curve) value[0] = scalarValues[i];
        else if (const auto* sample = selected[i]) {
            if (c.kind == MotionFeatureKind::Position) value = detail::schemaRotate({sample->x,sample->y,sample->z}, character, true);
            else if (c.kind == MotionFeatureKind::Velocity) value = detail::schemaRotate({sample->vx,sample->vy,sample->vz}, character, true);
            else {
                TransformTRS rotation;
                rotation.qy = std::sin(sample->yaw * .5f); rotation.qw = std::cos(sample->yaw * .5f);
                value = detail::schemaHeading(rotation, character, c.headingAxis);
            }
        } else {
            const auto& bone = now.world(c.bone);
            if (c.kind == MotionFeatureKind::Position) value = detail::schemaDifference(bone, now.world(c.origin), root);
            else if (c.kind == MotionFeatureKind::Heading) value = detail::schemaHeading(bone, root, c.headingAxis);
            else if (c.characterSpaceVelocity) {
                value = detail::schemaDifference(bone, now.world(c.origin), root);
                const auto old = detail::schemaDifference(past.world(c.bone), past.world(c.origin), previousRoot);
                for (int axis = 0; axis < 3; ++axis) value[axis] = (value[axis] - old[axis]) / dt;
            } else {
                value = detail::schemaDifference(bone, past.world(c.bone), root);
                for (float& v : value) v /= dt;
            }
        }
        detail::schemaEncode(std::span(query).subspan(schema.offsets[i]), c, value, schema.layout.normalizationLengthScale);
    }
    for (float v : query) if (!std::isfinite(v)) return fail("encoded feature query exceeds finite range");
    const float queryTrajectorySpeed = database_->schemaTrajectorySpeed(query);
    database_->normalizeFeature(query);
    for (float v : query) if (!std::isfinite(v)) return fail("normalized feature query exceeds finite range");
    schemaQuery_.swap(query);
    queryTrajectorySpeed_ = queryTrajectorySpeed;
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

void MotionMatcher::buildSchemaQuery(std::vector<float>& query) const {
    if (schemaQuery_.empty()) throw Exception("MotionMatcher: configure a feature query before searching");
    query = schemaQuery_;
}
}  // namespace eve::animation
