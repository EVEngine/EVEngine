#include "animation/MotionBuilder.h"

#include "common/Diagnostic.h"

#include <cstdint>

namespace eve::animation {
namespace {

[[nodiscard]] eve::Diagnostic consumedDiag(const char *api) {
    return eve::Diagnostic::error(eve::DiagnosticCode::PreconditionViolation,
                                  std::string(api) + ": builder already consumed");
}

}  // namespace

MotionBuilder::MotionBuilder(MotionRuntime &runtime, float from, float to, float duration)
    : runtime_(runtime) {
    desc_.from     = from;
    desc_.to       = to;
    desc_.duration = duration;
}

MotionBuilder &MotionBuilder::ease(std::string kind) {
    desc_.ease = std::move(kind);
    return *this;
}

MotionBuilder &MotionBuilder::delay(float seconds) {
    desc_.delay = seconds;
    return *this;
}

MotionBuilder &MotionBuilder::loops(int count, MotionLoopMode mode) {
    desc_.loops    = count;
    desc_.loopMode = mode;
    return *this;
}

MotionBuilder &MotionBuilder::onUpdate(MotionRuntime::FloatCallback cb) {
    desc_.onUpdate = std::move(cb);
    return *this;
}

MotionBuilder &MotionBuilder::onComplete(MotionRuntime::VoidCallback cb) {
    desc_.onComplete = std::move(cb);
    return *this;
}

MotionBuilder &MotionBuilder::onCancel(MotionRuntime::VoidCallback cb) {
    desc_.onCancel = std::move(cb);
    return *this;
}

MotionBuilder &MotionBuilder::cancelOnError(bool enabled) {
    desc_.cancelOnError = enabled;
    return *this;
}

MotionBuilder &MotionBuilder::to(IMotionFloatSink &sink) {
    desc_.sink = &sink;
    return *this;
}

eve::Result<MotionHandle> MotionBuilder::run() {
    if (consumed_) return eve::Result<MotionHandle>::failure(consumedDiag("MotionBuilder.run"));
    consumed_  = true;
    desc_.sink = nullptr;
    return runtime_.spawnFloat(desc_);
}

eve::Result<MotionHandle> MotionBuilder::bind(IMotionFloatSink &sink) {
    if (consumed_) return eve::Result<MotionHandle>::failure(consumedDiag("MotionBuilder.bind"));
    consumed_  = true;
    desc_.sink = &sink;
    return runtime_.spawnFloat(desc_);
}

eve::Result<MotionRuntime::FloatDesc> MotionBuilder::takeDesc() {
    if (consumed_) return eve::Result<MotionRuntime::FloatDesc>::failure(consumedDiag("MotionBuilder.takeDesc"));
    consumed_ = true;
    return eve::Result<MotionRuntime::FloatDesc>::success(std::move(desc_));
}

MotionVec2Builder::MotionVec2Builder(MotionRuntime &runtime, MotionVec2 from, MotionVec2 to,
                                     float duration)
    : runtime_(runtime) {
    desc_.from     = from;
    desc_.to       = to;
    desc_.duration = duration;
}

MotionVec2Builder &MotionVec2Builder::ease(std::string kind) {
    desc_.ease = std::move(kind);
    return *this;
}

MotionVec2Builder &MotionVec2Builder::delay(float seconds) {
    desc_.delay = seconds;
    return *this;
}

MotionVec2Builder &MotionVec2Builder::loops(int count, MotionLoopMode mode) {
    desc_.loops    = count;
    desc_.loopMode = mode;
    return *this;
}

MotionVec2Builder &MotionVec2Builder::onUpdate(MotionRuntime::Vec2Callback cb) {
    desc_.onUpdate = std::move(cb);
    return *this;
}

MotionVec2Builder &MotionVec2Builder::onComplete(MotionRuntime::VoidCallback cb) {
    desc_.onComplete = std::move(cb);
    return *this;
}

MotionVec2Builder &MotionVec2Builder::onCancel(MotionRuntime::VoidCallback cb) {
    desc_.onCancel = std::move(cb);
    return *this;
}

MotionVec2Builder &MotionVec2Builder::cancelOnError(bool enabled) {
    desc_.cancelOnError = enabled;
    return *this;
}

MotionVec2Builder &MotionVec2Builder::to(IMotionVec2Sink &sink) {
    desc_.sink = &sink;
    return *this;
}

eve::Result<MotionHandle> MotionVec2Builder::run() {
    if (consumed_) return eve::Result<MotionHandle>::failure(consumedDiag("MotionVec2Builder.run"));
    consumed_  = true;
    desc_.sink = nullptr;
    return runtime_.spawnVec2(desc_);
}

eve::Result<MotionHandle> MotionVec2Builder::bind(IMotionVec2Sink &sink) {
    if (consumed_) return eve::Result<MotionHandle>::failure(consumedDiag("MotionVec2Builder.bind"));
    consumed_  = true;
    desc_.sink = &sink;
    return runtime_.spawnVec2(desc_);
}

eve::Result<MotionRuntime::Vec2Desc> MotionVec2Builder::takeDesc() {
    if (consumed_)
        return eve::Result<MotionRuntime::Vec2Desc>::failure(consumedDiag("MotionVec2Builder.takeDesc"));
    consumed_ = true;
    return eve::Result<MotionRuntime::Vec2Desc>::success(std::move(desc_));
}

MotionVec3Builder::MotionVec3Builder(MotionRuntime &runtime, MotionVec3 from, MotionVec3 to,
                                     float duration)
    : runtime_(runtime) {
    desc_.from     = from;
    desc_.to       = to;
    desc_.duration = duration;
}

MotionVec3Builder &MotionVec3Builder::ease(std::string kind) {
    desc_.ease = std::move(kind);
    return *this;
}

MotionVec3Builder &MotionVec3Builder::delay(float seconds) {
    desc_.delay = seconds;
    return *this;
}

MotionVec3Builder &MotionVec3Builder::loops(int count, MotionLoopMode mode) {
    desc_.loops    = count;
    desc_.loopMode = mode;
    return *this;
}

MotionVec3Builder &MotionVec3Builder::onUpdate(MotionRuntime::Vec3Callback cb) {
    desc_.onUpdate = std::move(cb);
    return *this;
}

MotionVec3Builder &MotionVec3Builder::onComplete(MotionRuntime::VoidCallback cb) {
    desc_.onComplete = std::move(cb);
    return *this;
}

MotionVec3Builder &MotionVec3Builder::onCancel(MotionRuntime::VoidCallback cb) {
    desc_.onCancel = std::move(cb);
    return *this;
}

MotionVec3Builder &MotionVec3Builder::cancelOnError(bool enabled) {
    desc_.cancelOnError = enabled;
    return *this;
}

MotionVec3Builder &MotionVec3Builder::to(IMotionVec3Sink &sink) {
    desc_.sink = &sink;
    return *this;
}

eve::Result<MotionHandle> MotionVec3Builder::run() {
    if (consumed_) return eve::Result<MotionHandle>::failure(consumedDiag("MotionVec3Builder.run"));
    consumed_  = true;
    desc_.sink = nullptr;
    return runtime_.spawnVec3(desc_);
}

eve::Result<MotionHandle> MotionVec3Builder::bind(IMotionVec3Sink &sink) {
    if (consumed_) return eve::Result<MotionHandle>::failure(consumedDiag("MotionVec3Builder.bind"));
    consumed_  = true;
    desc_.sink = &sink;
    return runtime_.spawnVec3(desc_);
}

eve::Result<MotionRuntime::Vec3Desc> MotionVec3Builder::takeDesc() {
    if (consumed_)
        return eve::Result<MotionRuntime::Vec3Desc>::failure(consumedDiag("MotionVec3Builder.takeDesc"));
    consumed_ = true;
    return eve::Result<MotionRuntime::Vec3Desc>::success(std::move(desc_));
}


MotionBuilder &MotionBuilder::style(MotionStyle style) {
    desc_.style = style;
    return *this;
}
MotionBuilder &MotionBuilder::frequency(int count) {
    desc_.frequency = count;
    return *this;
}
MotionBuilder &MotionBuilder::dampingRatio(float ratio) {
    desc_.dampingRatio = ratio;
    return *this;
}
MotionBuilder &MotionBuilder::seed(std::uint32_t value) {
    desc_.seed = value;
    return *this;
}

MotionVec2Builder &MotionVec2Builder::style(MotionStyle style) {
    desc_.style = style;
    return *this;
}
MotionVec2Builder &MotionVec2Builder::frequency(int count) {
    desc_.frequency = count;
    return *this;
}
MotionVec2Builder &MotionVec2Builder::dampingRatio(float ratio) {
    desc_.dampingRatio = ratio;
    return *this;
}
MotionVec2Builder &MotionVec2Builder::seed(std::uint32_t value) {
    desc_.seed = value;
    return *this;
}

MotionVec3Builder &MotionVec3Builder::style(MotionStyle style) {
    desc_.style = style;
    return *this;
}
MotionVec3Builder &MotionVec3Builder::frequency(int count) {
    desc_.frequency = count;
    return *this;
}
MotionVec3Builder &MotionVec3Builder::dampingRatio(float ratio) {
    desc_.dampingRatio = ratio;
    return *this;
}
MotionVec3Builder &MotionVec3Builder::seed(std::uint32_t value) {
    desc_.seed = value;
    return *this;
}

MotionColorBuilder::MotionColorBuilder(MotionRuntime &runtime, MotionColor from, MotionColor to,
                                       float duration)
    : runtime_(runtime) {
    desc_.from     = from;
    desc_.to       = to;
    desc_.duration = duration;
}
MotionColorBuilder &MotionColorBuilder::ease(std::string kind) {
    desc_.ease = std::move(kind);
    return *this;
}
MotionColorBuilder &MotionColorBuilder::delay(float seconds) {
    desc_.delay = seconds;
    return *this;
}
MotionColorBuilder &MotionColorBuilder::loops(int count, MotionLoopMode mode) {
    desc_.loops    = count;
    desc_.loopMode = mode;
    return *this;
}
MotionColorBuilder &MotionColorBuilder::onUpdate(MotionRuntime::ColorCallback cb) {
    desc_.onUpdate = std::move(cb);
    return *this;
}
MotionColorBuilder &MotionColorBuilder::onComplete(MotionRuntime::VoidCallback cb) {
    desc_.onComplete = std::move(cb);
    return *this;
}
MotionColorBuilder &MotionColorBuilder::onCancel(MotionRuntime::VoidCallback cb) {
    desc_.onCancel = std::move(cb);
    return *this;
}
MotionColorBuilder &MotionColorBuilder::cancelOnError(bool enabled) {
    desc_.cancelOnError = enabled;
    return *this;
}
MotionColorBuilder &MotionColorBuilder::to(IMotionColorSink &sink) {
    desc_.sink = &sink;
    return *this;
}
eve::Result<MotionHandle> MotionColorBuilder::run() {
    if (consumed_) return eve::Result<MotionHandle>::failure(consumedDiag("MotionColorBuilder.run"));
    consumed_  = true;
    desc_.sink = nullptr;
    return runtime_.spawnColor(desc_);
}
eve::Result<MotionHandle> MotionColorBuilder::bind(IMotionColorSink &sink) {
    if (consumed_) return eve::Result<MotionHandle>::failure(consumedDiag("MotionColorBuilder.bind"));
    consumed_  = true;
    desc_.sink = &sink;
    return runtime_.spawnColor(desc_);
}
eve::Result<MotionRuntime::ColorDesc> MotionColorBuilder::takeDesc() {
    if (consumed_)
        return eve::Result<MotionRuntime::ColorDesc>::failure(consumedDiag("MotionColorBuilder.takeDesc"));
    consumed_ = true;
    return eve::Result<MotionRuntime::ColorDesc>::success(std::move(desc_));
}

MotionQuatBuilder::MotionQuatBuilder(MotionRuntime &runtime, MotionQuat from, MotionQuat to,
                                     float duration)
    : runtime_(runtime) {
    desc_.from     = from;
    desc_.to       = to;
    desc_.duration = duration;
}
MotionQuatBuilder &MotionQuatBuilder::ease(std::string kind) {
    desc_.ease = std::move(kind);
    return *this;
}
MotionQuatBuilder &MotionQuatBuilder::delay(float seconds) {
    desc_.delay = seconds;
    return *this;
}
MotionQuatBuilder &MotionQuatBuilder::loops(int count, MotionLoopMode mode) {
    desc_.loops    = count;
    desc_.loopMode = mode;
    return *this;
}
MotionQuatBuilder &MotionQuatBuilder::onUpdate(MotionRuntime::QuatCallback cb) {
    desc_.onUpdate = std::move(cb);
    return *this;
}
MotionQuatBuilder &MotionQuatBuilder::onComplete(MotionRuntime::VoidCallback cb) {
    desc_.onComplete = std::move(cb);
    return *this;
}
MotionQuatBuilder &MotionQuatBuilder::onCancel(MotionRuntime::VoidCallback cb) {
    desc_.onCancel = std::move(cb);
    return *this;
}
MotionQuatBuilder &MotionQuatBuilder::cancelOnError(bool enabled) {
    desc_.cancelOnError = enabled;
    return *this;
}
MotionQuatBuilder &MotionQuatBuilder::to(IMotionQuatSink &sink) {
    desc_.sink = &sink;
    return *this;
}
eve::Result<MotionHandle> MotionQuatBuilder::run() {
    if (consumed_) return eve::Result<MotionHandle>::failure(consumedDiag("MotionQuatBuilder.run"));
    consumed_  = true;
    desc_.sink = nullptr;
    return runtime_.spawnQuat(desc_);
}
eve::Result<MotionHandle> MotionQuatBuilder::bind(IMotionQuatSink &sink) {
    if (consumed_) return eve::Result<MotionHandle>::failure(consumedDiag("MotionQuatBuilder.bind"));
    consumed_  = true;
    desc_.sink = &sink;
    return runtime_.spawnQuat(desc_);
}
eve::Result<MotionRuntime::QuatDesc> MotionQuatBuilder::takeDesc() {
    if (consumed_)
        return eve::Result<MotionRuntime::QuatDesc>::failure(consumedDiag("MotionQuatBuilder.takeDesc"));
    consumed_ = true;
    return eve::Result<MotionRuntime::QuatDesc>::success(std::move(desc_));
}

}  // namespace eve::animation
