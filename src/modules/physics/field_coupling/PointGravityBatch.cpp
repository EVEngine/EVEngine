// eve-hot-path
#include "physics/field_coupling/PointGravityBatch.h"
#include <cmath>
#include <limits>
#include <new>
#include <utility>
#include "physics/World3D.h"
#include "physics/field/GravityField.h"
#include "physics/forces/ForceBatch.h"

namespace eve::physics {
namespace {
template <class T>
Result<T> fail(DiagnosticCode code, const char* message) {
    return Result<T>::failure(Diagnostic::error(code, message, "physics.field.coupling"));
}
bool same(ForceVector a, ForceVector b) { return a.x == b.x && a.y == b.y && a.z == b.z; }
}  // namespace
struct PointGravityBatch::Impl {
    ForceBodySnapshot        observation;
    std::vector<PhysicsLink> targets;
    ForceBatch               forces;
    SimulationStep           step;
    bool                     submitted = false;
    Impl(ForceBodySnapshot snapshot, std::vector<PhysicsLink> links, ForceBatch batch, SimulationStep s)
        : observation(std::move(snapshot)), targets(std::move(links)), forces(std::move(batch)), step(s) {}
};
PointGravityBatch::PointGravityBatch(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
PointGravityBatch::~PointGravityBatch()                                       = default;
PointGravityBatch::PointGravityBatch(PointGravityBatch&&) noexcept            = default;
PointGravityBatch& PointGravityBatch::operator=(PointGravityBatch&&) noexcept = default;

Result<PointGravityBatch> PointGravityBatch::prepare(World3D& world, std::span<const PhysicsLink> bodies,
                                                     const field::GravityPolicy& policy, SimulationStep step) {
    if (bodies.empty() || bodies.size() > policy.maxParticles || step.delta <= Duration::zero())
        return fail<PointGravityBatch>(DiagnosticCode::InvalidArgument, "Invalid gravity targets, duration or budget");
    try {
        auto observed = ForceBatch::captureCenters(world, bodies);
        if (!observed) return Result<PointGravityBatch>::failure(observed.status());
        const auto next = observed.value().tick.incremented();
        if (!next || step.tick != *next)
            return fail<PointGravityBatch>(DiagnosticCode::Conflict, "Gravity requires the next simulation tick");
        std::vector<field::Particle> particles;
        std::vector<PhysicsLink>     targets;
        particles.reserve(bodies.size());
        targets.reserve(bodies.size());
        for (const auto& body : observed.value().bodies) {
            particles.push_back({{body.center.x, body.center.y, body.center.z}, body.mass});
            targets.push_back(body.body);
        }
        const field::FieldStamp stamp{observed.value().tick, Revision{1}};
        auto                    field = field::GravityField::build(particles, stamp, policy);
        if (!field) return Result<PointGravityBatch>::failure(field.status());
        auto samples = field.value().evaluate(stamp);
        if (!samples) return Result<PointGravityBatch>::failure(samples.status());
        std::vector<ForceLoad> loads;
        loads.reserve(bodies.size());
        for (std::size_t i = 0; i < particles.size(); ++i) {
            const auto& body = observed.value().bodies[i];
            const auto& a    = samples.value()[i].acceleration;
            for (double acceleration : a)
                if (acceleration != 0 && body.mass * acceleration == 0)
                    return fail<PointGravityBatch>(DiagnosticCode::Unsupported,
                                                   "Gravity load underflows before backend conversion");
            const ForceVector force{body.mass * a[0], body.mass * a[1], body.mass * a[2]};
            for (double component : {force.x, force.y, force.z}) {
                if (!std::isfinite(component) || std::abs(component) > std::numeric_limits<float>::max() ||
                    (component != 0 && static_cast<float>(component) == 0))
                    return fail<PointGravityBatch>(DiagnosticCode::Unsupported,
                                                   "Gravity load exceeds rigid backend force representation");
            }
            loads.push_back({body.body, force, body.center, {}});
        }
        auto forces = ForceBatch::prepare(world, loads);
        if (!forces) return Result<PointGravityBatch>::failure(forces.status());
        return Result<PointGravityBatch>::success(PointGravityBatch(
            std::make_unique<Impl>(std::move(observed.value()), std::move(targets), std::move(forces.value()), step)));
    } catch (const std::bad_alloc&) {
        return fail<PointGravityBatch>(DiagnosticCode::Failed, "Gravity preparation allocation failed");
    }
}
Result<ForceReceipt> PointGravityBatch::commit(World3D& world) {
    if (!impl_ || impl_->submitted)
        return fail<ForceReceipt>(DiagnosticCode::PreconditionViolation, "Gravity batch is moved or already submitted");
    if (world.runtimeHandle() != impl_->observation.world)
        return fail<ForceReceipt>(DiagnosticCode::StaleHandle, "Gravity batch belongs to another world");
    try {
        auto current = ForceBatch::captureCenters(world, impl_->targets);
        if (!current) return Result<ForceReceipt>::failure(current.status());
        const auto& prior = impl_->observation;
        if (current.value().tick != prior.tick || current.value().bodies.size() != prior.bodies.size())
            return fail<ForceReceipt>(DiagnosticCode::Conflict, "Gravity observation tick or membership changed");
        for (std::size_t i = 0; i < prior.bodies.size(); ++i) {
            const auto& a = prior.bodies[i];
            const auto& b = current.value().bodies[i];
            if (a.body.body != b.body.body || a.mass != b.mass || !same(a.center, b.center))
                return fail<ForceReceipt>(DiagnosticCode::Conflict, "Gravity source position or mass changed");
        }
        auto receipt = impl_->forces.commit(world, impl_->step);
        if (!receipt) return Result<ForceReceipt>::failure(receipt.status());
        impl_->submitted = true;
        return receipt;
    } catch (const std::bad_alloc&) {
        return fail<ForceReceipt>(DiagnosticCode::Failed, "Gravity submission allocation failed");
    }
}
}  // namespace eve::physics
