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
 * @brief 从脚本表里读一个数值字段。
 * @remarks 不是表、缺字段、不是数都**抛**：脚本作者要看到明确原因，而不是收到一个全零的 motion。
 */
double readMotionNumber(const ssq::Object& table, const char* field, const char* what) {
    if (table.getType() != ssq::Type::TABLE) throw Exception("%s must be a table (x/y/z or r/g/b/a)", what);
    const ssq::Object value = table.find(field);
    const ssq::Type   type  = value.getType();
    if (type != ssq::Type::FLOAT && type != ssq::Type::INTEGER)
        throw Exception("%s requires a numeric '%s'", what, field);
    return value.toFloat();
}

/**
 * @brief 一种 motion 形状的脚本侧特征：值类型、描述类型、sink 接口、编组与生成。
 *
 * 四种非 float 形状（vec2/vec3/color/quat）共用下面那套模板：它们的差别只有"值怎么编组"
 * "怎么进描述""怎么 spawn"，写四遍必然漂移。float 那一套保留原样（它多了 style 之外的
 * 历史行为，不动它更安全）。
 */
struct MotionVec2Traits {
    using Value = MotionVec2;
    using Desc  = MotionRuntime::Vec2Desc;
    using Sink  = IMotionVec2Sink;

    static constexpr bool        kHasStyle = true;
    static constexpr const char* kType     = "MotionVec2";

    static Value read(const ssq::Object& table, const char* what) {
        return MotionVec2{float(readMotionNumber(table, "x", what)), float(readMotionNumber(table, "y", what))};
    }
    static std::vector<eve::ScriptArg> scriptArgs(const Value& value) {
        return {eve::ScriptArg::fromNumber(double(value.x)), eve::ScriptArg::fromNumber(double(value.y))};
    }
    static void configure(Desc& desc, Value from, Value to, float duration) {
        desc.from     = from;
        desc.to       = to;
        desc.duration = duration;
    }
    static eve::Result<MotionHandle> spawn(MotionRuntime& runtime, const Desc& desc) { return runtime.spawnVec2(desc); }
};

struct MotionVec3Traits {
    using Value = MotionVec3;
    using Desc  = MotionRuntime::Vec3Desc;
    using Sink  = IMotionVec3Sink;

    static constexpr bool        kHasStyle = true;
    static constexpr const char* kType     = "MotionVec3";

    static Value read(const ssq::Object& table, const char* what) {
        return MotionVec3{float(readMotionNumber(table, "x", what)), float(readMotionNumber(table, "y", what)),
                          float(readMotionNumber(table, "z", what))};
    }
    static std::vector<eve::ScriptArg> scriptArgs(const Value& value) {
        return {eve::ScriptArg::fromNumber(double(value.x)), eve::ScriptArg::fromNumber(double(value.y)),
                eve::ScriptArg::fromNumber(double(value.z))};
    }
    static void configure(Desc& desc, Value from, Value to, float duration) {
        desc.from     = from;
        desc.to       = to;
        desc.duration = duration;
    }
    static eve::Result<MotionHandle> spawn(MotionRuntime& runtime, const Desc& desc) { return runtime.spawnVec3(desc); }
};

struct MotionColorTraits {
    using Value = MotionColor;
    using Desc  = MotionRuntime::ColorDesc;
    using Sink  = IMotionColorSink;

    static constexpr bool        kHasStyle = false;
    static constexpr const char* kType     = "MotionColor";

    static Value read(const ssq::Object& table, const char* what) {
        return MotionColor{float(readMotionNumber(table, "r", what)), float(readMotionNumber(table, "g", what)),
                           float(readMotionNumber(table, "b", what)), float(readMotionNumber(table, "a", what))};
    }
    static std::vector<eve::ScriptArg> scriptArgs(const Value& value) {
        return {eve::ScriptArg::fromNumber(double(value.r)), eve::ScriptArg::fromNumber(double(value.g)),
                eve::ScriptArg::fromNumber(double(value.b)), eve::ScriptArg::fromNumber(double(value.a))};
    }
    static void configure(Desc& desc, Value from, Value to, float duration) {
        desc.from     = from;
        desc.to       = to;
        desc.duration = duration;
    }
    static eve::Result<MotionHandle> spawn(MotionRuntime& runtime, const Desc& desc) {
        return runtime.spawnColor(desc);
    }
};

struct MotionQuatTraits {
    using Value = MotionQuat;
    using Desc  = MotionRuntime::QuatDesc;
    using Sink  = IMotionQuatSink;

    static constexpr bool        kHasStyle = false;
    static constexpr const char* kType     = "MotionQuat";

