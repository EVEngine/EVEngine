#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>
#include "building/BuildingAssemblyInternal.h"
#include "building/PlacementSystem.h"
#include "grid/GridConfig.h"

namespace eve::building {
using namespace assembly_detail;
namespace {
void knownFields(const Value& value, std::initializer_list<const char*> names) {
    if (!value.isObject()) throw std::runtime_error("Expected schema object");
    for (const auto& key : value.keys()) {
        if (std::none_of(names.begin(), names.end(), [&](const char* name) { return key == name; }))
            throw std::runtime_error("Unknown core field: " + key);
    }
}
void knownInstance(const Value& value) {
    const auto shape = codec::instanceValue(BuildingInstanceSnapshot{});
    if (!value.isObject()) throw std::runtime_error("Expected instance object");
    for (const auto& key : value.keys()) {
        const auto* expected = shape.find(key);
        const auto* actual   = value.find(key);
        if (!expected || (expected->type() != actual->type() && !(expected->isDouble() && actual->isInt64())))
            throw std::runtime_error("Unknown or incorrectly typed instance field: " + key);
    }
}
void knownCurve(const Value& value) {
    knownFields(value, {"groupId", "buildingId", "level", "subdivisions", "controlPoints", "surfaceProviderName",
                        "surfaceId", "surfaceRevision", "surfaceSamples", "instances"});
    if (const auto* values = value.find("instances"); values && values->isArray())
        for (const auto& instance : *values->getIf<Value::Array>()) knownInstance(instance);
}
const Value& required(const Value& v, const char* key) {
    const auto* p = v.find(key);
    if (!p) throw std::runtime_error(std::string("Missing field: ") + key);
    return *p;
}
int integer(const Value& v, int min = std::numeric_limits<int>::min(), int max = std::numeric_limits<int>::max()) {
    if (!v.isInt64() || v.asInt() < min || v.asInt() > max) throw std::runtime_error("Integer outside schema range");
    return int(v.asInt());
}
float number(const Value& v) {
    if (!v.isNumeric()) throw std::runtime_error("Expected number");
    double n = v.isInt64() ? double(v.asInt()) : v.asDouble();
    if (!std::isfinite(n) || std::abs(n) > 1e10) throw std::runtime_error("Invalid finite number");
    return float(n);
}
const std::string& text(const Value& v) {
    if (!v.isString()) throw std::runtime_error("Expected string");
    return v.asString();
}
const Value::Array& array(const Value& v) {
    auto* a = v.getIf<Value::Array>();
    if (!a) throw std::runtime_error("Expected array");
    return *a;
}
void schema(const Value& v, const char* id) {
    if (text(required(v, "schema")) != id || integer(required(v, "version")) != 1)
        throw std::runtime_error("Unsupported schema/version; no implicit migration");
}
template <class T>
T identity(const Value& v) {
    auto id = T::parse(text(v));
    if (!id || id->isNil()) throw std::runtime_error("Invalid persistent identity");
    return *id;
}
template <class Map>
Value strings(const Map& map) {
    Value::Object o;
    for (const auto& [k, v] : map) o[k] = v;
    return o;
}
}  // namespace
Result<Value> AssemblyService::encodeBlueprint(const BuildingBlueprint& b) {
    Value::Array parts, curves;
    for (const auto& p : b.parts)
        parts.emplace_back(Value::Object{{"key", int64_t(p.id.value)}, {"instance", codec::instanceValue(p.local)}});
    for (const auto& c : b.curves) curves.emplace_back(codec::edgeCurveValue(c.group, c.instances));
    Value v     = Value::Object{{"schema", "eve.building.blueprint"},
                                {"version", 1},
                                {"id", b.id.format()},
                                {"assetVersion", int64_t(b.version)},
                                {"cellSize", b.cellSize},
                                {"floorHeight", b.floorHeight},
                                {"parts", std::move(parts)},
                                {"curves", std::move(curves)},
                                {"definitions", b.definitions}};
    auto  check = decodeBlueprint(v);
    if (!check.ok()) return fail<Value>("Cannot encode invalid blueprint");
    return Result<Value>::success(std::move(v));
}
Result<BuildingBlueprint> AssemblyService::decodeBlueprint(const Value& v) {
    try {
        knownFields(v, {"schema", "version", "id", "assetVersion", "cellSize", "floorHeight", "parts", "curves",
                        "definitions"});
        schema(v, "eve.building.blueprint");
        BuildingBlueprint b;
        b.id          = identity<AssetGuid>(required(v, "id"));
        b.version     = uint64_t(integer(required(v, "assetVersion"), 1));
        b.cellSize    = number(required(v, "cellSize"));
        b.floorHeight = number(required(v, "floorHeight"));
        if (b.cellSize <= 0 || b.floorHeight <= 0) throw std::runtime_error("Invalid blueprint scale");
        b.definitions = required(v, "definitions");
        auto defs     = checkDefinitions(b.definitions);
        if (!defs.ok())
            return fail<BuildingBlueprint>("Blueprint definitions missing or changed", DiagnosticCode::Conflict);
        std::set<int> keys;
        for (const auto& p : array(required(v, "parts"))) {
            knownFields(p, {"key", "instance"});
            knownInstance(required(p, "instance"));
            int        key    = integer(required(p, "key"), 1, std::numeric_limits<int>::max() - 1);
            auto       part   = codec::parseInstance(required(p, "instance"));
            const bool partOk = part.ok();
            if (!keys.insert(key).second || !partOk || part.value().instanceId != key ||
                !part.value().garrison.empty() || !b.definitions.find(part.value().buildingId))
                throw std::runtime_error("Invalid blueprint part or external entity reference");
            b.parts.push_back({{uint32_t(key)}, std::move(part).takeValue()});
        }
        if (b.parts.empty() || b.parts.size() > 100000)
            throw std::runtime_error("Blueprint must contain 1..100000 parts");
        std::set<int> curveMembers;
        for (const auto& c : array(required(v, "curves"))) {
            knownCurve(c);
            auto curve = codec::parseEdgeCurve(c);
            if (!curve.ok()) throw std::runtime_error("Invalid curve payload");
            for (const auto& p : curve.value().instances) {
                auto found = std::find_if(b.parts.begin(), b.parts.end(),
                                          [&](const auto& part) { return part.local.instanceId == p.instanceId; });
                if (found == b.parts.end() || !curveMembers.insert(p.instanceId).second ||
                    codec::instanceValue(found->local) != codec::instanceValue(p))
                    throw std::runtime_error("Curve membership differs from blueprint parts");
            }
            b.curves.push_back(std::move(curve).takeValue());
        }
        return Result<BuildingBlueprint>::success(std::move(b));
    } catch (const std::exception& e) {
        return fail<BuildingBlueprint>(e.what());
    }
}
Result<Value> AssemblyService::snapshotWorld(const PlacementWorld& w) {
    auto checked = checkGrid(w);
    if (!checked.ok()) return fail<Value>("World grid is not serializable by house schema");
    Value::Array instances, terrain, curves, houses;
    for (int id : w.instanceOrder_) {
        auto it = w.buildings_.find(id);
        if (it == w.buildings_.end()) return fail<Value>("Invalid world instance order");
        instances.emplace_back(codec::instanceValue(codec::snapshot(it->second)));
    }
    for (int y = 0; y < w.height_; ++y)
        for (int x = 0; x < w.width_; ++x) terrain.emplace_back(w.getTerrain(x, y));
    for (auto id : w.edgeCurveGroupIds()) {
        const auto&                           g = w.edgeCurveGroups_.at(id.value);
        std::vector<BuildingInstanceSnapshot> members;
        for (int member : g.instanceIds) {
            auto it = w.buildings_.find(member);
            if (it == w.buildings_.end()) return fail<Value>("Dangling curve member");
            members.push_back(codec::snapshot(it->second));
        }
        curves.emplace_back(codec::edgeCurveValue(g, members));
    }
    for (const auto& [id, a] : w.assemblyState_->records) {
        Value::Array members;
        for (const auto& [key, member] : a.members)
            members.emplace_back(Value::Object{
                {"key", int64_t(key)}, {"record", member}, {"persistentId", id.child(std::to_string(key)).format()}});
        houses.emplace_back(Value::Object{{"id", id.format()},
                                          {"blueprint", a.blueprint.format()},
                                          {"nextPartKey", int64_t(a.nextPartKey)},
                                          {"blueprintVersion", int64_t(a.blueprintVersion)},
                                          {"frame", poseValue(a.frame)},
                                          {"members", std::move(members)}});
    }
    Value result = Value::Object{{"schema", "eve.building.world"},
                                 {"version", 1},
                                 {"worldId", w.id_},
                                 {"width", w.width_},
                                 {"height", w.height_},
                                 {"cellSize", w.getCellSize()},
                                 {"plane", w.getGridPlaneName()},
                                 {"originX", w.getOriginX()},
                                 {"originY", w.getOriginY()},
                                 {"floorHeight", w.floorHeight_},
                                 {"activeLevel", w.activeLevel_},
                                 {"snapMode", w.snapMode_},
                                 {"validateRule", w.validateRule_},
                                 {"extra", strings(w.extra_)},
                                 {"nextInstance", w.instanceCounter_},
                                 {"nextCurve", int64_t(w.nextEdgeCurveGroupId_)},
                                 {"terrain", std::move(terrain)},
                                 {"instances", std::move(instances)},
                                 {"curves", std::move(curves)},
                                 {"assemblies", std::move(houses)},
                                 {"definitions", definitionsFor(w)}};
    return Result<Value>::success(std::move(result));
}
Result<std::unique_ptr<PlacementWorld>> AssemblyService::decodeWorld(const Value& v) {
    try {
        knownFields(
            v, {"schema",       "version",   "worldId",     "width",       "height",   "cellSize",     "plane",
                "originX",      "originY",   "floorHeight", "activeLevel", "snapMode", "validateRule", "extra",
                "nextInstance", "nextCurve", "terrain",     "instances",   "curves",   "assemblies",   "definitions"});
        schema(v, "eve.building.world");
        auto defs = checkDefinitions(required(v, "definitions"));
        if (!defs.ok()) throw std::runtime_error("World definitions missing or changed");
        int width = integer(required(v, "width"), 1, 4096), height = integer(required(v, "height"), 1, 4096);
        if (int64_t(width) * height > 4000000) throw std::runtime_error("World exceeds cell budget");
        float size = number(required(v, "cellSize"));
        if (size <= 0) throw std::runtime_error("Invalid cell size");
        auto w            = std::make_unique<PlacementWorld>(width, height, size);
        w->publishEvents_ = false;
        w->id_            = text(required(v, "worldId"));
        auto plane        = text(required(v, "plane"));
        if (plane != "xz" && plane != "xy") throw std::runtime_error("Invalid plane");
        w->setGridPlane(plane);
        w->setOrigin(number(required(v, "originX")), number(required(v, "originY")));
        w->floorHeight_ = number(required(v, "floorHeight"));
        if (w->floorHeight_ <= 0) throw std::runtime_error("Invalid floor height");
        w->activeLevel_   = integer(required(v, "activeLevel"));
        w->snapMode_      = text(required(v, "snapMode"));
        w->validateRule_  = text(required(v, "validateRule"));
        const auto& extra = required(v, "extra");
        if (!extra.isObject()) throw std::runtime_error("Invalid extras");
        for (const auto& key : extra.keys()) w->extra_[key] = text(*extra.find(key));
        const auto& terrain = array(required(v, "terrain"));
        if (terrain.size() != size_t(width) * height) throw std::runtime_error("Terrain dimensions mismatch");
        for (size_t i = 0; i < terrain.size(); ++i) w->terrain_[i] = integer(terrain[i]);
        w->terrainOverrides_.assign(terrain.size(), 1);
        w->instanceCounter_   = integer(required(v, "nextInstance"), 0);
        const auto& nextCurve = required(v, "nextCurve");
        if (!nextCurve.isInt64() || nextCurve.asInt() <= 0) throw std::runtime_error("Invalid curve sequence");
        w->nextEdgeCurveGroupId_ = uint64_t(nextCurve.asInt());
        for (const auto& record : array(required(v, "instances"))) {
            knownInstance(record);
            auto p = codec::parseInstance(record);
            if (!p.ok()) throw std::runtime_error("Invalid instance payload");
            int id = p.value().instanceId;
            if (id > w->instanceCounter_ || !required(v, "definitions").find(p.value().buildingId) ||
                !w->buildings_.emplace(id, codec::runtime(p.value())).second)
                throw std::runtime_error("Duplicate/unpinned instance record");
            w->instanceOrder_.push_back(id);
        }
        for (const auto& record : array(required(v, "curves"))) {
            knownCurve(record);
            auto c = codec::parseEdgeCurve(record);
            if (!c.ok()) throw std::runtime_error("Invalid curve");
            auto& payload = c.value();
            for (const auto& p : payload.instances) {
                auto it = w->buildings_.find(p.instanceId);
                if (it == w->buildings_.end() ||
                    codec::instanceValue(codec::snapshot(it->second)) != codec::instanceValue(p))
                    throw std::runtime_error("Curve member payload mismatch");
            }
            if (payload.group.id.value >= w->nextEdgeCurveGroupId_ ||
                !w->edgeCurveGroups_.emplace(payload.group.id.value, payload.group).second)
                throw std::runtime_error("Duplicate curve record");
        }
        for (const auto& record : array(required(v, "assemblies"))) {
            knownFields(record, {"id", "blueprint", "blueprintVersion", "frame", "members", "nextPartKey"});
            BuildingAssembly a;
            a.id               = identity<AssemblyId>(required(record, "id"));
            a.blueprint        = identity<AssetGuid>(required(record, "blueprint"));
            a.blueprintVersion = uint64_t(integer(required(record, "blueprintVersion"), 1));
            a.nextPartKey      = uint32_t(integer(required(record, "nextPartKey"), 1));
            auto pose          = parsePose(required(record, "frame"));
            if (!pose.ok()) throw std::runtime_error("Invalid house frame");
            a.frame = pose.value();
            for (const auto& m : array(required(record, "members"))) {
                knownFields(m, {"key", "record", "persistentId"});
                int key = integer(required(m, "key"), 1), member = integer(required(m, "record"), 1);
                if (!a.members.emplace(uint32_t(key), member).second ||
                    text(required(m, "persistentId")) != a.id.child(std::to_string(key)).format())
                    throw std::runtime_error("Invalid persistent part identity");
            }
            if (!w->assemblyState_->records.emplace(a.id, a).second)
                throw std::runtime_error("Duplicate house identity");
            w->assemblyState_->handles[a.id] = eve::RuntimeHandle<AssemblyHandleTag>(w->assemblyState_->nextSlot++, 1);
        }
        auto valid = validateFinal(*w);
        if (!valid.ok())
            return fail<std::unique_ptr<PlacementWorld>>("Saved world failed final-state validation",
                                                         DiagnosticCode::Conflict);
        return Result<std::unique_ptr<PlacementWorld>>::success(std::move(w));
    } catch (const std::exception& e) {
        return fail<std::unique_ptr<PlacementWorld>>(e.what());
    }
}
Result<void> AssemblyService::restoreWorld(PlacementWorld& w, const Value& v) {
    auto parsed = decodeWorld(v);
    if (!parsed.ok()) return fail<void>("World restore rejected without mutation", DiagnosticCode::Conflict);
    w.swapState(*parsed.value());
    if (w.publishEvents_) {
        BuildingChangeEvent e;
        e.action  = "reset";
        e.worldId = w.getId();
        PlacementSystem::pushEvent(std::move(e));
    }
    return Result<void>::success();
}
Result<void> AssemblyService::applyReceipt(PlacementWorld& w, const Value& expected, const Value& desired) {
    auto current = snapshotWorld(w);
    if (!current.ok() || current.value() != expected)
        return fail<void>("Undo/redo endpoint no longer matches world", DiagnosticCode::Conflict);
    return restoreWorld(w, desired);
}
}  // namespace eve::building
