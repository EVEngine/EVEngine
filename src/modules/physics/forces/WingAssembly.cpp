#include "physics/forces/WingAssembly.h"
#include <Box2D/Box2D.h>
#include <box3d/box3d.h>
#include <numbers>
#include <type_traits>
#include "physics/Body.h"
#include "physics/Body3D.h"
#include "physics/World.h"
#include "physics/World3D.h"
#include "physics/forces/ForceMath.h"

namespace eve::physics {
namespace {
using namespace force_detail;
bool fits(ForceVector v) {
    constexpr auto limit = std::numeric_limits<float>::max();
    return isFinite(v) && std::abs(v.x) <= limit && std::abs(v.y) <= limit && std::abs(v.z) <= limit;
}
ForceVector point(const Body& body, ForceVector p) {
    const auto v = body.raw()->GetWorldPoint({float(p.x), float(p.y)});
    return {v.x, v.y, 0};
}
ForceVector point(const Body3D& body, ForceVector p) {
    const auto v = b3RotateVector(b3Quat{body.getRotX(), body.getRotY(), body.getRotZ(), body.getRotW()},
                                  {float(p.x), float(p.y), float(p.z)});
    return {v.x + body.getX(), v.y + body.getY(), v.z + body.getZ()};
}
ForceVector direction(const Body& body, ForceVector p) {
    const auto v = body.raw()->GetWorldVector({float(p.x), float(p.y)});
    return {v.x, v.y, 0};
}
ForceVector direction(const Body3D& body, ForceVector p) {
    const auto v = b3RotateVector(b3Quat{body.getRotX(), body.getRotY(), body.getRotZ(), body.getRotW()},
                                  {float(p.x), float(p.y), float(p.z)});
    return {v.x, v.y, v.z};
}
ForceVector velocity(const Body& body, ForceVector p) {
    const auto v = body.raw()->GetLinearVelocityFromWorldPoint({float(p.x), float(p.y)});
    return {v.x, v.y, 0};
}
ForceVector velocity(const Body3D& body, ForceVector p) {
    const ForceVector r{p.x - body.getWorldCenterX(), p.y - body.getWorldCenterY(), p.z - body.getWorldCenterZ()};
    const ForceVector w{body.getAngularVelocityX(), body.getAngularVelocityY(), body.getAngularVelocityZ()};
    return {body.getLinearVelocityX() + w.y * r.z - w.z * r.y, body.getLinearVelocityY() + w.z * r.x - w.x * r.z,
            body.getLinearVelocityZ() + w.x * r.y - w.y * r.x};
}
template <class W>
Result<ForceBatch> prepareSurfaces(W& world, PhysicsLink link, std::span<const WingSurface> surfaces,
                                   std::span<const double> controls, ForceVector wind) {
    constexpr bool planar = std::is_same_v<W, World>;
    if (controls.size() != surfaces.size() || !isFinite(wind) || (planar && wind.z != 0)) return invalid<ForceBatch>();
    auto resolved = link.resolve(world);
    if (!resolved) return Result<ForceBatch>::failure(resolved.status());
    auto& body = *resolved.value();
    if (!body.isActive() || body.getType() != "dynamic") return invalid<ForceBatch>();
    std::vector<ForceLoad> loads;
    loads.reserve(surfaces.size());
    for (std::size_t i = 0; i < surfaces.size(); ++i) {
        const auto& surface = surfaces[i];
        if (!isFinite(controls[i]) ||
            (planar && (surface.center.z != 0 || surface.model.chord.z != 0 || surface.model.normal.z != 0)))
            return invalid<ForceBatch>();
        const double angle = std::remainder(controls[i], 2 * std::numbers::pi);
        const double c = std::cos(angle), s = std::sin(angle);
        auto         model = surface.model;
        model.chord        = direction(body, add(scale(surface.model.chord, c), scale(surface.model.normal, s)));
        model.normal       = direction(body, add(scale(surface.model.normal, c), scale(surface.model.chord, -s)));
        const auto center  = point(body, surface.center);
        if (!fits(center)) return invalid<ForceBatch>();
        const auto relative = add(velocity(body, center), scale(wind, -1));
        auto       sampled  = ForceModels::wing(model, relative);
        if (!sampled) return Result<ForceBatch>::failure(sampled.status());
        loads.push_back({link, sampled.value().force, center, {}});
    }
    return ForceBatch::prepare(world, loads);
}
}  // namespace
Result<WingAssembly> WingAssembly::build(std::span<const WingSurface> surfaces) {
    for (const auto& surface : surfaces) {
        if (!fits(surface.center)) return invalid<WingAssembly>();
        auto validated = ForceModels::wing(surface.model, {});
        if (!validated) return Result<WingAssembly>::failure(validated.status());
    }
    return Result<WingAssembly>::success(WingAssembly(surfaces));
}
Result<ForceBatch> WingAssembly::prepare(World& world, PhysicsLink body, std::span<const double> controls,
                                         ForceVector wind) const {
    return prepareSurfaces(world, body, surfaces_, controls, wind);
}
Result<ForceBatch> WingAssembly::prepare(World3D& world, PhysicsLink body, std::span<const double> controls,
                                         ForceVector wind) const {
    return prepareSurfaces(world, body, surfaces_, controls, wind);
}
}  // namespace eve::physics