    static Value read(const ssq::Object& table, const char* what) {
        return MotionQuat{float(readMotionNumber(table, "x", what)), float(readMotionNumber(table, "y", what)),
                          float(readMotionNumber(table, "z", what)), float(readMotionNumber(table, "w", what))};
    }
    static std::vector<eve::ScriptArg> scriptArgs(const Value& value) {
        return {eve::ScriptArg::fromNumber(double(value.x)), eve::ScriptArg::fromNumber(double(value.y)),
                eve::ScriptArg::fromNumber(double(value.z)), eve::ScriptArg::fromNumber(double(value.w))};
    }
    static void configure(Desc& desc, Value from, Value to, float duration) {
        desc.from     = from;
        desc.to       = to;
        desc.duration = duration;
    }
    static eve::Result<MotionHandle> spawn(MotionRuntime& runtime, const Desc& desc) { return runtime.spawnQuat(desc); }
};

/**
 * @brief 脚本对象实现的某一种 motion sink（vec2/vec3/color/quat）。
 *
 * 每次求值把值按**位置参数**推给脚本的 `write(...)`（vec2 两个数、vec3 三个、color 四个 r/g/b/a、
 * quat 四个 x/y/z/w）：这是逐帧路径，不为了好看给每帧造一张表。失败原样返回给 runtime。
 */
template <class Traits>
class ScriptMotionSink final : public Traits::Sink {
public:
    explicit ScriptMotionSink(ScriptProviderHandle handle) : handle_(std::move(handle)) {}

    /** @brief 本适配器所服务的那一代脚本对象。 */
    [[nodiscard]] const ScriptProviderHandle& handle() const noexcept { return handle_; }

    [[nodiscard]] eve::Result<void> write(typename Traits::Value value) override {
        const std::vector<eve::ScriptArg> args = Traits::scriptArgs(value);
        return eve::ScriptProviderRegistry::instance().call(handle_, "write", args);
    }

private:
    ScriptProviderHandle handle_;
};

/**
 * @brief 模块级持有脚本 motion sink 适配器（每种形状一份）。
 *
 * 为什么必须有主人：`MotionRuntime` 的槽位**借用** sink 指针，所以适配器要活到 motion 结束
 * （`advance()` 随时可能用到它）。这里按"一次 `to()`/`bind()` 一个适配器"登记，模块析构时整体撤回；
 * 正常用法是绑一次跑一个动画，不是每帧重绑（每帧重绑会让这张表一直长）。
 */
template <class Adapter>
struct ScriptMotionSinkStore {
    std::vector<std::string>              providerNames;
    std::vector<std::unique_ptr<Adapter>> adapters;
};

struct ScriptMotionSinkStores {
    ScriptMotionSinkStore<ScriptMotionFloatSink>               floats;
    ScriptMotionSinkStore<ScriptMotionSink<MotionVec2Traits>>  vec2s;
    ScriptMotionSinkStore<ScriptMotionSink<MotionVec3Traits>>  vec3s;
    ScriptMotionSinkStore<ScriptMotionSink<MotionColorTraits>> colors;
    ScriptMotionSinkStore<ScriptMotionSink<MotionQuatTraits>>  quats;
};

ScriptMotionSinkStores& scriptMotionSinkStores() {
    static ScriptMotionSinkStores stores;
    return stores;
}

std::uint64_t& scriptMotionSinkSequence() {
    static std::uint64_t sequence = 0;
    return sequence;
}

/**
 * @brief 发布脚本对象并把它交给模块持有。
 * @return 借用指针，供 motion 描述使用；适配器活到模块析构。
 */
template <class Adapter>
Adapter* adoptScriptMotionSink(ScriptMotionSinkStore<Adapter>& store, const ssq::Object& sink) {
    const std::string name      = "animation.motion-sink." + std::to_string(++scriptMotionSinkSequence());
    auto              published = eve::ScriptProviderRegistry::instance().publish(name, sink);
    if (!published.ok()) throwMotionError(published.error());
    auto     adapter = std::make_unique<Adapter>(published.value());
    Adapter* raw     = adapter.get();
    store.providerNames.push_back(name);
    store.adapters.push_back(std::move(adapter));
    return raw;
}

/** @brief 清空一种形状的适配器与它们的 provider 登记。 */
template <class Adapter>
void clearScriptMotionSinkStore(ScriptMotionSinkStore<Adapter>& store) {
    eve::ScriptProviderRegistry& registry = eve::ScriptProviderRegistry::instance();
    for (const std::string& name : store.providerNames) {
        registry.unload(eve::ScriptProviderHandle{name, registry.generationOf(name)})
            .ignore("module teardown may already have dropped this motion sink provider");
    }
    store.providerNames.clear();
    store.adapters.clear();
}

}  // namespace

