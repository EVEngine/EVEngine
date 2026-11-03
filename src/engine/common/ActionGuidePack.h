#pragma once

/**
 * @file ActionGuidePack.h
 * @brief Versioned `eve.action.guide-pack` document codec and CPU landmark projection.
 *
 * Owns the document protocol for the whitebox→AI-guide→2D-action pipeline. Does not own
 * GPU capture, sprite ingest, or AbilityRuntime state. Wire keys use `schema` /
 * `schemaVersion` (same convention as `eve.action.ability`).
 */

#include "common/Export.h"
#include "common/Result.h"
#include "common/Value.h"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace eve {

inline constexpr std::string_view kActionGuidePackSchemaId      = "eve.action.guide-pack";
inline constexpr std::uint64_t    kActionGuidePackSchemaVersion = 1;

/** @brief Root-motion handling when sampling the subject for guide shots. */
enum class ActionGuideRootMode : std::uint8_t { InPlace, WorldRoot };

/** @brief Camera projection used for guide AOVs and landmark projection. */
enum class ActionGuideProjection : std::uint8_t { Orthographic, Perspective };

/** @brief Generation-qualified subject handle; domain-owned, not a universal actor. */
struct ActionGuideSubject {
    std::string   domain;
    std::uint64_t id         = 0;
    std::uint64_t generation = 0;
};

/** @brief Output canvas contract for guide images and landmark pixels. */
struct ActionGuideCanvas {
    std::int64_t          width         = 0;
    std::int64_t          height        = 0;
    double                pixelsPerUnit = 1.0;
    std::array<double, 4> backgroundRgba{0.0, 0.0, 0.0, 0.0};
    bool                  transparent = true;
};

/** @brief Offscreen guide camera; never mutates the live gameplay camera. */
struct ActionGuideCamera {
    ActionGuideProjection projection  = ActionGuideProjection::Orthographic;
    double                fovYDegrees = 45.0;
    double                orthoSize   = 1.0;
    double                nearPlane   = 0.1;
    double                farPlane    = 100.0;
    std::array<double, 3> eye{0.0, 0.0, 5.0};
    std::array<double, 3> target{0.0, 0.0, 0.0};
    std::array<double, 3> up{0.0, 1.0, 0.0};
};

/** @brief One guide shot: sample time, view label, and required AOV set. */
struct ActionGuideShot {
    std::string                 shotId;
    double                      timeSeconds = 0.0;
    std::optional<std::int64_t> sampleIndex;
    std::string                 view = "front";
    std::vector<std::string>    aovs;
    std::vector<std::string>    tags;
};

/** @brief Named phase marker for later Action/Ability timeline binding. */
struct ActionGuidePhaseMark {
    std::string name;
    double      timeSeconds = 0.0;
};

/** @brief Draft binding from guide clips into an ability/clip identity. */
struct ActionGuideBinding {
    std::string                       abilityId;
    std::string                       clipName;
    double                            fps  = 12.0;
    bool                              loop = false;
    std::vector<ActionGuidePhaseMark> phaseMarks;
};

/** @brief Acceptance thresholds for candidate ingest / verify. */
struct ActionGuideQc {
    double       minSilhouetteIoU = 0.85;
    double       maxJointErrorPx  = 8.0;
    std::int64_t maxRetries       = 3;
};

/**
 * @brief Authoritative guide-pack document (no pointers, GPU tokens, or ECS slots).
 * @ownership Caller-owned value object; encode/decode copy all nested storage.
 * @thread Affine to the caller; encode/decode are reentrant and invoke no callbacks.
 */
struct ActionGuidePack {
    std::uint64_t                revision = 1;
    ActionGuideSubject           subject;
    std::string                  skeletonRef;
    std::string                  clipRef;
    ActionGuideRootMode          rootMode = ActionGuideRootMode::InPlace;
    ActionGuideCanvas            canvas;
    ActionGuideCamera            camera;
    std::vector<ActionGuideShot> shots;
    ActionGuideBinding           actionBinding;
    ActionGuideQc                qc;
};

/** @brief World-space bone sample supplied by an animation/pose owner. */
struct ActionGuideBoneWorld {
    std::string                boneId;
    std::string                name;
    std::array<double, 3>      position{};
    std::optional<std::string> parentId;
};

/** @brief Projected joint in guide-canvas pixels (origin top-left) plus view depth. */
struct ActionGuideLandmark {
    std::string                boneId;
    std::string                name;
    double                     x     = 0.0;
    double                     y     = 0.0;
    double                     depth = 0.0;
    std::optional<std::string> parentId;
};

/**
 * @brief Encode one validated guide pack as canonical schema version 1 data.
 * @param pack Owning pack borrowed only for this synchronous call.
 * @return Independently owning Value, or a structured validation/encoding failure.
 * @remarks Owner-thread-only and reentrant; no files, clocks, or callbacks.
 * @cost Linear in shot and landmark-free pack size.
 */
[[nodiscard]] EVENGINE_API_FOUNDATION Result<Value> encodeActionGuidePack(const ActionGuidePack& pack);

/**
 * @brief Decode schema version 1; unknown fields/versions and incomplete shots are rejected.
 * @param value Untrusted owning value tree borrowed only for this synchronous call.
 * @return Fully validated owning pack, or a structured parse/validation failure.
 * @remarks Failure publishes no runtime state. Nested objects reject unknown fields.
 */
[[nodiscard]] EVENGINE_API_FOUNDATION Result<ActionGuidePack> decodeActionGuidePack(const Value& value);

/**
 * @brief Project world-space bones into guide-canvas landmarks using the pack camera/canvas.
 * @param pack Validated pack borrowed for this call; camera and canvas define the projection.
 * @param bones Caller-owned world samples; not retained.
 * @return Owning landmark list, or a structured projection failure.
 * @remarks Does not sample animation or touch GPU. Depth is positive view-space distance.
 * @cost Linear in bone count; matrix setup is O(1) per call.
 */
[[nodiscard]] EVENGINE_API_FOUNDATION Result<std::vector<ActionGuideLandmark>> projectActionGuideLandmarks(
    const ActionGuidePack& pack, std::span<const ActionGuideBoneWorld> bones);

/**
 * @brief Optional GPU guide-capture provider (silhouette/depth/bone AOV export).
 * @thread Engine owner thread only; never wait for GPU inside request.
 * @lifetime Query on each call. Absent provider → MCP reports ProviderAbsent.
 */
class EVENGINE_API_FOUNDATION_INLINE IActionGuideCapture {
public:
    static constexpr const char* capabilityName = "IActionGuideCapture";
    virtual ~IActionGuideCapture()              = default;

    /**
     * @brief Queue guide AOV capture for one pack revision / shot set; never block on GPU.
     * @param packJson Canonical pack JSON borrowed for validation/copy on this call.
     * @param shotIds Empty means all shots in the pack.
     * @return Opaque job token, or Unsupported / budget diagnostic.
     * @cost Extra offscreen raster + readback per shot × AOV; reuse the resulting artifacts.
     */
    [[nodiscard]] virtual Result<std::string> requestCapture(std::string_view             packJson,
                                                             std::span<const std::string> shotIds) = 0;
    /** @brief Poll without waiting; Ready returns export paths / metadata JSON. */
    [[nodiscard]] virtual Result<Value> captureStatus(std::string_view token) = 0;
    /** @brief Cancel/release; GPU resources survive until their fence completes. */
    [[nodiscard]] virtual Result<void> releaseCapture(std::string_view token) = 0;
};

}  // namespace eve
