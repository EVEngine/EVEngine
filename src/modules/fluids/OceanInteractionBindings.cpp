#include "fluids/OceanBindingInternal.h"
#include "fluids/OceanBindings.h"
#include "fluids/OceanInteractionCodec.h"
#include "physics/Body3D.h"

namespace eve::fluids {
using namespace ocean_script;
namespace {
struct WakeBuilder {
    OceanWakeSettings settings;
};
struct BoatBuilder {
    OceanHullSettings settings;
};
}  // namespace

void exposeOceanInteractionTypes(ssq::Table& table) {
    const auto vm          = table.getHandle();
    auto       wakeBuilder = table.addClass<WakeBuilder>(
        "OceanWakeBuilder", std::function<WakeBuilder*()>([] { return new WakeBuilder(); }), true);
    wakeBuilder.addFunc("settings", [vm](WakeBuilder* self) {
        return script::projectStatusResult(vm, Status::success(), encodeOceanWakeSettings(self->settings));
    });
    wakeBuilder.addFunc("configure", [vm](WakeBuilder* self, ssq::Object object) {
        auto value = script::valueFromSquirrel(object);
        if (!value) return script::projectStatusResult(vm, value.status());
        auto decoded = decodeOceanWakeSettings(value.value());
        if (!decoded) return script::projectStatusResult(vm, decoded.status());
        self->settings = std::move(decoded).takeValue();
        return script::projectStatusResult(vm, Status::success());
    });
    wakeBuilder.addFunc("build",
                        [vm](WakeBuilder* self) { return ownedPointerResult(vm, OceanWake::create(self->settings)); });
    auto wake = table.addClass<OceanWake>("OceanWake", std::function<OceanWake*()>([] { return nullptr; }), true);
    table.addClass<OceanWakeStep>("OceanWakeStep", std::function<OceanWakeStep*()>([] { return nullptr; }), true);
    wake.addFunc("prepareStep", [vm](OceanWake* self, OceanSpectrumFrame* frame, SQInteger endTick, SQInteger rate) {
        if (!frame) return script::projectStatusResult(vm, invalid("Missing spectral frame"));
        auto time = fixedTime(endTick, rate);
        if (!time) return script::projectStatusResult(vm, time.status());
        return ownedPointerResult(vm, self->prepareStep(time.value().dt, time.value().time, *frame));
    });
    wake.addFunc("commitStep", [vm](OceanWake* self, OceanWakeStep* step) {
        if (!step) return script::projectStatusResult(vm, invalid("Missing wake step candidate"));
        return script::projectResult(vm, self->commitStep(*step));
    });
    wake.addFunc("recenter",
                 [vm](OceanWake* self, float x, float z) { return script::projectResult(vm, self->recenter({x, z})); });
    wake.addFunc("sampleSurface", [vm](OceanWake* self, OceanSpectrumFrame* frame, float x, float z) {
        if (!frame) return script::projectStatusResult(vm, invalid("Missing spectral frame"));
        return script::projectResult(vm, OceanWaterView(*frame, *self).sample({x, z}), sampleValue);
    });
    wake.addFunc("sample", [vm](OceanWake* self, float x, float z) {
        return script::projectResult(vm, self->sample({x, z}), [](const OceanWakeSample& s) {
            return Value(Value::Object{{"height", Value(s.height)},
                                       {"velocity", Value(s.velocity)},
                                       {"gradient", Value::Array{Value(s.gradient.x), Value(s.gradient.y)}},
                                       {"foam", Value(s.foam)}});
        });
    });
    wake.addFunc("injectReceipt", [vm](OceanWake* self, OceanBoatReceipt* receipt, SQInteger rate) {
        if (!receipt) return script::projectStatusResult(vm, invalid("Missing boat receipt"));
        auto time = fixedTime(0, rate);
        if (!time) return script::projectStatusResult(vm, time.status());
        return script::projectResult(vm, injectOceanBoatWake(*self, *receipt, time.value().dt));
    });

    auto boatBuilder = table.addClass<BoatBuilder>(
        "OceanBoatBuilder", std::function<BoatBuilder*()>([] { return new BoatBuilder(); }), true);
    boatBuilder.addFunc("settings", [vm](BoatBuilder* self) {
        return script::projectStatusResult(vm, Status::success(), encodeOceanHullSettings(self->settings));
    });
    boatBuilder.addFunc("configure", [vm](BoatBuilder* self, ssq::Object object) {
        auto value = script::valueFromSquirrel(object);
        if (!value) return script::projectStatusResult(vm, value.status());
        auto decoded = decodeOceanHullSettings(value.value());
        if (!decoded) return script::projectStatusResult(vm, decoded.status());
        self->settings = std::move(decoded).takeValue();
        return script::projectStatusResult(vm, Status::success());
    });
    boatBuilder.addFunc("build", [vm](BoatBuilder* self, physics::Body3D* body) {
        if (!body) return script::projectStatusResult(vm, invalid("Missing live physics body"));
        return ownedPointerResult(vm, OceanBoat::create(*body, self->settings));
    });
    auto boat = table.addClass<OceanBoat>("OceanBoat", std::function<OceanBoat*()>([] { return nullptr; }), true);
    boat.addFunc("apply", [vm](OceanBoat* self, OceanSpectrumFrame* frame, ssq::Object wakeObject, SQInteger rate,
                               float throttle, float rudder) {
        if (!frame) return script::projectStatusResult(vm, invalid("Missing spectral frame"));
        auto time = fixedTime(0, rate);
        if (!time) return script::projectStatusResult(vm, time.status());
        const auto* localWake = wakeObject.isNull() ? nullptr : wakeObject.to<OceanWake*>();
        const auto  water     = localWake ? OceanWaterView(*frame, *localWake) : OceanWaterView(*frame);
        return ownedResult(vm, self->apply(water, time.value().dt, throttle, rudder));
    });
    auto receipt = table.addClass<OceanBoatReceipt>("OceanBoatReceipt",
                                                    std::function<OceanBoatReceipt*()>([] { return nullptr; }), true);
    receipt.addFunc("summary", [vm](OceanBoatReceipt* self) {
        return script::projectStatusResult(
            vm, Status::success(),
            Value::Object{{"time", Value(self->time)},
                          {"totalForce", vector(self->totalForce)},
                          {"submergedVolume", Value(self->submergedVolume)},
                          {"contacts", Value(static_cast<std::int64_t>(self->contacts.size()))}});
    });
}
}  // namespace eve::fluids
