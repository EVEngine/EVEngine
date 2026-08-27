#pragma once

/**
 * @file Condition.h
 * @brief Pure, explainable condition trees shared by gameplay domains.
 */

#include "common/Value.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace eve::decision {

/** @brief Node kind in the side-effect-free condition AST. */
enum class ConditionKind : std::uint8_t {
    All,
    Any,
    Not,
    Compare,
    HasTag,
    HasAttribute,
    HasResource,
    StateEquals,
    AuthorityCheck,
    PolicyCall,
};

/** @brief Scalar comparison used by a Compare node. */
enum class CompareOperator : std::uint8_t {
    Equal,
    NotEqual,
    Less,
    LessEqual,
    Greater,
    GreaterEqual,
};

/** @brief Determinism contract declared by a script-backed condition policy. */
enum class DeterminismLevel : std::uint8_t {
    /** @brief Same inputs produce the same bits without an external clock. */
    BitExact,
    /** @brief Deterministic when evaluated at the injected simulation tick. */
    TickDeterministic,
    /** @brief Results may vary within a documented numeric tolerance. */
    ToleranceBounded,
    /** @brief Result is intentionally not suitable for replay or lockstep. */
    ExplicitlyNondeterministic,
};

/**
 * @brief Metadata a script policy must declare before it is used by a condition.
 *
 * Dependencies are logical read names, not pointers to engine objects. The
 * declaration is descriptive; the EvaluationContext remains the only way to
 * read state, and no condition may mutate it.
 */
struct ScriptConditionDeclaration {
    /** @brief Stable script policy name. */
    std::string name;
    /** @brief Logical state/query names read by the policy. */
    std::vector<std::string> dependencies;
    /** @brief Replay/network determinism contract. */
    DeterminismLevel determinism = DeterminismLevel::BitExact;
};

/** @brief Stable machine-readable explanation code for a condition outcome. */
enum class ConditionReasonCode : std::uint32_t {
    Passed = 0,
    ChildFailed = 1,
    NoChildPassed = 2,
    Negated = 3,
    MissingValue = 4,
    ValueMismatch = 5,
    TagMissing = 6,
    TagUnavailable = 7,
    AttributeMissing = 8,
    ResourceMissing = 9,
    StateMissing = 10,
    StateMismatch = 11,
    AuthorityDenied = 12,
    AuthorityUnavailable = 13,
    PolicyRejected = 14,
    PolicyUnavailable = 15,
    InvalidCondition = 16,
};

/**
 * @brief Return the stable lowercase spelling of a condition reason code.
 * @ownership Borrowed process-static text; callers must not free or modify it.
 * @nullable Never null; unknown numeric values use the stable fallback literal.
 * @lifetime Process lifetime.
 * @thread Thread-safe and reentrant; no mutable state or callbacks are used.
 */
[[nodiscard]] const char* conditionReasonCodeName(ConditionReasonCode code) noexcept;

/**
 * @brief Return the stable lowercase spelling of a condition node kind.
 * @ownership Borrowed process-static text; callers must not free or modify it.
 * @nullable Never null; unknown numeric values use the stable fallback literal.
 * @lifetime Process lifetime.
 * @thread Thread-safe and reentrant; no mutable state or callbacks are used.
 */
[[nodiscard]] const char* conditionKindName(ConditionKind kind) noexcept;

/**
 * @brief Return the stable lowercase spelling of a comparison operator.
 * @ownership Borrowed process-static text; callers must not free or modify it.
 * @nullable Never null; unknown numeric values use the stable fallback literal.
 * @lifetime Process lifetime.
 * @thread Thread-safe and reentrant; no mutable state or callbacks are used.
 */
[[nodiscard]] const char* compareOperatorName(CompareOperator op) noexcept;

/**
 * @brief Explain one condition evaluation in a UI- and log-friendly form.
 *
 * `evidence` is the value observed at the leaf or returned by a policy.
 * `details` contains stable field names and child explanations. Both values
 * own their storage and remain valid after the evaluation context is gone.
 */
class [[nodiscard("ConditionResult must be inspected before the decision is used")]] ConditionResult {
public:
    /** @brief Construct a passed result with optional evidence and details. */
    static ConditionResult success(Value evidence = {}, Value details = Value::Object{});
    /** @brief Construct a rejected result with a stable reason and explanation. */
    static ConditionResult failed(ConditionReasonCode reason, Value evidence = {},
                                  Value details = Value::Object{});

    /** @brief Whether the condition passed. */
    bool passed() const noexcept { return passed_; }
    /** @brief Stable machine-readable reason code. */
    ConditionReasonCode reasonCode() const noexcept { return reason_; }
    /** @brief Observed value or child evidence owned by this result. */
    const Value& evidence() const noexcept { return evidence_; }
    /** @brief Structured details suitable for UI explanation. */
    const Value& details() const noexcept { return details_; }

private:
    ConditionResult(bool passed, ConditionReasonCode reason, Value evidence, Value details)
        : passed_(passed), reason_(reason), evidence_(std::move(evidence)), details_(std::move(details)) {}

