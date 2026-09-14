#include "animation/MotionRuntime.h"

#include "animation/AnimationTime.h"
#include "common/Diagnostic.h"
#include "common/Exception.h"

#include <cmath>
#include <utility>

namespace eve::animation {
namespace {

[[nodiscard]] eve::Result<void> okApplied() {
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

[[nodiscard]] eve::Result<void> okNoOp() {
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::NoOp));
}

}  // namespace

eve::Result<void> MotionRuntime::validateDesc(float duration, float delay, int loops,
                                              const std::string &ease) {
    if (!(duration >= 0.f) || !std::isfinite(duration))
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "Motion duration must be finite and >= 0"));
    if (!(delay >= 0.f) || !std::isfinite(delay))
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "Motion delay must be finite and >= 0"));
    if (loops == 0 || loops < -1)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "Motion loops must be -1 or >= 1"));
    try {
        (void)evaluateMotionEase(0.5f, ease.c_str());
    } catch (const Exception &) {
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, std::string("Motion ease kind is unknown: ") + ease));
    }
    return okApplied();
}

eve::Result<void> MotionRuntime::applyFloat(FloatSlot &slot, float linearT, bool fireUpdate) {
    const float eased = evaluateMotionEase(linearT, slot.ease.c_str());
    const float a     = slot.reverse ? slot.to : slot.from;
    const float b     = slot.reverse ? slot.from : slot.to;
    slot.current      = lerpFloat(a, b, eased);
    if (slot.sink) {
        auto written = slot.sink->write(slot.current);
        if (!written) {
            if (slot.cancelOnError) {
                slot.phase = Phase::Cancelled;
                if (slot.onCancel) slot.onCancel();
                bumpGeneration(slot.generation);
            }
            return written;
        }
    }
    if (fireUpdate && slot.onUpdate) slot.onUpdate(slot.current);
    return okApplied();
}

eve::Result<void> MotionRuntime::applyVec2(Vec2Slot &slot, float linearT, bool fireUpdate) {
    const float eased = evaluateMotionEase(linearT, slot.ease.c_str());
    const MotionVec2 a = slot.reverse ? slot.to : slot.from;
    const MotionVec2 b = slot.reverse ? slot.from : slot.to;
    slot.current       = lerpVec2(a, b, eased);
    if (slot.sink) {
        auto written = slot.sink->write(slot.current);
        if (!written) {
            if (slot.cancelOnError) {
                slot.phase = Phase::Cancelled;
                if (slot.onCancel) slot.onCancel();
                bumpGeneration(slot.generation);
            }
            return written;
        }
    }
    if (fireUpdate && slot.onUpdate) slot.onUpdate(slot.current);
    return okApplied();
}

eve::Result<void> MotionRuntime::applyVec3(Vec3Slot &slot, float linearT, bool fireUpdate) {
    const float eased = evaluateMotionEase(linearT, slot.ease.c_str());
    const MotionVec3 a = slot.reverse ? slot.to : slot.from;
    const MotionVec3 b = slot.reverse ? slot.from : slot.to;
    slot.current       = lerpVec3(a, b, eased);
    if (slot.sink) {
        auto written = slot.sink->write(slot.current);
        if (!written) {
            if (slot.cancelOnError) {
                slot.phase = Phase::Cancelled;
                if (slot.onCancel) slot.onCancel();
                bumpGeneration(slot.generation);
            }
            return written;
        }
    }
    if (fireUpdate && slot.onUpdate) slot.onUpdate(slot.current);
    return okApplied();
}

eve::Result<void> MotionRuntime::finishFloat(FloatSlot &slot) {
    auto applied = applyFloat(slot, 1.f, true);
    if (!applied) return applied;
    if (slot.phase != Phase::Running) return applied;

    ++slot.played;
    if (slot.loops >= 0 && slot.played >= slot.loops) {
        slot.phase = Phase::Completed;
        if (slot.onComplete) slot.onComplete();
        bumpGeneration(slot.generation);
        return okApplied();
    }
    if (slot.loopMode == MotionLoopMode::Yoyo) slot.reverse = !slot.reverse;
    slot.elapsed = 0.f;
    return applyFloat(slot, 0.f, true);
}

