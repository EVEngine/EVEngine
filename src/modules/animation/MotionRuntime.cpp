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

eve::Result<void> MotionRuntime::validateStyle(MotionStyle style, int frequency, float dampingRatio) {
    if (style == MotionStyle::Tween) return okApplied();
    if (frequency < 1)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "Motion frequency must be >= 1 for Punch/Shake"));
    if (!(dampingRatio >= 0.f) || !std::isfinite(dampingRatio))
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "Motion dampingRatio must be finite and >= 0"));
    return okApplied();
}


void MotionRuntime::recycleFloat(FloatSlot &slot) {
    slot.sink       = nullptr;
    slot.onUpdate   = {};
    slot.onComplete = {};
    slot.onCancel   = {};
    const auto index = static_cast<std::uint32_t>(&slot - floats_.data());
    freeFloats_.push_back(index);
}

void MotionRuntime::recycleVec2(Vec2Slot &slot) {
    slot.sink       = nullptr;
    slot.onUpdate   = {};
    slot.onComplete = {};
    slot.onCancel   = {};
    const auto index = static_cast<std::uint32_t>(&slot - vec2s_.data());
    freeVec2s_.push_back(index);
}

void MotionRuntime::recycleVec3(Vec3Slot &slot) {
    slot.sink       = nullptr;
    slot.onUpdate   = {};
    slot.onComplete = {};
    slot.onCancel   = {};
    const auto index = static_cast<std::uint32_t>(&slot - vec3s_.data());
    freeVec3s_.push_back(index);
}

void MotionRuntime::recycleColor(ColorSlot &slot) {
    slot.sink       = nullptr;
    slot.onUpdate   = {};
    slot.onComplete = {};
    slot.onCancel   = {};
    const auto index = static_cast<std::uint32_t>(&slot - colors_.data());
    freeColors_.push_back(index);
}

void MotionRuntime::recycleQuat(QuatSlot &slot) {
    slot.sink       = nullptr;
    slot.onUpdate   = {};
    slot.onComplete = {};
    slot.onCancel   = {};
    const auto index = static_cast<std::uint32_t>(&slot - quats_.data());
    freeQuats_.push_back(index);
}

void MotionRuntime::ensureFloatCapacity(std::size_t count) {
    floats_.reserve(count);
    freeFloats_.reserve(count);
    while (floats_.size() < count) {
        FloatSlot slot;
        slot.phase      = Phase::Inactive;
        slot.generation = 1;
        floats_.push_back(std::move(slot));
        freeFloats_.push_back(static_cast<std::uint32_t>(floats_.size() - 1));
    }
}

void MotionRuntime::ensureVec2Capacity(std::size_t count) {
    vec2s_.reserve(count);
    freeVec2s_.reserve(count);
    while (vec2s_.size() < count) {
        Vec2Slot slot;
        slot.phase      = Phase::Inactive;
        slot.generation = 1;
        vec2s_.push_back(std::move(slot));
        freeVec2s_.push_back(static_cast<std::uint32_t>(vec2s_.size() - 1));
    }
}

void MotionRuntime::ensureVec3Capacity(std::size_t count) {
    vec3s_.reserve(count);
    freeVec3s_.reserve(count);
    while (vec3s_.size() < count) {
        Vec3Slot slot;
        slot.phase      = Phase::Inactive;
        slot.generation = 1;
        vec3s_.push_back(std::move(slot));
        freeVec3s_.push_back(static_cast<std::uint32_t>(vec3s_.size() - 1));
    }
}

void MotionRuntime::ensureColorCapacity(std::size_t count) {
    colors_.reserve(count);
    freeColors_.reserve(count);
    while (colors_.size() < count) {
        ColorSlot slot;
        slot.phase      = Phase::Inactive;
        slot.generation = 1;
        colors_.push_back(std::move(slot));
        freeColors_.push_back(static_cast<std::uint32_t>(colors_.size() - 1));
    }
}

void MotionRuntime::ensureQuatCapacity(std::size_t count) {
    quats_.reserve(count);
    freeQuats_.reserve(count);
    while (quats_.size() < count) {
        QuatSlot slot;
        slot.phase      = Phase::Inactive;
        slot.generation = 1;
        quats_.push_back(std::move(slot));
        freeQuats_.push_back(static_cast<std::uint32_t>(quats_.size() - 1));
    }
}



