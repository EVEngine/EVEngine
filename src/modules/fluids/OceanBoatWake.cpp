#include "fluids/OceanBoat.h"
#include "fluids/OceanWake.h"

#include <algorithm>
#include <cmath>

namespace eve::fluids {
Result<void> injectOceanBoatWake(OceanWake& wake, const OceanBoatReceipt& receipt, double dt) {
    const auto invalid = [] {
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "Invalid boat wake receipt", "fluids.ocean.wake"));
    };
    if (!std::isfinite(dt) || dt <= 0.0 || dt > 1.0 / 30.0 || receipt.contacts.size() > 64 ||
        !std::isfinite(receipt.time) || std::abs(receipt.time - wake.time()) > 1e-8)
        return invalid();
    const auto                    settings = wake.settings();
    const double                  cell     = settings.length / settings.resolution;
    std::vector<OceanWakeImpulse> impulses;
    impulses.reserve(receipt.contacts.size() * 2);
    for (const auto& contact : receipt.contacts) {
        const auto p = contact.position, v = contact.relativeVelocity;
        if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z) || !std::isfinite(v.x) ||
            !std::isfinite(v.y) || !std::isfinite(v.z) || !std::isfinite(contact.submergedVolume) ||
            contact.submergedVolume < 0.0 || contact.submergedVolume > 1e6 || glm::length(v) > 1e4)
            return invalid();
        if (contact.submergedVolume == 0.0) continue;
        const double radius = std::clamp(std::cbrt(contact.submergedVolume), cell, settings.length * 0.25);
        const double scale  = dt * contact.submergedVolume / (radius * radius * radius);
        const double speed  = std::hypot(v.x, v.z);
        const double foam   = std::clamp(scale * (speed + std::abs(v.y)) * 0.1, 0.0, 1.0);
        if (speed > 1e-6)
            impulses.push_back({{p.x, p.z}, radius, std::min(scale * speed * 2.0, 0.5), {v.x, v.z}, foam});
        if (std::abs(v.y) > 1e-6)
            impulses.push_back({{p.x, p.z}, radius, std::clamp(-scale * v.y, -0.5, 0.5), {0.0, 0.0}, foam});
    }
    return wake.inject(impulses);
}
}  // namespace eve::fluids
