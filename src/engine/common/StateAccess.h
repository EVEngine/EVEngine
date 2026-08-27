#pragma once

/**
 * @file StateAccess.h
 * @brief Consumer-owned query and mutation contracts for authoritative world state.
 */

#include "common/Export.h"
#include "common/Result.h"
#include "common/Value.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace eve::state {

/** @brief Stable textual identity used at a cross-module state boundary. */
using SubjectId = std::string;

/** @brief Kind of one requested world-state mutation. */
enum class MutationKind : std::uint8_t {
    Set,
    Remove,
    AddTag,
    RemoveTag,
    AddNumber,
};

/**
 * @brief One immutable mutation request owned by its caller.
 *
 * `subject` is the authoritative world's stable object id, not a pointer or
 * an ECS row. `key` is domain-defined; tags use their tag name and numeric
 * values use an attribute/reputation name. A persistent request must be
 * accepted by a StatePatch-backed provider; volatile providers reject it.
 */
struct StateMutation {
    SubjectId subject;
    std::string key;
    eve::Value value;
    MutationKind kind = MutationKind::Set;
    bool persistent = false;
};

/**
 * @brief Correlation metadata shared by a mutation transaction.
 *
 * Persistent mutations require a non-empty `transactionId`. The strings are
 * copied by transaction participants and are valid only for the synchronous
 * call that receives this context.
 */
struct MutationContext {
    std::string transactionId;
    std::string correlationId;
    std::string causationId;
};

/** @brief Observable result of an all-or-nothing mutation request. */
struct MutationReceipt {
    std::string transactionId;
    std::size_t changedCount = 0;
};

/**
 * @brief Read-only world-state boundary owned by consumers such as Dialogue.
 *
 * Implementations are synchronous and must not retain query arguments or
 * invoke unknown callbacks while holding a lock. `nullopt` means that this
 * provider does not own the requested domain/key. A returned `false` is a
 * known negative answer. The provider is borrowed by the caller and must
 * outlive the query context; it is not retained by the capability registry
 * after `revoke`.
 */
class EVENGINE_API IStateQuery {
public:
    static constexpr const char* capabilityName = "eve.state.IStateQuery";
    virtual ~IStateQuery() = default;

    /** @brief Query a generic world value. */
    [[nodiscard]] virtual std::optional<eve::Value> value(std::string_view subject,
                                                            std::string_view key) const = 0;
    /** @brief Query exact tag membership. */
    [[nodiscard]] virtual std::optional<bool> hasTag(std::string_view subject,
                                                      std::string_view tag) const = 0;
    /** @brief Query a numeric or structured attribute. */
    [[nodiscard]] virtual std::optional<eve::Value> attribute(std::string_view subject,
                                                               std::string_view key) const = 0;
    /** @brief Query a resource value or marker. */
    [[nodiscard]] virtual std::optional<eve::Value> resource(std::string_view subject,
                                                              std::string_view key) const = 0;
    /** @brief Query a persistent/world state value. */
    [[nodiscard]] virtual std::optional<eve::Value> state(std::string_view subject,
                                                           std::string_view key) const = 0;
    /** @brief Query read-only authority for a scope. */
    [[nodiscard]] virtual std::optional<bool> authority(std::string_view subject,
                                                         std::string_view scope) const = 0;
};

/**
 * @brief All-or-nothing mutation boundary owned by the consumer.
 *
 * Implementations must validate the complete span before publishing any
 * change. The call is synchronous, runs on the provider's documented thread,
 * and must not retain the span or values after returning. A failed Result
 * promises that no observable world state was changed.
 */
class EVENGINE_API IStateMutation {
public:
    static constexpr const char* capabilityName = "eve.state.IStateMutation";
    virtual ~IStateMutation() = default;

    /** @brief Apply a complete mutation set atomically. */
    [[nodiscard]] virtual eve::Result<MutationReceipt> apply(
        std::span<const StateMutation> mutations, const MutationContext& context) = 0;
};

}  // namespace eve::state

// Root aliases keep the common capability discoverable alongside existing
// interfaces such as eve::ISceneQuery while retaining the state namespace as
// the canonical spelling for new code.
namespace eve {
using IStateQuery    = state::IStateQuery;
using IStateMutation = state::IStateMutation;
using StateMutation  = state::StateMutation;
using MutationKind   = state::MutationKind;
using MutationContext = state::MutationContext;
using MutationReceipt = state::MutationReceipt;
}  // namespace eve