eve::Result<void> MotionRuntime::applyFloat(FloatSlot &slot, float linearT, bool fireUpdate) {
    const float eased = evaluateMotionEase(linearT, slot.ease.c_str());
    if (slot.style == MotionStyle::Tween) {
        const float a = slot.reverse ? slot.to : slot.from;
        const float b = slot.reverse ? slot.from : slot.to;
        slot.current  = lerpFloat(a, b, eased);
    } else {
        const float strength = slot.reverse ? -slot.to : slot.to;
        float wave = evaluateMotionOscillation(eased, slot.frequency, slot.dampingRatio);
        if (slot.style == MotionStyle::Shake)
            wave *= evaluateMotionShakeSign(slot.seed, slot.frequency, eased, 0);
        slot.current = slot.from + strength * wave;
    }
    if (slot.sink) {
        auto written = slot.sink->write(slot.current);
        if (!written) {
            if (slot.cancelOnError) {
                slot.phase = Phase::Cancelled;
                if (slot.onCancel) slot.onCancel();
                bumpGeneration(slot.generation);
                recycleFloat(slot);
            }
            return written;
        }
    }
    if (fireUpdate && slot.onUpdate) slot.onUpdate(slot.current);
    return okApplied();
}


eve::Result<void> MotionRuntime::applyVec2(Vec2Slot &slot, float linearT, bool fireUpdate) {
    const float eased = evaluateMotionEase(linearT, slot.ease.c_str());
    if (slot.style == MotionStyle::Tween) {
        const MotionVec2 a = slot.reverse ? slot.to : slot.from;
        const MotionVec2 b = slot.reverse ? slot.from : slot.to;
        slot.current       = lerpVec2(a, b, eased);
    } else {
        const float sx = slot.reverse ? -slot.to.x : slot.to.x;
        const float sy = slot.reverse ? -slot.to.y : slot.to.y;
        float wave = evaluateMotionOscillation(eased, slot.frequency, slot.dampingRatio);
        float wx = wave;
        float wy = wave;
        if (slot.style == MotionStyle::Shake) {
            wx *= evaluateMotionShakeSign(slot.seed, slot.frequency, eased, 0);
            wy *= evaluateMotionShakeSign(slot.seed, slot.frequency, eased, 1);
        }
        slot.current = MotionVec2{slot.from.x + sx * wx, slot.from.y + sy * wy};
    }
    if (slot.sink) {
        auto written = slot.sink->write(slot.current);
        if (!written) {
            if (slot.cancelOnError) {
                slot.phase = Phase::Cancelled;
                if (slot.onCancel) slot.onCancel();
                bumpGeneration(slot.generation);
                recycleVec2(slot);
            }
            return written;
        }
    }
    if (fireUpdate && slot.onUpdate) slot.onUpdate(slot.current);
    return okApplied();
}


