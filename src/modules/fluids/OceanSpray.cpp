#include "fluids/OceanSpray.h"
#include "fluids/OceanBoat.h"
#include "fluids/OceanWaterView.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace eve::fluids {
namespace {
template <class T>
Result<T> invalid() {
    return Result<T>::failure(Diagnostic::error(
        DiagnosticCode::InvalidArgument, "Invalid ocean spray settings, source or timestamp", "fluids.ocean.spray"));
}
bool finite(glm::dvec3 p) {
    return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z) && glm::length(p) <= 1e9;
}
std::uint64_t mix(std::uint64_t x) {
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}
double random(std::uint64_t& state) {
    state = mix(state);
    return double(state >> 11) * 0x1.0p-53;
}
}  // namespace
Result<OceanSprayFrame> OceanSprayFrame::create(const OceanSpraySettings& s) {
    if (s.capacity == 0 || s.capacity > 16384 || !std::isfinite(s.gravity) || s.gravity <= 0 || s.gravity > 100 ||
        !std::isfinite(s.lifetime) || s.lifetime < 0.05 || s.lifetime > 10 || !std::isfinite(s.radius) ||
        s.radius < 0.001 || s.radius > 0.5 || !std::isfinite(s.emissionPerVolumeSpeed) ||
        s.emissionPerVolumeSpeed < 0 || s.emissionPerVolumeSpeed > 1000)
        return invalid<OceanSprayFrame>();
    OceanSprayFrame out;
    out.settings_ = s;
    return Result<OceanSprayFrame>::success(std::move(out));
}
Result<OceanSprayFrame> OceanSprayStepBuilder::build() const {
    if (!std::isfinite(dt_) || dt_ <= 0 || dt_ > 1.0 / 30.0 || sources_.size() > 32 ||
        std::abs(water_.time() - previous_.time_) >
            std::max(1e-8, 4.0 * std::numeric_limits<double>::epsilon() * std::abs(water_.time())) ||
        water_.time() + dt_ > 1e9 || previous_.tick_ == std::numeric_limits<std::uint64_t>::max())
        return invalid<OceanSprayFrame>();
    std::vector<OceanSpraySource> sources(sources_.begin(), sources_.end());
    // References make source records non-assignable: sort indices instead of records.
    std::vector<unsigned> order(sources.size());
    for (unsigned i = 0; i < order.size(); ++i) order[i] = i;
    std::sort(order.begin(), order.end(), [&](unsigned a, unsigned b) { return sources[a].id < sources[b].id; });
    for (unsigned i = 0; i < order.size(); ++i) {
        const auto& source = sources[order[i]];
        if (source.id == 0 || (i && source.id == sources[order[i - 1]].id) || !std::isfinite(source.receipt.time) ||
            std::abs(source.receipt.time - water_.time()) > 1e-8 || source.receipt.contacts.size() > 64)
            return invalid<OceanSprayFrame>();
        for (const auto& c : source.receipt.contacts)
            if (!finite(c.position) || !finite(c.relativeVelocity) || glm::length(c.relativeVelocity) > 1e4 ||
                !std::isfinite(c.submergedVolume) || c.submergedVolume < 0 || c.submergedVolume > 1e6)
                return invalid<OceanSprayFrame>();
    }
    OceanSprayFrame out;
    out.settings_ = previous_.settings_;
    out.time_     = water_.time() + dt_;
    out.tick_     = previous_.tick_ + 1;
    out.nextId_   = previous_.nextId_;
    const auto& s = out.settings_;
    out.particles_.reserve(s.capacity);
    for (auto p : previous_.particles_) {
        p.age += dt_;
        if (p.age >= p.lifetime) {
            ++out.report_.expired;
            continue;
        }
        p.position += p.velocity * dt_ + glm::dvec3(0, -0.5 * s.gravity * dt_ * dt_, 0);
        p.velocity.y -= s.gravity * dt_;
        auto surface = water_.sample({p.position.x, p.position.z});
        if (!surface) return Result<OceanSprayFrame>::failure(surface.status());
        if (p.position.y <= surface.value().height) {
            ++out.report_.returnedToWater;
            continue;
        }
        out.particles_.push_back(p);
    }
    for (unsigned index : order) {
        const auto& source    = sources[index];
        double      remainder = 0;
        for (const auto& old : previous_.streams_)
            if (old.id == source.id) remainder = old.remainder;
        std::uint64_t rng = mix(mix(mix(s.seed) ^ source.id) ^ out.tick_);
        for (const auto& c : source.receipt.contacts) {
            const double speed = glm::length(c.relativeVelocity);
            const double drive = std::max(0.0, speed - 1.0);
            // Bounded contact contribution prevents a large displacement cell exhausting the budget.
            const double requested =
                remainder + std::min(c.submergedVolume, 1.0) * drive * s.emissionPerVolumeSpeed * dt_;
            const auto count = static_cast<unsigned>(std::floor(requested));
            remainder        = requested - count;
            if (!count) continue;
            auto surface = water_.sample({c.position.x, c.position.z});
            if (!surface) return Result<OceanSprayFrame>::failure(surface.status());
            const unsigned accepted = std::min(count, s.capacity - static_cast<unsigned>(out.particles_.size()));
            out.report_.dropped += count - accepted;
            for (unsigned i = 0; i < accepted; ++i) {
                if (out.nextId_ == std::numeric_limits<std::uint64_t>::max()) return invalid<OceanSprayFrame>();
                const double       a = random(rng), b = random(rng), d = random(rng);
                OceanSprayParticle p;
                p.id       = out.nextId_++;
                p.radius   = s.radius * (0.7 + 0.6 * d);
                p.lifetime = s.lifetime * (0.7 + 0.6 * a);
                p.position = {c.position.x, surface.value().height + p.radius * 2, c.position.z};
                p.velocity =
                    surface.value().velocity + c.relativeVelocity * 0.3 +
                    glm::dvec3((a - 0.5) * drive * 0.5, 1.0 + drive * (0.25 + 0.25 * d), (b - 0.5) * drive * 0.5);
                p.velocity.y = std::max(0.5, p.velocity.y);
                out.particles_.push_back(p);
                ++out.report_.emitted;
            }
        }
        out.streams_.push_back({source.id, remainder});
    }
    return Result<OceanSprayFrame>::success(std::move(out));
}
}  // namespace eve::fluids
