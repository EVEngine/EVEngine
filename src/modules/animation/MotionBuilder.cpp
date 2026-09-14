#include "animation/MotionBuilder.h"

namespace eve::animation {

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

eve::Result<MotionHandle> MotionBuilder::run() {
    desc_.sink = nullptr;
    return runtime_.spawnFloat(desc_);
}

eve::Result<MotionHandle> MotionBuilder::bind(IMotionFloatSink &sink) {
    desc_.sink = &sink;
    return runtime_.spawnFloat(desc_);
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

eve::Result<MotionHandle> MotionVec2Builder::run() {
    desc_.sink = nullptr;
    return runtime_.spawnVec2(desc_);
}

eve::Result<MotionHandle> MotionVec2Builder::bind(IMotionVec2Sink &sink) {
    desc_.sink = &sink;
    return runtime_.spawnVec2(desc_);
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

eve::Result<MotionHandle> MotionVec3Builder::run() {
    desc_.sink = nullptr;
    return runtime_.spawnVec3(desc_);
}

eve::Result<MotionHandle> MotionVec3Builder::bind(IMotionVec3Sink &sink) {
    desc_.sink = &sink;
    return runtime_.spawnVec3(desc_);
}

}  // namespace eve::animation