eve::Result<void> MotionRuntime::finishVec2(Vec2Slot &slot) {
    auto applied = applyVec2(slot, 1.f, true);
    if (!applied) return applied;
    if (slot.phase != Phase::Running) return applied;

    ++slot.played;
    if (slot.loops >= 0 && slot.played >= slot.loops) {
        slot.phase = Phase::Completed;
        if (slot.onComplete) slot.onComplete();
        bumpGeneration(slot.generation);
        return okApplied();
    }
    if (slot.loopMode == MotionLoopMode::Yoyo) slot.reverse = !slot.reverse;
    slot.elapsed = 0.f;
    return applyVec2(slot, 0.f, true);
}

eve::Result<void> MotionRuntime::finishVec3(Vec3Slot &slot) {
    auto applied = applyVec3(slot, 1.f, true);
    if (!applied) return applied;
    if (slot.phase != Phase::Running) return applied;

    ++slot.played;
    if (slot.loops >= 0 && slot.played >= slot.loops) {
        slot.phase = Phase::Completed;
        if (slot.onComplete) slot.onComplete();
        bumpGeneration(slot.generation);
        return okApplied();
    }
    if (slot.loopMode == MotionLoopMode::Yoyo) slot.reverse = !slot.reverse;
    slot.elapsed = 0.f;
    return applyVec3(slot, 0.f, true);
}

eve::Result<void> MotionRuntime::stepFloat(FloatSlot &slot, float dt) {
    if (slot.phase != Phase::Delayed && slot.phase != Phase::Running) return okNoOp();

    if (slot.phase == Phase::Delayed) {
        slot.delayLeft -= dt;
        if (slot.delayLeft > 0.f) return okApplied();
        dt             = -slot.delayLeft;
        slot.delayLeft = 0.f;
        slot.phase     = Phase::Running;
        auto started   = applyFloat(slot, 0.f, true);
        if (!started) return started;
        if (dt <= 0.f) return started;
    }

    if (slot.duration <= 0.f) return finishFloat(slot);

    slot.elapsed += dt;
    while (slot.phase == Phase::Running && slot.elapsed >= slot.duration) {
        const float over = slot.elapsed - slot.duration;
        auto finished    = finishFloat(slot);
        if (!finished) return finished;
        if (slot.phase != Phase::Running) break;
        slot.elapsed = over;
    }
    if (slot.phase == Phase::Running) return applyFloat(slot, slot.elapsed / slot.duration, true);
    return okApplied();
}

eve::Result<void> MotionRuntime::stepVec2(Vec2Slot &slot, float dt) {
    if (slot.phase != Phase::Delayed && slot.phase != Phase::Running) return okNoOp();

    if (slot.phase == Phase::Delayed) {
        slot.delayLeft -= dt;
        if (slot.delayLeft > 0.f) return okApplied();
        dt             = -slot.delayLeft;
        slot.delayLeft = 0.f;
        slot.phase     = Phase::Running;
        auto started   = applyVec2(slot, 0.f, true);
        if (!started) return started;
        if (dt <= 0.f) return started;
    }

    if (slot.duration <= 0.f) return finishVec2(slot);

    slot.elapsed += dt;
    while (slot.phase == Phase::Running && slot.elapsed >= slot.duration) {
        const float over = slot.elapsed - slot.duration;
        auto finished    = finishVec2(slot);
        if (!finished) return finished;
        if (slot.phase != Phase::Running) break;
        slot.elapsed = over;
    }
    if (slot.phase == Phase::Running) return applyVec2(slot, slot.elapsed / slot.duration, true);
    return okApplied();
}

eve::Result<void> MotionRuntime::stepVec3(Vec3Slot &slot, float dt) {
    if (slot.phase != Phase::Delayed && slot.phase != Phase::Running) return okNoOp();

    if (slot.phase == Phase::Delayed) {
        slot.delayLeft -= dt;
        if (slot.delayLeft > 0.f) return okApplied();
        dt             = -slot.delayLeft;
        slot.delayLeft = 0.f;
        slot.phase     = Phase::Running;
        auto started   = applyVec3(slot, 0.f, true);
        if (!started) return started;
        if (dt <= 0.f) return started;
    }

    if (slot.duration <= 0.f) return finishVec3(slot);

    slot.elapsed += dt;
    while (slot.phase == Phase::Running && slot.elapsed >= slot.duration) {
        const float over = slot.elapsed - slot.duration;
        auto finished    = finishVec3(slot);
        if (!finished) return finished;
        if (slot.phase != Phase::Running) break;
        slot.elapsed = over;
    }
    if (slot.phase == Phase::Running) return applyVec3(slot, slot.elapsed / slot.duration, true);
    return okApplied();
}

