#include <charconv>
#include "fluids/OceanBindingInternal.h"
#include "fluids/OceanBindings.h"
#include "fluids/OceanBoat.h"
#include "fluids/OceanCodecInternal.h"
#include "fluids/OceanSprayRenderer.h"
#include "fluids/OceanWake.h"
#include "graphics/Graphics.h"

namespace eve::fluids {
using namespace ocean_script;
// SimpleSquirrel hashes typeid names; anonymous binding types still need domain-unique names.
namespace {
struct SprayFrame {
    std::shared_ptr<const OceanSprayFrame> value;
};
struct SprayStep {
    std::shared_ptr<const OceanSprayFrame>                  previous;
    ssq::Object                                             water, wake;
    double                                                  dt;
    std::vector<std::pair<std::uint64_t, OceanBoatReceipt>> sources;
};
struct SprayRenderBuilder {};
struct SprayRenderer {
    std::shared_ptr<OceanSprayRenderer> value;
};
struct SprayPublication {
    std::shared_ptr<OceanSprayRenderer>    renderer;
    std::shared_ptr<const OceanSprayFrame> frame;
    OceanSprayView                         view;
};
ssq::Table frameResult(HSQUIRRELVM vm, Result<OceanSprayFrame> result) {
    if (!result) return script::projectStatusResult(vm, result.status());
    return ownedResult(vm,
                       Result<SprayFrame>::success({std::make_shared<OceanSprayFrame>(std::move(result).takeValue())}));
}
Value settingsValue(const OceanSpraySettings& s) {
    return Value::Object{{"seed", Value(std::to_string(s.seed))},
                         {"capacity", Value(static_cast<std::int64_t>(s.capacity))},
                         {"gravity", Value(s.gravity)},
                         {"lifetime", Value(s.lifetime)},
                         {"radius", Value(s.radius)},
                         {"emissionPerVolumeSpeed", Value(s.emissionPerVolumeSpeed)}};
}
Result<OceanSpraySettings> decodeSettings(const Value& value) {
    using namespace ocean_codec;
    try {
        keys(value, {"seed", "capacity", "gravity", "lifetime", "radius", "emissionPerVolumeSpeed"});
        OceanSpraySettings s;
        const auto&        seed = field(value, "seed");
        if (!seed.isString()) throw std::runtime_error("seed requires decimal text");
        const auto& text = seed.asString();
        if (text.empty() || (text.size() > 1 && text[0] == '0')) throw std::runtime_error("noncanonical seed");
        auto parsed = std::from_chars(text.data(), text.data() + text.size(), s.seed);
        if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
            throw std::runtime_error("seed outside uint64");
        const auto& capacity = field(value, "capacity");
        if (!capacity.isInt64() || capacity.asInt() < 1 || capacity.asInt() > 16384)
            throw std::runtime_error("capacity range");
        s.capacity               = static_cast<unsigned>(capacity.asInt());
        s.gravity                = number(field(value, "gravity"));
        s.lifetime               = number(field(value, "lifetime"));
        s.radius                 = number(field(value, "radius"));
        s.emissionPerVolumeSpeed = number(field(value, "emissionPerVolumeSpeed"));
        auto checked             = OceanSprayFrame::create(s);
        if (!checked) return Result<OceanSpraySettings>::failure(checked.status());
        return Result<OceanSpraySettings>::success(s);
    } catch (const std::runtime_error& e) {
        return Result<OceanSpraySettings>::failure(invalid(e.what()));
    }
}
Result<OceanSprayView> decodeView(const ssq::Object& object) {
    auto v = script::valueFromSquirrel(object);
    if (!v) return Result<OceanSprayView>::failure(v.status());
    try {
        using namespace ocean_codec;
        keys(v.value(), {"eye", "forward", "up", "origin"});
        return Result<OceanSprayView>::success({vector3(field(v.value(), "eye")), vector3(field(v.value(), "forward")),
                                                vector3(field(v.value(), "up")), vector3(field(v.value(), "origin"))});
    } catch (const std::runtime_error& e) {
        return Result<OceanSprayView>::failure(invalid(e.what()));
    }
}
}  // namespace
void exposeOceanSprayTypes(ssq::Table& table) {
    const auto vm = table.getHandle();
    auto       b  = table.addClass<OceanSpraySettings>(
        "OceanSprayBuilder", std::function<OceanSpraySettings*()>([] { return new OceanSpraySettings; }), true);
    b.addFunc("settings", [vm](OceanSpraySettings* s) {
        return script::projectStatusResult(vm, Status::success(), settingsValue(*s));
    });
    b.addFunc("configure", [vm](OceanSpraySettings* s, ssq::Object value) {
        auto v = script::valueFromSquirrel(value);
        if (!v) return script::projectStatusResult(vm, v.status());
        auto parsed = decodeSettings(v.value());
        if (!parsed) return script::projectStatusResult(vm, parsed.status());
        *s = std::move(parsed).takeValue();
        return script::projectStatusResult(vm, Status::success());
    });
    b.addFunc("build", [vm](OceanSpraySettings* s) { return frameResult(vm, OceanSprayFrame::create(*s)); });
    auto f = table.addClass<SprayFrame>("OceanSprayFrame", std::function<SprayFrame*()>([] { return nullptr; }), true);
    f.addFunc("stepBuilder", [vm](SprayFrame* self, ssq::Object water, ssq::Object wake, SQInteger rate) {
        if (water.isNull()) return script::projectStatusResult(vm, invalid("Missing spectral frame"));
        auto t = fixedTime(0, rate);
        if (!t) return script::projectStatusResult(vm, t.status());
        return ownedResult(vm, Result<SprayStep>::success({self->value, water, wake, t.value().dt, {}}));
    });
    f.addFunc("summary", [vm](SprayFrame* self) {
        const auto r = self->value->report();
        return script::projectStatusResult(
            vm, Status::success(),
            Value::Object{{"time", Value(self->value->time())},
                          {"alive", Value(static_cast<std::int64_t>(self->value->particles().size()))},
                          {"emitted", Value(static_cast<std::int64_t>(r.emitted))},
                          {"dropped", Value(static_cast<std::int64_t>(r.dropped))},
                          {"expired", Value(static_cast<std::int64_t>(r.expired))},
                          {"returnedToWater", Value(static_cast<std::int64_t>(r.returnedToWater))}});
    });
    auto step =
        table.addClass<SprayStep>("OceanSprayStepBuilder", std::function<SprayStep*()>([] { return nullptr; }), true);
    step.addFunc("addReceipt", [vm](SprayStep* self, SQInteger id, OceanBoatReceipt* receipt) {
        if (id <= 0 || !receipt || self->sources.size() >= 32 || receipt->contacts.size() > 64)
            return script::projectStatusResult(vm, invalid("Invalid spray source"));
        for (const auto& source : self->sources)
            if (source.first == static_cast<std::uint64_t>(id))
                return script::projectStatusResult(vm, invalid("Duplicate spray source"));
        self->sources.emplace_back(static_cast<std::uint64_t>(id), *receipt);
        return script::projectStatusResult(vm, Status::success());
    });
    step.addFunc("build", [vm](SprayStep* self) {
        const auto*                   water = self->water.to<OceanSpectrumFrame*>();
        const auto*                   wake  = self->wake.isNull() ? nullptr : self->wake.to<OceanWake*>();
        const auto                    view  = wake ? OceanWaterView(*water, *wake) : OceanWaterView(*water);
        std::vector<OceanSpraySource> sources;
        sources.reserve(self->sources.size());
        for (const auto& source : self->sources) sources.push_back({source.first, source.second});
        return frameResult(vm, OceanSprayStepBuilder(*self->previous, view, sources, self->dt).build());
    });
    auto rb = table.addClass<SprayRenderBuilder>(
        "OceanSprayRendererBuilder", std::function<SprayRenderBuilder*()>([] { return new SprayRenderBuilder; }), true);
    rb.addFunc("build", [vm](SprayRenderBuilder*, graphics::Graphics* gfx) {
        if (!gfx) return script::projectStatusResult(vm, invalid("Missing graphics provider"));
        auto built = OceanSprayRendererBuilder(*gfx).build();
        if (!built) return script::projectStatusResult(vm, built.status());
        return ownedResult(
            vm, Result<SprayRenderer>::success({std::shared_ptr<OceanSprayRenderer>(std::move(built).takeValue())}));
    });
    auto r = table.addClass<SprayRenderer>("OceanSprayRenderer",
                                           std::function<SprayRenderer*()>([] { return nullptr; }), true);
    r.addFunc("frameBuilder", [vm](SprayRenderer* self, SprayFrame* frame, ssq::Object view) {
        if (!frame) return script::projectStatusResult(vm, invalid("Missing spray frame"));
        auto parsed = decodeView(view);
        if (!parsed) return script::projectStatusResult(vm, parsed.status());
        return ownedResult(
            vm, Result<SprayPublication>::success({self->value, frame->value, std::move(parsed).takeValue()}));
    });
    r.addFunc("draw", [vm](SprayRenderer* self) { return script::projectResult(vm, self->value->draw()); });
    auto p = table.addClass<SprayPublication>("OceanSprayPublication",
                                              std::function<SprayPublication*()>([] { return nullptr; }), true);
    p.addFunc("publish", [vm](SprayPublication* self) {
        OceanSprayMeshBuilder b(*self->frame);
        b.view     = self->view;
        auto built = b.build();
        if (!built) return script::projectStatusResult(vm, built.status());
        return script::projectResult(vm, self->renderer->publish(built.value()));
    });
}
}  // namespace eve::fluids
