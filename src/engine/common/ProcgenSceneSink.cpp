#include "common/ProcgenSceneSink.h"
#include <unordered_map>
#include <unordered_set>

namespace eve {
Result<std::vector<ProcgenInstanceDesc>> buildProcgenInstanceDeltaSnapshot(
    const std::vector<ProcgenInstanceDesc>& current, uint64_t currentRevision, const ProcgenInstanceDelta& delta) {
    using Output = Result<std::vector<ProcgenInstanceDesc>>;
    if (delta.baseRevision == 0 || delta.targetRevision == 0 || currentRevision != delta.baseRevision ||
        delta.targetRevision != delta.baseRevision + 1)
        return Output::failure(eve::Diagnostic::error(eve::DiagnosticCode::Conflict,
                                                      "procedural scene delta revision is stale", "baseRevision"));

    std::unordered_map<std::string, eve::ProcgenInstanceDesc> staged;
    std::unordered_map<uint64_t, std::string>                 stagedPointIds;
    for (const auto& instance : current) {
        if (instance.id.empty() || !staged.emplace(instance.id, instance).second ||
            (instance.sourcePointId && !stagedPointIds.emplace(instance.sourcePointId, instance.id).second))
            return Output::failure(Diagnostic::error(
                DiagnosticCode::Conflict, "committed procedural snapshot contains ambiguous identities", "current"));
    }
    std::unordered_set<std::string> removedIds;
    removedIds.reserve(delta.removedPointIds.size() + delta.removed.size());
    for (const auto pointId : delta.removedPointIds) {
        const auto found = stagedPointIds.find(pointId);
        if (pointId == 0 || found == stagedPointIds.end() || !removedIds.insert(found->second).second)
            return Output::failure(eve::Diagnostic::error(eve::DiagnosticCode::Conflict,
                                                          "procedural scene delta removes an unknown source PointId",
                                                          "removedPointIds"));
    }
    for (const auto& id : delta.removed)
        if (id.empty() || !removedIds.insert(id).second)
            return Output::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::Conflict, "procedural scene delta repeats a removed identity", "removed"));
    for (const auto& id : removedIds) {
        const auto found = staged.find(id);
        if (found == staged.end())
            return Output::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::Conflict, "procedural scene delta removes an unknown identity", "removed"));
        if (found->second.sourcePointId != 0) stagedPointIds.erase(found->second.sourcePointId);
        staged.erase(found);
    }
    for (const auto& instance : delta.updated) {
        if (instance.id.empty() || instance.sourcePointId == 0)
            return Output::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                       "procedural scene delta updates require an id and source PointId", "updated"));
        const auto identity = stagedPointIds.find(instance.sourcePointId);
        if (identity == stagedPointIds.end())
            return Output::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::Conflict, "procedural scene delta updates an unknown identity", "updated"));
        if (identity->second != instance.id && staged.find(instance.id) != staged.end())
            return Output::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::Conflict,
                                       "procedural scene delta update renames to a duplicate identity", "updated"));
        staged.erase(identity->second);
        staged.emplace(instance.id, instance);
        identity->second = instance.id;
    }
    for (const auto& instance : delta.added) {
        if (instance.id.empty() || instance.sourcePointId == 0 ||
            stagedPointIds.find(instance.sourcePointId) != stagedPointIds.end() ||
            !staged.emplace(instance.id, instance).second)
            return Output::failure(eve::Diagnostic::error(eve::DiagnosticCode::Conflict,
                                                          "procedural scene delta adds a duplicate identity", "added"));
        stagedPointIds.emplace(instance.sourcePointId, instance.id);
    }
    if (!delta.targetPointOrder.empty() && !delta.targetOrder.empty())
        return Output::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument,
            "procedural scene delta cannot mix PointId and compatibility target orders", "targetPointOrder"));
    std::vector<std::string> resolvedOrder;
    if (!delta.targetPointOrder.empty()) {
        resolvedOrder.reserve(delta.targetPointOrder.size());
        std::unordered_set<uint64_t> orderedPointIds;
        for (const auto pointId : delta.targetPointOrder) {
            const auto found = stagedPointIds.find(pointId);
            if (pointId == 0 || found == stagedPointIds.end() || !orderedPointIds.insert(pointId).second)
                return Output::failure(eve::Diagnostic::error(
                    eve::DiagnosticCode::Conflict,
                    "procedural scene delta target order has an unknown or duplicate source PointId",
                    "targetPointOrder"));
            resolvedOrder.push_back(found->second);
        }
    } else {
        resolvedOrder = delta.targetOrder;
    }
    if (staged.size() != resolvedOrder.size())
        return Output::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Conflict, "procedural scene delta target order is incomplete", "targetOrder"));
    std::vector<eve::ProcgenInstanceDesc> target;
    target.reserve(resolvedOrder.size());
    for (const auto& id : resolvedOrder) {
        auto found = staged.find(id);
        if (found == staged.end())
            return Output::failure(eve::Diagnostic::error(eve::DiagnosticCode::Conflict,
                                                          "procedural scene delta target order has unknown identity",
                                                          "targetOrder"));
        target.push_back(found->second);
        staged.erase(found);
    }
    if (!staged.empty())
        return Output::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Conflict, "procedural scene delta target order repeats an identity", "targetOrder"));
    return Output::success(std::move(target));
}
}  // namespace eve
