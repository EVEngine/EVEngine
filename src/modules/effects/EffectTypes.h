#pragma once

/**
 * @file EffectTypes.h
 * @brief Definition, policy, instance and lifecycle value types for effects.
 *
 * These types deliberately contain no RPG settlement or attribute logic.  An
 * effect is a lifecycle fact; a domain executor may interpret its payload,
 * magnitude and tags later.
 */

#include "common/Identity.h"
#include "common/Result.h"
#include "common/Time.h"
#include "common/Value.h"
#include "common/definitions/DefinitionRuntime.h"

#include <cstdint>
#include <ostream>
#include <string>
#include <vector>

namespace eve::effects {

/** @brief Compatibility policy used by the original effects facade. */
enum class StackPolicy { Replace, Stack, Refresh };

/** @brief Determines whether a matching application replaces, reuses, or creates an instance. */
enum class StackMode { Replace, NewInstance, Reuse, Accumulate };

/** @brief Determines how an application changes an existing stack count. */
enum class StackCountPolicy { Keep, Increment, Set };

/** @brief Determines how an application changes an existing remaining duration. */
enum class DurationPolicy { Keep, Replace, Extend };

/** @brief Determines how an application changes an existing magnitude value. */
enum class MagnitudePolicy { Keep, Replace, Add, Max };

/** @brief Determines what happens when a stack-count limit would be exceeded. */
enum class OverflowPolicy { Reject, Clamp, ReplaceOldest };

/**
 * @brief Independent policy dimensions used when applying an effect definition.
 *
 * `StackMode` selects the instance relationship.  The remaining dimensions
 * are evaluated independently when an existing instance is reused or
 * accumulated.  `maxStacks == 0` means unlimited; otherwise it is the maximum
 * stack count for one instance.
 */
struct EffectPolicy {
    StackMode        stackMode  = StackMode::Replace;
    StackCountPolicy stackCount = StackCountPolicy::Keep;
    DurationPolicy   duration   = DurationPolicy::Replace;
    MagnitudePolicy  magnitude  = MagnitudePolicy::Replace;
    OverflowPolicy   overflow   = OverflowPolicy::Reject;
    std::uint32_t    maxStacks  = 1;
};

/** @brief Deterministic JSON-compatible payload carried by a definition or instance. */
class EffectPayload {
public:
    /** @brief Sets a JSON string field. */
    void setString(const std::string& key, const std::string& value);
    /** @brief Sets a JSON number field. */
    void setNumber(const std::string& key, double value);
    /** @brief Sets a JSON boolean field. */
    void setBool(const std::string& key, bool value);
    /** @brief Sets a JSON null field. */
    void setNull(const std::string& key);
    /** @brief Parses JSON text and stores its typed Value at the field. */
    [[nodiscard]] eve::Result<void> setJson(const std::string& key, const std::string& json);
    /** @brief Returns whether a field exists. */
    bool has(const std::string& key) const;
    /** @brief Removes a field; an absent field is an explicit NoOp result. */
    [[nodiscard]] eve::Result<void> erase(const std::string& key);
    /** @brief Serializes a field's typed Value to JSON text, or returns empty when absent/invalid. */
    std::string getJson(const std::string& key) const;
    /** @brief Serializes all fields as a deterministic JSON object. */
    std::string toJson() const;
    /** @brief Returns the read-only canonical Value object owned by this payload. */
    const eve::Value::Object& object() const noexcept;
    /** @brief Removes every field. */
    void clear();

private:
    eve::Value::Object values_;
};

/**
 * @brief Immutable-by-convention template used to create effect instances.
 *
 * The container copies the definition data it needs at apply time.  It never
 * retains the caller's reference, so a definition may be stack-owned or
 * reloaded after the call.  The payload is descriptive data only; applying a
 * definition does not mutate gameplay attributes.
 */
struct EffectDefinition {
    std::string id;
    std::string stackKey;
    int         priority = 0;
    double      duration = 0.0;
    /** @brief Period in seconds for domain executor settlement; zero disables periodic ticks. */
    double                   period     = 0.0;
    double                   magnitude  = 0.0;
    std::uint32_t            stackCount = 1;
    EffectPolicy             policy;
    EffectPayload            payload;
    std::vector<std::string> tags;

