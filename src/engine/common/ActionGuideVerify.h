#pragma once

/**
 * @file ActionGuideVerify.h
 * @brief CPU silhouette IoU and joint-error gates for ActionGuide candidates.
 *
 * Owns geometric QC against GuidePack.qc. Does not touch GPU, AbilityRuntime,
 * SpriteSheet*, filesystem, or eve_play. Play-smoke evidence is a DevTools concern.
 */

#include "common/ActionGuidePack.h"
#include "common/Export.h"
#include "common/Result.h"
#include "common/Value.h"

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace eve {

inline constexpr std::string_view kActionGuideVerifyReportSchemaId      = "eve.action.guide-verify-report";
inline constexpr std::uint64_t    kActionGuideVerifyReportSchemaVersion = 1;

/** @brief Measured silhouette overlap for one shot. */
struct ActionGuideSilhouetteMetrics {
    double        iou                 = 0.0;
    std::uint64_t intersection        = 0;
    std::uint64_t unionCount          = 0;
    std::uint64_t guideForeground     = 0;
    std::uint64_t candidateForeground = 0;
};

/** @brief Measured joint L2 error for one shot (optional when joints absent). */
struct ActionGuideJointMetrics {
    bool          evaluated    = false;
    double        maxErrorPx   = 0.0;
    std::uint64_t matchedBones = 0;
    std::uint64_t missingBones = 0;
};

/** @brief Per-shot verify outcome. */
struct ActionGuideShotVerifyResult {
    std::string                  shotId;
    std::int64_t                 shotIndex = 0;
    bool                         passed    = false;
    ActionGuideSilhouetteMetrics silhouette;
    ActionGuideJointMetrics      joints;
    std::string                  reason;
};

/**
 * @brief Aggregate verify report for one GuidePack revision.
 * @ownership Caller-owned; Values are independent copies.
 */
struct ActionGuideVerifyReport {
    std::uint64_t                            sourceGuideRevision = 0;
    bool                                     passed              = false;
    double                                   minSilhouetteIoU    = 0.0;
    double                                   maxJointErrorPx     = 0.0;
    std::vector<ActionGuideShotVerifyResult> shots;
    Value                                    evidence;
};

/** @brief One shot of RGBA inputs for geometric verify (lease-owned buffers). */
struct ActionGuideVerifyShotInput {
    std::string                                      shotId;
    std::uint32_t                                    width  = 0;
    std::uint32_t                                    height = 0;
    std::shared_ptr<const std::vector<std::uint8_t>> guideSilhouetteRgba8;
    std::shared_ptr<const std::vector<std::uint8_t>> candidateRgba8;
    /** @brief Expected landmarks from the guide pack (authoritative). */
    std::vector<ActionGuideLandmark> expectedJoints;
    /** @brief Optional agent-reported / estimated joints; empty skips joint gate. */
    std::vector<ActionGuideLandmark> observedJoints;
};

/** @brief In-memory verify request. */
struct ActionGuideVerifyRequest {
    ActionGuidePack                         pack;
    std::vector<ActionGuideVerifyShotInput> shots;
    std::uint8_t                            guideLuminanceThreshold = 200;
    std::uint8_t                            candidateAlphaThreshold = 8;
};

/**
 * @brief Compute silhouette IoU between guide luminance mask and candidate alpha mask.
 * @remarks Empty guide or candidate foreground is InvalidArgument (not IoU=1).
 * @cost Linear in width × height.
 */
[[nodiscard]] EVENGINE_API_FOUNDATION Result<ActionGuideSilhouetteMetrics> compareActionGuideSilhouetteIoU(
    std::span<const std::uint8_t> guideRgba8, std::span<const std::uint8_t> candidateRgba8, std::uint32_t width,
    std::uint32_t height, std::uint8_t guideLuminanceThreshold = 200, std::uint8_t candidateAlphaThreshold = 8);

/**
 * @brief Max L2 pixel error across bones present in both landmark lists (matched by boneId).
 * @cost Linear in expected × observed size (small joint counts).
 */
[[nodiscard]] EVENGINE_API_FOUNDATION Result<ActionGuideJointMetrics> compareActionGuideJointError(
    std::span<const ActionGuideLandmark> expected, std::span<const ActionGuideLandmark> observed);

/**
 * @brief Evaluate every shot against pack.qc; returns a report even when some shots fail.
 * @remarks Failure of the Result itself is only for malformed input (size mismatch, empty pack).
 *          Geometric Rejected is expressed as report.passed=false with per-shot reasons.
 * @cost Sum of per-shot IoU (+ joint) costs.
 */
[[nodiscard]] EVENGINE_API_FOUNDATION Result<ActionGuideVerifyReport> verifyActionGuideCandidates(
    const ActionGuideVerifyRequest& request);

/** @brief Encode a verify report as a durable evidence Value. */
[[nodiscard]] EVENGINE_API_FOUNDATION Result<Value> encodeActionGuideVerifyReport(
    const ActionGuideVerifyReport& report);

}  // namespace eve
