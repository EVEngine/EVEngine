#pragma once
#include <algorithm>
#include <cmath>
#include <set>
#include "animation/AnimTransformInternal.h"
#include "animation/ControlRig.h"

namespace eve::animation::rig_detail {
template <class T>
Result<T> error(std::string message) {
    return Result<T>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, std::move(message), "controlRig"));
}
inline bool validTRS(const TransformTRS& t) {
    const float values[]{t.px, t.py, t.pz, t.qx, t.qy, t.qz, t.qw, t.sx, t.sy, t.sz};
    for (float v : values)
        if (!std::isfinite(v)) return false;
    const float length         = t.qx * t.qx + t.qy * t.qy + t.qz * t.qz + t.qw * t.qw;
    const float scaleTolerance = 1e-5f * std::max({t.sx, t.sy, t.sz});
    return std::abs(length - 1.f) < 1e-4f && t.sx > 1e-6f && t.sy > 1e-6f && t.sz > 1e-6f &&
           std::abs(t.sx - t.sy) <= scaleTolerance && std::abs(t.sx - t.sz) <= scaleTolerance;
}
inline TransformTRS relative(const TransformTRS& parent, const TransformTRS& world) {
    TransformTRS inverse;
    inverse.qx         = -parent.qx;
    inverse.qy         = -parent.qy;
    inverse.qz         = -parent.qz;
    inverse.qw         = parent.qw;
    TransformTRS delta = world;
    delta.px -= parent.px;
    delta.py -= parent.py;
    delta.pz -= parent.pz;
    auto local = detail::mulTRS(inverse, delta);
    local.px /= parent.sx;
    local.py /= parent.sy;
    local.pz /= parent.sz;
    local.sx /= parent.sx;
    local.sy /= parent.sy;
    local.sz /= parent.sz;
    return local;
}
inline int controlIndex(const RigDefinition& d, std::string_view id) {
    for (size_t i = 0; i < d.controls.size(); ++i)
        if (d.controls[i].id == id) return static_cast<int>(i);
    return -1;
}
/** @brief Resolve authoring hierarchy order.
 * @cost Allocates a control-sized result and scans dependencies; amortize at compilation or edits.
 */
inline Result<std::vector<int>> controlOrder(const RigDefinition& d) {
    std::vector<int>  order;
    std::vector<bool> visited(d.controls.size(), false);
    while (order.size() < d.controls.size()) {
        bool progress = false;
        for (size_t i = 0; i < d.controls.size(); ++i) {
            if (visited[i]) continue;
            const auto& parent = d.controls[i].parent;
            const int   p      = controlIndex(d, parent);
            if (!parent.empty() && p < 0) return error<std::vector<int>>("missing parent: " + parent);
            if (parent.empty() || visited[static_cast<size_t>(p)]) {
                visited[i] = true;
                order.push_back(static_cast<int>(i));
                progress = true;
            }
        }
        if (!progress) return error<std::vector<int>>("control hierarchy cycle");
    }
    return Result<std::vector<int>>::success(std::move(order));
}
}  // namespace eve::animation::rig_detail
