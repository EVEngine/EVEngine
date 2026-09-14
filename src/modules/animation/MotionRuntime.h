#pragma once

/**
 * @file MotionRuntime.h
 * @brief Dense motion storage advanced by Animation::advance(SimulationStep).
 */

#include "animation/MotionTypes.h"
#include "common/Time.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace eve::animation {

/**
 * @brief Owns typed motion slots and applies SimulationStep updates.
 *
 * @ownership Animation owns the runtime. Sinks and callbacks are borrowed.
 * @thread Main-thread animation pump only; not synchronized.
 * @reentrancy Callbacks must not spawn/cancel motions on this same runtime
 *             during advance (Phase 1 has no deferred command queue).
 */
class MotionRuntime {
public:
    using FloatCallback = std::function<void(float)>;
    using Vec2Callback  = std::function<void(MotionVec2)>;
    using Vec3Callback  = std::function<void(MotionVec3)>;
    using VoidCallback  = std::function<void()>;

    struct FloatDesc {
        float             from          = 0.f;
        float             to            = 0.f;
        float             duration      = 0.f;
        float             delay         = 0.f;
        int               loops         = 1;  ///< 1 = once, N = N cycles, -1 = infinite.
        MotionLoopMode    loopMode      = MotionLoopMode::Restart;
        std::string       ease          = "linear";
        IMotionFloatSink *sink          = nullptr;
        FloatCallback     onUpdate;
        VoidCallback      onComplete;
        VoidCallback      onCancel;
        bool              cancelOnError = true;
    };

    struct Vec2Desc {
        MotionVec2       from{};
        MotionVec2       to{};
        float            duration      = 0.f;
        float            delay         = 0.f;
        int              loops         = 1;
        MotionLoopMode   loopMode      = MotionLoopMode::Restart;
        std::string      ease          = "linear";
        IMotionVec2Sink *sink          = nullptr;
        Vec2Callback     onUpdate;
        VoidCallback     onComplete;
        VoidCallback     onCancel;
        bool             cancelOnError = true;
    };

    struct Vec3Desc {
        MotionVec3       from{};
        MotionVec3       to{};
        float            duration      = 0.f;
        float            delay         = 0.f;
        int              loops         = 1;
        MotionLoopMode   loopMode      = MotionLoopMode::Restart;
        std::string      ease          = "linear";
        IMotionVec3Sink *sink          = nullptr;
        Vec3Callback     onUpdate;
        VoidCallback     onComplete;
        VoidCallback     onCancel;
        bool             cancelOnError = true;
    };

    MotionRuntime()  = default;
    ~MotionRuntime() = default;

    MotionRuntime(const MotionRuntime &)            = delete;
    MotionRuntime &operator=(const MotionRuntime &) = delete;

    void ensureFloatCapacity(std::size_t count) { floats_.reserve(count); }
    void ensureVec2Capacity(std::size_t count) { vec2s_.reserve(count); }
    void ensureVec3Capacity(std::size_t count) { vec3s_.reserve(count); }

    [[nodiscard]] eve::Result<MotionHandle> spawnFloat(const FloatDesc &desc);
    [[nodiscard]] eve::Result<MotionHandle> spawnVec2(const Vec2Desc &desc);
    [[nodiscard]] eve::Result<MotionHandle> spawnVec3(const Vec3Desc &desc);

    [[nodiscard]] bool isActive(MotionHandle handle) const noexcept;
    [[nodiscard]] eve::Result<float> floatValue(MotionHandle handle) const;
    [[nodiscard]] eve::Result<MotionVec2> vec2Value(MotionHandle handle) const;
    [[nodiscard]] eve::Result<MotionVec3> vec3Value(MotionHandle handle) const;

    [[nodiscard]] eve::Result<void> complete(MotionHandle handle);
    [[nodiscard]] eve::Result<void> cancel(MotionHandle handle);

    [[nodiscard]] int activeCount() const noexcept;
    [[nodiscard]] int floatSlotCount() const noexcept { return static_cast<int>(floats_.size()); }

    [[nodiscard]] eve::Result<void> advance(const eve::SimulationStep &step);

