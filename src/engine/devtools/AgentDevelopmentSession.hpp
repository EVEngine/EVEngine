#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace eve::dev {

/** @brief Ordered stage of an Agent-driven game-development session. */
enum class AgentDevelopmentPhase { Discover, Modify, Run, Observe, Verify, Recover, Complete, Aborted };

/** @brief One explicit acceptance condition for a development objective. */
struct AgentAcceptanceCriterion {
    std::string id;
    std::string description;
    bool        required = true;
    bool        passed   = false;
};

/** @brief Immutable evidence recorded against an acceptance condition. */
struct AgentDevelopmentEvidence {
    std::uint64_t sequence = 0;
    std::string   criterionId;
    std::string   kind;
    std::string   status;
    std::string   summary;
    std::string   artifact;
};

/** @brief Structured result for development-session state transitions. */
struct [[nodiscard]] AgentDevelopmentResult {
    std::string code;
    std::string message;

    /** @brief Whether the requested transition was accepted. */
    bool isAccepted() const { return code == "applied"; }
};

/**
 * @brief Process-owned state machine and evidence ledger for Agent game development.
 *
 * All methods have main-thread affinity and never invoke callbacks. The singleton
 * owns all strings and snapshots until reset or process shutdown. It does not own
 * game, Editor, renderer, filesystem, or test-runner state; evidence is an immutable
 * receipt that refers to those authoritative systems.
 */
class AgentDevelopmentSession final {
public:
    /** @brief Return the process-lifetime session authority. */
    static AgentDevelopmentSession& instance();

    /** @brief Start one session; rejects empty objectives, duplicate criteria, or an active session. */
    [[nodiscard]] AgentDevelopmentResult start(std::string objective,
                                               std::vector<AgentAcceptanceCriterion> criteria);
    /** @brief Advance through the declared workflow graph without mutating game state. */
    [[nodiscard]] AgentDevelopmentResult advance(std::string_view sessionId, AgentDevelopmentPhase phase);
    /** @brief Append evidence; only pass evidence updates criterion satisfaction. */
    [[nodiscard]] AgentDevelopmentResult record(std::string_view sessionId, AgentDevelopmentEvidence evidence);
    /** @brief Complete only from Verify when every required criterion has passing evidence. */
    [[nodiscard]] AgentDevelopmentResult complete(std::string_view sessionId, std::string summary);
    /** @brief Abort an active session with an explicit reason. */
    [[nodiscard]] AgentDevelopmentResult abort(std::string_view sessionId, std::string reason);
    /** @brief Clear all process-owned session metadata. Intended for host teardown and tests. */
    void reset();

    /** @brief Whether a non-terminal session currently owns the workflow. */
    bool active() const;
    /** @brief Stable process-local session identity, invalidated by reset. */
    const std::string& sessionId() const { return sessionId_; }
    /** @brief Current workflow phase. */
    AgentDevelopmentPhase phase() const { return phase_; }
    /** @brief User-supplied objective. */
    const std::string& objective() const { return objective_; }
    /** @brief Terminal summary or abort reason. */
    const std::string& summary() const { return summary_; }
    /** @brief Immutable acceptance snapshot owned by this session. */
    const std::vector<AgentAcceptanceCriterion>& criteria() const { return criteria_; }
    /** @brief Immutable evidence snapshot owned by this session. */
    const std::vector<AgentDevelopmentEvidence>& evidence() const { return evidence_; }
    /** @brief Whether all required criteria have passing evidence. */
    bool readyToComplete() const;

private:
    AgentDevelopmentSession() = default;

    std::string                            sessionId_;
    std::string                            objective_;
    std::string                            summary_;
    AgentDevelopmentPhase                  phase_ = AgentDevelopmentPhase::Aborted;
    std::vector<AgentAcceptanceCriterion>  criteria_;
    std::vector<AgentDevelopmentEvidence>  evidence_;
    std::uint64_t                          nextSession_  = 1;
    std::uint64_t                          nextEvidence_ = 1;
};

/** @brief Stable JSON protocol name for a development phase. */
std::string_view agentDevelopmentPhaseName(AgentDevelopmentPhase phase);
/** @brief Parse a stable JSON protocol phase name. */
[[nodiscard]] AgentDevelopmentResult parseAgentDevelopmentPhase(std::string_view name,
                                                                AgentDevelopmentPhase* phase);

}  // namespace eve::dev
