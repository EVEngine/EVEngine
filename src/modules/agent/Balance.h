#pragma once

#include "common/Export.h"
#include "common/Result.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace ssq {
class Table;
class Class;
}  // namespace ssq

namespace eve::agent::balance {

/** @brief One tunable scalar; equal bounds freeze it. Names are unique adapter keys. */
struct Parameter {
    std::string name;
    double      initial = 0;
    double      minimum = 0;
    double      maximum = 1;
};

/** @brief Desired metric interval; scale normalizes units, weight expresses design priority. */
struct Target {
    std::string name;
    double      minimum = 0;
    double      maximum = 1;
    double      scale   = 1;
    double      weight  = 1;
};

/** @brief Owning search specification. Seeds for tuning and holdout must be unique and disjoint. */
struct Problem {
    std::vector<Parameter>     parameters;
    std::vector<Target>        targets;
    std::vector<std::uint64_t> trainingSeeds{11, 22, 33};
    std::vector<std::uint64_t> validationSeeds{101, 202, 303};
    std::uint64_t              searchSeed   = 7;
    std::uint32_t              candidates   = 128;
    std::uint32_t              steps        = 120;
    double                     dt           = 0.1;
    double                     changeWeight = 0.01;
};

/**
 * @brief Game-owned isolated simulation adapter, not a second combat implementation.
 * @ownership Caller owns the adapter. Inputs are borrowed only during evaluate; output owns its metrics.
 * @thread Domain owner thread. No locks are held; do not reenter/destroy the search or adapter in callbacks.
 * @remarks Every call must reset domain state, events and named RNG streams from seed, then advance
 * exactly steps ticks of dt. Return metrics in target order. Errors are failures, never low scores.
 * Learned/heuristic policy populations may be evaluated here; no automatic policy training is implied.
 */
class IEvaluator {
public:
    /** @brief Release the domain adapter. */
    virtual ~IEvaluator() = default;
    /**
     * @brief Reset and simulate a single candidate and seed.
     * @cost One domain rollout per call; amortize by reusing immutable domain definitions.
     */
    [[nodiscard]] virtual Result<std::vector<double>> evaluate(std::span<const double> parameters, std::uint64_t seed,
                                                               double dt, std::uint32_t steps) = 0;
};

/** @brief Per-target sample mean and sample standard deviation across the specified seeds. */
struct Metric {
    double mean      = 0;
    double deviation = 0;
};

/** @brief Owning measurements; score includes parameter drift, loss contains only target violations. */
struct Assessment {
    std::vector<Metric> metrics;
    double              loss  = 0;
    double              score = 0;
};

/** @brief Holdout status concerns metric means only; it is not a statistical confidence guarantee. */
enum class Verdict { WithinTargets, OutsideTargets };

/** @brief Owning proposal and evidence. No game configuration is modified by the search. */
struct Report {
    Problem             problem;
    std::vector<double> parameters;
    Assessment          baseline;
    Assessment          tuned;
    Assessment          baselineValidation;
    Assessment          validation;
    std::uint64_t       evaluations = 0;
    Verdict             verdict     = Verdict::OutsideTargets;
};

/**
 * @brief Configure-then-run builder for bounded black-box numerical balancing.
 * @ownership Owns configuration; borrows evaluators synchronously and returns owning reports.
 * @thread Owner thread only. configure/run reentry returns Conflict. No background work or retained callbacks.
 * @remarks The initial configuration is always candidate zero; selection sees training seeds only.
 * Search evaluates lower and upper parameter corners when budget permits, then uses local
 * feasible coordinate restoration, SplitMix64, global exploration and shrinking mutations
 * of the best proposal.
 * Same build and deterministic evaluator reproduce results; cross-platform floating point uses tolerance.
 * No ECS state, links, persistence, rendering or optional backend is introduced.
 */
class EVENGINE_API_FOUNDATION BalanceSearch {
public:
    /** @brief Create an unconfigured search builder. */
    BalanceSearch()                                = default;
    BalanceSearch(const BalanceSearch&)            = delete;
    BalanceSearch& operator=(const BalanceSearch&) = delete;
    /** @brief Register the VM-owned builder in the Agent script surface; composition thread only. */
    static void expose(ssq::Table& table);
    /** @brief Bind configure/run to a registered builder class; composition thread only. */
    static void expose(ssq::Class& cls);
    /** @brief Validate and copy a problem atomically; a failed configuration preserves the previous one. */
    [[nodiscard]] Result<void> configure(const Problem& problem);
    /**
     * @brief Search on training seeds, then assess initial and best parameters on untouched holdout seeds.
     * @return Owning proposal or a validation/callback failure; no partial report is published.
     * @cost Up to candidates times training seed count plus twice holdout seed count domain rollouts;
     * use offline or at a loading boundary, never once per frame. Maximum 65536 rollouts per run.
     */
    [[nodiscard]] Result<Report> run(IEvaluator& evaluator);

private:
    std::optional<Problem> problem_;
    bool                   active_ = false;
};

}  // namespace eve::agent::balance
