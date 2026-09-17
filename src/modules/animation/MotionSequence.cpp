#include "animation/MotionSequence.h"

#include "common/Diagnostic.h"
#include "common/Status.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace eve::animation {
namespace {

[[nodiscard]] eve::Result<void> okApplied() {
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

[[nodiscard]] eve::Diagnostic badArg(const char *message) {
    return eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, message);
}

[[nodiscard]] eve::Diagnostic precondition(const char *message) {
    return eve::Diagnostic::error(eve::DiagnosticCode::PreconditionViolation, message);
}

}  // namespace

MotionSequenceHandle::MotionSequenceHandle(MotionRuntime *runtime, std::vector<MotionHandle> children)
    : runtime_(runtime), children_(std::move(children)) {}

bool MotionSequenceHandle::isActive() const noexcept {
    if (!runtime_) return false;
    for (const MotionHandle &h : children_) {
        if (runtime_->isActive(h)) return true;
    }
    return false;
}

MotionHandle MotionSequenceHandle::child(int index) const {
    if (index < 0 || index >= static_cast<int>(children_.size())) return {};
    return children_[static_cast<std::size_t>(index)];
}

eve::Result<void> MotionSequenceHandle::complete() {
    if (!runtime_) return eve::Result<void>::failure(precondition("MotionSequenceHandle.complete: no runtime"));
    for (const MotionHandle &h : children_) {
        if (!runtime_->isActive(h)) continue;
        auto r = runtime_->complete(h);
        if (!r.ok()) return r;
    }
    return okApplied();
}

eve::Result<void> MotionSequenceHandle::cancel() {
    if (!runtime_) return eve::Result<void>::failure(precondition("MotionSequenceHandle.cancel: no runtime"));
    for (const MotionHandle &h : children_) {
        if (!runtime_->isActive(h)) continue;
        auto r = runtime_->cancel(h);
        if (!r.ok()) return r;
    }
    return okApplied();
}

MotionSequence::MotionSequence(MotionRuntime &runtime) : runtime_(runtime) {}

eve::Result<float> MotionSequence::spanOf(float delay, float duration, int loops) {
    if (!(delay >= 0.f) || !std::isfinite(delay))
        return eve::Result<float>::failure(badArg("MotionSequence: delay must be finite and >= 0"));
    if (!(duration >= 0.f) || !std::isfinite(duration))
        return eve::Result<float>::failure(badArg("MotionSequence: duration must be finite and >= 0"));
    if (loops < 0)
        return eve::Result<float>::failure(
            badArg("MotionSequence: infinite loops (-1) cannot be scheduled"));
    if (loops == 0)
        return eve::Result<float>::failure(badArg("MotionSequence: loops must be >= 1"));
    return eve::Result<float>::success(delay + duration * static_cast<float>(loops));
}

eve::Result<void> MotionSequence::schedule(float start, float span, Item item) {
    if (!(start >= 0.f) || !std::isfinite(start))
        return eve::Result<void>::failure(badArg("MotionSequence: start time must be finite and >= 0"));
    if (!(span >= 0.f) || !std::isfinite(span))
        return eve::Result<void>::failure(badArg("MotionSequence: span must be finite and >= 0"));
    item.start = start;
    items_.push_back(std::move(item));
    duration_ = std::max(duration_, start + span);
    return okApplied();
}

eve::Result<void> MotionSequence::appendAtCursor(Item item, float delay, float duration, int loops) {
    auto span = spanOf(delay, duration, loops);
    if (!span.ok()) return eve::Result<void>::failure(span.status());
    const float start = cursor_;
    auto scheduled = schedule(start, span.value(), std::move(item));
    if (!scheduled.ok()) return scheduled;
    lastAppendStart_ = start;
    hasAppend_       = true;
    cursor_          = start + span.value();
    return okApplied();
}

eve::Result<void> MotionSequence::joinAtLast(Item item, float delay, float duration, int loops) {
    auto span = spanOf(delay, duration, loops);
    if (!span.ok()) return eve::Result<void>::failure(span.status());
    const float start = hasAppend_ ? lastAppendStart_ : cursor_;
    auto scheduled = schedule(start, span.value(), std::move(item));
    if (!scheduled.ok()) return scheduled;
    cursor_ = std::max(cursor_, start + span.value());
    return okApplied();
}

