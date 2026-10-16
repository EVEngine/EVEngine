#include "fluids/OceanBoat.h"

#include "physics/Body3D.h"
#include "physics/PhysicsLink.h"
#include "physics/World3D.h"

#include <algorithm>
#include <cmath>
#include <glm/gtc/quaternion.hpp>
#include <limits>
#include <numbers>

namespace eve::fluids {
struct OceanBoat::Impl {
    physics::PhysicsLink link;
    OceanHullSettings    hull;
    double               lastTime = -std::numeric_limits<double>::infinity();
};
namespace {
bool finite(glm::dvec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
template <class T>
Result<T> failure(DiagnosticCode code, const char* message) {
    return Result<T>::failure(Diagnostic::error(code, message, "fluids.ocean.boat"));
}
glm::dvec3 vector(physics::PhysicsVector3D v) { return {v.x, v.y, v.z}; }
}  // namespace

OceanBoat::OceanBoat(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
OceanBoat::~OceanBoat() = default;

Result<std::unique_ptr<OceanBoat>> OceanBoat::create(physics::Body3D& body, const OceanHullSettings& s) {
    if (s.cells.empty() || s.cells.size() > 64 || !std::isfinite(s.waterDensity) || s.waterDensity <= 0.0 ||
        s.waterDensity > 1e5 || !std::isfinite(s.gravity) || s.gravity <= 0.0 || s.gravity > 100.0 || !finite(s.drag) ||
        glm::any(glm::lessThan(s.drag, glm::dvec3(0.0))) || glm::length(s.drag) > 100.0 || !finite(s.propeller) ||
        glm::length(s.propeller) > 1000.0 || !std::isfinite(s.thrust) || s.thrust < 0.0 || s.thrust > 1e8 ||
        !std::isfinite(s.rudderCoefficient) || s.rudderCoefficient < 0.0 || s.rudderCoefficient > 1e6)
        return failure<std::unique_ptr<OceanBoat>>(DiagnosticCode::InvalidArgument, "Invalid hull settings");
    for (const auto& c : s.cells) {
        if (!finite(c.center) || glm::length(c.center) > 1000.0 || !std::isfinite(c.radius) || c.radius <= 0.0 ||
            c.radius > 100.0 || !std::isfinite(c.volume) || c.volume <= 0.0 || c.volume > 1e6)
            return failure<std::unique_ptr<OceanBoat>>(DiagnosticCode::InvalidArgument, "Invalid hull cell");
    }
    auto link = physics::PhysicsLink::fromBody(body);
    if (!link) return Result<std::unique_ptr<OceanBoat>>::failure(link.status());
    if (body.getType() != "dynamic" || !std::isfinite(body.getMass()) || body.getMass() <= 0.0F)
        return failure<std::unique_ptr<OceanBoat>>(DiagnosticCode::InvalidArgument,
                                                   "Boat requires a dynamic body with positive mass");
    auto impl  = std::make_unique<Impl>();
    impl->link = link.value();
    impl->hull = s;
    return Result<std::unique_ptr<OceanBoat>>::success(std::unique_ptr<OceanBoat>(new OceanBoat(std::move(impl))));
}

Result<OceanBoatReceipt> OceanBoat::apply(const OceanWaterView& frame, double dt, double throttle, double rudder) {
    if (!std::isfinite(dt) || dt <= 0.0 || dt > 1.0 / 30.0 || !std::isfinite(throttle) || std::abs(throttle) > 1.0 ||
        !std::isfinite(rudder) || std::abs(rudder) > 1.0 || frame.time() <= impl_->lastTime)
        return failure<OceanBoatReceipt>(DiagnosticCode::InvalidArgument, "Invalid or repeated boat step");
    auto* world = physics::World3D::findWorld(impl_->link.world);
    if (!world) return failure<OceanBoatReceipt>(DiagnosticCode::StaleHandle, "Boat world no longer exists");
    auto resolved = impl_->link.resolve(*world);
    if (!resolved) return Result<OceanBoatReceipt>::failure(resolved.status());
    auto&            body = *resolved.value();
    const auto&      h    = impl_->hull;
    const glm::dquat rotation{body.getRotW(), body.getRotX(), body.getRotY(), body.getRotZ()};
    const auto       inverse = glm::conjugate(rotation);
    const double     mass    = body.getMass();
    if (!std::isfinite(mass) || mass <= 0.0 || body.getType() != "dynamic")
        return failure<OceanBoatReceipt>(DiagnosticCode::InvalidArgument, "Boat body is no longer dynamic");
    OceanBoatReceipt receipt;
    receipt.time = frame.time();
    std::vector<OceanCellForce> forces;
    forces.reserve(h.cells.size() + 1);
    receipt.contacts.reserve(h.cells.size());
    for (const auto& cell : h.cells) {
        auto p = body.localToWorldPointOwned(float(cell.center.x), float(cell.center.y), float(cell.center.z));
        if (!p) return Result<OceanBoatReceipt>::failure(p.status());
        auto v = body.getLocalPointVelocityOwned(float(cell.center.x), float(cell.center.y), float(cell.center.z));
        if (!v) return Result<OceanBoatReceipt>::failure(v.status());
        const auto position = vector(p.value()), velocity = vector(v.value());
        auto       water = frame.sample({position.x, position.z});
        if (!water) return Result<OceanBoatReceipt>::failure(water.status());
        OceanBuoyancyCell probe{position, velocity, cell.radius, cell.volume, 0.0};
        auto              force = evaluateOceanCellForce(probe, water.value(), h.waterDensity, h.gravity);
        if (!force) return Result<OceanBoatReceipt>::failure(force.status());
        const auto   relative = velocity - water.value().velocity;
        const auto   local    = inverse * relative;
        const double fraction = force.value().submergedVolume / cell.volume;
        const double area     = std::numbers::pi * cell.radius * cell.radius * fraction;
        auto         drag     = -0.5 * h.waterDensity * area * h.drag * glm::abs(local) * local;
        // Cap each cell's dissipative impulse to its share of body momentum.
        for (int axis = 0; axis < 3; ++axis) {
            const double bound = mass * std::abs(local[axis]) / (dt * double(h.cells.size()));
            drag[axis]         = std::clamp(drag[axis], -bound, bound);
        }
        force.value().force += rotation * drag;
        receipt.submergedVolume += force.value().submergedVolume;
        receipt.contacts.push_back({position, relative, force.value().submergedVolume});
        forces.push_back(force.value());
    }
    auto propeller = body.localToWorldPointOwned(float(h.propeller.x), float(h.propeller.y), float(h.propeller.z));
    if (!propeller) return Result<OceanBoatReceipt>::failure(propeller.status());
    const auto p     = vector(propeller.value());
    auto       water = frame.sample({p.x, p.z});
    if (!water) return Result<OceanBoatReceipt>::failure(water.status());
    auto propVelocity =
        body.getLocalPointVelocityOwned(float(h.propeller.x), float(h.propeller.y), float(h.propeller.z));
    if (!propVelocity) return Result<OceanBoatReceipt>::failure(propVelocity.status());
    const auto   relative = inverse * (vector(propVelocity.value()) - water.value().velocity);
    const double wet      = std::clamp((water.value().height - p.y) / 0.3, 0.0, 1.0);
    const double rudderForce =
        std::clamp(-rudder * h.rudderCoefficient * relative.z * std::abs(relative.z), -mass * 10.0, mass * 10.0);
    forces.push_back({p, rotation * (glm::dvec3(rudderForce, 0.0, throttle * h.thrust) * wet), 0.0});
    for (const auto& force : forces) {
        if (!finite(force.force) || glm::length(force.force) > 1e12)
            return failure<OceanBoatReceipt>(DiagnosticCode::InvalidArgument, "Hull force exceeds supported range");
        receipt.totalForce += force.force;
    }
    // No fallible operations or external callbacks after the first force is applied.
    for (const auto& f : forces)
        body.applyForceAt(float(f.force.x), float(f.force.y), float(f.force.z), float(f.position.x),
                          float(f.position.y), float(f.position.z));
    impl_->lastTime = frame.time();
    return Result<OceanBoatReceipt>::success(std::move(receipt));
}
}  // namespace eve::fluids
