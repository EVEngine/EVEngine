#include "animation/MotionTypes.h"

#include "common/Diagnostic.h"
#include "common/Exception.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
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

eve::Result<void> FloatBufferSink::write(float value) {
    if (!buffer_) {
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::PreconditionViolation, "FloatBufferSink.write: buffer is null"));
    }
    buffer_[index_] = value;
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


eve::Result<void> ColorPointerSink::write(MotionColor value) {
    if (!r_ || !g_ || !b_ || !a_) {
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::PreconditionViolation, "ColorPointerSink.write: target is null"));
    }
    *r_ = value.r;
    *g_ = value.g;
    *b_ = value.b;
    *a_ = value.a;
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> QuatPointerSink::write(MotionQuat value) {
    if (!x_ || !y_ || !z_ || !w_) {
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::PreconditionViolation, "QuatPointerSink.write: target is null"));
    }
    *x_ = value.x;
    *y_ = value.y;
    *z_ = value.z;
    *w_ = value.w;
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

float evaluateMotionOscillation(float t, int frequency, float dampingRatio) {
    if (!(t > 0.f) || !(t < 1.f) || frequency < 1) return 0.f;
    if (!std::isfinite(t) || !std::isfinite(dampingRatio)) return 0.f;
    const float freq = static_cast<float>(frequency);
    const float angular = freq * float(M_PI);
    const float dampingFactor = dampingRatio * freq / (2.f * float(M_PI));
    return std::sin(angular * t) * std::exp(-dampingFactor * t);
}

float evaluateMotionShakeSign(std::uint32_t seed, int frequency, float t, int axis) {
    if (frequency < 1) frequency = 1;
    const float clamped = std::clamp(t, 0.f, 1.f);
    const int step = static_cast<int>(clamped * static_cast<float>(frequency) * 2.f);
    std::uint32_t h = seed;
    h ^= 0x9E3779B9u * (static_cast<std::uint32_t>(axis) + 1u);
    h ^= 0x85EBCA6Bu * (static_cast<std::uint32_t>(step) + 1u);
    h ^= static_cast<std::uint32_t>(frequency) * 0xC2B2AE35u;
    h ^= h >> 16;
    h *= 0x7FEB352Du;
    h ^= h >> 15;
    h *= 0x846CA68Bu;
    h ^= h >> 16;
    // Map to [-1, 1]
    const float u = static_cast<float>(h & 0x00FFFFFFu) / static_cast<float>(0x00FFFFFFu);
    return u * 2.f - 1.f;
}

MotionColor lerpMotionColor(MotionColor a, MotionColor b, float t) {
    return MotionColor{a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t,
                       a.a + (b.a - a.a) * t};
}

MotionQuat slerpMotionQuat(MotionQuat a, MotionQuat b, float t) {
    auto normalize = [](MotionQuat q) {
        const float len = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
        if (!(len > 0.f)) return MotionQuat{0.f, 0.f, 0.f, 1.f};
        const float inv = 1.f / len;
        return MotionQuat{q.x * inv, q.y * inv, q.z * inv, q.w * inv};
    };
    a = normalize(a);
    b = normalize(b);
    float dot = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    if (dot < 0.f) {
        b = MotionQuat{-b.x, -b.y, -b.z, -b.w};
        dot = -dot;
    }
    constexpr float kEps = 0.9995f;
    if (dot > kEps) {
        return normalize(MotionQuat{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t,
                                    a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t});
    }
    const float theta0 = std::acos(std::clamp(dot, -1.f, 1.f));
    const float theta = theta0 * t;
    const float sinTheta0 = std::sin(theta0);
    const float s0 = std::sin(theta0 - theta) / sinTheta0;
    const float s1 = std::sin(theta) / sinTheta0;
    return MotionQuat{a.x * s0 + b.x * s1, a.y * s0 + b.y * s1, a.z * s0 + b.z * s1,
                      a.w * s0 + b.w * s1};
}

}  // namespace eve::animation
