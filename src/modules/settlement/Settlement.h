#pragma once

/**
 * @file Settlement.h
 * @brief Domain-neutral, deterministic settlement pipeline.
 *
 * Settlement owns the calculation protocol, not health, armor, shields or
 * any other gameplay state.  A policy adapter reads its domain state, stages
 * a candidate mutation, and commits that candidate only after every stage
 * has succeeded.  RPG, Vehicle, Card and Weapon therefore remain independent
 * domain roots while sharing one result and event contract.
 */

#include "common/Result.h"
#include "common/SubjectRef.h"
#include "common/Time.h"
#include "common/Value.h"
#include "game_event/GameEvent.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace eve::settlement {

/** @brief Kind of ordered calculation stage in a settlement pipeline. */
enum class StageKind : std::uint8_t {
    Validate,
    SourceModifiers,
    TargetMitigation,
    ArmorShield,
    Clamp,
    Apply,
    Event,
    Trigger,
};

/**
 * @brief Return the stable lowercase spelling of a stage kind.
 * @return Borrowed non-null pointer to immutable process-lifetime storage.
 * @ownership The returned static text is not caller-owned and must not be freed.
 * @lifetime Valid for the lifetime of the process; copy it when storing it.
 * @thread Thread-safe because the characters are immutable.
 * @reentrancy Does not invoke callbacks.
 */
[[nodiscard]] const char* stageKindName(StageKind kind) noexcept;

/**
 * @brief Domain-neutral input to one settlement operation.
 *
 * `magnitude` is the requested positive amount before source modifiers.
 * `context` is an owned canonical Value and must contain only serializable
 * data.  `tick` is supplied by the simulation owner; the pipeline never reads
 * wall-clock time.  Causation and correlation are optional canonical event
 * metadata and are copied to the result event.
 */
struct SettlementRequest {
    SubjectRef                source;
    SubjectRef                target;
    std::string               kind;
    double                    magnitude = 0.0;
    std::vector<std::string>  tags;
    Value                     context = Value(Value::Object{});
    SimulationTick            tick    = SimulationTick::zero();
    game_event::CausationRef  causation;
    game_event::CorrelationId correlation;
};

/**
 * @brief Explanation emitted for one executed stage.
 *
 * `before` and `after` are the working amount around this stage.  `details`
 * is an owning canonical object so UI, logs and replay tools do not need to
 * understand a policy-specific pointer or callback.
 */
struct SettlementStageResult {
    StageKind       kind = StageKind::Validate;
    std::string     name;
    eve::StatusCode status  = eve::StatusCode::Ok;
    double          before  = 0.0;
    double          after   = 0.0;
    Value           details = Value(Value::Object{});
};

/**
 * @brief Complete observable outcome of one successful settlement.
 *
 * `applied` is the amount written to the policy-owned state.  `absorbed` is
 * removed by a shield/barrier, `resisted` is removed by mitigation, and
 * `clamped` is removed by the final amount bounds.  `requested` remains the
 * original request even when a critical source modifier increases the
 * effective amount.
 */
struct SettlementResult {
    double                               requested = 0.0;
    double                               applied   = 0.0;
    double                               absorbed  = 0.0;
    double                               resisted  = 0.0;
    double                               clamped   = 0.0;
    bool                                 critical  = false;
    SimulationTick                       tick      = SimulationTick::zero();
    std::vector<SettlementStageResult>   stages;
    std::optional<game_event::GameEvent> event;

    /** @brief Return whether a named stage explanation exists. */
    [[nodiscard]] bool hasStage(std::string_view name) const noexcept;

    /**
     * @brief Return the first stage explanation with this name, or null.
     * @return Borrowed nullable result owned by this SettlementResult.
     * @ownership SettlementResult owns its stage explanations; callers must not delete the result.
     * @lifetime Valid until the result is mutated or destroyed.
     * @thread Read on the thread owning this result; concurrent mutation is not supported.
     * @reentrancy The query invokes no callbacks and is invalid across re-entrant result mutation.
     */
    [[nodiscard]] const SettlementStageResult* stage(std::string_view name) const noexcept;
};

