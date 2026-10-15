#include "physics/mechanics/Coupling3DBindings.h"
#include <simplesquirrel/simplesquirrel.hpp>
#include "common/SquirrelBinding.h"
#include "common/SquirrelOwnership.h"
#include "physics/Body3D.h"
#include "physics/Joint3D.h"
#include "physics/World3D.h"
#include "physics/mechanics/Coupling3D.h"

namespace eve::physics {
namespace {
template <class T>
T* borrowed(const ssq::Object& object) {
    if (object.getType() != ssq::Type::INSTANCE || object.getTypeTag() != typeid(T*).hash_code()) return nullptr;
    try {
        return object.to<T*>();
    } catch (const ssq::TypeException&) {
        return nullptr;
    }
}
template <class T>
T* receiver(HSQUIRRELVM vm, T* value) {
    SQUserPointer tag = nullptr;
    if (sq_gettype(vm, 1) != OT_INSTANCE || SQ_FAILED(sq_gettypetag(vm, 1, &tag)) ||
        reinterpret_cast<std::size_t>(tag) != typeid(T*).hash_code())
        return nullptr;
    return value;
}
ssq::Table invalid(HSQUIRRELVM vm) {
    return script::projectResult(
        vm, Result<void>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "Expected typed transmission inputs", "physics.coupling3d.script")));
}
template <class T>
ssq::Table own(HSQUIRRELVM vm, Result<std::unique_ptr<T>> result) {
    if (!result) return script::projectStatusResult(vm, result.status());
    auto instance = script::makeOwnedSquirrelInstance(vm, std::move(result).takeValue());
    if (!instance) return script::projectStatusResult(vm, instance.status());
    return script::projectStatusResult(vm, Status::success(), std::move(instance).takeValue());
}
// This VM-owned object observes a single incarnation; it never owns the solver constraint.
struct CouplingObserver {
    World3D*                  owner;
    std::weak_ptr<const void> worldLifetime, instanceLifetime;
    int                       id;
};
ResultRef<Coupling3D> resolve(Coupling3D* coupling) {
    if (coupling && coupling->isValid()) return ResultRef<Coupling3D>::success(std::ref(*coupling));
    return ResultRef<Coupling3D>::failure(
        Diagnostic::error(DiagnosticCode::StaleHandle, "Transmission is absent or destroyed"));
}
ResultRef<Coupling3D> resolve(CouplingObserver* observer) {
    if (observer) {
        const auto worldGuard    = observer->worldLifetime.lock();
        const auto instanceGuard = observer->instanceLifetime.lock();
        if (worldGuard && instanceGuard) return observer->owner->resolveCoupling3D(observer->id);
    }
    return ResultRef<Coupling3D>::failure(
        Diagnostic::error(DiagnosticCode::StaleHandle, "Transmission reference is stale"));
}
int          identity(Coupling3D* coupling) { return coupling ? coupling->getId() : 0; }
int          identity(CouplingObserver* observer) { return observer ? observer->id : 0; }
Result<void> remove(Coupling3D* coupling) {
    return coupling ? coupling->destroy()
                    : Result<void>::failure(Diagnostic::error(DiagnosticCode::StaleHandle, "Transmission is absent"));
}
Result<void> remove(CouplingObserver* observer) {
    auto target = resolve(observer);
    return target ? target.value().get().destroy() : Result<void>::failure(target.status());
}
template <class T>
void exposeOperations(ssq::Class& coupling, HSQUIRRELVM vm) {
    coupling.addFunc("getId", [vm](T* self) { return identity(receiver(vm, self)); });
    coupling.addFunc("isValid", [vm](T* self) { return resolve(receiver(vm, self)).ok(); });
    coupling.addFunc("setEnabled", [vm](T* self, bool enabled) {
        auto target = resolve(receiver(vm, self));
        return target ? script::projectResult(vm, target.value().get().setEnabled(enabled))
                      : script::projectStatusResult(vm, target.status());
    });
    coupling.addFunc("destroy", [vm](T* self) { return script::projectResult(vm, remove(receiver(vm, self))); });
    coupling.addFunc("sample", [vm](T* self) {
        auto target = resolve(receiver(vm, self));
        if (!target) return script::projectStatusResult(vm, target.status());
        return script::projectResult(vm, target.value().get().sample(), [](const Coupling3DState& state) {
            return Value(Value::Object{{"coordinateA", Value(state.coordinateA)},
                                       {"coordinateB", Value(state.coordinateB)},
                                       {"error", Value(state.error)},
                                       {"impulse", Value(state.impulse)},
                                       {"active", Value(state.active)},
                                       {"saturated", Value(state.saturated)}});
        });
    });
}
}  // namespace
void exposeCoupling3DBindings(ssq::Table& table) {
    auto point = table.addClass("CouplingPoint3D", ssq::Class::Ctor<CouplingPoint3D()>());
    point.addVar("x", &CouplingPoint3D::x);
    point.addVar("y", &CouplingPoint3D::y);
    point.addVar("z", &CouplingPoint3D::z);
    auto limits = table.addClass("CouplingLimits3D", ssq::Class::Ctor<CouplingLimits3D()>());
    limits.addVar("positionCorrection", &CouplingLimits3D::positionCorrection);
    limits.addVar("maxCorrectionSpeed", &CouplingLimits3D::maxCorrectionSpeed);
    limits.addVar("maxForce", &CouplingLimits3D::maxForce);
    auto gear = table.addClass("GearCoupling3DSettings", ssq::Class::Ctor<GearCoupling3DSettings()>());
    gear.addVar("ratio", &GearCoupling3DSettings::ratio);
    gear.addVar("limits", &GearCoupling3DSettings::limits);
    auto pulley = table.addClass("PulleyCoupling3DSettings", ssq::Class::Ctor<PulleyCoupling3DSettings()>());
    pulley.addVar("localAnchorA", &PulleyCoupling3DSettings::localAnchorA);
    pulley.addVar("localAnchorB", &PulleyCoupling3DSettings::localAnchorB);
    pulley.addVar("groundA", &PulleyCoupling3DSettings::groundA);
    pulley.addVar("groundB", &PulleyCoupling3DSettings::groundB);
    pulley.addVar("ratio", &PulleyCoupling3DSettings::ratio);
    pulley.addVar("length", &PulleyCoupling3DSettings::length);
    pulley.addVar("tensionOnly", &PulleyCoupling3DSettings::tensionOnly);
    pulley.addVar("limits", &PulleyCoupling3DSettings::limits);
    auto coupling =
        table.addClass<Coupling3D>("Coupling3D", std::function<Coupling3D*()>([] { return nullptr; }), true);
    const auto vm = table.getHandle();
    exposeOperations<Coupling3D>(coupling, vm);
    auto observer = table.addClass<CouplingObserver>("Coupling3DReference",
                                                     std::function<CouplingObserver*()>([] { return nullptr; }), true);
    exposeOperations<CouplingObserver>(observer, vm);
    table.addFunc("resolveCoupling3D", [vm](ssq::Object worldObject, int id) {
        auto* world = borrowed<World3D>(worldObject);
        if (!world) return invalid(vm);
        auto target = world->resolveCoupling3D(id);
        if (!target) return script::projectStatusResult(vm, target.status());
        return own(vm, Result<std::unique_ptr<CouplingObserver>>::success(std::make_unique<CouplingObserver>(
                           CouplingObserver{world, world->lifetimeToken(), target.value().get().lifetimeToken(), id})));
    });
    table.addFunc("createGearCoupling3D", [vm](ssq::Object w, ssq::Object a, ssq::Object b, ssq::Object s) {
        auto* world    = borrowed<World3D>(w);
        auto* first    = borrowed<Joint3D>(a);
        auto* second   = borrowed<Joint3D>(b);
        auto* settings = borrowed<GearCoupling3DSettings>(s);
        if (!world || !first || !second || !settings) return invalid(vm);
        return own(vm, Coupling3D::createGear(*world, *first, *second, *settings));
    });
    table.addFunc("createPulleyCoupling3D", [vm](ssq::Object w, ssq::Object a, ssq::Object b, ssq::Object s) {
        auto* world    = borrowed<World3D>(w);
        auto* first    = borrowed<Body3D>(a);
        auto* second   = borrowed<Body3D>(b);
        auto* settings = borrowed<PulleyCoupling3DSettings>(s);
        if (!world || !first || !second || !settings) return invalid(vm);
        return own(vm, Coupling3D::createPulley(*world, *first, *second, *settings));
    });
}
}  // namespace eve::physics
