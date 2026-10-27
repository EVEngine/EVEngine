// eve-hot-path
#include "sensing/TargetingPipeline.h"

#include "common/Capability.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace eve::sensing {
namespace {

bool finite(float v) noexcept { return std::isfinite(v); }

bool insideCone(float px, float py, float apexX, float apexY, float dirX, float dirY, float halfAngle,
                float range) noexcept {
    const float dx     = px - apexX;
    const float dy     = py - apexY;
    const float distSq = dx * dx + dy * dy;
    if (distSq > range * range) return false;
    if (distSq == 0.f) return true;
    const float facingLen = std::hypot(dirX, dirY);
    if (!(facingLen > 0.f)) return false;
    const float invDist = 1.f / std::sqrt(distSq);
    const float nx      = dx * invDist;
    const float ny      = dy * invDist;
    const float fx      = dirX / facingLen;
    const float fy      = dirY / facingLen;
    const float dot     = std::clamp(nx * fx + ny * fy, -1.f, 1.f);
    return std::acos(dot) <= halfAngle;
}

[[nodiscard]] bool matchesFactFilters(const Subject& subject, float distance, const QuerySpec& spec) {
    for (const auto& tag : spec.requiredTags)
        if (!subject.tags.count(tag)) return false;
    for (const auto& tag : spec.excludedTags)
        if (subject.tags.count(tag)) return false;
    if (!spec.includeFactions.empty()) {
        bool included = false;
        for (const auto& faction : spec.includeFactions)
            if (subject.faction == faction) {
                included = true;
                break;
            }
        if (!included) return false;
    }
    for (const auto& faction : spec.excludeFactions)
        if (subject.faction == faction) return false;
    if (spec.visibleTo && !subject.visibleTo.count(*spec.visibleTo)) return false;
    if (distance < spec.minRange || distance > spec.maxRange) return false;
    return true;
}

class SelectWorldTask final : public ITargetingTask {
public:
    [[nodiscard]] std::string_view id() const noexcept override { return "sensing.select.world"; }
    [[nodiscard]] TargetingTaskKind kind() const noexcept override { return TargetingTaskKind::Select; }

    [[nodiscard]] Result<void> execute(TargetingSourceContext& context, std::vector<RankedCandidate>& inout,
                                       const TargetingTaskStep& step) const override {
        if (context.world == nullptr) {
            return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                           "sensing.select.world requires a SensingWorld",
                                                           "pipeline.world", {}, "sensing"));
        }
        auto queried = context.world->collectCandidates(context.origin, step.querySpec);
        if (!queried) return Result<void>::failure(queried.status());
        auto collection     = std::move(queried).takeValue();
        context.usedSpatial = collection.usedSpatial;
        context.scanned     = collection.scanned;
        inout               = std::move(collection.candidates);
        return Result<void>::success(Status::success(StatusCode::Applied));
    }
};

class FilterSpecTask final : public ITargetingTask {
public:
    [[nodiscard]] std::string_view id() const noexcept override { return "sensing.filter.spec"; }
    [[nodiscard]] TargetingTaskKind kind() const noexcept override { return TargetingTaskKind::Filter; }

    [[nodiscard]] Result<void> execute(TargetingSourceContext& context, std::vector<RankedCandidate>& inout,
                                       const TargetingTaskStep& step) const override {
        if (context.world == nullptr) {
            return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                           "sensing.filter.spec requires a SensingWorld",
                                                           "pipeline.world", {}, "sensing"));
        }
        auto valid = step.querySpec.validate();
        if (!valid) return Result<void>::failure(valid.status());

        std::vector<RankedCandidate> kept;
        kept.reserve(inout.size());
        for (const auto& candidate : inout) {
            const auto found = context.world->subjects().find(candidate.id);
            if (found == context.world->subjects().end()) continue;
            if (matchesFactFilters(found->second, candidate.distance, step.querySpec)) kept.push_back(candidate);
        }
        inout = std::move(kept);
        return Result<void>::success(Status::success(StatusCode::Applied));
    }
};

