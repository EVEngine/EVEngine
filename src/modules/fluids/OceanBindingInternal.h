#pragma once

#include "common/SquirrelBinding.h"
#include "common/SquirrelOwnership.h"
#include "fluids/OceanSpectrum.h"

namespace eve::fluids::ocean_script {
template <class T>
ssq::Table ownedPointerResult(HSQUIRRELVM vm, Result<std::unique_ptr<T>> result) {
    if (!result) return script::projectStatusResult(vm, result.status());
    auto object = script::makeOwnedSquirrelInstance<T>(vm, std::move(result).takeValue());
    if (!object) return script::projectStatusResult(vm, object.status());
    return script::projectStatusResult(vm, Status::success(), std::move(object).takeValue());
}
template <class T>
ssq::Table ownedResult(HSQUIRRELVM vm, Result<T> result) {
    if (!result) return script::projectStatusResult(vm, result.status());
    return ownedPointerResult(vm,
                              Result<std::unique_ptr<T>>::success(std::make_unique<T>(std::move(result).takeValue())));
}
inline Status invalid(const char* message) {
    return Status::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, message, "fluids.ocean.script"));
}
struct FixedTime {
    double time, dt;
};
inline Result<FixedTime> fixedTime(SQInteger tick, SQInteger rate) {
    if (tick < 0 || rate < 30 || rate > 1000 || static_cast<double>(tick) > 1e9 * rate)
        return Result<FixedTime>::failure(
            invalid("Expected nonnegative tick, frequency 30..1000 Hz and time <= 1e9 s"));
    return Result<FixedTime>::success({static_cast<double>(tick) / rate, 1.0 / rate});
}
inline Value vector(glm::dvec3 v) { return Value::Array{Value(v.x), Value(v.y), Value(v.z)}; }
inline Value sampleValue(const OceanSurfaceSample& sample) {
    return Value::Object{
        {"height", Value(sample.height)}, {"normal", vector(sample.normal)}, {"velocity", vector(sample.velocity)}};
}
}  // namespace eve::fluids::ocean_script
