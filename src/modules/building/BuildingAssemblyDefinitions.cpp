#include "building/BuildingAssemblyInternal.h"
#include "building/BuildingDef.h"
namespace eve::building::assembly_detail {
Value definitionValue(const BuildingDefinition& d) {
    Value result(Value::Object{});
    result.set("id", d.id);
    result.set("displayName", d.displayName);
    result.set("category", d.category);
    result.set("channel", d.channel);
    result.set("renderMode", d.renderMode);
    result.set("placementKind", d.placementKind);
    result.set("freeRadiusCells", d.freeRadiusCells);
    result.set("freeFootprintWidthCells", d.freeFootprintWidthCells);
    result.set("freeFootprintHeightCells", d.freeFootprintHeightCells);
    result.set("connectionGroup", d.connectionGroup);
    result.set("edgeThicknessCells", d.edgeThicknessCells);
    {
        Value::Array a;
        for (const auto& c : d.blockedByEdgeChannels) a.emplace_back(c);
        result.set("blockedByEdgeChannels", std::move(a));
    }
    result.set("structuralRole", d.structuralRole);
    result.set("supportMode", d.supportMode);
    result.set("supportTag", d.supportTag);
    result.set("footprintW", d.footprintW);
    result.set("footprintH", d.footprintH);
    result.set("snapMode", d.snapMode);
    result.set("rotationMode", d.rotationMode);
    result.set("validateRule", d.validateRule);
    result.set("maxSurfaceSlopeDegrees", d.maxSurfaceSlopeDegrees);
    result.set("maxSurfaceHeightDelta", d.maxSurfaceHeightDelta);
    result.set("requireAdjacentTag", d.requireAdjacentTag);
    result.set("requireAdjacentTerrain", d.requireAdjacentTerrain);
    {
        Value::Array a;
        for (const auto& item : d.freeFootprintVertices) a.emplace_back(item);
        result.set("freeFootprintVertices", std::move(a));
    }
    {
        Value::Array a;
        for (const auto& item : d.footprintMask) a.emplace_back(item);
        result.set("footprintMask", std::move(a));
    }
    {
        Value::Array a;
        for (const auto& item : d.tags) a.emplace_back(item);
        result.set("tags", std::move(a));
    }
    {
        Value::Array a;
        for (const auto& item : d.requireTerrain) a.emplace_back(item);
        result.set("requireTerrain", std::move(a));
    }
    {
        Value::Array a;
        for (const auto& item : d.forbidTerrain) a.emplace_back(item);
        result.set("forbidTerrain", std::move(a));
    }
    {
        Value::Object a;
        for (const auto& [key, item] : d.cost) a.emplace(key, item);
        result.set("cost", std::move(a));
    }
    {
        Value::Object a;
        for (const auto& [key, item] : d.extra) a.emplace(key, item);
        result.set("extra", std::move(a));
    }
    {
        Value::Object a;
        for (const auto& [key, item] : d.visual2d) a.emplace(key, item);
        result.set("visual2d", std::move(a));
    }
    {
        Value::Object a;
        for (const auto& [key, item] : d.visual3d) a.emplace(key, item);
        result.set("visual3d", std::move(a));
    }
    return result;
}
Value definitionsFor(const PlacementWorld& w) {
    Value::Object result;
    for (const auto& [id, p] : w.buildings())
        if (const auto* d = BuildingRegistry::find(p.buildingId)) result[p.buildingId] = definitionValue(*d);
    return result;
}
Result<void> checkDefinitions(const Value& value) {
    if (!value.isObject()) return fail<void>("Missing pinned definitions");
    for (const auto& key : value.keys()) {
        const auto* def = BuildingRegistry::find(key);
        if (!def || definitionValue(*def) != *value.find(key))
            return fail<void>("Definition changed or missing: " + key, DiagnosticCode::Conflict);
    }
    return Result<void>::success();
}
}  // namespace eve::building::assembly_detail
