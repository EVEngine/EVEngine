#pragma once

/**
 * @file TurnPolicy.h
 * @brief Pluggable deterministic turn-scheduling policies.
 *
 * The battle stores only a **stable string id**; the behaviour behind that id is
 * resolved from a registry. That split is what keeps persistent data readable by a
 * build that does not link a given project policy: an unknown id is a structured
 * failure at resolve time, never a silently different schedule.
 */

#include "common/Result.h"
#include "tactics/TacticsTypes.h"

#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace eve::tactics {

// The built-in policy ids live in TacticsTypes.h, because they are part of the
// persisted protocol (snapshots and replay commands carry these exact strings).

/**
 * @brief The scheduling projection a policy is allowed to compare.
 *
 * This carries **values, not handles**, on purpose: a policy cannot resolve a
 * unit, mutate it, or depend on a component the caller did not offer, so a policy
 * cannot quietly become a second authority over battle state. It also means the
 * projection has no lifetime relationship with the battle.
 */
struct UnitOrder {
    /** @brief Per-turn initiative declared by the unit's turn resources. */
    int initiative = 0;
    /** @brief Index of the unit's side inside the battle's side list. */
    std::size_t sideIndex = 0;
};

/**
 * @brief The total order a policy reports for one pair of units.
 *
 * This is a named three-valued result rather than a bool on purpose: "equivalent"
 * is a real outcome with real meaning (the caller then applies the canonical
 * subject tie-break), and a bool cannot express it without an out-of-band
 * convention that every reader has to know.
 */
enum class TurnOrder : std::uint8_t {
    /** @brief The left unit acts first. */
    LeftFirst,
    /** @brief The right unit acts first. */
    RightFirst,
    /** @brief Both compare equal; the caller decides by canonical order. */
    Equivalent,
};

/**
 * @brief One deterministic unit-activation ordering strategy.
 *
 * A policy answers exactly one question — which of two units acts first inside a
 * round — so policies cannot reach into resource resets, events or objectives.
 * The caller owns the canonical-subject tie-break, which means every policy is
 * automatically deterministic on equal keys and cannot restate that rule.
 *
 * Implementations are stateless value-like objects shared across battles. They
 * must be pure: no clock reads, no random draws, no mutation of the battle.
 */
class ITurnPolicy {
public:
    virtual ~ITurnPolicy() = default;

    /** @brief Return the stable protocol id this policy is registered under. */
    [[nodiscard]] virtual std::string_view id() const noexcept = 0;

    /**
     * @brief Report the order of two units in the current round.
     * @param battle Read-only battle, for policies that depend on round or phase.
     * @return Which unit acts first, or `Equivalent` when the policy has no
     *         opinion and the caller's canonical tie-break must decide.
     * @remarks Must be antisymmetric and transitive over the three outcomes, so
     *          the caller can sort with it: `order(a,b)` and `order(b,a)` must not
     *          both report a winner, and `Equivalent` must be consistent.
     */
    [[nodiscard]] virtual TurnOrder order(const Battle& battle, const UnitOrder& left,
                                          const UnitOrder& right) const = 0;
};

/**
 * @brief Registry of turn policies keyed by stable id.
 *
 * The registry never owns battle state and is safe to share: policies are
 * immutable and lookups are read-only. Callers must not register while another
 * thread resolves.
 */
class TurnPolicyRegistry final {
public:
    /**
     * @brief Register one policy under its own id.
     * @return Applied, InvalidArgument for a null or empty-id policy, or Conflict
     *         when the id is already taken.
     */
    [[nodiscard]] Result<void> add(std::shared_ptr<const ITurnPolicy> policy);
    /** @brief Resolve a registered policy by id, or NotFound. */
    [[nodiscard]] Result<const ITurnPolicy*> find(std::string_view id) const;
    /** @brief Return registered ids in lexical order. */
    [[nodiscard]] std::vector<std::string> ids() const;
    /** @brief Return whether an id is registered. */
    [[nodiscard]] bool contains(std::string_view id) const;

    /**
     * @brief Return the process-wide registry carrying the built-in policies.
     * @remarks Built-ins are registered on first use and never replaced, so the
     *          returned registry is effectively immutable to callers.
     */
    [[nodiscard]] static TurnPolicyRegistry& builtins();

private:
    std::map<std::string, std::shared_ptr<const ITurnPolicy>, std::less<>> policies_;
};

}  // namespace eve::tactics
