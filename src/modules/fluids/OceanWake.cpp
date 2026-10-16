#include "fluids/OceanWake.h"
#include "fluids/OceanFourierInternal.h"
#include "fluids/OceanInterpolationInternal.h"
#include "fluids/OceanSpectrum.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <limits>

namespace eve::fluids {
struct OceanWake::Impl {
    OceanWakeSettings   settings;
    double              time     = 0.0;
    std::uint64_t       identity = 0, revision = 0;
    std::vector<double> height, velocity, foam;
};
namespace {
std::atomic<std::uint64_t> nextWakeIdentity{1};
bool finite(glm::dvec2 p) { return std::isfinite(p.x) && std::isfinite(p.y) && glm::length(p) <= 1e9; }
template <class T>
Result<T> invalid() {
    return Result<T>::failure(
        Diagnostic::error(DiagnosticCode::InvalidArgument, "Invalid ocean wake input", "fluids.ocean.wake"));
}
}  // namespace
OceanWake::OceanWake(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
OceanWake::~OceanWake() = default;
Result<std::unique_ptr<OceanWake>> OceanWake::create(const OceanWakeSettings& s) {
    if (s.resolution < 8 || s.resolution > 512 || (s.resolution & (s.resolution - 1)) != 0 ||
        !std::isfinite(s.length) || s.length < 8.0 || s.length > 4096.0 || !std::isfinite(s.depth) || s.depth < 0.1 ||
        s.depth > 10000.0 || !std::isfinite(s.gravity) || s.gravity <= 0.0 || s.gravity > 100.0 ||
        !std::isfinite(s.damping) || s.damping < 0.0 || s.damping > 10.0 || !std::isfinite(s.absorptionWidth) ||
        s.absorptionWidth < 2.0 * s.length / s.resolution || s.absorptionWidth > s.length * 0.4 ||
        !std::isfinite(s.foamDecay) || s.foamDecay < 0.0 || s.foamDecay > 10.0 || !finite(s.center))
        return invalid<std::unique_ptr<OceanWake>>();
    auto impl      = std::make_unique<Impl>();
    impl->settings = s;
    impl->identity = nextWakeIdentity.fetch_add(1, std::memory_order_relaxed);
    impl->height.resize(s.resolution * s.resolution);
    impl->velocity.resize(impl->height.size());
    impl->foam.resize(impl->height.size());
    return Result<std::unique_ptr<OceanWake>>::success(std::unique_ptr<OceanWake>(new OceanWake(std::move(impl))));
}
Result<void> OceanWake::inject(std::span<const OceanWakeImpulse> impulses) {
    const auto& s = impl_->settings;
    if (impl_->revision == std::numeric_limits<std::uint64_t>::max() || impulses.size() > 256) return invalid<void>();
    for (const auto& p : impulses)
        if (!finite(p.position) || !finite(p.direction) || !std::isfinite(p.radius) ||
            p.radius < s.length / s.resolution || p.radius > s.length * 0.25 || !std::isfinite(p.velocity) ||
            std::abs(p.velocity) > 5.0 || !std::isfinite(p.foam) || p.foam < 0.0 || p.foam > 1.0)
            return invalid<void>();
    for (unsigned z = 0; z < s.resolution; ++z) {
        for (unsigned x = 0; x < s.resolution; ++x) {
            const auto index = z * s.resolution + x;
            const auto point = s.center + (glm::dvec2(x, z) / double(s.resolution) - 0.5) * s.length;
            double     rate = 0.0, foam = 0.0;
            for (const auto& p : impulses) {
                const auto   delta     = (point - p.position) / p.radius;
                const double distance2 = glm::dot(delta, delta);
                if (distance2 > 16.0) continue;
                const double envelope        = std::exp(-0.5 * distance2);
                const double directionLength = glm::length(p.direction);
                const double shape = directionLength > 1e-8 ? glm::dot(delta, p.direction / directionLength) : 1.0;
                rate += p.velocity * envelope * shape;
                foam += p.foam * envelope;
            }
            impl_->velocity[index] = std::clamp(impl_->velocity[index] + rate, -10.0, 10.0);
            impl_->foam[index]     = std::clamp(impl_->foam[index] + foam, 0.0, 1.0);
        }
    }
    ++impl_->revision;
    return Result<void>::success();
}
OceanWakeStep::OceanWakeStep(std::unique_ptr<OceanWake> candidate) : candidate_(std::move(candidate)) {}
OceanWakeStep::~OceanWakeStep() = default;
Result<std::unique_ptr<OceanWakeStep>> OceanWake::prepareStep(double dt, double endTime,
                                                              const OceanSpectrumFrame& water) const {
    if (impl_->revision == std::numeric_limits<std::uint64_t>::max()) return invalid<std::unique_ptr<OceanWakeStep>>();
    auto candidate = std::unique_ptr<OceanWake>(new OceanWake(std::make_unique<Impl>(*impl_)));
    auto prepared  = candidate->advance(dt, endTime, water);
    if (!prepared) return Result<std::unique_ptr<OceanWakeStep>>::failure(prepared.status());
    return Result<std::unique_ptr<OceanWakeStep>>::success(
        std::unique_ptr<OceanWakeStep>(new OceanWakeStep(std::move(candidate))));
}
Result<void> OceanWake::commitStep(OceanWakeStep& step) {
    if (step.consumed_ || step.candidate_->impl_->identity != impl_->identity ||
        step.candidate_->impl_->revision != impl_->revision)
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::Conflict, "Ocean wake candidate is foreign, consumed or stale", "fluids.ocean.wake"));
    impl_.swap(step.candidate_->impl_);
    ++impl_->revision;
    step.consumed_ = true;
    return Result<void>::success();
}
Result<void> OceanWake::advance(double dt, double endTime, const OceanSpectrumFrame& water) {
    if (!std::isfinite(dt) || dt <= 0.0 || dt > 1.0 / 30.0 || !std::isfinite(endTime) ||
        std::abs(endTime - impl_->time - dt) > 1e-8 || endTime > 1e9 || std::abs(water.time() - impl_->time) > 1e-8)
        return invalid<void>();
    const auto& s = impl_->settings;
    const auto  n = s.resolution;
    // Stage transport before mutating any wave state: an inverse-query failure is atomic.
    std::vector<double> foam(impl_->foam.size(), 0.0);
    if (std::any_of(impl_->foam.begin(), impl_->foam.end(), [](double f) { return f > 0.0; })) {
        const double cell     = s.length / n;
        auto         coverage = [&](glm::dvec2 point) {
            const auto uv = (point - s.center) / cell + double(n) * 0.5;
            if (uv.x <= -1.0 || uv.y <= -1.0 || uv.x >= n || uv.y >= n) return 0.0;
            const auto base = glm::floor(uv), fraction = uv - base;
            double     result = 0.0;
            for (int dz = 0; dz < 2; ++dz)
                for (int dx = 0; dx < 2; ++dx) {
                    const int x = int(base.x) + dx, z = int(base.y) + dz;
                    if (x < 0 || z < 0 || x >= int(n) || z >= int(n)) continue;
                    result += impl_->foam[z * n + x] * (dx ? fraction.x : 1.0 - fraction.x) *
                              (dz ? fraction.y : 1.0 - fraction.y);
                }
            return std::clamp(result, 0.0, 1.0);
        };
        for (unsigned z = 0; z < n; ++z)
            for (unsigned x = 0; x < n; ++x) {
                const auto point = s.center + (glm::dvec2(x, z) - double(n) * 0.5) * cell;
                auto       start = water.sample(point);
                if (!start) return Result<void>::failure(start.status());
                const auto v        = start.value().velocity;
                auto       midpoint = water.sample(point - glm::dvec2(v.x, v.z) * (dt * 0.5));
                if (!midpoint) return Result<void>::failure(midpoint.status());
                const auto m    = midpoint.value().velocity;
                foam[z * n + x] = coverage(point - glm::dvec2(m.x, m.z) * dt);
            }
    }
    std::vector<ocean_detail::Complex> height(impl_->height.begin(), impl_->height.end());
    std::vector<ocean_detail::Complex> velocity(impl_->velocity.begin(), impl_->velocity.end());
    ocean_detail::forward2D(height, n);
    ocean_detail::forward2D(velocity, n);
    for (unsigned z = 0; z < n; ++z) {
        for (unsigned x = 0; x < n; ++x) {
            const int    ix = x < n / 2 ? int(x) : int(x) - int(n);
            const int    iz = z < n / 2 ? int(z) : int(z) - int(n);
            const auto   i  = z * n + x;
            const double k  = std::hypot(double(ix), double(iz)) * ocean_detail::tau / s.length;
            if (k == 0.0) {
                height[i]   = 0.0;
                velocity[i] = 0.0;
                continue;
            }
            const double omega = std::sqrt(s.gravity * k * std::tanh(k * s.depth));
            const double sn = std::sin(omega * dt), cs = std::cos(omega * dt);
            const auto   oldHeight = height[i];
            height[i]              = oldHeight * cs + velocity[i] * (sn / omega);
            velocity[i]            = velocity[i] * cs - oldHeight * (omega * sn);
        }
    }
    ocean_detail::inverse2D(height, n);
    ocean_detail::inverse2D(velocity, n);
    for (unsigned z = 0; z < n; ++z) {
        for (unsigned x = 0; x < n; ++x) {
            const auto   i      = z * n + x;
            const double edge   = double(std::min({x, z, n - 1 - x, n - 1 - z})) * s.length / n;
            const double sponge = std::clamp(1.0 - edge / s.absorptionWidth, 0.0, 1.0);
            const double decay  = std::exp(-(s.damping + 8.0 * sponge * sponge) * dt);
            impl_->height[i]    = std::clamp(height[i].real() * decay, -5.0, 5.0);
            impl_->velocity[i]  = std::clamp(velocity[i].real() * decay, -10.0, 10.0);
            impl_->foam[i]      = foam[i] * std::exp(-(s.foamDecay + 8.0 * sponge * sponge) * dt);
        }
    }
    impl_->time = endTime;
    return Result<void>::success();
}
Result<void> OceanWake::recenter(glm::dvec2 center) {
    if (impl_->revision == std::numeric_limits<std::uint64_t>::max() || !finite(center)) return invalid<void>();
    auto&        s         = impl_->settings;
    const double cell      = s.length / s.resolution;
    const auto   shift     = glm::round((center - s.center) / cell);
    const auto   oldHeight = impl_->height, oldVelocity = impl_->velocity, oldFoam = impl_->foam;
    for (unsigned z = 0; z < s.resolution; ++z) {
        for (unsigned x = 0; x < s.resolution; ++x) {
            const auto   i  = z * s.resolution + x;
            const double ox = x + shift.x, oz = z + shift.y;
            if (ox >= 0.0 && ox < s.resolution && oz >= 0.0 && oz < s.resolution) {
                const auto j       = unsigned(oz) * s.resolution + unsigned(ox);
                impl_->height[i]   = oldHeight[j];
                impl_->velocity[i] = oldVelocity[j];
                impl_->foam[i]     = oldFoam[j];
            } else {
                impl_->height[i]   = 0.0;
                impl_->velocity[i] = 0.0;
                impl_->foam[i]     = 0.0;
            }
        }
    }
    s.center += shift * cell;
    ++impl_->revision;
    return Result<void>::success();
}
Result<OceanWakeSample> OceanWake::sample(glm::dvec2 worldXZ) const {
    if (!finite(worldXZ)) return invalid<OceanWakeSample>();
    const auto&     s  = impl_->settings;
    const auto      uv = ((worldXZ - s.center) / s.length + 0.5) * double(s.resolution);
    OceanWakeSample out;
    if (uv.x < 0.0 || uv.y < 0.0 || uv.x >= s.resolution - 1 || uv.y >= s.resolution - 1)
        return Result<OceanWakeSample>::success(out);
    const auto   x = int(uv.x), z = int(uv.y);
    const double fx = uv.x - x, fz = uv.y - z;
    const auto   wx = ocean_detail::weights(fx), wz = ocean_detail::weights(fz);
    const auto   dx = ocean_detail::derivatives(fx), dz = ocean_detail::derivatives(fz);
    for (int j = 0; j < 4; ++j) {
        for (int i = 0; i < 4; ++i) {
            const int ix = x + i - 1, iz = z + j - 1;
            if (ix < 0 || iz < 0 || ix >= int(s.resolution) || iz >= int(s.resolution)) continue;
            const auto index = unsigned(iz) * s.resolution + unsigned(ix);
            out.height += impl_->height[index] * wx[i] * wz[j];
            out.velocity += impl_->velocity[index] * wx[i] * wz[j];
            out.foam += impl_->foam[index] * wx[i] * wz[j];
            out.gradient += impl_->height[index] * glm::dvec2(dx[i] * wz[j], wx[i] * dz[j]) * (s.resolution / s.length);
        }
    }
    const double cell      = s.length / s.resolution;
    const auto   edgeBlend = [&](double coordinate) {
        const double far      = double(s.resolution - 1) - coordinate;
        const double distance = std::min(coordinate, far) * cell;
        const double t        = std::clamp(distance / s.absorptionWidth, 0.0, 1.0);
        return glm::dvec2(t * t * (3.0 - 2.0 * t),
                            (coordinate < far ? 1.0 : -1.0) * 6.0 * t * (1.0 - t) / s.absorptionWidth);
    };
    const auto   bx = edgeBlend(uv.x), bz = edgeBlend(uv.y);
    const double blend = bx.x * bz.x;
    out.gradient       = out.gradient * blend + out.height * glm::dvec2(bx.y * bz.x, bx.x * bz.y);
    out.height *= blend;
    out.velocity *= blend;
    out.foam = std::clamp(out.foam * blend, 0.0, 1.0);
    return Result<OceanWakeSample>::success(out);
}
double            OceanWake::time() const { return impl_->time; }
OceanWakeSettings OceanWake::settings() const { return impl_->settings; }
OceanWakeGridView OceanWake::gridView() const { return {impl_->settings, impl_->time, impl_->height, impl_->foam}; }
}  // namespace eve::fluids