const MotionRuntime::FloatSlot *MotionRuntime::resolveFloat(MotionHandle handle) const {
    if (handle.isNull() || handle.index >= floats_.size()) return nullptr;
    const auto &slot = floats_[handle.index];
    if (slot.generation != handle.generation) return nullptr;
    if (slot.phase != Phase::Delayed && slot.phase != Phase::Running) return nullptr;
    return &slot;
}

const MotionRuntime::Vec2Slot *MotionRuntime::resolveVec2(MotionHandle handle) const {
    if (handle.isNull() || handle.index >= vec2s_.size()) return nullptr;
    const auto &slot = vec2s_[handle.index];
    if (slot.generation != handle.generation) return nullptr;
    if (slot.phase != Phase::Delayed && slot.phase != Phase::Running) return nullptr;
    return &slot;
}

const MotionRuntime::Vec3Slot *MotionRuntime::resolveVec3(MotionHandle handle) const {
    if (handle.isNull() || handle.index >= vec3s_.size()) return nullptr;
    const auto &slot = vec3s_[handle.index];
    if (slot.generation != handle.generation) return nullptr;
    if (slot.phase != Phase::Delayed && slot.phase != Phase::Running) return nullptr;
    return &slot;
}

eve::Result<MotionHandle> MotionRuntime::occupyFloat(FloatSlot slot) {
    slot.phase   = slot.delayLeft > 0.f ? Phase::Delayed : Phase::Running;
    slot.elapsed = 0.f;
    slot.played  = 0;
    slot.reverse = false;
    slot.current = slot.from;

    std::uint32_t index = 0;
    bool          found = false;
    for (; index < floats_.size(); ++index) {
        if (floats_[index].phase == Phase::Inactive || floats_[index].phase == Phase::Completed ||
            floats_[index].phase == Phase::Cancelled) {
            found = true;
            break;
        }
    }
    if (!found) {
        index = static_cast<std::uint32_t>(floats_.size());
        floats_.push_back(std::move(slot));
    } else {
        const std::uint32_t gen = floats_[index].generation == 0 ? 1u : floats_[index].generation;
        floats_[index]            = std::move(slot);
        floats_[index].generation = gen;
    }

    MotionHandle handle{index, floats_[index].generation};
    if (floats_[index].phase == Phase::Running) {
        auto applied = applyFloat(floats_[index], 0.f, true);
        if (!applied) return eve::Result<MotionHandle>::failure(applied.status());
        if (floats_[index].phase != Phase::Running) return eve::Result<MotionHandle>::failure(staleDiag());
        handle.generation = floats_[index].generation;
    }
    return eve::Result<MotionHandle>::success(handle);
}

eve::Result<MotionHandle> MotionRuntime::occupyVec2(Vec2Slot slot) {
    slot.phase   = slot.delayLeft > 0.f ? Phase::Delayed : Phase::Running;
    slot.elapsed = 0.f;
    slot.played  = 0;
    slot.reverse = false;
    slot.current = slot.from;

    std::uint32_t index = 0;
    bool          found = false;
    for (; index < vec2s_.size(); ++index) {
        if (vec2s_[index].phase == Phase::Inactive || vec2s_[index].phase == Phase::Completed ||
            vec2s_[index].phase == Phase::Cancelled) {
            found = true;
            break;
        }
    }
    if (!found) {
        index = static_cast<std::uint32_t>(vec2s_.size());
        vec2s_.push_back(std::move(slot));
    } else {
        const std::uint32_t gen = vec2s_[index].generation == 0 ? 1u : vec2s_[index].generation;
        vec2s_[index]            = std::move(slot);
        vec2s_[index].generation = gen;
    }

    MotionHandle handle{index, vec2s_[index].generation};
    if (vec2s_[index].phase == Phase::Running) {
        auto applied = applyVec2(vec2s_[index], 0.f, true);
        if (!applied) return eve::Result<MotionHandle>::failure(applied.status());
        if (vec2s_[index].phase != Phase::Running) return eve::Result<MotionHandle>::failure(staleDiag());
        handle.generation = vec2s_[index].generation;
    }
    return eve::Result<MotionHandle>::success(handle);
}