eve::Result<void> MotionSequence::insertAt(float at, Item item, float delay, float duration, int loops) {
    auto span = spanOf(delay, duration, loops);
    if (!span.ok()) return eve::Result<void>::failure(span.status());
    auto scheduled = schedule(at, span.value(), std::move(item));
    if (!scheduled.ok()) return scheduled;
    cursor_ = std::max(cursor_, at + span.value());
    return okApplied();
}

eve::Result<void> MotionSequence::append(MotionBuilder &&builder) {
    if (&builder.runtime() != &runtime_)
        return eve::Result<void>::failure(badArg("MotionSequence.append: builder runtime mismatch"));
    auto taken = builder.takeDesc();
    if (!taken.ok()) return eve::Result<void>::failure(taken.status());
    auto desc = std::move(taken).value();
    const float delay    = desc.delay;
    const float duration = desc.duration;
    const int   loops    = desc.loops;
    Item        item;
    item.kind      = Kind::Float;
    item.floatDesc = std::move(desc);
    return appendAtCursor(std::move(item), delay, duration, loops);
}

eve::Result<void> MotionSequence::append(MotionVec2Builder &&builder) {
    if (&builder.runtime() != &runtime_)
        return eve::Result<void>::failure(badArg("MotionSequence.append: builder runtime mismatch"));
    auto taken = builder.takeDesc();
    if (!taken.ok()) return eve::Result<void>::failure(taken.status());
    auto desc = std::move(taken).value();
    const float delay    = desc.delay;
    const float duration = desc.duration;
    const int   loops    = desc.loops;
    Item        item;
    item.kind     = Kind::Vec2;
    item.vec2Desc = std::move(desc);
    return appendAtCursor(std::move(item), delay, duration, loops);
}

eve::Result<void> MotionSequence::append(MotionVec3Builder &&builder) {
    if (&builder.runtime() != &runtime_)
        return eve::Result<void>::failure(badArg("MotionSequence.append: builder runtime mismatch"));
    auto taken = builder.takeDesc();
    if (!taken.ok()) return eve::Result<void>::failure(taken.status());
    auto desc = std::move(taken).value();
    const float delay    = desc.delay;
    const float duration = desc.duration;
    const int   loops    = desc.loops;
    Item        item;
    item.kind     = Kind::Vec3;
    item.vec3Desc = std::move(desc);
    return appendAtCursor(std::move(item), delay, duration, loops);
}

eve::Result<void> MotionSequence::join(MotionBuilder &&builder) {
    if (&builder.runtime() != &runtime_)
        return eve::Result<void>::failure(badArg("MotionSequence.join: builder runtime mismatch"));
    auto taken = builder.takeDesc();
    if (!taken.ok()) return eve::Result<void>::failure(taken.status());
    auto desc = std::move(taken).value();
    const float delay    = desc.delay;
    const float duration = desc.duration;
    const int   loops    = desc.loops;
    Item        item;
    item.kind      = Kind::Float;
    item.floatDesc = std::move(desc);
    return joinAtLast(std::move(item), delay, duration, loops);
}

eve::Result<void> MotionSequence::join(MotionVec2Builder &&builder) {
    if (&builder.runtime() != &runtime_)
        return eve::Result<void>::failure(badArg("MotionSequence.join: builder runtime mismatch"));
    auto taken = builder.takeDesc();
    if (!taken.ok()) return eve::Result<void>::failure(taken.status());
    auto desc = std::move(taken).value();
    const float delay    = desc.delay;
    const float duration = desc.duration;
    const int   loops    = desc.loops;
    Item        item;
    item.kind     = Kind::Vec2;
    item.vec2Desc = std::move(desc);
    return joinAtLast(std::move(item), delay, duration, loops);
}

eve::Result<void> MotionSequence::join(MotionVec3Builder &&builder) {
    if (&builder.runtime() != &runtime_)
        return eve::Result<void>::failure(badArg("MotionSequence.join: builder runtime mismatch"));
    auto taken = builder.takeDesc();
    if (!taken.ok()) return eve::Result<void>::failure(taken.status());
    auto desc = std::move(taken).value();
    const float delay    = desc.delay;
    const float duration = desc.duration;
    const int   loops    = desc.loops;
    Item        item;
    item.kind     = Kind::Vec3;
    item.vec3Desc = std::move(desc);
    return joinAtLast(std::move(item), delay, duration, loops);
}

