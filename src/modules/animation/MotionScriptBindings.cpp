#include "animation/MotionScriptBindings.h"

#include "animation/Animation.h"
#include "animation/MotionBuilder.h"
#include "animation/MotionRuntime.h"
#include "animation/MotionSequence.h"
#include "animation/MotionTypes.h"
#include "common/Exception.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <cstdint>
#include <string>
#include <utility>

namespace eve::animation {
namespace {

[[noreturn]] void throwMotionError(const eve::Diagnostic *diag) {
    throw Exception("%s", diag ? diag->message().c_str() : "Motion error");
}

MotionLoopMode parseLoopMode(const std::string &mode) {
    if (mode.empty() || mode == "restart") return MotionLoopMode::Restart;
    if (mode == "yoyo") return MotionLoopMode::Yoyo;
    throw Exception("Motion.loops: unknown mode '%s' (use restart|yoyo)", mode.c_str());
}

Animation *requireAnimation(Animation *animation) {
    if (!animation) throw Exception("MotionSequence: Animation is null");
    return animation;
}

class ScriptMotionHandle {
public:
    ScriptMotionHandle(MotionRuntime *runtime, MotionHandle handle)
        : runtime_(runtime), handle_(handle) {}

    bool isActive() const { return runtime_ && runtime_->isActive(handle_); }

    float value() const {
        if (!runtime_) throw Exception("Motion.value: no runtime");
        auto result = runtime_->floatValue(handle_);
        if (!result.ok()) throwMotionError(result.error());
        return result.value();
    }

    void complete() {
        if (!runtime_) throw Exception("Motion.complete: no runtime");
        auto result = runtime_->complete(handle_);
        if (!result.ok()) throwMotionError(result.error());
    }

    void cancel() {
        if (!runtime_) throw Exception("Motion.cancel: no runtime");
        auto result = runtime_->cancel(handle_);
        if (!result.ok()) throwMotionError(result.error());
    }

private:
    MotionRuntime *runtime_ = nullptr;
    MotionHandle   handle_{};
};

class ScriptMotionBuilder {
public:
    ScriptMotionBuilder(Animation *animation, float from, float to, float duration)
        : animation_(animation) {
        if (!animation_) throw Exception("MotionBuilder: Animation is null");
        desc_.from     = from;
        desc_.to       = to;
        desc_.duration = duration;
    }

    void ease(std::string kind) { desc_.ease = std::move(kind); }
    void delay(float seconds) { desc_.delay = seconds; }
    void loops(int count, std::string mode = "restart") {
        desc_.loops    = count;
        desc_.loopMode = parseLoopMode(mode);
    }
    void cancelOnError(bool enabled) { desc_.cancelOnError = enabled; }
    void frequency(int count) { desc_.frequency = count; }
    void dampingRatio(float ratio) { desc_.dampingRatio = ratio; }
    void seed(int value) { desc_.seed = static_cast<std::uint32_t>(value); }
    void stylePunch() { desc_.style = MotionStyle::Punch; }
    void styleShake() { desc_.style = MotionStyle::Shake; }

    ScriptMotionHandle *run() {
        ensureLive();
        consumed_      = true;
        desc_.sink     = nullptr;
        auto spawned   = animation_->motions().spawnFloat(desc_);
        if (!spawned.ok()) throwMotionError(spawned.error());
        return new ScriptMotionHandle(&animation_->motions(), spawned.value());
    }

    [[nodiscard]] MotionRuntime &runtime() {
        ensureLive();
        return animation_->motions();
    }

    [[nodiscard]] eve::Result<MotionRuntime::FloatDesc> takeDesc() {
        ensureLive();
        consumed_ = true;
        return eve::Result<MotionRuntime::FloatDesc>::success(std::move(desc_));
    }

private:
    void ensureLive() {
        if (!animation_) throw Exception("MotionBuilder: Animation is null");
        if (consumed_) throw Exception("MotionBuilder: already consumed (run/append)");
    }

