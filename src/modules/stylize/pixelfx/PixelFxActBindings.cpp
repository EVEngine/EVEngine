#include <algorithm>
#include <cmath>
#include <functional>
#include <simplesquirrel/simplesquirrel.hpp>
#include "common/BorrowedRef.h"
#include "common/Exception.h"
#include "common/SquirrelBinding.h"
#include "common/SquirrelOwnership.h"
#include "image/ImageData.h"
#include "stylize/pixelfx/ActRuntime.h"
#include "stylize/pixelfx/PixelFxBindings.h"
namespace eve::stylize {
namespace {
using namespace pixelfx;
struct ScriptActSession {
    std::shared_ptr<ActRenderSession> session = std::make_shared<ActRenderSession>();
};
struct ScriptActFrame {
    ActPreparedFrame      frame;
    int                   width, height, originX, originY, imageWidth, imageHeight;
    std::vector<uint32_t> color, light;
    PixelFxCanvas         canvas() { return {width, height, originX, originY, color, light}; }
};
Status rejected(const char* message, const char* path) {
    return Status::failure(Diagnostic(DiagnosticCode::InvalidArgument, Severity::Error, message, path));
}
struct CallbackFailure {
    Status status;
};
struct CallbackStack {
    HSQUIRRELVM vm;
    SQInteger   top;
    explicit CallbackStack(HSQUIRRELVM handle) : vm(handle), top(sq_gettop(handle)) {}
    ~CallbackStack() { sq_settop(vm, top); }
};
/** @brief Invoke a call-local observer on the owning VM thread, with owning payload copies.
 * @note The native method receiver remains rooted on the VM call stack; no callback is retained.
 * Script failures unwind native stepping before the hit is consumed, preserving source retry semantics.
 * @cost Copies one authored hit and event per synchronous hit invocation, without raster work. */
void observeHit(HSQUIRRELVM vm, const ssq::Object& observer, const Value& hit, const ActHitEvent& event) {
    CallbackStack stack(vm);
    auto          authored = script::pushValue(vm, hit);
    if (!authored) throw CallbackFailure{authored.status()};
    auto notification = script::pushValue(vm, Value::object({{"index", Value(int64_t(event.index))},
                                                             {"sourceTime", Value(event.sourceTime)},
                                                             {"actTime", Value(event.actTime)},
                                                             {"effectTime", Value(event.effectTime)}}));
    if (!notification) throw CallbackFailure{notification.status()};
    HSQOBJECT rawHit, rawEvent, root;
    sq_getstackobj(vm, -2, &rawHit);
    sq_getstackobj(vm, -1, &rawEvent);
    sq_pushroottable(vm);
    sq_getstackobj(vm, -1, &root);
    if (SQ_FAILED(script::callFunction(vm, observer.getRaw(), root, rawHit, rawEvent))) {
        throw CallbackFailure{Status::failure(
            Diagnostic(DiagnosticCode::CallbackFailure, Severity::Error,
                       "Act hit callback failed; the VM error was reported by callFunction", "/act/observer"))};
    }
}
template <class T>
ssq::Table own(HSQUIRRELVM vm, std::unique_ptr<T> value) {
    auto object = script::makeOwnedSquirrelInstance(vm, std::move(value));
    if (!object) return script::projectStatusResult(vm, object.status());
    return script::projectStatusResult(vm, Status::success(), std::move(object).takeValue());
}
template <class T>
ssq::Table token(HSQUIRRELVM vm, Result<T>&& value) {
    if (!value) return script::projectStatusResult(vm, value.status());
    return own(vm, std::make_unique<T>(std::move(value).takeValue()));
}
/** @brief Borrow one checked native argument only for the current VM call. */
template <class T>
ResultRef<T> argument(const ssq::Object& object, const char* path) {
    if (object.getType() == ssq::Type::INSTANCE && object.getTypeTag() == script::detail::squirrelTypeHash<T*>()) {
        try {
            if (auto* value = object.to<T*>()) return ResultRef<T>::success(std::ref(*value));
        } catch (const ssq::TypeException&) {
            return ResultRef<T>::failure(rejected("Malformed native argument", path));
        }
    }
    return ResultRef<T>::failure(rejected("Expected a live native argument of the requested type", path));
}
Result<void> images(image::ImageData* c, image::ImageData* l) {
    if (!c || !l || c == l || !c->getData() || !l->getData() || c->getData() == l->getData())
        return Result<void>::failure(rejected("Two disjoint RGBA8 images are required", "/images"));
    if (c->getFormat() != "RGBA8" || l->getFormat() != "RGBA8" || c->getWidth() < 1 || c->getHeight() < 1 ||
        c->getWidth() > 4096 || c->getHeight() > 4096 || c->getWidth() != l->getWidth() ||
        c->getHeight() != l->getHeight())
        return Result<void>::failure(rejected("Expected matching bounded RGBA8 images", "/images"));
    return Result<void>::success();
}
std::vector<uint32_t> readImage(image::ImageData& image, int width, int height) {
    std::vector<uint32_t> out(size_t(width) * size_t(height));
    const auto*           bytes = static_cast<const uint8_t*>(image.getData());
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            const size_t i      = size_t(y) * size_t(width) + size_t(x),
                         source = (size_t(y) * size_t(image.getWidth()) + size_t(x)) * 4;
            for (int c = 0; c < 4; ++c) out[i] |= uint32_t(bytes[source + size_t(c)]) << (c * 8);
        }
    return out;
}
void writeImage(std::span<const uint32_t> values, image::ImageData& image, int width, int height) {
    auto* bytes = static_cast<uint8_t*>(image.getData());
    std::fill(bytes, bytes + image.getSize(), uint8_t(0));
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            const size_t i      = size_t(y) * size_t(width) + size_t(x),
                         target = (size_t(y) * size_t(image.getWidth()) + size_t(x)) * 4;
            for (int c = 0; c < 4; ++c) bytes[target + size_t(c)] = uint8_t(values[i] >> (c * 8));
        }
}
/** @brief Copy source timing to an owning script value, without clock or cache mutation.
 * @cost Copies authored keys/hits once per metadata inspection; no raster or simulation work. */
