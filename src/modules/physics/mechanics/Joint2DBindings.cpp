#include "physics/mechanics/Joint2DBindings.h"
#include "common/SquirrelBinding.h"
#include "common/SquirrelOwnership.h"
#include "physics/Body.h"
#include "physics/PhysicsLink.h"
#include "physics/World.h"
#include "physics/mechanics/Joint2D.h"

#include <simplesquirrel/simplesquirrel.hpp>
#include <string>

namespace eve::physics {
namespace {
struct ScriptJoint2DSettings {
    std::string             kind = "revolute";
    JointPoint2D            anchorA, anchorB, axis{1, 0}, groundA{-1, 1}, groundB{1, 1};
    float                   length = 1, ratio = 1, springHertz = 0, dampingRatio = 0;
    bool                    collideConnected = false;
    Result<Joint2DSettings> native() const {
        Joint2DKind type;
        if (kind == "revolute")
            type = Joint2DKind::Revolute;
        else if (kind == "prismatic")
            type = Joint2DKind::Prismatic;
        else if (kind == "distance")
            type = Joint2DKind::Distance;
        else if (kind == "pulley")
            type = Joint2DKind::Pulley;
        else if (kind == "wheel")
            type = Joint2DKind::Wheel;
        else if (kind == "weld")
            type = Joint2DKind::Weld;
        else
            return Result<Joint2DSettings>::failure(
                Diagnostic::error(DiagnosticCode::InvalidArgument, "Unknown planar joint kind"));
        return Result<Joint2DSettings>::success({type, anchorA, anchorB, axis, groundA, groundB, length, ratio,
                                                 springHertz, dampingRatio, collideConnected});
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
ssq::Table invalid(HSQUIRRELVM vm) {
    return script::projectResult(
        vm, Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "Live joint inputs required")));
}
template <class T>
ssq::Table own(HSQUIRRELVM vm, Result<std::unique_ptr<T>> result) {
    if (!result.ok()) return script::projectStatusResult(vm, result.status());
    auto instance = script::makeOwnedSquirrelInstance(vm, std::move(result).takeValue());
    if (!instance.ok()) return script::projectStatusResult(vm, instance.status());
    return script::projectStatusResult(vm, Status::success(), std::move(instance).takeValue());
}
// VM owns this observation only. Its destruction never destroys the solver joint.
struct ScriptJointObserver {
    World*                    owner;
    std::weak_ptr<const void> lifetime;
    PhysicsJointLink          link;
    int                       id;
};
ResultRef<Joint2D> resolve(Joint2D* joint) {
    if (joint && joint->isValid()) return ResultRef<Joint2D>::success(std::ref(*joint));
    return ResultRef<Joint2D>::failure(Diagnostic::error(DiagnosticCode::StaleHandle, "Joint is absent or destroyed"));
}
ResultRef<Joint2D> resolve(ScriptJointObserver* observer) {
    if (observer) {
        const auto guard = observer->lifetime.lock();
        if (guard) return observer->link.resolve(*observer->owner);
    }
    return ResultRef<Joint2D>::failure(
        Diagnostic::error(DiagnosticCode::StaleHandle, "Joint world is absent or destroyed"));
}
Result<void> remove(Joint2D* joint) {
    return joint ? joint->destroyChecked()
                 : Result<void>::failure(Diagnostic::error(DiagnosticCode::StaleHandle, "Joint is absent"));
}
Result<void> remove(ScriptJointObserver* observer) {
    auto target = resolve(observer);
    return target ? target.value().get().destroyChecked() : Result<void>::failure(target.status());
}
int                persistentId(Joint2D* joint) { return joint ? joint->getId() : 0; }
int                persistentId(ScriptJointObserver* observer) { return observer ? observer->id : 0; }
ResultRef<Joint2D> jointInput(const ssq::Object& object) {
    if (auto* joint = borrowed<Joint2D>(object)) return resolve(joint);
    return resolve(borrowed<ScriptJointObserver>(object));
}
template <class T>
T* checkedReceiver(HSQUIRRELVM vm, T* receiver) {
    SQUserPointer tag = nullptr;
    if (sq_gettype(vm, 1) != OT_INSTANCE || SQ_FAILED(sq_gettypetag(vm, 1, &tag)) ||
        reinterpret_cast<std::size_t>(tag) != eve::script::detail::squirrelTypeHash<T*>())
        return nullptr;
    return receiver;
}
template <class T>
void exposeOperations(ssq::Class& joint, HSQUIRRELVM vm) {
    joint.addFunc("getId", [vm](T* self) { return persistentId(checkedReceiver(vm, self)); });
    joint.addFunc("isValid", [vm](T* self) { return resolve(checkedReceiver(vm, self)).ok(); });
    joint.addFunc("setMotor", [vm](T* self, bool enabled, float speed, float maximumForce) {
        auto target = resolve(checkedReceiver(vm, self));
        return target ? script::projectResult(vm, target.value().get().setMotor(enabled, speed, maximumForce))
                      : script::projectStatusResult(vm, target.status());
    });
    joint.addFunc("setLimits", [vm](T* self, bool enabled, float lower, float upper) {
        auto target = resolve(checkedReceiver(vm, self));
        return target ? script::projectResult(vm, target.value().get().setLimits(enabled, lower, upper))
                      : script::projectStatusResult(vm, target.status());
    });
    joint.addFunc("destroy", [vm](T* self) { return script::projectResult(vm, remove(checkedReceiver(vm, self))); });
    joint.addFunc("sample", [vm](T* self, float dt) {
        auto target = resolve(checkedReceiver(vm, self));
        if (!target) return script::projectStatusResult(vm, target.status());
        return script::projectResult(vm, target.value().get().sample(dt), [](const Joint2DState& s) {
            auto v = [](JointPoint2D p) { return Value(Value::Object{{"x", Value(p.x)}, {"y", Value(p.y)}}); };
            return Value(Value::Object{{"coordinate", Value(s.coordinate)},
                                       {"secondaryCoordinate", Value(s.secondaryCoordinate)},
                                       {"anchorA", v(s.anchorA)},
                                       {"anchorB", v(s.anchorB)},
                                       {"reactionForce", v(s.reactionForce)},
                                       {"reactionTorque", Value(s.reactionTorque)}});
        });
    });
}
}  // namespace
void exposeJoint2DBindings(ssq::Table& table) {
    auto point = table.addClass("JointPoint2D", ssq::Class::Ctor<JointPoint2D()>());
    point.addVar("x", &JointPoint2D::x);
    point.addVar("y", &JointPoint2D::y);
    auto settings = table.addClass("Joint2DSettings", ssq::Class::Ctor<ScriptJoint2DSettings()>());
    settings.addVar("kind", &ScriptJoint2DSettings::kind);
    settings.addVar("anchorA", &ScriptJoint2DSettings::anchorA);
    settings.addVar("anchorB", &ScriptJoint2DSettings::anchorB);
    settings.addVar("axis", &ScriptJoint2DSettings::axis);
    settings.addVar("groundA", &ScriptJoint2DSettings::groundA);
    settings.addVar("groundB", &ScriptJoint2DSettings::groundB);
    settings.addVar("length", &ScriptJoint2DSettings::length);
    settings.addVar("ratio", &ScriptJoint2DSettings::ratio);
    settings.addVar("springHertz", &ScriptJoint2DSettings::springHertz);
    settings.addVar("dampingRatio", &ScriptJoint2DSettings::dampingRatio);
    settings.addVar("collideConnected", &ScriptJoint2DSettings::collideConnected);
    auto joint = table.addClass<Joint2D>("Joint2D", std::function<Joint2D*()>([] { return nullptr; }), true);
    exposeOperations<Joint2D>(joint, table.getHandle());
    joint.addFunc("getKind", &Joint2D::getKind);
    joint.addFunc("getBodyAId", &Joint2D::getBodyAId);
    joint.addFunc("getBodyBId", &Joint2D::getBodyBId);
    joint.addFunc("setCollideConnected", &Joint2D::setCollideConnected);
    joint.addFunc("getCollideConnected", &Joint2D::getCollideConnected);
    joint.addFunc("setDistanceLength", &Joint2D::setDistanceLength);
    joint.addFunc("getDistanceLength", &Joint2D::getDistanceLength);
    joint.addFunc("setDistanceSpring", &Joint2D::setDistanceSpring);
    joint.addFunc("getDistanceFrequency", &Joint2D::getDistanceFrequency);
    joint.addFunc("getDistanceDampingRatio", &Joint2D::getDistanceDampingRatio);
    joint.addFunc("setRevoluteLimits", &Joint2D::setRevoluteLimits);
    joint.addFunc("setRevoluteMotor", &Joint2D::setRevoluteMotor);
    joint.addFunc("getRevoluteAngle", &Joint2D::getRevoluteAngle);
    joint.addFunc("getRevoluteSpeed", &Joint2D::getRevoluteSpeed);
    joint.addFunc("getRevoluteMotorTorque", &Joint2D::getRevoluteMotorTorque);
    joint.addFunc("setPrismaticLimits", &Joint2D::setPrismaticLimits);
    joint.addFunc("setPrismaticMotor", &Joint2D::setPrismaticMotor);
    joint.addFunc("getPrismaticTranslation", &Joint2D::getPrismaticTranslation);
    joint.addFunc("getPrismaticSpeed", &Joint2D::getPrismaticSpeed);
    joint.addFunc("setWeldSpring", &Joint2D::setWeldSpring);
    joint.addFunc("getWeldFrequency", &Joint2D::getWeldFrequency);
    joint.addFunc("getWeldDampingRatio", &Joint2D::getWeldDampingRatio);
    joint.addFunc("setWheelSpring", &Joint2D::setWheelSpring);
    joint.addFunc("setWheelMotor", &Joint2D::setWheelMotor);
    joint.addFunc("getWheelTranslation", &Joint2D::getWheelTranslation);
    joint.addFunc("getWheelSpeed", &Joint2D::getWheelSpeed);
    joint.addFunc("setMotorLinearOffset", &Joint2D::setMotorLinearOffset);
    joint.addFunc("setMotorAngularOffset", &Joint2D::setMotorAngularOffset);
    joint.addFunc("setMotorLimits", &Joint2D::setMotorLimits);
    joint.addFunc("getMotorLinearOffsetX", &Joint2D::getMotorLinearOffsetX);
    joint.addFunc("getMotorLinearOffsetY", &Joint2D::getMotorLinearOffsetY);
    joint.addFunc("getMotorAngularOffset", &Joint2D::getMotorAngularOffset);
    joint.addFunc("setGearRatio", &Joint2D::setGearRatio);
    joint.addFunc("getGearRatio", &Joint2D::getGearRatio);
    joint.addFunc("getGearJoint1", &Joint2D::getGearJoint1);
    joint.addFunc("getGearJoint2", &Joint2D::getGearJoint2);
    auto reference = table.addClass<ScriptJointObserver>(
        "Joint2DReference", std::function<ScriptJointObserver*()>([] { return nullptr; }), true);
    exposeOperations<ScriptJointObserver>(reference, table.getHandle());
    table.addFunc("resolveJoint2D", [vm = table.getHandle()](ssq::Object worldObject, int id) {
        auto* world = borrowed<World>(worldObject);
        if (!world) return invalid(vm);
        auto target = world->resolveJoint2D(id);
        if (!target) return script::projectStatusResult(vm, target.status());
        auto link = PhysicsJointLink::fromJoint(target.value().get());
        if (!link) return script::projectStatusResult(vm, link.status());
        return own(vm, Result<std::unique_ptr<ScriptJointObserver>>::success(std::make_unique<ScriptJointObserver>(
                           ScriptJointObserver{world, world->lifetimeToken(), link.value(), id})));
    });
    table.addFunc("createJoint2D", [vm = table.getHandle()](ssq::Object worldObject, ssq::Object aObject,
                                                            ssq::Object bObject, ssq::Object settingsObject) {
        auto* world = borrowed<World>(worldObject);
        auto* a     = borrowed<Body>(aObject);
        auto* b     = borrowed<Body>(bObject);
        auto* s     = borrowed<ScriptJoint2DSettings>(settingsObject);
        if (!world || !a || !b || !s) return invalid(vm);
        auto native = s->native();
        if (!native.ok()) return script::projectStatusResult(vm, native.status());
        return own(vm, Joint2D::create(*world, *a, *b, native.value()));
    });
    table.addFunc("createGearJoint2D", [vm = table.getHandle()](ssq::Object worldObject, ssq::Object aObject,
                                                                ssq::Object bObject, float ratio) {
        auto* world = borrowed<World>(worldObject);
        if (!world) return invalid(vm);
        auto a = jointInput(aObject);
        if (!a) return script::projectStatusResult(vm, a.status());
        auto b = jointInput(bObject);
        if (!b) return script::projectStatusResult(vm, b.status());
        return own(vm, Joint2D::createGear(*world, a.value().get(), b.value().get(), ratio));
    });
}
}  // namespace eve::physics