    Animation               *animation_ = nullptr;
    MotionRuntime::FloatDesc desc_{};
    bool                     consumed_ = false;
};

class ScriptMotionSequenceHandle {
public:
    explicit ScriptMotionSequenceHandle(MotionSequenceHandle handle) : handle_(std::move(handle)) {}

    bool isActive() const { return handle_.isActive(); }
    int childCount() const { return handle_.childCount(); }

    void complete() {
        auto result = handle_.complete();
        if (!result.ok()) throwMotionError(result.error());
    }

    void cancel() {
        auto result = handle_.cancel();
        if (!result.ok()) throwMotionError(result.error());
    }

private:
    MotionSequenceHandle handle_;
};

class ScriptMotionSequence {
public:
    explicit ScriptMotionSequence(Animation *animation)
        : animation_(animation), sequence_(requireAnimation(animation)->motions()) {}

    void append(ScriptMotionBuilder *builder) {
        if (!builder) throw Exception("MotionSequence.append: builder is null");
        auto taken = builder->takeDesc();
        if (!taken.ok()) throwMotionError(taken.error());
        auto desc = std::move(taken).value();
        MotionBuilder bridge(animation_->motions(), desc.from, desc.to, desc.duration);
        bridge.ease(desc.ease).delay(desc.delay).loops(desc.loops, desc.loopMode).cancelOnError(desc.cancelOnError)
            .style(desc.style).frequency(desc.frequency).dampingRatio(desc.dampingRatio).seed(desc.seed);
        if (desc.sink) bridge.to(*desc.sink);
        auto scheduled = sequence_.append(std::move(bridge));
        if (!scheduled.ok()) throwMotionError(scheduled.error());
    }

    void join(ScriptMotionBuilder *builder) {
        if (!builder) throw Exception("MotionSequence.join: builder is null");
        auto taken = builder->takeDesc();
        if (!taken.ok()) throwMotionError(taken.error());
        auto desc = std::move(taken).value();
        MotionBuilder bridge(animation_->motions(), desc.from, desc.to, desc.duration);
        bridge.ease(desc.ease).delay(desc.delay).loops(desc.loops, desc.loopMode).cancelOnError(desc.cancelOnError)
            .style(desc.style).frequency(desc.frequency).dampingRatio(desc.dampingRatio).seed(desc.seed);
        if (desc.sink) bridge.to(*desc.sink);
        auto scheduled = sequence_.join(std::move(bridge));
        if (!scheduled.ok()) throwMotionError(scheduled.error());
    }

    void insert(float atSeconds, ScriptMotionBuilder *builder) {
        if (!builder) throw Exception("MotionSequence.insert: builder is null");
        auto taken = builder->takeDesc();
        if (!taken.ok()) throwMotionError(taken.error());
        auto desc = std::move(taken).value();
        MotionBuilder bridge(animation_->motions(), desc.from, desc.to, desc.duration);
        bridge.ease(desc.ease).delay(desc.delay).loops(desc.loops, desc.loopMode).cancelOnError(desc.cancelOnError)
            .style(desc.style).frequency(desc.frequency).dampingRatio(desc.dampingRatio).seed(desc.seed);
        if (desc.sink) bridge.to(*desc.sink);
        auto scheduled = sequence_.insert(atSeconds, std::move(bridge));
        if (!scheduled.ok()) throwMotionError(scheduled.error());
    }

    void appendInterval(float seconds) {
        auto scheduled = sequence_.appendInterval(seconds);
        if (!scheduled.ok()) throwMotionError(scheduled.error());
    }

    float cursor() const { return sequence_.cursor(); }
    float duration() const { return sequence_.duration(); }
    int itemCount() const { return sequence_.itemCount(); }