eve::Result<void> MotionRuntime::applyVec3(Vec3Slot &slot, float linearT, bool fireUpdate) {
    const float eased = evaluateMotionEase(linearT, slot.ease.c_str());
    if (slot.style == MotionStyle::Tween) {
        const MotionVec3 a = slot.reverse ? slot.to : slot.from;
        const MotionVec3 b = slot.reverse ? slot.from : slot.to;
        slot.current       = lerpVec3(a, b, eased);
    } else {
        const float sx = slot.reverse ? -slot.to.x : slot.to.x;
        const float sy = slot.reverse ? -slot.to.y : slot.to.y;
        const float sz = slot.reverse ? -slot.to.z : slot.to.z;
        float wave = evaluateMotionOscillation(eased, slot.frequency, slot.dampingRatio);
        float wx = wave, wy = wave, wz = wave;
        if (slot.style == MotionStyle::Shake) {
            wx *= evaluateMotionShakeSign(slot.seed, slot.frequency, eased, 0);
            wy *= evaluateMotionShakeSign(slot.seed, slot.frequency, eased, 1);
            wz *= evaluateMotionShakeSign(slot.seed, slot.frequency, eased, 2);
        }
        slot.current = MotionVec3{slot.from.x + sx * wx, slot.from.y + sy * wy, slot.from.z + sz * wz};
    }
    if (slot.sink) {
        auto written = slot.sink->write(slot.current);
        if (!written) {
            if (slot.cancelOnError) {
                slot.phase = Phase::Cancelled;
                if (slot.onCancel) slot.onCancel();
                bumpGeneration(slot.generation);
                recycleVec3(slot);
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
        recycleFloat(slot);
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
        recycleVec2(slot);
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
        recycleVec3(slot);
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
    if (!freeFloats_.empty()) {
        index = freeFloats_.back();
        freeFloats_.pop_back();
        const std::uint32_t gen = floats_[index].generation == 0 ? 1u : floats_[index].generation;
        floats_[index]            = std::move(slot);
        floats_[index].generation = gen;
    } else {
        index = static_cast<std::uint32_t>(floats_.size());
        floats_.push_back(std::move(slot));
    }

    MotionHandle handle{index, floats_[index].generation};
    if (floats_[index].phase == Phase::Running) {
        auto applied = applyFloat(floats_[index], 0.f, true);
        if (!applied) return eve::Result<MotionHandle>::failure(applied.status());
        if (floats_[index].phase != Phase::Running)
            return eve::Result<MotionHandle>::failure(staleDiag());
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
    if (!freeVec2s_.empty()) {
        index = freeVec2s_.back();
        freeVec2s_.pop_back();
        const std::uint32_t gen = vec2s_[index].generation == 0 ? 1u : vec2s_[index].generation;
        vec2s_[index]            = std::move(slot);
        vec2s_[index].generation = gen;
    } else {
        index = static_cast<std::uint32_t>(vec2s_.size());
        vec2s_.push_back(std::move(slot));
    }

    MotionHandle handle{index, vec2s_[index].generation};
    if (vec2s_[index].phase == Phase::Running) {
        auto applied = applyVec2(vec2s_[index], 0.f, true);
        if (!applied) return eve::Result<MotionHandle>::failure(applied.status());
        if (vec2s_[index].phase != Phase::Running)
            return eve::Result<MotionHandle>::failure(staleDiag());
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
    if (!freeVec3s_.empty()) {
        index = freeVec3s_.back();
        freeVec3s_.pop_back();
        const std::uint32_t gen = vec3s_[index].generation == 0 ? 1u : vec3s_[index].generation;
        vec3s_[index]            = std::move(slot);
        vec3s_[index].generation = gen;
    } else {
        index = static_cast<std::uint32_t>(vec3s_.size());
        vec3s_.push_back(std::move(slot));
    }

    MotionHandle handle{index, vec3s_[index].generation};
    if (vec3s_[index].phase == Phase::Running) {
        auto applied = applyVec3(vec3s_[index], 0.f, true);
        if (!applied) return eve::Result<MotionHandle>::failure(applied.status());
        if (vec3s_[index].phase != Phase::Running)
            return eve::Result<MotionHandle>::failure(staleDiag());
        handle.generation = vec3s_[index].generation;
    }
    return eve::Result<MotionHandle>::success(handle);
}


eve::Result<MotionHandle> MotionRuntime::spawnFloat(const FloatDesc &desc) {
    auto valid = validateDesc(desc.duration, desc.delay, desc.loops, desc.ease.empty() ? "linear" : desc.ease);
    if (!valid) return eve::Result<MotionHandle>::failure(valid.status());
    auto styleOk = validateStyle(desc.style, desc.frequency, desc.dampingRatio);
    if (!styleOk) return eve::Result<MotionHandle>::failure(styleOk.status());

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
    slot.style         = desc.style;
    slot.frequency     = desc.frequency;
    slot.dampingRatio  = desc.dampingRatio;
    slot.seed          = desc.seed;
    return occupyFloat(std::move(slot));
}

eve::Result<MotionHandle> MotionRuntime::spawnVec2(const Vec2Desc &desc) {
    auto valid = validateDesc(desc.duration, desc.delay, desc.loops, desc.ease.empty() ? "linear" : desc.ease);
    if (!valid) return eve::Result<MotionHandle>::failure(valid.status());
    auto styleOk = validateStyle(desc.style, desc.frequency, desc.dampingRatio);
    if (!styleOk) return eve::Result<MotionHandle>::failure(styleOk.status());

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
    slot.style         = desc.style;
    slot.frequency     = desc.frequency;
    slot.dampingRatio  = desc.dampingRatio;
    slot.seed          = desc.seed;
    return occupyVec2(std::move(slot));
}

eve::Result<MotionHandle> MotionRuntime::spawnVec3(const Vec3Desc &desc) {
    auto valid = validateDesc(desc.duration, desc.delay, desc.loops, desc.ease.empty() ? "linear" : desc.ease);
    if (!valid) return eve::Result<MotionHandle>::failure(valid.status());
    auto styleOk = validateStyle(desc.style, desc.frequency, desc.dampingRatio);
    if (!styleOk) return eve::Result<MotionHandle>::failure(styleOk.status());

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
    slot.style         = desc.style;
    slot.frequency     = desc.frequency;
    slot.dampingRatio  = desc.dampingRatio;
    slot.seed          = desc.seed;
    return occupyVec3(std::move(slot));
}

bool MotionRuntime::isActive(MotionHandle handle) const noexcept {
    return resolveFloat(handle) || resolveVec2(handle) || resolveVec3(handle) ||
           resolveColor(handle) || resolveQuat(handle);
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
    if (auto *slot = const_cast<ColorSlot *>(resolveColor(handle))) {
        slot->loops   = slot->played + 1;
        slot->phase   = Phase::Running;
        slot->elapsed = slot->duration;
        return finishColor(*slot);
    }
    if (auto *slot = const_cast<QuatSlot *>(resolveQuat(handle))) {
        slot->loops   = slot->played + 1;
        slot->phase   = Phase::Running;
        slot->elapsed = slot->duration;
        return finishQuat(*slot);
    }
    return eve::Result<void>::failure(staleDiag());
}

eve::Result<void> MotionRuntime::cancel(MotionHandle handle) {
    if (auto *slot = const_cast<FloatSlot *>(resolveFloat(handle))) {
        slot->phase = Phase::Cancelled;
        if (slot->onCancel) slot->onCancel();
        bumpGeneration(slot->generation);
        recycleFloat(*slot);
        return okApplied();
    }
    if (auto *slot = const_cast<Vec2Slot *>(resolveVec2(handle))) {
        slot->phase = Phase::Cancelled;
        if (slot->onCancel) slot->onCancel();
        bumpGeneration(slot->generation);
        recycleVec2(*slot);
        return okApplied();
    }
    if (auto *slot = const_cast<Vec3Slot *>(resolveVec3(handle))) {
        slot->phase = Phase::Cancelled;
        if (slot->onCancel) slot->onCancel();
        bumpGeneration(slot->generation);
        recycleVec3(*slot);
        return okApplied();
    }
    if (auto *slot = const_cast<ColorSlot *>(resolveColor(handle))) {
        slot->phase = Phase::Cancelled;
        if (slot->onCancel) slot->onCancel();
        bumpGeneration(slot->generation);
        recycleColor(*slot);
        return okApplied();
    }
    if (auto *slot = const_cast<QuatSlot *>(resolveQuat(handle))) {
        slot->phase = Phase::Cancelled;
        if (slot->onCancel) slot->onCancel();
        bumpGeneration(slot->generation);
        recycleQuat(*slot);
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
    for (const auto &s : colors_)
        if (s.phase == Phase::Delayed || s.phase == Phase::Running) ++n;
    for (const auto &s : quats_)
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
    for (auto &slot : colors_) {
        auto r = stepColor(slot, dt);
        if (!r) return r;
    }
    for (auto &slot : quats_) {
        auto r = stepQuat(slot, dt);
        if (!r) return r;
    }

    lastTick_    = step.tick;
    hasLastTick_ = true;
    return okApplied();
}


eve::Result<void> MotionRuntime::applyColor(ColorSlot &slot, float linearT, bool fireUpdate) {
    const float eased = evaluateMotionEase(linearT, slot.ease.c_str());
    const MotionColor a = slot.reverse ? slot.to : slot.from;
    const MotionColor b = slot.reverse ? slot.from : slot.to;
    slot.current        = lerpMotionColor(a, b, eased);
    if (slot.sink) {
        auto written = slot.sink->write(slot.current);
        if (!written) {
            if (slot.cancelOnError) {
                slot.phase = Phase::Cancelled;
                if (slot.onCancel) slot.onCancel();
                bumpGeneration(slot.generation);
                recycleColor(slot);
            }
            return written;
        }
    }
    if (fireUpdate && slot.onUpdate) slot.onUpdate(slot.current);
    return okApplied();
}

eve::Result<void> MotionRuntime::applyQuat(QuatSlot &slot, float linearT, bool fireUpdate) {
    const float eased = evaluateMotionEase(linearT, slot.ease.c_str());
    const MotionQuat a = slot.reverse ? slot.to : slot.from;
    const MotionQuat b = slot.reverse ? slot.from : slot.to;
    slot.current       = slerpMotionQuat(a, b, eased);
    if (slot.sink) {
        auto written = slot.sink->write(slot.current);
        if (!written) {
            if (slot.cancelOnError) {
                slot.phase = Phase::Cancelled;
                if (slot.onCancel) slot.onCancel();
                bumpGeneration(slot.generation);
                recycleQuat(slot);
            }
            return written;
        }
    }
    if (fireUpdate && slot.onUpdate) slot.onUpdate(slot.current);
    return okApplied();
}

eve::Result<void> MotionRuntime::finishColor(ColorSlot &slot) {
    auto applied = applyColor(slot, 1.f, true);
    if (!applied) return applied;
    if (slot.phase != Phase::Running) return applied;

    ++slot.played;
    if (slot.loops >= 0 && slot.played >= slot.loops) {
        slot.phase = Phase::Completed;
        if (slot.onComplete) slot.onComplete();
        bumpGeneration(slot.generation);
        recycleColor(slot);
        return okApplied();
    }
    if (slot.loopMode == MotionLoopMode::Yoyo) slot.reverse = !slot.reverse;
    slot.elapsed = 0.f;
    return applyColor(slot, 0.f, true);
}

eve::Result<void> MotionRuntime::finishQuat(QuatSlot &slot) {
    auto applied = applyQuat(slot, 1.f, true);
    if (!applied) return applied;
    if (slot.phase != Phase::Running) return applied;

    ++slot.played;
    if (slot.loops >= 0 && slot.played >= slot.loops) {
        slot.phase = Phase::Completed;
        if (slot.onComplete) slot.onComplete();
        bumpGeneration(slot.generation);
        recycleQuat(slot);
        return okApplied();
    }
    if (slot.loopMode == MotionLoopMode::Yoyo) slot.reverse = !slot.reverse;
    slot.elapsed = 0.f;
    return applyQuat(slot, 0.f, true);
}

eve::Result<void> MotionRuntime::stepColor(ColorSlot &slot, float dt) {
    if (slot.phase != Phase::Delayed && slot.phase != Phase::Running) return okNoOp();

    if (slot.phase == Phase::Delayed) {
        slot.delayLeft -= dt;
        if (slot.delayLeft > 0.f) return okApplied();
        dt             = -slot.delayLeft;
        slot.delayLeft = 0.f;
        slot.phase     = Phase::Running;
        auto started   = applyColor(slot, 0.f, true);
        if (!started) return started;
        if (dt <= 0.f) return started;
    }

    if (slot.duration <= 0.f) return finishColor(slot);

    slot.elapsed += dt;
    while (slot.phase == Phase::Running && slot.elapsed >= slot.duration) {
        const float over = slot.elapsed - slot.duration;
        auto finished    = finishColor(slot);
        if (!finished) return finished;
        if (slot.phase != Phase::Running) break;
        slot.elapsed = over;
    }
    if (slot.phase == Phase::Running) return applyColor(slot, slot.elapsed / slot.duration, true);
    return okApplied();
}

eve::Result<void> MotionRuntime::stepQuat(QuatSlot &slot, float dt) {
    if (slot.phase != Phase::Delayed && slot.phase != Phase::Running) return okNoOp();

    if (slot.phase == Phase::Delayed) {
        slot.delayLeft -= dt;
        if (slot.delayLeft > 0.f) return okApplied();
        dt             = -slot.delayLeft;
        slot.delayLeft = 0.f;
        slot.phase     = Phase::Running;
        auto started   = applyQuat(slot, 0.f, true);
        if (!started) return started;
        if (dt <= 0.f) return started;
    }

    if (slot.duration <= 0.f) return finishQuat(slot);

    slot.elapsed += dt;
    while (slot.phase == Phase::Running && slot.elapsed >= slot.duration) {
        const float over = slot.elapsed - slot.duration;
        auto finished    = finishQuat(slot);
        if (!finished) return finished;
        if (slot.phase != Phase::Running) break;
        slot.elapsed = over;
    }
    if (slot.phase == Phase::Running) return applyQuat(slot, slot.elapsed / slot.duration, true);
    return okApplied();
}

const MotionRuntime::ColorSlot *MotionRuntime::resolveColor(MotionHandle handle) const {
    if (handle.isNull() || handle.index >= colors_.size()) return nullptr;
    const auto &slot = colors_[handle.index];
    if (slot.generation != handle.generation) return nullptr;
    if (slot.phase != Phase::Delayed && slot.phase != Phase::Running) return nullptr;
    return &slot;
}

const MotionRuntime::QuatSlot *MotionRuntime::resolveQuat(MotionHandle handle) const {
    if (handle.isNull() || handle.index >= quats_.size()) return nullptr;
    const auto &slot = quats_[handle.index];
    if (slot.generation != handle.generation) return nullptr;
    if (slot.phase != Phase::Delayed && slot.phase != Phase::Running) return nullptr;
    return &slot;
}

eve::Result<MotionHandle> MotionRuntime::occupyColor(ColorSlot slot) {
    slot.phase   = slot.delayLeft > 0.f ? Phase::Delayed : Phase::Running;
    slot.elapsed = 0.f;
    slot.played  = 0;
    slot.reverse = false;
    slot.current = slot.from;

    std::uint32_t index = 0;
    if (!freeColors_.empty()) {
        index = freeColors_.back();
        freeColors_.pop_back();
        const std::uint32_t gen = colors_[index].generation == 0 ? 1u : colors_[index].generation;
        colors_[index]            = std::move(slot);
        colors_[index].generation = gen;
    } else {
        index = static_cast<std::uint32_t>(colors_.size());
        colors_.push_back(std::move(slot));
    }

    MotionHandle handle{index, colors_[index].generation};
    if (colors_[index].phase == Phase::Running) {
        auto applied = applyColor(colors_[index], 0.f, true);
        if (!applied) return eve::Result<MotionHandle>::failure(applied.status());
        if (colors_[index].phase != Phase::Running)
            return eve::Result<MotionHandle>::failure(staleDiag());
        handle.generation = colors_[index].generation;
    }
    return eve::Result<MotionHandle>::success(handle);
}


eve::Result<MotionHandle> MotionRuntime::occupyQuat(QuatSlot slot) {
    slot.phase   = slot.delayLeft > 0.f ? Phase::Delayed : Phase::Running;
    slot.elapsed = 0.f;
    slot.played  = 0;
    slot.reverse = false;
    slot.current = slot.from;

    std::uint32_t index = 0;
    if (!freeQuats_.empty()) {
        index = freeQuats_.back();
        freeQuats_.pop_back();
        const std::uint32_t gen = quats_[index].generation == 0 ? 1u : quats_[index].generation;
        quats_[index]            = std::move(slot);
        quats_[index].generation = gen;
    } else {
        index = static_cast<std::uint32_t>(quats_.size());
        quats_.push_back(std::move(slot));
    }

    MotionHandle handle{index, quats_[index].generation};
    if (quats_[index].phase == Phase::Running) {
        auto applied = applyQuat(quats_[index], 0.f, true);
        if (!applied) return eve::Result<MotionHandle>::failure(applied.status());
        if (quats_[index].phase != Phase::Running)
            return eve::Result<MotionHandle>::failure(staleDiag());
        handle.generation = quats_[index].generation;
    }
    return eve::Result<MotionHandle>::success(handle);
}


eve::Result<MotionHandle> MotionRuntime::spawnColor(const ColorDesc &desc) {
    auto valid = validateDesc(desc.duration, desc.delay, desc.loops, desc.ease.empty() ? "linear" : desc.ease);
    if (!valid) return eve::Result<MotionHandle>::failure(valid.status());

    ColorSlot slot;
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
    return occupyColor(std::move(slot));
}

eve::Result<MotionHandle> MotionRuntime::spawnQuat(const QuatDesc &desc) {
    auto valid = validateDesc(desc.duration, desc.delay, desc.loops, desc.ease.empty() ? "linear" : desc.ease);
    if (!valid) return eve::Result<MotionHandle>::failure(valid.status());

    QuatSlot slot;
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
    return occupyQuat(std::move(slot));
}

eve::Result<MotionColor> MotionRuntime::colorValue(MotionHandle handle) const {
    const auto *slot = resolveColor(handle);
    if (!slot) return eve::Result<MotionColor>::failure(staleDiag());
    return eve::Result<MotionColor>::success(slot->current);
}

eve::Result<MotionQuat> MotionRuntime::quatValue(MotionHandle handle) const {
    const auto *slot = resolveQuat(handle);
    if (!slot) return eve::Result<MotionQuat>::failure(staleDiag());
    return eve::Result<MotionQuat>::success(slot->current);
}

}  // namespace eve::animation
