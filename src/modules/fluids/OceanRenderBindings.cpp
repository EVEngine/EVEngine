#include "fluids/OceanBindingInternal.h"
#include "fluids/OceanBindings.h"
#include "fluids/OceanCodecInternal.h"
#include "fluids/OceanGpuSpectrum.h"
#include "fluids/OceanHullMask.h"
#include "fluids/OceanRenderer.h"
#include "fluids/OceanWake.h"
#include "fluids/OceanWaterView.h"
#include "gpgpu/Gpgpu.h"
#include "graphics/Graphics.h"

namespace eve::fluids {
using namespace ocean_script;
namespace {
// Native leases keep projections alive across script GC. Providers still follow their
// existing retirement contracts; retaining a projection never retains a graphics device.
struct GpuBuilder {};
struct GpuOcean {
    std::shared_ptr<OceanGpuSpectrum> value;
};
struct GpuSubmission {
    std::shared_ptr<OceanGpuSpectrum> owner;
    double                            time;
};
struct RenderBuilder {
    OceanMeshSettings  mesh;
    OceanRenderBackend backend = OceanRenderBackend::GpuResident;
};
struct Renderer {
    std::shared_ptr<OceanRenderer> value;
};
struct Placement {
    glm::dvec2 center{0.0};
    glm::dvec3 origin{0.0};
};
struct Publication {
    std::shared_ptr<OceanRenderer>    renderer;
    std::shared_ptr<OceanGpuSpectrum> gpu;
    OceanGpuFrameToken                token;
    // These rooted call inputs form an acyclic lease: neither input references this builder.
    ssq::Object frame, wake;
    Placement   placement;
};
struct HullMaskScriptPublication {
    std::shared_ptr<OceanRenderer> renderer;
    std::vector<OceanHullVolume>   volumes;
    glm::dvec3                     origin{0.0};
};
Result<HullMaskScriptPublication> decodeHullMask(const Value& value, std::shared_ptr<OceanRenderer> renderer) {
    using namespace ocean_codec;
    try {
        keys(value, {"origin", "volumes"});
        HullMaskScriptPublication result;
        result.renderer     = std::move(renderer);
        result.origin       = vector3(field(value, "origin"));
        const auto& volumes = field(value, "volumes");
        if (!volumes.isArray() || volumes.arraySize() > 32) throw std::runtime_error("At most 32 hull volumes");
        auto vector4 = [](const Value& v) {
            if (!v.isArray() || v.arraySize() != 4) throw std::runtime_error("Four plane/quaternion components");
            return glm::dvec4(number(v.at(0)), number(v.at(1)), number(v.at(2)), number(v.at(3)));
        };
        for (std::size_t i = 0; i < volumes.arraySize(); ++i) {
            const auto& v = volumes.at(i);
            keys(v, {"center", "rotation", "halfExtent", "bevels"});
            OceanHullVolume hull;
            hull.center        = vector3(field(v, "center"));
            hull.halfExtent    = vector3(field(v, "halfExtent"));
            const auto q       = vector4(field(v, "rotation"));
            hull.rotation      = {q.w, q.x, q.y, q.z};
            const auto& bevels = field(v, "bevels");
            if (!bevels.isArray() || bevels.arraySize() > 10) throw std::runtime_error("At most ten hull bevels");
            for (std::size_t j = 0; j < bevels.arraySize(); ++j) hull.bevels.push_back(vector4(bevels.at(j)));
            result.volumes.push_back(std::move(hull));
        }
        return Result<HullMaskScriptPublication>::success(std::move(result));
    } catch (const std::runtime_error& error) {
        return Result<HullMaskScriptPublication>::failure(invalid(error.what()));
    }
}
Value settingsValue(const RenderBuilder& b) {
    return Value::Object{{"segments", Value(static_cast<std::int64_t>(b.mesh.segments))},
                         {"rings", Value(static_cast<std::int64_t>(b.mesh.rings))},
                         {"firstRadius", Value(b.mesh.firstRadius)},
                         {"outerRadius", Value(b.mesh.outerRadius)},
                         {"backend", Value(b.backend == OceanRenderBackend::GpuResident ? "gpu" : "cpu")}};
}
Result<RenderBuilder> decodeSettings(const Value& v) {
    using namespace ocean_codec;
    try {
        keys(v, {"segments", "rings", "firstRadius", "outerRadius", "backend"});
        const auto& segments = field(v, "segments");
        const auto& rings    = field(v, "rings");
        const auto& backend  = field(v, "backend");
        if (!segments.isInt64() || segments.asInt() < 16 || segments.asInt() > 1024 || !rings.isInt64() ||
            rings.asInt() < 2 || rings.asInt() > 512 || !backend.isString() ||
            (backend.asString() != "gpu" && backend.asString() != "cpu"))
            return Result<RenderBuilder>::failure(invalid("Invalid render quality/backend"));
        RenderBuilder out;
        out.mesh    = {static_cast<unsigned>(segments.asInt()), static_cast<unsigned>(rings.asInt()),
                       number(field(v, "firstRadius")), number(field(v, "outerRadius"))};
        out.backend = backend.asString() == "gpu" ? OceanRenderBackend::GpuResident : OceanRenderBackend::CpuReference;
        return Result<RenderBuilder>::success(out);
    } catch (const std::runtime_error& error) {
        return Result<RenderBuilder>::failure(invalid(error.what()));
    }
}
Result<Placement> decodePlacement(const ssq::Object& object) {
    auto v = script::valueFromSquirrel(object);
    if (!v) return Result<Placement>::failure(v.status());
    try {
        ocean_codec::keys(v.value(), {"center", "origin"});
        return Result<Placement>::success({ocean_codec::vector(ocean_codec::field(v.value(), "center")),
                                           ocean_codec::vector3(ocean_codec::field(v.value(), "origin"))});
    } catch (const std::runtime_error& error) {
        return Result<Placement>::failure(invalid(error.what()));
    }
}
Result<void> publish(const Publication& p) {
    const auto* wake = p.wake.isNull() ? nullptr : p.wake.to<OceanWake*>();
    if (p.gpu) {
        auto b   = p.renderer->gpuFrameBuilder(*p.gpu, p.token);
        b.center = p.placement.center;
        b.origin = p.placement.origin;
        b.wake   = wake;
        return b.publish();
    }
    const auto* frame = p.frame.to<OceanSpectrumFrame*>();
    const auto  water = wake ? OceanWaterView(*frame, *wake) : OceanWaterView(*frame);
    auto        b     = p.renderer->frameBuilder(water);
    b.center          = p.placement.center;
    b.origin          = p.placement.origin;
    auto prepared     = b.build();
    if (!prepared) return Result<void>::failure(prepared.status());
    return p.renderer->publish(prepared.value());
}
}  // namespace

void exposeOceanRenderTypes(ssq::Table& table) {
    const auto vm = table.getHandle();
    auto       gb = table.addClass<GpuBuilder>("OceanGpuSpectrumBuilder",
                                               std::function<GpuBuilder*()>([] { return new GpuBuilder(); }), true);
    gb.addFunc(
        "build", [vm](GpuBuilder*, OceanSpectrum* spectrum, graphics::Graphics* graphics, gpgpu::Gpgpu* compute) {
            if (!spectrum || !graphics || !compute)
                return script::projectStatusResult(vm, invalid("Missing GPU ocean inputs"));
            auto built = OceanGpuSpectrumBuilder(*spectrum, *graphics, *compute).build();
            if (!built) return script::projectStatusResult(vm, built.status());
            return ownedResult(
                vm, Result<GpuOcean>::success({std::shared_ptr<OceanGpuSpectrum>(std::move(built).takeValue())}));
        });
    auto gpu = table.addClass<GpuOcean>("OceanGpuSpectrum", std::function<GpuOcean*()>([] { return nullptr; }), true);
    gpu.addFunc("frameBuilder", [vm](GpuOcean* self, SQInteger tick, SQInteger rate) {
        auto time = fixedTime(tick, rate);
        if (!time) return script::projectStatusResult(vm, time.status());
        return ownedResult(vm, Result<GpuSubmission>::success({self->value, time.value().time}));
    });
    auto submit = table.addClass<GpuSubmission>("OceanGpuFrameBuilder",
                                                std::function<GpuSubmission*()>([] { return nullptr; }), true);
    submit.addFunc("submit", [vm](GpuSubmission* self) {
        return ownedResult(vm, self->owner->frameBuilder(self->time).submit());
    });
    auto token = table.addClass<OceanGpuFrameToken>("OceanGpuFrameToken",
                                                    std::function<OceanGpuFrameToken*()>([] { return nullptr; }), true);
    token.addFunc("time", [](OceanGpuFrameToken* self) { return static_cast<float>(self->time()); });
    auto rb = table.addClass<RenderBuilder>("OceanRendererBuilder",
                                            std::function<RenderBuilder*()>([] { return new RenderBuilder(); }), true);
    rb.addFunc("settings", [vm](RenderBuilder* self) {
        return script::projectStatusResult(vm, Status::success(), settingsValue(*self));
    });
    rb.addFunc("configure", [vm](RenderBuilder* self, ssq::Object settings) {
        auto value = script::valueFromSquirrel(settings);
        if (!value) return script::projectStatusResult(vm, value.status());
        auto parsed = decodeSettings(value.value());
        if (!parsed) return script::projectStatusResult(vm, parsed.status());
        *self = std::move(parsed).takeValue();
        return script::projectStatusResult(vm, Status::success());
    });
    rb.addFunc("build", [vm](RenderBuilder* self, graphics::Graphics* graphics) {
        if (!graphics) return script::projectStatusResult(vm, invalid("Missing graphics provider"));
        OceanRendererBuilder b(*graphics);
        b.mesh     = self->mesh;
        b.backend  = self->backend;
        auto built = b.build();
        if (!built) return script::projectStatusResult(vm, built.status());
        return ownedResult(vm,
                           Result<Renderer>::success({std::shared_ptr<OceanRenderer>(std::move(built).takeValue())}));
    });
    auto render = table.addClass<Renderer>("OceanRenderer", std::function<Renderer*()>([] { return nullptr; }), true);
    render.addFunc("hullMaskBuilder", [vm](Renderer* self, ssq::Object config) {
        auto value = script::valueFromSquirrel(config);
        if (!value) return script::projectStatusResult(vm, value.status());
        return ownedResult(vm, decodeHullMask(value.value(), self->value));
    });
    auto hullPublication = table.addClass<HullMaskScriptPublication>(
        "OceanHullMaskPublication", std::function<HullMaskScriptPublication*()>([] { return nullptr; }), true);
    hullPublication.addFunc("publish", [vm](HullMaskScriptPublication* self) {
        OceanHullMaskBuilder builder(self->volumes);
        builder.origin = self->origin;
        auto frame     = builder.build();
        if (!frame) return script::projectStatusResult(vm, frame.status());
        return script::projectResult(vm, self->renderer->hullMaskBuilder(frame.value()).publish());
    });
    render.addFunc("draw", [vm](Renderer* self) { return script::projectResult(vm, self->value->draw()); });
    render.addFunc("frameBuilder", [vm](Renderer* self, ssq::Object frame, ssq::Object wake, ssq::Object placement) {
        auto position = decodePlacement(placement);
        if (!position) return script::projectStatusResult(vm, position.status());
        if (!frame.to<OceanSpectrumFrame*>()) return script::projectStatusResult(vm, invalid("Missing spectral frame"));
        if (!wake.isNull()) static_cast<void>(wake.to<OceanWake*>());
        return ownedResult(vm, Result<Publication>::success(
                                   {self->value, {}, {}, std::move(frame), std::move(wake), position.value()}));
    });
    render.addFunc("gpuFrameBuilder", [vm](Renderer* self, GpuOcean* source, OceanGpuFrameToken* token,
                                           ssq::Object wake, ssq::Object placement) {
        if (!source || !token) return script::projectStatusResult(vm, invalid("Missing GPU projection/token"));
        auto position = decodePlacement(placement);
        if (!position) return script::projectStatusResult(vm, position.status());
        if (!wake.isNull()) static_cast<void>(wake.to<OceanWake*>());
        return ownedResult(vm, Result<Publication>::success(
                                   {self->value, source->value, *token, {}, std::move(wake), position.value()}));
    });
    auto publication = table.addClass<Publication>("OceanRenderFrameBuilder",
                                                   std::function<Publication*()>([] { return nullptr; }), true);
    publication.addFunc("publish", [vm](Publication* self) { return script::projectResult(vm, publish(*self)); });
}
}  // namespace eve::fluids
