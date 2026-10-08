#pragma once
#include "common/Export.h"

/**
 * @file MotionBuilder.h
 * @brief Fluent LitMotion-style builder that commits into MotionRuntime.
 */

#include "animation/MotionRuntime.h"
#include "animation/MotionTypes.h"

#include <string>
#include <utility>

namespace eve::animation {

class MotionSequence;

/**
 * @brief Configure then bind/run a float motion into a MotionRuntime.
 *
 * @note `bind(sink)` / `to(sink)` borrow the sink; the caller must keep it alive
 *       while the motion is active. Prefer a stack/test-owned sink or a
 *       module-owned one.
 */
class EVENGINE_API_WORLD MotionBuilder {
public:
    MotionBuilder(MotionRuntime &runtime, float from, float to, float duration);

    MotionBuilder &ease(std::string kind);
    MotionBuilder &delay(float seconds);
    MotionBuilder &loops(int count, MotionLoopMode mode = MotionLoopMode::Restart);
    MotionBuilder &onUpdate(MotionRuntime::FloatCallback cb);
    MotionBuilder &onComplete(MotionRuntime::VoidCallback cb);
    MotionBuilder &onCancel(MotionRuntime::VoidCallback cb);
    MotionBuilder &cancelOnError(bool enabled);

    /** @brief Select Tween / Punch / Shake evaluation. */
    MotionBuilder &style(MotionStyle style);
    /** @brief Oscillation count for Punch/Shake (default 10). */
    MotionBuilder &frequency(int count);
    /** @brief Damping ratio for Punch/Shake (0 = none, 1 = full). */
    MotionBuilder &dampingRatio(float ratio);
    /** @brief Deterministic seed for Shake signs. */
    MotionBuilder &seed(std::uint32_t value);

    /**
     * @brief Attach a sink without spawning (for MotionSequence).
     * @note Does not start playback; call run()/bind() or hand to a sequence.
     */
    MotionBuilder &to(IMotionFloatSink &sink);

    /** @brief Start without a sink; read values via MotionRuntime::floatValue. */
    [[nodiscard]] eve::Result<MotionHandle> run();

    /** @brief Start and push each sample into `sink` (borrowed). */
    [[nodiscard]] eve::Result<MotionHandle> bind(IMotionFloatSink &sink);

    [[nodiscard]] MotionRuntime &runtime() noexcept { return runtime_; }
    [[nodiscard]] const MotionRuntime &runtime() const noexcept { return runtime_; }

    /**
     * @brief Extract the configured desc for sequence scheduling (consumes builder).
     * @post Further run/bind/takeDesc fail with PreconditionViolation.
     */
    [[nodiscard]] eve::Result<MotionRuntime::FloatDesc> takeDesc();

private:
    friend class MotionSequence;

    MotionRuntime            &runtime_;
    MotionRuntime::FloatDesc  desc_{};
    bool                      consumed_ = false;
};

/** @brief Vec2 fluent builder. */
class EVENGINE_API_WORLD MotionVec2Builder {
public:
    MotionVec2Builder(MotionRuntime &runtime, MotionVec2 from, MotionVec2 to, float duration);

    MotionVec2Builder &ease(std::string kind);
    MotionVec2Builder &delay(float seconds);
    MotionVec2Builder &loops(int count, MotionLoopMode mode = MotionLoopMode::Restart);
    MotionVec2Builder &onUpdate(MotionRuntime::Vec2Callback cb);
    MotionVec2Builder &onComplete(MotionRuntime::VoidCallback cb);
    MotionVec2Builder &onCancel(MotionRuntime::VoidCallback cb);
    MotionVec2Builder &cancelOnError(bool enabled);

    /** @brief Select Tween / Punch / Shake evaluation. */
    MotionVec2Builder &style(MotionStyle style);
    /** @brief Oscillation count for Punch/Shake (default 10). */
    MotionVec2Builder &frequency(int count);
    /** @brief Damping ratio for Punch/Shake (0 = none, 1 = full). */
    MotionVec2Builder &dampingRatio(float ratio);
    /** @brief Deterministic seed for Shake signs. */
    MotionVec2Builder &seed(std::uint32_t value);
    MotionVec2Builder &to(IMotionVec2Sink &sink);

    [[nodiscard]] eve::Result<MotionHandle> run();
    [[nodiscard]] eve::Result<MotionHandle> bind(IMotionVec2Sink &sink);

    [[nodiscard]] MotionRuntime &runtime() noexcept { return runtime_; }
    [[nodiscard]] const MotionRuntime &runtime() const noexcept { return runtime_; }
    [[nodiscard]] eve::Result<MotionRuntime::Vec2Desc> takeDesc();

private:
    friend class MotionSequence;

