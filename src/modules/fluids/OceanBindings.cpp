#include "fluids/OceanBindings.h"

#include "fluids/OceanBindingInternal.h"
#include "fluids/OceanSpectrumCodec.h"

namespace eve::fluids {
using namespace ocean_script;

void exposeOceanTypes(ssq::Table& table) {
    exposeOceanInteractionTypes(table);
    exposeOceanRenderTypes(table);
    exposeOceanSprayTypes(table);
    const auto vm      = table.getHandle();
    auto       builder = table.addClass<OceanSpectrumBuilder>(
        "OceanSpectrumBuilder", std::function<OceanSpectrumBuilder*()>([] { return new OceanSpectrumBuilder(); }),
        true);
    builder.addFunc("settings", [vm](OceanSpectrumBuilder* self) {
        return script::projectStatusResult(vm, Status::success(), encodeOceanSpectrumSettings(self->settings));
    });
    builder.addFunc("configure", [vm](OceanSpectrumBuilder* self, ssq::Object object) {
        auto value = script::valueFromSquirrel(object);
        if (!value) return script::projectStatusResult(vm, value.status());
        auto decoded = decodeOceanSpectrumSettings(value.value());
        if (!decoded) return script::projectStatusResult(vm, decoded.status());
        self->settings = std::move(decoded).takeValue();
        return script::projectStatusResult(vm, Status::success());
    });
    builder.addFunc("build", [vm](OceanSpectrumBuilder* self) { return ownedResult(vm, self->build()); });

    auto spectrum =
        table.addClass<OceanSpectrum>("OceanSpectrum", std::function<OceanSpectrum*()>([] { return nullptr; }), true);
    spectrum.addFunc("variance", [](OceanSpectrum* self) { return static_cast<float>(self->variance()); });
    spectrum.addFunc("frameBuilder", [vm](OceanSpectrum* self, SQInteger tick, SQInteger rate) {
        auto time = fixedTime(tick, rate);
        if (!time) return script::projectStatusResult(vm, time.status());
        return ownedResult(vm, Result<OceanSpectrumFrameBuilder>::success(self->frameBuilder(time.value().time)));
    });
    auto frameBuilder = table.addClass<OceanSpectrumFrameBuilder>(
        "OceanSpectrumFrameBuilder", std::function<OceanSpectrumFrameBuilder*()>([] { return nullptr; }), true);
    frameBuilder.addFunc("build", [vm](OceanSpectrumFrameBuilder* self) { return ownedResult(vm, self->build()); });
    auto frame = table.addClass<OceanSpectrumFrame>("OceanSpectrumFrame",
                                                    std::function<OceanSpectrumFrame*()>([] { return nullptr; }), true);
    frame.addFunc("time", [](OceanSpectrumFrame* self) { return static_cast<float>(self->time()); });
    frame.addFunc("sample", [vm](OceanSpectrumFrame* self, float x, float z) {
        return script::projectResult(vm, self->sample({x, z}), sampleValue);
    });
}
}  // namespace eve::fluids