    [[nodiscard]] bool hasCurrentTick() const noexcept { return hasLastTick_; }
    [[nodiscard]] eve::SimulationTick currentTick() const noexcept { return lastTick_; }

private:
    enum class Phase : std::uint8_t { Inactive, Delayed, Running, Completed, Cancelled };

    template <class Value, class Sink>
    struct Slot {
        std::uint32_t              generation    = 1;
        Phase                      phase         = Phase::Inactive;
        Value                      from{};
        Value                      to{};
        Value                      current{};
        float                      duration      = 0.f;
        float                      delayLeft     = 0.f;
        float                      elapsed       = 0.f;
        int                        loops         = 1;
        int                        played        = 0;
        MotionLoopMode             loopMode      = MotionLoopMode::Restart;
        bool                       reverse       = false;
        bool                       cancelOnError = true;
        std::string                ease          = "linear";
        Sink                      *sink          = nullptr;
        std::function<void(Value)> onUpdate;
        VoidCallback               onComplete;
        VoidCallback               onCancel;
    };

    using FloatSlot = Slot<float, IMotionFloatSink>;
    using Vec2Slot  = Slot<MotionVec2, IMotionVec2Sink>;
    using Vec3Slot  = Slot<MotionVec3, IMotionVec3Sink>;

    static float lerpFloat(float a, float b, float t) { return a + (b - a) * t; }
    static MotionVec2 lerpVec2(MotionVec2 a, MotionVec2 b, float t) {
        return MotionVec2{lerpFloat(a.x, b.x, t), lerpFloat(a.y, b.y, t)};
    }
    static MotionVec3 lerpVec3(MotionVec3 a, MotionVec3 b, float t) {
        return MotionVec3{lerpFloat(a.x, b.x, t), lerpFloat(a.y, b.y, t), lerpFloat(a.z, b.z, t)};
    }

    static void bumpGeneration(std::uint32_t &generation) {
        generation = generation == UINT32_MAX ? 1u : generation + 1u;
    }

    [[nodiscard]] static eve::Diagnostic staleDiag() {
        return eve::Diagnostic::error(eve::DiagnosticCode::StaleHandle, "MotionHandle is inactive or stale");
    }

    [[nodiscard]] eve::Result<void> applyFloat(FloatSlot &slot, float linearT, bool fireUpdate);
    [[nodiscard]] eve::Result<void> applyVec2(Vec2Slot &slot, float linearT, bool fireUpdate);
    [[nodiscard]] eve::Result<void> applyVec3(Vec3Slot &slot, float linearT, bool fireUpdate);

    [[nodiscard]] eve::Result<void> finishFloat(FloatSlot &slot);
    [[nodiscard]] eve::Result<void> finishVec2(Vec2Slot &slot);
    [[nodiscard]] eve::Result<void> finishVec3(Vec3Slot &slot);

    [[nodiscard]] eve::Result<void> stepFloat(FloatSlot &slot, float dt);
    [[nodiscard]] eve::Result<void> stepVec2(Vec2Slot &slot, float dt);
    [[nodiscard]] eve::Result<void> stepVec3(Vec3Slot &slot, float dt);

    [[nodiscard]] const FloatSlot *resolveFloat(MotionHandle handle) const;
    [[nodiscard]] const Vec2Slot *resolveVec2(MotionHandle handle) const;
    [[nodiscard]] const Vec3Slot *resolveVec3(MotionHandle handle) const;

    [[nodiscard]] eve::Result<MotionHandle> occupyFloat(FloatSlot slot);
    [[nodiscard]] eve::Result<MotionHandle> occupyVec2(Vec2Slot slot);
    [[nodiscard]] eve::Result<MotionHandle> occupyVec3(Vec3Slot slot);

    [[nodiscard]] static eve::Result<void> validateDesc(float duration, float delay, int loops,
                                                        const std::string &ease);

    std::vector<FloatSlot> floats_;
    std::vector<Vec2Slot>  vec2s_;
    std::vector<Vec3Slot>  vec3s_;
    eve::SimulationTick    lastTick_    = eve::SimulationTick::zero();
    bool                   hasLastTick_ = false;
};

}  // namespace eve::animation
