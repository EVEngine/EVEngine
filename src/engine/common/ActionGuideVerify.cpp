#include "common/ActionGuideVerify.h"

#include "common/Diagnostic.h"

#include <algorithm>
#include <cmath>
#include <span>
#include <utility>

namespace eve {
namespace {

template <typename T>
Result<T> invalid(std::string message, std::string path) {
    return Result<T>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, std::move(message), std::move(path)));
}

std::uint8_t maxRgb(std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    return static_cast<std::uint8_t>(std::max({r, g, b}));
}

}  // namespace

Result<ActionGuideSilhouetteMetrics> compareActionGuideSilhouetteIoU(std::span<const std::uint8_t> guideRgba8,
                                                                     std::span<const std::uint8_t> candidateRgba8,
                                                                     std::uint32_t width, std::uint32_t height,
                                                                     std::uint8_t guideLuminanceThreshold,
                                                                     std::uint8_t candidateAlphaThreshold) {
    const size_t expected = size_t(width) * height * 4;
    if (width == 0 || height == 0) return invalid<ActionGuideSilhouetteMetrics>("empty canvas", "size");
    if (guideRgba8.size() != expected || candidateRgba8.size() != expected)
        return invalid<ActionGuideSilhouetteMetrics>("RGBA buffer size mismatch", "rgba8");

    ActionGuideSilhouetteMetrics metrics;
    for (size_t p = 0; p + 3 < expected; p += 4) {
        const bool guideFg = maxRgb(guideRgba8[p], guideRgba8[p + 1], guideRgba8[p + 2]) >= guideLuminanceThreshold;
        const bool candFg  = candidateRgba8[p + 3] > candidateAlphaThreshold;
        if (guideFg) ++metrics.guideForeground;
        if (candFg) ++metrics.candidateForeground;
        if (guideFg && candFg) ++metrics.intersection;
        if (guideFg || candFg) ++metrics.unionCount;
    }
    if (metrics.guideForeground == 0)
        return invalid<ActionGuideSilhouetteMetrics>("guide silhouette foreground is empty", "guide");
    if (metrics.candidateForeground == 0)
        return invalid<ActionGuideSilhouetteMetrics>("candidate foreground is empty", "candidate");
    metrics.iou = static_cast<double>(metrics.intersection) / static_cast<double>(metrics.unionCount);
    return Result<ActionGuideSilhouetteMetrics>::success(metrics);
}

Result<ActionGuideJointMetrics> compareActionGuideJointError(std::span<const ActionGuideLandmark> expected,
                                                             std::span<const ActionGuideLandmark> observed) {
    ActionGuideJointMetrics metrics;
    if (expected.empty()) {
        metrics.evaluated = false;
        return Result<ActionGuideJointMetrics>::success(metrics);
    }
    if (observed.empty())
        return invalid<ActionGuideJointMetrics>("observed joints required when expected landmarks are present",
                                                "joints");

    metrics.evaluated = true;
    double maxError   = 0.0;
    for (const auto& exp : expected) {
        const ActionGuideLandmark* match = nullptr;
        for (const auto& obs : observed) {
            if (obs.boneId == exp.boneId) {
                match = &obs;
                break;
            }
        }
        if (!match) {
            ++metrics.missingBones;
            continue;
        }
        ++metrics.matchedBones;
        const double dx    = exp.x - match->x;
        const double dy    = exp.y - match->y;
        const double error = std::sqrt(dx * dx + dy * dy);
        if (error > maxError) maxError = error;
    }
    metrics.maxErrorPx = maxError;
    if (metrics.matchedBones == 0)
        return invalid<ActionGuideJointMetrics>("no overlapping boneId between expected and observed joints", "joints");
    return Result<ActionGuideJointMetrics>::success(metrics);
}

