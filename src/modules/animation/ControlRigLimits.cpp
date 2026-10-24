#include "animation/ControlRigInternal.h"

namespace eve::animation {
namespace {
constexpr float radians = 0.017453292519943295f;
bool            axisValid(const RigAxisLimit& a, float neutral, float lo, float hi) {
    return (a.mode == RigAxisMode::Free || a.mode == RigAxisMode::Locked || a.mode == RigAxisMode::Limited) &&
           std::isfinite(a.minimum) && std::isfinite(a.maximum) && a.minimum <= a.maximum &&
           (a.mode != RigAxisMode::Limited ||
            (a.minimum <= neutral && a.maximum >= neutral && a.minimum >= lo && a.maximum <= hi));
}
float project(float value, const RigAxisLimit& a, float neutral) {
    if (a.mode == RigAxisMode::Locked) return neutral;
    return a.mode == RigAxisMode::Limited ? std::clamp(value, a.minimum, a.maximum) : value;
}
}  // namespace
Result<TransformTRS> constrainRigControl(const RigControlLimits& l, const TransformTRS& t) {
    using namespace rig_detail;
    if (!validTRS(t) || !validTRS(l.reference)) return error<TransformTRS>("invalid constraint TRS");
    for (size_t i = 0; i < 3; ++i) {
        if (!axisValid(l.translation[i], 0, -1e20f, 1e20f) ||
            !axisValid(l.rotation[i], 0, i == 1 ? -90.f : -180.f, i == 1 ? 90.f : 180.f))
            return error<TransformTRS>("invalid axis range or mode");
        if (l.extension && l.translation[i].mode != RigAxisMode::Free)
            return error<TransformTRS>("extension and translation axis limits are mutually exclusive");
    }
    if (!axisValid(l.scale, 1, 1e-5f, 1e20f) || !std::isfinite(l.minimumLength) || !std::isfinite(l.maximumLength) ||
        l.minimumLength <= 0 || l.minimumLength > 1 || l.maximumLength < 1)
        return error<TransformTRS>("invalid scale or extension range");
    auto        result = t;
    const auto& r      = l.reference;
    if (l.extension) {
        const double length2 = double(r.px) * r.px + double(r.py) * r.py + double(r.pz) * r.pz;
        if (length2 < 1e-12) return error<TransformTRS>("extension needs nonzero reference bone length");
        const double ratio = std::clamp((double(t.px) * r.px + double(t.py) * r.py + double(t.pz) * r.pz) / length2,
                                        double(l.minimumLength), double(l.maximumLength));
        result.px          = static_cast<float>(r.px * ratio);
        result.py          = static_cast<float>(r.py * ratio);
        result.pz          = static_cast<float>(r.pz * ratio);
    } else {
        if (l.translation[0].mode != RigAxisMode::Free) result.px = r.px + project(t.px - r.px, l.translation[0], 0);
        if (l.translation[1].mode != RigAxisMode::Free) result.py = r.py + project(t.py - r.py, l.translation[1], 0);
        if (l.translation[2].mode != RigAxisMode::Free) result.pz = r.pz + project(t.pz - r.pz, l.translation[2], 0);
    }
    if (std::any_of(l.rotation.begin(), l.rotation.end(), [](const auto& a) { return a.mode != RigAxisMode::Free; })) {
        TransformTRS inverse;
        inverse.qx     = -r.qx;
        inverse.qy     = -r.qy;
        inverse.qz     = -r.qz;
        inverse.qw     = r.qw;
        const auto  q  = detail::mulTRS(inverse, t);
        const float sy = std::clamp(2 * (q.qw * q.qy - q.qz * q.qx), -1.f, 1.f);
        float       x, y = std::asin(sy), z;
        if (std::abs(sy) > 0.999999f) {
            // Canonical singular branch: Z=0, X contains the remaining rotation.
            x = 2 * std::atan2(q.qx, q.qw);
            z = 0;
        } else {
            x = std::atan2(2 * (q.qw * q.qx + q.qy * q.qz), 1 - 2 * (q.qx * q.qx + q.qy * q.qy));
            z = std::atan2(2 * (q.qw * q.qz + q.qx * q.qy), 1 - 2 * (q.qy * q.qy + q.qz * q.qz));
        }
        x = std::remainder(x / radians, 360.f);
        y /= radians;
        z /= radians;
        const float px = project(x, l.rotation[0], 0), py = project(y, l.rotation[1], 0),
                    pz = project(z, l.rotation[2], 0);
        if (std::abs(px - x) > 1e-4f || std::abs(py - y) > 1e-4f || std::abs(pz - z) > 1e-4f) {
            x              = px * radians * 0.5f;
            y              = py * radians * 0.5f;
            z              = pz * radians * 0.5f;
            const float cx = std::cos(x), sx = std::sin(x), cy = std::cos(y), sy2 = std::sin(y), cz = std::cos(z),
                        sz = std::sin(z);
            TransformTRS delta;
            delta.qx            = sx * cy * cz - cx * sy2 * sz;
            delta.qy            = cx * sy2 * cz + sx * cy * sz;
            delta.qz            = cx * cy * sz - sx * sy2 * cz;
            delta.qw            = cx * cy * cz + sx * sy2 * sz;
            const auto rotation = detail::mulTRS(r, delta);
            result.qx           = rotation.qx;
            result.qy           = rotation.qy;
            result.qz           = rotation.qz;
            result.qw           = rotation.qw;
            result.normalizeRotation();
        }
    }
    if (l.scale.mode != RigAxisMode::Free) result.sx = result.sy = result.sz = r.sx * project(t.sx / r.sx, l.scale, 1);
    if (!validTRS(result)) return error<TransformTRS>("constraint projection overflow");
    return Result<TransformTRS>::success(result);
}
}  // namespace eve::animation
