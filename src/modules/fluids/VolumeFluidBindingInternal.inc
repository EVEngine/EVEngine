#pragma once
#include <cmath>
#include <limits>
#include <simplesquirrel/simplesquirrel.hpp>
#include "common/Exception.h"
#include "common/SquirrelBinding.h"
#include "common/SquirrelOwnership.h"
#include "fluids/Fluids.h"
#include "fluids/VolumeFluidBindings.h"
#include "fluids/VolumeFluidCodec.h"
#include "fluids/VolumeFluidCoupling.h"
#include "image/ImageData.h"
#include "physics/Body3D.h"
#include "physics/World3D.h"

namespace eve::fluids {
namespace {
template <class Emitter>
void exposeEmitterState(ssq::Class& cls, HSQUIRRELVM vm) {
    cls.addFunc("isEmitting", [](Emitter* self) { return self->isEmitting(); });
    cls.addFunc("activeParticleCount", [vm](Emitter*, VolumeFluid* solver, ssq::Object emissionObject) {
        if (!solver)
            return script::projectStatusResult(
                vm,
                Status::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "Missing volume solver",
                                                  "fluids.volume.actorParticleCount")),
                false, false);
        auto value = script::valueFromSquirrel(emissionObject);
        if (!value) return script::projectStatusResult(vm, value.status(), false, false);
        auto emission = decodeVolumeFluidEmission(value.value());
        if (!emission) return script::projectStatusResult(vm, emission.status(), false, false);
        return script::projectResult(vm, solver->actorParticleCount(emission.value().prototype.actorGroup),
                                     [](unsigned count) { return Value(int64_t(count)); });
    });
    cls.addFunc("killParticle", [vm](Emitter*, VolumeFluid* solver, ssq::Object emissionObject, int actorIndex) {
        if (!solver || actorIndex < 0)
            return script::projectStatusResult(
                vm,
                Status::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                  "Missing solver or invalid actor particle index",
                                                  "fluids.volume.killActorParticle")),
                false, false);
        auto value = script::valueFromSquirrel(emissionObject);
        if (!value) return script::projectStatusResult(vm, value.status(), false, false);
        auto emission = decodeVolumeFluidEmission(value.value());
        if (!emission) return script::projectStatusResult(vm, emission.status(), false, false);
        return script::projectResult(
            vm, solver->killActorParticle(emission.value().prototype.actorGroup, unsigned(actorIndex)));
    });
    cls.addFunc("snapshot", [vm](Emitter* self) {
        return script::projectStatusResult(vm, Status::success(), true, true,
                                           encodeVolumeFluidEmitterState(self->snapshot()));
    });
    cls.addFunc("restore", [vm](Emitter* self, ssq::Object object) {
        auto value = script::valueFromSquirrel(object);
        if (!value) return script::projectStatusResult(vm, value.status(), false, false);
        auto decoded = decodeVolumeFluidEmitterState(value.value());
        if (!decoded) return script::projectStatusResult(vm, decoded.status(), false, false);
        return script::projectResult(vm, self->restore(decoded.value()));
    });
    cls.addFunc("checkpoint", [vm](Emitter* self, VolumeFluid* solver, ssq::Object emissionObject) {
        if (!solver)
            return script::projectStatusResult(
                vm,
                Status::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "Missing volume solver",
                                                  "fluids.volume.emitterCheckpoint")),
                false, false);
        auto value = script::valueFromSquirrel(emissionObject);
        if (!value) return script::projectStatusResult(vm, value.status(), false, false);
        auto emission = decodeVolumeFluidEmission(value.value());
        if (!emission) return script::projectStatusResult(vm, emission.status(), false, false);
        return script::projectStatusResult(
            vm, Status::success(), true, true,
            encodeVolumeFluidEmitterCheckpoint(captureVolumeFluidEmitterCheckpoint(*solver, emission.value(), *self)));
    });
    cls.addFunc("restoreCheckpoint", [vm](Emitter* self, VolumeFluid* solver, ssq::Object checkpointObject) {
        if (!solver)
            return script::projectStatusResult(
                vm,
                Status::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "Missing volume solver",
                                                  "fluids.volume.emitterCheckpoint")),
                false, false);
        auto value = script::valueFromSquirrel(checkpointObject);
        if (!value) return script::projectStatusResult(vm, value.status(), false, false);
        auto checkpoint = decodeVolumeFluidEmitterCheckpoint(value.value());
        if (!checkpoint) return script::projectStatusResult(vm, checkpoint.status(), false, false);
        auto restored = restoreVolumeFluidEmitterCheckpoint(*solver, *self, checkpoint.value());
        if (!restored) return script::projectStatusResult(vm, restored.status(), false, false);
        return script::projectStatusResult(vm, Status::success(), true, true,
                                           encodeVolumeFluidEmission(checkpoint.value().emission));
    });
}
Result<VolumeFluidSnapshot> read(const ssq::Object& object) {
    auto value = script::valueFromSquirrel(object);
    if (!value) return Result<VolumeFluidSnapshot>::failure(value.status());
    return decodeVolumeFluid(value.value());
}
Result<std::vector<unsigned>> readParticleIndices(const ssq::Object& object) {
    auto value = script::valueFromSquirrel(object);
    if (!value) return Result<std::vector<unsigned>>::failure(value.status());
    if (!value.value().isArray() || value.value().arraySize() > 65536)
        return Result<std::vector<unsigned>>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "Particle indices must be a bounded array", "fluids.volume.attachment"));
    std::vector<unsigned> indices;
    indices.reserve(value.value().arraySize());
    for (size_t i = 0; i < value.value().arraySize(); ++i) {
        const auto& item = value.value().at(i);
        if (!item.isInt64() || item.asInt() < 0 ||
            uint64_t(item.asInt()) > uint64_t(std::numeric_limits<unsigned>::max()))
            return Result<std::vector<unsigned>>::failure(
                Diagnostic::error(DiagnosticCode::InvalidArgument, "Particle indices must be nonnegative integers",
                                  "fluids.volume.attachment"));
        indices.push_back(unsigned(item.asInt()));
    }
    return Result<std::vector<unsigned>>::success(std::move(indices));
}
}  // namespace


}  // namespace eve::fluids
