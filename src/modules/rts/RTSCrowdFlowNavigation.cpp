#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include "rts/RTSCrowdFlowNavigationInternal.h"
namespace eve::rts {
namespace {
template <class T>
Result<T> failure(DiagnosticCode code, const char* text) {
    return Result<T>::failure(Diagnostic::error(code, text, "crowdFlowNavigation"));
}
}  // namespace
Result<void> CrowdFlowNavigation::configure(CrowdFlowNavigationSettings settings) noexcept {
    if (enabled_ && settings.maxWorkPerTick == settings_.maxWorkPerTick &&
        settings.maxStartsPerTick == settings_.maxStartsPerTick)
        return Result<void>::success(Status::success(StatusCode::NoOp));
    settings_ = settings;
    enabled_  = true;
    return Result<void>::success(Status::success(StatusCode::Applied));
}
void CrowdFlowNavigation::disable() noexcept {
    enabled_ = false;
    jobs_.clear();
    groups_.clear();
    notices_.clear();
    workAfter_ = roundThrough_ = 0;
}
Result<CrowdFlowNavigation::InputChange> CrowdFlowNavigation::begin(crowd::Crowd& crowd, const SimulationStep& step) {
    if (!enabled_) return failure<InputChange>(DiagnosticCode::PreconditionViolation, "Shared-flow policy is disabled");
    notices_.clear();
    const auto source = crowd.getFieldStatus();
    if (source.surface.surface.isZero())
        return failure<InputChange>(DiagnosticCode::PreconditionViolation,
                                    "Shared-flow mode requires an imported movement projection");
    const bool ownerChanged = owner_ != source.owner;
    const bool changed      = ownerChanged || input_ != source.inputRevision;
    if (changed) {
        jobs_.clear();
        workAfter_    = 0;
        roundThrough_ = 0;
        frame_.reset();
        if (ownerChanged) pumped_.reset();
        owner_ = source.owner;
        input_ = source.inputRevision;
    }
    if (!frame_ || *frame_ != step.tick) {
        if (frame_)
            jobs_.erase(std::remove_if(jobs_.begin(), jobs_.end(), [&](const Job& job) { return job.used != *frame_; }),
                        jobs_.end());
        frame_ = step.tick;
    }
    return Result<InputChange>::success(changed ? InputChange::Changed : InputChange::Unchanged);
}
Result<crowd::FlowGoal> CrowdFlowNavigation::goalCell(crowd::Crowd& crowd, WorldPosition target) const {
    if (!std::isfinite(target.x) || !std::isfinite(target.y))
        return failure<crowd::FlowGoal>(DiagnosticCode::InvalidArgument, "Nonfinite shared goal");
    const double x = std::floor((double(target.x) - crowd.getFieldOriginX()) / crowd.getCellSize());
    const double y = std::floor((double(target.y) - crowd.getFieldOriginY()) / crowd.getCellSize());
    if (x < 0 || y < 0 || x >= crowd.getFieldWidth() || y >= crowd.getFieldHeight())
        return failure<crowd::FlowGoal>(DiagnosticCode::InvalidArgument, "Shared goal outside navigation surface");
    return Result<crowd::FlowGoal>::success({static_cast<int>(x), static_cast<int>(y)});
}
Result<std::vector<crowd::FlowGoal>> CrowdFlowNavigation::goalSet(crowd::Crowd&                  crowd,
                                                                  std::span<const WorldPosition> targets) const {
    if (targets.empty() || targets.size() > 1024)
        return failure<std::vector<crowd::FlowGoal>>(DiagnosticCode::InvalidArgument,
                                                     "Expected 1..1024 shared targets");
    std::vector<crowd::FlowGoal> goals;
    goals.reserve(targets.size());
    for (const auto target : targets) {
        auto cell = goalCell(crowd, target);
        if (!cell) return Result<std::vector<crowd::FlowGoal>>::failure(cell.status());
        goals.push_back(cell.value());
    }
    std::sort(goals.begin(), goals.end());
    goals.erase(std::unique(goals.begin(), goals.end()), goals.end());
    return Result<std::vector<crowd::FlowGoal>>::success(std::move(goals));
}
Result<void> CrowdFlowNavigation::request(crowd::Crowd& crowd, WorldPosition target, SimulationTick tick) {
    return request(crowd, std::span<const WorldPosition>(&target, 1), tick);
}
Result<void> CrowdFlowNavigation::request(crowd::Crowd& crowd, std::span<const WorldPosition> targets,
                                          SimulationTick tick) try {
    auto goal = goalSet(crowd, targets);
    if (!goal) return Result<void>::failure(goal.status());
    return requestGoals(goal.value(), tick);
} catch (const std::exception& error) {
    return failure<void>(DiagnosticCode::Failed, error.what());
}
Result<void> CrowdFlowNavigation::requestGoals(std::span<const crowd::FlowGoal> goals, SimulationTick tick) try {
    auto found = std::lower_bound(jobs_.begin(), jobs_.end(), goals, [](const Job& job, const auto& key) {
        return std::lexicographical_compare(job.goals.begin(), job.goals.end(), key.begin(), key.end());
    });
    if (found != jobs_.end() && std::equal(found->goals.begin(), found->goals.end(), goals.begin(), goals.end())) {
        found->used = tick;
        return Result<void>::success(Status::success(StatusCode::NoOp));
    }
    if (jobs_.size() == 10000)
        return failure<void>(DiagnosticCode::PreconditionViolation,
                             "Shared-goal queue exceeds 10000 distinct goal sets");
    if (nextSequence_ == std::numeric_limits<std::uint64_t>::max())
        return failure<void>(DiagnosticCode::PreconditionViolation, "Shared-goal admission sequence exhausted");
    Job job;
    job.sequence = nextSequence_;
    job.goals.assign(goals.begin(), goals.end());
    job.used = tick;
    jobs_.insert(found, std::move(job));
    ++nextSequence_;
    return Result<void>::success();
} catch (const std::exception& error) {
    return failure<void>(DiagnosticCode::Failed, error.what());
}
Result<void> CrowdFlowNavigation::pump(crowd::Crowd& crowd, const SimulationStep& step) try {
    if (step.delta.isZero() || (pumped_ && *pumped_ == step.tick))
        return Result<void>::success(Status::success(StatusCode::NoOp));
    // One attempt owns this tick's quota, even if a later stage fails. Input replacement
    // may retire the partial plans, but does not refund work in the same owner timeline.
    pumped_         = step.tick;
    stats_.lastWork = stats_.lastStarts = 0;
    stats_.cachePressure                = false;
    // Canonical key order serves lookup only. Admission order serves quotas, so new low keys
    // cannot displace older work. These pointers are borrowed only while jobs_ is unchanged.
    std::vector<Job*> ordered(jobs_.size(), nullptr);
    std::size_t       count = 0, activeBuilders = 0;
    for (const auto& job : jobs_) {
        const auto state = job.plan.state();
        if (state == crowd::Crowd::FlowBuildState::Pending || state == crowd::Crowd::FlowBuildState::Ready ||
            state == crowd::Crowd::FlowBuildState::Unreachable)
            ++activeBuilders;
    }
    for (auto& job : jobs_)
        if (job.used == step.tick) ordered[count++] = &job;
    std::sort(ordered.begin(), ordered.begin() + count,
              [](const Job* left, const Job* right) { return left->sequence < right->sequence; });
    for (std::size_t index = 0; index < count; ++index) {
        auto& job = *ordered[index];
        if (job.used != step.tick || job.lease.generation() != 0) continue;
        if (job.plan.state() == crowd::Crowd::FlowBuildState::Cancelled) {
            auto cached = crowd.acquireFlowField(job.goals);
            if (cached) {
                job.lease = std::move(cached).takeValue();
                ++stats_.cacheHits;
                continue;
            }
            if (cached.code() != StatusCode::NotFound) return Result<void>::failure(cached.status());
            cached.ignore("Ready cache miss; explicit bounded preparation follows");
            if (stats_.lastStarts == settings_.maxStartsPerTick || activeBuilders == 64) continue;
            ++stats_.lastStarts;
            auto prepared = crowd.prepareFlowBuild(job.goals);
            if (!prepared) return Result<void>::failure(prepared.status());
            job.plan = std::move(prepared).takeValue();
            ++stats_.prepared;
            ++activeBuilders;
        }
    }
    int remaining = settings_.maxWorkPerTick;
    while (remaining > 0 && count != 0) {
        Job* selected = nullptr;
        for (std::size_t index = 0; index < count; ++index) {
            auto* job = ordered[index];
            if (job->sequence > workAfter_ && job->sequence <= roundThrough_ &&
                job->plan.state() == crowd::Crowd::FlowBuildState::Pending) {
                selected = job;
                break;
            }
        }
        if (!selected) {
            // Freeze admission at each round boundary: newcomers wait until older
            // pending requests have each had their quantum, even across ticks.
            roundThrough_ = ordered[count - 1]->sequence;
            workAfter_    = 0;
            for (std::size_t index = 0; index < count; ++index)
                if (ordered[index]->plan.state() == crowd::Crowd::FlowBuildState::Pending) {
                    selected = ordered[index];
                    break;
                }
        }
        if (!selected) break;
        const int reserved = std::min(remaining, 256);
        stats_.lastWork += reserved;
        auto progress = crowd.advanceFlowBuild(selected->plan, reserved);
        if (!progress) {
            // A failed builder may contain partial internal work; it must never become a ready field.
            auto cancelled = selected->plan.cancel();
            if (!cancelled) return Result<void>::failure(cancelled.status());
            return Result<void>::failure(progress.status());
        }
        const int work = progress.value().heapPops + progress.value().directionCells;
        workAfter_     = selected->sequence;
        remaining -= work;
        stats_.lastWork += work - reserved;
        if (work == 0 && selected->plan.state() == crowd::Crowd::FlowBuildState::Pending) break;
    }
    for (auto& job : jobs_) {
        if (job.used != step.tick || job.lease.generation() != 0) continue;
        if (job.plan.state() != crowd::Crowd::FlowBuildState::Ready &&
            job.plan.state() != crowd::Crowd::FlowBuildState::Unreachable)
            continue;
        auto acquired = crowd.acquireFlowField(std::move(job.plan));
        if (acquired) {
            job.lease = std::move(acquired).takeValue();
            continue;
        }
        if (acquired.code() != StatusCode::Rejected) return Result<void>::failure(acquired.status());
        acquired.ignore("Cache pressure retains completed work for the next quota phase");
        stats_.cachePressure = true;
    }
    return Result<void>::success();
} catch (const std::exception& error) {
    return failure<void>(DiagnosticCode::Failed, error.what());
}
Result<CrowdFlowNavigation::DriveState> CrowdFlowNavigation::steer(crowd::Crowd& crowd, WorldPosition position,
                                                                   WorldPosition target, float radius,
                                                                   crowd::MovementBinding movement,
                                                                   crowd::AgentUpdate&    update) {
    return steer(crowd, position, std::span<const WorldPosition>(&target, 1), target, radius, movement, update);
}
Result<CrowdFlowNavigation::DriveState> CrowdFlowNavigation::steer(crowd::Crowd& crowd, WorldPosition position,
                                                                   std::span<const WorldPosition> targets,
                                                                   WorldPosition target, float radius,
                                                                   crowd::MovementBinding movement,
                                                                   crowd::AgentUpdate&    update) {
    auto goal = goalSet(crowd, targets);
    if (!goal) return Result<DriveState>::failure(goal.status());
    return steerGoals(crowd, position, goal.value(), target, radius, movement, update);
}
Result<CrowdFlowNavigation::DriveState> CrowdFlowNavigation::steerGoals(crowd::Crowd& crowd, WorldPosition position,
                                                                        std::span<const crowd::FlowGoal> goals,
                                                                        WorldPosition target, float radius,
                                                                        crowd::MovementBinding movement,
                                                                        crowd::AgentUpdate&    update) {
    auto destinationCell = goalCell(crowd, target);
    if (!destinationCell) return Result<DriveState>::failure(destinationCell.status());
    if (!std::binary_search(goals.begin(), goals.end(), destinationCell.value()))
        return Result<DriveState>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "Exact destination must belong to the shared goal set", "target"));
    auto found    = std::lower_bound(jobs_.begin(), jobs_.end(), goals, [](const Job& job, const auto& key) {
        return std::lexicographical_compare(job.goals.begin(), job.goals.end(), key.begin(), key.end());
    });
    update.flow   = crowd::FlowLease{};
    update.intent = {crowd::AgentAction::Idle, false, 0.f, 0.f};
    if (found == jobs_.end() || !std::equal(found->goals.begin(), found->goals.end(), goals.begin(), goals.end()) ||
        found->lease.generation() == 0) {
        update.maxSpeed = 0.f;
        return Result<DriveState>::success(DriveState::Pending);
    }
    update.flow       = found->lease;
    const auto source = crowd.getFieldStatus().surface;
    if (radius > source.clearanceRadius ||
        (!movement.movement.isZero() &&
         (movement.movement != source.movement || movement.revision != source.movementRevision))) {
        update.maxSpeed = 0.f;
        return Result<DriveState>::success(DriveState::Unavailable);
    }
    auto sampled = crowd.sampleFlowField(found->lease, position.x, position.y);
    if (!sampled) return Result<DriveState>::failure(sampled.status());
    auto destination = crowd.sampleFlowField(found->lease, target.x, target.y);
    if (!destination) return Result<DriveState>::failure(destination.status());
    if (sampled.value().state == crowd::FlowSampleState::Unreachable ||
        destination.value().state == crowd::FlowSampleState::Unreachable) {
        update.maxSpeed = 0.f;
        return Result<DriveState>::success(DriveState::Unreachable);
    }
    auto cell = goalCell(crowd, position);
    if (!cell) return Result<DriveState>::failure(cell.status());
    update.intent = cell.value() == destinationCell.value()
                        ? crowd::SteeringIntent{crowd::AgentAction::Seek, true, target.x, target.y}
                        : crowd::SteeringIntent{crowd::AgentAction::Flow, false, 0.f, 0.f};
    return Result<DriveState>::success(DriveState::Ready);
}
CrowdFlowNavigationObservation CrowdFlowNavigation::observation() const noexcept {
    auto result = stats_;
    for (const auto& job : jobs_) {
        if (job.lease.generation() == 0)
            ++result.pendingGoals;
        else
            ++result.readyGoals;
    }
    return result;
}
Result<void> RTS::configureCrowdFlowNavigation(CrowdFlowNavigationSettings settings) try {
    if (settings.maxWorkPerTick < 1 || settings.maxWorkPerTick > 1000000 || settings.maxStartsPerTick < 1 ||
        settings.maxStartsPerTick > 16)
        return failure<void>(DiagnosticCode::InvalidArgument, "Invalid shared-flow quotas");
    if (crowdFlowNavigation_) return crowdFlowNavigation_->configure(settings);
    crowdFlowNavigation_ = std::make_unique<CrowdFlowNavigation>(settings);
    return Result<void>::success();
} catch (const std::exception& error) {
    return failure<void>(DiagnosticCode::Failed, error.what());
}
void RTS::clearCrowdFlowNavigation() noexcept {
    if (crowdFlowNavigation_) crowdFlowNavigation_->disable();
    clearCrowdFlowBindings_ = true;
}
Result<std::size_t> RTS::advanceCrowdMotion(const SimulationStep& step) try {
    if (clearCrowdFlowBindings_) {
        crowd::AgentUpdateBatch updates;
        auto                    view = ecs::View<Unit, Unit::Identity, Unit::Crowd>();
        for (auto it = view.begin(); it != view.end(); ++it) {
            auto [identity, settings] = *it;
            if (!identity || !ecs::try_get(identity->self)) continue;
            const crowd::AgentRef ref{settings->runtimeAgent, settings->runtimeOwner};
            if (!crowd_->isAgentAlive(ref)) continue;
            crowd::AgentUpdate update;
            update.reference = ref;
            update.flow      = crowd::FlowLease{};
            updates.agents.push_back(std::move(update));
        }
        auto cleared = crowd_->updateAgents(updates);
        if (!cleared) return Result<std::size_t>::failure(cleared.status());
        clearCrowdFlowBindings_ = false;
    }
    const bool shared = crowdFlowNavigation_ && crowdFlowNavigation_->enabled();
    auto       moved  = shared ? CrowdMotionSystem::stepSharedFlow(step, *crowd_, *crowdFlowNavigation_)
                               : CrowdMotionSystem::step(step, *crowd_);
    if (!moved) return moved;
    if (shared) {
        auto       notices  = crowdFlowNavigation_->takeNotices();
        const auto callback = navigationEvent_;
        for (const auto& notice : notices) {
            auto* unit = dynamic_cast<Unit*>(ecs::try_get(notice.unit));
            if (unit && unit->navigation()->unreachable && unit->navigation()->plannedOrderId == notice.order.id) {
                unit->navigation()->unreachableReported = true;
                if (callback) callback(*unit, notice.order);
            }
        }
    }
    return moved;
} catch (const std::exception& error) {
    return failure<std::size_t>(DiagnosticCode::Failed, error.what());
}
CrowdFlowNavigationObservation RTS::getCrowdFlowNavigationStatus() const noexcept {
    return crowdFlowNavigation_ && crowdFlowNavigation_->enabled() ? crowdFlowNavigation_->observation()
                                                                   : CrowdFlowNavigationObservation{};
}
}  // namespace eve::rts