    ScriptMotionSequenceHandle *run() {
        auto started = sequence_.run();
        if (!started.ok()) throwMotionError(started.error());
        return new ScriptMotionSequenceHandle(std::move(started).value());
    }

private:
    Animation     *animation_ = nullptr;
    MotionSequence sequence_;
};

}  // namespace

void exposeMotionScriptBindings(ssq::Table &table, ssq::Class &animationClass) {
    auto motion = table.addClass<ScriptMotionHandle>(
        "Motion", std::function<ScriptMotionHandle *()>([]() -> ScriptMotionHandle * { return nullptr; }),
        true);
    motion.addFunc("isActive", &ScriptMotionHandle::isActive);
    motion.addFunc("value", &ScriptMotionHandle::value);
    motion.addFunc("complete", &ScriptMotionHandle::complete);
    motion.addFunc("cancel", &ScriptMotionHandle::cancel);

    auto builder = table.addClass<ScriptMotionBuilder>(
        "MotionBuilder",
        std::function<ScriptMotionBuilder *()>([]() -> ScriptMotionBuilder * { return nullptr; }), true);
    builder.addFunc("ease", &ScriptMotionBuilder::ease);
    builder.addFunc("delay", &ScriptMotionBuilder::delay);
    builder.addFunc("loops", &ScriptMotionBuilder::loops);
    builder.addFunc("cancelOnError", &ScriptMotionBuilder::cancelOnError);
    builder.addFunc("frequency", &ScriptMotionBuilder::frequency);
    builder.addFunc("dampingRatio", &ScriptMotionBuilder::dampingRatio);
    builder.addFunc("seed", &ScriptMotionBuilder::seed);
    builder.addFunc("run", &ScriptMotionBuilder::run);

    auto seqHandle = table.addClass<ScriptMotionSequenceHandle>(
        "MotionSequenceHandle",
        std::function<ScriptMotionSequenceHandle *()>([]() -> ScriptMotionSequenceHandle * { return nullptr; }),
        true);
    seqHandle.addFunc("isActive", &ScriptMotionSequenceHandle::isActive);
    seqHandle.addFunc("childCount", &ScriptMotionSequenceHandle::childCount);
    seqHandle.addFunc("complete", &ScriptMotionSequenceHandle::complete);
    seqHandle.addFunc("cancel", &ScriptMotionSequenceHandle::cancel);

    auto seq = table.addClass<ScriptMotionSequence>(
        "MotionSequence",
        std::function<ScriptMotionSequence *()>([]() -> ScriptMotionSequence * { return nullptr; }), true);
    seq.addFunc("append", &ScriptMotionSequence::append);
    seq.addFunc("join", &ScriptMotionSequence::join);
    seq.addFunc("insert", &ScriptMotionSequence::insert);
    seq.addFunc("appendInterval", &ScriptMotionSequence::appendInterval);
    seq.addFunc("cursor", &ScriptMotionSequence::cursor);
    seq.addFunc("duration", &ScriptMotionSequence::duration);
    seq.addFunc("itemCount", &ScriptMotionSequence::itemCount);
    seq.addFunc("run", &ScriptMotionSequence::run);

    animationClass.addFunc(
        "newMotion",
        std::function<ScriptMotionBuilder *(Animation *, float, float, float)>(
            [](Animation *self, float from, float to, float duration) -> ScriptMotionBuilder * {
                return new ScriptMotionBuilder(self, from, to, duration);
            }));
    animationClass.addFunc(
        "newMotionPunch",
        std::function<ScriptMotionBuilder *(Animation *, float, float, float)>(
            [](Animation *self, float from, float strength, float duration) -> ScriptMotionBuilder * {
                auto *b = new ScriptMotionBuilder(self, from, strength, duration);
                b->stylePunch();
                return b;
            }));
    animationClass.addFunc(
        "newMotionShake",
        std::function<ScriptMotionBuilder *(Animation *, float, float, float)>(
            [](Animation *self, float from, float strength, float duration) -> ScriptMotionBuilder * {
                auto *b = new ScriptMotionBuilder(self, from, strength, duration);
                b->styleShake();
                return b;
            }));
    animationClass.addFunc(
        "newMotionSequence",
        std::function<ScriptMotionSequence *(Animation *)>(
            [](Animation *self) -> ScriptMotionSequence * { return new ScriptMotionSequence(self); }));
}

}  // namespace eve::animation
