#include "animation/MotionScriptBindings.h"

#include "animation/Animation.h"
#include "animation/MotionBuilder.h"
#include "animation/MotionRuntime.h"
#include "animation/MotionSequence.h"
#include "animation/MotionTypes.h"
#include "common/Exception.h"
#include "common/ScriptProvider.h"
#include "common/Value.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

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

/**
 * @brief 脚本对象实现的 `IMotionFloatSink`。
 *
 * 脚本对象实现 `write(value)`，每次求值调用一次。**失败原样返回**：`MotionRuntime` 靠这个返回值
 * 决定要不要按 `cancelOnError` 取消 motion——脚本抛错被吞掉会让动画继续跑在一个已经不再接收值的
 * 目标上。脚本对象的持有与代际由 provider 注册表负责（拿一份 VM 引用、给一个带代际的句柄），
 * 所以适配器只存句柄，不存裸对象。
 */
class ScriptMotionFloatSink final : public IMotionFloatSink {
public:
    explicit ScriptMotionFloatSink(ScriptProviderHandle handle) : handle_(std::move(handle)) {}

    /** @brief 本适配器所服务的那一代脚本对象。 */
    [[nodiscard]] const ScriptProviderHandle& handle() const noexcept { return handle_; }

    [[nodiscard]] eve::Result<void> write(float value) override {
        const eve::ScriptArg args[] = {eve::ScriptArg::fromNumber(double(value))};
        return eve::ScriptProviderRegistry::instance().call(handle_, "write", args);
    }

private:
    ScriptProviderHandle handle_;
};

/**
 * @brief 模块级持有脚本 motion sink 适配器。
 *
 * 为什么必须有主人：`MotionRuntime` 的槽位**借用** sink 指针，所以适配器要活到 motion 结束
 * （`advance()` 随时可能用到它）。这里按"一次 `to()` 一个适配器"登记，模块析构时整体撤回；
 * 正常用法是绑一次跑一个动画，不是每帧重绑（每帧重绑会让这个列表一直长）。
 */
struct ScriptMotionSinkEntry {
    std::string                            providerName;
    std::unique_ptr<ScriptMotionFloatSink> adapter;
};

std::vector<ScriptMotionSinkEntry>& scriptMotionSinkEntries() {
    static std::vector<ScriptMotionSinkEntry> entries;
    return entries;
}

std::uint64_t& scriptMotionSinkSequence() {
    static std::uint64_t sequence = 0;
    return sequence;
}

}  // namespace

void clearScriptMotionSinkAdapters() {
    auto& entries = scriptMotionSinkEntries();
    for (ScriptMotionSinkEntry& entry : entries) {
        eve::ScriptProviderRegistry& registry = eve::ScriptProviderRegistry::instance();
        registry.unload(eve::ScriptProviderHandle{entry.providerName, registry.generationOf(entry.providerName)})
            .ignore("module teardown may already have dropped this motion sink provider");
    }
    entries.clear();
}

namespace {

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

    /**
     * @brief 把一个**脚本对象**绑成 sink（不启动；`run()` 会带着它跑）。
     *
     * 对象要实现 `write(value)`。这就是把 animation 侧五个"直接持有指针"的 sink 接口之一
     * （`IMotionFloatSink`）接到脚本上的方式：适配器实现那个接口，脚本对象扮演原本由 C++ 适配器
     * 扮演的角色。**重复 `to()` 以后一次为准**，前一个适配器留到模块析构。
     */
    void to(const ssq::Object& sink) {
        ensureLive();
        if (sink.getType() != ssq::Type::INSTANCE)
            throw Exception("MotionBuilder.to: sink must be an instance implementing write(value)");
        const std::string name      = "animation.motion-sink." + std::to_string(++scriptMotionSinkSequence());
        auto              published = eve::ScriptProviderRegistry::instance().publish(name, sink);
        if (!published.ok()) throwMotionError(published.error());
        auto adapter = std::make_unique<ScriptMotionFloatSink>(published.value());
        desc_.sink   = adapter.get();
        scriptMotionSinkEntries().push_back(ScriptMotionSinkEntry{name, std::move(adapter)});
    }

    /** @brief 绑上脚本 sink 并立刻开跑（对应 C++ 的 `MotionBuilder::bind`）。 */
    ScriptMotionHandle* bind(const ssq::Object& sink) {
        to(sink);
        return run();
    }

    ScriptMotionHandle* run() {
        ensureLive();
        consumed_    = true;
        auto spawned = animation_->motions().spawnFloat(desc_);
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
    builder.addFunc("to", [](ScriptMotionBuilder* self, const ssq::Object& sink) {
        if (!self) throw Exception("MotionBuilder.to: builder is null");
        self->to(sink);
    });
    builder.addFunc("bind", [](ScriptMotionBuilder* self, const ssq::Object& sink) -> ScriptMotionHandle* {
        if (!self) throw Exception("MotionBuilder.bind: builder is null");
        return self->bind(sink);
    });
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
    animationClass.addFunc(
        "ensureMotionCapacity",
        std::function<void(Animation *, int)>(
            [](Animation *self, int count) {
                if (!self) throw Exception("ensureMotionCapacity: Animation is null");
                if (count < 0) throw Exception("ensureMotionCapacity: count must be >= 0");
                self->ensureMotionCapacity(static_cast<std::size_t>(count));
            }));
    animationClass.addFunc(
        "getMotionFloatCapacity",
        std::function<int(Animation *)>(
            [](Animation *self) -> int {
                if (!self) throw Exception("getMotionFloatCapacity: Animation is null");
                return static_cast<int>(self->getMotionFloatCapacity());
            }));
    animationClass.addFunc(
        "getMotionFloatFreeCount",
        std::function<int(Animation *)>(
            [](Animation *self) -> int {
                if (!self) throw Exception("getMotionFloatFreeCount: Animation is null");
                return static_cast<int>(self->getMotionFloatFreeCount());
            }));
}

}  // namespace eve::animation