void clearScriptMotionSinkAdapters() {
    ScriptMotionSinkStores& stores = scriptMotionSinkStores();
    clearScriptMotionSinkStore(stores.floats);
    clearScriptMotionSinkStore(stores.vec2s);
    clearScriptMotionSinkStore(stores.vec3s);
    clearScriptMotionSinkStore(stores.colors);
    clearScriptMotionSinkStore(stores.quats);
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
        desc_.sink = adoptScriptMotionSink(scriptMotionSinkStores().floats, sink);
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

/**
 * @brief vec2/vec3/color/quat 共用的脚本 builder。
 *
 * 与 float 的 `ScriptMotionBuilder` 同名同义：`to(脚本对象)` 只绑不跑（给 sequence 用），
 * `bind(脚本对象)` 绑上并开跑，`run()` 不带 sink 跑。差别只在值怎么读/怎么编组（`Traits`）。
 * @remarks color/quat 的描述里没有 style/frequency/dampingRatio/seed，所以那四个方法对这两种形状
 *          **不注册**（脚本调用会明确报"没有这个方法"），而不是注册了再静默忽略。
 */
template <class Traits>
class ScriptMotionBuilderT {
public:
    using Desc  = typename Traits::Desc;
    using Value = typename Traits::Value;

    ScriptMotionBuilderT(Animation* animation, Value from, Value to, float duration) : animation_(animation) {
        if (!animation_) throw Exception("%s: Animation is null", Traits::kType);
        Traits::configure(desc_, from, to, duration);
    }

    void ease(std::string kind) { desc_.ease = std::move(kind); }
    void delay(float seconds) { desc_.delay = seconds; }
    void loops(int count, std::string mode = "restart") {
        desc_.loops    = count;
        desc_.loopMode = parseLoopMode(mode);
    }
    void cancelOnError(bool enabled) { desc_.cancelOnError = enabled; }
    void frequency(int count) {
        if constexpr (Traits::kHasStyle) desc_.frequency = count;
    }
    void dampingRatio(float ratio) {
        if constexpr (Traits::kHasStyle) desc_.dampingRatio = ratio;
    }
    void seed(int value) {
        if constexpr (Traits::kHasStyle) desc_.seed = static_cast<std::uint32_t>(value);
    }
    void stylePunch() {
        if constexpr (Traits::kHasStyle) desc_.style = MotionStyle::Punch;
    }
    void styleShake() {
        if constexpr (Traits::kHasStyle) desc_.style = MotionStyle::Shake;
    }

    void to(const ssq::Object& sink) {
        ensureLive();
        if (sink.getType() != ssq::Type::INSTANCE)
            throw Exception("%s.to: sink must be an instance implementing write(...)", Traits::kType);
        desc_.sink = adoptTraitsSink(sink);
    }

    ScriptMotionHandle* bind(const ssq::Object& sink) {
        to(sink);
        return run();
    }

    ScriptMotionHandle* run() {
        ensureLive();
        consumed_    = true;
        auto spawned = Traits::spawn(animation_->motions(), desc_);
        if (!spawned.ok()) throwMotionError(spawned.error());
        return new ScriptMotionHandle(&animation_->motions(), spawned.value());
    }

    [[nodiscard]] eve::Result<Desc> takeDesc() {
        ensureLive();
        consumed_ = true;
        return eve::Result<Desc>::success(std::move(desc_));
    }

private:
    using Adapter = ScriptMotionSink<Traits>;

    /** 按形状挑对应的模块级 store（保证适配器活到模块析构）。 */
    static ScriptMotionSinkStore<Adapter>& store() {
        if constexpr (std::is_same_v<Traits, MotionVec2Traits>) {
            return scriptMotionSinkStores().vec2s;
        } else if constexpr (std::is_same_v<Traits, MotionVec3Traits>) {
            return scriptMotionSinkStores().vec3s;
        } else if constexpr (std::is_same_v<Traits, MotionColorTraits>) {
            return scriptMotionSinkStores().colors;
        } else {
            return scriptMotionSinkStores().quats;
        }
    }

    static typename Traits::Sink* adoptTraitsSink(const ssq::Object& sink) {
        return adoptScriptMotionSink(store(), sink);
    }

    void ensureLive() {
        if (!animation_) throw Exception("%s: Animation is null", Traits::kType);
        if (consumed_) throw Exception("%s: builder already consumed (run/bind)", Traits::kType);
    }

    Animation* animation_ = nullptr;
    Desc       desc_{};
    bool       consumed_ = false;
};

/** @brief 注册一种非 float 形状的 builder 类（方法集按 `kHasStyle` 取舍）。 */
template <class Traits>
void exposeMotionTraitsBuilder(ssq::Table& table, const char* className) {
    using Builder = ScriptMotionBuilderT<Traits>;
    auto builder =
        table.addClass<Builder>(className, std::function<Builder*()>([]() -> Builder* { return nullptr; }), true);
    builder.addFunc("ease", &Builder::ease);
    builder.addFunc("delay", &Builder::delay);
    builder.addFunc("loops", &Builder::loops);
    builder.addFunc("cancelOnError", &Builder::cancelOnError);
    if constexpr (Traits::kHasStyle) {
        builder.addFunc("stylePunch", &Builder::stylePunch);
        builder.addFunc("styleShake", &Builder::styleShake);
        builder.addFunc("frequency", &Builder::frequency);
        builder.addFunc("dampingRatio", &Builder::dampingRatio);
        builder.addFunc("seed", &Builder::seed);
    }
    builder.addFunc("to", [](Builder* self, const ssq::Object& sink) {
        if (!self) throw Exception("%s.to: builder is null", Traits::kType);
        self->to(sink);
    });
    builder.addFunc("bind", [](Builder* self, const ssq::Object& sink) -> ScriptMotionHandle* {
        if (!self) throw Exception("%s.bind: builder is null", Traits::kType);
        return self->bind(sink);
    });
    builder.addFunc("run", &Builder::run);
}

/** @brief 注册 vec2/vec3/color/quat 四套 builder 与它们的 Animation 工厂。 */
void exposeNonFloatMotionBuilders(ssq::Table& table, ssq::Class& animationClass) {
    exposeMotionTraitsBuilder<MotionVec2Traits>(table, "MotionVec2Builder");
    exposeMotionTraitsBuilder<MotionVec3Traits>(table, "MotionVec3Builder");
    exposeMotionTraitsBuilder<MotionColorTraits>(table, "MotionColorBuilder");
    exposeMotionTraitsBuilder<MotionQuatTraits>(table, "MotionQuatBuilder");

    animationClass.addFunc("newMotionVec2",
                           [](Animation* self, const ssq::Object& from, const ssq::Object& to,
                              float duration) -> ScriptMotionBuilderT<MotionVec2Traits>* {
                               if (!self) throw Exception("newMotionVec2: Animation is null");
                               return new ScriptMotionBuilderT<MotionVec2Traits>(
                                   self, MotionVec2Traits::read(from, "newMotionVec2 'from'"),
                                   MotionVec2Traits::read(to, "newMotionVec2 'to'"), duration);
                           });
    animationClass.addFunc("newMotionVec3",
                           [](Animation* self, const ssq::Object& from, const ssq::Object& to,
                              float duration) -> ScriptMotionBuilderT<MotionVec3Traits>* {
                               if (!self) throw Exception("newMotionVec3: Animation is null");
                               return new ScriptMotionBuilderT<MotionVec3Traits>(
                                   self, MotionVec3Traits::read(from, "newMotionVec3 'from'"),
                                   MotionVec3Traits::read(to, "newMotionVec3 'to'"), duration);
                           });
    animationClass.addFunc("newMotionColor",
                           [](Animation* self, const ssq::Object& from, const ssq::Object& to,
                              float duration) -> ScriptMotionBuilderT<MotionColorTraits>* {
                               if (!self) throw Exception("newMotionColor: Animation is null");
                               return new ScriptMotionBuilderT<MotionColorTraits>(
                                   self, MotionColorTraits::read(from, "newMotionColor 'from'"),
                                   MotionColorTraits::read(to, "newMotionColor 'to'"), duration);
                           });
    animationClass.addFunc("newMotionQuat",
                           [](Animation* self, const ssq::Object& from, const ssq::Object& to,
                              float duration) -> ScriptMotionBuilderT<MotionQuatTraits>* {
                               if (!self) throw Exception("newMotionQuat: Animation is null");
                               return new ScriptMotionBuilderT<MotionQuatTraits>(
                                   self, MotionQuatTraits::read(from, "newMotionQuat 'from'"),
                                   MotionQuatTraits::read(to, "newMotionQuat 'to'"), duration);
                           });
}

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

    // 非 float 形状（vec2/vec3/color/quat）：builder + 脚本 sink，与 float 那套同形。
    exposeNonFloatMotionBuilders(table, animationClass);
}

}  // namespace eve::animation
