#pragma once

/**
 * @file SettlementRules.h
 * @brief Declarative, domain-neutral rules for buffs and gameplay effects.
 */

#include "settlement/Settlement.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace eve::effects {
class EffectContainer;
}

namespace eve::settlement {

/** @brief Supported deterministic mutations of a settlement calculation. */
enum class RuleOperation : std::uint8_t {
    Add,
    Multiply,
    ResistFlat,
    ResistPercent,
    AbsorbFlat,
    ClampMaximum,
    Critical,
    Immune,
    Lifesteal,
    Reflect,
};

/**
 * @brief Predicate selecting requests to which a rule applies.
 *
 * Empty `kinds` and tag lists are wildcards. All required tags must be present
 * and no excluded tag may be present. Matching is exact and case-sensitive.
 */
struct RuleFilter {
    std::vector<std::string> kinds;
    std::vector<std::string> requiredTags;
    std::vector<std::string> excludedTags;
};

/**
 * @brief One owning, serializable projection of a buff/effect contribution.
 *
 * `value + valuePerExtraStack * (stacks - 1)` is the effective operand. The
 * source effect remains authoritative for lifecycle and stacking; this value
 * is a per-settlement snapshot and never retains an EffectInstance pointer.
 */
struct SettlementRule {
    std::string   id;
    std::string   source;
    StageKind     stage              = StageKind::SourceModifiers;
    int           priority           = 0;
    RuleOperation operation          = RuleOperation::Add;
    double        value              = 0.0;
    double        valuePerExtraStack = 0.0;
    std::uint32_t stacks             = 1;
    RuleFilter    filter;
    /** @brief Optional side-effect-free expression compiled by configure(). */
    std::string when;
};

/**
 * @brief Validated immutable-style rule collection installable into pipelines.
 *
 * Configuration is transactional: a failed replacement leaves the previous
 * rules unchanged. Installing copies the current rules into pipeline-owned
 * stage callbacks, so the rule set may be destroyed immediately afterwards.
 * The collection is simulation-thread confined; installed callbacks are
 * synchronous and do not retain request or policy references.
 */
class SettlementRuleSet {
public:
    /** @brief Stable schema version used by canonical rule-set identity. */
    [[nodiscard]] static constexpr std::uint32_t schemaVersion() noexcept { return 1; }

    /** @brief Atomically replace all rules after validating ids, operands and phases. */
    [[nodiscard]] eve::Result<void> configure(std::vector<SettlementRule> rules);

    /**
     * @brief Decode and compile a strict versioned JSON rule document without mutating an existing rule set.
     * @param json Canonical `settlement.rules` document shared by runtime and editor/tool validation.
     * @return A validated owning rule set, or the same located diagnostic used by configureJson().
     * @cost Linear JSON decode plus condition compilation for every rule.
     */
    [[nodiscard]] static eve::Result<SettlementRuleSet> fromJson(std::string_view json);

    /**
     * @brief Atomically decode and replace a strict versioned JSON rule document.
     * @param json Canonical `settlement.rules` document; unknown fields and versions are rejected.
     * @return Applied, or a located parse/schema/rule-validation diagnostic while preserving old rules.
     * @cost Linear JSON decode plus condition compilation for every rule.
     */
    [[nodiscard]] eve::Result<void> configureJson(std::string_view json);

    /**
     * @brief Install a stable snapshot as custom stages on a pipeline.
     * @param pipeline Pipeline mutated on the caller's simulation thread.
     * @return Applied, or a duplicate-stage/configuration diagnostic.
     * @cost Linear in rule count and copies the validated configuration once.
     */
    [[nodiscard]] eve::Result<void> install(SettlementPipeline& pipeline) const;

    /** @brief Return the number of validated rules in this collection. */
    [[nodiscard]] std::size_t size() const noexcept { return rules_.size(); }

    /**
     * @brief Serialize validated rules in their semantic execution order.
     * @return Versioned canonical JSON, or a structured serialization failure.
     * @cost O(n log n) in rule count due to semantic ordering and filter normalization.
     */
    [[nodiscard]] eve::Result<std::string> canonicalJson() const;

    /**
     * @brief Hash the canonical validated rule-set representation.
     * @param hashProvider Explicit provider shared with snapshot and settlement result hashing.
     * @return Stable content identity, or a structured serialization/provider failure.
     * @cost Canonical serialization plus the injected provider's hashing cost.
     */
    [[nodiscard]] eve::Result<eve::ContentId> digest(const eve::SnapshotHashProvider& hashProvider) const;

private:
    using ConditionFunction = std::function<eve::Result<bool>(const SettlementRequest&)>;

    std::vector<SettlementRule>    rules_;
    std::vector<ConditionFunction> conditions_;
    std::vector<std::string>       conditionDigests_;
};

/**
 * @brief Project active effect payloads into an owning settlement rule snapshot.
 * @param effects Sole lifecycle owner inspected synchronously; no pointers are retained.
 * @param scope Non-empty stable prefix distinguishing source, target, or domain containers.
 * @return Validated rules for payloads containing `settlement.rule`, or a located diagnostic.
 * @thread Call on the effect container's owning simulation thread.
 * @reentrancy No callbacks are invoked.
 * @cost Linear in active effect count and projected condition source length.
 */
[[nodiscard]] eve::Result<SettlementRuleSet> projectEffectRules(const effects::EffectContainer& effects,
                                                                std::string_view                scope);

}  // namespace eve::settlement
