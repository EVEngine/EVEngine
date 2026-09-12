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
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace eve::action {

/** @brief Canonical schema identifier for persisted action timelines. */
inline constexpr std::string_view kActionTimelineSchemaId = "eve.action.timeline";
/** @brief Current action timeline schema version. */
inline constexpr std::uint64_t kActionTimelineSchemaVersion = 3;

/** @brief Built-in deterministic cross-fade curve evaluated without asset callbacks. */
enum class ActionBlendCurve : std::uint8_t {
    Linear,
    EaseInOut,
};

/** @brief Montage-wide presentation settings persisted with the action timeline. */
struct ActionMontageSettings {
    double        basePlayRate         = 1.0;
    bool          looping              = false;
    bool          footIk               = false;
    std::uint32_t animationLayer       = 0;
    Duration      defaultBlendIn       = Duration::zero();
    Duration      defaultBlendOut      = Duration::zero();
    Duration      blendOutOffset       = Duration::zero();
    bool          rootMotionHorizontal = true;
    bool          rootMotionVertical   = true;
    bool          rootMotionRotation   = true;

    auto operator<=>(const ActionMontageSettings&) const = default;
};

/**
 * @brief Objective animation segment placed on the montage presentation lane.
 *
 * Sections describe only clip geometry and blending. Gameplay phase meaning
 * remains owned by ActionRuntime and must not be inferred from section names.
 */
struct ActionAnimationSection {
    LogicalId   id;
    std::string animationUri;
    Duration    start   = Duration::zero();
    Duration    end     = Duration::zero();
    Duration    blendIn = Duration::zero();
    /** @brief Clip-local trim-in timestamp. */
    Duration sourceStart = Duration::zero();
    /** @brief Clip-local trim-out timestamp; zero means the physical clip end. */
    Duration sourceEnd = Duration::zero();
    /** @brief Curve used for the incoming cross-fade. */
    ActionBlendCurve blendCurve = ActionBlendCurve::EaseInOut;

    auto operator<=>(const ActionAnimationSection&) const = default;
};

/** @brief Stable persisted name for a montage cross-fade curve. */
[[nodiscard]] std::string_view actionBlendCurveName(ActionBlendCurve curve) noexcept;
/** @brief Parse a persisted montage cross-fade curve name. */
[[nodiscard]] std::optional<ActionBlendCurve> actionBlendCurveFromName(std::string_view name) noexcept;

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

/** @brief Owning per-step sample of one currently active timeline state. */
struct ActionActiveBlock {
    LogicalId     trackId;
    LogicalId     itemId;
    LogicalId     type;
    Duration      localTime = Duration::zero();
    Duration      duration  = Duration::zero();
    Value::Object payload;

    auto operator<=>(const ActionActiveBlock&) const = default;
};

/**
 * @brief Canonical action timeline asset shared by runtime and editor.
 *
 * The action module is the sole schema owner. Animation, audio, VFX and camera
 * resources are referenced by URI/type payloads and resolved by downstream
 * adapters; this L1 module never depends on those presentation modules.
 */
struct ActionTimeline {
    SchemaVersion schemaVersion{kActionTimelineSchemaVersion};
    LogicalId     actionId;
    Duration      duration = Duration::zero();
    /** @brief Compatibility-only single-clip projection; v2 runtime uses animationSections. */
    std::string                         animationUri;
    std::vector<ActionAnimationSection> animationSections;
    ActionMontageSettings               montage;
    /** @brief Sorted objective split timestamps forming N+1 physical sections. */
    std::vector<Duration>    splitTimestamps;
    std::vector<ActionTrack> tracks;
    Value::Object            metadata;

    /** @brief Validate ids, ranges, uniqueness and deterministic ordering. */
    [[nodiscard]] Result<void> validate() const;
    /** @brief Number of objective physical sections. */
    [[nodiscard]] std::size_t sectionCount() const noexcept { return splitTimestamps.size() + 1; }
    /** @brief Resolve one physical section to its inclusive start and exclusive end. */
    [[nodiscard]] Result<std::pair<Duration, Duration>> sectionRange(std::size_t index) const;

    /**
     * @brief Sample all boundaries in `(previous,current]` in stable order.
     * @param includePrevious Include events exactly at previous; use once when playback starts.
     * @return Owning event list, or a validation/range diagnostic.
     */
    [[nodiscard]] Result<std::vector<ActionTimelineEvent>> sample(Duration previous, Duration current,
                                                                  bool includePrevious = false) const;

    /**
     * @brief Sample every unmuted state active at one authoritative timeline time.
     * @param time Timestamp in the closed timeline range; state ends remain exclusive.
     * @return Owning blocks in stable track/item order, or a validation/range diagnostic.
     */
    [[nodiscard]] Result<std::vector<ActionActiveBlock>> activeBlocks(Duration time) const;

    /** @brief Encode the canonical schema as an owning deterministic Value. */
    [[nodiscard]] Result<Value> toValue() const;
    /** @brief Decode and validate one canonical schema value transactionally. */
    [[nodiscard]] static Result<ActionTimeline> fromValue(const Value& value);
};

}  // namespace eve::action
