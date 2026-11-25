#include "stylize/pixelfx/PixelFxBindings.h"
#include <cmath>
#include <cstring>
#include <simplesquirrel/simplesquirrel.hpp>
#include <string>
#include <type_traits>
#include "common/Exception.h"
#include "common/SquirrelBinding.h"
#include "common/SquirrelOwnership.h"
#include "image/ImageData.h"
#include "stylize/PixelFx.h"
#include "stylize/pixelfx/Descriptor.h"
#include "stylize/pixelfx/Rig.h"

namespace eve::stylize {
namespace {
template <class M>
struct MemberValue;
template <class T, class Owner>
struct MemberValue<T Owner::*> {
    using Type = T;
};
// VM values are synchronous borrowed boundary input; no VM object is retained.
double scriptNumber(const ssq::Object& value) {
    if (value.getType() == ssq::Type::INTEGER) return double(value.toInt());
    if (value.getType() == ssq::Type::FLOAT) return double(value.toFloat());
    throw eve::Exception("PixelFx: expected an integer or floating-point number");
}
script::SquirrelValueOptions configurationLimits() {
    script::SquirrelValueOptions limits;
    // Two 256x256 actor planes, bounded eye cells and all active material ramps fit this budget.
    limits.maxElements = 140000;
    limits.source      = "stylize.pixelfx.configuration";
    return limits;
}
ssq::Table projectConfiguration(HSQUIRRELVM vm, const Value& value) {
    const SQInteger top    = sq_gettop(vm);
    auto            pushed = script::pushValue(vm, value, configurationLimits());
    if (!pushed) {
        sq_settop(vm, top);
        return script::projectStatusResult(vm, pushed.status());
    }
    ssq::Object object(vm);
    if (SQ_FAILED(sq_getstackobj(vm, -1, &object.getRaw()))) {
        sq_settop(vm, top);
        return script::projectStatusResult(
            vm, Status::failure(Diagnostic(DiagnosticCode::InvalidArgument, Severity::Error,
                                           "Unable to root configuration payload", "/configuration")));
    }
    sq_addref(vm, &object.getRaw());
    sq_settop(vm, top);
    return script::projectStatusResult(vm, Status::success(), std::move(object));
}
void applyConfiguration(PixelFx& self, const Value& value) {
    if (value == self.configurationValue()) return;  // Equal compatibility setters do not rebuild the backend.
    auto prepared = PixelFx::prepareConfigurationValue(self.info().kind, value);
    if (!prepared) throw eve::Exception("%s", prepared.status().describe().c_str());
    auto applied = self.commitConfiguration(std::move(prepared).takeValue());
    if (!applied) throw eve::Exception("%s", applied.status().describe().c_str());
}
void setRampColor(PixelFx& self, const std::string& name, int index, int r, int g, int b) {
    if (r < 0 || r > 255 || g < 0 || g > 255 || b < 0 || b > 255)
        throw eve::Exception("PixelFx: RGB bytes must be in [0,255]");
    bool        palette    = false;
    const auto& descriptor = pixelfx::descriptorFor(self.info().kind);
    for (auto options : {pixelfx::commonOptions(), descriptor.options})
        for (const auto& option : options)
            if (option.name == name)
                palette = std::visit(
                    [](auto member) {
                        using T = typename MemberValue<decltype(member)>::Type;
                        return std::is_same_v<T, std::vector<uint32_t>> || std::is_same_v<T, std::vector<int64_t>>;
                    },
                    option.member);
    auto  config = self.configurationValue();
    auto* ramp   = config.find("options")->find(name);
    if (!palette || !ramp || !ramp->isArray() || index < 0 || size_t(index) >= ramp->arraySize())
        throw eve::Exception("PixelFx: invalid active palette/index");
    ramp->at(size_t(index)) = Value(int64_t(0xff000000u | (uint32_t(b) << 16) | (uint32_t(g) << 8) | uint32_t(r)));
    applyConfiguration(self, config);
}
Result<void> validateImages(const PixelFx& self, const image::ImageData* color, const image::ImageData* light) {
    if (!color || !light || color == light)
        return Result<void>::failure(Diagnostic(DiagnosticCode::InvalidArgument, Severity::Error,
                                                "Two distinct images are required", "/images"));
    const auto info = self.info();
    for (const auto* value : {color, light})
        if (value->getWidth() != info.width || value->getHeight() != info.height || value->getFormat() != "RGBA8")
            return Result<void>::failure(Diagnostic(DiagnosticCode::InvalidArgument, Severity::Error,
                                                    "Image dimensions/format must match the effect RGBA8 planes",
                                                    "/images"));
    return Result<void>::success();
}
void copyImages(const PixelFx& self, image::ImageData& color, image::ImageData& light) {
    const auto copy = [](std::span<const uint32_t> pixels, image::ImageData& target) {
        auto* bytes = static_cast<uint8_t*>(target.getData());
        for (size_t i = 0; i < pixels.size(); ++i)
            for (int shift = 0; shift < 4; ++shift)
                bytes[i * 4 + size_t(shift)] = uint8_t((pixels[i] >> (shift * 8)) & 255u);
    };
    copy(self.color(), color);
    copy(self.light(), light);
}
Result<void> validateRigImage(const image::ImageData* target, const ssq::Object& x, const ssq::Object& y) {
    if (!target || target->getFormat() != "RGBA8" || target->getWidth() <= 0 || target->getHeight() <= 0 ||
        target->getWidth() > 2048 || target->getHeight() > 2048)
        return Result<void>::failure(Diagnostic(DiagnosticCode::InvalidArgument, Severity::Error,
                                                "Expected a bounded RGBA8 rig target", "/target"));
    if (x.getType() != ssq::Type::INTEGER || y.getType() != ssq::Type::INTEGER || x.toInt() < -4096 ||
        x.toInt() > 4096 || y.toInt() < -4096 || y.toInt() > 4096)
        return Result<void>::failure(Diagnostic(DiagnosticCode::InvalidArgument, Severity::Error,
                                                "Rig origin must contain bounded integers", "/origin"));
    return Result<void>::success();
}
Result<Value> renderRigImage(pixelfx::rig::Rasterizer& self, const pixelfx::rig::Pose& pose, image::ImageData& target,
                             int originX, int originY) {
    auto rendered = self.render(pose);
    if (!rendered) return Result<Value>::failure(rendered.status());
    const auto& pixels = rendered.value();
    const int   width = target.getWidth(), height = target.getHeight();
    auto*       bytes = static_cast<uint8_t*>(target.getData());
    std::memset(bytes, 0, size_t(width) * size_t(height) * 4);
    for (size_t i = 0; i < pixels.color.size(); ++i) {
        const int x = int(pixels.xs[i]) + originX, y = int(pixels.ys[i]) + originY;
        if (x < 0 || y < 0 || x >= width || y >= height) continue;
        const size_t offset = (size_t(y) * size_t(width) + size_t(x)) * 4;
        for (int c = 0; c < 4; ++c) bytes[offset + size_t(c)] = uint8_t((pixels.color[i] >> (c * 8)) & 255u);
    }
    return Result<Value>::success(Value::object({{"pixelCount", Value(int64_t(pixels.color.size()))},
                                                 {"x0", Value(int64_t(pixels.x0))},
                                                 {"y0", Value(int64_t(pixels.y0))},
                                                 {"x1", Value(int64_t(pixels.x1))},
                                                 {"y1", Value(int64_t(pixels.y1))}}));
}
/** @brief Project one owning seek candidate through the common Result boundary.
 * @cost Reuses native preparation and its replay budget once per explicit cold seek.
 * @note Synchronous owner-thread borrow; no VM references enter the native candidate. */
ssq::Table prepareSeekResult(HSQUIRRELVM vm, PixelFx& self, double seconds) {
    auto prepared = self.prepareSeek(seconds);
    if (!prepared) return script::projectStatusResult(vm, prepared.status());
    auto object =
        script::makeOwnedSquirrelInstance(vm, std::make_unique<PixelFxPreparedSeek>(std::move(prepared).takeValue()));
    if (!object) return script::projectStatusResult(vm, object.status());
    return script::projectStatusResult(vm, Status::success(), std::move(object).takeValue());
}
}  // namespace
void exposePixelFx(ssq::Table& table) {
    exposePixelFxAct(table);
    const auto vm = table.getHandle();
    table.addClass<PixelFxPreparedConfig>(
        "PixelFxPreparedConfig", std::function<PixelFxPreparedConfig*()>([]() -> PixelFxPreparedConfig* {
            throw eve::Exception("Use PixelFx.prepareConfiguration to create an owned candidate");
        }),
        true);
    table.addClass<PixelFxPreparedSeek>("PixelFxPreparedSeek",
                                        std::function<PixelFxPreparedSeek*()>([]() -> PixelFxPreparedSeek* {
                                            throw eve::Exception("Use PixelFx.prepareSeek to create an owned seek");
                                        }),
                                        true);
    auto rigClass = table.addClass<pixelfx::rig::Rasterizer>(
        "PixelFxRig", std::function<pixelfx::rig::Rasterizer*()>([]() { return new pixelfx::rig::Rasterizer(); }),
        true);
    auto trackClass = table.addClass<pixelfx::rig::PoseTrack>(
        "PixelFxPoseTrack", std::function<pixelfx::rig::PoseTrack*()>([]() -> pixelfx::rig::PoseTrack* {
            throw eve::Exception("Use PixelFxRig.prepareTrack to create an owned track");
        }),
        true);
    trackClass.addFunc("sample", [vm](pixelfx::rig::PoseTrack* self, const ssq::Object& time) -> ssq::Table {
        if (time.getType() != ssq::Type::INTEGER && time.getType() != ssq::Type::FLOAT)
            return script::projectStatusResult(
                vm, Status::failure(Diagnostic(DiagnosticCode::InvalidArgument, Severity::Error,
                                               "Expected numeric track time", "/track/time")));
        return script::projectResult(vm, self->sample(scriptNumber(time)),
                                     [](pixelfx::rig::Pose pose) { return pixelfx::rig::encodePose(pose); });
    });
    trackClass.addFunc("isHeld", [vm](pixelfx::rig::PoseTrack* self, const ssq::Object& time) -> ssq::Table {
        if (time.getType() != ssq::Type::INTEGER && time.getType() != ssq::Type::FLOAT)
            return script::projectStatusResult(
                vm, Status::failure(Diagnostic(DiagnosticCode::InvalidArgument, Severity::Error,
                                               "Expected numeric track time", "/track/time")));
        return script::projectResult(vm, self->isHeld(scriptNumber(time)), [](bool held) { return Value(held); });
    });
    rigClass.addFunc("renderTo",
                     [vm](pixelfx::rig::Rasterizer* self, const ssq::Object& input, image::ImageData* target,
                          const ssq::Object& x, const ssq::Object& y) -> ssq::Table {
                         auto valid = validateRigImage(target, x, y);
                         if (!valid) return script::projectStatusResult(vm, valid.status());
                         auto value = script::valueFromSquirrel(input);
                         if (!value) return script::projectStatusResult(vm, value.status());
                         auto pose = pixelfx::rig::decodePose(value.value());
                         if (!pose) return script::projectStatusResult(vm, pose.status());
                         return script::projectResult(
                             vm, renderRigImage(*self, pose.value(), *target, int(x.toInt()), int(y.toInt())),
                             [](Value value) { return value; });
                     });
    rigClass.addFunc("prepareTrack", [vm](pixelfx::rig::Rasterizer*, const ssq::Object& input) -> ssq::Table {
        auto limits        = configurationLimits();
        limits.maxElements = 200000;
        limits.source      = "stylize.pixelfx.pose-track";
        auto value         = script::valueFromSquirrel(input, limits);
        if (!value) return script::projectStatusResult(vm, value.status());
        auto prepared = pixelfx::rig::PoseTrack::prepare(value.value());
        if (!prepared) return script::projectStatusResult(vm, prepared.status());
        auto object = script::makeOwnedSquirrelInstance(
            vm, std::make_unique<pixelfx::rig::PoseTrack>(std::move(prepared).takeValue()));
        if (!object) return script::projectStatusResult(vm, object.status());
        return script::projectStatusResult(vm, Status::success(), std::move(object).takeValue());
    });
    rigClass.addFunc("renderTrackTo",
                     [vm](pixelfx::rig::Rasterizer* self, pixelfx::rig::PoseTrack* track, const ssq::Object& time,
                          image::ImageData* target, const ssq::Object& x, const ssq::Object& y) -> ssq::Table {
                         auto valid = validateRigImage(target, x, y);
                         if (!valid) return script::projectStatusResult(vm, valid.status());
                         if (!track)
                             return script::projectStatusResult(
                                 vm, Status::failure(Diagnostic(DiagnosticCode::InvalidArgument, Severity::Error,
                                                                "Expected an owned pose track", "/track")));
                         if (time.getType() != ssq::Type::INTEGER && time.getType() != ssq::Type::FLOAT)
                             return script::projectStatusResult(
                                 vm, Status::failure(Diagnostic(DiagnosticCode::InvalidArgument, Severity::Error,
                                                                "Expected numeric track time", "/track/time")));
                         auto pose = track->sample(scriptNumber(time));
                         if (!pose) return script::projectStatusResult(vm, pose.status());
                         // Floor motion is source-rounded separately from 12-FPS limb evaluation.
                         const int originX = int(x.toInt()) + int(std::floor(pose.value().x + .5)),
                                   originY = int(y.toInt()) + int(std::floor(pose.value().y + .5));
                         return script::projectResult(vm,
                                                      renderRigImage(*self, pose.value(), *target, originX, originY),
                                                      [](Value value) { return value; });
                     });
    rigClass.addFunc("getSkeleton", [vm](pixelfx::rig::Rasterizer*, const ssq::Object& input) -> ssq::Table {
        auto value = script::valueFromSquirrel(input);
        if (!value) return script::projectStatusResult(vm, value.status());
        auto pose = pixelfx::rig::decodePose(value.value());
        if (!pose) return script::projectStatusResult(vm, pose.status());
        auto solved = pixelfx::rig::skeleton(pose.value());
        if (!solved) return script::projectStatusResult(vm, solved.status());
        Value::Object joints;
        size_t        i = 0;
        for (auto name : pixelfx::rig::jointNames()) {
            const auto& p = solved.value().joints[i++];
            joints.emplace(std::string(name), Value::array({Value(p[0]), Value(p[1]), Value(p[2])}));
        }
        return script::projectStatusResult(vm, Status::success(), Value::object(std::move(joints)));
    });
    auto cls = table.addClass<PixelFx>("PixelFx", std::function<PixelFx*(std::string)>([](std::string id) {
                                           for (const auto& i : PixelFx::catalogue())
                                               if (i.id == id) return new PixelFx(i.kind);
                                           throw eve::Exception("PixelFx: unsupported effect id %s", id.c_str());
                                       }),
                                       true);
    cls.addFunc("getWidth", [](PixelFx* self) -> int { return self->info().width; });
    cls.addFunc("getHeight", [](PixelFx* self) -> int { return self->info().height; });
    cls.addFunc("getOriginX", [](PixelFx* self) -> int { return self->info().originX; });
    cls.addFunc("getOriginY", [](PixelFx* self) -> int { return self->info().originY; });
    cls.addFunc("getDuration", [](PixelFx* self) -> float { return float(self->info().duration); });
    cls.addFunc("getDurationTicks",
                [](PixelFx* self) -> int64_t { return int64_t(std::ceil(self->info().duration * 120 - 1e-9)); });
    cls.addFunc("isLoop", [](PixelFx* self) -> bool { return self->info().loop; });
    cls.addFunc("isSimulated", [](PixelFx* self) -> bool { return self->isSimulated(); });
    cls.addFunc("isDone", [](PixelFx* self) -> bool { return self->done(); });
    cls.addFunc("getActiveTickCount", [](PixelFx* self) -> int64_t { return int64_t(self->activeTickCount()); });
    cls.addFunc("isActorVisible",
                [](PixelFx* self) -> bool { return self->actorVisibility() == PixelFxActorVisibility::Visible; });
    cls.addFunc("getEffectTime", [](PixelFx* self) -> float { return float(self->effectTime()); });
    cls.addFunc("stop", [](PixelFx* self) -> void { self->stop(); });
    cls.addFunc("advance", [vm](PixelFx* self, const ssq::Object& delta) -> ssq::Table {
        if (delta.getType() != ssq::Type::INTEGER && delta.getType() != ssq::Type::FLOAT)
            return script::projectStatusResult(
                vm, Status::failure(Diagnostic(DiagnosticCode::InvalidArgument, Severity::Error,
                                               "Expected numeric deltaSeconds", "/deltaSeconds")));
        return script::projectResult(vm, self->advance(scriptNumber(delta)));
    });
    cls.addFunc("prepareSeek", [vm](PixelFx* self, const ssq::Object& time) -> ssq::Table {
        if (time.getType() != ssq::Type::INTEGER && time.getType() != ssq::Type::FLOAT)
            return script::projectStatusResult(
                vm, Status::failure(Diagnostic(DiagnosticCode::InvalidArgument, Severity::Error,
                                               "Expected numeric effectSeconds", "/effectSeconds")));
        return prepareSeekResult(vm, *self, scriptNumber(time));
    });
    cls.addFunc("prepareSeekTicks", [vm](PixelFx* self, const ssq::Object& ticks) -> ssq::Table {
        if (ticks.getType() != ssq::Type::INTEGER || ticks.toInt() < 0 || ticks.toInt() > 120000000000000LL)
            return script::projectStatusResult(
                vm, Status::failure(Diagnostic(DiagnosticCode::InvalidArgument, Severity::Error,
                                               "Expected integer effectTicks in [0,120000000000000]", "/effectTicks")));
        return prepareSeekResult(vm, *self, double(ticks.toInt()) / 120);
    });
    cls.addFunc("commitSeek", [vm](PixelFx* self, PixelFxPreparedSeek* candidate) -> ssq::Table {
        if (!candidate)
            return script::projectStatusResult(
                vm, Status::failure(Diagnostic(DiagnosticCode::InvalidArgument, Severity::Error,
                                               "Expected an owned seek candidate", "/candidate")));
        return script::projectResult(vm, self->commitSeek(std::move(*candidate)));
    });
    cls.addFunc("drawTo", [vm](PixelFx* self, image::ImageData* color, image::ImageData* light) -> ssq::Table {
        // VM pointers are borrowed only during this synchronous adapter call.
        auto valid = validateImages(*self, color, light);
        if (!valid) return script::projectStatusResult(vm, valid.status());
        self->draw();
        copyImages(*self, *color, *light);
        return script::projectResult(vm, Result<void>::success());
    });
    cls.addFunc(
        "drawLayerTo",
        [vm](PixelFx* self, image::ImageData* color, image::ImageData* light, const ssq::Object& layer) -> ssq::Table {
            auto valid = validateImages(*self, color, light);
            if (!valid) return script::projectStatusResult(vm, valid.status());
            if (layer.getType() != ssq::Type::INTEGER || layer.toInt() < 0 || layer.toInt() > 2)
                return script::projectStatusResult(
                    vm, Status::failure(Diagnostic(DiagnosticCode::InvalidArgument, Severity::Error,
                                                   "Layer must be integer0(back),1(front)or2(both)", "/layer")));
            auto drawn = self->drawLayer(static_cast<PixelFxLayer>(layer.toInt()));
            if (!drawn) return script::projectStatusResult(vm, drawn.status());
            copyImages(*self, *color, *light);
            return script::projectResult(vm, Result<void>::success());
        });
    cls.addFunc("drawActorTo", [vm](PixelFx* self, image::ImageData* target) -> ssq::Table {
        // The image is borrowed only during this synchronous adapter call.
        const auto info = self->info();
        if (!target || target->getWidth() != info.width || target->getHeight() != info.height ||
            target->getFormat() != "RGBA8")
            return script::projectStatusResult(
                vm, Status::failure(Diagnostic(DiagnosticCode::InvalidArgument, Severity::Error,
                                               "Actor image must match effect RGBA8 dimensions", "/target")));
        std::vector<uint32_t> pixels(size_t(info.width) * size_t(info.height));
        auto                  drawn = self->drawActor(pixels);
        if (!drawn) return script::projectStatusResult(vm, drawn.status());
        auto* bytes = static_cast<uint8_t*>(target->getData());
        for (size_t i = 0; i < pixels.size(); ++i)
            for (int c = 0; c < 4; ++c) bytes[i * 4 + size_t(c)] = uint8_t((pixels[i] >> (c * 8)) & 255u);
        return script::projectResult(vm, Result<void>::success());
    });
    cls.addFunc("getConfiguration",
                [vm](PixelFx* self) -> ssq::Table { return projectConfiguration(vm, self->configurationValue()); });
    cls.addFunc("getParameters", [vm](PixelFx* self) -> ssq::Table {
        return script::projectResult(vm, PixelFx::describeParameters(self->info().kind),
                                     [](Value value) { return value; });
    });
    cls.addFunc("prepareConfiguration", [vm](PixelFx* self, const ssq::Object& input) -> ssq::Table {
        auto value = script::valueFromSquirrel(input, configurationLimits());
        if (!value) return script::projectStatusResult(vm, value.status());
        auto prepared = PixelFx::prepareConfigurationValue(self->info().kind, value.value());
        if (!prepared) return script::projectStatusResult(vm, prepared.status());
        auto object = script::makeOwnedSquirrelInstance(
            vm, std::make_unique<PixelFxPreparedConfig>(std::move(prepared).takeValue()));
        if (!object) return script::projectStatusResult(vm, object.status());
        return script::projectStatusResult(vm, Status::success(), std::move(object).takeValue());
    });
    cls.addFunc("commitConfiguration", [vm](PixelFx* self, PixelFxPreparedConfig* candidate) -> ssq::Table {
        if (!candidate)
            return script::projectStatusResult(
                vm, Status::failure(Diagnostic(DiagnosticCode::InvalidArgument, Severity::Error,
                                               "Expected an owned configuration candidate", "/candidate")));
        return script::projectResult(vm, self->commitConfiguration(std::move(*candidate)));
    });
    cls.addFunc("setSeed", [](PixelFx* self, int seed) -> void {
        auto config = self->configurationValue();
        config.find("options")->set("seed", Value(int64_t(uint32_t(seed))));
        applyConfiguration(*self, config);
    });
    cls.addFunc("setParameter", [](PixelFx* self, const std::string& name, const ssq::Object& value) -> void {
        const double number  = scriptNumber(value);
        auto         config  = self->configurationValue();
        const auto*  current = config.find("options")->find(name);
        if (!current) throw eve::Exception("PixelFx: unknown active option %s", name.c_str());
        Value candidate(number);
        if (current->isInt64()) {
            if (!std::isfinite(number) || std::floor(number) != number || std::abs(number) > 9007199254740991.0)
                throw eve::Exception("PixelFx: expected a bounded integer");
            candidate = Value(int64_t(number));
        }
        config.find("options")->set(name, std::move(candidate));
        applyConfiguration(*self, config);
    });
    cls.addFunc("setPaletteColor", [](PixelFx* self, int index, int r, int g, int b) -> void {
        setRampColor(*self, "palette", index, r, g, b);
    });
    cls.addFunc("getPaletteColorCount", [](PixelFx* self) -> int { return int(self->settings().palette.size()); });
    cls.addFunc("setRampColor", [](PixelFx* self, const std::string& name, int index, int r, int g, int b) -> void {
        setRampColor(*self, name, index, r, g, b);
    });
    cls.addFunc(
        "renderTo",
        [](PixelFx* self, const ssq::Object& seconds, image::ImageData* color, image::ImageData* light) -> void {
            auto valid = validateImages(*self, color, light);
            if (!valid) throw eve::Exception("%s", valid.status().describe().c_str());
            self->render(scriptNumber(seconds));
            copyImages(*self, *color, *light);
        });
}
}  // namespace eve::stylize