class FilterConeTask final : public ITargetingTask {
public:
    [[nodiscard]] std::string_view id() const noexcept override { return "sensing.filter.cone"; }
    [[nodiscard]] TargetingTaskKind kind() const noexcept override { return TargetingTaskKind::Filter; }

    [[nodiscard]] Result<void> execute(TargetingSourceContext& context, std::vector<RankedCandidate>& inout,
                                       const TargetingTaskStep& step) const override {
        if (!finite(step.coneHalfAngle) || step.coneHalfAngle < 0.f || step.coneHalfAngle > 3.14159265f ||
            !finite(step.coneRange) || step.coneRange < 0.f || !finite(context.dirX) || !finite(context.dirY) ||
            !(std::hypot(context.dirX, context.dirY) > 0.f)) {
            return Result<void>::failure(
                Diagnostic::error(DiagnosticCode::InvalidArgument,
                                  "sensing.filter.cone requires finite facing, halfAngle in [0,pi], range >= 0",
                                  "pipeline.cone", {}, "sensing"));
        }
        std::vector<RankedCandidate> kept;
        kept.reserve(inout.size());
        for (const auto& candidate : inout) {
            if (insideCone(candidate.x, candidate.y, context.origin.x, context.origin.y, context.dirX, context.dirY,
                           step.coneHalfAngle, step.coneRange)) {
                kept.push_back(candidate);
            }
        }
        inout = std::move(kept);
        return Result<void>::success(Status::success(StatusCode::Applied));
    }
};

class FilterLosTask final : public ITargetingTask {
public:
    [[nodiscard]] std::string_view id() const noexcept override { return "sensing.filter.los"; }
    [[nodiscard]] TargetingTaskKind kind() const noexcept override { return TargetingTaskKind::Filter; }

    [[nodiscard]] Result<void> execute(TargetingSourceContext& context, std::vector<RankedCandidate>& inout,
                                       const TargetingTaskStep& /*step*/) const override {
        auto* los = cap::query<ILineOfSightQuery>();
        if (los == nullptr) {
            return Result<void>::failure(Diagnostic::error(DiagnosticCode::Unsupported,
                                                           "sensing.filter.los requires ILineOfSightQuery capability",
                                                           "pipeline.los", {}, "sensing"));
        }
        auto from = context.origin.space == CoordinateSpace::World3D
                        ? WorldPoint::world3D(context.origin.x, context.origin.y, context.origin.z)
                        : WorldPoint::world2D(context.origin.x, context.origin.y);
        if (!from) return Result<void>::failure(from.status());
        std::vector<RankedCandidate> kept;
        kept.reserve(inout.size());
        for (const auto& candidate : inout) {
            auto to = candidate.space == CoordinateSpace::World3D
                          ? WorldPoint::world3D(candidate.x, candidate.y, candidate.z)
                          : WorldPoint::world2D(candidate.x, candidate.y);
            if (!to) return Result<void>::failure(to.status());
            auto visible = los->query(TargetLocation{from.value()}, TargetLocation{to.value()});
            if (!visible) return Result<void>::failure(visible.status());
            if (visible.value().visible) kept.push_back(candidate);
        }
        inout = std::move(kept);
        return Result<void>::success(Status::success(StatusCode::Applied));
    }
};

class SortDistanceTask final : public ITargetingTask {
public:
    [[nodiscard]] std::string_view id() const noexcept override { return "sensing.sort.distance"; }
    [[nodiscard]] TargetingTaskKind kind() const noexcept override { return TargetingTaskKind::Sort; }

    [[nodiscard]] Result<void> execute(TargetingSourceContext& /*context*/, std::vector<RankedCandidate>& inout,
                                       const TargetingTaskStep& /*step*/) const override {
        std::sort(inout.begin(), inout.end(), [](const RankedCandidate& a, const RankedCandidate& b) {
            if (a.distance != b.distance) return a.distance < b.distance;
            return a.id < b.id;
        });
        for (auto& candidate : inout) {
            candidate.score       = -candidate.distance;
            candidate.scoreReason = "distance";
        }
        return Result<void>::success(Status::success(StatusCode::Applied));
    }
};

