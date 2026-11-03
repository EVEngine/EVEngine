#pragma once

/**
 * @file ActionGuideIngest.h
 * @brief Candidate-frame validation and ability/clip draft builders for guide ingest.
 *
 * Owns the CPU-side ingest contract (candidate meta + revision/size checks + draft Values).
 * Does not touch GPU, SpriteSheet*, AbilityRuntime, or the filesystem.
 */

#include "common/ActionGuidePack.h"
#include "common/Export.h"
#include "common/Result.h"
#include "common/Value.h"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace eve {

inline constexpr std::string_view kActionGuideCandidateMetaSchemaId      = "eve.action.guide-candidate";
inline constexpr std::uint64_t    kActionGuideCandidateMetaSchemaVersion = 1;
inline constexpr std::string_view kActionGuideAbilityDraftSchemaId       = "eve.action.guide-ability-draft";
inline constexpr std::uint64_t    kActionGuideAbilityDraftSchemaVersion  = 1;
inline constexpr std::string_view kActionGuideClipRecipeSchemaId         = "eve.action.guide-clip";
inline constexpr std::uint64_t    kActionGuideClipRecipeSchemaVersion    = 1;

/** @brief Per-candidate attribution and revision link back to a GuidePack. */
struct ActionGuideCandidateMeta {
    std::uint64_t sourceGuideRevision = 0;
    std::string   shotId;
    std::string   model;
    std::int64_t  seed = 0;
    std::string   promptHash;
};

/** @brief One candidate frame with lease-owned RGBA8 pixels. */
struct ActionGuideCandidateFrame {
    ActionGuideCandidateMeta                         meta;
    std::uint32_t                                    width  = 0;
    std::uint32_t                                    height = 0;
    std::shared_ptr<const std::vector<std::uint8_t>> rgba8;
};

/** @brief In-memory ingest request; frames ordered to match pack.shots (or shotId map). */
struct ActionGuideIngestRequest {
    ActionGuidePack                        pack;
    std::vector<ActionGuideCandidateFrame> frames;
    std::string                            outputClipName;
    bool                                   requireTransparentEdge = true;
};

/**
 * @brief Draft artifacts produced by ingest (no formal AbilityRuntime mutation).
 * @ownership Caller owns; Values are independent copies.
 */
struct ActionGuideIngestDraft {
    std::uint64_t sourceGuideRevision = 0;
    std::string   clipName;
    double        fps             = 12.0;
    bool          loop            = false;
    std::int64_t  frameCount      = 0;
    std::string   sequencePattern = "frames/frame_{n}.png";
    std::int64_t  sequenceFirst   = 0;
    std::int64_t  sequenceLast    = -1;
    Value         abilityDraft;
    Value         clipDescriptor;
    Value         attribution;
};

/**
 * @brief Decode one candidate meta object; unknown fields/versions are rejected.
 * @cost Linear in object field count.
 */
[[nodiscard]] EVENGINE_API_FOUNDATION Result<ActionGuideCandidateMeta> decodeActionGuideCandidateMeta(
    const Value& value);

/** @brief Encode one validated candidate meta document. */
[[nodiscard]] EVENGINE_API_FOUNDATION Result<Value> encodeActionGuideCandidateMeta(
    const ActionGuideCandidateMeta& meta);

/**
 * @brief Validate candidate frames against a GuidePack contract (revision, count, size, alpha).
 * @remarks Does not write files or register abilities. Failure publishes no state.
 * @cost Linear in frame count × pixels when transparency checks are enabled.
 */
[[nodiscard]] EVENGINE_API_FOUNDATION Result<void> validateActionGuideCandidates(
    const ActionGuideIngestRequest& request);

/**
 * @brief Build clip/ability draft Values from a validated ingest request.
 * @remarks Does not call AbilityRuntime or create SpriteSheet*. Caller should validate first.
 * @cost Linear in frame count for attribution assembly.
 */
[[nodiscard]] EVENGINE_API_FOUNDATION Result<ActionGuideIngestDraft> buildActionGuideIngestDraft(
    const ActionGuideIngestRequest& request);

}  // namespace eve