/**
 * @brief Prepared domain mutation used to make settlement application atomic.
 *
 * A policy creates this object from a candidate copy or equivalent private
 * transaction.  `commit` must make the candidate observable atomically; the
 * supplied rollback callback must restore the previous state if a later event
 * append fails.  The object is synchronous and must not retain temporary
 * request/context references.
 */
class PreparedApply {
public:
    using CommitFunction   = std::function<eve::Result<void>()>;
    using RollbackFunction = std::function<void()>;

    /** @brief Construct an empty invalid mutation. */
    PreparedApply() = default;

    /** @brief Construct a mutation from commit and rollback callbacks. */
    PreparedApply(CommitFunction commit, RollbackFunction rollback);

    PreparedApply(const PreparedApply&)            = delete;
    PreparedApply& operator=(const PreparedApply&) = delete;
    PreparedApply(PreparedApply&& other) noexcept;
    PreparedApply& operator=(PreparedApply&& other) noexcept;
    ~PreparedApply();

    /** @brief Return whether both transaction callbacks are available. */
    [[nodiscard]] bool isValid() const noexcept;

    /** @brief Publish the staged candidate; failure must leave state unchanged. */
    [[nodiscard]] eve::Result<void> commit();

    /**
     * @brief Restore or discard the staged candidate; safe to call repeatedly.
     * @remarks The rollback callback is a noexcept contract.  If it throws,
     *          the implementation terminates because reporting an ordinary
     *          failure would falsely promise that state was restored.
     */
    void rollback() noexcept;

    /** @brief Return whether commit has completed successfully. */
    [[nodiscard]] bool isCommitted() const noexcept { return committed_; }

private:
    CommitFunction   commit_;
    RollbackFunction rollback_;
    bool             committed_  = false;
    bool             rolledBack_ = false;
};

class ISettlementPolicy;

/**
 * @brief Mutable calculation frame visible to policy and custom stage code.
 *
 * The frame owns no domain state.  It exposes only the working amount and
 * explanation accumulators; adapters keep health/armor/shield facts in their
 * own authoritative store.  After canonical Apply preparation, amount and
 * numeric result inputs are frozen.  After canonical Event preparation,
 * stage details are frozen too.  An attempted late mutation records a fatal
 * pipeline violation instead of silently creating stale prepared data.  All
 * methods are synchronous and the frame must not escape a settle call.
 */
class SettlementContext {
public:
    /** @brief Return the immutable request currently being settled. */
    [[nodiscard]] const SettlementRequest& request() const noexcept { return request_; }

    /** @brief Return the policy participating in this synchronous call. */
    [[nodiscard]] ISettlementPolicy& policy() const noexcept { return policy_; }

    /** @brief Return the current amount after all preceding stages. */
    [[nodiscard]] double magnitude() const noexcept { return magnitude_; }

    /** @brief Return the accumulated amount absorbed by policy barriers. */
    [[nodiscard]] double absorbed() const noexcept { return absorbed_; }

    /** @brief Return the accumulated amount removed by policy mitigation. */
    [[nodiscard]] double resisted() const noexcept { return resisted_; }

    /** @brief Return the accumulated amount removed by the generic clamp. */
    [[nodiscard]] double clamped() const noexcept { return clamped_; }

    /** @brief Set the current amount; late writes after Apply are rejected. */
    [[nodiscard]] eve::Result<void> setMagnitude(double value);

    /** @brief Add an amount absorbed by a barrier or shield; late writes are rejected. */
    [[nodiscard]] eve::Result<void> addAbsorbed(double value);

    /** @brief Add an amount removed by target mitigation; late writes are rejected. */
    [[nodiscard]] eve::Result<void> addResisted(double value);

    /** @brief Add an amount removed by the final clamp stage; late writes are rejected. */
    [[nodiscard]] eve::Result<void> addClamped(double value);

