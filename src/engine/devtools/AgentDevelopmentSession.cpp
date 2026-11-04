#include "devtools/AgentDevelopmentSession.hpp"

#include "devtools/McpTaskProfile.hpp"

#include <algorithm>
#include <set>
#include <utility>

namespace eve::dev {
namespace {

AgentDevelopmentResult applied() { return {"applied", {}}; }

AgentDevelopmentResult rejected(std::string code, std::string message) { return {std::move(code), std::move(message)}; }

bool escalateEdgeAllowed(std::string_view from, std::string_view to) {
    if (from == to) return false;
    if (from == "content.scene" && (to == "verify.play" || to == "content.map" || to == "workshop.rpg")) return true;
    if (from == "content.map" && (to == "verify.play" || to == "content.scene" || to == "workshop.rpg")) return true;
    if (from == "script.bind" && (to == "verify.play" || to == "fix.compile")) return true;
    if (from == "verify.play" && (to == "content.scene" || to == "content.map" || to == "script.bind" ||
                                  to == "workshop.rpg" || to == "fix.compile"))
        return true;
    if (from == "fix.compile" && (to == "script.bind" || to == "verify.play")) return true;
    if (from == "workshop.rpg" &&
        (to == "verify.play" || to == "content.scene" || to == "content.map" || to == "script.bind"))
        return true;
    return false;
}

bool canAdvance(AgentDevelopmentPhase from, AgentDevelopmentPhase to) {
    if (to == AgentDevelopmentPhase::Recover)
        return from != AgentDevelopmentPhase::Recover && from != AgentDevelopmentPhase::Complete &&
               from != AgentDevelopmentPhase::Aborted;
    switch (from) {
        case AgentDevelopmentPhase::Discover: return to == AgentDevelopmentPhase::Modify;
        case AgentDevelopmentPhase::Modify: return to == AgentDevelopmentPhase::Run;
        case AgentDevelopmentPhase::Run: return to == AgentDevelopmentPhase::Observe;
        case AgentDevelopmentPhase::Observe: return to == AgentDevelopmentPhase::Verify;
        case AgentDevelopmentPhase::Verify: return to == AgentDevelopmentPhase::Modify;
        case AgentDevelopmentPhase::Recover:
            return to == AgentDevelopmentPhase::Modify || to == AgentDevelopmentPhase::Run;
        default: return false;
    }
}

}  // namespace

AgentDevelopmentSession& AgentDevelopmentSession::instance() {
    static AgentDevelopmentSession session;
    return session;
}

AgentDevelopmentResult AgentDevelopmentSession::start(std::string                           objective,
                                                      std::vector<AgentAcceptanceCriterion> criteria,
                                                      std::string taskKind, std::string profile) {
    if (active()) return rejected("active-session", "A development session is already active");
    if (objective.empty()) return rejected("invalid-objective", "objective must not be empty");
    if (criteria.empty()) return rejected("invalid-criteria", "at least one acceptance criterion is required");
    std::string resolvedProfile;
    std::string resolvedKind;
    std::string errorCode;
    std::string errorMessage;
    if (!mcpResolveTaskProfile(taskKind, profile, &resolvedProfile, &resolvedKind, &errorCode, &errorMessage))
        return rejected(std::move(errorCode), std::move(errorMessage));
    std::set<std::string> ids;
    for (const auto& criterion : criteria) {
        if (criterion.id.empty() || criterion.description.empty())
            return rejected("invalid-criterion", "criterion id and description must not be empty");
        if (!ids.insert(criterion.id).second) return rejected("duplicate-criterion", "criterion ids must be unique");
    }
    reset();
    sessionId_     = "agent.dev." + std::to_string(nextSession_++);
    objective_     = std::move(objective);
    taskKind_      = std::move(resolvedKind);
    profile_       = std::move(resolvedProfile);
    criteria_      = std::move(criteria);
    phase_         = AgentDevelopmentPhase::Discover;
    escalateCount_ = 0;
    mcpSetSessionProfile(profile_);
    return applied();
}

AgentDevelopmentResult AgentDevelopmentSession::escalate(std::string_view sessionId, std::string_view toProfile) {
    if (sessionId != sessionId_) return rejected("session-not-found", "development session was not found");
    if (!active()) return rejected("session-terminal", "development session is already terminal");
    std::string resolvedProfile;
    std::string resolvedKind;
    std::string errorCode;
    std::string errorMessage;
    if (!mcpResolveTaskProfile({}, toProfile, &resolvedProfile, &resolvedKind, &errorCode, &errorMessage))
        return rejected(std::move(errorCode), std::move(errorMessage));
    if (!escalateEdgeAllowed(profile_, resolvedProfile))
        return rejected("invalid-escalate", "requested profile is not a legal escalate edge from the current profile");
    profile_  = std::move(resolvedProfile);
    taskKind_ = std::move(resolvedKind);
    ++escalateCount_;
    mcpSetSessionProfile(profile_);
    return applied();
}

AgentDevelopmentResult AgentDevelopmentSession::advance(std::string_view sessionId, AgentDevelopmentPhase phase) {
    if (sessionId != sessionId_) return rejected("session-not-found", "development session was not found");
    if (!active()) return rejected("session-terminal", "development session is already terminal");
    if (!canAdvance(phase_, phase))
        return rejected("invalid-transition", "requested phase does not follow the development workflow");
    phase_ = phase;
    return applied();
}

AgentDevelopmentResult AgentDevelopmentSession::record(std::string_view sessionId, AgentDevelopmentEvidence evidence) {
    if (sessionId != sessionId_) return rejected("session-not-found", "development session was not found");
    if (!active()) return rejected("session-terminal", "development session is already terminal");
    if (evidence.kind.empty() || evidence.summary.empty())
        return rejected("invalid-evidence", "evidence kind and summary must not be empty");
    static const std::set<std::string> kEvidenceKinds{
        "runtime-observation", "test", "screenshot", "checkpoint", "artifact", "manual", "play-trace"};
    if (evidence.kind == "eval") return rejected("invalid-evidence-kind", "required criteria reject eval evidence");
    if (!kEvidenceKinds.contains(evidence.kind))
        return rejected("invalid-evidence-kind", "evidence kind is not part of schema version one");
    if (evidence.status != "pass" && evidence.status != "fail" && evidence.status != "pending")
        return rejected("invalid-evidence-status", "evidence status must be pass, fail, or pending");
    auto criterion = std::find_if(criteria_.begin(), criteria_.end(),
                                  [&](const auto& value) { return value.id == evidence.criterionId; });
    if (criterion == criteria_.end())
        return rejected("criterion-not-found", "evidence criterion was not declared by the session");
    evidence.sequence = nextEvidence_++;
    const bool visual = criterion->id == "visual" || criterion->id == "screenshot";
    if (evidence.status == "pass" && evidence.kind != "manual") {
        if (!visual || evidence.kind == "screenshot") criterion->passed = true;
    }
    if (evidence.status == "fail") criterion->passed = false;
    evidence_.push_back(std::move(evidence));
    return applied();
}

AgentDevelopmentResult AgentDevelopmentSession::complete(std::string_view sessionId, std::string summary) {
    if (sessionId != sessionId_) return rejected("session-not-found", "development session was not found");
    if (!active()) return rejected("session-terminal", "development session is already terminal");
    if (phase_ != AgentDevelopmentPhase::Verify)
        return rejected("invalid-phase", "development session can complete only from verify");
    if (!readyToComplete())
        return rejected("acceptance-incomplete", "required acceptance criteria do not all have passing evidence");
    if (summary.empty()) return rejected("invalid-summary", "completion summary must not be empty");
    summary_ = std::move(summary);
    phase_   = AgentDevelopmentPhase::Complete;
    return applied();
}

AgentDevelopmentResult AgentDevelopmentSession::abort(std::string_view sessionId, std::string reason) {
    if (sessionId != sessionId_) return rejected("session-not-found", "development session was not found");
    if (!active()) return rejected("session-terminal", "development session is already terminal");
    if (reason.empty()) return rejected("invalid-reason", "abort reason must not be empty");
    summary_ = std::move(reason);
    phase_   = AgentDevelopmentPhase::Aborted;
    return applied();
}

void AgentDevelopmentSession::reset() {
    sessionId_.clear();
    objective_.clear();
    taskKind_.clear();
    profile_.clear();
    summary_.clear();
    criteria_.clear();
    evidence_.clear();
    phase_         = AgentDevelopmentPhase::Aborted;
    nextEvidence_  = 1;
    escalateCount_ = 0;
    mcpSetSessionProfile({});
}

bool AgentDevelopmentSession::active() const {
    return !sessionId_.empty() && phase_ != AgentDevelopmentPhase::Complete && phase_ != AgentDevelopmentPhase::Aborted;
}

bool AgentDevelopmentSession::readyToComplete() const {
    return std::all_of(criteria_.begin(), criteria_.end(), [&](const auto& criterion) {
        if (!criterion.required) return true;
        if (!criterion.passed) return false;
        const bool visual = criterion.id == "visual" || criterion.id == "screenshot";
        if (!visual) return true;
        return std::any_of(evidence_.begin(), evidence_.end(), [&](const auto& evidence) {
            return evidence.criterionId == criterion.id && evidence.kind == "screenshot" && evidence.status == "pass";
        });
    });
}

std::string_view agentDevelopmentPhaseName(AgentDevelopmentPhase phase) {
    switch (phase) {
        case AgentDevelopmentPhase::Discover: return "discover";
        case AgentDevelopmentPhase::Modify: return "modify";
        case AgentDevelopmentPhase::Run: return "run";
        case AgentDevelopmentPhase::Observe: return "observe";
        case AgentDevelopmentPhase::Verify: return "verify";
        case AgentDevelopmentPhase::Recover: return "recover";
        case AgentDevelopmentPhase::Complete: return "complete";
        case AgentDevelopmentPhase::Aborted: return "aborted";
    }
    return "aborted";
}

AgentDevelopmentResult parseAgentDevelopmentPhase(std::string_view name, AgentDevelopmentPhase* phase) {
    if (!phase) return rejected("invalid-output", "phase output must not be null");
    if (name == "discover")
        *phase = AgentDevelopmentPhase::Discover;
    else if (name == "modify")
        *phase = AgentDevelopmentPhase::Modify;
    else if (name == "run")
        *phase = AgentDevelopmentPhase::Run;
    else if (name == "observe")
        *phase = AgentDevelopmentPhase::Observe;
    else if (name == "verify")
        *phase = AgentDevelopmentPhase::Verify;
    else if (name == "recover")
        *phase = AgentDevelopmentPhase::Recover;
    else
        return rejected("invalid-phase", "phase must be discover, modify, run, observe, verify, or recover");
    return applied();
}

}  // namespace eve::dev
