#include "physics/rope/Rope3D.h"

#include <algorithm>
#include <cmath>

namespace eve::physics {
namespace {
Rope3D::Vec3 subtract(Rope3D::Vec3 a, Rope3D::Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Rope3D::Vec3 addScaled(Rope3D::Vec3 a, Rope3D::Vec3 direction, float scale) {
    return {a.x + direction.x * scale, a.y + direction.y * scale, a.z + direction.z * scale};
}
float magnitude(Rope3D::Vec3 value) { return std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z); }
Rope3D::Vec3 direction(Rope3D::Vec3 from, Rope3D::Vec3 to) {
    auto        delta  = subtract(to, from);
    const float length = magnitude(delta);
    if (length <= 1e-6f) return {1.f, 0.f, 0.f};
    return {delta.x / length, delta.y / length, delta.z / length};
}
}  // namespace

eve::Result<RopeTopologyChange> Rope3D::changeLength(float requested, float spacing, bool fromEnd) {
    if (!std::isfinite(requested) || requested <= 0.f || !std::isfinite(spacing) || spacing <= 0.f)
        return eve::Result<RopeTopologyChange>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "Length and particle spacing must be finite and positive",
            "physics.rope3d.changeLength"));
    if (std::any_of(elements_.begin(), elements_.end(), [](const Element& element) { return !element.active; }))
        return eve::Result<RopeTopologyChange>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Conflict, "A torn rope must be repaired before cursor topology changes",
            "physics.rope3d.changeLength"));
    const float original = getRestLength();
    if (std::abs(requested - original) <= 1e-6f)
        return eve::Result<RopeTopologyChange>::success(RopeTopologyChange::Unchanged);

    bool structuralChange = false;
    if (requested > original) {
        float remaining = requested - original;
        while (remaining > 1e-6f) {
            const float segmentLength = std::min(spacing, remaining);
            if (particles_.size() >= 100000)
                return eve::Result<RopeTopologyChange>::failure(
                    eve::Diagnostic::error(eve::DiagnosticCode::PreconditionViolation, "Rope particle limit reached",
                                           "physics.rope3d.changeLength"));
            if (fromEnd) {
                const auto heading = direction(particles_[particles_.size() - 2].position, particles_.back().position);
                Particle   particle;
                particle.position = particle.previous = addScaled(particles_.back().position, heading, segmentLength);
                const int previous                    = getParticleCount() - 1;
                particles_.push_back(particle);
                Element element;
                element.a          = previous;
                element.b          = previous + 1;
                element.restLength = segmentLength;
                elements_.push_back(element);
            } else {
                const auto heading = direction(particles_[1].position, particles_[0].position);
                Particle   particle;
                particle.position = particle.previous = addScaled(particles_[0].position, heading, segmentLength);
                particles_.insert(particles_.begin(), particle);
                for (auto& element : elements_) {
                    ++element.a;
                    ++element.b;
                }
                Element element;
                element.a          = 0;
                element.b          = 1;
                element.restLength = segmentLength;
                elements_.insert(elements_.begin(), element);
            }
            remaining -= segmentLength;
            structuralChange = true;
        }
    } else {
        float remaining = original - requested;
        while (elements_.size() > 1) {
            Element& edge = fromEnd ? elements_.back() : elements_.front();
            if (remaining + 1e-6f < edge.restLength) break;
            remaining -= edge.restLength;
            if (fromEnd) {
                const Particle removed = particles_.back();
                particles_.pop_back();
                elements_.pop_back();
                if (removed.attached) {
                    auto& endpoint      = particles_.back();
                    endpoint.attached   = true;
                    endpoint.attachment = endpoint.position = endpoint.previous = removed.attachment;
                }
            } else {
                const Particle removed = particles_.front();
                particles_.erase(particles_.begin());
                elements_.erase(elements_.begin());
                for (auto& element : elements_) {
                    --element.a;
                    --element.b;
                }
                if (removed.attached) {
                    auto& endpoint      = particles_.front();
                    endpoint.attached   = true;
                    endpoint.attachment = endpoint.position = endpoint.previous = removed.attachment;
                }
            }
            structuralChange = true;
        }
        Element& edge   = fromEnd ? elements_.back() : elements_.front();
        edge.restLength = std::max(1e-6f, edge.restLength - remaining);
    }
    if (structuralChange || std::abs(requested - original) > 1e-6f) ++topologyRevision_;
    syncBendState();
    return eve::Result<RopeTopologyChange>::success(RopeTopologyChange::Changed);
}

int Rope3D::getLastTornElement(int eventIndex) const {
    return eventIndex >= 0 && eventIndex < getLastTornElementCount()
               ? lastTornElements_[static_cast<std::size_t>(eventIndex)]
               : -1;
}

}  // namespace eve::physics
