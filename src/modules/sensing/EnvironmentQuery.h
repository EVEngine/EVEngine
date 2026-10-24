#pragma once

#include "common/Export.h"
#include "common/Value.h"
#include "sensing/Targeting.h"

#include <memory>
#include <span>

namespace eve::sensing {

/** @brief A query-local identity and typed location; subjectKey names a mirrored fact, not a UUID. */
struct EnvironmentItem {
    std::uint64_t              id = 0;
    TargetLocation             location;
    std::optional<std::string> subjectKey;
};

/** @brief Owning context snapshot, resolved once at instance creation. */
struct EnvironmentContext {
    std::string                 name;
    std::vector<TargetLocation> locations;
};

/** @brief Per-execution values. No world pointers or callbacks are retained. */
struct EnvironmentRequest {
    std::vector<EnvironmentContext> contexts;
    std::vector<EnvironmentItem>    input;
};

/** @brief Borrowed providers for this synchronous execution only. */
struct EnvironmentServices {
    OptionalRef<const ILineOfSightQuery> lineOfSight;
};

/** @brief Cost tiers used only for explicitly reorderable tests. */
enum class EnvironmentCost : std::uint8_t { Low, Medium, High };
/** @brief Independent hard constraint and utility purposes. */
enum class EnvironmentPurpose : std::uint8_t { Filter, Score, FilterAndScore };
/** @brief How repeated context measurements determine acceptance. */
enum class EnvironmentFilterOp : std::uint8_t { All, Any };
/** @brief How repeated context measurements form one raw metric. */
enum class EnvironmentAggregate : std::uint8_t { Average, Minimum, Maximum };
/** @brief Explicit scoring reference population. */
enum class EnvironmentNormalization : std::uint8_t { FixedRange, ActivePopulation };
/** @brief Utility curve applied after normalization and preference direction. */
enum class EnvironmentCurve : std::uint8_t { Linear, Square, SquareRoot };
/** @brief Ranking preference; reference uses distance from a target raw value. */
enum class EnvironmentPreference : std::uint8_t { Higher, Lower, Reference };
/** @brief All retains ranked candidates; Best returns one candidate without assigning primary. */
enum class EnvironmentRunMode : std::uint8_t { All, Best };
/** @brief None, bounded stage counts, or bounded per-item explanations. */
enum class EnvironmentTraceLevel : std::uint8_t { None, Summary, Detailed };
/** @brief Input facts or generated points on a world-space XY plane. */
enum class EnvironmentGeneratorKind : std::uint8_t { Input, Ring, Grid };
/** @brief Built-in measurements and the typed extension point. */
enum class EnvironmentMetricKind : std::uint8_t { Distance, LineOfSight, Custom };

/**
 * @brief Intrinsic metric guarantees; false reorderable preserves an ordering barrier.
 * @details True promises measurements depend only on the item, context and stable service state;
 * calls may be reordered or skipped. Stateful cross-item measurements must keep this false.
 */
struct EnvironmentMetricContract {
    CoordinateSpace space               = CoordinateSpace::World2D;
    EnvironmentCost cost                = EnvironmentCost::High;
    bool            reorderable         = false;
    bool            requiresLineOfSight = false;
};

/**
 * @brief Immutable measurement extension; filtering, aggregation and scoring belong to the executor.
 * @ownership Definitions and compiled plans share ownership because either may outlive the other.
 * @thread Called synchronously on the provider's simulation thread; never retain borrowed arguments.
 * @reentrancy No locks are held. A metric must not mutate the queried world or this instance.
 */
class IEnvironmentMetric {
public:
    /** @brief Releases a measurement implementation. */
    virtual ~IEnvironmentMetric() = default;
    /** @brief Declares supported space and optimization guarantees. */
    [[nodiscard]] virtual EnvironmentMetricContract contract() const noexcept = 0;
    /** @brief Returns one finite raw measurement, or a structured backend failure. */
    [[nodiscard]] virtual Result<double> measure(const EnvironmentItem& item, const TargetLocation& reference,
                                                 const EnvironmentServices& services) const = 0;
};

/** @brief Generator parameters; maxItems is a hard resource bound, never silent truncation. */
struct EnvironmentGenerator {
    EnvironmentGeneratorKind kind = EnvironmentGeneratorKind::Input;
    std::string              context;
    double                   radius   = 10;
    double                   spacing  = 2;
    std::uint32_t            samples  = 16;
    std::uint32_t            maxItems = 4096;
};

/** @brief One metric with independent filtering and scoring parameters. */
struct EnvironmentTest {
    std::string                               id;
    std::string                               context;
    EnvironmentMetricKind                     metric = EnvironmentMetricKind::Distance;
    std::shared_ptr<const IEnvironmentMetric> custom;
    EnvironmentPurpose                        purpose        = EnvironmentPurpose::FilterAndScore;
    EnvironmentFilterOp                       filterOp       = EnvironmentFilterOp::All;
    EnvironmentAggregate                      aggregate      = EnvironmentAggregate::Average;
    double                                    minimum        = 0;
    double                                    maximum        = std::numeric_limits<double>::max();
    EnvironmentNormalization                  normalization  = EnvironmentNormalization::FixedRange;
    double                                    scoreMinimum   = 0;
    double                                    scoreMaximum   = 100;
    EnvironmentPreference                     preference     = EnvironmentPreference::Lower;
    double                                    referenceValue = 0;
    EnvironmentCurve                          curve          = EnvironmentCurve::Linear;
    double                                    weight         = 1;
};

/** @brief Ordered alternative: advance only when this option has no valid items. */
struct EnvironmentOption {
    std::string                  id;
    EnvironmentGenerator         generator;
    std::vector<EnvironmentTest> tests;
};

/** @brief Authoring value; compile validates and owns a copy. Not a persistent wire format. */
struct EnvironmentQueryDefinition {
    std::string                    id;
    std::uint64_t                  revision = 1;
    CoordinateSpace                space    = CoordinateSpace::World2D;
    std::vector<std::string>       contexts;
    std::vector<EnvironmentOption> options;
    EnvironmentRunMode             mode          = EnvironmentRunMode::All;
    std::uint32_t                  maxResults    = 8;
    bool                           optimize      = true;
    EnvironmentTraceLevel          trace         = EnvironmentTraceLevel::Summary;
    std::uint32_t                  maxTraceItems = 256;
};

/** @brief Per-item score contribution with its exact normalization population bounds. */
struct EnvironmentContribution {
    std::string test;
    double      raw        = 0;
    double      lower      = 0;
    double      upper      = 0;
    double      normalized = 0;
    double      weighted   = 0;
    bool        passed     = true;
};

/** @brief Stage counts include deferred tests and actual backend measurement calls. */
struct EnvironmentStageTrace {
    std::string option;
    std::string test;
    std::size_t input    = 0;
    std::size_t output   = 0;
    std::size_t measured = 0;
    bool        deferred = false;
};

/** @brief Bounded explanation for one option-local candidate. */
struct EnvironmentItemTrace {
    std::string                          option;
    std::uint64_t                        id = 0;
    std::string                          rejectedBy;
    std::vector<EnvironmentContribution> contributions;
};

/** @brief Owning trace survives failed execution; untested is distinct from passed. */
struct EVENGINE_API_PLATFORM EnvironmentTrace {
    std::string                        query;
    std::uint64_t                      revision = 0;
    std::string                        state    = "ready";
    std::vector<EnvironmentStageTrace> stages;
    std::vector<EnvironmentItemTrace>  items;
    bool                               truncated = false;
    std::vector<Diagnostic>            diagnostics; /**< Structured failure details; empty after success. */
    /** @brief Versioned deterministic diagnostic projection; ignores no runtime state. */
    [[nodiscard]] Value toValue() const;
};

/** @brief One owning ranked candidate. */
struct EnvironmentRankedItem {
    EnvironmentItem item;
    double          score = 0;
};

/** @brief Owning result; empty ranked is successful exhaustion, not a backend failure. */
struct EVENGINE_API_PLATFORM EnvironmentQueryResult {
    std::string                        option;
    std::vector<EnvironmentRankedItem> ranked;
    /** @brief Canonical typed result projection shared by scripts and diagnostics. */
    [[nodiscard]] Value toValue() const;
};

struct EnvironmentPlanData;
class EnvironmentQueryInstance;

/**
 * @brief Immutable resolved plan, safe to reuse after definitions are edited or destroyed.
 * @ownership Shares immutable compiled data with instances; no borrowed definition storage.
 */
class EVENGINE_API_PLATFORM EnvironmentQueryPlan {
public:
    /** @brief Validates types, resolves context slots and builds a constrained execution order.
     * @cost Copies and resolves a definition once per revision; retain the plan for repeated requests.
     */
    [[nodiscard]] static Result<EnvironmentQueryPlan> compile(const EnvironmentQueryDefinition& definition);
    /** @brief Validates and snapshots a request into an independently owned one-shot instance.
     * @cost Copies context and input values once per request; execution is a separate explicit step.
     */
    [[nodiscard]] Result<EnvironmentQueryInstance> instantiate(EnvironmentRequest request) const;

private:
    friend class EnvironmentQueryInstance;
    explicit EnvironmentQueryPlan(std::shared_ptr<const EnvironmentPlanData> data) : data_(std::move(data)) {}
    std::shared_ptr<const EnvironmentPlanData> data_;
};

/**
 * @brief One-shot synchronous execution state with query-local contexts and diagnostics.
 * @thread Execute on the simulation thread required by services. No concurrent mutation.
 * @reentrancy Re-entering run is rejected. No callbacks are invoked while holding locks.
 */
class EVENGINE_API_PLATFORM EnvironmentQueryInstance {
public:
    EnvironmentQueryInstance(const EnvironmentQueryInstance&)                = delete;
    EnvironmentQueryInstance& operator=(const EnvironmentQueryInstance&)     = delete;
    EnvironmentQueryInstance(EnvironmentQueryInstance&&) noexcept            = default;
    EnvironmentQueryInstance& operator=(EnvironmentQueryInstance&&) noexcept = default;
    /** @brief Runs the compiled plan and returns owning results; repeated execution is rejected.
     * @ownership Services are borrowed only until return; instance never stores provider references.
     * @cost Candidate count times tests and contexts, plus final sorting; provider calls may dominate.
     */
    [[nodiscard]] Result<EnvironmentQueryResult> run(const EnvironmentServices& services);
    /** @brief Borrows diagnostics until instance mutation or destruction; copy for later use. */
    [[nodiscard]] const EnvironmentTrace& trace() const noexcept { return trace_; }

private:
    friend class EnvironmentQueryPlan;
    EnvironmentQueryInstance(std::shared_ptr<const EnvironmentPlanData> plan, EnvironmentRequest request);
    [[nodiscard]] Result<EnvironmentQueryResult> evaluate(const EnvironmentServices& services);
    std::shared_ptr<const EnvironmentPlanData>   plan_;
    EnvironmentRequest                           request_;
    EnvironmentTrace                             trace_;
    bool                                         started_ = false;
};

}  // namespace eve::sensing
