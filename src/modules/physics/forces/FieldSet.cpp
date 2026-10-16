#include "physics/forces/FieldSet.h"
#include <Box2D/Box2D.h>
#include <box3d/box3d.h>
#include <set>
#include <type_traits>
#include "physics/Body.h"
#include "physics/Body3D.h"
#include "physics/World.h"
#include "physics/World3D.h"
#include "physics/forces/ForceMath.h"

namespace eve::physics {
namespace {
using namespace force_detail;
Result<ForceVector> sample(const FieldDefinition& field, ForceVector position, double mass) {
    return std::visit(
        [&](const auto& model) -> Result<ForceVector> {
            using T = std::decay_t<decltype(model)>;
            if constexpr (std::is_same_v<T, DirectionalField>)
                return ForceModels::directional(model, position, mass);
            else if constexpr (std::is_same_v<T, DirectionalForceField>)
                return ForceModels::directionalForce(model, position);
            else
                return ForceModels::radial(model, position, mass);
        },
        field.model);
}
ForceVector center(const Body& body) {
    const auto p = body.raw()->GetWorldCenter();
    return {p.x, p.y, 0};
}
ForceVector center(const Body3D& body) {
    return {body.getWorldCenterX(), body.getWorldCenterY(), body.getWorldCenterZ()};
}
template <class W>
Result<ForceBatch> prepareFields(W& world, std::span<const FieldDefinition> fields,
                                 std::span<const FieldTarget> targets) {
    std::set<PhysicsBodyHandle> seen;
    std::vector<ForceLoad>      loads;
    loads.reserve(targets.size());
    for (const auto& target : targets) {
        auto resolved = target.body.resolve(world);
        if (!resolved) return Result<ForceBatch>::failure(resolved.status());
        auto& body = *resolved.value();
        if (!body.isActive() || body.getType() != "dynamic" || !seen.insert(target.body.body).second)
            return invalid<ForceBatch>();
        const auto   point = center(body);
        const double mass  = body.getMass();
        if (!isFinite(point) || !isPositive(mass)) return invalid<ForceBatch>();
        ForceVector sum{};
        for (const auto& field : fields) {
            if (!(target.categories & field.filter.includeAny) || (target.categories & field.filter.excludeAny))
                continue;
            auto value = sample(field, point, mass);
            if (!value) return Result<ForceBatch>::failure(value.status());
            sum = add(sum, value.value());
            if (!isFinite(sum)) return invalid<ForceBatch>();
        }
        // Keep zero loads in the batch so their target identities are also revalidated at commit.
        loads.push_back({target.body, sum, point, {}});
    }
    return ForceBatch::prepare(world, loads);
}
}  // namespace
Result<FieldSet> FieldSet::build(std::span<const FieldDefinition> fields) {
    for (const auto& field : fields) {
        const auto origin    = std::visit([](const auto& model) { return model.region.center; }, field.model);
        auto       validated = sample(field, origin, 1);
        if (!validated) return Result<FieldSet>::failure(validated.status());
    }
    return Result<FieldSet>::success(FieldSet(fields));
}
Result<ForceBatch> FieldSet::prepare(World& world, std::span<const FieldTarget> targets) const {
    return prepareFields(world, fields_, targets);
}
Result<ForceBatch> FieldSet::prepare(World3D& world, std::span<const FieldTarget> targets) const {
    return prepareFields(world, fields_, targets);
}
}  // namespace eve::physics
