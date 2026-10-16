#pragma once
#include "common/Export.h"


#include "common/Result.h"

#include <cstdint>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace eve::procgen {

/** @brief Scale interpolation on the outgoing span of an owning spline point. */
enum class SplineScaleInterpolation : std::uint8_t { Linear = 0, CatmullRom = 1 };

/** @brief Roll interpolation on one outgoing span; generic splines retain Linear. */
enum class SplineRollInterpolation : std::uint8_t { Linear = 0, SmoothStep = 1 };

/** @brief Native outgoing overrides; Inherit uses the path kind. */
enum class SplineSpanControl : std::uint8_t { Inherit = 0, StraightXZ = 1, StraightXYZ = 2, Circular = 3 };

/** @brief One spline anchor with incoming and outgoing handle offsets. */
struct SplinePoint {
    float                    x = 0.f, y = 0.f, z = 0.f;
    float                    inX = 0.f, inY = 0.f, inZ = 0.f;
    float                    outX = 0.f, outY = 0.f, outZ = 0.f;
    float                    rollDegrees = 0.f;
    float                    scaleX = 1.f, scaleY = 1.f;
    bool                     breakBefore  = false;
    float                    pitchDegrees = 0.f, yawDegrees = 0.f;
    SplineScaleInterpolation scaleXInterpolation =
        SplineScaleInterpolation::Linear;  ///< Native-only; cubic scale is floored at 1e-4 to retain a positive frame.
    bool worldUpFrame =
        false;  ///< Native-only outgoing frame policy; vertical tangents use a deterministic X reference.
    SplineRollInterpolation rollInterpolation =
        SplineRollInterpolation::Linear;         ///< Native-only owning outgoing policy.
    float             spanStrength       = .5f;  ///< Native-only outgoing Catmull tangent coefficient in [0.01,1].
    SplineSpanControl spanControl        = SplineSpanControl::Inherit;  ///< Native-only outgoing geometry controller.
    bool              alignStraightStart = false,
         alignStraightEnd =
             false;  ///< Straight-adjacent XZ alignment with zero vertical derivative; reversal swaps ends.
    bool  circularFromEnd = false;  ///< Derive circular geometry from the following endpoint tangent after reversal.
    float rollPivotSide   = 0.f;    ///< Local transverse roll-axis offset before scaleX; native owning value, metres.
};

/** @brief Evaluated spline position and normalized tangent. */
struct SplineSample {
    float x = 0.f, y = 0.f, z = 0.f;
    float tangentX = 1.f, tangentY = 0.f, tangentZ = 0.f;
    float normalizedDistance = 0.f;
    float rollDegrees        = 0.f;
    float scaleX = 1.f, scaleY = 1.f;
    int   chunkIndex   = 0;
    float pitchDegrees = 0.f, yawDegrees = 0.f;
    bool  worldUpFrame  = false;  ///< Owning projection of the outgoing point policy.
    float rollPivotSide = 0.f;    ///< Interpolated local roll-axis offset before scaleX, metres.
};

/** @brief Owning section frame; sample position includes roll-pivot displacement, unlike the author centerline sample.
 */
struct SplineFrameSample {
    SplineSample sample;
    float        sideX = 1.f, sideY = 0.f, sideZ = 0.f;
    float        upX = 0.f, upY = 1.f, upZ = 0.f;
    float        forwardX = 0.f, forwardY = 0.f, forwardZ = 1.f;
    bool         authoredMarker = false;  ///< Native-only derived tag emitted by marker-preserving frame sampling.
};

/** @brief Owning uniformly distributed spline samples for instancing. */
class EVENGINE_API_DOMAINS SplineDistribution {
public:
    /** @brief Construct an owning distribution from validated samples. */
    explicit SplineDistribution(std::vector<SplineFrameSample> frames = {}) : frames_(std::move(frames)) {}
    /** @brief Return the number of independent sample values. */
    [[nodiscard]] int count() const noexcept { return static_cast<int>(frames_.size()); }
    /** @brief Return one sample value, or a structured out-of-range diagnostic. */
    [[nodiscard]] Result<SplineSample> sampleResult(int index) const;
    /** @brief Return one sample with its transported orientation frame. */
    [[nodiscard]] Result<SplineFrameSample> frameResult(int index) const;

private:
    std::vector<SplineFrameSample> frames_;
};

