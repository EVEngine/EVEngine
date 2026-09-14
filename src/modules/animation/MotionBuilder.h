#pragma once

/**
 * @file MotionBuilder.h
 * @brief Fluent LitMotion-style builder that commits into MotionRuntime.
 */

#include "animation/MotionRuntime.h"
#include "animation/MotionTypes.h"

#include <string>

namespace eve::animation {

/**
 * @brief Configure then bind/run a float motion into a MotionRuntime.
 *
 * @note `bind(sink)` borrows the sink; the caller must keep it alive while the
 *       motion is active. Prefer a stack/test-owned sink or a module-owned one.
 */
class MotionBuilder {
public:
    MotionBuilder(MotionRuntime &runtime, float from, float to, float duration);

    MotionBuilder &ease(std::string kind);
    MotionBuilder &delay(float seconds);
    MotionBuilder &loops(int count, MotionLoopMode mode = MotionLoopMode::Restart);
    MotionBuilder &onUpdate(MotionRuntime::FloatCallback cb);
    MotionBuilder &onComplete(MotionRuntime::VoidCallback cb);
    MotionBuilder &onCancel(MotionRuntime::VoidCallback cb);
    MotionBuilder &cancelOnError(bool enabled);

    /** @brief Start without a sink; read values via MotionRuntime::floatValue. */
    [[nodiscard]] eve::Result<MotionHandle> run();

    /** @brief Start and push each sample into `sink` (borrowed). */
    [[nodiscard]] eve::Result<MotionHandle> bind(IMotionFloatSink &sink);

private:
    MotionRuntime           &runtime_;
    MotionRuntime::FloatDesc desc_{};
};

/** @brief Vec2 fluent builder. */
class MotionVec2Builder {
public:
    MotionVec2Builder(MotionRuntime &runtime, MotionVec2 from, MotionVec2 to, float duration);

    MotionVec2Builder &ease(std::string kind);
    MotionVec2Builder &delay(float seconds);
    MotionVec2Builder &loops(int count, MotionLoopMode mode = MotionLoopMode::Restart);
    MotionVec2Builder &onUpdate(MotionRuntime::Vec2Callback cb);
    MotionVec2Builder &onComplete(MotionRuntime::VoidCallback cb);
    MotionVec2Builder &onCancel(MotionRuntime::VoidCallback cb);
    MotionVec2Builder &cancelOnError(bool enabled);

    [[nodiscard]] eve::Result<MotionHandle> run();
    [[nodiscard]] eve::Result<MotionHandle> bind(IMotionVec2Sink &sink);

private:
    MotionRuntime          &runtime_;
    MotionRuntime::Vec2Desc desc_{};
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

    [[nodiscard]] eve::Result<MotionHandle> run();
    [[nodiscard]] eve::Result<MotionHandle> bind(IMotionVec3Sink &sink);

private:
    MotionRuntime          &runtime_;
    MotionRuntime::Vec3Desc desc_{};
};

}  // namespace eve::animation