Value timingValue(std::optional<ActTimingView> timing) {
    if (!timing) return Value();
    Value::Array                         targets, hits, poses;
    constexpr std::array<const char*, 9> skins{"", "flash", "char", "ice", "shadow", "holy", "red", "shock", "poison"};
    for (double x : timing->targets) targets.emplace_back(x);
    for (const auto& hit : timing->hits) {
        Value::Object fields{{"t", Value(hit.time)},
                             {"freeze", Value(hit.freeze)},
                             {"shake", Value(hit.shake)},
                             {"amp", Value(hit.amp)},
                             {"push", Value(hit.push)}};
        if (hit.skin)
            fields.emplace("skin", *hit.skin == ActTargetSkin::None ? Value() : Value(skins[size_t(*hit.skin)]));
        if (hit.at) fields.emplace("at", Value(*hit.at));
        hits.push_back(Value::object(std::move(fields)));
    }
    const auto names = rig::poseFieldNames();
    for (const auto& key : timing->poses) {
        auto          encoded = rig::encodePose(key.pose.pose);
        Value::Object fields;
        for (size_t i = 0; i < names.size(); ++i)
            if (key.pose.contains(rig::PoseField(i)))
                fields.emplace(std::string(names[i]), *encoded.find("fields")->find(std::string(names[i])));
        poses.push_back(Value::object(
            {{"t", Value(key.time)}, {"how", Value(key.how)}, {"pose", Value::object(std::move(fields))}}));
    }
    return Value::object({{"windup", Value(timing->windup)},
                          {"recovery", Value(timing->recovery)},
                          {"onBody", Value(timing->onBody)},
                          {"targets", Value::array(std::move(targets))},
                          {"hits", Value::array(std::move(hits))},
                          {"poses", Value::array(std::move(poses))}});
}
Value stateValue(const ActRuntime& act) {
    const auto   s = act.snapshot();
    Value::Array presence, hits;
    const auto   names = rig::poseFieldNames();
    for (size_t i = 0; i < names.size(); ++i)
        if (s.pose.contains(rig::PoseField(i))) presence.emplace_back(std::string(names[i]));
    for (const auto& h : act.hitEvents())
        hits.push_back(Value::object({{"index", Value(int64_t(h.index))},
                                      {"sourceTime", Value(h.sourceTime)},
                                      {"actTime", Value(h.actTime)},
                                      {"effectTime", Value(h.effectTime)}}));
    return Value::object({{"schema", Value("eve.pixelfx.act-snapshot")},
                          {"version", Value(int64_t(1))},
                          {"id", Value(std::string(act.info().id))},
                          {"wallTime", Value(s.wallTime)},
                          {"actTime", Value(s.actTime)},
                          {"effectTime", Value(s.effectTime)},
                          {"frozen", Value(s.frozen)},
                          {"freezeRemaining", Value(s.freezeRemaining)},
                          {"shakeRemaining", Value(s.shakeRemaining)},
                          {"stopped", Value(s.stopped)},
                          {"done", Value(s.done)},
                          {"hasAct", Value(s.hasAct)},
                          {"loops", Value(int64_t(s.loops))},
                          {"shake", Value::array({Value(int64_t(s.shake[0])), Value(int64_t(s.shake[1]))})},
                          {"pose", rig::encodePose(s.pose.pose)},
                          {"presence", Value::array(std::move(presence))},
                          {"hits", Value::array(std::move(hits))}});
}
/** @brief Cold image/frame preparation at the script boundary; no image or VM references survive.
 * @cost Copies background planes and performs native body/effect composition per explicit call. */