    /** @brief Mark the result as critical; late writes are rejected. */
    void setCritical(bool value) noexcept {
        if (applyPrepared_) {
            recordMutationViolation("settlement critical flag cannot change after apply preparation", "critical");
            return;
        }
        critical_ = value;
    }

    /** @brief Return whether a source policy marked this settlement critical. */
    [[nodiscard]] bool critical() const noexcept { return critical_; }

    /** @brief Set or clear the maximum amount used by the generic clamp stage. */
    [[nodiscard]] eve::Result<void> setClampMax(std::optional<double> value);

    /**
     * @brief Attach one serializable explanation field to the current stage.
     * @remarks Details are rejected after the Event terminal boundary. The
     *          pipeline reports such a late mutation as a failed settlement.
     */
    void setStageDetail(std::string key, Value value);

    /**
     * @brief Return a contract violation recorded by a late frame mutation.
     * @return Borrowed nullable diagnostic owned by this context, or null when valid.
     * @ownership SettlementContext owns the diagnostic; callers must not delete it.
     * @lifetime Valid until the context is mutated or destroyed.
     * @thread Read on the settlement pipeline's owning thread.
     * @reentrancy Does not invoke callbacks and is invalid across re-entrant context mutation.
     */
    [[nodiscard]] const eve::Diagnostic* mutationViolation() const noexcept {
        return mutationViolation_ ? &*mutationViolation_ : nullptr;
    }

    /** @brief Return the current stage's owned explanation fields. */
    [[nodiscard]] const Value::Object& stageDetails() const noexcept { return stageDetails_; }

    /** @brief Return the projected result visible to trigger preparation. */
    [[nodiscard]] const SettlementResult& projectedResult() const noexcept { return result_; }

    /** @brief Apply the adapter-configured max bound and record any clamp loss. */
    [[nodiscard]] eve::Result<void> applyClamp();

    /** @brief Prepare the adapter mutation without publishing domain state. */
    [[nodiscard]] eve::Result<void> prepareApply();

    /** @brief Build the canonical GameEvent without appending it. */
    [[nodiscard]] eve::Result<void> prepareEvent();

    /**
     * @brief Return the staged mutation, if the apply stage prepared one.
     * @return Borrowed nullable mutation owned by this context.
     * @ownership SettlementContext owns the staged mutation; callers must not delete it.
     * @lifetime Valid until commit/rollback or context destruction.
     * @thread Read and consume on the settlement pipeline's owning thread.
     * @reentrancy Do not retain across callbacks or stage mutation.
     */
    [[nodiscard]] PreparedApply* pendingApply() noexcept { return pendingApply_ ? &*pendingApply_ : nullptr; }

    /**
     * @brief Return the prepared event, if the event stage created one.
     * @return Borrowed nullable event owned by this context.
     * @ownership SettlementContext owns the pending event; callers must not delete it.
     * @lifetime Valid until event commit/rollback or context destruction.
     * @thread Read on the settlement pipeline's owning thread.
     * @reentrancy Do not retain across callbacks or stage mutation.
     */
    [[nodiscard]] const game_event::GameEvent* pendingEvent() const noexcept {
        return pendingEvent_ ? &*pendingEvent_ : nullptr;
    }

    /** @brief Copy the current frame counters into SettlementResult. */
    void synchronizeResult() noexcept;

private:
    friend class SettlementPipeline;

    SettlementContext(const SettlementRequest& request, SettlementResult& result, ISettlementPolicy& policy);
    void beginStage() noexcept {
        stageDetails_.clear();
        mutationViolation_.reset();
    }
    void clearPendingApply() noexcept { pendingApply_.reset(); }
    void clearPendingEvent() noexcept { pendingEvent_.reset(); }
    void recordMutationViolation(std::string message, std::string path) noexcept;