    bool                 passed_ = false;
    ConditionReasonCode  reason_ = ConditionReasonCode::InvalidCondition;
    Value                evidence_;
    Value                details_;
};

/**
 * @brief Read-only state boundary used by every condition evaluation.
 *
 * Implementations must not mutate domain state. Methods are synchronous and
 * called on the caller's thread; implementations must not retain the caller's
 * query keys or invoke unknown callbacks while holding a lock. `nullopt`
 * means the requested capability/value is unavailable, which is distinct from
 * a known false value.
 */
class EvaluationContext {
public:
    virtual ~EvaluationContext() = default;

    /** @brief Query a named scalar or structured value for Compare nodes. */
    [[nodiscard]] virtual std::optional<Value> value(std::string_view key) const = 0;
    /** @brief Query tag membership; nullopt means the tag source is unavailable. */
    [[nodiscard]] virtual std::optional<bool> hasTag(std::string_view tag) const = 0;
    /** @brief Query an attribute value; nullopt means the attribute is absent. */
    [[nodiscard]] virtual std::optional<Value> attribute(std::string_view key) const = 0;
    /** @brief Query a resource amount or resource marker. */
    [[nodiscard]] virtual std::optional<Value> resource(std::string_view key) const = 0;
    /** @brief Query a state value for StateEquals nodes. */
    [[nodiscard]] virtual std::optional<Value> state(std::string_view key) const = 0;
    /** @brief Query authority without changing grants, leases, or state. */
    [[nodiscard]] virtual std::optional<bool> authority(std::string_view scope) const = 0;
    /** @brief Evaluate a named read-only policy with owned arguments. */
    [[nodiscard]] virtual std::optional<ConditionResult> policy(std::string_view name,
                                                                  const Value& arguments) const = 0;
};

/**
 * @brief Immutable condition AST node.
 *
 * Construct nodes through the named factories. A node owns all child nodes,
 * keys and values; it keeps no pointers into a domain object. Evaluation is
 * synchronous, const, and side-effect free.
 */
class Condition {
public:
    /** @brief Construct an empty All node, which passes vacuously. */
    Condition() = default;

    /** @brief Construct a conjunction of child conditions. */
    static Condition all(std::vector<Condition> children);
    /** @brief Construct a disjunction of child conditions. */
    static Condition any(std::vector<Condition> children);
    /** @brief Construct a logical negation of one child condition. */
    static Condition not_(Condition child);
    /** @brief Compare a context value against an expected scalar value. */
    static Condition compare(std::string key, CompareOperator op, Value expected);
    /** @brief Require a tag to be present. */
    static Condition hasTag(std::string tag);
    /** @brief Require an attribute to exist. */
    static Condition hasAttribute(std::string attribute);
    /** @brief Require a resource to exist. */
    static Condition hasResource(std::string resource);
    /** @brief Require a named state value to equal the expected value. */
    static Condition stateEquals(std::string key, Value expected);
    /** @brief Require read-only authority for a scope. */
    static Condition authorityCheck(std::string scope);
    /**
     * @brief Call a named read-only policy and use its ConditionResult.
     * @param name Stable policy/script name.
     * @param arguments Owning policy arguments; defaults to an empty object.
     * @param scriptDeclaration Optional dependency/determinism declaration for
     *        a script-backed policy. Its name must match `name` when supplied.
     */
    static Condition policyCall(std::string name, Value arguments = Value::Object{},
                                std::optional<ScriptConditionDeclaration> scriptDeclaration = std::nullopt);

    /** @brief Return this node's kind. */
    ConditionKind kind() const noexcept { return kind_; }
    /** @brief Return owned children; empty for a leaf node. */
    const std::vector<Condition>& children() const noexcept { return children_; }
    /** @brief Return the key/tag/scope/name used by a leaf, if applicable. */
    const std::string& key() const noexcept { return key_; }
    /** @brief Return the comparison operator used by Compare. */
    CompareOperator compareOperator() const noexcept { return compare_; }
    /** @brief Return the expected value owned by Compare/StateEquals. */
    const Value& expected() const noexcept { return expected_; }
    /** @brief Return owned PolicyCall arguments. */
    const Value& arguments() const noexcept { return arguments_; }
    /** @brief Return an optional script dependency/determinism declaration. */
    const std::optional<ScriptConditionDeclaration>& scriptDeclaration() const noexcept {
        return scriptDeclaration_;
    }
    /** @brief Return whether the factory invariants for this node hold. */
    [[nodiscard]] bool isValid() const noexcept;
    /** @brief Evaluate this tree against a read-only context. */
    [[nodiscard]] ConditionResult evaluate(const EvaluationContext& context) const;

private:
    explicit Condition(ConditionKind kind) : kind_(kind) {}

    ConditionKind                              kind_ = ConditionKind::All;
    std::vector<Condition>                     children_;
    std::string                                key_;
    CompareOperator                            compare_ = CompareOperator::Equal;
    Value                                       expected_;
    Value                                       arguments_ = Value::Object{};
    std::optional<ScriptConditionDeclaration>  scriptDeclaration_;
};

}  // namespace eve::decision
