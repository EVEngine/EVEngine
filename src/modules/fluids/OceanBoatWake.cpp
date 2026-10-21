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
    double                        displacedVolume = 0.0;
    // Validate the whole receipt before mutating the field. All contact cells share
    // one forcing budget: treating every cell as a complete moving source saturated
    // the velocity/foam clamps on large hulls and erased their divergent wave train.
    for (const auto& contact : receipt.contacts) {
        const auto p = contact.position, v = contact.relativeVelocity;
        if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z) || !std::isfinite(v.x) ||
            !std::isfinite(v.y) || !std::isfinite(v.z) || !std::isfinite(contact.submergedVolume) ||
            contact.submergedVolume < 0.0 || contact.submergedVolume > 1e6 || glm::length(v) > 1e4)
            return invalid();
        displacedVolume += contact.submergedVolume;
    }
    const double supportVolume = std::max(displacedVolume, cell * cell * cell);
    glm::dvec2   center(0.0), heading(0.0);
    for (const auto& contact : receipt.contacts) {
        center += glm::dvec2(contact.position.x, contact.position.z) * contact.submergedVolume;
        heading += glm::dvec2(contact.relativeVelocity.x, contact.relativeVelocity.z) * contact.submergedVolume;
    }
    const bool underway = displacedVolume > 0.0 && glm::length(heading) > 1e-6;
    double     front = 0.0, rear = 0.0;
    if (underway) {
        center /= displacedVolume;
        heading = glm::normalize(heading);
        for (const auto& contact : receipt.contacts) {
            if (contact.submergedVolume == 0.0) continue;
            const double along = glm::dot(glm::dvec2(contact.position.x, contact.position.z) - center, heading);
            front              = std::max(front, along);
            rear               = std::min(rear, along);
        }
    }
    // A moving pressure source at the leading end radiates a divergent wave
    // train. Repeating that source along the hull cancels the outward waves and
    // instead produces long, nearly uniform ridges against both hull sides.
    const double bowWidth  = std::max(cell, (front - rear) * 0.10);
    const auto   bowWeight = [&](const OceanBoatContact& contact) {
        if (!underway) return 1.0;
        const double aft = front - glm::dot(glm::dvec2(contact.position.x, contact.position.z) - center, heading);
        return std::exp(-0.5 * aft * aft / (bowWidth * bowWidth));
    };
    double bowVolume = 0.0;
    for (const auto& contact : receipt.contacts) bowVolume += contact.submergedVolume * bowWeight(contact);
    const double                  bowSupport = std::max(bowVolume, cell * cell * cell);
    std::vector<OceanWakeImpulse> impulses;
    impulses.reserve(receipt.contacts.size() * 2);
    for (const auto& contact : receipt.contacts) {
        const auto p = contact.position, v = contact.relativeVelocity;
        if (contact.submergedVolume == 0.0) continue;
        const double radius = std::clamp(std::cbrt(contact.submergedVolume), cell, settings.length * 0.25);
        const double scale    = dt * contact.submergedVolume / supportVolume;
        const double bowScale = dt * contact.submergedVolume * bowWeight(contact) / bowSupport;
        const double speed  = std::hypot(v.x, v.z);
        const double foam     = std::clamp(scale * (speed + std::abs(v.y)) * 0.005, 0.0, 1.0);
        if (speed > 1e-6) impulses.push_back({{p.x, p.z}, radius, std::min(bowScale * speed, 0.5), {v.x, v.z}, foam});
        if (std::abs(v.y) > 1e-6)
            impulses.push_back({{p.x, p.z}, radius, std::clamp(-scale * v.y, -0.5, 0.5), {0.0, 0.0}, foam});
    }
    return wake.inject(impulses);
}
}  // namespace eve::fluids