eve::Result<MotionHandle> MotionRuntime::occupyVec3(Vec3Slot slot) {
    slot.phase   = slot.delayLeft > 0.f ? Phase::Delayed : Phase::Running;
    slot.elapsed = 0.f;
    slot.played  = 0;
    slot.reverse = false;
    slot.current = slot.from;

    std::uint32_t index = 0;
    bool          found = false;
    for (; index < vec3s_.size(); ++index) {
        if (vec3s_[index].phase == Phase::Inactive || vec3s_[index].phase == Phase::Completed ||
            vec3s_[index].phase == Phase::Cancelled) {
            found = true;
            break;
        }
    }
    if (!found) {
        index = static_cast<std::uint32_t>(vec3s_.size());
        vec3s_.push_back(std::move(slot));
    } else {
        const std::uint32_t gen = vec3s_[index].generation == 0 ? 1u : vec3s_[index].generation;
        vec3s_[index]            = std::move(slot);
        vec3s_[index].generation = gen;
    }

    MotionHandle handle{index, vec3s_[index].generation};
    if (vec3s_[index].phase == Phase::Running) {
        auto applied = applyVec3(vec3s_[index], 0.f, true);
        if (!applied) return eve::Result<MotionHandle>::failure(applied.status());
        if (vec3s_[index].phase != Phase::Running) return eve::Result<MotionHandle>::failure(staleDiag());
        handle.generation = vec3s_[index].generation;
    }
    return eve::Result<MotionHandle>::success(handle);
}

eve::Result<MotionHandle> MotionRuntime::spawnFloat(const FloatDesc &desc) {
    auto valid = validateDesc(desc.duration, desc.delay, desc.loops, desc.ease.empty() ? "linear" : desc.ease);
    if (!valid) return eve::Result<MotionHandle>::failure(valid.status());

    FloatSlot slot;
    slot.from          = desc.from;
    slot.to            = desc.to;
    slot.duration      = desc.duration;
    slot.delayLeft     = desc.delay;
    slot.loops         = desc.loops;
    slot.loopMode      = desc.loopMode;
    slot.ease          = desc.ease.empty() ? "linear" : desc.ease;
    slot.sink          = desc.sink;
    slot.onUpdate      = desc.onUpdate;
    slot.onComplete    = desc.onComplete;
    slot.onCancel      = desc.onCancel;
    slot.cancelOnError = desc.cancelOnError;
    return occupyFloat(std::move(slot));
}

eve::Result<MotionHandle> MotionRuntime::spawnVec2(const Vec2Desc &desc) {
    auto valid = validateDesc(desc.duration, desc.delay, desc.loops, desc.ease.empty() ? "linear" : desc.ease);
    if (!valid) return eve::Result<MotionHandle>::failure(valid.status());

    Vec2Slot slot;
    slot.from          = desc.from;
    slot.to            = desc.to;
    slot.duration      = desc.duration;
    slot.delayLeft     = desc.delay;
    slot.loops         = desc.loops;
    slot.loopMode      = desc.loopMode;
    slot.ease          = desc.ease.empty() ? "linear" : desc.ease;
    slot.sink          = desc.sink;
    slot.onUpdate      = desc.onUpdate;
    slot.onComplete    = desc.onComplete;
    slot.onCancel      = desc.onCancel;
    slot.cancelOnError = desc.cancelOnError;
    return occupyVec2(std::move(slot));
}

eve::Result<MotionHandle> MotionRuntime::spawnVec3(const Vec3Desc &desc) {
    auto valid = validateDesc(desc.duration, desc.delay, desc.loops, desc.ease.empty() ? "linear" : desc.ease);
    if (!valid) return eve::Result<MotionHandle>::failure(valid.status());

    Vec3Slot slot;
    slot.from          = desc.from;
    slot.to            = desc.to;
    slot.duration      = desc.duration;
    slot.delayLeft     = desc.delay;
    slot.loops         = desc.loops;
    slot.loopMode      = desc.loopMode;
    slot.ease          = desc.ease.empty() ? "linear" : desc.ease;
    slot.sink          = desc.sink;
    slot.onUpdate      = desc.onUpdate;
    slot.onComplete    = desc.onComplete;
    slot.onCancel      = desc.onCancel;
    slot.cancelOnError = desc.cancelOnError;
    return occupyVec3(std::move(slot));
}