    const SettlementRequest&             request_;
    SettlementResult&                    result_;
    ISettlementPolicy&                   policy_;
    double                               magnitude_ = 0.0;
    double                               absorbed_  = 0.0;
    double                               resisted_  = 0.0;
    double                               clamped_   = 0.0;
    bool                                 critical_  = false;
    std::optional<double>                clampMax_;
    Value::Object                        stageDetails_;
    std::optional<PreparedApply>         pendingApply_;
    std::optional<game_event::GameEvent> pendingEvent_;
    std::optional<eve::Diagnostic>       mutationViolation_;
    bool                                 applyPrepared_ = false;
    bool                                 eventPrepared_ = false;
};

/**
 * @brief Domain-owned policy hooks used by the common settlement pipeline.
 *
 * Implementations must inspect state and prepare candidates only in the
 * calculation stages.  `prepareTrigger` must also be side-effect free;
 * observable trigger work is represented by the committed event and can be
 * consumed by the owning domain after settlement returns.
 */
class ISettlementPolicy {
public:
    virtual ~ISettlementPolicy() = default;

    /** @brief Validate request and target-owned state without mutating it. */
    [[nodiscard]] virtual eve::Result<void> validate(SettlementContext& context) = 0;

    /** @brief Apply source-side modifiers such as critical or elemental power. */
    [[nodiscard]] virtual eve::Result<void> sourceModifiers(SettlementContext& context) = 0;

    /** @brief Apply target mitigation such as resistance or penetration rules. */
    [[nodiscard]] virtual eve::Result<void> targetMitigation(SettlementContext& context) = 0;

    /** @brief Apply armor and shield policy, recording absorbed amount. */
    [[nodiscard]] virtual eve::Result<void> armorShield(SettlementContext& context) = 0;

    /** @brief Configure policy-specific amount bounds for the generic clamp. */
    [[nodiscard]] virtual eve::Result<void> clamp(SettlementContext& context) = 0;

    /** @brief Build an atomic candidate mutation without publishing it. */
    [[nodiscard]] virtual eve::Result<PreparedApply> prepareApply(const SettlementContext& context) = 0;

    /** @brief Validate or prepare trigger semantics without mutating state. */
    [[nodiscard]] virtual eve::Result<void> prepareTrigger(const SettlementContext& context,
                                                           const SettlementResult&  result);
};

/**
 * @brief Deterministically ordered, composable settlement pipeline.
 *
 * The constructor installs the eight canonical stages.  Additional stages
 * can be inserted into any phase with a stable priority and unique name.
 * Apply, Event and Trigger each have an unregistrable terminal step; custom
 * stages in those phases always run before their terminal step regardless of
 * priority or name.  Other ordering is phase, priority, name, then
 * registration sequence.
 */
class SettlementPipeline {
public:
    using StageFunction = std::function<eve::Result<void>(SettlementContext&)>;

    /** @brief Construct a pipeline with the canonical stages installed. */
    SettlementPipeline();

    /** @brief Add one named custom stage; duplicate names are rejected. */
    [[nodiscard]] eve::Result<void> addStage(StageKind kind, std::string name, int priority, StageFunction function);

    /**
     * @brief Execute one settlement transaction against a domain policy.
     * @param request Owned request data that remains valid for this synchronous call.
     * @param policy Borrowed adapter whose domain state outlives this call.
     * @param events Optional borrowed event stream; a present stream receives
     *        the canonical result event after the policy mutation commits.
     * @return A complete result, or a structured failure. Any failure rolls
     *         back the prepared mutation and leaves policy state unchanged.
     */
    [[nodiscard]] eve::Result<SettlementResult> settle(const SettlementRequest& request, ISettlementPolicy& policy,
                                                       game_event::GameEventLog* events = nullptr) const;

private:
    struct StageEntry {
        StageKind     kind = StageKind::Validate;
        std::string   name;
        int           priority     = 0;
        std::uint64_t registration = 0;
        bool          terminal     = false;
        StageFunction function;
    };

    std::vector<StageEntry> stages_;
    std::uint64_t           nextRegistration_ = 1;
};

}  // namespace eve::settlement
