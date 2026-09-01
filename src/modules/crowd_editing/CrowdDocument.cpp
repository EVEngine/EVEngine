#include "crowd_editing/CrowdDocument.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <utility>

namespace eve::crowd_editing {
namespace {

template <class T>
EditorResult<T> failure(EditorStatus status, const char* rule, std::string message) {
    return EditorResult<T>::error(status, RuleId(rule), std::move(message));
}

const EditorValue* field(const EditorValue& value, const char* name) {
    const auto* object = value.getIf<EditorValue::Object>();
    if (!object) return nullptr;
    const auto found = object->find(name);
    return found == object->end() ? nullptr : &found->second;
}

EditorValue pointValue(const CrowdWaypointRecord& point) {
    return EditorValue::Object{{"id", point.id.value()}, {"x", point.x}, {"y", point.y},
                               {"arriveRadius", point.arriveRadius}, {"waitSeconds", point.waitSeconds}};
}

EditorResult<CrowdWaypointRecord> parsePoint(const EditorValue& value) {
    const auto string = [&](const char* key) -> const std::string* {
        const auto* entry = field(value, key); return entry ? entry->getIf<std::string>() : nullptr;
    };
    const auto number = [&](const char* key) -> const double* {
        const auto* entry = field(value, key); return entry ? entry->getIf<double>() : nullptr;
    };
    const auto* id = string("id"); const auto* x = number("x"); const auto* y = number("y");
    const auto* radius = number("arriveRadius"); const auto* wait = number("waitSeconds");
    if (!id || id->empty() || !x || !y || !radius || !wait || !std::isfinite(*x) || !std::isfinite(*y) ||
        !std::isfinite(*radius) || !std::isfinite(*wait) || *radius <= 0.0 || *wait < 0.0)
        return failure<CrowdWaypointRecord>(EditorStatus::Rejected, "editor.crowd.invalid-waypoint",
                                            "Waypoint requires finite coordinates, positive radius and nonnegative wait");
    return EditorResult<CrowdWaypointRecord>::applied({StableId(*id), *x, *y, *radius, *wait});
}

EditorValue pathValue(const CrowdPathRecord& path) {
    EditorValue::Array points;
    for (const auto& point : path.points) points.push_back(pointValue(point));
    return EditorValue::Object{{"id", path.id.value()}, {"name", path.name}, {"loop", path.loop},
                               {"points", std::move(points)}};
}

EditorResult<CrowdPathRecord> parsePath(const EditorValue& value) {
    const auto* idEntry = field(value, "id"); const auto* nameEntry = field(value, "name");
    const auto* loopEntry = field(value, "loop"); const auto* pointsEntry = field(value, "points");
    const auto* id = idEntry ? idEntry->getIf<std::string>() : nullptr;
    const auto* name = nameEntry ? nameEntry->getIf<std::string>() : nullptr;
    const auto* loop = loopEntry ? loopEntry->getIf<bool>() : nullptr;
    const auto* points = pointsEntry ? pointsEntry->getIf<EditorValue::Array>() : nullptr;
    if (!id || id->empty() || !name || name->empty() || !loop || !points || points->empty())
        return failure<CrowdPathRecord>(EditorStatus::Rejected, "editor.crowd.invalid-path",
                                        "Path requires identity, name and at least one waypoint");
    CrowdPathRecord result{StableId(*id), *name, *loop, {}};
    std::set<StableId> ids;
    for (const auto& entry : *points) {
        auto point = parsePoint(entry);
        if (!point.value || !ids.insert(point.value->id).second)
            return failure<CrowdPathRecord>(EditorStatus::Rejected, "editor.crowd.duplicate-waypoint",
                                            "Path waypoint ids must be unique and valid");
        result.points.push_back(std::move(*point.value));
    }
    if (result.loop && result.points.size() < 2)
        return failure<CrowdPathRecord>(EditorStatus::Rejected, "editor.crowd.loop-point-count",
                                        "Looping paths require at least two waypoints");
    return EditorResult<CrowdPathRecord>::applied(std::move(result));
}

EditorValue agentValue(const CrowdAgentRecord& agent) {
    return EditorValue::Object{{"id", agent.id.value()}, {"archetype", agent.archetype}, {"x", agent.x},
        {"y", agent.y}, {"heading", agent.heading}, {"radius", agent.radius},
        {"maximumSpeed", agent.maximumSpeed}, {"behavior", agent.behavior}, {"path", agent.path.value()}};
}

EditorResult<CrowdAgentRecord> parseAgent(const EditorValue& value) {
    const auto string = [&](const char* key) -> const std::string* { const auto* v = field(value, key); return v ? v->getIf<std::string>() : nullptr; };
    const auto number = [&](const char* key) -> const double* { const auto* v = field(value, key); return v ? v->getIf<double>() : nullptr; };
    const auto* id = string("id"); const auto* archetype = string("archetype"); const auto* behavior = string("behavior");
    const auto* path = string("path"); const auto* x = number("x"); const auto* y = number("y");
    const auto* heading = number("heading"); const auto* radius = number("radius"); const auto* speed = number("maximumSpeed");
    static const std::set<std::string> behaviors{"idle", "flow", "seek", "boids", "path"};
    if (!id || id->empty() || !archetype || !behavior || !behaviors.contains(*behavior) || !path || !x || !y ||
        !heading || !radius || !speed || *radius <= 0.0 || *speed < 0.0)
        return failure<CrowdAgentRecord>(EditorStatus::Rejected, "editor.crowd.invalid-agent",
                                         "Agent requires identity, supported behavior and valid movement limits");
    for (const double* component : {x, y, heading, radius, speed})
        if (!std::isfinite(*component)) return failure<CrowdAgentRecord>(EditorStatus::Rejected,
            "editor.crowd.nonfinite-agent", "Agent numeric properties must be finite");
    if (*behavior == "path" && path->empty()) return failure<CrowdAgentRecord>(EditorStatus::Rejected,
        "editor.crowd.missing-agent-path", "Path behavior requires a path reference");
    return EditorResult<CrowdAgentRecord>::applied({StableId(*id), *archetype, *x, *y, *heading,
                                                     *radius, *speed, *behavior, StableId(*path)});
}

EditorValue zoneValue(const CrowdZoneRecord& zone) {
    EditorValue::Array points;
    for (const auto& point : zone.points) points.push_back(EditorValue::Array{point[0], point[1]});
    return EditorValue::Object{{"id", zone.id.value()}, {"name", zone.name}, {"kind", zone.kind},
                               {"points", std::move(points)}, {"weight", zone.weight}, {"enabled", zone.enabled}};
}

EditorResult<CrowdZoneRecord> parseZone(const EditorValue& value) {
    const auto* idEntry = field(value, "id"); const auto* nameEntry = field(value, "name");
    const auto* kindEntry = field(value, "kind"); const auto* pointsEntry = field(value, "points");
    const auto* weightEntry = field(value, "weight"); const auto* enabledEntry = field(value, "enabled");
    const auto* id = idEntry ? idEntry->getIf<std::string>() : nullptr;
    const auto* name = nameEntry ? nameEntry->getIf<std::string>() : nullptr;
    const auto* kind = kindEntry ? kindEntry->getIf<std::string>() : nullptr;
    const auto* points = pointsEntry ? pointsEntry->getIf<EditorValue::Array>() : nullptr;
    const auto* weight = weightEntry ? weightEntry->getIf<double>() : nullptr;
    const auto* enabled = enabledEntry ? enabledEntry->getIf<bool>() : nullptr;
    static const std::set<std::string> kinds{"avoid", "slow", "goal", "sense", "spawn"};
    if (!id || id->empty() || !name || name->empty() || !kind || !kinds.contains(*kind) || !points ||
        points->size() < 3 || !weight || !std::isfinite(*weight) || *weight < 0.0 || !enabled)
        return failure<CrowdZoneRecord>(EditorStatus::Rejected, "editor.crowd.invalid-zone",
                                        "Zone requires identity, supported kind, weight and at least three points");
    CrowdZoneRecord result{StableId(*id), *name, *kind, {}, *weight, *enabled};
    for (const auto& pointEntry : *points) {
        const auto* point = pointEntry.getIf<EditorValue::Array>();
        const auto* x = point && point->size() == 2 ? (*point)[0].getIf<double>() : nullptr;
        const auto* y = point && point->size() == 2 ? (*point)[1].getIf<double>() : nullptr;
        if (!x || !y || !std::isfinite(*x) || !std::isfinite(*y))
            return failure<CrowdZoneRecord>(EditorStatus::Rejected, "editor.crowd.invalid-zone-point",
                                            "Zone vertices must contain two finite coordinates");
        result.points.push_back({*x, *y});
    }
    return EditorResult<CrowdZoneRecord>::applied(std::move(result));
}

DomainOperation operation(const char* type, const char* inverseType, const std::string& target,
                          EditorValue payload, EditorValue inverse, const StableId& affected) {
    DomainOperation result;
    result.type = type; result.inverseType = inverseType; result.target = TargetId(target);
    result.payload = std::move(payload); result.inverse = std::move(inverse); result.hasInverse = true;
    result.affectedObjects.push_back({TargetId(target), affected.value(), 0}); return result;
}

template <class Record>
std::vector<Record> values(const std::map<StableId, Record>& records) {
    std::vector<Record> result; result.reserve(records.size());
    for (const auto& [id, record] : records) { (void)id; result.push_back(record); }
    return result;
}

}  // namespace

CrowdDocumentTarget::CrowdDocumentTarget(std::string id) : id_(std::move(id)) {}

TargetDescriptor CrowdDocumentTarget::describe() const {
    TargetDescriptor result; result.id = TargetId(id_); result.type = "crowd-document";
    result.revision = revision_; result.capabilities = {editorCapabilityId()}; return result;
}

void* CrowdDocumentTarget::queryCapability(const CapabilityId& capability) {
    return capability == editorCapabilityId() ? static_cast<CrowdDocumentTarget*>(this) : nullptr;
}

EditorResult<void> CrowdDocumentTarget::applyDomainOperation(const DomainOperation& op) {
    if (op.target != TargetId(id_)) return failure<void>(EditorStatus::Rejected, "editor.crowd.wrong-target", "Operation targets another crowd document");
    auto apply = [&](const std::string& type, const EditorValue& payload) -> EditorResult<void> {
        if (type == "crowd.agent.set.v1") { auto value = parseAgent(payload); if (!value.value) { EditorResult<void> r; r.status=value.status; r.diagnostics=value.diagnostics; return r; } agents_.insert_or_assign(value.value->id, *value.value); }
        else if (type == "crowd.zone.set.v1") { auto value = parseZone(payload); if (!value.value) { EditorResult<void> r; r.status=value.status; r.diagnostics=value.diagnostics; return r; } zones_.insert_or_assign(value.value->id, *value.value); }
        else if (type == "crowd.path.set.v1") { auto value = parsePath(payload); if (!value.value) { EditorResult<void> r; r.status=value.status; r.diagnostics=value.diagnostics; return r; } paths_.insert_or_assign(value.value->id, *value.value); }
        else if (type == "crowd.agent.delete.v1" || type == "crowd.zone.delete.v1" || type == "crowd.path.delete.v1") {
            const auto* id = payload.getIf<std::string>(); if (!id || id->empty()) return failure<void>(EditorStatus::Rejected, "editor.crowd.invalid-delete", "Delete payload requires a stable id");
            const StableId stable(*id); std::size_t erased = type[6] == 'a' ? agents_.erase(stable) : (type[6] == 'z' ? zones_.erase(stable) : paths_.erase(stable));
            if (!erased) return failure<void>(EditorStatus::NotFound, "editor.crowd.object-not-found", "Crowd object was not found");
        } else return failure<void>(EditorStatus::Rejected, "editor.crowd.unsupported-operation", "Unsupported crowd document operation");
        ++revision_; dirty_.include(0, 0); return EditorResult<void>::applied();
    };
    return apply(op.type, op.payload);
}

std::unique_ptr<IDomainOperationTarget> CrowdDocumentTarget::cloneDomainState() const { return std::make_unique<CrowdDocumentTarget>(*this); }
EditorResult<void> CrowdDocumentTarget::commitDomainState(std::unique_ptr<IDomainOperationTarget> candidate) {
    auto* typed = dynamic_cast<CrowdDocumentTarget*>(candidate.get());
    if (!typed || typed->id_ != id_) return failure<void>(EditorStatus::Rejected, "editor.crowd.invalid-staging-state", "Staged state belongs to another target type or id");
    *this = *typed; return EditorResult<void>::applied();
}

std::vector<CrowdAgentRecord> CrowdDocumentTarget::agents() const { return values(agents_); }
std::vector<CrowdZoneRecord> CrowdDocumentTarget::zones() const { return values(zones_); }
std::vector<CrowdPathRecord> CrowdDocumentTarget::paths() const { return values(paths_); }

EditorResult<DomainOperation> CrowdDocumentTarget::makeSetAgent(const CrowdAgentRecord& record) const {
    auto parsed = parseAgent(agentValue(record)); if (!parsed.value) return failure<DomainOperation>(parsed.status, "editor.crowd.invalid-agent", "Cannot plan invalid agent");
    const auto found = agents_.find(record.id); return EditorResult<DomainOperation>::applied(operation("crowd.agent.set.v1", found == agents_.end() ? "crowd.agent.delete.v1" : "crowd.agent.set.v1", id_, agentValue(record), found == agents_.end() ? EditorValue(record.id.value()) : agentValue(found->second), record.id));
}
EditorResult<DomainOperation> CrowdDocumentTarget::makeDeleteAgent(const StableId& id) const { const auto found=agents_.find(id); if(found==agents_.end()) return failure<DomainOperation>(EditorStatus::NotFound,"editor.crowd.agent-not-found","Agent was not found"); return EditorResult<DomainOperation>::applied(operation("crowd.agent.delete.v1","crowd.agent.set.v1",id_,id.value(),agentValue(found->second),id)); }
EditorResult<DomainOperation> CrowdDocumentTarget::makeSetZone(const CrowdZoneRecord& record) const { auto parsed=parseZone(zoneValue(record)); if(!parsed.value) return failure<DomainOperation>(parsed.status,"editor.crowd.invalid-zone","Cannot plan invalid zone"); const auto found=zones_.find(record.id); return EditorResult<DomainOperation>::applied(operation("crowd.zone.set.v1",found==zones_.end()?"crowd.zone.delete.v1":"crowd.zone.set.v1",id_,zoneValue(record),found==zones_.end()?EditorValue(record.id.value()):zoneValue(found->second),record.id)); }
EditorResult<DomainOperation> CrowdDocumentTarget::makeDeleteZone(const StableId& id) const { const auto found=zones_.find(id); if(found==zones_.end()) return failure<DomainOperation>(EditorStatus::NotFound,"editor.crowd.zone-not-found","Zone was not found"); return EditorResult<DomainOperation>::applied(operation("crowd.zone.delete.v1","crowd.zone.set.v1",id_,id.value(),zoneValue(found->second),id)); }
EditorResult<DomainOperation> CrowdDocumentTarget::makeSetPath(const CrowdPathRecord& record) const { auto parsed=parsePath(pathValue(record)); if(!parsed.value) return failure<DomainOperation>(parsed.status,"editor.crowd.invalid-path","Cannot plan invalid path"); const auto found=paths_.find(record.id); return EditorResult<DomainOperation>::applied(operation("crowd.path.set.v1",found==paths_.end()?"crowd.path.delete.v1":"crowd.path.set.v1",id_,pathValue(record),found==paths_.end()?EditorValue(record.id.value()):pathValue(found->second),record.id)); }
EditorResult<DomainOperation> CrowdDocumentTarget::makeDeletePath(const StableId& id) const { const auto found=paths_.find(id); if(found==paths_.end()) return failure<DomainOperation>(EditorStatus::NotFound,"editor.crowd.path-not-found","Path was not found"); for(const auto& [agentId,agent]:agents_) { (void)agentId; if(agent.path==id) return failure<DomainOperation>(EditorStatus::Conflict,"editor.crowd.path-in-use","Path is referenced by an agent"); } return EditorResult<DomainOperation>::applied(operation("crowd.path.delete.v1","crowd.path.set.v1",id_,id.value(),pathValue(found->second),id)); }

std::vector<EditorDiagnostic> CrowdDocumentTarget::validate() const {
    std::vector<EditorDiagnostic> result;
    for (const auto& [id, agent] : agents_) if (!agent.path.empty() && !paths_.contains(agent.path)) result.push_back({RuleId("editor.crowd.dangling-path"), DiagnosticSeverity::Error, "Agent " + id.value() + " references a missing path"});
    for (const auto& [id, path] : paths_) for (std::size_t i=1;i<path.points.size();++i) if(path.points[i-1].x==path.points[i].x && path.points[i-1].y==path.points[i].y) result.push_back({RuleId("editor.crowd.zero-path-segment"),DiagnosticSeverity::Warning,"Path " + id.value() + " contains a zero-length segment"});
    return result;
}

CrowdOverlayResult CrowdDocumentTarget::overlay(int budget) const {
    CrowdOverlayResult result; result.revision=revision_; result.diagnostics=validate();
    if (budget <= 0) { result.status=EditorStatus::Rejected; result.diagnostics.push_back({RuleId("editor.crowd.invalid-overlay-budget"),DiagnosticSeverity::Error,"Overlay primitive budget must be positive"}); return result; }
    auto add=[&](CrowdOverlayPrimitive primitive){ if(static_cast<int>(result.primitives.size())>=budget) return false; result.primitives.push_back(std::move(primitive)); return true; };
    for(const auto& [id,path]:paths_) for(std::size_t i=1;i<path.points.size();++i) if(!add({"line",id,{path.points[i-1].x,path.points[i-1].y,path.points[i].x,path.points[i].y},path.name})) goto exhausted;
    for(const auto& [id,zone]:zones_) { for(std::size_t i=0;i<zone.points.size();++i) { const auto& a=zone.points[i]; const auto& b=zone.points[(i+1)%zone.points.size()]; if(!add({"line",id,{a[0],a[1],b[0],b[1]},zone.kind})) goto exhausted; } }
    for(const auto& [id,agent]:agents_) if(!add({"circle",id,{agent.x,agent.y,agent.radius,agent.heading},agent.behavior})) goto exhausted;
    result.status=EditorStatus::Applied; return result;
exhausted:
    result.status=EditorStatus::Rejected; result.diagnostics.push_back({RuleId("editor.crowd.overlay-budget"),DiagnosticSeverity::Error,"Crowd overlay exceeds the primitive budget"}); return result;
}

EditorValue CrowdDocumentTarget::snapshotValue() const {
    EditorValue::Array agents, zones, paths; for(const auto& [id,value]:agents_){(void)id;agents.push_back(agentValue(value));} for(const auto& [id,value]:zones_){(void)id;zones.push_back(zoneValue(value));} for(const auto& [id,value]:paths_){(void)id;paths.push_back(pathValue(value));}
    return EditorValue::Object{{"schemaVersion",int64_t{1}},{"agents",std::move(agents)},{"zones",std::move(zones)},{"paths",std::move(paths)}};
}

EditorResult<void> CrowdDocumentTarget::loadSnapshot(const EditorValue& snapshot) {
    const auto* versionEntry=field(snapshot,"schemaVersion"); const auto* agentsEntry=field(snapshot,"agents"); const auto* zonesEntry=field(snapshot,"zones"); const auto* pathsEntry=field(snapshot,"paths");
    const auto* version=versionEntry?versionEntry->getIf<int64_t>():nullptr; const auto* agents=agentsEntry?agentsEntry->getIf<EditorValue::Array>():nullptr; const auto* zones=zonesEntry?zonesEntry->getIf<EditorValue::Array>():nullptr; const auto* paths=pathsEntry?pathsEntry->getIf<EditorValue::Array>():nullptr;
    if(!version||*version!=1||!agents||!zones||!paths) return failure<void>(EditorStatus::Rejected,"editor.crowd.invalid-snapshot","Crowd snapshot requires schema version one and all collections");
    CrowdDocumentTarget candidate(id_);
    for(const auto& entry:*paths){auto value=parsePath(entry);if(!value.value)return failure<void>(value.status,"editor.crowd.invalid-snapshot-path","Snapshot contains an invalid path");if(!candidate.paths_.emplace(value.value->id,*value.value).second)return failure<void>(EditorStatus::Rejected,"editor.crowd.duplicate-path","Snapshot path ids must be unique");}
    for(const auto& entry:*zones){auto value=parseZone(entry);if(!value.value)return failure<void>(value.status,"editor.crowd.invalid-snapshot-zone","Snapshot contains an invalid zone");if(!candidate.zones_.emplace(value.value->id,*value.value).second)return failure<void>(EditorStatus::Rejected,"editor.crowd.duplicate-zone","Snapshot zone ids must be unique");}
    for(const auto& entry:*agents){auto value=parseAgent(entry);if(!value.value)return failure<void>(value.status,"editor.crowd.invalid-snapshot-agent","Snapshot contains an invalid agent");if(!candidate.agents_.emplace(value.value->id,*value.value).second)return failure<void>(EditorStatus::Rejected,"editor.crowd.duplicate-agent","Snapshot agent ids must be unique");}
    for(const auto& diagnostic:candidate.validate()) if(diagnostic.severity==DiagnosticSeverity::Error) return failure<void>(EditorStatus::Rejected,"editor.crowd.invalid-snapshot-reference","Snapshot contains dangling references");
    agents_=std::move(candidate.agents_);zones_=std::move(candidate.zones_);paths_=std::move(candidate.paths_);++revision_;dirty_.include(0,0);return EditorResult<void>::applied();
}

}  // namespace eve::crowd_editing
