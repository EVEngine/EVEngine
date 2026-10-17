#include "physics/destruction/cook/AssemblyFractureSettings.h"
#include <cmath>
namespace eve::physics::destruction_cook {
namespace {
Result<void> check(int id, const FractureComponentPolicy& p) {
    if (id < 0 || id > 4095 || p.fragments < 1 || p.fragments > 64 || !std::isfinite(p.cohesion) || p.cohesion <= 0 ||
        !std::isfinite(p.connectionStrength) || p.connectionStrength <= 0)
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument,
            "policy requires valid id, 1..64 fragments and finite positive cohesion/connection strength",
            "fracturePolicy"));
    return Result<void>::success();
}
}  // namespace
Result<void> AssemblyFractureSettings::validate() const {
    auto valid = recipe.validate();
    if (!valid) return valid;
    if (!std::isfinite(contactTolerance) || contactTolerance < 0 || contactTolerance > .25f)
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "contact tolerance must be in [0,0.25] metres", "contactTolerance"));
    for (const auto& [id, policy] : groups) {
        auto r = check(id, policy);
        if (!r) return r;
    }
    for (const auto& [id, policy] : components) {
        auto r = check(id, policy);
        if (!r) return r;
    }
    return Result<void>::success();
}
Result<void> AssemblyFractureSettings::setGroupPolicy(int id, FractureComponentPolicy policy) {
    auto r = check(id, policy);
    if (!r) return r;
    groups[id] = policy;
    return Result<void>::success();
}
Result<void> AssemblyFractureSettings::setComponentPolicy(int id, FractureComponentPolicy policy) {
    auto r = check(id, policy);
    if (!r) return r;
    components[id] = policy;
    return Result<void>::success();
}
}  // namespace eve::physics::destruction_cook
