#include "rts/RTS.h"

namespace eve::rts {
Result<void> RTS::checkCrowdLifetime() {
    if (!crowd_ || crowdLifetime_.isAlive()) return Result<void>::success();
    for (const auto& handle : units_) {
        auto* unit = dynamic_cast<Unit*>(ecs::try_get(handle));
        if (!unit) continue;
        unit->crowd()->runtimeAgent = {};
        unit->crowd()->runtimeOwner = 0;
        unit->crowd()->runtimeBindingKey.clear();
    }
    return Result<void>::failure(
        Diagnostic::error(DiagnosticCode::Conflict, "Canonical Crowd provider is unavailable", "crowdProvider"));
}
}  // namespace eve::rts