class SortTruncateTask final : public ITargetingTask {
public:
    [[nodiscard]] std::string_view id() const noexcept override { return "sensing.sort.truncate"; }
    [[nodiscard]] TargetingTaskKind kind() const noexcept override { return TargetingTaskKind::Sort; }

    [[nodiscard]] Result<void> execute(TargetingSourceContext& /*context*/, std::vector<RankedCandidate>& inout,
                                       const TargetingTaskStep& step) const override {
        if (inout.size() > static_cast<std::size_t>(step.truncateCount))
            inout.resize(static_cast<std::size_t>(step.truncateCount));
        return Result<void>::success(Status::success(StatusCode::Applied));
    }
};

}  // namespace

TargetingPipeline TargetingPipeline::withBuiltins() {
    TargetingPipeline pipeline;
    pipeline.registerTask(std::make_unique<SelectWorldTask>()).ignore("builtin sensing.select.world");
    pipeline.registerTask(std::make_unique<FilterSpecTask>()).ignore("builtin sensing.filter.spec");
    pipeline.registerTask(std::make_unique<FilterConeTask>()).ignore("builtin sensing.filter.cone");
    pipeline.registerTask(std::make_unique<FilterLosTask>()).ignore("builtin sensing.filter.los");
    pipeline.registerTask(std::make_unique<SortDistanceTask>()).ignore("builtin sensing.sort.distance");
    pipeline.registerTask(std::make_unique<SortTruncateTask>()).ignore("builtin sensing.sort.truncate");

    TargetingPreset coneSelect;
    coneSelect.id = "sensing.builtin.coneSelect";
    {
        TargetingTaskStep select;
        select.taskId                = "sensing.select.world";
        select.querySpec.maxRange    = 32.f;
        select.querySpec.countPolicy = CountPolicy::TruncateToMax;
        select.querySpec.sortKey     = SortKey::None;
        coneSelect.steps.push_back(std::move(select));
    }
    {
        TargetingTaskStep cone;
        cone.taskId        = "sensing.filter.cone";
        cone.coneHalfAngle = 0.785398163f;  // 45 degrees
        cone.coneRange     = 32.f;
        coneSelect.steps.push_back(std::move(cone));
    }
    {
        TargetingTaskStep sort;
        sort.taskId = "sensing.sort.distance";
        coneSelect.steps.push_back(std::move(sort));
    }
    {
        TargetingTaskStep truncate;
        truncate.taskId        = "sensing.sort.truncate";
        truncate.truncateCount = 8;
        coneSelect.steps.push_back(std::move(truncate));
    }
    pipeline.registerPreset(std::move(coneSelect)).ignore("builtin sensing.builtin.coneSelect");
    return pipeline;
}

TargetingPipeline& TargetingPipeline::sharedBuiltins() {
    static TargetingPipeline pipeline = withBuiltins();
    return pipeline;
}

