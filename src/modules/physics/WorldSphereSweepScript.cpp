#include "physics/WorldSphereSweepScript.h"
#include "common/SquirrelBinding.h"
#include "common/ValueRead.h"
#include "physics/World3D.h"

namespace eve::physics {
namespace {
// Script boundary accepts the owning event envelope and ignores unrelated event fields.
eve::Result<void> applySweepImpulse(World3D& world, const ssq::Object& event, PhysicsVector3D impulse,
                                    PhysicsVector3D point) {
    auto decoded = eve::script::valueFromSquirrel(event);
    if (!decoded) return eve::Result<void>::failure(decoded.status());
    const auto* object = decoded.value().getIf<eve::Value::Object>();
    if (!object)
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                 "Expected a physical contact event", "contact"));
    const char*   fields[] = {"worldIndex", "worldGeneration", "bodyIndex", "bodyGeneration", "bodyId"};
    std::uint32_t parts[5]{};
    for (std::size_t i = 0; i < 5; ++i) {
        const auto found = object->find(fields[i]);
        if (found == object->end())
            return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                     "Missing physical contact identity", fields[i]));
        auto part = eve::readUInt32Value(found->second);
        if (!part) return eve::Result<void>::failure(part.status());
        parts[i] = part.value();
    }
    const auto epochField = object->find("worldEpoch");
    if (epochField == object->end() || parts[4] > static_cast<std::uint32_t>(INT32_MAX))
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "Missing world revision or invalid body ID", "contact"));
    auto epoch = eve::readInt64Value(epochField->second);
    if (!epoch) return eve::Result<void>::failure(epoch.status());
    if (epoch.value() <= 0)
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                 "World revision must be positive", "worldEpoch"));
    eve::WorldSphereContact contact;
    contact.identity   = {(static_cast<std::uint64_t>(parts[1]) << 32) | parts[0],
                          (static_cast<std::uint64_t>(parts[3]) << 32) | parts[2]};
    contact.bodyId     = static_cast<int>(parts[4]);
    contact.worldEpoch = static_cast<std::uint64_t>(epoch.value());
    return world.applySweepContactImpulse(contact, impulse, point);
}
}  // namespace

ssq::Table projectSweepContactImpulse(World3D* world, const ssq::Object& event, PhysicsVector3D impulse,
                                      PhysicsVector3D point) {
    const auto vm = event.getHandle();
    if (!world)
        return eve::script::projectResult(
            vm, eve::Result<void>::failure(
                    eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "World3D must not be null", "world")));
    return eve::script::projectResult(vm, applySweepImpulse(*world, event, impulse, point));
}
}  // namespace eve::physics
