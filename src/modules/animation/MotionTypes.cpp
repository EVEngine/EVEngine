#include "animation/MotionTypes.h"

#include "common/Diagnostic.h"
#include "common/Exception.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace eve::animation {

namespace {

float clamp01(float t) { return std::clamp(t, 0.f, 1.f); }

}  // namespace

eve::Result<void> FloatPointerSink::write(float value) {
    if (!target_) {
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::PreconditionViolation, "FloatPointerSink.write: target is null"));
    }
    *target_ = value;
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> Vec2PointerSink::write(MotionVec2 value) {
    if (!x_ || !y_) {
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::PreconditionViolation, "Vec2PointerSink.write: target is null"));
    }
    *x_ = value.x;
    *y_ = value.y;
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> Vec3PointerSink::write(MotionVec3 value) {
    if (!x_ || !y_ || !z_) {
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::PreconditionViolation, "Vec3PointerSink.write: target is null"));
    }
    *x_ = value.x;
    *y_ = value.y;
    *z_ = value.z;
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

float evaluateMotionEase(float t, const char *kind) {
    t = clamp01(t);
    if (!kind || kind[0] == '\0' || std::strcmp(kind, "linear") == 0) return t;
    if (std::strcmp(kind, "inQuad") == 0) return t * t;
    if (std::strcmp(kind, "outQuad") == 0) return 1.f - (1.f - t) * (1.f - t);
    if (std::strcmp(kind, "inOutQuad") == 0)
        return t < 0.5f ? 2.f * t * t : 1.f - std::pow(-2.f * t + 2.f, 2.f) * 0.5f;
    if (std::strcmp(kind, "inCubic") == 0) return t * t * t;
    if (std::strcmp(kind, "outCubic") == 0) return 1.f - std::pow(1.f - t, 3.f);
    if (std::strcmp(kind, "inOutCubic") == 0)
        return t < 0.5f ? 4.f * t * t * t : 1.f - std::pow(-2.f * t + 2.f, 3.f) * 0.5f;
    if (std::strcmp(kind, "inSine") == 0) return 1.f - std::cos(t * float(M_PI) * 0.5f);
    if (std::strcmp(kind, "outSine") == 0) return std::sin(t * float(M_PI) * 0.5f);
    if (std::strcmp(kind, "inOutSine") == 0) return -(std::cos(float(M_PI) * t) - 1.f) * 0.5f;
    if (std::strcmp(kind, "inExpo") == 0) return t <= 0.f ? 0.f : std::pow(2.f, 10.f * t - 10.f);
    if (std::strcmp(kind, "outExpo") == 0) return t >= 1.f ? 1.f : 1.f - std::pow(2.f, -10.f * t);
    if (std::strcmp(kind, "inOutExpo") == 0) {
        if (t <= 0.f) return 0.f;
        if (t >= 1.f) return 1.f;
        return t < 0.5f ? std::pow(2.f, 20.f * t - 10.f) * 0.5f
                        : (2.f - std::pow(2.f, -20.f * t + 10.f)) * 0.5f;
    }
    throw Exception("Motion.ease: unknown kind '%s'", kind);
}

}  // namespace eve::animation
