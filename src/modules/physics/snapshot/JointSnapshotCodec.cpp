#include "physics/mechanics/Joint2D.h"
#include "physics/snapshot/WorldSnapshotAccess.h"
#include "physics/snapshot/WorldSnapshotCodec.h"

#include <array>
#include <map>

namespace eve::physics {
namespace {
using namespace snapshot_detail;
using Object = Value::Object;
template <class T>
Result<T> malformed(const char* message) {
    return Result<T>::failure(Diagnostic::error(DiagnosticCode::ParseError, message, "payload.joints"));
}
void anchors(Object& o, b2Vec2 a, b2Vec2 b) {
    o.emplace("ax", Value(a.x));
    o.emplace("ay", Value(a.y));
    o.emplace("bx", Value(b.x));
    o.emplace("by", Value(b.y));
}
void spring(Object& o, float frequency, float damping) {
    o.emplace("frequency", Value(frequency));
    o.emplace("damping", Value(damping));
}
void motor(Object& o, bool enabled, float speed, float maximum) {
    o.emplace("motorEnabled", Value(enabled));
    o.emplace("speed", Value(speed));
    o.emplace("maximum", Value(maximum));
}
template <class J>
void limits(Object& o, const J& j) {
    o.emplace("limitEnabled", Value(j.IsLimitEnabled()));
    o.emplace("lower", Value(j.GetLowerLimit()));
    o.emplace("upper", Value(j.GetUpperLimit()));
}
Result<std::vector<float>> parameters(const Object& data, std::initializer_list<std::string_view> names) {
    if (!hasExactFields(data, names))
        return malformed<std::vector<float>>("Joint parameters have unknown or missing fields");
    std::vector<float> result;
    for (auto name : names) {
        if (name == "motorEnabled" || name == "limitEnabled") {
            auto value = readBool(data, name);
            if (!value) return Result<std::vector<float>>::failure(value.status());
            result.push_back(value.value() ? 1.f : 0.f);
        } else {
            auto value = readFloat(data, name);
            if (!value) return Result<std::vector<float>>::failure(value.status());
            result.push_back(value.value());
        }
    }
    return Result<std::vector<float>>::success(std::move(result));
}
Result<int> positiveId(const Object& o, std::string_view name) {
    auto id = readInt64(o, name);
    if (!id) return Result<int>::failure(id.status());
    if (id.value() <= 0 || id.value() >= std::numeric_limits<int>::max())
        return malformed<int>("Joint topology id is out of range");
    return Result<int>::success(static_cast<int>(id.value()));
}
int bodyId(b2Body& body) {
    const auto* wrapper = static_cast<const Body*>(body.GetUserData());
    return wrapper ? wrapper->getId() : -1;
}
}  // namespace

Result<Value> WorldSnapshotAccess::captureJoints2D(const World& world) {
    if (static_cast<std::size_t>(world.raw()->GetJointCount()) != world.joints_.size())
        return Result<Value>::failure(Diagnostic::error(
            DiagnosticCode::Unsupported, "Snapshot cannot capture unmanaged backend joints", "physics.world.snapshot"));
    std::vector<Joint2D*> sorted(world.joints_.begin(), world.joints_.end());
    std::sort(sorted.begin(), sorted.end(), [](auto* a, auto* b) { return a->getId() < b->getId(); });
    Value::Array output;
    for (const auto* wrapper : sorted) {
        auto&  raw = *wrapper->joint_;
        Object data;
        switch (wrapper->kind_) {
            case Joint2DKind::Revolute: {
                const auto& j = static_cast<const b2RevoluteJoint&>(raw);
                anchors(data, j.GetLocalAnchorA(), j.GetLocalAnchorB());
                data.emplace("referenceAngle", Value(j.GetReferenceAngle()));
                limits(data, j);
                motor(data, j.IsMotorEnabled(), j.GetMotorSpeed(), j.GetMaxMotorTorque());
                break;
            }
            case Joint2DKind::Prismatic: {
                const auto& j = static_cast<const b2PrismaticJoint&>(raw);
                anchors(data, j.GetLocalAnchorA(), j.GetLocalAnchorB());
                data.emplace("referenceAngle", Value(j.GetReferenceAngle()));
                data.emplace("axisX", Value(j.GetLocalAxisA().x));
                data.emplace("axisY", Value(j.GetLocalAxisA().y));
                limits(data, j);
                motor(data, j.IsMotorEnabled(), j.GetMotorSpeed(), j.GetMaxMotorForce());
                break;
            }
            case Joint2DKind::Distance: {
                const auto& j = static_cast<const b2DistanceJoint&>(raw);
                anchors(data, j.GetLocalAnchorA(), j.GetLocalAnchorB());
                data.emplace("length", Value(j.GetLength()));
                spring(data, j.GetFrequency(), j.GetDampingRatio());
                break;
            }
            case Joint2DKind::Wheel: {
                const auto& j = static_cast<const b2WheelJoint&>(raw);
                anchors(data, j.GetLocalAnchorA(), j.GetLocalAnchorB());
                data.emplace("axisX", Value(j.GetLocalAxisA().x));
                data.emplace("axisY", Value(j.GetLocalAxisA().y));
                spring(data, j.GetSpringFrequencyHz(), j.GetSpringDampingRatio());
                motor(data, j.IsMotorEnabled(), j.GetMotorSpeed(), j.GetMaxMotorTorque());
                break;
            }
            case Joint2DKind::Weld: {
                const auto& j = static_cast<const b2WeldJoint&>(raw);
                anchors(data, j.GetLocalAnchorA(), j.GetLocalAnchorB());
                data.emplace("referenceAngle", Value(j.GetReferenceAngle()));
                spring(data, j.GetFrequency(), j.GetDampingRatio());
                break;
            }
            case Joint2DKind::Pulley: {
                const auto& j = static_cast<const b2PulleyJoint&>(raw);
                anchors(data, {wrapper->pulleyLocalA_.x, wrapper->pulleyLocalA_.y},
                        {wrapper->pulleyLocalB_.x, wrapper->pulleyLocalB_.y});
                data.emplace("groundAx", Value(j.GetGroundAnchorA().x));
                data.emplace("groundAy", Value(j.GetGroundAnchorA().y));
                data.emplace("groundBx", Value(j.GetGroundAnchorB().x));
                data.emplace("groundBy", Value(j.GetGroundAnchorB().y));
                data.emplace("lengthA", Value(j.GetLengthA()));
                data.emplace("lengthB", Value(j.GetLengthB()));
                data.emplace("ratio", Value(j.GetRatio()));
                break;
            }
            case Joint2DKind::Motor: {
                const auto& j = static_cast<const b2MotorJoint&>(raw);
                data.emplace("offsetX", Value(j.GetLinearOffset().x));
                data.emplace("offsetY", Value(j.GetLinearOffset().y));
                data.emplace("angle", Value(j.GetAngularOffset()));
                data.emplace("force", Value(j.GetMaxForce()));
                data.emplace("torque", Value(j.GetMaxTorque()));
                data.emplace("correction", Value(j.GetCorrectionFactor()));
                break;
            }
            case Joint2DKind::Gear: {
                const auto& j     = static_cast<const b2GearJoint&>(raw);
                int         first = -1, second = -1;
                for (const auto* source : sorted) {
                    if (source->handle_ == wrapper->firstSource_) first = source->getId();
                    if (source->handle_ == wrapper->secondSource_) second = source->getId();
                }
                if (first < 0 || second < 0) return malformed<Value>("Gear source is missing");
                data.emplace("first", Value(std::to_string(first)));
                data.emplace("second", Value(std::to_string(second)));
                data.emplace("ratio", Value(j.GetRatio()));
                Value::Array poses;
                for (float value : wrapper->gearReferencePoses_) poses.emplace_back(value);
                data.emplace("referencePoses", Value(std::move(poses)));
                break;
            }
        }
        output.emplace_back(Object{{"id", Value(std::to_string(wrapper->getId()))},
                                   {"kind", Value(std::to_string(static_cast<int>(wrapper->kind_)))},
                                   {"bodyA", Value(std::to_string(bodyId(*raw.GetBodyA())))},
                                   {"bodyB", Value(std::to_string(bodyId(*raw.GetBodyB())))},
                                   {"collideConnected", Value(raw.GetCollideConnected())},
                                   {"data", Value(std::move(data))}});
    }
    return Result<Value>::success(Value(std::move(output)));
}

Result<void> WorldSnapshotAccess::restoreJoints2D(World& w, const Value::Array& values,
                                                  std::uint32_t firstHandleIndex) try {
    if (values.size() > static_cast<std::uint64_t>(PhysicsJointHandle::invalidIndex) - firstHandleIndex)
        return malformed<void>("Restored joint handles exceed available identity space");
    w.nextJointHandleIndex_ = firstHandleIndex;
    struct Record {
        int           id, a, b;
        Joint2DKind   kind;
        bool          collide;
        const Object* data;
    };
    std::vector<Record>     records;
    std::set<int>           ids;
    std::map<int, Body*>    bodies;
    std::map<int, Joint2D*> joints;
    for (auto* body : w.bodies_) bodies.emplace(body->getId(), body);
    for (const auto& value : values) {
        const auto* o = value.getIf<Object>();
        if (!o || !hasExactFields(*o, {"id", "kind", "bodyA", "bodyB", "collideConnected", "data"}))
            return malformed<void>("Joint record has unknown or missing fields");
        auto id = positiveId(*o, "id");
        if (!id) return Result<void>::failure(id.status());
        auto a = positiveId(*o, "bodyA");
        if (!a) return Result<void>::failure(a.status());
        auto b = positiveId(*o, "bodyB");
        if (!b) return Result<void>::failure(b.status());
        auto kind = readInt64(*o, "kind");
        if (!kind) return Result<void>::failure(kind.status());
        auto collide = readBool(*o, "collideConnected");
        if (!collide) return Result<void>::failure(collide.status());
        const auto* data = field(*o, "data")->getIf<Object>();
        if (!data || !ids.insert(id.value()).second || !bodies.contains(a.value()) || !bodies.contains(b.value()) ||
            a.value() == b.value() || kind.value() < 0 || kind.value() > static_cast<int>(Joint2DKind::Motor))
            return malformed<void>("Invalid joint kind, duplicated id or missing/distinct endpoint");
        if (bodies.at(a.value())->raw()->GetType() != b2_dynamicBody &&
            bodies.at(b.value())->raw()->GetType() != b2_dynamicBody)
            return malformed<void>("Joint requires at least one dynamic endpoint");
        records.push_back(
            {id.value(), a.value(), b.value(), static_cast<Joint2DKind>(kind.value()), collide.value(), data});
    }
    // Gear dependencies are restricted to revolute/prismatic; normals always precede gears regardless of input order.
    std::sort(records.begin(), records.end(), [](const Record& a, const Record& b) {
        if ((a.kind == Joint2DKind::Gear) != (b.kind == Joint2DKind::Gear)) return a.kind != Joint2DKind::Gear;
        return a.id < b.id;
    });
    auto adopt = [&](const Record& record, b2JointDef& def) -> Joint2D& {
        def.bodyA            = bodies.at(record.a)->raw();
        def.bodyB            = bodies.at(record.b)->raw();
        def.collideConnected = record.collide;
        auto* raw            = w.raw()->CreateJoint(&def);
        auto  owned          = std::unique_ptr<Joint2D>(
            new Joint2D(w, *raw, record.kind, PhysicsJointHandle(w.nextJointHandleIndex_++, 1u)));
        owned->id_   = record.id;
        auto& result = *owned;
        w.joints_.insert(owned.get());
        w.restoredJoints2D_.push_back(std::move(owned));
        joints.emplace(record.id, &result);
        return result;
    };
    for (const auto& r : records) {
        const auto& o = *r.data;
        switch (r.kind) {
            case Joint2DKind::Motor: {
                auto parsed = parameters(o, {"offsetX", "offsetY", "angle", "force", "torque", "correction"});
                if (!parsed) return Result<void>::failure(parsed.status());
                const auto& p = parsed.value();
                if (p[3] < 0 || p[4] < 0 || p[5] < 0 || p[5] > 1) return malformed<void>("Invalid motor parameters");
                b2MotorJointDef d;
                d.linearOffset.Set(p[0], p[1]);
                d.angularOffset    = p[2];
                d.maxForce         = p[3];
                d.maxTorque        = p[4];
                d.correctionFactor = p[5];
                adopt(r, d);
                break;
            }
            case Joint2DKind::Revolute: {
                auto parsed = parameters(o, {"ax", "ay", "bx", "by", "referenceAngle", "limitEnabled", "lower", "upper",
                                             "motorEnabled", "speed", "maximum"});
                if (!parsed) return Result<void>::failure(parsed.status());
                const auto& p = parsed.value();
                if (p[6] > p[7] || p[10] < 0) return malformed<void>("Invalid revolute limit/motor parameters");
                b2RevoluteJointDef d;
                d.localAnchorA.Set(p[0], p[1]);
                d.localAnchorB.Set(p[2], p[3]);
                d.referenceAngle = p[4];
                d.enableLimit    = p[5] != 0;
                d.lowerAngle     = p[6];
                d.upperAngle     = p[7];
                d.enableMotor    = p[8] != 0;
                d.motorSpeed     = p[9];
                d.maxMotorTorque = p[10];
                adopt(r, d);
                break;
            }
            case Joint2DKind::Prismatic: {
                auto parsed = parameters(o, {"ax", "ay", "bx", "by", "referenceAngle", "axisX", "axisY", "limitEnabled",
                                             "lower", "upper", "motorEnabled", "speed", "maximum"});
                if (!parsed) return Result<void>::failure(parsed.status());
                const auto& p = parsed.value();
                if (p[8] > p[9] || p[12] < 0 || std::abs(std::hypot(p[5], p[6]) - 1.f) > 1e-5f)
                    return malformed<void>("Invalid prismatic axis/limit/motor parameters");
                b2PrismaticJointDef d;
                d.localAnchorA.Set(p[0], p[1]);
                d.localAnchorB.Set(p[2], p[3]);
                d.referenceAngle = p[4];
                d.localAxisA.Set(p[5], p[6]);
                d.enableLimit      = p[7] != 0;
                d.lowerTranslation = p[8];
                d.upperTranslation = p[9];
                d.enableMotor      = p[10] != 0;
                d.motorSpeed       = p[11];
                d.maxMotorForce    = p[12];
                adopt(r, d);
                break;
            }
            case Joint2DKind::Distance: {
                auto parsed = parameters(o, {"ax", "ay", "bx", "by", "length", "frequency", "damping"});
                if (!parsed) return Result<void>::failure(parsed.status());
                const auto& p = parsed.value();
                if (p[4] <= 0 || p[5] < 0 || p[6] < 0) return malformed<void>("Invalid distance spring parameters");
                b2DistanceJointDef d;
                d.localAnchorA.Set(p[0], p[1]);
                d.localAnchorB.Set(p[2], p[3]);
                d.length       = p[4];
                d.frequencyHz  = p[5];
                d.dampingRatio = p[6];
                adopt(r, d);
                break;
            }
            case Joint2DKind::Wheel: {
                auto parsed = parameters(o, {"ax", "ay", "bx", "by", "axisX", "axisY", "frequency", "damping",
                                             "motorEnabled", "speed", "maximum"});
                if (!parsed) return Result<void>::failure(parsed.status());
                const auto& p = parsed.value();
                if (p[6] < 0 || p[7] < 0 || p[10] < 0 || std::abs(std::hypot(p[4], p[5]) - 1.f) > 1e-5f)
                    return malformed<void>("Invalid wheel axis/spring/motor parameters");
                b2WheelJointDef d;
                d.localAnchorA.Set(p[0], p[1]);
                d.localAnchorB.Set(p[2], p[3]);
                d.localAxisA.Set(p[4], p[5]);
                d.frequencyHz    = p[6];
                d.dampingRatio   = p[7];
                d.enableMotor    = p[8] != 0;
                d.motorSpeed     = p[9];
                d.maxMotorTorque = p[10];
                adopt(r, d);
                break;
            }
            case Joint2DKind::Weld: {
                auto parsed = parameters(o, {"ax", "ay", "bx", "by", "referenceAngle", "frequency", "damping"});
                if (!parsed) return Result<void>::failure(parsed.status());
                const auto& p = parsed.value();
                if (p[5] < 0 || p[6] < 0) return malformed<void>("Invalid weld spring parameters");
                b2WeldJointDef d;
                d.localAnchorA.Set(p[0], p[1]);
                d.localAnchorB.Set(p[2], p[3]);
                d.referenceAngle = p[4];
                d.frequencyHz    = p[5];
                d.dampingRatio   = p[6];
                adopt(r, d);
                break;
            }
            case Joint2DKind::Pulley: {
                auto parsed = parameters(o, {"ax", "ay", "bx", "by", "groundAx", "groundAy", "groundBx", "groundBy",
                                             "lengthA", "lengthB", "ratio"});
                if (!parsed) return Result<void>::failure(parsed.status());
                const auto& p = parsed.value();
                if (p[8] <= b2_linearSlop || p[9] <= b2_linearSlop || p[10] <= b2_epsilon ||
                    !std::isfinite(p[8] + p[10] * p[9]))
                    return malformed<void>("Invalid pulley reference length or ratio");
                b2PulleyJointDef d;
                d.localAnchorA.Set(p[0], p[1]);
                d.localAnchorB.Set(p[2], p[3]);
                d.groundAnchorA.Set(p[4], p[5]);
                d.groundAnchorB.Set(p[6], p[7]);
                d.lengthA       = p[8];
                d.lengthB       = p[9];
                d.ratio         = p[10];
                auto& j         = adopt(r, d);
                j.pulleyLocalA_ = {p[0], p[1]};
                j.pulleyLocalB_ = {p[2], p[3]};
                break;
            }
            case Joint2DKind::Gear: {
                if (!hasExactFields(o, {"first", "second", "ratio", "referencePoses"}))
                    return malformed<void>("Invalid gear fields");
                auto first = positiveId(o, "first");
                if (!first) return Result<void>::failure(first.status());
                auto second = positiveId(o, "second");
                if (!second) return Result<void>::failure(second.status());
                auto ratio = readFloat(o, "ratio");
                if (!ratio) return Result<void>::failure(ratio.status());
                if (!joints.contains(first.value()) || !joints.contains(second.value()) ||
                    first.value() == second.value() || ratio.value() == 0 || r.collide)
                    return malformed<void>("Missing gear sources or invalid gear ratio/collision policy");
                auto* a          = joints.at(first.value());
                auto* b          = joints.at(second.value());
                auto  sourceKind = [](Joint2DKind kind) {
                    return kind == Joint2DKind::Revolute || kind == Joint2DKind::Prismatic;
                };
                if (!sourceKind(a->kind_) || !sourceKind(b->kind_) || bodyId(*a->joint_->GetBodyB()) != r.a ||
                    bodyId(*b->joint_->GetBodyB()) != r.b || a->joint_->GetBodyB()->GetType() != b2_dynamicBody ||
                    b->joint_->GetBodyB()->GetType() != b2_dynamicBody)
                    return malformed<void>("Invalid gear source kinds or endpoints");
                const auto* poses = field(o, "referencePoses")->getIf<Value::Array>();
                if (!poses || poses->size() != 12)
                    return malformed<void>("Gear reference poses must contain four XY-angle triples");
                std::array<float, 12> reference{}, original{};
                for (std::size_t i = 0; i < reference.size(); ++i) {
                    auto value = readFloat(Object{{"value", (*poses)[i]}}, "value");
                    if (!value) return Result<void>::failure(value.status());
                    reference[i] = value.value();
                }
                const std::array<b2Body*, 4> endpoints{a->joint_->GetBodyA(), a->joint_->GetBodyB(),
                                                       b->joint_->GetBodyA(), b->joint_->GetBodyB()};
                for (std::size_t i = 0; i < endpoints.size(); ++i) {
                    original[3 * i]     = endpoints[i]->GetPosition().x;
                    original[3 * i + 1] = endpoints[i]->GetPosition().y;
                    original[3 * i + 2] = endpoints[i]->GetAngle();
                    for (std::size_t j = 0; j < i; ++j)
                        if (endpoints[i] == endpoints[j] &&
                            (reference[3 * i] != reference[3 * j] || reference[3 * i + 1] != reference[3 * j + 1] ||
                             reference[3 * i + 2] != reference[3 * j + 2]))
                            return malformed<void>("Shared gear endpoint has inconsistent reference poses");
                }
                for (std::size_t i = 0; i < endpoints.size(); ++i)
                    endpoints[i]->SetTransform({reference[3 * i], reference[3 * i + 1]}, reference[3 * i + 2]);
                auto coordinate = [](Joint2D& j) {
                    return j.kind_ == Joint2DKind::Revolute
                               ? static_cast<b2RevoluteJoint*>(j.joint_)->GetJointAngle()
                               : static_cast<b2PrismaticJoint*>(j.joint_)->GetJointTranslation();
                };
                if (!std::isfinite(coordinate(*a) + ratio.value() * coordinate(*b)))
                    return malformed<void>("Gear reference constant overflows");
                b2GearJointDef d;
                d.joint1              = a->joint_;
                d.joint2              = b->joint_;
                d.ratio               = ratio.value();
                auto& j               = adopt(r, d);
                j.firstSource_        = a->handle_;
                j.secondSource_       = b->handle_;
                j.gearReferencePoses_ = reference;
                for (std::size_t i = 0; i < endpoints.size(); ++i)
                    endpoints[i]->SetTransform({original[3 * i], original[3 * i + 1]}, original[3 * i + 2]);
                break;
            }
        }
    }
    w.nextJointId_ = ids.empty() ? 1 : *ids.rbegin() + 1;
    return Result<void>::success();
} catch (const std::exception& error) {
    return Result<void>::failure(Diagnostic::error(DiagnosticCode::Failed,
                                                   std::string("Planar joint preparation failed: ") + error.what(),
                                                   "physics.world.restore.prepare"));
}
}  // namespace eve::physics