    /**
     * @brief Validates definition fields without changing any state.
     * @return Success when the definition can be applied, otherwise a
     *         structured rejection diagnostic.
     */
    [[nodiscard]] eve::Result<void> validate() const;
};

/**
 * @brief One owned runtime effect fact.
 *
 * This object has lifecycle state only.  It does not know how magnitude is
 * settled, how tags affect an actor, or how a period causes damage/healing.
 */
struct EffectInstance {
    std::string id;
    std::string subject;
    std::string type;
    std::string source;
    std::string stackKey;
    int         priority  = 0;
    double      duration  = 0.0;
    double      remaining = -1.0;
    /** @brief Copied periodic interval; the common container owns its accumulator. */
    double period = 0.0;
    /** @brief Elapsed time since the last periodic tick, owned by the instance lifecycle. */
    double                   periodElapsed = 0.0;
    double                   magnitude     = 0.0;
    std::uint32_t            stackCount    = 1;
    EffectPayload            payload;
    std::vector<std::string> tags;
    /** @brief Typed application policy projected from the current definition. */
    EffectPolicy policy;
    /** @brief UUID-backed identity for persistence/event boundaries; nil in the legacy facade. */
    eve::EffectId identity;
    /**
     * @brief Canonical definition link used by the typed definition adapter.
     *
     * The legacy `type` string remains a one-way projection for old callers;
     * new code checks this identity against the current definition generation.
     */
    eve::definition::InstanceIdentity definitionIdentity;
    eve::definition::ReloadPolicy     reloadPolicy = eve::definition::ReloadPolicy::KeepInstanceValues;

    /** @brief Adds a tag while retaining deterministic lexical order. */
    [[nodiscard]] eve::Result<void> addTag(const std::string& tag);
    /** @brief Removes a tag; an absent tag is an explicit NoOp result. */
    [[nodiscard]] eve::Result<void> removeTag(const std::string& tag);
    /** @brief Returns whether this effect has a tag. */
    bool hasTag(const std::string& tag) const;
    /** @brief Returns the number of tags. */
    int tagCount() const;
    /** @brief Returns a tag by lexical index, or an empty string. */
    std::string tagAt(int index) const;
};

/** @brief Backward-compatible name for the runtime instance type. */
using Effect = EffectInstance;

/** @brief Generation-qualified local reference to a canonical effect instance. */
struct EffectHandle {
    std::string   instanceId;
    std::uint64_t containerGeneration = 0;

    /** @brief Whether this handle contains a local instance id and generation. */
    [[nodiscard]] bool isValid() const noexcept { return !instanceId.empty() && containerGeneration != 0; }
};

/** @brief Lifecycle event kind emitted by an effect container. */
enum class EffectEventKind { Applied, Refreshed, Stacked, Periodic, Expired, Removed };

/** @brief Deterministically sequenced effect lifecycle event. */
struct EffectEvent {
    std::uint64_t   sequence = 0;
    EffectEventKind kind     = EffectEventKind::Applied;
    std::string     effectId;
    std::string     subject;
    std::string     type;
    std::string     source;
    std::string     reason;
    /** @brief UUID-backed effect identity; the string field remains a compatibility projection. */
    eve::EffectId effectIdentity;
    /** @brief Scheduler tick at which the lifecycle event was committed. */
    eve::SimulationTick tick = eve::SimulationTick::zero();
};

/** @brief One periodic trigger handed to a domain executor for settlement. */
struct EffectPeriodicTick {
    std::string              effectId;
    std::string              subject;
    std::string              source;
    double                   magnitude  = 0.0;
    std::uint32_t            stackCount = 1;
    std::vector<std::string> tags;
    eve::EffectId            effectIdentity;
    eve::SimulationTick      tick = eve::SimulationTick::zero();
};

/** @brief Summary returned by the lifecycle executor after one simulation step. */
struct EffectUpdateSummary {
    std::uint32_t expired        = 0;
    double        elapsedSeconds = 0.0;
    /** @brief Periodic triggers in deterministic container order. */
    std::vector<EffectPeriodicTick> periodicTicks;
    /** @brief Scheduler tick that produced this summary. */
    eve::SimulationTick tick = eve::SimulationTick::zero();
};

/** @brief Returns the stable lowercase name of a legacy stack policy. */
std::string policyName(StackPolicy policy);
/** @brief Parses a lowercase legacy stack policy name. */
bool parsePolicy(const std::string& name, StackPolicy& policy);
/** @brief Returns the stable lowercase name of an event kind. */
std::string eventKindName(EffectEventKind kind);

/** @brief Writes the stable lifecycle-event spelling to a stream. */
inline std::ostream& operator<<(std::ostream& stream, EffectEventKind kind) { return stream << eventKindName(kind); }

}  // namespace eve::effects