Result<ActionGuideVerifyReport> verifyActionGuideCandidates(const ActionGuideVerifyRequest& request) {
    if (request.pack.shots.empty()) return invalid<ActionGuideVerifyReport>("guide pack has no shots", "pack.shots");
    if (request.shots.size() != request.pack.shots.size())
        return invalid<ActionGuideVerifyReport>("verify shot count must equal pack.shots size", "shots");

    ActionGuideVerifyReport report;
    report.sourceGuideRevision = request.pack.revision;
    report.minSilhouetteIoU    = request.pack.qc.minSilhouetteIoU;
    report.maxJointErrorPx     = request.pack.qc.maxJointErrorPx;
    report.passed              = true;

    for (size_t i = 0; i < request.shots.size(); ++i) {
        const auto&                 in   = request.shots[i];
        const auto                  path = "shots[" + std::to_string(i) + "]";
        ActionGuideShotVerifyResult shot;
        shot.shotIndex = static_cast<std::int64_t>(i);
        shot.shotId    = in.shotId.empty() ? request.pack.shots[i].shotId : in.shotId;
        if (!in.shotId.empty() && in.shotId != request.pack.shots[i].shotId) {
            shot.passed   = false;
            shot.reason   = "shotId does not match pack.shots order";
            report.passed = false;
            report.shots.push_back(std::move(shot));
            continue;
        }
        if (!in.guideSilhouetteRgba8 || !in.candidateRgba8)
            return invalid<ActionGuideVerifyReport>("missing guide or candidate RGBA", path);
        if (in.width == 0 || in.height == 0 || in.width != static_cast<std::uint32_t>(request.pack.canvas.width) ||
            in.height != static_cast<std::uint32_t>(request.pack.canvas.height))
            return invalid<ActionGuideVerifyReport>("shot size must match pack.canvas", path + ".size");

        auto iou = compareActionGuideSilhouetteIoU(*in.guideSilhouetteRgba8, *in.candidateRgba8, in.width, in.height,
                                                   request.guideLuminanceThreshold, request.candidateAlphaThreshold);
        if (!iou) return Result<ActionGuideVerifyReport>::failure(iou.status());
        shot.silhouette = iou.value();

        if (shot.silhouette.iou + 1e-12 < report.minSilhouetteIoU) {
            shot.passed = false;
            shot.reason = "silhouette IoU below qc.minSilhouetteIoU";
        } else {
            shot.passed = true;
        }

        if (!in.expectedJoints.empty()) {
            auto joints = compareActionGuideJointError(in.expectedJoints, in.observedJoints);
            if (!joints) {
                shot.passed = false;
                shot.reason = joints.status().describe();
                joints.ignore();
            } else {
                shot.joints = joints.value();
                if (shot.joints.evaluated &&
                    (shot.joints.maxErrorPx > report.maxJointErrorPx || shot.joints.missingBones > 0)) {
                    shot.passed = false;
                    shot.reason = shot.joints.missingBones > 0 ? "joint boneId missing from observation"
                                                               : "joint error exceeds qc.maxJointErrorPx";
                }
            }
        }

        if (!shot.passed) report.passed = false;
        report.shots.push_back(std::move(shot));
    }

    auto encoded = encodeActionGuideVerifyReport(report);
    if (!encoded) return Result<ActionGuideVerifyReport>::failure(encoded.status());
    report.evidence = std::move(encoded).takeValue();
    return Result<ActionGuideVerifyReport>::success(std::move(report));
}

Result<Value> encodeActionGuideVerifyReport(const ActionGuideVerifyReport& report) {
    Value::Array shots;
    for (const auto& shot : report.shots) {
        Value::Object row{
            {"shotId", shot.shotId},
            {"shotIndex", shot.shotIndex},
            {"passed", shot.passed},
            {"reason", shot.reason},
            {"silhouette",
             Value::object({{"iou", shot.silhouette.iou},
                            {"intersection", static_cast<std::int64_t>(shot.silhouette.intersection)},
                            {"union", static_cast<std::int64_t>(shot.silhouette.unionCount)},
                            {"guideForeground", static_cast<std::int64_t>(shot.silhouette.guideForeground)},
                            {"candidateForeground", static_cast<std::int64_t>(shot.silhouette.candidateForeground)}})},
            {"joints", Value::object({{"evaluated", shot.joints.evaluated},
                                      {"maxErrorPx", shot.joints.maxErrorPx},
                                      {"matchedBones", static_cast<std::int64_t>(shot.joints.matchedBones)},
                                      {"missingBones", static_cast<std::int64_t>(shot.joints.missingBones)}})}};
        shots.emplace_back(std::move(row));
    }
    return Result<Value>::success(Value::object(
        {{"schema", std::string(kActionGuideVerifyReportSchemaId)},
         {"schemaVersion", static_cast<std::int64_t>(kActionGuideVerifyReportSchemaVersion)},
         {"sourceGuideRevision", static_cast<std::int64_t>(report.sourceGuideRevision)},
         {"passed", report.passed},
         {"minSilhouetteIoU", report.minSilhouetteIoU},
         {"maxJointErrorPx", report.maxJointErrorPx},
         {"shots", Value(std::move(shots))},
         {"playSmoke", Value::object({{"status", std::string("skipped")},
                                      {"hint", std::string("optional eve_play smoke after geometric pass")}})}}));
}

}  // namespace eve
