#include "physics/forces/ForceBindings.h"
#include <charconv>
#include <cmath>
#include <simplesquirrel/simplesquirrel.hpp>
#include "common/SquirrelBinding.h"
#include "common/SquirrelOwnership.h"
#include "physics/Body.h"
#include "physics/Body3D.h"
#include "physics/World.h"
#include "physics/World3D.h"
#include "physics/forces/BuoyancyGeometry.h"
#include "physics/forces/FieldSet.h"
#include "physics/forces/ForceModels.h"
#include "physics/forces/WingAssembly.h"

namespace eve::physics {
namespace {
// VM boundary values use supported float fields; native model evaluation retains double precision.
struct ScriptForceVector {
    float       x = 0;
    float       y = 0;
    float       z = 0;
    ForceVector native() const { return {x, y, z}; }
};
struct ScriptFieldRegion {
    ScriptForceVector center      = {};
    float             radius      = -1;
    float             edgeWidth   = 0;
    std::string       kind        = "sphere";
    ScriptForceVector halfExtents = {1, 1, 1};
    FieldRegion       native() const {
        auto shape = static_cast<FieldRegionKind>(-1);
        if (kind == "sphere")
            shape = FieldRegionKind::Sphere;
        else if (kind == "rectangle")
            shape = FieldRegionKind::RectangleXY;
        else if (kind == "box")
            shape = FieldRegionKind::Box;
        else if (kind == "global")
            shape = FieldRegionKind::Global;
        return {center.native(), radius, edgeWidth, shape, halfExtents.native()};
    }
};
struct ScriptDirectionalField {
    ScriptFieldRegion region       = {};
    ScriptForceVector acceleration = {};
    DirectionalField  native() const { return {region.native(), acceleration.native()}; }
};
struct ScriptDirectionalForceField {
    ScriptFieldRegion     region = {};
    ScriptForceVector     force  = {};
    DirectionalForceField native() const { return {region.native(), force.native()}; }
};
struct ScriptRadialField {
    ScriptFieldRegion region          = {};
    float             strength        = 1;
    float             softening       = 0.1f;
    float             maxAcceleration = std::numeric_limits<float>::max();
    float             maxForce        = std::numeric_limits<float>::max();
    RadialField       native() const { return {region.native(), strength, softening, maxAcceleration, maxForce}; }
};
struct ScriptBuoyancyModel {
    ScriptForceVector gravity         = {0, -9.81f, 0};
    float             fluidDensity    = 1000;
    float             displacedVolume = 0;
    BuoyancyModel     native() const { return {gravity.native(), fluidDensity, displacedVolume}; }
};
struct ScriptDampingModel {
    float        linear    = 0;
    float        quadratic = 0;
    DampingModel native() const { return {linear, quadratic}; }
};
struct ScriptAngularDampingModel {
    float               linear = 0, quadratic = 0;
    AngularDampingModel native() const { return {linear, quadratic}; }
};
struct ScriptWingModel {
    ScriptForceVector chord         = {1, 0, 0};
    ScriptForceVector normal        = {0, 1, 0};
    float             density       = 1.225f;
    float             area          = 1;
    float             liftSlope     = 6.2831853f;
    float             zeroLiftAngle = 0;
    float             stallAngle    = 0.2617994f;
    float             baseDrag      = 0.02f;
    float             aspectRatio   = 6;
    float             efficiency    = 0.8f;
    WingModel         native() const {
        return {chord.native(), normal.native(), density,  area,        liftSlope,
                        zeroLiftAngle,  stallAngle,      baseDrag, aspectRatio, efficiency};
    }
};
struct ScriptFrictionModel {
    float         staticCoefficient  = 0.9f;
    float         kineticCoefficient = 0.7f;
    FrictionModel native() const { return {staticCoefficient, kineticCoefficient}; }
};
struct ScriptTireModel {
    float     radius                = 0.3f;
    float     inertia               = 1;
    float     friction              = 1;
    float     longitudinalStiffness = 10000;
    float     corneringStiffness    = 10000;
    float     rollingResistance     = 0.015f;
    float     minimumSlipSpeed      = 0.5f;
    TireModel native() const {
        return {radius,          inertia, friction, longitudinalStiffness, corneringStiffness, rollingResistance,
                minimumSlipSpeed};
    }
};
struct ScriptTireInput {
    float     longitudinalVelocity    = 0;
    float     lateralVelocity         = 0;
    float     angularVelocity         = 0;
    float     normalLoad              = 0;
    float     driveTorque             = 0;
    float     brakeTorque             = 0;
    float     longitudinalInverseMass = 0, lateralInverseMass = 0;
    TireInput native() const {
        return {longitudinalVelocity, lateralVelocity, angularVelocity,         normalLoad,
                driveTorque,          brakeTorque,     longitudinalInverseMass, lateralInverseMass};
    }
};
template <class T>
T* borrowed(const ssq::Object& value) {
    // SimpleSquirrel's pointer conversion deliberately skips native type-tag validation.
    if (value.getType() != ssq::Type::INSTANCE || value.getTypeTag() != eve::script::detail::squirrelTypeHash<T*>())
        return nullptr;
    try {
        return value.to<T*>();
    } catch (const ssq::TypeException&) {
        return nullptr;
    }
}
Value vectorValue(ForceVector v) {
    return Value(Value::Object{{"x", Value(v.x)}, {"y", Value(v.y)}, {"z", Value(v.z)}});
}
Value immersionValue(const ImmersionSample& s) {
    return Value(Value::Object{
        {"volume", Value(s.volume)}, {"fraction", Value(s.fraction)}, {"centroid", vectorValue(s.centroid)}});
}
Value wingValue(const WingSample& s) {
    return Value(Value::Object{{"force", vectorValue(s.force)},
                               {"angleOfAttack", Value(s.angleOfAttack)},
                               {"liftCoefficient", Value(s.liftCoefficient)},
                               {"dragCoefficient", Value(s.dragCoefficient)}});
}
Value tireValue(const TireSample& s) {
    return Value(Value::Object{{"longitudinalForce", Value(s.longitudinalForce)},
                               {"lateralForce", Value(s.lateralForce)},
                               {"slipRatio", Value(s.slipRatio)},
                               {"slipAngle", Value(s.slipAngle)},
                               {"angularVelocity", Value(s.angularVelocity)},
                               {"reactionTorque", Value(s.reactionTorque)},
                               {"resistanceTorque", Value(s.resistanceTorque)}});
}
template <class T>
Result<T> nullInput() {
    return Result<T>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "Force model input is null"));
}
struct ScriptFieldBuilder {
    std::vector<FieldDefinition> fields;
};
struct ScriptFieldTargets {
    std::vector<FieldTarget> targets;
};
Result<std::uint64_t> maskValue(const ssq::Object& value) {
    if (value.getType() != ssq::Type::STRING) return nullInput<std::uint64_t>();
    const auto    text   = value.toString();
    std::uint64_t mask   = 0;
    const auto    parsed = std::from_chars(text.data(), text.data() + text.size(), mask);
    if (text.empty() || parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
        return Result<std::uint64_t>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "Expected unsigned decimal 64-bit mask"));
    return Result<std::uint64_t>::success(mask);
}
template <class T>
ssq::Table ownedValue(HSQUIRRELVM vm, Result<T> result) {
    if (!result) return script::projectStatusResult(vm, result.status());
    auto instance = script::makeOwnedSquirrelInstance(vm, std::make_unique<T>(std::move(result).takeValue()));
    if (!instance) return script::projectStatusResult(vm, instance.status());
    return script::projectStatusResult(vm, Status::success(), std::move(instance).takeValue());
}
template <class Model>
Result<void> appendField(const ssq::Object& builderObject, const ssq::Object& modelObject,
                         const ssq::Object& includeObject, const ssq::Object& excludeObject) {
    auto* builder = borrowed<ScriptFieldBuilder>(builderObject);
    auto* model   = borrowed<Model>(modelObject);
    if (!builder || !model) return nullInput<void>();
    auto include = maskValue(includeObject);
    if (!include) return Result<void>::failure(include.status());
    auto exclude = maskValue(excludeObject);
    if (!exclude) return Result<void>::failure(exclude.status());
    FieldDefinition definition{model->native(), {include.value(), exclude.value()}};
    auto            validated = FieldSet::build(std::span(&definition, 1));
    if (!validated) return Result<void>::failure(validated.status());
    builder->fields.push_back(std::move(definition));
    return Result<void>::success();
}
template <class BodyType>
Result<void> appendTarget(const ssq::Object& targetsObject, const ssq::Object& bodyObject,
                          const ssq::Object& maskObject) {
    auto* targets = borrowed<ScriptFieldTargets>(targetsObject);
    auto* body    = borrowed<BodyType>(bodyObject);
    if (!targets || !body) return nullInput<void>();
    auto mask = maskValue(maskObject);
    if (!mask) return Result<void>::failure(mask.status());
    auto link = PhysicsLink::fromBody(*body);
    if (!link) return Result<void>::failure(link.status());
    targets->targets.push_back({link.value(), mask.value()});
    return Result<void>::success();
}
template <class W>
ssq::Table prepareFields(HSQUIRRELVM vm, const ssq::Object& setObject, const ssq::Object& worldObject,
                         const ssq::Object& targetsObject) {
    auto* set     = borrowed<FieldSet>(setObject);
    auto* world   = borrowed<W>(worldObject);
    auto* targets = borrowed<ScriptFieldTargets>(targetsObject);
    return ownedValue(vm, set && world && targets ? set->prepare(*world, targets->targets) : nullInput<ForceBatch>());
}
Result<SimulationStep> forceStepValue(const ssq::Object& tickObject, const ssq::Object& nsObject) {
    auto tick = maskValue(tickObject);
    if (!tick) return Result<SimulationStep>::failure(tick.status());
    auto ns = maskValue(nsObject);
    if (!ns) return Result<SimulationStep>::failure(ns.status());
    if (ns.value() == 0 || ns.value() > 50000000)
        return Result<SimulationStep>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "Force step duration must be 1..50000000 nanoseconds"));
    return Result<SimulationStep>::success(
        {SimulationTick{tick.value()}, Duration::fromNanoseconds(static_cast<std::int64_t>(ns.value()))});
}
template <class W>
ssq::Table commitFields(HSQUIRRELVM vm, const ssq::Object& batchObject, const ssq::Object& worldObject,
                        const ssq::Object& tickObject, const ssq::Object& nsObject) {
    auto* batch = borrowed<ForceBatch>(batchObject);
    auto* world = borrowed<W>(worldObject);
    if (!batch || !world) return script::projectResult(vm, nullInput<void>());
    auto step = forceStepValue(tickObject, nsObject);
    if (!step) return script::projectStatusResult(vm, step.status());
    return ownedValue(vm, batch->commit(*world, step.value()));
}
template <class W>
ssq::Table stepForceWorld(HSQUIRRELVM vm, const ssq::Object& worldObject, const ssq::Object& tickObject,
                          const ssq::Object& nsObject, const SimulationSettings& settings) {
    auto* world = borrowed<W>(worldObject);
    if (!world) return script::projectResult(vm, nullInput<void>());
    auto step = forceStepValue(tickObject, nsObject);
    if (!step) return script::projectStatusResult(vm, step.status());
    return script::projectResult(vm, world->step(step.value(), settings));
}