    MotionRuntime           &runtime_;
    MotionRuntime::Vec2Desc  desc_{};
    bool                     consumed_ = false;
};

/** @brief Vec3 fluent builder. */
class MotionVec3Builder {
public:
    MotionVec3Builder(MotionRuntime &runtime, MotionVec3 from, MotionVec3 to, float duration);

    MotionVec3Builder &ease(std::string kind);
    MotionVec3Builder &delay(float seconds);
    MotionVec3Builder &loops(int count, MotionLoopMode mode = MotionLoopMode::Restart);
    MotionVec3Builder &onUpdate(MotionRuntime::Vec3Callback cb);
    MotionVec3Builder &onComplete(MotionRuntime::VoidCallback cb);
    MotionVec3Builder &onCancel(MotionRuntime::VoidCallback cb);
    MotionVec3Builder &cancelOnError(bool enabled);

    /** @brief Select Tween / Punch / Shake evaluation. */
    MotionVec3Builder &style(MotionStyle style);
    /** @brief Oscillation count for Punch/Shake (default 10). */
    MotionVec3Builder &frequency(int count);
    /** @brief Damping ratio for Punch/Shake (0 = none, 1 = full). */
    MotionVec3Builder &dampingRatio(float ratio);
    /** @brief Deterministic seed for Shake signs. */
    MotionVec3Builder &seed(std::uint32_t value);
    MotionVec3Builder &to(IMotionVec3Sink &sink);

    [[nodiscard]] eve::Result<MotionHandle> run();
    [[nodiscard]] eve::Result<MotionHandle> bind(IMotionVec3Sink &sink);

    [[nodiscard]] MotionRuntime &runtime() noexcept { return runtime_; }
    [[nodiscard]] const MotionRuntime &runtime() const noexcept { return runtime_; }
    [[nodiscard]] eve::Result<MotionRuntime::Vec3Desc> takeDesc();

private:
    friend class MotionSequence;

    MotionRuntime           &runtime_;
    MotionRuntime::Vec3Desc  desc_{};
    bool                     consumed_ = false;
};


/** @brief Color fluent builder (Tween / lerp only). */
class EVENGINE_API_WORLD MotionColorBuilder {
public:
    MotionColorBuilder(MotionRuntime &runtime, MotionColor from, MotionColor to, float duration);

    MotionColorBuilder &ease(std::string kind);
    MotionColorBuilder &delay(float seconds);
    MotionColorBuilder &loops(int count, MotionLoopMode mode = MotionLoopMode::Restart);
    MotionColorBuilder &onUpdate(MotionRuntime::ColorCallback cb);
    MotionColorBuilder &onComplete(MotionRuntime::VoidCallback cb);
    MotionColorBuilder &onCancel(MotionRuntime::VoidCallback cb);
    MotionColorBuilder &cancelOnError(bool enabled);
    MotionColorBuilder &to(IMotionColorSink &sink);

    [[nodiscard]] eve::Result<MotionHandle> run();
    [[nodiscard]] eve::Result<MotionHandle> bind(IMotionColorSink &sink);

    [[nodiscard]] MotionRuntime &runtime() noexcept { return runtime_; }
    [[nodiscard]] const MotionRuntime &runtime() const noexcept { return runtime_; }
    [[nodiscard]] eve::Result<MotionRuntime::ColorDesc> takeDesc();

private:
    MotionRuntime            &runtime_;
    MotionRuntime::ColorDesc  desc_{};
    bool                      consumed_ = false;
};

/** @brief Quaternion fluent builder (Tween / slerp only). */
class EVENGINE_API_WORLD MotionQuatBuilder {
public:
    MotionQuatBuilder(MotionRuntime &runtime, MotionQuat from, MotionQuat to, float duration);

    MotionQuatBuilder &ease(std::string kind);
    MotionQuatBuilder &delay(float seconds);
    MotionQuatBuilder &loops(int count, MotionLoopMode mode = MotionLoopMode::Restart);
    MotionQuatBuilder &onUpdate(MotionRuntime::QuatCallback cb);
    MotionQuatBuilder &onComplete(MotionRuntime::VoidCallback cb);
    MotionQuatBuilder &onCancel(MotionRuntime::VoidCallback cb);
    MotionQuatBuilder &cancelOnError(bool enabled);
    MotionQuatBuilder &to(IMotionQuatSink &sink);

    [[nodiscard]] eve::Result<MotionHandle> run();
    [[nodiscard]] eve::Result<MotionHandle> bind(IMotionQuatSink &sink);

    [[nodiscard]] MotionRuntime &runtime() noexcept { return runtime_; }
    [[nodiscard]] const MotionRuntime &runtime() const noexcept { return runtime_; }
    [[nodiscard]] eve::Result<MotionRuntime::QuatDesc> takeDesc();

private:
    MotionRuntime           &runtime_;
    MotionRuntime::QuatDesc  desc_{};
    bool                     consumed_ = false;
};

}  // namespace eve::animation
