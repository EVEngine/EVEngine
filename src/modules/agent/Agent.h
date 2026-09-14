#pragma once

#include "common/Result.h"

#include <cstdint>
#include <string>
#include <vector>

namespace eve::agent {

/** @brief Domain outcome, independent of reward and infrastructure errors. */
enum class Outcome { Running, Success, Failure };

/** @brief Owning state projection; action IDs index a fixed domain action catalogue. */
struct Observation {
    std::vector<float>         features;
    std::vector<std::uint32_t> legalActions;
    std::vector<std::string>   coverage;
    double                     reward  = 0;
    Outcome                    outcome = Outcome::Running;
    std::string                finding;
};

/**
 * @brief Adapter for a resettable game, simulation, UI or other decision-making environment.
 * @ownership The caller owns the environment and all domain state. The runner
 * borrows it only for the synchronous call; no references escape.
 * @thread Call on the domain owner thread. Callbacks may call unrelated services,
 * but must not reenter or destroy this environment. The runner holds no locks.
 * @remarks reset must reset all relevant state and RNG, including pending events.
 * A failed reset/step returns Result failure, never a synthetic bug or reward.
 * Every search resets and mutates this instance; do not supply a user's
 * unsaved live session. Async hosts implement this adapter in their own driver.
 */
class IEnvironment {
public:
    virtual ~IEnvironment() = default;
    /** @brief Reset atomically to the injected seed; return initial state (reward zero). */
    [[nodiscard]] virtual Result<Observation> reset(std::uint64_t seed) = 0;
    /** @brief Execute one legal action, advance exactly dt seconds, then inspect invariants. */
    [[nodiscard]] virtual Result<Observation> step(std::uint32_t action, double dt) = 0;
};

/** @brief Explicit algorithm choice, also used for equal-budget random baselines. */
enum class Strategy { Random, EvolutionLearning };

/** @brief Explicit backend; Tensor is eager CPU, Gpu accelerates inference and SGD via agent_tensor. */
enum class Backend { Cpu, Tensor, Gpu };

/** @brief Bounded search configuration; seed streams for environment, search and learning are separate. */
struct Config {
    std::uint32_t featureCount        = 1;
    std::uint32_t actionCount         = 2;
    std::uint32_t population          = 24;
    std::uint32_t generations         = 8;
    std::uint32_t horizon             = 32;
    std::uint32_t elites              = 6;
    std::uint32_t hiddenWidth         = 16;
    std::uint32_t trainingEpochs      = 3;
    std::uint32_t maxFindings         = 16;
    std::uint64_t environmentSeed     = 1;
    std::uint64_t searchSeed          = 2;
    std::uint64_t learningSeed        = 3;
    double        dt                  = 1.0 / 60.0;
    double        mutationProbability = 0.2;
    double        randomProbability   = 0.2;
    double        coverageWeight      = 0;
    double        failureWeight       = 0;
    double        learningRate        = 0.01;
    Strategy      strategy            = Strategy::EvolutionLearning;
    Backend       backend             = Backend::Cpu;
};

/** @brief Owning executed action and resulting observation; no domain pointers are retained. */
struct TraceStep {
    std::uint32_t action = 0;
    Observation   observation;
};

/**
 * @brief In-memory versioned replay evidence, independent of learned model state.
 * @remarks Version 1 is the initial format. Replay rejects unknown schema/version;
 * the canonical Value/JSON codec rejects unknown fields. Adapters retain their
 * domain version separately and must reject incompatible domain builds. No older
 * released schema exists; future migrations must be explicit.
 */
struct Trace {
    std::string            schemaId        = "evengine.agent.trace";
    std::uint32_t          schemaVersion   = 1;
    std::uint64_t          environmentSeed = 0;
    double                 dt              = 0;
    Observation            initial;
    std::vector<TraceStep> steps;
};

/** @brief Owning version-1 portable network weights; import validates the entire value before use. */
struct Policy {
    std::string         schemaId      = "evengine.agent.policy";
    std::uint32_t       schemaVersion = 1;
    std::uint32_t       featureCount  = 0;
    std::uint32_t       actionCount   = 0;
    std::uint32_t       hiddenWidth   = 0;
    std::vector<double> weights;
};

/**
 * @brief Optional inference service. Providers own their registration and revoke before destruction.
 * @thread Owner thread only; no reentry, provider unload or external callbacks during inference.
 * @ownership Inputs are borrowed only for the call. Outputs own all data. Core resolves the
 * capability anew for each inference; it never retains a provider across environment callbacks.
 */
class IPolicyBackend {
public:
    static constexpr const char* capabilityName = "agent.IPolicyBackend";
    virtual ~IPolicyBackend()                   = default;
    /** @brief Owning device/backend label; must describe the actual execution path. */
    [[nodiscard]] virtual std::string name() const = 0;
    /** @brief Evaluate inputs validated by agent::infer; return masked probabilities or structured failure. */
    [[nodiscard]] virtual Result<std::vector<double>> evaluate(const Policy&      policy,
                                                               const Observation& observation) = 0;
};

/**
 * @brief Optional GPU policy service, registered independently of eager tensor CPU.
 * @ownership Same synchronous borrowing contract as IPolicyBackend; revoke before destruction.
 * @thread Device owner thread only. No callbacks, reentry or device teardown during a call.
 */
class IGpuPolicyBackend : public IPolicyBackend {
public:
    static constexpr const char* capabilityName = "agent.IGpuPolicyBackend";
    /** @brief Check live device availability without executing an environment callback. */
    [[nodiscard]] virtual Result<void> available() const = 0;
    /** @brief GPU forward/backprop/update on a validated sample; publish owning weights atomically. */
    [[nodiscard]] virtual Result<Policy> train(const Policy& policy, const Observation& observation,
                                               std::uint32_t action, double rate) = 0;
};

/** @brief Validate the complete owning policy before inference/import, with no mutation or callbacks. */
[[nodiscard]] EVENGINE_API Result<void> validatePolicy(const Policy& policy);

/** @brief Return an owning backend label, or Unsupported if Tensor is unavailable; owner thread only. */
[[nodiscard]] EVENGINE_API Result<std::string> backendName(Backend backend);

/** @brief Owning search result; reward, coverage and failures remain separate evidence. */
struct Report {
    Policy                   policy;
    Trace                    best;
    std::vector<Trace>       findings;
    std::vector<std::string> coverage;
    std::uint64_t            episodes        = 0;
    std::uint64_t            steps           = 0;
    std::uint64_t            failures        = 0;
    std::uint64_t            trainingSamples = 0;
    double                   bestScore       = 0;
    std::string              backend         = "cpu-mlp";
    std::string              trainingBackend = "cpu-sgd";
};

/**
 * @brief Run bounded evolutionary rollout search and elite policy distillation.
 * @param config All budgets, time and RNG streams; invalid/nonfinite inputs are rejected before reset.
 * @param environment Borrowed isolated domain adapter, left at its final evaluated state.
 * @return Owning evidence, or adapter/validation failure. No partial report is published on error.
 * @remarks Two tanh hidden layers and softmax are trained by explicit backpropagation
 * on elite trajectories. This is evolutionary search, not stationary MCMC.
 * Same build/config/deterministic adapter gives repeatable traces. Floating-point
 * cross-platform equivalence uses replay's explicit tolerance. No global RNG,
 * worker, ECS system or persistent domain link is introduced. Tensor inference
 * requires an explicitly registered provider. Gpu executes forward/backprop/SGD
 * on the device, Cpu/Tensor use CPU SGD. GPU FP32 is tolerance-based, not bitwise
 * equivalent to CPU; stochastic action choices can amplify small numeric differences.
 */
[[nodiscard]] EVENGINE_API Result<Report> run(const Config& config, IEnvironment& environment);

/**
 * @brief Compute masked action probabilities with validated version-1 owning weights.
 * @param policy Borrowed only during this call; unknown versions/shapes/NaNs are rejected.
 * @param observation Borrowed state with nonempty legal mask and normalized finite features.
 * @return Owning probabilities indexed by action ID (illegal actions have zero probability).
 * @param backend CPU, eager Tensor or GPU; missing provider/device returns Unsupported.
 * @remarks No retained references; CPU is pure, Tensor calls are owner-thread affine.
 */
[[nodiscard]] EVENGINE_API Result<std::vector<double>> infer(const Policy& policy, const Observation& observation,
                                                             Backend backend = Backend::Cpu);

/**
 * @brief Reset and replay actual actions, checking all observations and failure evidence.
 * @param trace Version-1 owning evidence from run; independent of training.
 * @param environment Borrowed isolated adapter, mutated synchronously on its owner thread.
 * @param absoluteTolerance Finite nonnegative feature/reward tolerance; masks, coverage,
 * outcomes and findings must match exactly. Invalid trace is rejected before reset.
 * @return Conflict for divergence, or the original adapter failure; no locks/callback retention.
 */
[[nodiscard]] EVENGINE_API Result<void> replay(const Trace& trace, IEnvironment& environment,
                                               double absoluteTolerance = 1e-6);

}  // namespace eve::agent
