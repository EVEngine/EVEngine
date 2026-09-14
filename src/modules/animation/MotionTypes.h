#pragma once

/**
 * @file MotionTypes.h
 * @brief LitMotion-style typed motion handles, values, and sink interfaces.
 *
 * Cross-module write-back goes through sink interfaces (not upward includes).
 * Upper modules implement sinks and pass them into MotionBuilder::bind*.
 */

#include "common/Result.h"

#include <cstdint>

namespace eve::animation {

/** @brief Generation-checked slot identity for a live motion. */
struct MotionHandle {
    std::uint32_t index      = 0;
    std::uint32_t generation = 0;

    [[nodiscard]] constexpr bool isNull() const noexcept { return generation == 0; }

    [[nodiscard]] constexpr bool operator==(const MotionHandle &other) const noexcept {
        return index == other.index && generation == other.generation;
    }

    [[nodiscard]] constexpr bool operator!=(const MotionHandle &other) const noexcept {
        return !(*this == other);
    }
};

/** @brief Compact float2 used by motion adapters (avoids graphics math deps). */
struct MotionVec2 {
    float x = 0.f;
    float y = 0.f;
};

/** @brief Compact float3 used by motion adapters. */
struct MotionVec3 {
    float x = 0.f;
    float y = 0.f;
    float z = 0.f;
};

/** @brief Compact RGBA color used by motion adapters. */
struct MotionColor {
    float r = 0.f;
    float g = 0.f;
    float b = 0.f;
    float a = 1.f;
};

/** @brief Compact quaternion used by motion adapters (xyzw). */
struct MotionQuat {
    float x = 0.f;
    float y = 0.f;
    float z = 0.f;
    float w = 1.f;
};

/** @brief Loop behaviour after a cycle completes. */
enum class MotionLoopMode : std::uint8_t {
    Restart = 0,  ///< Jump back to start each cycle.
    Yoyo    = 1,  ///< Alternate direction each cycle.
};

/**
 * @brief Evaluation style for a motion slot.
 * @note Punch/Shake treat `to` as oscillation strength about `from` (LitMotion semantics).
 */
enum class MotionStyle : std::uint8_t {
    Tween = 0,  ///< Lerp/slerp from -> to.
    Punch = 1,  ///< Finite damped sine punch about `from`.
    Shake = 2,  ///< Punch with per-axis deterministic random sign.
};

/** @brief Push target for a float motion. Lifetime is borrowed by MotionRuntime. */
class IMotionFloatSink {
public:
    static constexpr const char *capabilityName = "IMotionFloatSink";

    virtual ~IMotionFloatSink() = default;

    /** @brief Write the latest interpolated value. */
    [[nodiscard]] virtual eve::Result<void> write(float value) = 0;
};

/** @brief Push target for a MotionVec2 motion. */
class IMotionVec2Sink {
public:
    static constexpr const char *capabilityName = "IMotionVec2Sink";

    virtual ~IMotionVec2Sink() = default;

    [[nodiscard]] virtual eve::Result<void> write(MotionVec2 value) = 0;
};

/** @brief Push target for a MotionVec3 motion. */
class IMotionVec3Sink {
public:
    static constexpr const char *capabilityName = "IMotionVec3Sink";

    virtual ~IMotionVec3Sink() = default;

    [[nodiscard]] virtual eve::Result<void> write(MotionVec3 value) = 0;
};

/** @brief Push target for a MotionColor motion. */
class IMotionColorSink {
public:
    static constexpr const char *capabilityName = "IMotionColorSink";

    virtual ~IMotionColorSink() = default;

    [[nodiscard]] virtual eve::Result<void> write(MotionColor value) = 0;
};

/** @brief Push target for a MotionQuat motion. */
class IMotionQuatSink {
public:
    static constexpr const char *capabilityName = "IMotionQuatSink";

    virtual ~IMotionQuatSink() = default;

    [[nodiscard]] virtual eve::Result<void> write(MotionQuat value) = 0;
};

/**
 * @brief Sink that writes into a borrowed float*.
 * @ownership Does not own `target`; caller must keep it alive while bound.
 */
class FloatPointerSink final : public IMotionFloatSink {
public:
    explicit FloatPointerSink(float *target) : target_(target) {}

    [[nodiscard]] eve::Result<void> write(float value) override;

private:
    float *target_ = nullptr;
};

/**
 * @brief Sink that writes into borrowed float x/y pointers.
 * @ownership Does not own the pointers; caller must keep them alive while bound.
 */
class Vec2PointerSink final : public IMotionVec2Sink {
public:
    Vec2PointerSink(float *x, float *y) : x_(x), y_(y) {}

    [[nodiscard]] eve::Result<void> write(MotionVec2 value) override;

private:
    float *x_ = nullptr;
    float *y_ = nullptr;
};

/**
 * @brief Sink that writes into borrowed float x/y/z pointers.
 * @ownership Does not own the pointers; caller must keep them alive while bound.
 */
class Vec3PointerSink final : public IMotionVec3Sink {
public:
    Vec3PointerSink(float *x, float *y, float *z) : x_(x), y_(y), z_(z) {}

    [[nodiscard]] eve::Result<void> write(MotionVec3 value) override;

private:
    float *x_ = nullptr;
    float *y_ = nullptr;
    float *z_ = nullptr;
};

/**
 * @brief Sink that writes into borrowed RGBA float pointers.
 * @ownership Does not own the pointers; caller must keep them alive while bound.
 */
class ColorPointerSink final : public IMotionColorSink {
public:
    ColorPointerSink(float *r, float *g, float *b, float *a) : r_(r), g_(g), b_(b), a_(a) {}

    [[nodiscard]] eve::Result<void> write(MotionColor value) override;

private:
    float *r_ = nullptr;
    float *g_ = nullptr;
    float *b_ = nullptr;
    float *a_ = nullptr;
};

/**
 * @brief Sink that writes into borrowed quaternion xyzw float pointers.
 * @ownership Does not own the pointers; caller must keep them alive while bound.
 */
class QuatPointerSink final : public IMotionQuatSink {
public:
    QuatPointerSink(float *x, float *y, float *z, float *w) : x_(x), y_(y), z_(z), w_(w) {}

    [[nodiscard]] eve::Result<void> write(MotionQuat value) override;

private:
    float *x_ = nullptr;
    float *y_ = nullptr;
    float *z_ = nullptr;
    float *w_ = nullptr;
};

/** @brief Evaluate an ease curve; kinds match Math.ease / Tween. */
[[nodiscard]] float evaluateMotionEase(float t, const char *kind);

/**
 * @brief LitMotion-style damped sine envelope in [0,1] progress.
 * @param t Normalized progress (typically eased). Returns 0 at t<=0 and t>=1 when frequency is integral.
 * @param frequency Oscillation count until end (>= 1).
 * @param dampingRatio 0 = no damping, 1 = fully damped (LitMotion default).
 */
[[nodiscard]] float evaluateMotionOscillation(float t, int frequency, float dampingRatio);

/**
 * @brief Deterministic shake sign in [-1,1] for axis (0..n).
 * @note Stable across SimulationStep replay for the same seed/progress/frequency.
 */
[[nodiscard]] float evaluateMotionShakeSign(std::uint32_t seed, int frequency, float t, int axis);

/** @brief Component-wise lerp for MotionColor. */
[[nodiscard]] MotionColor lerpMotionColor(MotionColor a, MotionColor b, float t);

/** @brief Spherical linear interpolation for MotionQuat (shortest path). */
[[nodiscard]] MotionQuat slerpMotionQuat(MotionQuat a, MotionQuat b, float t);

}  // namespace eve::animation
