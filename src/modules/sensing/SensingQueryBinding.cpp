#include "sensing/SensingQueryBinding.h"
#include <simplesquirrel/simplesquirrel.hpp>
#include "common/SquirrelBinding.h"
#include "sensing/SensingQueryCodec.h"

namespace eve::sensing {
namespace {
ssq::Table failure(HSQUIRRELVM vm) {
    return script::projectStatusResult(
        vm, Status::failure(
                Diagnostic::error(DiagnosticCode::StaleHandle, "sensing world is not live", "world", {}, "sensing")));
}
EnvironmentServices services() {
    EnvironmentServices result;
    if (const auto* los = cap::query<ILineOfSightQuery>()) result.lineOfSight = std::cref(*los);
    return result;
}
ssq::Table project(HSQUIRRELVM vm, Result<Value>&& result) {
    return script::projectResult(vm, std::move(result), [](Value value) { return value; });
}
Result<Value> query(SensingWorld& world, const std::string& json) {
    auto request = decodeSensingQuery(json);
    if (!request) return Result<Value>::failure(request.status());
    const auto injected = services();
    auto       result   = world.query(request.value().origin, request.value().spec, injected.lineOfSight);
    if (!result) return Result<Value>::failure(result.status());
    EnvironmentQueryResult output;
    output.option = "subjects";
    for (const auto& candidate : result.value().ranked()) {
        auto point = candidate.space == CoordinateSpace::World3D
                         ? WorldPoint::world3D(candidate.x, candidate.y, candidate.z)
                         : WorldPoint::world2D(candidate.x, candidate.y);
        if (!point) return Result<Value>::failure(point.status());
        output.ranked.push_back({{output.ranked.size(), TargetLocation{point.value()}, candidate.id}, candidate.score});
    }
    return Result<Value>::success(output.toValue());
}
class ScriptEnvironmentPlan {
public:
    explicit ScriptEnvironmentPlan(EnvironmentQueryPlan plan) : plan_(std::move(plan)) {}
    Result<Value> run(const std::string& json) const {
        auto request = decodeEnvironmentRequest(json);
        if (!request) return Result<Value>::failure(request.status());
        auto instance = plan_.instantiate(std::move(request).takeValue());
        if (!instance) return Result<Value>::failure(instance.status());
        auto result = instance.value().run(services());
        if (!result) return Result<Value>::failure(result.status());
        return Result<Value>::success(result.value().toValue());
    }

private:
    EnvironmentQueryPlan plan_;
};
}  // namespace
void exposeSensingQueries(ssq::Table& table, ssq::Class& worldClass) {
    const auto vm = table.getHandle();
    worldClass.addFunc(
        "upsertPosition", [vm](ScriptSensingWorld* proxy, const std::string& id, float x, float y, float z,
                               const std::string& faction, const std::string& tags, const std::string& visibleTo) {
            if (!proxy) return failure(vm);
            auto world = Sensing::resolve(proxy->reference);
            if (!world.isBound()) return failure(vm);
            return script::projectResult(vm, world->upsertPosition(id, x, y, z, faction, tags, visibleTo));
        });
    worldClass.addFunc("rebuildIndex", [vm](ScriptSensingWorld* proxy, const std::string& kind, float cellSize) {
        if (!proxy) return failure(vm);
        auto world = Sensing::resolve(proxy->reference);
        if (!world.isBound()) return failure(vm);
        SpatialIndexKind index;
        if (kind == "none")
            index = SpatialIndexKind::None;
        else if (kind == "xy")
            index = SpatialIndexKind::XY;
        else if (kind == "xz")
            index = SpatialIndexKind::XZ;
        else if (kind == "yz")
            index = SpatialIndexKind::YZ;
        else if (kind == "xyz")
            index = SpatialIndexKind::XYZ;
        else
            return script::projectStatusResult(
                vm, Status::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "unknown index kind", "kind", {},
                                                      "sensing")));
        return script::projectResult(vm, world->rebuildIndex(index, cellSize));
    });
    worldClass.addFunc("query", [vm](ScriptSensingWorld* proxy, const std::string& json) {
        if (!proxy) return failure(vm);
        auto world = Sensing::resolve(proxy->reference);
        if (!world.isBound()) return failure(vm);
        return project(vm, query(*world.operator->(), json));
    });
    auto planClass = table.addClass<ScriptEnvironmentPlan>(
        "EnvironmentQueryPlan", std::function<ScriptEnvironmentPlan*()>([] { return nullptr; }), true);
    planClass.addFunc("run", [vm](ScriptEnvironmentPlan* plan, const std::string& json) {
        return plan ? project(vm, plan->run(json)) : failure(vm);
    });
    table.addFunc("compileEnvironmentQuery", [vm](const std::string& json) {
        auto definition = decodeEnvironmentDefinition(json);
        if (!definition) return script::projectStatusResult(vm, definition.status());
        auto plan = EnvironmentQueryPlan::compile(definition.value());
        if (!plan) return script::projectStatusResult(vm, plan.status());
        auto instance = script::makeOwnedSquirrelInstance<ScriptEnvironmentPlan>(
            vm, std::make_unique<ScriptEnvironmentPlan>(std::move(plan).takeValue()));
        if (!instance) return script::projectStatusResult(vm, instance.status());
        auto result = script::projectStatusResult(vm, Status::success(StatusCode::Applied));
        result.set("value", std::move(instance).takeValue());
        result.set("ownership", std::string("owned"));
        return result;
    });
}
}  // namespace eve::sensing