struct ScriptWingBuilder {
    std::vector<WingSurface> surfaces;
};
struct ScriptWingControls {
    std::vector<double> angles;
};
template <class W, class B>
ssq::Table prepareWings(HSQUIRRELVM vm, const ssq::Object& assemblyObject, const ssq::Object& worldObject,
                        const ssq::Object& bodyObject, const ssq::Object& controlsObject,
                        const ssq::Object& windObject) {
    auto* assembly = borrowed<WingAssembly>(assemblyObject);
    auto* world    = borrowed<W>(worldObject);
    auto* body     = borrowed<B>(bodyObject);
    auto* controls = borrowed<ScriptWingControls>(controlsObject);
    auto* wind     = borrowed<ScriptForceVector>(windObject);
    if (!assembly || !world || !body || !controls || !wind) return script::projectResult(vm, nullInput<void>());
    auto link = PhysicsLink::fromBody(*body);
    if (!link) return script::projectStatusResult(vm, link.status());
    return ownedValue(vm, assembly->prepare(*world, link.value(), controls->angles, wind->native()));
}
void exposeWingCollections(ssq::Table& table) {
    auto vm = table.getHandle();
    table.addClass("WingAssemblyBuilder", ssq::Class::Ctor<ScriptWingBuilder()>());
    table.addClass("WingControls", ssq::Class::Ctor<ScriptWingControls()>());
    table.addClass<WingAssembly>("WingAssembly", std::function<WingAssembly*()>([] { return nullptr; }), true);
    table.addFunc("addWingSurface", [vm](ssq::Object builderObject, ssq::Object modelObject, ssq::Object centerObject) {
        auto* builder = borrowed<ScriptWingBuilder>(builderObject);
        auto* model   = borrowed<ScriptWingModel>(modelObject);
        auto* center  = borrowed<ScriptForceVector>(centerObject);
        if (!builder || !model || !center) return script::projectResult(vm, nullInput<void>());
        WingSurface surface{model->native(), center->native()};
        auto        valid = WingAssembly::build(std::span(&surface, 1));
        if (!valid) return script::projectStatusResult(vm, valid.status());
        builder->surfaces.push_back(surface);
        return script::projectResult(vm, Result<void>::success());
    });
    table.addFunc("buildWingAssembly", [vm](ssq::Object builderObject) {
        auto* builder = borrowed<ScriptWingBuilder>(builderObject);
        return ownedValue(vm, builder ? WingAssembly::build(builder->surfaces) : nullInput<WingAssembly>());
    });
    table.addFunc("addWingDeflection", [vm](ssq::Object controlsObject, float angle) {
        auto* controls = borrowed<ScriptWingControls>(controlsObject);
        if (!controls || !std::isfinite(angle)) return script::projectResult(vm, nullInput<void>());
        controls->angles.push_back(angle);
        return script::projectResult(vm, Result<void>::success());
    });
    table.addFunc("setWingDeflection", [vm](ssq::Object controlsObject, SQInteger index, float angle) {
        auto* controls = borrowed<ScriptWingControls>(controlsObject);
        if (!controls || index < 0 || static_cast<std::size_t>(index) >= controls->angles.size() ||
            !std::isfinite(angle))
            return script::projectResult(vm, nullInput<void>());
        controls->angles[static_cast<std::size_t>(index)] = angle;
        return script::projectResult(vm, Result<void>::success());
    });
    table.addFunc("prepareWingForces2D",
                  [vm](ssq::Object a, ssq::Object w, ssq::Object b, ssq::Object c, ssq::Object wind) {
                      return prepareWings<World, Body>(vm, a, w, b, c, wind);
                  });
    table.addFunc("prepareWingForces3D",
                  [vm](ssq::Object a, ssq::Object w, ssq::Object b, ssq::Object c, ssq::Object wind) {
                      return prepareWings<World3D, Body3D>(vm, a, w, b, c, wind);
                  });
}
void exposeFieldCollections(ssq::Table& table) {
    auto vm = table.getHandle();
    table.addClass("FieldSetBuilder", ssq::Class::Ctor<ScriptFieldBuilder()>());
    table.addClass("FieldTargets", ssq::Class::Ctor<ScriptFieldTargets()>());
    table.addClass<FieldSet>("FieldSet", std::function<FieldSet*()>([] { return nullptr; }), true);
    table.addClass<ForceBatch>("ForceBatch", std::function<ForceBatch*()>([] { return nullptr; }), true);
    table.addFunc("addDirectionalField", [vm](ssq::Object b, ssq::Object m, ssq::Object i, ssq::Object e) {
        return script::projectResult(vm, appendField<ScriptDirectionalField>(b, m, i, e));
    });
    table.addFunc("addDirectionalConstantField", [vm](ssq::Object b, ssq::Object m, ssq::Object i, ssq::Object e) {
        return script::projectResult(vm, appendField<ScriptDirectionalForceField>(b, m, i, e));
    });
    table.addFunc("addRadialField", [vm](ssq::Object b, ssq::Object m, ssq::Object i, ssq::Object e) {
        return script::projectResult(vm, appendField<ScriptRadialField>(b, m, i, e));
    });
    table.addFunc("buildFieldSet", [vm](ssq::Object b) {
        auto* builder = borrowed<ScriptFieldBuilder>(b);
        return ownedValue(vm, builder ? FieldSet::build(builder->fields) : nullInput<FieldSet>());
    });
    table.addFunc("addFieldTarget2D", [vm](ssq::Object t, ssq::Object b, ssq::Object m) {
        return script::projectResult(vm, appendTarget<Body>(t, b, m));
    });
    table.addFunc("addFieldTarget3D", [vm](ssq::Object t, ssq::Object b, ssq::Object m) {
        return script::projectResult(vm, appendTarget<Body3D>(t, b, m));
    });
    table.addFunc("prepareFieldForces2D",
                  [vm](ssq::Object s, ssq::Object w, ssq::Object t) { return prepareFields<World>(vm, s, w, t); });
    table.addFunc("prepareFieldForces3D",
                  [vm](ssq::Object s, ssq::Object w, ssq::Object t) { return prepareFields<World3D>(vm, s, w, t); });
    table.addFunc("commitForceBatch2D", [vm](ssq::Object b, ssq::Object w, ssq::Object tick, ssq::Object ns) {
        return commitFields<World>(vm, b, w, tick, ns);
    });
    table.addFunc("commitForceBatch3D", [vm](ssq::Object b, ssq::Object w, ssq::Object tick, ssq::Object ns) {
        return commitFields<World3D>(vm, b, w, tick, ns);
    });
}
}  // namespace
void exposeForceBindings(ssq::Table& table) {
    const auto receiptVm = table.getHandle();
    table.addClass<ForceReceipt>("ForceReceipt", std::function<ForceReceipt*()>([] { return nullptr; }), true);
    table.addFunc("forceReceiptStatus", [receiptVm](ssq::Object object) {
        auto* receipt = borrowed<ForceReceipt>(object);
        if (!receipt) return script::projectResult(receiptVm, nullInput<void>());
        const char* status = "cancelled";
        if (receipt->status() == ForceStepStatus::Pending) status = "pending";
        if (receipt->status() == ForceStepStatus::Applied) status = "applied";
        return script::projectStatusResult(receiptVm, Status::success(), Value(status));
    });
    table.addFunc("stepPhysicsWorld2D",
                  [receiptVm](ssq::Object w, ssq::Object tick, ssq::Object ns, int velocity, int position) {
                      return stepForceWorld<World>(receiptVm, w, tick, ns, {velocity, position, 4});
                  });
    table.addFunc("stepPhysicsWorld3D", [receiptVm](ssq::Object w, ssq::Object tick, ssq::Object ns, int substeps) {
        return stepForceWorld<World3D>(receiptVm, w, tick, ns, {8, 3, substeps});
    });

    exposeFieldCollections(table);
    exposeWingCollections(table);
    auto cls0 = table.addClass("ForceVector", ssq::Class::Ctor<ScriptForceVector()>());
    cls0.addVar("x", &ScriptForceVector::x);
    cls0.addVar("y", &ScriptForceVector::y);
    cls0.addVar("z", &ScriptForceVector::z);
    auto cls1 = table.addClass("FieldRegion", ssq::Class::Ctor<ScriptFieldRegion()>());
    cls1.addVar("center", &ScriptFieldRegion::center);
    cls1.addVar("radius", &ScriptFieldRegion::radius);
    cls1.addVar("edgeWidth", &ScriptFieldRegion::edgeWidth);
    cls1.addVar("kind", &ScriptFieldRegion::kind);
    cls1.addVar("halfExtents", &ScriptFieldRegion::halfExtents);
    auto cls2 = table.addClass("DirectionalField", ssq::Class::Ctor<ScriptDirectionalField()>());
    cls2.addVar("region", &ScriptDirectionalField::region);
    cls2.addVar("acceleration", &ScriptDirectionalField::acceleration);
    auto constant = table.addClass("DirectionalForceField", ssq::Class::Ctor<ScriptDirectionalForceField()>());
    constant.addVar("region", &ScriptDirectionalForceField::region);
    constant.addVar("force", &ScriptDirectionalForceField::force);
    auto cls3 = table.addClass("RadialField", ssq::Class::Ctor<ScriptRadialField()>());
    cls3.addVar("region", &ScriptRadialField::region);
    cls3.addVar("strength", &ScriptRadialField::strength);
    cls3.addVar("softening", &ScriptRadialField::softening);
    cls3.addVar("maxAcceleration", &ScriptRadialField::maxAcceleration);
    cls3.addVar("maxForce", &ScriptRadialField::maxForce);
    auto cls4 = table.addClass("BuoyancyModel", ssq::Class::Ctor<ScriptBuoyancyModel()>());
    cls4.addVar("gravity", &ScriptBuoyancyModel::gravity);
    cls4.addVar("fluidDensity", &ScriptBuoyancyModel::fluidDensity);
    cls4.addVar("displacedVolume", &ScriptBuoyancyModel::displacedVolume);
    auto cls5 = table.addClass("DampingModel", ssq::Class::Ctor<ScriptDampingModel()>());
    cls5.addVar("linear", &ScriptDampingModel::linear);
    cls5.addVar("quadratic", &ScriptDampingModel::quadratic);
    auto angular = table.addClass("AngularDampingModel", ssq::Class::Ctor<ScriptAngularDampingModel()>());
    angular.addVar("linear", &ScriptAngularDampingModel::linear);
    angular.addVar("quadratic", &ScriptAngularDampingModel::quadratic);
    auto cls6 = table.addClass("WingModel", ssq::Class::Ctor<ScriptWingModel()>());
    cls6.addVar("chord", &ScriptWingModel::chord);
    cls6.addVar("normal", &ScriptWingModel::normal);
    cls6.addVar("density", &ScriptWingModel::density);
    cls6.addVar("area", &ScriptWingModel::area);
    cls6.addVar("liftSlope", &ScriptWingModel::liftSlope);
    cls6.addVar("zeroLiftAngle", &ScriptWingModel::zeroLiftAngle);
    cls6.addVar("stallAngle", &ScriptWingModel::stallAngle);
    cls6.addVar("baseDrag", &ScriptWingModel::baseDrag);
    cls6.addVar("aspectRatio", &ScriptWingModel::aspectRatio);
    cls6.addVar("efficiency", &ScriptWingModel::efficiency);
    auto cls7 = table.addClass("FrictionModel", ssq::Class::Ctor<ScriptFrictionModel()>());
    cls7.addVar("staticCoefficient", &ScriptFrictionModel::staticCoefficient);
    cls7.addVar("kineticCoefficient", &ScriptFrictionModel::kineticCoefficient);
    auto cls8 = table.addClass("TireModel", ssq::Class::Ctor<ScriptTireModel()>());
    cls8.addVar("radius", &ScriptTireModel::radius);
    cls8.addVar("inertia", &ScriptTireModel::inertia);
    cls8.addVar("friction", &ScriptTireModel::friction);
    cls8.addVar("longitudinalStiffness", &ScriptTireModel::longitudinalStiffness);
    cls8.addVar("corneringStiffness", &ScriptTireModel::corneringStiffness);
    cls8.addVar("rollingResistance", &ScriptTireModel::rollingResistance);
    cls8.addVar("minimumSlipSpeed", &ScriptTireModel::minimumSlipSpeed);
    auto cls9 = table.addClass("TireInput", ssq::Class::Ctor<ScriptTireInput()>());
    cls9.addVar("longitudinalVelocity", &ScriptTireInput::longitudinalVelocity);
    cls9.addVar("lateralVelocity", &ScriptTireInput::lateralVelocity);
    cls9.addVar("angularVelocity", &ScriptTireInput::angularVelocity);
    cls9.addVar("normalLoad", &ScriptTireInput::normalLoad);
    cls9.addVar("driveTorque", &ScriptTireInput::driveTorque);
    cls9.addVar("brakeTorque", &ScriptTireInput::brakeTorque);
    cls9.addVar("longitudinalInverseMass", &ScriptTireInput::longitudinalInverseMass);
    cls9.addVar("lateralInverseMass", &ScriptTireInput::lateralInverseMass);
    table.addFunc(
        "sampleDirectionalForce", [vm = table.getHandle()](ssq::Object mObject, ssq::Object pObject, float mass) {
            auto* m = borrowed<ScriptDirectionalField>(mObject);
            auto* p = borrowed<ScriptForceVector>(pObject);
            return script::projectResult(
                vm, m && p ? ForceModels::directional(m->native(), p->native(), mass) : nullInput<ForceVector>(),
                vectorValue);
        });
    table.addFunc("sampleDirectionalConstantForce", [vm = table.getHandle()](ssq::Object mObject, ssq::Object pObject) {
        auto* m = borrowed<ScriptDirectionalForceField>(mObject);
        auto* p = borrowed<ScriptForceVector>(pObject);
        return script::projectResult(
            vm, m && p ? ForceModels::directionalForce(m->native(), p->native()) : nullInput<ForceVector>(),
            vectorValue);
    });
    table.addFunc("sampleRadialForce", [vm = table.getHandle()](ssq::Object mObject, ssq::Object pObject, float mass) {
        auto* m = borrowed<ScriptRadialField>(mObject);
        auto* p = borrowed<ScriptForceVector>(pObject);
        return script::projectResult(
            vm, m && p ? ForceModels::radial(m->native(), p->native(), mass) : nullInput<ForceVector>(), vectorValue);
    });
    table.addFunc("sampleCircleImmersion2D", [vm = table.getHandle()](ssq::Object center, float radius, float thickness,
                                                                      float surfaceY) {
        auto* p = borrowed<ScriptForceVector>(center);
        return script::projectResult(
            vm, p ? BuoyancyGeometry::circle2D(p->native(), radius, thickness, surfaceY) : nullInput<ImmersionSample>(),
            immersionValue);
    });
    table.addFunc("sampleRectangleImmersion2D", [vm = table.getHandle()](ssq::Object center, float width, float height,
                                                                         float thickness, float surfaceY) {
        auto* p = borrowed<ScriptForceVector>(center);
        return script::projectResult(vm,
                                     p ? BuoyancyGeometry::rectangle2D(p->native(), width, height, thickness, surfaceY)
                                       : nullInput<ImmersionSample>(),
                                     immersionValue);
    });
    table.addFunc(
        "sampleSphereImmersion3D", [vm = table.getHandle()](ssq::Object center, float radius, float surfaceY) {
            auto* p = borrowed<ScriptForceVector>(center);
            return script::projectResult(
                vm, p ? BuoyancyGeometry::sphere3D(p->native(), radius, surfaceY) : nullInput<ImmersionSample>(),
                immersionValue);
        });
    table.addFunc(
        "sampleBoxImmersion3D", [vm = table.getHandle()](ssq::Object center, ssq::Object size, float surfaceY) {
            auto* p = borrowed<ScriptForceVector>(center);
            auto* s = borrowed<ScriptForceVector>(size);
            return script::projectResult(
                vm, p && s ? BuoyancyGeometry::box3D(p->native(), s->native(), surfaceY) : nullInput<ImmersionSample>(),
                immersionValue);
        });
    table.addFunc("sampleBuoyancyForce", [vm = table.getHandle()](ssq::Object mObject) {
        auto* m = borrowed<ScriptBuoyancyModel>(mObject);
        return script::projectResult(vm, m ? ForceModels::buoyancy(m->native()) : nullInput<ForceVector>(),
                                     vectorValue);
    });
    table.addFunc(
        "sampleDampingForce", [vm = table.getHandle()](ssq::Object mObject, ssq::Object vObject, float mass, float dt) {
            auto* m = borrowed<ScriptDampingModel>(mObject);
            auto* v = borrowed<ScriptForceVector>(vObject);
            return script::projectResult(
                vm, m && v ? ForceModels::damping(m->native(), v->native(), mass, dt) : nullInput<ForceVector>(),
                vectorValue);
        });
    table.addFunc(
        "sampleAngularDamping2D", [vm = table.getHandle()](ssq::Object model, float velocity, float inertia, float dt) {
            auto* m = borrowed<ScriptAngularDampingModel>(model);
            return script::projectResult(
                vm, m ? ForceModels::angularDamping2D(m->native(), velocity, inertia, dt) : nullInput<double>(),
                [](double torque) { return Value(torque); });
        });
    table.addFunc("sampleAngularDamping3D",
                  [vm = table.getHandle()](ssq::Object model, ssq::Object velocity, ssq::Object inertia, float dt) {
                      auto* m = borrowed<ScriptAngularDampingModel>(model);
                      auto* v = borrowed<ScriptForceVector>(velocity);
                      auto* i = borrowed<ScriptForceVector>(inertia);
                      return script::projectResult(
                          vm,
                          m && v && i ? ForceModels::angularDamping3D(m->native(), v->native(), i->native(), dt)
                                      : nullInput<ForceVector>(),
                          vectorValue);
                  });
    table.addFunc("sampleWingForce", [vm = table.getHandle()](ssq::Object mObject, ssq::Object vObject) {
        auto* m = borrowed<ScriptWingModel>(mObject);
        auto* v = borrowed<ScriptForceVector>(vObject);
        return script::projectResult(vm, m && v ? ForceModels::wing(m->native(), v->native()) : nullInput<WingSample>(),
                                     wingValue);
    });
    table.addFunc("sampleFrictionForce", [vm = table.getHandle()](ssq::Object mObject, ssq::Object vObject,
                                                                  ssq::Object demandObject, float load) {
        auto* m      = borrowed<ScriptFrictionModel>(mObject);
        auto* v      = borrowed<ScriptForceVector>(vObject);
        auto* demand = borrowed<ScriptForceVector>(demandObject);
        return script::projectResult(vm,
                                     m && v && demand
                                         ? ForceModels::friction(m->native(), v->native(), demand->native(), load)
                                         : nullInput<ForceVector>(),
                                     vectorValue);
    });
    table.addFunc("stepTire", [vm = table.getHandle()](ssq::Object mObject, ssq::Object inObject, float dt) {
        auto* m  = borrowed<ScriptTireModel>(mObject);
        auto* in = borrowed<ScriptTireInput>(inObject);
        return script::projectResult(
            vm, m && in ? ForceModels::tire(m->native(), in->native(), dt) : nullInput<TireSample>(), tireValue);
    });
    table.addFunc(
        "applyForceSI2D", [vm = table.getHandle()](ssq::Object bodyObject, float fx, float fy, float px, float py) {
            auto* body = borrowed<Body>(bodyObject);
            return script::projectResult(vm, body ? applyForceSI(*body, {fx, fy, 0}, {px, py, 0}) : nullInput<void>());
        });
    table.addFunc("applyForceSI3D", [vm = table.getHandle()](ssq::Object bodyObject, float fx, float fy, float fz,
                                                             float px, float py, float pz) {
        auto* body = borrowed<Body3D>(bodyObject);
        return script::projectResult(vm, body ? applyForceSI(*body, {fx, fy, fz}, {px, py, pz}) : nullInput<void>());
    });
}
}  // namespace eve::physics