Result<void> TargetingPipeline::registerTask(std::unique_ptr<ITargetingTask> task) {
    if (!task) {
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "targeting task must not be null", "task", {}, "sensing"));
    }
    const std::string id{task->id()};
    if (id.empty()) {
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "targeting task id must not be empty", "task.id", {}, "sensing"));
    }
    if (tasks_.count(id) != 0) {
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::AlreadyExists, "targeting task id already registered", "task.id", {}, "sensing"));
    }
    tasks_.emplace(id, std::move(task));
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<TargetingPlan> TargetingPipeline::compile(const TargetingPreset& preset) const {
    if (preset.id.empty() || preset.steps.empty()) {
        return Result<TargetingPlan>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "preset requires an id and steps", "preset", {}, "sensing"));
    }
    TargetingPlan plan;
    bool          selected  = false;
    bool          truncated = false;
    for (const auto& step : preset.steps) {
        const auto found = tasks_.find(step.taskId);
        if (found == tasks_.end()) {
            return Result<TargetingPlan>::failure(
                Diagnostic::error(DiagnosticCode::NotFound, "unknown targeting task", step.taskId, {}, "sensing"));
        }
        if (truncated || (!selected && found->second->kind() != TargetingTaskKind::Select) ||
            (selected && found->second->kind() == TargetingTaskKind::Select)) {
            return Result<TargetingPlan>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "preset requires one leading selection and terminal truncation",
                step.taskId, {}, "sensing"));
        }
        if (step.taskId == "sensing.sort.truncate" && !plan.hasSort_) {
            TargetingTaskStep order;
            order.taskId = "sensing.sort.distance";
            plan.steps_.push_back({std::make_shared<SortDistanceTask>(), order});
        }
        plan.hasSort_ = plan.hasSort_ || found->second->kind() == TargetingTaskKind::Sort;
        selected      = true;
        truncated     = step.taskId == "sensing.sort.truncate";
        if (step.taskId == "sensing.select.world" || step.taskId == "sensing.filter.spec") {
            auto valid = step.querySpec.validate();
            if (!valid) return Result<TargetingPlan>::failure(valid.status());
        }
        if (step.taskId == "sensing.select.world") {
            plan.minCount_ = step.querySpec.minCount;
            plan.maxCount_ = step.querySpec.maxCount;
        }
        plan.steps_.push_back({found->second, step});
    }
    return Result<TargetingPlan>::success(std::move(plan));
}

Result<void> TargetingPipeline::registerPreset(TargetingPreset preset) {
    auto compiled = compile(preset);
    if (!compiled) return Result<void>::failure(compiled.status());
    presets_.insert_or_assign(preset.id, std::move(compiled).takeValue());
    return Result<void>::success(Status::success(StatusCode::Applied));
}

bool TargetingPipeline::hasPreset(std::string_view presetId) const noexcept {
    return presets_.find(std::string(presetId)) != presets_.end();
}

Result<CandidateQueryResult> TargetingPipeline::execute(TargetingSourceContext& context,
                                                        const TargetingPreset&  preset) const {
    auto compiled = compile(preset);
    if (!compiled) return Result<CandidateQueryResult>::failure(compiled.status());
    return compiled.value().execute(context);
}

Result<CandidateQueryResult> TargetingPlan::execute(TargetingSourceContext& context) const {
    if (context.world == nullptr || !finite(context.origin.x) || !finite(context.origin.y)) {
        return Result<CandidateQueryResult>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "plan requires a world and finite origin", "pipeline", {}, "sensing"));
    }
    context.usedSpatial = false;
    context.scanned     = 0;
    std::vector<RankedCandidate> working;
    for (const auto& step : steps_) {
        auto ran = step.task->execute(context, working, step.parameters);
        if (!ran) return Result<CandidateQueryResult>::failure(ran.status());
    }
    if (!hasSort_) {
        std::sort(working.begin(), working.end(), [](const auto& a, const auto& b) {
            if (a.distance != b.distance) return a.distance < b.distance;
            return a.id < b.id;
        });
    }
    if (working.size() > maxCount_) working.resize(maxCount_);
    if (working.size() < minCount_) {
        return Result<CandidateQueryResult>::failure(Diagnostic::error(
            DiagnosticCode::PreconditionViolation, "final candidates below minCount", "pipeline.count", {}, "sensing"));
    }
    return Result<CandidateQueryResult>::success(CandidateQueryResult{std::move(working)});
}

Result<CandidateQueryResult> TargetingPipeline::executePreset(TargetingSourceContext& context,
                                                              std::string_view         presetId) const {
    const auto found = presets_.find(std::string(presetId));
    if (found == presets_.end()) {
        return Result<CandidateQueryResult>::failure(Diagnostic::error(
            DiagnosticCode::NotFound, "targeting preset is not registered", "preset.id", {}, "sensing"));
    }
    return found->second.execute(context);
}

}  // namespace eve::sensing