ssq::Table prepareFrame(HSQUIRRELVM vm, ActRuntime& act, image::ImageData* c, image::ImageData* l,
                        const ssq::Object& options) {
    auto valid = images(c, l);
    if (!valid) return script::projectStatusResult(vm, valid.status());
    auto parsed = script::valueFromSquirrel(options);
    if (!parsed) return script::projectStatusResult(vm, parsed.status());
    const auto& o      = parsed.value();
    const auto* fields = o.getIf<Value::Object>();
    if (!fields) return script::projectStatusResult(vm, rejected("Expected frame options table", "/frame"));
    int    width = c->getWidth(), height = c->getHeight();
    double x = 0, y = 0;
    int    ox = 0, oy = 0;
    bool   flip = false, targets = false;
    auto   body = ActBodyPresentation::SourceRig;
    for (const auto& [key, value] : *fields) {
        if (key == "width" || key == "height") {
            const int maximum = key == "width" ? c->getWidth() : c->getHeight();
            if (!value.isInt64() || value.asInt() < 1 || value.asInt() > maximum)
                return script::projectStatusResult(
                    vm, rejected("Logical extent must fit the background images", "/frame/extent"));
            (key == "width" ? width : height) = int(value.asInt());
        } else if (key == "originX" || key == "originY") {
            if (!value.isInt64() || value.asInt() < -4096 || value.asInt() > 4096)
                return script::projectStatusResult(vm, rejected("Expected bounded integer origin", "/frame/origin"));
            (key == "originX" ? ox : oy) = int(value.asInt());
        } else if (key == "x" || key == "y") {
            if (!value.isInt64() && !value.isDouble())
                return script::projectStatusResult(vm, rejected("Expected numeric placement", "/frame/placement"));
            const double n = value.isInt64() ? double(value.asInt()) : value.asDouble();
            if (!std::isfinite(n) || std::abs(n) > 4096)
                return script::projectStatusResult(vm, rejected("Placement is out of bounds", "/frame/placement"));
            (key == "x" ? x : y) = n;
        } else if (key == "flip" || key == "targets") {
            if (!value.isBool())
                return script::projectStatusResult(vm, rejected("Expected boolean frame option", "/frame"));
            (key == "flip" ? flip : targets) = value.asBool();
        } else if (key == "body") {
            if (!value.isString() ||
                (value.asString() != "source" && value.asString() != "actor" && value.asString() != "none"))
                return script::projectStatusResult(vm, rejected("Body must be source, actor or none", "/frame/body"));
            body = value.asString() == "none"    ? ActBodyPresentation::EffectsOnly
                   : value.asString() == "actor" ? ActBodyPresentation::InputActor
                                                 : ActBodyPresentation::SourceRig;
        } else
            return script::projectStatusResult(vm, rejected("Unknown frame option", "/frame"));
    }
    auto color = readImage(*c, width, height), light = readImage(*l, width, height);
    auto frame = act.prepareFrame({width, height, ox, oy, color, light}, {x, y, 0, flip}, targets, body);
    if (!frame) return script::projectStatusResult(vm, frame.status());
    return own(vm, std::make_unique<ScriptActFrame>(ScriptActFrame{std::move(frame).takeValue(), width, height, ox, oy,
                                                                   c->getWidth(), c->getHeight(), std::move(color),
                                                                   std::move(light)}));
}
}  // namespace
void exposePixelFxAct(ssq::Table& table) {
    const auto vm      = table.getHandle();
    auto       session = table.addClass<ScriptActSession>(
        "PixelFxActSession", std::function<ScriptActSession*()>([] { return new ScriptActSession; }), true);
    auto candidate = table.addClass<ActPreparedRuntime>(
        "PixelFxActPrepared", std::function<ActPreparedRuntime*()>([]() -> ActPreparedRuntime* {
            throw Exception("Use PixelFxActSession.prepare");
        }),
        true);
    auto actor = table.addClass<ActRuntime>(
        "PixelFxAct",
        std::function<ActRuntime*()>([]() -> ActRuntime* { throw Exception("Use PixelFxActPrepared.publish"); }), true);
    auto frame = table.addClass<ScriptActFrame>(
        "PixelFxActFrame",
        std::function<ScriptActFrame*()>([]() -> ScriptActFrame* { throw Exception("Use PixelFxAct.prepareFrame"); }),
        true);
    auto bounds = table.addClass<ActPreparedBounds>("PixelFxActBounds",
                                                    std::function<ActPreparedBounds*()>([]() -> ActPreparedBounds* {
                                                        throw Exception("Use PixelFxAct.prepareBounds");
                                                    }),
                                                    true);
    bounds.addFunc("getValue", [vm](ActPreparedBounds* self) -> ssq::Table {
        const auto b = self->bounds();
        return script::projectResult(
            vm,
            Result<Value>::success(
                Value::object({{"x0", Value(b.x0)}, {"y0", Value(b.y0)}, {"x1", Value(b.x1)}, {"y1", Value(b.y1)}})),
            [](Value v) { return v; });
    });
    session.addFunc("prepare", [vm](ScriptActSession* self, const ssq::Object& effect) -> ssq::Table {
        auto owner = argument<PixelFx>(effect, "/effect");
        if (!owner) return script::projectStatusResult(vm, owner.status());
        auto& fx = owner.value().get();
        return token(vm, ActRuntime::prepareInSession(fx.info().kind, fx.settings(), self->session));
    });
    // Cold metadata discovery reads the sole native catalogue and constructs no effect programs.
    session.addFunc("getCatalogue", [vm](ScriptActSession*) -> ssq::Table {
        Value::Array items;
        for (const auto& info : PixelFx::catalogue())
            items.push_back(Value::object({{"id", Value(std::string(info.id))},
                                           {"width", Value(int64_t(info.width))},
                                           {"height", Value(int64_t(info.height))},
                                           {"originX", Value(int64_t(info.originX))},
                                           {"originY", Value(int64_t(info.originY))},
                                           {"duration", Value(info.duration)},
                                           {"loop", Value(info.loop)}}));
        return script::projectResult(
            vm,
            Result<Value>::success(Value::object({{"schema", Value("eve.pixelfx.catalogue")},
                                                  {"version", Value(int64_t(1))},
                                                  {"effects", Value::array(std::move(items))}})),
            [](Value v) { return v; });
    });
    session.addFunc("getStatistics", [vm](ScriptActSession* self) -> ssq::Table {
        const auto s = self->session->cacheStats();
        return script::projectResult(
            vm,
            Result<Value>::success(Value::object({{"entries", Value(int64_t(s.entries))},
                                                  {"hits", Value(int64_t(s.hits))},
                                                  {"rasterEvaluations", Value(int64_t(s.rasterEvaluations))},
                                                  {"clears", Value(int64_t(s.clears))}})),
            [](Value v) { return v; });
    });
    candidate.addFunc("publish", [vm](ActPreparedRuntime* self) -> ssq::Table {
        auto ready = ActRuntime::publish(std::move(*self));
        if (!ready) return script::projectStatusResult(vm, ready.status());
        return own(vm, std::move(ready).takeValue());
    });
    actor.addFunc("step", [vm](ActRuntime* self, const ssq::Object& delta) -> ssq::Table {
        if (delta.getType() != ssq::Type::INTEGER && delta.getType() != ssq::Type::FLOAT)
            return script::projectStatusResult(vm, rejected("Expected numeric Act delta", "/delta"));
        return script::projectResult(
            vm, self->step(delta.getType() == ssq::Type::INTEGER ? double(delta.toInt()) : double(delta.toFloat())));
    });
    actor.addFunc("stop", [](ActRuntime* self) { self->stop(); });
    actor.addFunc("prepareReplay", [vm](ActRuntime* self, const ssq::Object& history) -> ssq::Table {
        auto parsed = script::valueFromSquirrel(history);
        if (!parsed) return script::projectStatusResult(vm, parsed.status());
        const auto* records = parsed.value().getIf<Value::Array>();
        if (!records) return script::projectStatusResult(vm, rejected("Expected replay command array", "/act/replay"));
        std::vector<ActReplayCommand> commands;
        commands.reserve(records->size());
        for (const auto& record : *records) {
            const auto* fields = record.getIf<Value::Object>();
            const auto* op     = record.find("op");
            if (!fields || !op || !op->isString())
                return script::projectStatusResult(vm, rejected("Expected replay operation", "/act/replay/operation"));
            if (op->asString() == "stop") {
                if (fields->size() != 1)
                    return script::projectStatusResult(vm, rejected("Stop takes no fields except op", "/act/replay"));
                commands.push_back({ActReplayOperation::Stop, 0});
            } else if (op->asString() == "step") {
                const auto* dt = record.find("delta");
                if (fields->size() != 2 || !dt || (!dt->isInt64() && !dt->isDouble()))
                    return script::projectStatusResult(
                        vm, rejected("Step requires only op and numeric delta", "/act/replay"));
                commands.push_back({ActReplayOperation::Step, dt->isInt64() ? double(dt->asInt()) : dt->asDouble()});
            } else
                return script::projectStatusResult(vm, rejected("Unknown replay operation", "/act/replay/operation"));
        }
        return token(vm, self->prepareReplay(commands));
    });
    actor.addFunc(
        "stepObserved", [vm](ActRuntime* self, const ssq::Object& delta, const ssq::Object& observer) -> ssq::Table {
            if (delta.getType() != ssq::Type::INTEGER && delta.getType() != ssq::Type::FLOAT)
                return script::projectStatusResult(vm, rejected("Expected numeric Act delta", "/delta"));
            const auto type = sq_type(observer.getRaw());
            if (type != OT_CLOSURE && type != OT_NATIVECLOSURE)
                return script::projectStatusResult(vm, rejected("Expected synchronous hit closure", "/act/observer"));
            try {
                return script::projectResult(
                    vm, self->step(
                            delta.getType() == ssq::Type::INTEGER ? double(delta.toInt()) : double(delta.toFloat()),
                            [&](const Value& hit, const ActHitEvent& event) { observeHit(vm, observer, hit, event); }));
            } catch (const CallbackFailure& failure) {
                return script::projectStatusResult(vm, failure.status);
            }
        });
    actor.addFunc("getSnapshot", [vm](ActRuntime* self) -> ssq::Table {
        return script::projectResult(vm, Result<Value>::success(stateValue(*self)), [](Value v) { return v; });
    });
    actor.addFunc("getTiming", [vm](ActRuntime* self) -> ssq::Table {
        auto value = timingValue(self->timing());
        if (!value.isNull()) {
            value.set("schema", Value("eve.pixelfx.act-timing"));
            value.set("version", Value(int64_t(1)));
        }
        return script::projectResult(vm, Result<Value>::success(std::move(value)), [](Value v) { return v; });
    });
    actor.addFunc("prepareBounds", [vm](ActRuntime* self) -> ssq::Table { return token(vm, self->prepareBounds()); });
    actor.addFunc("prepareFrame",
                  [vm](ActRuntime* self, const ssq::Object& color, const ssq::Object& light,
                       const ssq::Object& options) -> ssq::Table {
                      auto c = argument<image::ImageData>(color, "/images/color");
                      if (!c) return script::projectStatusResult(vm, c.status());
                      auto l = argument<image::ImageData>(light, "/images/light");
                      if (!l) return script::projectStatusResult(vm, l.status());
                      return prepareFrame(vm, *self, &c.value().get(), &l.value().get(), options);
                  });
    frame.addFunc("copyTo",
                  [vm](ScriptActFrame* self, const ssq::Object& color, const ssq::Object& light) -> ssq::Table {
                      auto cRef = argument<image::ImageData>(color, "/images/color");
                      if (!cRef) return script::projectStatusResult(vm, cRef.status());
                      auto lRef = argument<image::ImageData>(light, "/images/light");
                      if (!lRef) return script::projectStatusResult(vm, lRef.status());
                      auto* c     = &cRef.value().get();
                      auto* l     = &lRef.value().get();
                      auto  valid = images(c, l);
                      if (!valid) return script::projectStatusResult(vm, valid.status());
                      if (c->getWidth() != self->imageWidth || c->getHeight() != self->imageHeight)
                          return script::projectStatusResult(vm, rejected("Frame image dimensions changed", "/images"));
                      auto copied = self->frame.copyTo(self->canvas());
                      if (!copied) return script::projectStatusResult(vm, copied.status());
                      writeImage(self->color, *c, self->width, self->height);
                      writeImage(self->light, *l, self->width, self->height);
                      return script::projectStatusResult(vm, Status::success());
                  });
}
}  // namespace eve::stylize
