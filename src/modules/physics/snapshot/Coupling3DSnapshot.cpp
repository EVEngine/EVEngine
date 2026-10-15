#include <cmath>
#include <map>
#include "physics/mechanics/Coupling3DInternal.h"
#include "physics/snapshot/WorldSnapshotAccess.h"
#include "physics/snapshot/WorldSnapshotCodec.h"

namespace eve::physics {
namespace {
using namespace snapshot_detail;
Result<void> invalidCoupling(const char* message) {
    return Result<void>::failure(Diagnostic::error(DiagnosticCode::ParseError, message, "payload.couplings"));
}
Result<double> number(const Value::Object& object, const char* name) {
    const auto* value = field(object, name);
    if (!value || !value->isNumeric())
        return Result<double>::failure(
            Diagnostic::error(DiagnosticCode::ParseError, "Transmission field must be numeric", name));
    const double n = value->isDouble() ? value->asDouble() : value->asInt();
    if (!std::isfinite(n))
        return Result<double>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "Transmission field must be finite", name));
    return Result<double>::success(n);
}
Value                   point(CouplingPoint3D p) { return Value(Value::Array{Value(p.x), Value(p.y), Value(p.z)}); }
Result<CouplingPoint3D> readPoint(const Value::Object& o, const char* name) {
    auto a = readFloatArray(o, name);
    if (!a) return Result<CouplingPoint3D>::failure(a.status());
    if (a.value().size() != 3)
        return Result<CouplingPoint3D>::failure(
            Diagnostic::error(DiagnosticCode::ParseError, "Transmission point needs three components", name));
    return Result<CouplingPoint3D>::success({a.value()[0], a.value()[1], a.value()[2]});
}
}  // namespace
Result<Value> WorldSnapshotAccess::captureCouplings3D(const World3D& world) {
    Value::Array records;
    for (auto [id, coupling] : world.couplings3D_) {
        const auto& d   = *coupling->data_;
        auto        row = d.row();
        if (!row) return Result<Value>::failure(row.status());
        const auto&   limits = d.limits();
        Value::Object object{{"id", Value(std::to_string(id))},
                             {"kind", Value(d.gear ? "gear" : "pulley")},
                             {"enabled", Value(d.enabled)},
                             {"impulse", Value(d.lastImpulse)},
                             {"saturated", Value(d.saturated)},
                             {"limits", Value(Value::Array{Value(limits.positionCorrection),
                                                           Value(limits.maxCorrectionSpeed), Value(limits.maxForce)})}};
        if (d.gear) {
            for (int i = 0; i < 2; ++i) {
                auto* source = world.findJoint(d.sources[i]);
                if (!source)
                    return Result<Value>::failure(
                        Diagnostic::error(DiagnosticCode::StaleHandle, "Missing gear source"));
                object.emplace(i == 0 ? "sourceA" : "sourceB", Value(std::to_string(source->getId())));
            }
            object.emplace("ratio", Value(d.gearSettings.ratio));
            object.emplace("reference", Value(d.reference));
            object.emplace("coordinateA", Value(row.value().coordinate[0]));
            object.emplace("coordinateB", Value(row.value().coordinate[1]));
        } else {
            for (int i = 0; i < 2; ++i) {
                auto* body = world.findBody(d.bodies[i]);
                if (!body)
                    return Result<Value>::failure(
                        Diagnostic::error(DiagnosticCode::StaleHandle, "Missing pulley body"));
                object.emplace(i == 0 ? "sourceA" : "sourceB", Value(std::to_string(body->getId())));
            }
            const auto& p = d.pulleySettings;
            object.emplace("ratio", Value(p.ratio));
            object.emplace("length", Value(p.length));
            object.emplace("tensionOnly", Value(p.tensionOnly));
            object.emplace("localAnchorA", point(p.localAnchorA));
            object.emplace("localAnchorB", point(p.localAnchorB));
            object.emplace("groundA", point(p.groundA));
            object.emplace("groundB", point(p.groundB));
        }
        records.emplace_back(std::move(object));
    }
    return Result<Value>::success(Value(std::move(records)));
}
Result<void> WorldSnapshotAccess::restoreCouplings3D(World3D& world, const Value::Array& values) {
    using namespace snapshot_detail;
    try {
        std::map<int, const Value::Object*> ordered;
        for (const auto& value : values) {
            const auto* o = value.getIf<Value::Object>();
            if (!o) return invalidCoupling("Transmission must be an object");
            auto id = readInt64(*o, "id");
            if (!id) return Result<void>::failure(id.status());
            if (id.value() <= 0 || id.value() >= std::numeric_limits<int>::max() ||
                !ordered.emplace(static_cast<int>(id.value()), o).second)
                return invalidCoupling("Invalid or duplicate transmission id");
        }
        std::map<int, Joint3D*> joints;
        for (auto* joint : world.joints_) joints.emplace(joint->getId(), joint);
        for (auto [id, o] : ordered) {
            auto kind = readString(*o, "kind");
            if (!kind) return Result<void>::failure(kind.status());
            const bool gear = kind.value() == "gear";
            if (!gear && kind.value() != "pulley") return invalidCoupling("Unknown transmission kind");
            if (!(gear ? hasExactFields(*o, {"id", "kind", "enabled", "impulse", "saturated", "limits", "sourceA",
                                             "sourceB", "ratio", "reference", "coordinateA", "coordinateB"})
                       : hasExactFields(*o, {"id", "kind", "enabled", "impulse", "saturated", "limits", "sourceA",
                                             "sourceB", "ratio", "length", "tensionOnly", "localAnchorA",
                                             "localAnchorB", "groundA", "groundB"})))
                return invalidCoupling("Unknown or missing transmission fields");
            auto sourceA = readInt64(*o, "sourceA");
            if (!sourceA) return Result<void>::failure(sourceA.status());
            auto sourceB = readInt64(*o, "sourceB");
            if (!sourceB) return Result<void>::failure(sourceB.status());
            if (sourceA.value() <= 0 || sourceA.value() >= std::numeric_limits<int>::max() || sourceB.value() <= 0 ||
                sourceB.value() >= std::numeric_limits<int>::max())
                return invalidCoupling("Source id out of range");
            auto enabled = readBool(*o, "enabled");
            if (!enabled) return Result<void>::failure(enabled.status());
            auto saturated = readBool(*o, "saturated");
            if (!saturated) return Result<void>::failure(saturated.status());
            auto impulse = number(*o, "impulse");
            if (!impulse) return Result<void>::failure(impulse.status());
            auto ratio = readFloat(*o, "ratio");
            if (!ratio) return Result<void>::failure(ratio.status());
            auto limits = readFloatArray(*o, "limits");
            if (!limits) return Result<void>::failure(limits.status());
            if (limits.value().size() != 3) return invalidCoupling("Transmission limits need three values");
            CouplingLimits3D policy{limits.value()[0], limits.value()[1], limits.value()[2]};
            world.nextCoupling3DId_ = id;
            std::unique_ptr<Coupling3D> owned;
            if (gear) {
                auto reference = number(*o, "reference");
                if (!reference) return Result<void>::failure(reference.status());
                auto a = number(*o, "coordinateA");
                if (!a) return Result<void>::failure(a.status());
                auto b = number(*o, "coordinateB");
                if (!b) return Result<void>::failure(b.status());
                auto first  = joints.find(static_cast<int>(sourceA.value())),
                     second = joints.find(static_cast<int>(sourceB.value()));
                if (first == joints.end() || second == joints.end()) return invalidCoupling("Missing gear source");
                auto created = Coupling3D::createGear(world, *first->second, *second->second, {ratio.value(), policy});
                if (!created) return Result<void>::failure(created.status());
                owned                    = std::move(created).takeValue();
                owned->data_->reference  = reference.value();
                owned->data_->coordinate = {a.value(), b.value()};
            } else {
                auto length = readFloat(*o, "length");
                if (!length) return Result<void>::failure(length.status());
                auto tension = readBool(*o, "tensionOnly");
                if (!tension) return Result<void>::failure(tension.status());
                auto la = readPoint(*o, "localAnchorA");
                if (!la) return Result<void>::failure(la.status());
                auto lb = readPoint(*o, "localAnchorB");
                if (!lb) return Result<void>::failure(lb.status());
                auto ga = readPoint(*o, "groundA");
                if (!ga) return Result<void>::failure(ga.status());
                auto gb = readPoint(*o, "groundB");
                if (!gb) return Result<void>::failure(gb.status());
                auto* first  = world.findBodyById(static_cast<int>(sourceA.value()));
                auto* second = world.findBodyById(static_cast<int>(sourceB.value()));
                if (!first || !second) return invalidCoupling("Missing pulley body");
                auto created = Coupling3D::createPulley(world, *first, *second,
                                                        {la.value(), lb.value(), ga.value(), gb.value(), ratio.value(),
                                                         length.value(), tension.value(), policy});
                if (!created) return Result<void>::failure(created.status());
                owned = std::move(created).takeValue();
            }
            owned->data_->enabled     = enabled.value();
            owned->data_->lastImpulse = impulse.value();
            owned->data_->saturated   = saturated.value();
            auto row                  = owned->data_->row();
            if (!row) return Result<void>::failure(row.status());
            world.restoredCouplings3D_.push_back(std::move(owned));
        }
        return Result<void>::success();
    } catch (const std::exception& e) {
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::Failed, std::string("Transmission preparation failed: ") + e.what(), "payload.couplings"));
    }
}
}  // namespace eve::physics
