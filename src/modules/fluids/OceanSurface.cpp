#include "fluids/OceanSurface.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace eve::fluids {
namespace {
constexpr double tau = 2.0 * std::numbers::pi;

bool finite(glm::dvec2 v) { return std::isfinite(v.x) && std::isfinite(v.y); }
bool finite(glm::dvec3 v) { return finite(glm::dvec2(v)) && std::isfinite(v.z); }
bool validQuery(glm::dvec2 p, double t) {
    return finite(p) && std::abs(p.x) <= 1e9 && std::abs(p.y) <= 1e9 && std::isfinite(t) && std::abs(t) <= 1e9;
}
template <class T>
Result<T> invalid() {
    return Result<T>::failure(
        Diagnostic::error(DiagnosticCode::InvalidArgument, "Invalid ocean input", "fluids.ocean"));
}

struct Evaluation {
    glm::dvec3 position{0.0};
    glm::dvec3 tangentX{1.0, 0.0, 0.0};
    glm::dvec3 tangentZ{0.0, 0.0, 1.0};
    glm::dvec3 velocity{0.0};
};

Evaluation evaluate(const OceanSurfaceSettings& s, glm::dvec2 q, double time) {
    Evaluation e;
    e.position = {q.x, s.seaLevel, q.y};
    e.velocity = {s.current.x, 0.0, s.current.y};
    for (const auto& wave : s.waves) {
        const double k     = tau / wave.wavelength;
        const double omega = std::sqrt(s.gravity * k);
        const auto   d     = wave.direction;
        const double phase = std::remainder(k * glm::dot(d, q - s.current * time) - omega * time + wave.phase, tau);
        const double sn = std::sin(phase), cs = std::cos(phase);
        const double horizontal = wave.choppiness * wave.amplitude;
        e.position += glm::dvec3(horizontal * d.x * cs, wave.amplitude * sn, horizontal * d.y * cs);
        e.tangentX += glm::dvec3(-horizontal * k * d.x * d.x * sn, wave.amplitude * k * d.x * cs,
                                 -horizontal * k * d.x * d.y * sn);
        e.tangentZ += glm::dvec3(-horizontal * k * d.x * d.y * sn, wave.amplitude * k * d.y * cs,
                                 -horizontal * k * d.y * d.y * sn);
        // Material derivative follows the uniform current, cancelling its phase advection.
        e.velocity +=
            glm::dvec3(horizontal * omega * d.x * sn, -wave.amplitude * omega * cs, horizontal * omega * d.y * sn);
    }
    return e;
}
}  // namespace

Result<OceanSurface> OceanSurface::create(const OceanSurfaceSettings& settings) {
    if (!std::isfinite(settings.seaLevel) || std::abs(settings.seaLevel) > 1e6 || !std::isfinite(settings.gravity) ||
        settings.gravity <= 0.0 || settings.gravity > 100.0 || !finite(settings.current) ||
        glm::length(settings.current) > 100.0 || settings.waves.size() > 64)
        return invalid<OceanSurface>();
    auto   normalized = settings;
    double steepness  = 0.0;
    for (auto& w : normalized.waves) {
        if (!finite(w.direction) || glm::length(w.direction) < 1e-8 || glm::length(w.direction) > 1e6 ||
            !std::isfinite(w.amplitude) || w.amplitude < 0.0 || w.amplitude > 100.0 || !std::isfinite(w.wavelength) ||
            w.wavelength < 0.05 || w.wavelength > 1e6 || !std::isfinite(w.phase) || std::abs(w.phase) > 1e9 ||
            !std::isfinite(w.choppiness) || w.choppiness < 0.0 || w.choppiness > 1.0)
            return invalid<OceanSurface>();
        w.direction = glm::normalize(w.direction);
        steepness += w.choppiness * w.amplitude * tau / w.wavelength;
    }
    if (steepness > 0.75) return invalid<OceanSurface>();
    return Result<OceanSurface>::success(OceanSurface(std::move(normalized)));
}

Result<glm::dvec3> OceanSurface::position(glm::dvec2 parameterXZ, double time) const {
    if (!validQuery(parameterXZ, time)) return invalid<glm::dvec3>();
    return Result<glm::dvec3>::success(evaluate(settings_, parameterXZ, time).position);
}

Result<OceanSurfaceSample> OceanSurface::sample(glm::dvec2 worldXZ, double time) const {
    if (!validQuery(worldXZ, time)) return invalid<OceanSurfaceSample>();
    auto q = worldXZ;
    // The admitted displacement is a contraction (Lipschitz <= .75). This
    // safeguarded fixed-point solve cannot jump to a different folded branch.
    for (int i = 0; i < 96; ++i) {
        const auto e        = evaluate(settings_, q, time);
        const auto residual = glm::dvec2(e.position.x, e.position.z) - worldXZ;
        q -= residual;
        if (glm::length(residual) < 1e-7) break;
    }
    const auto         e = evaluate(settings_, q, time);
    OceanSurfaceSample sample;
    sample.height             = e.position.y;
    sample.normal             = glm::normalize(glm::cross(e.tangentZ, e.tangentX));
    sample.velocity           = e.velocity;
    sample.parameterPosition  = q;
    sample.time               = time;
    sample.horizontalJacobian = e.tangentX.x * e.tangentZ.z - e.tangentX.z * e.tangentZ.x;
    return Result<OceanSurfaceSample>::success(sample);
}

Result<OceanCellForce> evaluateOceanCellForce(const OceanBuoyancyCell& cell, const OceanSurfaceSample& water,
                                              double density, double gravity) {
    if (!finite(cell.center) || !finite(cell.pointVelocity) || glm::length(cell.center) > 1e9 ||
        glm::length(cell.pointVelocity) > 1e4 || !std::isfinite(cell.radius) || cell.radius <= 0.0 ||
        cell.radius > 1e4 || !std::isfinite(cell.volume) || cell.volume <= 0.0 || cell.volume > 1e9 ||
        !std::isfinite(cell.dragCoefficient) || cell.dragCoefficient < 0.0 || cell.dragCoefficient > 100.0 ||
        !std::isfinite(water.height) || std::abs(water.height) > 1e9 || !finite(water.velocity) ||
        glm::length(water.velocity) > 1e4 || !std::isfinite(density) || density <= 0.0 || density > 1e5 ||
        !std::isfinite(gravity) || gravity <= 0.0 || gravity > 100.0)
        return invalid<OceanCellForce>();
    const double   depth    = std::clamp(water.height - cell.center.y + cell.radius, 0.0, 2.0 * cell.radius);
    const double   h        = depth / cell.radius;
    const double   fraction = h * h * (3.0 - h) * 0.25;
    OceanCellForce result;
    result.position        = cell.center;
    result.submergedVolume = cell.volume * fraction;
    const auto   relative  = cell.pointVelocity - water.velocity;
    const double area      = std::numbers::pi * cell.radius * cell.radius * fraction;
    result.force           = -0.5 * density * cell.dragCoefficient * area * glm::length(relative) * relative;
    result.force.y += density * gravity * result.submergedVolume;
    return Result<OceanCellForce>::success(result);
}
}  // namespace eve::fluids
