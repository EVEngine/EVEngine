#include "gpuagents/GpuAgentSimulation.h"

#include "gpuagents/solvers/BirdSolver.h"
#include "gpuagents/solvers/FishSolver.h"
#include "gpuagents/solvers/LifeNetworkSolver.h"
#include "gpuagents/solvers/PetalSolver.h"

#include "common/Assert.h"
#include "common/Diagnostic.h"

#include <utility>

namespace eve::gpuagents {

Result<void> GpuAgentSimulation::makeDefaultSolver() {
    switch (profile_.kind) {
        case EffectKind::Fish:
            solver_ = std::make_unique<FishSolver>();
            break;
        case EffectKind::LifeNetwork:
            solver_ = std::make_unique<LifeNetworkSolver>();
            break;
        case EffectKind::Bird:
            solver_ = std::make_unique<BirdSolver>();
            break;
        case EffectKind::Petal:
            solver_ = std::make_unique<PetalSolver>();
            break;
        default:
            return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "unknown EffectKind",
                                                           "kind", {}, "gpuagents"));
    }
    return Result<void>::success();
}

Result<void> GpuAgentSimulation::configure(const EffectProfile& profile) {
    if (profile.base().maxAgents <= 0) {
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "maxAgents must be positive",
                                                       "maxAgents", {}, "gpuagents"));
    }
    if (profile.base().fixedDt <= 0.f) {
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "fixedDt must be positive",
                                                       "fixedDt", {}, "gpuagents"));
    }
    profile_  = profile;
    capacity_ = profile.base().maxAgents;
    buffers_[0].assign(static_cast<size_t>(capacity_), AgentState{});
    buffers_[1].assign(static_cast<size_t>(capacity_), AgentState{});
    front_       = 0;
    accumulator_ = 0.f;
    simTime_     = 0.f;
    stepCount_   = 0;
    configured_  = true;
    auto solverRes = makeDefaultSolver();
    if (!solverRes.ok()) {
        configured_ = false;
        return solverRes;
    }
    return Result<void>::success();
}

Result<void> GpuAgentSimulation::setSolver(std::unique_ptr<IAgentSolver> solver) {
    if (!solver) {
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "solver is null", "solver", {}, "gpuagents"));
    }
    if (configured_ && solver->kind() != profile_.kind) {
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "solver kind mismatch", "solver", {}, "gpuagents"));
    }
    solver_ = std::move(solver);
    return Result<void>::success();
}

Result<void> GpuAgentSimulation::initialize(int count, const std::function<AgentState(int)>& spawnFn) {
    if (!configured_) {
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::PreconditionViolation,
                                                       "simulation not configured", "configure", {}, "gpuagents"));
    }
    if (!spawnFn) {
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "spawnFn is empty", "spawnFn", {}, "gpuagents"));
    }
    if (count < 0 || count > capacity_) {
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "initialize count out of range", "count", {}, "gpuagents"));
    }
    auto& buf = buffers_[front_];
    for (int i = 0; i < capacity_; ++i) {
        if (i < count) {
            AgentState s = spawnFn(i);
            s.alive      = 1;
            buf[static_cast<size_t>(i)] = s;
        } else {
            buf[static_cast<size_t>(i)] = AgentState{};
        }
    }
    buffers_[1 - front_] = buf;
    accumulator_         = 0.f;
    return Result<void>::success();
}

void GpuAgentSimulation::reset() {
    if (!configured_) return;
    for (auto& b : buffers_) {
        for (auto& s : b) s = AgentState{};
    }
    accumulator_ = 0.f;
    simTime_     = 0.f;
    stepCount_   = 0;
}

int GpuAgentSimulation::aliveCount() const {
    int n = 0;
    for (const auto& s : buffers_[front_]) {
        if (s.alive) ++n;
    }
    return n;
}

Result<void> GpuAgentSimulation::step(float dt, const EnvironmentSnapshot& env) {
    if (!configured_ || !solver_) {
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::PreconditionViolation,
                                                       "simulation not configured", "configure", {}, "gpuagents"));
    }
    EV_PARAM_CHECK(dt >= 0.f);
    accumulator_ += dt;
    const float fixed = profile_.base().fixedDt;
    int         guard = 0;
    while (accumulator_ + 1e-8f >= fixed && guard < 8) {
        const int read  = front_;
        const int write = 1 - front_;
        solver_->step(buffers_[read], buffers_[write], profile_, env, fixed);
        front_ = write;
        accumulator_ -= fixed;
        simTime_ += fixed;
        ++stepCount_;
        ++guard;
    }
    return Result<void>::success();
}

}  // namespace eve::gpuagents
