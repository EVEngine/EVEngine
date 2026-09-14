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

    // Back (overshoot)
    constexpr float backS = 1.70158f;
    if (std::strcmp(kind, "inBack") == 0) return t * t * ((backS + 1.f) * t - backS);
    if (std::strcmp(kind, "outBack") == 0) {
        const float u = t - 1.f;
        return u * u * ((backS + 1.f) * u + backS) + 1.f;
    }
    if (std::strcmp(kind, "inOutBack") == 0) {
        const float s = backS * 1.525f;
        if (t < 0.5f) {
            const float u = t * 2.f;
            return 0.5f * (u * u * ((s + 1.f) * u - s));
        }
        const float u = t * 2.f - 2.f;
        return 0.5f * (u * u * ((s + 1.f) * u + s) + 2.f);
    }

    // Elastic
    if (std::strcmp(kind, "inElastic") == 0) {
        if (t <= 0.f || t >= 1.f) return t;
        return -std::pow(2.f, 10.f * t - 10.f) *
               std::sin((t * 10.f - 10.75f) * (2.f * float(M_PI) / 3.f));
    }
    if (std::strcmp(kind, "outElastic") == 0) {
        if (t <= 0.f || t >= 1.f) return t;
        return std::pow(2.f, -10.f * t) *
                   std::sin((t * 10.f - 0.75f) * (2.f * float(M_PI) / 3.f)) +
               1.f;
    }
    if (std::strcmp(kind, "inOutElastic") == 0) {
        if (t <= 0.f || t >= 1.f) return t;
        if (t < 0.5f) {
            return -0.5f * std::pow(2.f, 20.f * t - 10.f) *
                   std::sin((20.f * t - 11.125f) * (2.f * float(M_PI) / 4.5f));
        }
        return std::pow(2.f, -20.f * t + 10.f) *
                   std::sin((20.f * t - 11.125f) * (2.f * float(M_PI) / 4.5f)) * 0.5f +
               1.f;
    }

    // Bounce
    auto outBounce = [](float x) {
        constexpr float n1 = 7.5625f;
        constexpr float d1 = 2.75f;
        if (x < 1.f / d1) return n1 * x * x;
        if (x < 2.f / d1) {
            x -= 1.5f / d1;
            return n1 * x * x + 0.75f;
        }
        if (x < 2.5f / d1) {
            x -= 2.25f / d1;
            return n1 * x * x + 0.9375f;
        }
        x -= 2.625f / d1;
        return n1 * x * x + 0.984375f;
    };
    if (std::strcmp(kind, "inBounce") == 0) return 1.f - outBounce(1.f - t);
    if (std::strcmp(kind, "outBounce") == 0) return outBounce(t);
    if (std::strcmp(kind, "inOutBounce") == 0) {
        return t < 0.5f ? (1.f - outBounce(1.f - 2.f * t)) * 0.5f
                        : (1.f + outBounce(2.f * t - 1.f)) * 0.5f;
    }

    throw Exception("Motion.ease: unknown kind '%s'", kind);
}

}  // namespace eve::animation
