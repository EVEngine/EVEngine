#include "sensing/TargetingPipeline.h"

#include "common/Capability.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace eve::sensing {
namespace {

template <class T>
Result<T> pipelineFailure(DiagnosticCode code, std::string message, std::string path = {}) {
    return Result<T>::failure(Diagnostic::error(code, std::move(message), std::move(path), {}, "sensing"));
}

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
            return pipelineFailure<void>(DiagnosticCode::InvalidArgument, "sensing.select.world requires a SensingWorld",
                                         "pipeline.world");
        }
        QuerySpec spec       = step.querySpec;
        spec.countPolicy     = CountPolicy::TruncateToMax;
        if (spec.sortKey == SortKey::None) spec.sortKey = SortKey::DistanceAscending;
        auto queried = context.world->query(context.origin, spec);
        if (!queried) return Result<void>::failure(queried.status());
        const auto ranked = queried.value().ranked();
        inout.assign(ranked.begin(), ranked.end());
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
            return pipelineFailure<void>(DiagnosticCode::InvalidArgument, "sensing.filter.spec requires a SensingWorld",
                                         "pipeline.world");
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
            return pipelineFailure<void>(DiagnosticCode::InvalidArgument,
                                         "sensing.filter.cone requires finite facing, halfAngle in [0,pi], range >= 0",
                                         "pipeline.cone");
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
            return pipelineFailure<void>(DiagnosticCode::Unsupported,
                                         "sensing.filter.los requires ILineOfSightQuery capability", "pipeline.los");
        }
        auto from = WorldPoint::world2D(context.origin.x, context.origin.y);
        if (!from) return Result<void>::failure(from.status());
        std::vector<RankedCandidate> kept;
        kept.reserve(inout.size());
        for (const auto& candidate : inout) {
            auto to = WorldPoint::world2D(candidate.x, candidate.y);
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
        return pipelineFailure<void>(DiagnosticCode::InvalidArgument, "targeting task must not be null", "task");
    }
    const std::string id{task->id()};
    if (id.empty()) {
        return pipelineFailure<void>(DiagnosticCode::InvalidArgument, "targeting task id must not be empty", "task.id");
    }
    if (tasks_.count(id) != 0) {
        return pipelineFailure<void>(DiagnosticCode::AlreadyExists, "targeting task id already registered", "task.id");
    }
    tasks_.emplace(id, std::move(task));
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> TargetingPipeline::registerPreset(TargetingPreset preset) {
    if (preset.id.empty()) {
        return pipelineFailure<void>(DiagnosticCode::InvalidArgument, "targeting preset id must not be empty",
                                     "preset.id");
    }
    if (preset.steps.empty()) {
        return pipelineFailure<void>(DiagnosticCode::InvalidArgument, "targeting preset must contain at least one step",
                                     "preset.steps");
    }
    for (const auto& step : preset.steps) {
        if (step.taskId.empty() || tasks_.count(step.taskId) == 0) {
            return pipelineFailure<void>(DiagnosticCode::NotFound, "targeting preset references unknown task id",
                                         "preset.steps.taskId");
        }
    }
    presets_.insert_or_assign(preset.id, std::move(preset));
    return Result<void>::success(Status::success(StatusCode::Applied));
}

bool TargetingPipeline::hasPreset(std::string_view presetId) const noexcept {
    return presets_.find(std::string(presetId)) != presets_.end();
}

Result<CandidateQueryResult> TargetingPipeline::execute(TargetingSourceContext& context,
                                                        const TargetingPreset&  preset) const {
    if (context.world == nullptr) {
        return pipelineFailure<CandidateQueryResult>(DiagnosticCode::InvalidArgument,
                                                     "TargetingPipeline requires a SensingWorld", "pipeline.world");
    }
    if (!finite(context.origin.x) || !finite(context.origin.y)) {
        return pipelineFailure<CandidateQueryResult>(DiagnosticCode::InvalidArgument,
                                                     "TargetingSourceContext origin must be finite", "pipeline.origin");
    }
    if (preset.steps.empty()) {
        return pipelineFailure<CandidateQueryResult>(DiagnosticCode::InvalidArgument,
                                                     "TargetingPreset must contain at least one step", "preset.steps");
    }

    std::vector<RankedCandidate> working;
    for (const auto& step : preset.steps) {
        const auto found = tasks_.find(step.taskId);
        if (found == tasks_.end() || !found->second) {
            return pipelineFailure<CandidateQueryResult>(DiagnosticCode::NotFound,
                                                         "TargetingPreset step task is not registered",
                                                         "preset.steps.taskId");
        }
        auto ran = found->second->execute(context, working, step);
        if (!ran) return Result<CandidateQueryResult>::failure(ran.status());
    }
    return Result<CandidateQueryResult>::success(CandidateQueryResult{std::move(working)});
}

Result<CandidateQueryResult> TargetingPipeline::executePreset(TargetingSourceContext& context,
                                                              std::string_view         presetId) const {
    const auto found = presets_.find(std::string(presetId));
    if (found == presets_.end()) {
        return pipelineFailure<CandidateQueryResult>(DiagnosticCode::NotFound, "targeting preset is not registered",
                                                     "preset.id");
    }
    return execute(context, found->second);
}

}  // namespace eve::sensing