bool MotionRuntime::isActive(MotionHandle handle) const noexcept {
    return resolveFloat(handle) || resolveVec2(handle) || resolveVec3(handle);
}

eve::Result<float> MotionRuntime::floatValue(MotionHandle handle) const {
    const auto *slot = resolveFloat(handle);
    if (!slot) return eve::Result<float>::failure(staleDiag());
    return eve::Result<float>::success(slot->current);
}

eve::Result<MotionVec2> MotionRuntime::vec2Value(MotionHandle handle) const {
    const auto *slot = resolveVec2(handle);
    if (!slot) return eve::Result<MotionVec2>::failure(staleDiag());
    return eve::Result<MotionVec2>::success(slot->current);
}

eve::Result<MotionVec3> MotionRuntime::vec3Value(MotionHandle handle) const {
    const auto *slot = resolveVec3(handle);
    if (!slot) return eve::Result<MotionVec3>::failure(staleDiag());
    return eve::Result<MotionVec3>::success(slot->current);
}

eve::Result<void> MotionRuntime::complete(MotionHandle handle) {
    if (auto *slot = const_cast<FloatSlot *>(resolveFloat(handle))) {
        slot->loops   = slot->played + 1;
        slot->phase   = Phase::Running;
        slot->elapsed = slot->duration;
        return finishFloat(*slot);
    }
    if (auto *slot = const_cast<Vec2Slot *>(resolveVec2(handle))) {
        slot->loops   = slot->played + 1;
        slot->phase   = Phase::Running;
        slot->elapsed = slot->duration;
        return finishVec2(*slot);
    }
    if (auto *slot = const_cast<Vec3Slot *>(resolveVec3(handle))) {
        slot->loops   = slot->played + 1;
        slot->phase   = Phase::Running;
        slot->elapsed = slot->duration;
        return finishVec3(*slot);
    }
    return eve::Result<void>::failure(staleDiag());
}

eve::Result<void> MotionRuntime::cancel(MotionHandle handle) {
    if (auto *slot = const_cast<FloatSlot *>(resolveFloat(handle))) {
        slot->phase = Phase::Cancelled;
        if (slot->onCancel) slot->onCancel();
        bumpGeneration(slot->generation);
        return okApplied();
    }
    if (auto *slot = const_cast<Vec2Slot *>(resolveVec2(handle))) {
        slot->phase = Phase::Cancelled;
        if (slot->onCancel) slot->onCancel();
        bumpGeneration(slot->generation);
        return okApplied();
    }
    if (auto *slot = const_cast<Vec3Slot *>(resolveVec3(handle))) {
        slot->phase = Phase::Cancelled;
        if (slot->onCancel) slot->onCancel();
        bumpGeneration(slot->generation);
        return okApplied();
    }
    return eve::Result<void>::failure(staleDiag());
}

int MotionRuntime::activeCount() const noexcept {
    int n = 0;
    for (const auto &s : floats_)
        if (s.phase == Phase::Delayed || s.phase == Phase::Running) ++n;
    for (const auto &s : vec2s_)
        if (s.phase == Phase::Delayed || s.phase == Phase::Running) ++n;
    for (const auto &s : vec3s_)
        if (s.phase == Phase::Delayed || s.phase == Phase::Running) ++n;
    return n;
}

eve::Result<void> MotionRuntime::advance(const eve::SimulationStep &step) {
    auto seconds = detail::secondsForStep(step, hasLastTick_, lastTick_, "MotionRuntime");
    if (!seconds) return eve::Result<void>::failure(seconds.status());
    const float dt = std::move(seconds).takeValue();

    for (auto &slot : floats_) {
        auto r = stepFloat(slot, dt);
        if (!r) return r;
    }
    for (auto &slot : vec2s_) {
        auto r = stepVec2(slot, dt);
        if (!r) return r;
    }
    for (auto &slot : vec3s_) {
        auto r = stepVec3(slot, dt);
        if (!r) return r;
    }

    lastTick_    = step.tick;
    hasLastTick_ = true;
    return okApplied();
}

}  // namespace eve::animation
