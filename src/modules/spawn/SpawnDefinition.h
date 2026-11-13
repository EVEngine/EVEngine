#pragma once

#include "common/Export.h"
#include "common/ResourceRef.h"
#include "common/Time.h"
#include "common/Value.h"

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace eve::spawn {

/** @brief One fixed member or weighted choice; count and weight are never runtime population. */
struct Member {
    std::string   id;
    DefinitionRef unit;
    std::uint32_t count  = 1;
    std::uint32_t weight = 1;
};
/** @brief Immutable-content fixed group. */
struct FixedContent {
    std::vector<Member> members;
};
/** @brief Independent per-unit weighted draws, not an elite-count constraint. */
struct WeightedContent {
    std::vector<Member> entries;
};
/** @brief Select content at compilation; waves own their own fixed members. */
using Content = std::variant<FixedContent, WeightedContent>;
/** @brief Publish one fixed batch exactly once. */
struct OnceSchedule {};
/** @brief Periodic weighted batch; missed occurrences are skipped. */
struct IntervalSchedule {
    Duration      period;
    Duration      initialDelay;
    std::uint32_t count = 1;
};
/** @brief Death-only replacement with injected random cooldown. */
struct PopulationSchedule {
    std::uint32_t target  = 1;
    std::uint32_t initial = 1;
    Duration      minimumDelay;
    Duration      maximumDelay;
};
/** @brief A fixed group whose completion requires individual death facts. */
struct Wave {
    std::string         id;
    Duration            delay;
    std::vector<Member> members;
};
/** @brief Ordered finite encounter waves. */
struct WaveSchedule {
    std::vector<Wave> waves;
};
/** @brief Closed serializable scheduling policy. */
using Schedule = std::variant<OnceSchedule, IntervalSchedule, PopulationSchedule, WaveSchedule>;
/** @brief Spawn-owned count limits; domain population remains domain-owned. */
struct Limits {
    std::uint32_t                alive = 1;
    std::optional<std::uint64_t> total;
    std::string                  budgetGroup;
};
/** @brief Required placement constraints; no missing-provider fallback. */
enum class ConstraintKind { Walkable, Clearance, DistanceFromPlayers };
/** @brief Typed placement predicate; distance is radius or minimum player distance. */
struct PlacementConstraint {
    ConstraintKind kind     = ConstraintKind::Walkable;
    double         distance = 0;
};
/** @brief Bounded candidate search, independent from final atomic validation. */
struct PlacementSpec {
    std::uint32_t                    attempts = 12;
    std::vector<PlacementConstraint> constraints;
};
/** @brief Owning typed template input; compiled revisions expose this read-only. */
struct TemplateDefinition {
    std::string   id;
    Content       content;
    Schedule      schedule;
    Limits        limits;
    PlacementSpec placement;
    Duration      retryDelay;
    Value         extensions = Value::Object{};
};

/**
 * @brief Domain-owned synchronous reference validator used only during compilation.
 * @ownership Borrowed for compileTemplate; never retained.
 * @thread Calling content thread; must not reenter compiler or mutate source definitions.
 */
class IUnitDefinitionResolver {
public:
    /** @brief Release the resolver interface. */
    virtual ~IUnitDefinitionResolver() = default;
    /** @brief Validate a borrowed reference without creating a unit; failure supplies its reason. */
    [[nodiscard]] virtual Result<void> validate(const DefinitionRef& reference) const = 0;
};

class EVENGINE_API_FOUNDATION CompiledTemplate;

/**
 * @brief Validate a complete template, resolve every unit and produce an owning revision.
 * @param source Borrowed untrusted data, never retained or changed.
 * @param resolver Borrowed synchronous domain validator, never retained.
 * @return Validated revision or diagnostic with source field path; no state is published on failure.
 * @cost Linear in asset fields and members plus provider validation, amortized once per revision.
 * @thread Content owner thread; invokes resolver without locks; compiler is reentrant.
 */
[[nodiscard]] EVENGINE_API_FOUNDATION Result<CompiledTemplate> compileTemplate(const Value&                   source,
                                                                               const IUnitDefinitionResolver& resolver);

/**
 * @brief Validated owning immutable revision; cannot be constructed from unchecked data.
 * @remarks Source values and resolver may be destroyed after compilation.
 */
class EVENGINE_API_FOUNDATION CompiledTemplate {
public:
    /** @brief Borrow typed data until this revision is moved, assigned or destroyed; no callbacks. */
    [[nodiscard]] const TemplateDefinition& definition() const noexcept { return definition_; }
    /** @brief Borrow canonical source JSON for this revision's lifetime; includes extensions. */
    [[nodiscard]] const std::string& canonicalJson() const noexcept { return canonicalJson_; }

private:
    friend EVENGINE_API_FOUNDATION Result<CompiledTemplate> compileTemplate(const Value&,
                                                                            const IUnitDefinitionResolver&);
    CompiledTemplate(TemplateDefinition definition, std::string canonicalJson)
        : definition_(std::move(definition)), canonicalJson_(std::move(canonicalJson)) {}
    TemplateDefinition definition_;
    std::string        canonicalJson_;
};

}  // namespace eve::spawn
