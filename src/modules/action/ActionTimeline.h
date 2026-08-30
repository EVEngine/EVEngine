#pragma once

/**
 * @file ActionTimeline.h
 * @brief Versioned, deterministic authoring timeline for gameplay actions.
 */

#include "common/Identity.h"
#include "common/Result.h"
#include "common/SchemaVersion.h"
#include "common/Time.h"
#include "common/Value.h"

#include <cstdint>
#include <string>
#include <vector>

namespace eve::action {

/** @brief Canonical schema identifier for persisted action timelines. */
inline constexpr std::string_view kActionTimelineSchemaId = "eve.action.timeline";
/** @brief Current action timeline schema version. */
inline constexpr std::uint64_t kActionTimelineSchemaVersion = 1;

/** @brief Semantic lane kind; hosts choose presentation without changing data. */
enum class ActionTrackKind : std::uint8_t {
    Animation,
    Gameplay,
    Effect,
    Audio,
    Camera,
    Movement,
    Tag,
    Custom,
};

/** @brief Stable lowercase protocol spelling for a track kind. */
[[nodiscard]] std::string_view actionTrackKindName(ActionTrackKind kind) noexcept;
/** @brief Parse a stable track kind spelling. */
[[nodiscard]] Result<ActionTrackKind> parseActionTrackKind(std::string_view text);

/** @brief Instantaneous typed event placed on an action track. */
struct ActionNotify {
    LogicalId     id;
    LogicalId     type;
    Duration      time = Duration::zero();
    Value::Object payload;

    auto operator<=>(const ActionNotify&) const = default;
};

/** @brief Typed interval with explicit enter/exit boundaries. */
struct ActionNotifyState {
    LogicalId     id;
    LogicalId     type;
    Duration      start = Duration::zero();
    Duration      end   = Duration::zero();
    Value::Object payload;

    auto operator<=>(const ActionNotifyState&) const = default;
};

/** @brief Ordered semantic lane in one action timeline. */
struct ActionTrack {
    LogicalId                      id;
    std::string                    label;
    ActionTrackKind                kind   = ActionTrackKind::Custom;
    bool                           muted  = false;
    bool                           locked = false;
    std::vector<ActionNotify>      notifies;
    std::vector<ActionNotifyState> states;

    auto operator<=>(const ActionTrack&) const = default;
};

/** @brief Boundary emitted by deterministic timeline sampling. */
enum class ActionTimelineEventKind : std::uint8_t { Notify, StateEnter, StateExit };

/**
 * @brief Owning event projection returned by the runtime sampler.
 *
 * The projection owns its payload so consumers may queue it without retaining
 * references into a hot-reloaded definition.
 */
struct ActionTimelineEvent {
    ActionTimelineEventKind kind = ActionTimelineEventKind::Notify;
    LogicalId               trackId;
    LogicalId               itemId;
    LogicalId               type;
    Duration                time = Duration::zero();
    Value::Object           payload;

    auto operator<=>(const ActionTimelineEvent&) const = default;
};

/**
 * @brief Canonical action timeline asset shared by runtime and editor.
 *
 * The action module is the sole schema owner. Animation, audio, VFX and camera
 * resources are referenced by URI/type payloads and resolved by downstream
 * adapters; this L1 module never depends on those presentation modules.
 */
struct ActionTimeline {
    SchemaVersion            schemaVersion{kActionTimelineSchemaVersion};
    LogicalId                actionId;
    Duration                 duration = Duration::zero();
    std::string              animationUri;
    std::vector<ActionTrack> tracks;
    Value::Object            metadata;

    /** @brief Validate ids, ranges, uniqueness and deterministic ordering. */
    [[nodiscard]] Result<void> validate() const;

    /**
     * @brief Sample all boundaries in `(previous,current]` in stable order.
     * @param includePrevious Include events exactly at previous; use once when playback starts.
     * @return Owning event list, or a validation/range diagnostic.
     */
    [[nodiscard]] Result<std::vector<ActionTimelineEvent>> sample(Duration previous, Duration current,
                                                                  bool includePrevious = false) const;

    /** @brief Encode the canonical schema as an owning deterministic Value. */
    [[nodiscard]] Result<Value> toValue() const;
    /** @brief Decode and validate one canonical schema value transactionally. */
    [[nodiscard]] static Result<ActionTimeline> fromValue(const Value& value);
};

}  // namespace eve::action
