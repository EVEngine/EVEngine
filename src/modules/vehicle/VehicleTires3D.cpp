// eve-hot-path
#include <box3d/box3d.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include "physics/Body3D.h"
#include "physics/Shape3D.h"
#include "physics/World3D.h"
#include "physics/box3d/Box3DBody.h"
#include "physics/forces/ForceBatch.h"
#include "vehicle/VehiclePhysics.h"
#include "vehicle/VehicleTireRuntime.h"

namespace eve::vehicle::tire_detail {
namespace {
using V = physics::ForceVector;
template <class T>
V vec(T v) {
    return {v.x, v.y, v.z};
}
V      add(V a, V b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
V      scale(V a, double k) { return {a.x * k, a.y * k, a.z * k}; }
double dot(V a, V b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V      cross(V a, V b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
double length(V a) { return std::hypot(a.x, a.y, a.z); }
bool   finite(V a) { return std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(a.z); }
bool   positive(double x) { return std::isfinite(x) && x > 0; }
bool   nonnegative(double x) { return std::isfinite(x) && x >= 0; }
bool   fits(V a) {
    const double limit = std::numeric_limits<float>::max();
    return finite(a) && std::abs(a.x) <= limit && std::abs(a.y) <= limit && std::abs(a.z) <= limit;
}
b3Vec3 native(V v) { return {float(v.x), float(v.y), float(v.z)}; }
V      multiply(b3Matrix3 m, V a) {
    return {m.cx.x * a.x + m.cy.x * a.y + m.cz.x * a.z, m.cx.y * a.x + m.cy.y * a.y + m.cz.y * a.z,
                 m.cx.z * a.x + m.cy.z * a.y + m.cz.z * a.z};
}
Result<VehiclePhysicsStatus> reject(std::string message) {
    return Result<VehiclePhysicsStatus>::failure(
        Diagnostic::error(DiagnosticCode::InvalidArgument, std::move(message), "vehicle.tires3d"));
}
struct Contact {
    physics::Body3D*  ground = nullptr;
    V                 point{}, normal{};
    b3SurfaceMaterial material{};
    float             fraction = 1;
    int               shapeId  = 0;
};
struct RayContext {
    physics::Body3D& chassis;
    V                up;
    Contact          contact;
    // Only this synchronous native callback sees the borrowed wrappers. No user callbacks or query-cache mutation.
    static float collect(b3ShapeId id, b3Pos point, b3Vec3 normal, float fraction, uint64_t, int triangle, int,
                         void* opaque) {
        auto& context = *static_cast<RayContext*>(opaque);
        if (b3Shape_IsSensor(id)) return -1;
        auto* shape  = static_cast<physics::Shape3D*>(b3Shape_GetUserData(id));
        auto* ground = static_cast<physics::Body3D*>(b3Body_GetUserData(b3Shape_GetBody(id)));
        if (!shape || !ground || ground == &context.chassis || !ground->isValid() || !shape->isValid()) return -1;
        const auto n = vec(normal);
        if (dot(n, context.up) <= 1e-4) return -1;
        auto& hit = context.contact;
        if (!hit.ground || fraction < hit.fraction || (fraction == hit.fraction && shape->getId() < hit.shapeId)) {
            auto material = b3Shape_GetSurfaceMaterial(id);
            if (triangle >= 0 && triangle < shape->getTriangleMeshTriangleCount()) {
                const int slot = shape->getTriangleMeshMaterialIndex(triangle);
                if (slot >= 0 && slot < b3Shape_GetMeshMaterialCount(id))
                    material = b3Shape_GetMeshSurfaceMaterial(id, slot);
            }
            // Conveyor velocity is local to the shape, then projected onto the hit plane.
            const b3Quat local{shape->getLocalRotX(), shape->getLocalRotY(), shape->getLocalRotZ(),
                               shape->getLocalRotW()};
            material.tangentVelocity = b3Body_GetWorldVector(eve::physics::box3d::Access::bodyId(*ground),
                                                             b3RotateVector(local, material.tangentVelocity));
            hit                      = {ground, vec(point), n, material, fraction, shape->getId()};
        }
        return hit.fraction;
    }
};
double response(physics::Body3D& body, V point, V a, V b) {
    if (b3Body_GetType(eve::physics::box3d::Access::bodyId(body)) != b3_dynamicBody) return 0;
    const auto r = add(point, scale(vec(b3Body_GetWorldCenterOfMass(eve::physics::box3d::Access::bodyId(body))), -1));
    const auto inertia = b3Body_GetWorldInverseRotationalInertia(eve::physics::box3d::Access::bodyId(body));
    return b3Body_GetInverseMass(eve::physics::box3d::Access::bodyId(body)) * dot(a, b) +
           dot(cross(r, a), multiply(inertia, cross(r, b)));
}
// Numeric predictor is local to one submission; backend bodies are never mutated here.
struct PredictedBody {
    physics::Body3D* body;
    V                center, freeLinear, freeAngular, linear, angular;
    b3Matrix3        inverseInertia;
    double           inverseMass;
    V                velocity(V point) const { return add(linear, cross(angular, add(point, scale(center, -1)))); }
    void             impulse(V point, V impulse) {
        linear  = add(linear, scale(impulse, inverseMass));
        angular = add(angular, multiply(inverseInertia, cross(add(point, scale(center, -1)), impulse)));
    }
};
struct PreparedWheel {
    Contact               contact;
    V                     point, forward, lateral, normal, conveyor;
    std::size_t           ground = 0;
    std::array<double, 3> diagonal{}, impulse{};
    double                compression = 0, spring = 0, gain = 0;
    physics::TireInput    input;
    physics::TireModel    model;
};
}  // namespace
Result<VehiclePhysicsStatus> submit3D(VehicleEntity& vehicle, physics::Body3D& body,
                                      std::shared_ptr<VehicleWheelRuntime>& runtime, const SimulationStep& step) {
    using namespace physics;
    auto pending = submissionState(runtime, step);
    if (!pending || pending.value() != VehiclePhysicsStatus::Unavailable) return pending;
    const auto* definition = vehicle.definition()->def;
    auto*       world      = body.getWorld();
    if (!definition || !world || !body.isValid()) return reject("Vehicle definition or spatial body is unavailable");
    auto phase = ForceBatch::prepare(*world, {});
    if (!phase) return Result<VehiclePhysicsStatus>::failure(phase.status());
    if (world->rigidBackendKind() != physics::RigidBackend3D::Box3D)
        return Result<VehiclePhysicsStatus>::failure(
            Diagnostic::error(DiagnosticCode::Unsupported,
                              "Ray tire contact integration currently requires the Box3D backend", "vehicle.tires3d"));
    const auto&  config = definition->suspension;
    const auto   input  = *vehicle.input();
    const double dt = step.delta.seconds(), mass = body.getMass();
    if (!positive(dt) || dt > .05 || !positive(mass) || !nonnegative(definition->maxSpeed) ||
        !nonnegative(definition->accel) || !nonnegative(config.maxTravel) || !nonnegative(config.driveForce) ||
        !nonnegative(config.brakeTorque) || !nonnegative(config.maxSteerAngle) || config.maxSteerAngle > 1.4 ||
        !std::isfinite(input.throttle) || !std::isfinite(input.steer) || !std::isfinite(input.brake))
        return reject("Invalid spatial tire input, suspension travel or fixed duration");
    auto wheels = config.wheels;
    if (wheels.empty()) {
        if (!positive(definition->radius))
            return reject("Default spatial wheel layout requires a positive chassis size");
        for (int x : {-1, 1})
            for (int z : {-1, 1}) {
                SuspensionWheel wheel;
                wheel.x     = float(x) * definition->radius * .45f;
                wheel.z     = float(z) * definition->radius * .6f;
                wheel.y     = -.175f;
                wheel.steer = z > 0;
                wheels.push_back(wheel);
            }
    }
    std::vector<VehicleEntity::SuspensionState::WheelState> candidate(wheels.size());
    if (vehicle.suspension()->wheels.size() == wheels.size()) candidate = vehicle.suspension()->wheels;
    const double count = static_cast<double>(wheels.size());
    const auto   driven =
        static_cast<std::size_t>(std::count_if(wheels.begin(), wheels.end(), [](const auto& w) { return w.drive; }));
    const auto up = vec(b3Body_GetWorldVector(eve::physics::box3d::Access::bodyId(body), {0, 1, 0}));
    if (!finite(up)) return reject("Invalid suspension orientation");
    auto chassisLink = PhysicsLink::fromBody(body);
    if (!chassisLink) return Result<VehiclePhysicsStatus>::failure(chassisLink.status());
    std::vector<ForceLoad> loads;
    loads.reserve(wheels.size() * 2);
    std::vector<PredictedBody> predicted;
    auto                       predict = [&](physics::Body3D& source) {
        for (std::size_t i = 0; i < predicted.size(); ++i)
            if (predicted[i].body == &source) return i;
        const auto raw     = physics::box3d::Access::bodyId(source);
        const bool dynamic = b3Body_GetType(raw) == b3_dynamicBody;
        const V    center  = vec(b3Body_GetWorldCenterOfMass(raw));
        V          linear = vec(b3Body_GetLinearVelocity(raw)), angular = vec(b3Body_GetAngularVelocity(raw));
        if (dynamic)
            linear = add(linear, scale(V{world->getGravityX(), world->getGravityY(), world->getGravityZ()},
                                                             dt * source.getGravityScale()));
        predicted.push_back({&source, center, linear, angular, linear, angular,
                             dynamic ? b3Body_GetWorldInverseRotationalInertia(raw) : b3Matrix3{},
                             dynamic ? b3Body_GetInverseMass(raw) : 0});
        return predicted.size() - 1;
    };
    predict(body);
    std::vector<PreparedWheel> contacts;
    contacts.reserve(wheels.size());
    for (std::size_t i = 0; i < wheels.size(); ++i) {
        const auto&  wheel = wheels[i];
        const V      localMount{wheel.x, double(wheel.y) + wheel.restLength, wheel.z};
        const double rayLength = double(wheel.restLength) + wheel.radius + config.maxTravel;
        if (!fits(localMount) || !positive(wheel.radius) || !nonnegative(wheel.restLength) ||
            !nonnegative(wheel.stiffness) || !nonnegative(wheel.damping) || !positive(rayLength))
            return reject("Invalid spatial wheel geometry or spring parameters");
        const auto mount       = b3Body_GetWorldPoint(eve::physics::box3d::Access::bodyId(body), native(localMount));
        const auto translation = scale(up, -rayLength);
        if (!fits(vec(mount)) || !fits(translation) || !fits(add(vec(mount), translation)))
            return reject("Suspension ray exceeds supported coordinate range");
        RayContext ray{body, up, {}};
        b3World_CastRay(eve::physics::box3d::Access::worldId(*world), mount, native(translation),
                        b3DefaultQueryFilter(), &RayContext::collect, &ray);
        const auto&  contact = ray.contact;
        const double steer   = wheel.steer ? std::clamp(double(input.steer), -1.0, 1.0) * config.maxSteerAngle : 0;
        auto         forward = vec(b3Body_GetWorldVector(eve::physics::box3d::Access::bodyId(body),
                                                         {float(std::sin(steer)), 0, float(std::cos(steer))}));
        V            normal  = contact.ground ? contact.normal : up;
        forward              = add(forward, scale(normal, -dot(forward, normal)));
        const double norm    = length(forward);
        if (!positive(norm) || norm < 1e-6) return reject("Wheel heading is perpendicular to the contact plane");
        forward             = scale(forward, 1 / norm);
        const auto lateral  = cross(normal, forward);
        const V    point    = contact.ground ? contact.point : vec(mount);
        auto       relative = vec(b3Body_GetWorldPointVelocity(eve::physics::box3d::Access::bodyId(body),
                                                               {float(point.x), float(point.y), float(point.z)}));
        if (contact.ground) {
            relative            = add(relative,
                                      scale(vec(b3Body_GetWorldPointVelocity(eve::physics::box3d::Access::bodyId(*contact.ground),
                                                                             {float(point.x), float(point.y), float(point.z)})),
                                            -1));
            const auto conveyor = vec(contact.material.tangentVelocity);
            relative            = add(relative, scale(add(conveyor, scale(normal, -dot(conveyor, normal))), -1));
        }
        std::array<V, 3> axes{forward, lateral, normal};
        double           matrix[3][3]{};
        for (int a = 0; a < 3; ++a)
            for (int b = 0; b < 3; ++b) {
                matrix[a][b] = response(body, point, axes[a], axes[b]);
                if (contact.ground) matrix[a][b] += response(*contact.ground, point, axes[a], axes[b]);
                if (!std::isfinite(matrix[a][b])) return reject("Invalid spatial contact inertia");
            }
        double diagonal[3]{};
        for (int a = 0; a < 3; ++a) {
            diagonal[a] = matrix[a][a];
            for (int b = 0; b < 3; ++b)
                if (b != a) diagonal[a] += std::abs(matrix[a][b]);
            diagonal[a] *= count;
            if (!nonnegative(diagonal[a])) return reject("Invalid contact response bound");
        }
        PreparedWheel prepared;
        prepared.contact  = contact;
        prepared.point    = point;
        prepared.forward  = forward;
        prepared.lateral  = lateral;
        prepared.normal   = normal;
        prepared.diagonal = {diagonal[0], diagonal[1], diagonal[2]};
        if (contact.ground) {
            prepared.ground      = predict(*contact.ground);
            const auto conveyor  = vec(contact.material.tangentVelocity);
            prepared.conveyor    = add(conveyor, scale(normal, -dot(conveyor, normal)));
            prepared.compression = std::clamp(double(wheel.restLength) - (contact.fraction * rayLength - wheel.radius),
                                              0.0, double(config.maxTravel));
            const double alpha   = dot(normal, up);
            prepared.spring      = wheel.stiffness * prepared.compression / alpha;
            prepared.gain        = (wheel.damping + wheel.stiffness * dt) / (alpha * alpha);
        }
        prepared.input.angularVelocity         = candidate[i].angularVelocity;
        prepared.input.longitudinalVelocity    = dot(relative, forward);
        prepared.input.lateralVelocity         = dot(relative, lateral);
        prepared.input.longitudinalInverseMass = diagonal[0];
        prepared.input.lateralInverseMass      = diagonal[1];
        driveInputs(prepared.input, config, input, wheel, driven, definition->maxSpeed,
                    std::min(double(config.driveForce), mass * definition->accel));
        prepared.model = model(config, wheel.radius);
        if (contact.ground)
            prepared.model.friction = std::min(prepared.model.friction, double(contact.material.friction));
        contacts.push_back(prepared);
    }
    // Bounded Jacobi iterations use the diagonal majorant as a preconditioner.
    // Every sample starts with the original wheel speed: drive/brake torque is integrated once.
    for (int iteration = 0; iteration < 64; ++iteration) {
        for (std::size_t i = 0; i < contacts.size(); ++i) {
            auto& c        = contacts[i];
            V     relative = predicted[0].velocity(c.point);
            if (c.contact.ground) relative = add(relative, scale(predicted[c.ground].velocity(c.point), -1));
            relative        = add(relative, scale(c.conveyor, -1));
            const double vn = dot(relative, c.normal) - c.diagonal[2] * c.impulse[2];
            const double load =
                c.contact.ground ? std::max(0.0, (c.spring - c.gain * vn) / (1 + dt * c.gain * c.diagonal[2])) : 0;
            if (!nonnegative(load)) return reject("Suspension force is not representable");
            auto ti                 = c.input;
            ti.longitudinalVelocity = dot(relative, c.forward) - c.diagonal[0] * c.impulse[0];
            ti.lateralVelocity      = dot(relative, c.lateral) - c.diagonal[1] * c.impulse[1];
            ti.normalLoad           = load;
            auto sampled            = ForceModels::tire(c.model, ti, dt);
            if (!sampled) return Result<VehiclePhysicsStatus>::failure(sampled.status());
            const auto& value       = sampled.value();
            c.impulse               = {value.longitudinalForce * dt, value.lateralForce * dt, load * dt};
            auto& state             = candidate[i];
            state.angularVelocity   = value.angularVelocity;
            state.longitudinalForce = value.longitudinalForce;
            state.lateralForce      = value.lateralForce;
            state.slipRatio         = value.slipRatio;
            state.slipAngle         = value.slipAngle;
            state.normalLoad        = load;
            state.prevCompression   = float(c.compression);
            state.grounded          = load > 0;
        }
        for (auto& value : predicted) {
            value.linear  = value.freeLinear;
            value.angular = value.freeAngular;
        }
        for (const auto& c : contacts) {
            const auto impulse =
                add(scale(c.normal, c.impulse[2]), add(scale(c.forward, c.impulse[0]), scale(c.lateral, c.impulse[1])));
            predicted[0].impulse(c.point, impulse);
            if (c.contact.ground) predicted[c.ground].impulse(c.point, scale(impulse, -1));
        }
    }
    for (const auto& c : contacts) {
        const auto force = scale(
            add(scale(c.normal, c.impulse[2]), add(scale(c.forward, c.impulse[0]), scale(c.lateral, c.impulse[1]))),
            1 / dt);
        loads.push_back({chassisLink.value(), force, c.point, {}});
        if (c.contact.ground && predicted[c.ground].inverseMass > 0) {
            auto link = PhysicsLink::fromBody(*c.contact.ground);
            if (!link) return Result<VehiclePhysicsStatus>::failure(link.status());
            loads.push_back({link.value(), scale(force, -1), c.point, {}});
        }
    }
    auto batch = ForceBatch::prepare(*world, loads);
    if (!batch) return Result<VehiclePhysicsStatus>::failure(batch.status());
    return commit(*world, batch.value(), runtime, std::move(candidate), step);
}
}  // namespace eve::vehicle::tire_detail