/** @brief Owning sampled polyline for renderer-neutral runtime visualization adapters. */
class EVENGINE_API_DOMAINS SplinePolyline {
public:
    /** @brief Construct from an owning sequence of sampled points. */
    explicit SplinePolyline(std::vector<std::vector<SplineSample>> chunks = {}, bool closed = false)
        /** @brief Chunks. */
        : chunks_(std::move(chunks)), closed_(closed) {}
    /** @brief Return sampled point count. */
    [[nodiscard]] int count() const noexcept;
    /** @brief Return one point sample or a structured range error. */
    [[nodiscard]] Result<SplineSample> pointResult(int index) const;
    /** @brief Return disconnected polyline chunk count. */
    [[nodiscard]] int chunkCount() const noexcept { return static_cast<int>(chunks_.size()); }
    /** @brief Return point count for one chunk, or zero for an invalid index. */
    [[nodiscard]] int chunkPointCount(int chunk) const noexcept;
    /** @brief Return one point inside one disconnected chunk. */
    [[nodiscard]] Result<SplineSample> chunkPointResult(int chunk, int index) const;
    /** @brief Report whether presentation adapters should close the last-to-first edge. */
    [[nodiscard]] bool isClosed() const noexcept { return closed_; }

private:
    std::vector<std::vector<SplineSample>> chunks_;
    bool                                   closed_ = false;
};

/**
 * @brief Owning multi-segment 3D spline with deterministic bounded sampling.
 *
 * Calls are synchronous and owner-thread only. Mutations invalidate the arc-length cache.
 * Returned samples are values and retain no pointers into the path.
 */
class EVENGINE_API_DOMAINS SplinePath {
public:
    /** @brief Select linear, catmullRom, quadraticBezier, or cubic bezier interpolation. */
    [[nodiscard]] Result<void> setKindResult(std::string_view kind);
    /** @brief Configure whether the final point connects back to the first. */
    void setClosed(bool closed);
    /** @brief Append an owning control point after validating finite coordinates. */
    [[nodiscard]] Result<void> addPointResult(const SplinePoint& point);
    /** @brief Replace a control point atomically. */
    [[nodiscard]] Result<void> setPointResult(int index, const SplinePoint& point);
    /** @brief Atomically replace per-point roll and cross-section scale after finite positive validation. */
    [[nodiscard]] Result<void> setPointProfileResult(int index, float rollDegrees, float scaleX, float scaleY);
    /** @brief Atomically replace point-local pitch, yaw, and roll orientation. */
    [[nodiscard]] Result<void> setPointRotationResult(int index, float pitchDegrees, float yawDegrees,
                                                      float rollDegrees);
    /** @brief Start or reconnect an open-path chunk before one non-first point. */
    [[nodiscard]] Result<void> setPointChunkBreakResult(int index, bool disconnected);
    /** @brief Remove a control point by stable current index. */
    [[nodiscard]] Result<void> removePointResult(int index);
    /** @brief Remove every point. */
    void clear();

