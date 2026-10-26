#include <algorithm>
#include <limits>
#include <set>
#include "building/BuildingAssemblyInternal.h"
#include "building/BuildingDef.h"
#include "building/PlacementSystem.h"

namespace eve::building {
using namespace assembly_detail;
PreparedAssembly::PreparedAssembly(std::unique_ptr<PreparedAssemblyData> data) : data_(std::move(data)) {}
PreparedAssembly::PreparedAssembly(PreparedAssembly&&) noexcept            = default;
PreparedAssembly& PreparedAssembly::operator=(PreparedAssembly&&) noexcept = default;
PreparedAssembly::~PreparedAssembly()                                      = default;
std::span<const PlacedBuilding> PreparedAssembly::preview() const {
    return data_ ? std::span<const PlacedBuilding>(data_->preview) : std::span<const PlacedBuilding>{};
}
namespace {
Result<EdgeCurveGroup> transformedCurve(const EdgeCurveGroup& source, AssemblyPose from, AssemblyPose to,
                                        const std::map<int, int>& remap, EdgeCurveGroupId id) {
    if (!source.surfaceSamples.empty())
        return fail<EdgeCurveGroup>("Surface-attached curves must be recaptured on their provider");
    auto out  = source;
    out.id    = id;
    out.level = source.level - from.level + to.level;
    int q     = (to.quarterTurns - from.quarterTurns + 4) % 4;
    for (auto& p : out.controlPoints) {
        float x = p.x - from.x, y = p.y - from.y;
        for (int i = 0; i < q; ++i) {
            float old = x;
            x         = y;
            y         = -old;
        }
        p.x = x + to.x;
        p.y = y + to.y;
    }
    for (int& member : out.instanceIds) {
        auto it = remap.find(member);
        if (it == remap.end()) return fail<EdgeCurveGroup>("Capture requires complete curve membership");
        member = it->second;
    }
    return Result<EdgeCurveGroup>::success(std::move(out));
}
}  // namespace
Result<BuildingBlueprint> AssemblyService::capture(const PlacementWorld& w, std::span<const int> members, AssetGuid id,
                                                   AssemblyPose origin) {
    auto grid = checkGrid(w);
    if (!grid.ok() || id.isNil() || members.empty())
        return fail<BuildingBlueprint>("Capture requires a valid grid, asset identity and selection");
    BuildingBlueprint b;
    b.id          = id;
    b.cellSize    = w.getCellSize();
    b.floorHeight = w.getFloorHeight();
    b.definitions = Value::Object{};
    std::map<int, int> remap;
    int                key = 0;
    for (int member : members)
        if (!w.hasBuilding(member) || !remap.emplace(member, ++key).second)
            return fail<BuildingBlueprint>("Missing or duplicate capture member");
    for (int member : members) {
        const auto& p = w.buildings_.at(member);
        for (int support : p.supportInstanceIds)
            if (!remap.contains(support))
                return fail<BuildingBlueprint>("Selection has structural support outside the house");
        auto transformed = transformPart(p, w, origin, {});
        if (!transformed.ok()) return fail<BuildingBlueprint>("Could not transform captured part");
        auto local             = std::move(transformed).takeValue();
        local.instanceId       = remap.at(member);
        local.edgeCurveGroupId = {};
        local.worldX -= w.getOriginX();
        local.worldY -= w.getOriginY();
        local.garrison.clear();
        local.garrisonRevision = Revision::zero();
        // Arbitrary properties may contain foreign entity ids; only definition defaults transfer.
        local.props.clear();
        b.parts.push_back({{uint32_t(local.instanceId)}, codec::snapshot(local)});
        const auto* def = BuildingRegistry::find(p.buildingId);
        if (!def) return fail<BuildingBlueprint>("Missing captured definition");
        b.definitions.set(p.buildingId, definitionValue(*def));
    }
    for (auto curveId : w.edgeCurveGroupIds()) {
        const auto& curve = w.edgeCurveGroups_.at(curveId.value);
        if (std::none_of(curve.instanceIds.begin(), curve.instanceIds.end(),
                         [&](int member) { return remap.contains(member); }))
            continue;
        auto transformed = transformedCurve(curve, origin, {}, remap, {uint64_t(b.curves.size() + 1)});
        if (!transformed.ok()) return fail<BuildingBlueprint>("Selection contains a partial or surface-attached curve");
        codec::EdgeCurvePayload payload;
        payload.group = std::move(transformed).takeValue();
        for (int member : payload.group.instanceIds) payload.instances.push_back(b.parts[size_t(member - 1)].local);
        b.curves.push_back(std::move(payload));
    }
    return Result<BuildingBlueprint>::success(std::move(b));
}
Result<PreparedAssembly> AssemblyService::prepareState(const PlacementWorld&           w,
                                                       std::unique_ptr<PlacementWorld> candidate, AssemblyId id) {
    auto valid = validateFinal(*candidate);
    if (!valid.ok()) return Result<PreparedAssembly>::failure(valid.status());
    auto before = snapshotWorld(w);
    if (!before.ok()) return fail<PreparedAssembly>("Cannot snapshot source world");
    auto token         = std::make_unique<PreparedAssemblyData>();
    token->epoch       = w.assemblyState_->epoch;
    token->before      = std::move(before).takeValue();
    token->admission   = admissionStamp(w);
    token->definitions = definitionsFor(*candidate);
    token->id          = id;
    if (auto it = candidate->assemblyState_->records.find(id); it != candidate->assemblyState_->records.end())
        for (const auto& [key, member] : it->second.members) token->preview.push_back(candidate->buildings_.at(member));
    token->candidate = std::move(candidate);
    return Result<PreparedAssembly>::success(PreparedAssembly(std::move(token)));
}
Result<PreparedAssembly> AssemblyService::preparePlacement(const PlacementWorld& w, const BuildingBlueprint& b,
                                                           AssemblyId id, AssemblyPose pose) {
    auto encoded = encodeBlueprint(b);
    if (!encoded.ok() || id.isNil() || w.assemblyState_->records.contains(id) || b.cellSize != w.getCellSize() ||
        b.floorHeight != w.getFloorHeight())
        return fail<PreparedAssembly>("Invalid blueprint, incompatible scale or duplicate house identity");
    if (b.parts.size() > size_t(std::numeric_limits<int>::max() - w.instanceCounter_) ||
        w.assemblyState_->nextSlot == std::numeric_limits<uint32_t>::max())
        return fail<PreparedAssembly>("World identity capacity exhausted");
    auto             candidate = w.cloneState();
    BuildingAssembly a;
    a.id               = id;
    a.blueprint        = b.id;
    a.blueprintVersion = b.version;
    a.frame            = pose;
    std::map<int, int> remap;
    for (const auto& part : b.parts) {
        auto p = codec::runtime(part.local);
        p.worldX += w.getOriginX();
        p.worldY += w.getOriginY();
        auto transformed = transformPart(p, w, {}, pose);
        if (!transformed.ok()) return fail<PreparedAssembly>("Invalid transformed blueprint part");
        p                  = std::move(transformed).takeValue();
        p.instanceId       = PlacementSystem::nextInstanceId(*candidate);
        p.edgeCurveGroupId = {};
        remap.emplace(part.local.instanceId, p.instanceId);
        a.members.emplace(part.id.value, p.instanceId);
        a.nextPartKey = std::max(a.nextPartKey, part.id.value + 1);
        candidate->instanceOrder_.push_back(p.instanceId);
        candidate->buildings_.emplace(p.instanceId, std::move(p));
    }
    for (const auto& c : b.curves) {
        auto curve = transformedCurve(c.group, {}, pose, remap, {candidate->nextEdgeCurveGroupId_++});
        if (!curve.ok()) return fail<PreparedAssembly>("Invalid transformed curve");
        candidate->edgeCurveGroups_.emplace(curve.value().id.value, std::move(curve).takeValue());
    }
    candidate->assemblyState_->records.emplace(id, std::move(a));
    candidate->assemblyState_->handles[id] =
        eve::RuntimeHandle<AssemblyHandleTag>(candidate->assemblyState_->nextSlot++, 1);
    return prepareState(w, std::move(candidate), id);
}
Result<PreparedAssembly> AssemblyService::prepareMove(const PlacementWorld& w, AssemblyId id, AssemblyPose pose) {
    auto it = w.assemblyState_->records.find(id);
    if (it == w.assemblyState_->records.end())
        return fail<PreparedAssembly>("House not found", DiagnosticCode::NotFound);
    auto               candidate = w.cloneState();
    std::map<int, int> remap;
    for (const auto& [key, member] : it->second.members) {
        auto p = transformPart(w.buildings_.at(member), w, it->second.frame, pose);
        if (!p.ok()) return fail<PreparedAssembly>("Cannot transform house member");
        candidate->buildings_.at(member) = std::move(p).takeValue();
        remap[member]                    = member;
    }
    for (auto& [curveId, curve] : candidate->edgeCurveGroups_) {
        if (std::none_of(curve.instanceIds.begin(), curve.instanceIds.end(), [&](int m) { return remap.contains(m); }))
            continue;
        auto moved = transformedCurve(curve, it->second.frame, pose, remap, curve.id);
        if (!moved.ok()) return fail<PreparedAssembly>("Cannot move a shared or surface-attached curve");
        curve = std::move(moved).takeValue();
    }
    candidate->assemblyState_->records.at(id).frame = pose;
    return prepareState(w, std::move(candidate), id);
}
Result<PreparedAssembly> AssemblyService::prepareRemoval(const PlacementWorld& w, AssemblyId id) {
    auto a = assembly(w, id);
    if (!a.ok()) return fail<PreparedAssembly>("House not found", DiagnosticCode::NotFound);
    auto candidate = w.cloneState();
    for (const auto& [key, member] : a.value().members)
        PlacementSystem::removeBuildingUnchecked(candidate.get(), member);
    candidate->assemblyState_->records.erase(id);
    candidate->assemblyState_->handles.erase(id);
    return prepareState(w, std::move(candidate), id);
}
Result<PreparedAssembly> AssemblyService::prepareRemodel(const PlacementWorld& w, AssemblyId id,
                                                         std::span<const PlacedBuilding> members) {
    auto old = assembly(w, id);
    if (!old.ok()) return Result<PreparedAssembly>::failure(old.status());
    if (members.empty()) return prepareRemoval(w, id);
    auto                    candidate = w.cloneState();
    auto                    a         = old.value();
    std::map<int, uint32_t> keys;
    uint32_t                nextKey = a.nextPartKey;
    for (const auto& [key, member] : a.members) {
        keys[member] = key;
        candidate->buildings_.erase(member);
    }
    std::erase_if(candidate->instanceOrder_, [&](int member) { return keys.contains(member); });
    a.members.clear();
    std::set<int> seen;
    for (auto p : members) {
        uint32_t key = 0;
        if (p.instanceId) {
            auto it = keys.find(p.instanceId);
            if (it == keys.end() || !seen.insert(p.instanceId).second)
                return fail<PreparedAssembly>("Remodel cannot steal or duplicate a member");
            key = it->second;
        } else {
            if (candidate->instanceCounter_ == std::numeric_limits<int>::max() ||
                nextKey >= uint32_t(std::numeric_limits<int>::max()))
                return fail<PreparedAssembly>("Member identity capacity exhausted");
            p.instanceId = PlacementSystem::nextInstanceId(*candidate);
            key          = nextKey++;
        }
        a.members.emplace(key, p.instanceId);
        candidate->instanceOrder_.push_back(p.instanceId);
        candidate->buildings_.emplace(p.instanceId, std::move(p));
    }
    // Curves remain authoritative only if all exact addresses survive; partial edits dissolve them.
    std::erase_if(candidate->edgeCurveGroups_, [&](const auto& entry) {
        for (int member : entry.second.instanceIds) {
            auto current  = candidate->buildings_.find(member);
            auto previous = w.buildings_.find(member);
            if (current == candidate->buildings_.end() || previous == w.buildings_.end() ||
                current->second.edge != previous->second.edge ||
                current->second.buildingId != previous->second.buildingId ||
                current->second.level != previous->second.level)
                return true;
        }
        return false;
    });
    a.nextPartKey                          = nextKey;
    candidate->assemblyState_->records[id] = std::move(a);
    return prepareState(w, std::move(candidate), id);
}
Result<AssemblyReceipt> AssemblyService::commit(PlacementWorld& w, PreparedAssembly token) {
    if (!token.data_ || token.data_->epoch != w.assemblyState_->epoch)
        return fail<AssemblyReceipt>("Expired prepared house operation", DiagnosticCode::Conflict);
    auto&      t             = *token.data_;
    auto       current       = snapshotWorld(w);
    auto       defs          = checkDefinitions(t.definitions);
    const bool currentOk     = current.ok();
    const bool definitionsOk = defs.ok();
    if (!currentOk || current.value() != t.before || !definitionsOk || admissionStamp(w) != t.admission)
        return fail<AssemblyReceipt>("World or definitions changed since preparation", DiagnosticCode::Conflict);
    auto valid = validateFinal(*t.candidate);
    if (!valid.ok()) return Result<AssemblyReceipt>::failure(valid.status());
    auto after = snapshotWorld(*t.candidate);
    if (!after.ok()) return fail<AssemblyReceipt>("Cannot capture commit receipt");
    AssemblyReceipt receipt{t.id, std::move(t.before), std::move(after).takeValue()};
    // Invalidate other prepared tokens after every commit; existing assembly handles are
    // intentionally invalidated too, and callers resolve persistent identities again.
    t.candidate->assemblyState_->epoch = nextEpoch();
    w.swapState(*t.candidate);
    if (w.publishEvents_) {
        BuildingChangeEvent e;
        e.action  = "reset";
        e.worldId = w.getId();
        PlacementSystem::pushEvent(std::move(e));
    }
    return Result<AssemblyReceipt>::success(std::move(receipt));
}
Result<BuildingAssembly> AssemblyService::assembly(const PlacementWorld& w, AssemblyId id) {
    auto it = w.assemblyState_->records.find(id);
    if (it == w.assemblyState_->records.end())
        return fail<BuildingAssembly>("House not found", DiagnosticCode::NotFound);
    return Result<BuildingAssembly>::success(it->second);
}
Result<AssemblyHandle> AssemblyService::handle(const PlacementWorld& w, AssemblyId id) {
    auto it = w.assemblyState_->handles.find(id);
    if (it == w.assemblyState_->handles.end()) return fail<AssemblyHandle>("House not found", DiagnosticCode::NotFound);
    return Result<AssemblyHandle>::success({it->second, w.assemblyState_->epoch});
}
Result<BuildingAssembly> AssemblyService::resolve(const PlacementWorld& w, AssemblyHandle h) {
    if (h.ownerEpoch != w.assemblyState_->epoch)
        return fail<BuildingAssembly>("Stale house handle", DiagnosticCode::Conflict);
    for (const auto& [id, slot] : w.assemblyState_->handles)
        if (slot == h.slot) return assembly(w, id);
    return fail<BuildingAssembly>("Stale house slot", DiagnosticCode::NotFound);
}
std::optional<AssemblyId> AssemblyService::owner(const PlacementWorld& w, int member) {
    for (const auto& [id, a] : w.assemblyState_->records)
        for (const auto& [key, m] : a.members)
            if (m == member) return id;
    return std::nullopt;
}
std::vector<AssemblyId> AssemblyService::assemblies(const PlacementWorld& w) {
    std::vector<AssemblyId> result;
    for (const auto& [id, a] : w.assemblyState_->records) result.push_back(id);
    return result;
}
}  // namespace eve::building