eve::Result<void> MotionSequence::insert(float atSeconds, MotionBuilder &&builder) {
    if (&builder.runtime() != &runtime_)
        return eve::Result<void>::failure(badArg("MotionSequence.insert: builder runtime mismatch"));
    auto taken = builder.takeDesc();
    if (!taken.ok()) return eve::Result<void>::failure(taken.status());
    auto desc = std::move(taken).value();
    const float delay    = desc.delay;
    const float duration = desc.duration;
    const int   loops    = desc.loops;
    Item        item;
    item.kind      = Kind::Float;
    item.floatDesc = std::move(desc);
    return insertAt(atSeconds, std::move(item), delay, duration, loops);
}

eve::Result<void> MotionSequence::insert(float atSeconds, MotionVec2Builder &&builder) {
    if (&builder.runtime() != &runtime_)
        return eve::Result<void>::failure(badArg("MotionSequence.insert: builder runtime mismatch"));
    auto taken = builder.takeDesc();
    if (!taken.ok()) return eve::Result<void>::failure(taken.status());
    auto desc = std::move(taken).value();
    const float delay    = desc.delay;
    const float duration = desc.duration;
    const int   loops    = desc.loops;
    Item        item;
    item.kind     = Kind::Vec2;
    item.vec2Desc = std::move(desc);
    return insertAt(atSeconds, std::move(item), delay, duration, loops);
}

eve::Result<void> MotionSequence::insert(float atSeconds, MotionVec3Builder &&builder) {
    if (&builder.runtime() != &runtime_)
        return eve::Result<void>::failure(badArg("MotionSequence.insert: builder runtime mismatch"));
    auto taken = builder.takeDesc();
    if (!taken.ok()) return eve::Result<void>::failure(taken.status());
    auto desc = std::move(taken).value();
    const float delay    = desc.delay;
    const float duration = desc.duration;
    const int   loops    = desc.loops;
    Item        item;
    item.kind     = Kind::Vec3;
    item.vec3Desc = std::move(desc);
    return insertAt(atSeconds, std::move(item), delay, duration, loops);
}

eve::Result<void> MotionSequence::appendInterval(float seconds) {
    if (!(seconds >= 0.f) || !std::isfinite(seconds))
        return eve::Result<void>::failure(badArg("MotionSequence.appendInterval: seconds must be finite and >= 0"));
    cursor_ += seconds;
    duration_ = std::max(duration_, cursor_);
    return okApplied();
}

eve::Result<MotionSequenceHandle> MotionSequence::run() {
    std::vector<MotionHandle> children;
    children.reserve(items_.size());

    for (Item &item : items_) {
        switch (item.kind) {
            case Kind::Float: {
                item.floatDesc.delay += item.start;
                auto spawned = runtime_.spawnFloat(item.floatDesc);
                if (!spawned.ok()) return eve::Result<MotionSequenceHandle>::failure(spawned.status());
                children.push_back(spawned.value());
                break;
            }
            case Kind::Vec2: {
                item.vec2Desc.delay += item.start;
                auto spawned = runtime_.spawnVec2(item.vec2Desc);
                if (!spawned.ok()) return eve::Result<MotionSequenceHandle>::failure(spawned.status());
                children.push_back(spawned.value());
                break;
            }
            case Kind::Vec3: {
                item.vec3Desc.delay += item.start;
                auto spawned = runtime_.spawnVec3(item.vec3Desc);
                if (!spawned.ok()) return eve::Result<MotionSequenceHandle>::failure(spawned.status());
                children.push_back(spawned.value());
                break;
            }
        }
    }

    items_.clear();
    cursor_          = 0.f;
    duration_        = 0.f;
    lastAppendStart_ = 0.f;
    hasAppend_       = false;
    return eve::Result<MotionSequenceHandle>::success(MotionSequenceHandle(&runtime_, std::move(children)));
}

}  // namespace eve::animation