    /** @brief Evaluate by normalized segment parameter, not arc length. */
    [[nodiscard]] Result<SplineSample> evaluateResult(float t) const;
    /** @brief Evaluate by world-space distance using a bounded arc-length table. */
    [[nodiscard]] Result<SplineSample> evaluateDistanceResult(float distance, int samplesPerSegment = 24) const;
    /** @brief Return approximate total world-space length. */
    [[nodiscard]] Result<float> lengthResult(int samplesPerSegment = 24) const;
    /** @brief Return the sampled closest path location to a point. */
    [[nodiscard]] Result<SplineSample> closestPointResult(float x, float y, float z, int samplesPerSegment = 32) const;
    /**
     * @brief Sample stable parallel-transport frames for extrusion and placement.
     * @param sampleCount Number of base intervals; returns sampleCount plus one frames before optional anchors.
     * @param uniformByDistance Use the arc-length table instead of segment parameter spacing.
     * @param rollDegrees Constant roll applied about each tangent after transport.
     * @param samplesPerSegment Arc-table resolution when uniformByDistance is true.
     * @param preserveMarkers Add exact anchor frames on a continuous path; output remains bounded to 4097 frames.
     * @cost Output-proportional transport plus bounded sorting for retained markers; cached arc queries amortize
     * table construction per path revision.
     * @return Owning deterministic frame samples, or a structured validation diagnostic.
     * @note Synchronous, owner-thread-only, non-reentrant; results retain no path references.
     */
    [[nodiscard]] Result<std::vector<SplineFrameSample>> sampleFramesResult(int sampleCount, bool uniformByDistance,
                                                                            float rollDegrees       = 0.f,
                                                                            int   samplesPerSegment = 24,
                                                                            bool  preserveMarkers   = false) const;
    /**
     * @brief Build frames at explicitly ordered curve parameters using the canonical transport and roll pipeline.
     * @param parameters Borrowed strictly increasing finite values in [0,1], bounded to 1..4097 samples.
     * @param samplesPerSegment Arc-table resolution for the returned normalized arc distances.
     * @return Owning frames; partial ranges start transport at their first sample and do not force a closed seam.
     * @cost Curve queries scale with requested frames and path points; transport is output-linear and cached
     * arc construction amortizes per path revision.
     * @note Synchronous, owner-thread-only, non-reentrant; no input span or path pointer is retained.
     */
    [[nodiscard]] Result<std::vector<SplineFrameSample>> sampleFramesAtParametersResult(
        std::span<const float> parameters, int samplesPerSegment = 24) const;
    /** @brief Evaluate distance-driven travel using clamp, loop, or pingPong wrapping without reading wall time. */
    [[nodiscard]] Result<SplineSample> travelResult(float distance, std::string_view wrapMode = "clamp",
                                                    int samplesPerSegment = 24) const;
    /** @brief Evaluate distance-driven travel with a transported orientation frame. */
    [[nodiscard]] Result<SplineFrameSample> travelFrameResult(float distance, std::string_view wrapMode = "clamp",
                                                              int samplesPerSegment = 24) const;
    /** @brief Produce owning arc-length-uniform samples for instance distribution. */
    [[nodiscard]] Result<SplineDistribution> distributeResult(int instanceCount, bool includeEnd = true,
                                                              int samplesPerSegment = 24) const;
    /** @brief Build an owning sampled polyline without depending on a graphics module. */
    [[nodiscard]] Result<SplinePolyline> polylineResult(int sampleCount = 64, bool uniformByDistance = true,
                                                        int samplesPerSegment = 24) const;
    /**
     * @brief Atomically replace this path with line, circle, arc, spiral, or wave preset points.
     * @param preset Named preset.
     * @param pointCount Generated control-point count in [2, 4096].
     * @param radius Positive horizontal radius or half-length.
     * @param height Vertical extent or wave amplitude.
     * @param turns Positive revolution/cycle count.
     */
    [[nodiscard]] Result<void> applyShapePresetResult(std::string_view preset, int pointCount, float radius,
                                                      float height, float turns = 1.f);

    /** @brief Return point count. */
    [[nodiscard]] int pointCount() const noexcept { return static_cast<int>(points_.size()); }
    /** @brief Return segment count. */
    [[nodiscard]] int segmentCount() const noexcept;
    /** @brief Return the number of independently connected chunks. */
    [[nodiscard]] int chunkCount() const noexcept;
    /** @brief Copy one independently connected chunk as an owning path. */
    [[nodiscard]] Result<SplinePath> chunkPathResult(int chunk) const;
    /** @brief Return interpolation kind. */
    [[nodiscard]] std::string_view kind() const noexcept { return kind_; }
    /** @brief Report whether the path is closed. */
    [[nodiscard]] bool isClosed() const noexcept { return closed_; }
    /** @brief Return monotonic mutation revision. */
    [[nodiscard]] std::uint64_t revision() const noexcept { return revision_; }

private:
    struct ArcEntry {
        int   segment  = 0;
        float u        = 0.f;
        float distance = 0.f;
    };
    [[nodiscard]] Result<void> validateReady() const;
    [[nodiscard]] SplineSample evaluateUnchecked(float t) const;
    [[nodiscard]] SplineSample evaluateSegmentUnchecked(int segment, float u, float normalized) const;
    [[nodiscard]] std::vector<std::pair<int, int>>       segmentEndpoints() const;
    [[nodiscard]] Result<void>                           ensureArcTable(int samplesPerSegment) const;
    [[nodiscard]] Result<std::vector<SplineFrameSample>> orientFrameSamplesResult(std::vector<SplineFrameSample> frames,
                                                                                  std::span<const float>         ratios,
                                                                                  float rollDegrees) const;
    void                                                 invalidate();

    std::vector<SplinePoint>      points_;
    std::string                   kind_     = "catmullRom";
    bool                          closed_   = false;
    std::uint64_t                 revision_ = 0;
    mutable std::vector<ArcEntry> arcTable_;
    mutable int                   arcSamplesPerSegment_ = 0;
};

}  // namespace eve::procgen
