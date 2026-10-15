#pragma once

#include "physics/forces/ForceModels.h"

#include <cmath>

namespace eve::physics::force_detail {
inline bool        isFinite(double value) { return std::isfinite(value); }
inline bool        isFinite(ForceVector v) { return isFinite(v.x) && isFinite(v.y) && isFinite(v.z); }
inline bool        isNonnegative(double value) { return isFinite(value) && value >= 0; }
inline bool        isPositive(double value) { return isFinite(value) && value > 0; }
inline ForceVector scale(ForceVector v, double s) { return {v.x * s, v.y * s, v.z * s}; }
inline ForceVector add(ForceVector a, ForceVector b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline double      dot(ForceVector a, ForceVector b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline double      length(ForceVector v) { return std::hypot(v.x, v.y, v.z); }
template <class T>
Result<T> invalid() {
    return Result<T>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                "Invalid or overflowing physical model sample", "physics.forces"));
}
inline Result<ForceVector> checked(ForceVector value) {
    if (!isFinite(value)) return invalid<ForceVector>();
    return Result<ForceVector>::success(value);
}
}  // namespace eve::physics::force_detail
