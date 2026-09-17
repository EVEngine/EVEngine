#pragma once
#include <array>
#include <span>
#include <map>
#include <memory>
#include "animation/AnimCurveLibrary.h"
#include "animation/AnimTransformInternal.h"
#include "animation/MotionFeatureLayout.h"

namespace eve::animation { class AnimClip; }
namespace eve::animation::detail {
struct MotionSchemaState {
    MotionFeatureLayout layout;
    std::vector<int> offsets;
    std::vector<float> weights;
    std::vector<MotionFeatureSource> sources;
    std::vector<MotionFeatureKind> kinds;
    std::vector<MotionFeatureQuery> queries;
    std::array<float, 3> weightSums{};
    int dimension = 0;
    std::unique_ptr<AnimCurveLibrary> curves;
    std::map<const AnimClip*, std::string> curveSources;
};
inline std::array<float, 3> schemaRotate(const std::array<float, 3>& p, const TransformTRS& q, bool inverse = false) {
    TransformTRS r = q, v;
    r.px = r.py = r.pz = 0.f;
    r.sx = r.sy = r.sz = 1.f;
    if (inverse) { r.qx = -r.qx; r.qy = -r.qy; r.qz = -r.qz; }
    v.px = p[0]; v.py = p[1]; v.pz = p[2];
    const auto result = mulTRS(r, v);
    return {result.px, result.py, result.pz};
}
inline std::array<float, 3> schemaDifference(const TransformTRS& a, const TransformTRS& b, const TransformTRS& root) {
    return schemaRotate({a.px - b.px, a.py - b.py, a.pz - b.pz}, root, true);
}
inline void schemaEncode(std::span<float> output, const MotionFeatureChannel& channel, std::array<float, 3> value, float lengthScale) {
    if (channel.kind == MotionFeatureKind::Velocity && channel.normalizeVelocity) {
        const float divisor = std::max(.01f, std::sqrt(value[0]*value[0] + value[1]*value[1] + value[2]*value[2]));
        for (float& component : value) component /= divisor;
    } else if (channel.kind == MotionFeatureKind::Position || channel.kind == MotionFeatureKind::Velocity)
        for (float& component : value) component *= lengthScale;
    int index = 0;
    for (int axis = 0; axis < 3; ++axis) if (channel.axes & (1 << axis)) output[index++] = value[axis];
}
inline std::array<float, 3> schemaHeading(const TransformTRS& bone, const TransformTRS& root, int axis) {
    std::array<float, 3> direction{}; direction[axis] = 1.f;
    return schemaRotate(schemaRotate(direction, bone), root, true);
}
}  // namespace eve::animation::detail
