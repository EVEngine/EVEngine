#pragma once

/**
 * @file MotionSequence.h
 * @brief Lean LitMotion-style sequence: Append / Join / Insert / AppendInterval.
 */

#include "animation/MotionBuilder.h"
#include "animation/MotionRuntime.h"
#include "animation/MotionTypes.h"
#include "common/Result.h"

#include <cstdint>
#include <vector>

namespace eve::animation {

/**
 * @brief Playback handle for a sequence's child motions.
 *
 * @ownership Does not own the runtime; borrows Animation's MotionRuntime.
 * @thread Main-thread animation API only.
 */
class MotionSequenceHandle {
public:
    MotionSequenceHandle() = default;
    MotionSequenceHandle(MotionRuntime *runtime, std::vector<MotionHandle> children);

    [[nodiscard]] bool isActive() const noexcept;
    [[nodiscard]] int childCount() const noexcept { return static_cast<int>(children_.size()); }
    [[nodiscard]] MotionHandle child(int index) const;

    /** @brief Complete every still-active child (fires onComplete). */
    [[nodiscard]] eve::Result<void> complete();
    /** @brief Cancel every still-active child (fires onCancel). */
    [[nodiscard]] eve::Result<void> cancel();

private:
    MotionRuntime             *runtime_ = nullptr;
    std::vector<MotionHandle>  children_;
};

/**
 * @brief Schedule unspawned builders onto a shared MotionRuntime timeline.
 *
 * Semantics (LitMotion-aligned):
 * - Append: start at cursor; cursor advances by item span
 * - Join: start at last Append's start; cursor = max(cursor, end)
 * - Insert(t): start at absolute t; cursor = max(cursor, end)
 * - AppendInterval(dt): cursor += dt
 *
 * Item span = desc.delay + duration * loops. Infinite loops (-1) are rejected.
 *
 * @ownership Borrows MotionRuntime; does not spawn until run().
 */
class MotionSequence {
public:
    explicit MotionSequence(MotionRuntime &runtime);

    [[nodiscard]] eve::Result<void> append(MotionBuilder &&builder);
    [[nodiscard]] eve::Result<void> append(MotionVec2Builder &&builder);
    [[nodiscard]] eve::Result<void> append(MotionVec3Builder &&builder);

    [[nodiscard]] eve::Result<void> join(MotionBuilder &&builder);
    [[nodiscard]] eve::Result<void> join(MotionVec2Builder &&builder);
    [[nodiscard]] eve::Result<void> join(MotionVec3Builder &&builder);

    [[nodiscard]] eve::Result<void> insert(float atSeconds, MotionBuilder &&builder);
    [[nodiscard]] eve::Result<void> insert(float atSeconds, MotionVec2Builder &&builder);
    [[nodiscard]] eve::Result<void> insert(float atSeconds, MotionVec3Builder &&builder);

    [[nodiscard]] eve::Result<void> appendInterval(float seconds);

    [[nodiscard]] float cursor() const noexcept { return cursor_; }
    [[nodiscard]] float duration() const noexcept { return duration_; }
    [[nodiscard]] int itemCount() const noexcept { return static_cast<int>(items_.size()); }

    /** @brief Spawn every scheduled item into the runtime. */
    [[nodiscard]] eve::Result<MotionSequenceHandle> run();

private:
    enum class Kind : std::uint8_t { Float, Vec2, Vec3 };

    struct Item {
        Kind                      kind = Kind::Float;
        float                     start = 0.f;
        MotionRuntime::FloatDesc  floatDesc{};
        MotionRuntime::Vec2Desc   vec2Desc{};
        MotionRuntime::Vec3Desc   vec3Desc{};
    };

    [[nodiscard]] static eve::Result<float> spanOf(float delay, float duration, int loops);
    [[nodiscard]] eve::Result<void> schedule(float start, float span, Item item);
    [[nodiscard]] eve::Result<void> appendAtCursor(Item item, float delay, float duration, int loops);
    [[nodiscard]] eve::Result<void> joinAtLast(Item item, float delay, float duration, int loops);
    [[nodiscard]] eve::Result<void> insertAt(float at, Item item, float delay, float duration, int loops);

    MotionRuntime        &runtime_;
    std::vector<Item>     items_;
    float                 cursor_          = 0.f;
    float                 duration_        = 0.f;
    float                 lastAppendStart_ = 0.f;
    bool                  hasAppend_       = false;
};

}  // namespace eve::animation
