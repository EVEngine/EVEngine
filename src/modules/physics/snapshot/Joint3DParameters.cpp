#include "physics/snapshot/Joint3DParameters.h"
#include <box3d/box3d.h>
#include <functional>
#include <limits>
#include <vector>
#include "physics/snapshot/WorldSnapshotCodec.h"

namespace eve::physics::snapshot_detail {
namespace {
using Numbers = std::vector<float>;
struct NumericField {
    const char*                                    name;
    std::size_t                                    count;
    float                                          minimum, maximum;
    bool                                           ordered = false, quaternion = false;
    std::function<Numbers(b3JointId)>              read;
    std::function<void(b3JointId, const Numbers&)> write;
};
struct BooleanField {
    const char* name;
    bool (*read)(b3JointId);
    void (*write)(b3JointId, bool);
};
struct Fields {
    std::vector<NumericField> numbers;
    std::vector<BooleanField> flags;
};
constexpr float largest = std::numeric_limits<float>::max();
NumericField    scalar(const char* name, float (*get)(b3JointId), void (*set)(b3JointId, float), float minimum = 0,
                       float maximum = largest) {
    return {name,
               1,
               minimum,
               maximum,
               false,
               false,
               [get](b3JointId id) { return Numbers{get(id)}; },
               [set](b3JointId id, const Numbers& n) { set(id, n[0]); }};
}
NumericField pair(const char* name, float (*lower)(b3JointId), float (*upper)(b3JointId),
                  void (*set)(b3JointId, float, float), float minimum = -largest, float maximum = largest) {
    return {name,
            2,
            minimum,
            maximum,
            true,
            false,
            [lower, upper](b3JointId id) { return Numbers{lower(id), upper(id)}; },
            [set](b3JointId id, const Numbers& n) { set(id, n[0], n[1]); }};
}
Fields fields(b3JointType kind) {
    Fields f;
    f.flags.push_back({"collideConnected", b3Joint_GetCollideConnected, b3Joint_SetCollideConnected});
    f.numbers.push_back(scalar("forceThreshold", b3Joint_GetForceThreshold, b3Joint_SetForceThreshold));
    f.numbers.push_back(scalar("torqueThreshold", b3Joint_GetTorqueThreshold, b3Joint_SetTorqueThreshold));
    f.numbers.push_back({"constraintTuning", 2, 0, largest, false, false,
                         [](b3JointId id) {
                             float h, d;
                             b3Joint_GetConstraintTuning(id, &h, &d);
                             return Numbers{h, d};
                         },
                         [](b3JointId id, const Numbers& n) { b3Joint_SetConstraintTuning(id, n[0], n[1]); }});
    switch (kind) {
        case b3_distanceJoint:
            f.flags.push_back({"SpringEnabled", b3DistanceJoint_IsSpringEnabled, b3DistanceJoint_EnableSpring});
            f.flags.push_back({"MotorEnabled", b3DistanceJoint_IsMotorEnabled, b3DistanceJoint_EnableMotor});
            f.numbers.push_back(
                scalar("SpringHertz", b3DistanceJoint_GetSpringHertz, b3DistanceJoint_SetSpringHertz, 0, largest));
            f.numbers.push_back(scalar("SpringDampingRatio", b3DistanceJoint_GetSpringDampingRatio,
                                       b3DistanceJoint_SetSpringDampingRatio, 0, largest));
            f.numbers.push_back(scalar("MaxMotorForce", b3DistanceJoint_GetMaxMotorForce,
                                       b3DistanceJoint_SetMaxMotorForce, 0, largest));
            f.numbers.push_back(
                scalar("MotorSpeed", b3DistanceJoint_GetMotorSpeed, b3DistanceJoint_SetMotorSpeed, -largest, largest));
            f.flags.push_back({"LimitEnabled", b3DistanceJoint_IsLimitEnabled, b3DistanceJoint_EnableLimit});
            f.numbers.push_back(scalar("Length", b3DistanceJoint_GetLength, b3DistanceJoint_SetLength, 0, largest));
            f.numbers.push_back(pair("LengthRange", b3DistanceJoint_GetMinLength, b3DistanceJoint_GetMaxLength,
                                     b3DistanceJoint_SetLengthRange, 0, largest));
            f.numbers.push_back(
                {"SpringForceRange", 2, -largest, largest, true, false,
                 [](b3JointId id) {
                     float a, b;
                     b3DistanceJoint_GetSpringForceRange(id, &a, &b);
                     return Numbers{a, b};
                 },
                 [](b3JointId id, const Numbers& n) { b3DistanceJoint_SetSpringForceRange(id, n[0], n[1]); }});
            break;
        case b3_revoluteJoint:
            f.flags.push_back({"SpringEnabled", b3RevoluteJoint_IsSpringEnabled, b3RevoluteJoint_EnableSpring});
            f.flags.push_back({"MotorEnabled", b3RevoluteJoint_IsMotorEnabled, b3RevoluteJoint_EnableMotor});
            f.numbers.push_back(
                scalar("SpringHertz", b3RevoluteJoint_GetSpringHertz, b3RevoluteJoint_SetSpringHertz, 0, largest));
            f.numbers.push_back(scalar("SpringDampingRatio", b3RevoluteJoint_GetSpringDampingRatio,
                                       b3RevoluteJoint_SetSpringDampingRatio, 0, largest));
            f.numbers.push_back(scalar("MaxMotorTorque", b3RevoluteJoint_GetMaxMotorTorque,
                                       b3RevoluteJoint_SetMaxMotorTorque, 0, largest));
            f.numbers.push_back(
                scalar("MotorSpeed", b3RevoluteJoint_GetMotorSpeed, b3RevoluteJoint_SetMotorSpeed, -largest, largest));
            f.flags.push_back({"LimitEnabled", b3RevoluteJoint_IsLimitEnabled, b3RevoluteJoint_EnableLimit});
            f.numbers.push_back(pair("Limits", b3RevoluteJoint_GetLowerLimit, b3RevoluteJoint_GetUpperLimit,
                                     b3RevoluteJoint_SetLimits, -largest, largest));
            f.numbers.push_back(
                scalar("TargetAngle", b3RevoluteJoint_GetTargetAngle, b3RevoluteJoint_SetTargetAngle, -B3_PI, B3_PI));
            break;
        case b3_prismaticJoint:
            f.flags.push_back({"SpringEnabled", b3PrismaticJoint_IsSpringEnabled, b3PrismaticJoint_EnableSpring});
            f.flags.push_back({"MotorEnabled", b3PrismaticJoint_IsMotorEnabled, b3PrismaticJoint_EnableMotor});
            f.numbers.push_back(
                scalar("SpringHertz", b3PrismaticJoint_GetSpringHertz, b3PrismaticJoint_SetSpringHertz, 0, largest));
            f.numbers.push_back(scalar("SpringDampingRatio", b3PrismaticJoint_GetSpringDampingRatio,
                                       b3PrismaticJoint_SetSpringDampingRatio, 0, largest));
            f.numbers.push_back(scalar("MaxMotorForce", b3PrismaticJoint_GetMaxMotorForce,
                                       b3PrismaticJoint_SetMaxMotorForce, 0, largest));
            f.numbers.push_back(scalar("MotorSpeed", b3PrismaticJoint_GetMotorSpeed, b3PrismaticJoint_SetMotorSpeed,
                                       -largest, largest));
            f.flags.push_back({"LimitEnabled", b3PrismaticJoint_IsLimitEnabled, b3PrismaticJoint_EnableLimit});
            f.numbers.push_back(pair("Limits", b3PrismaticJoint_GetLowerLimit, b3PrismaticJoint_GetUpperLimit,
                                     b3PrismaticJoint_SetLimits, -largest, largest));
            f.numbers.push_back(scalar("TargetTranslation", b3PrismaticJoint_GetTargetTranslation,
                                       b3PrismaticJoint_SetTargetTranslation, -largest, largest));
            break;
        case b3_sphericalJoint:
            f.flags.push_back({"SpringEnabled", b3SphericalJoint_IsSpringEnabled, b3SphericalJoint_EnableSpring});
            f.flags.push_back({"MotorEnabled", b3SphericalJoint_IsMotorEnabled, b3SphericalJoint_EnableMotor});
            f.numbers.push_back(
                scalar("SpringHertz", b3SphericalJoint_GetSpringHertz, b3SphericalJoint_SetSpringHertz, 0, largest));
            f.numbers.push_back(scalar("SpringDampingRatio", b3SphericalJoint_GetSpringDampingRatio,
                                       b3SphericalJoint_SetSpringDampingRatio, 0, largest));
            f.numbers.push_back(scalar("MaxMotorTorque", b3SphericalJoint_GetMaxMotorTorque,
                                       b3SphericalJoint_SetMaxMotorTorque, 0, largest));
            f.flags.push_back(
                {"ConeLimitEnabled", b3SphericalJoint_IsConeLimitEnabled, b3SphericalJoint_EnableConeLimit});
            f.flags.push_back(
                {"TwistLimitEnabled", b3SphericalJoint_IsTwistLimitEnabled, b3SphericalJoint_EnableTwistLimit});
            f.numbers.push_back(
                scalar("ConeLimit", b3SphericalJoint_GetConeLimit, b3SphericalJoint_SetConeLimit, 0, 0.5f * B3_PI));
            f.numbers.push_back(pair("TwistLimits", b3SphericalJoint_GetLowerTwistLimit,
                                     b3SphericalJoint_GetUpperTwistLimit, b3SphericalJoint_SetTwistLimits, -largest,
                                     largest));
            f.numbers.push_back({"TargetRotation", 4, -1, 1, false, true,
                                 [](b3JointId id) {
                                     auto q = b3SphericalJoint_GetTargetRotation(id);
                                     return Numbers{q.v.x, q.v.y, q.v.z, q.s};
                                 },
                                 [](b3JointId id, const Numbers& n) {
                                     b3SphericalJoint_SetTargetRotation(id, {{n[0], n[1], n[2]}, n[3]});
                                 }});
            f.numbers.push_back(
                {"MotorVelocity", 3, -largest, largest, false, false,
                 [](b3JointId id) {
                     auto v = b3SphericalJoint_GetMotorVelocity(id);
                     return Numbers{v.x, v.y, v.z};
                 },
                 [](b3JointId id, const Numbers& n) { b3SphericalJoint_SetMotorVelocity(id, {n[0], n[1], n[2]}); }});
            break;
        case b3_wheelJoint:
            f.flags.push_back({"SuspensionEnabled", b3WheelJoint_IsSuspensionEnabled, b3WheelJoint_EnableSuspension});
            f.flags.push_back(
                {"SuspensionLimitEnabled", b3WheelJoint_IsSuspensionLimitEnabled, b3WheelJoint_EnableSuspensionLimit});
            f.flags.push_back({"SpinMotorEnabled", b3WheelJoint_IsSpinMotorEnabled, b3WheelJoint_EnableSpinMotor});
            f.flags.push_back({"SteeringEnabled", b3WheelJoint_IsSteeringEnabled, b3WheelJoint_EnableSteering});
            f.flags.push_back(
                {"SteeringLimitEnabled", b3WheelJoint_IsSteeringLimitEnabled, b3WheelJoint_EnableSteeringLimit});
            f.numbers.push_back(scalar("SuspensionHertz", b3WheelJoint_GetSuspensionHertz,
                                       b3WheelJoint_SetSuspensionHertz, 0, largest));
            f.numbers.push_back(scalar("SuspensionDampingRatio", b3WheelJoint_GetSuspensionDampingRatio,
                                       b3WheelJoint_SetSuspensionDampingRatio, 0, largest));
            f.numbers.push_back(
                scalar("MaxSpinTorque", b3WheelJoint_GetMaxSpinTorque, b3WheelJoint_SetMaxSpinTorque, 0, largest));
            f.numbers.push_back(
                scalar("SteeringHertz", b3WheelJoint_GetSteeringHertz, b3WheelJoint_SetSteeringHertz, 0, largest));
            f.numbers.push_back(scalar("SteeringDampingRatio", b3WheelJoint_GetSteeringDampingRatio,
                                       b3WheelJoint_SetSteeringDampingRatio, 0, largest));
            f.numbers.push_back(scalar("MaxSteeringTorque", b3WheelJoint_GetMaxSteeringTorque,
                                       b3WheelJoint_SetMaxSteeringTorque, 0, largest));
            f.numbers.push_back(scalar("SpinMotorSpeed", b3WheelJoint_GetSpinMotorSpeed, b3WheelJoint_SetSpinMotorSpeed,
                                       -largest, largest));
            f.numbers.push_back(scalar("TargetSteeringAngle", b3WheelJoint_GetTargetSteeringAngle,
                                       b3WheelJoint_SetTargetSteeringAngle, -largest, largest));
            f.numbers.push_back(pair("SuspensionLimits", b3WheelJoint_GetLowerSuspensionLimit,
                                     b3WheelJoint_GetUpperSuspensionLimit, b3WheelJoint_SetSuspensionLimits, -largest,
                                     largest));
            f.numbers.push_back(pair("SteeringLimits", b3WheelJoint_GetLowerSteeringLimit,
                                     b3WheelJoint_GetUpperSteeringLimit, b3WheelJoint_SetSteeringLimits, -largest,
                                     largest));
            break;
        case b3_weldJoint:
            f.numbers.push_back(scalar("LinearHertz", b3WeldJoint_GetLinearHertz, b3WeldJoint_SetLinearHertz));
            f.numbers.push_back(
                scalar("LinearDampingRatio", b3WeldJoint_GetLinearDampingRatio, b3WeldJoint_SetLinearDampingRatio));
            f.numbers.push_back(scalar("AngularHertz", b3WeldJoint_GetAngularHertz, b3WeldJoint_SetAngularHertz));
            f.numbers.push_back(
                scalar("AngularDampingRatio", b3WeldJoint_GetAngularDampingRatio, b3WeldJoint_SetAngularDampingRatio));
            break;
        case b3_parallelJoint:
            f.numbers.push_back(scalar("SpringHertz", b3ParallelJoint_GetSpringHertz, b3ParallelJoint_SetSpringHertz));
            f.numbers.push_back(scalar("SpringDampingRatio", b3ParallelJoint_GetSpringDampingRatio,
                                       b3ParallelJoint_SetSpringDampingRatio));
            f.numbers.push_back(scalar("MaxTorque", b3ParallelJoint_GetMaxTorque, b3ParallelJoint_SetMaxTorque));
            break;
        case b3_motorJoint:
            f.numbers.push_back(
                scalar("MaxVelocityForce", b3MotorJoint_GetMaxVelocityForce, b3MotorJoint_SetMaxVelocityForce));
            f.numbers.push_back(
                scalar("MaxVelocityTorque", b3MotorJoint_GetMaxVelocityTorque, b3MotorJoint_SetMaxVelocityTorque));
            f.numbers.push_back(scalar("LinearHertz", b3MotorJoint_GetLinearHertz, b3MotorJoint_SetLinearHertz));
            f.numbers.push_back(
                scalar("LinearDampingRatio", b3MotorJoint_GetLinearDampingRatio, b3MotorJoint_SetLinearDampingRatio));
            f.numbers.push_back(scalar("AngularHertz", b3MotorJoint_GetAngularHertz, b3MotorJoint_SetAngularHertz));
            f.numbers.push_back(scalar("AngularDampingRatio", b3MotorJoint_GetAngularDampingRatio,
                                       b3MotorJoint_SetAngularDampingRatio));
            f.numbers.push_back(
                scalar("MaxSpringForce", b3MotorJoint_GetMaxSpringForce, b3MotorJoint_SetMaxSpringForce));
            f.numbers.push_back(
                scalar("MaxSpringTorque", b3MotorJoint_GetMaxSpringTorque, b3MotorJoint_SetMaxSpringTorque));
            f.numbers.push_back(
                {"LinearVelocity", 3, -largest, largest, false, false,
                 [](b3JointId id) {
                     auto v = b3MotorJoint_GetLinearVelocity(id);
                     return Numbers{v.x, v.y, v.z};
                 },
                 [](b3JointId id, const Numbers& n) { b3MotorJoint_SetLinearVelocity(id, {n[0], n[1], n[2]}); }});
            f.numbers.push_back(
                {"AngularVelocity", 3, -largest, largest, false, false,
                 [](b3JointId id) {
                     auto v = b3MotorJoint_GetAngularVelocity(id);
                     return Numbers{v.x, v.y, v.z};
                 },
                 [](b3JointId id, const Numbers& n) { b3MotorJoint_SetAngularVelocity(id, {n[0], n[1], n[2]}); }});
            break;
        default: break;
    }
    return f;
}
Result<void> invalid(const std::string& name) {
    return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "Invalid 3D joint parameter",
                                                   "payload.joints.parameters." + name));
}
}  // namespace
Value captureJoint3DParameters(b3JointId id) {
    const auto    definition = fields(b3Joint_GetType(id));
    Value::Object out;
    for (const auto& f : definition.flags) out.emplace(f.name, Value(f.read(id)));
    for (const auto& f : definition.numbers) {
        Value::Array values;
        for (float x : f.read(id)) values.emplace_back(x);
        out.emplace(f.name, Value(std::move(values)));
    }
    return Value(std::move(out));
}
Result<void> restoreJoint3DParameters(b3JointId id, const Value& value) {
    const auto* object     = value.getIf<Value::Object>();
    const auto  definition = fields(b3Joint_GetType(id));
    if (!object || object->size() != definition.flags.size() + definition.numbers.size()) return invalid("fields");
    std::vector<bool>    flags;
    std::vector<Numbers> numbers;
    for (const auto& f : definition.flags) {
        auto parsed = readBool(*object, f.name);
        if (!parsed) return Result<void>::failure(parsed.status());
        flags.push_back(parsed.value());
    }
    for (const auto& f : definition.numbers) {
        auto parsed = readFloatArray(*object, f.name);
        if (!parsed) return Result<void>::failure(parsed.status());
        const auto& n = parsed.value();
        if (n.size() != f.count) return invalid(f.name);
        for (float x : n)
            if (x < f.minimum || x > f.maximum) return invalid(f.name);
        if (f.ordered && n[0] > n[1]) return invalid(f.name);
        if (f.quaternion && !b3IsNormalizedQuat({{n[0], n[1], n[2]}, n[3]})) return invalid(f.name);
        numbers.push_back(std::move(parsed).takeValue());
    }
    // No setter runs until every field has passed validation.
    for (std::size_t i = 0; i < definition.numbers.size(); ++i) definition.numbers[i].write(id, numbers[i]);
    for (std::size_t i = 0; i < definition.flags.size(); ++i) definition.flags[i].write(id, flags[i]);
    return Result<void>::success();
}
}  // namespace eve::physics::snapshot_detail
