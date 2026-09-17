#include "procgen/spline/SplinePath.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>

namespace eve::procgen {
namespace {

template <class T>
Result<T> splineFailure(DiagnosticCode code, std::string message, std::string path = {}) {
    return Result<T>::failure(Diagnostic::error(code, std::move(message), std::move(path), {}, "procgen.splinePath"));
}

bool finite(const SplinePoint& p) {
    return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z) && std::isfinite(p.inX) &&
           std::isfinite(p.inY) && std::isfinite(p.inZ) && std::isfinite(p.outX) && std::isfinite(p.outY) &&
           std::isfinite(p.outZ) && std::isfinite(p.rollDegrees) && std::isfinite(p.scaleX) &&
           std::isfinite(p.scaleY) && std::isfinite(p.pitchDegrees) && std::isfinite(p.yawDegrees) && p.scaleX > 0.f &&
           p.scaleY > 0.f;
}

struct V3 {
    float x, y, z;
};

V3    add(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
V3    sub(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
V3    mul(V3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V3    cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
float length(V3 value) { return std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z); }
V3    normalized(V3 value) {
    const float magnitude = length(value);
    return magnitude > 1e-7f ? mul(value, 1.f / magnitude) : V3{1.f, 0.f, 0.f};
}
V3 rotateAround(V3 value, V3 axis, float angle) {
    const float c = std::cos(angle), s = std::sin(angle);
    return add(add(mul(value, c), mul(cross(axis, value), s)), mul(axis, dot(axis, value) * (1.f - c)));
}
V3 position(const SplinePoint& p) { return {p.x, p.y, p.z}; }

}  // namespace

Result<void> SplinePath::setKindResult(std::string_view kind) {
    if (kind != "linear" && kind != "catmullRom" && kind != "quadraticBezier" && kind != "bezier")
        return splineFailure<void>(DiagnosticCode::InvalidArgument,
                                   "spline kind must be linear, catmullRom, quadraticBezier, or bezier", "kind");
    kind_ = kind;
    invalidate();
    return Result<void>::success();
}

void SplinePath::setClosed(bool closed) {
    bool changed = closed_ != closed;
    if (closed)
        for (std::size_t i = 1; i < points_.size(); ++i) {
            changed                = changed || points_[i].breakBefore;
            points_[i].breakBefore = false;
        }
    if (!changed) return;
    closed_ = closed;
    invalidate();
}

Result<void> SplinePath::addPointResult(const SplinePoint& point) {
    if (!finite(point))
        return splineFailure<void>(DiagnosticCode::InvalidArgument, "spline point must be finite", "point");
    points_.push_back(point);
    invalidate();
    return Result<void>::success();
}

Result<void> SplinePath::setPointResult(int index, const SplinePoint& point) {
    if (index < 0 || index >= pointCount())
        return splineFailure<void>(DiagnosticCode::NotFound, "spline point index is out of range", "point.index");
    if (!finite(point))
        return splineFailure<void>(DiagnosticCode::InvalidArgument, "spline point must be finite", "point");
    points_[static_cast<std::size_t>(index)] = point;
    invalidate();
    return Result<void>::success();
}

Result<void> SplinePath::setPointProfileResult(int index, float rollDegrees, float scaleX, float scaleY) {
    if (index < 0 || index >= pointCount())
        return splineFailure<void>(DiagnosticCode::NotFound, "spline point index is out of range", "point.index");
    if (!std::isfinite(rollDegrees) || !std::isfinite(scaleX) || !std::isfinite(scaleY) || scaleX <= 0.f ||
        scaleY <= 0.f)
        return splineFailure<void>(DiagnosticCode::InvalidArgument,
                                   "spline point profile requires finite roll and positive scales", "point.profile");
    auto candidate                           = points_[static_cast<std::size_t>(index)];
    candidate.rollDegrees                    = rollDegrees;
    candidate.scaleX                         = scaleX;
    candidate.scaleY                         = scaleY;
    points_[static_cast<std::size_t>(index)] = candidate;
    invalidate();
    return Result<void>::success();
}

Result<void> SplinePath::setPointRotationResult(int index, float pitchDegrees, float yawDegrees, float rollDegrees) {
    if (index < 0 || index >= pointCount())
        return splineFailure<void>(DiagnosticCode::NotFound, "spline point index is out of range", "point.index");
    if (!std::isfinite(pitchDegrees) || !std::isfinite(yawDegrees) || !std::isfinite(rollDegrees))
        return splineFailure<void>(DiagnosticCode::InvalidArgument, "spline point rotation must be finite",
                                   "point.rotation");
    auto candidate                           = points_[static_cast<std::size_t>(index)];
    candidate.pitchDegrees                   = pitchDegrees;
    candidate.yawDegrees                     = yawDegrees;
    candidate.rollDegrees                    = rollDegrees;
    points_[static_cast<std::size_t>(index)] = candidate;
    invalidate();
    return Result<void>::success();
}

Result<void> SplinePath::setPointChunkBreakResult(int index, bool disconnected) {
    if (index <= 0 || index >= pointCount())
        return splineFailure<void>(DiagnosticCode::NotFound, "chunk breaks require a non-first existing spline point",
                                   "point.index");
    if (closed_ && disconnected)
        return splineFailure<void>(DiagnosticCode::PreconditionViolation,
                                   "closed splines cannot contain disconnected chunks", "closed");
    if (points_[static_cast<std::size_t>(index)].breakBefore == disconnected) return Result<void>::success();
    points_[static_cast<std::size_t>(index)].breakBefore = disconnected;
    invalidate();
    return Result<void>::success();
}

Result<void> SplinePath::removePointResult(int index) {
    if (index < 0 || index >= pointCount())
        return splineFailure<void>(DiagnosticCode::NotFound, "spline point index is out of range", "point.index");
    points_.erase(points_.begin() + index);
    invalidate();
    return Result<void>::success();
}

void SplinePath::clear() {
    if (points_.empty()) return;
    points_.clear();
    invalidate();
}

int SplinePath::segmentCount() const noexcept {
    if (points_.size() < 2u) return 0;
    int result = closed_ ? 1 : 0;
    for (std::size_t i = 1; i < points_.size(); ++i)
        if (!points_[i].breakBefore) ++result;
    return result;
}

int SplinePath::chunkCount() const noexcept {
    if (points_.empty()) return 0;
    int result = 1;
    for (std::size_t i = 1; i < points_.size(); ++i)
        if (points_[i].breakBefore) ++result;
    return result;
}

Result<SplinePath> SplinePath::chunkPathResult(int chunk) const {
    if (chunk < 0 || chunk >= chunkCount())
        return splineFailure<SplinePath>(DiagnosticCode::NotFound, "spline chunk index is out of range", "chunk.index");
    SplinePath result;
    result.kind_ = kind_;
    int current  = 0;
    for (std::size_t i = 0; i < points_.size(); ++i) {
        if (i > 0u && points_[i].breakBefore) ++current;
        if (current != chunk) continue;
        auto point        = points_[i];
        point.breakBefore = false;
        result.points_.push_back(point);
    }
    result.closed_   = closed_ && chunkCount() == 1;
    result.revision_ = revision_;
    if (result.segmentCount() == 0)
        return splineFailure<SplinePath>(DiagnosticCode::PreconditionViolation,
                                         "each spline chunk requires at least two points", "chunk.points");
    return Result<SplinePath>::success(std::move(result));
}

std::vector<std::pair<int, int>> SplinePath::segmentEndpoints() const {
    std::vector<std::pair<int, int>> result;
    for (int i = 0; i + 1 < pointCount(); ++i)
        if (!points_[static_cast<std::size_t>(i + 1)].breakBefore) result.emplace_back(i, i + 1);
    if (closed_ && pointCount() > 1) result.emplace_back(pointCount() - 1, 0);
    return result;
}

Result<void> SplinePath::validateReady() const {
    if (segmentCount() == 0)
        return splineFailure<void>(DiagnosticCode::PreconditionViolation, "spline requires at least two points",
                                   "points");
    return Result<void>::success();
}

SplineSample SplinePath::evaluateUnchecked(float t) const {
    t                    = std::clamp(t, 0.f, 1.f);
    const int   segments = segmentCount();
    const float scaled   = t * static_cast<float>(segments);
    const int   segment  = std::min(static_cast<int>(scaled), segments - 1);
    const float u        = segment == segments - 1 && t == 1.f ? 1.f : scaled - static_cast<float>(segment);
    return evaluateSegmentUnchecked(segment, u, t);
}

SplineSample SplinePath::evaluateSegmentUnchecked(int segment, float u, float normalizedDistance) const {
    const auto  endpoints  = segmentEndpoints();
    const int   aIndex     = endpoints[static_cast<std::size_t>(segment)].first;
    const int   bIndex     = endpoints[static_cast<std::size_t>(segment)].second;
    const auto& a          = points_[static_cast<std::size_t>(aIndex)];
    const auto& b          = points_[static_cast<std::size_t>(bIndex)];
    int         chunkIndex = 0;
    for (int i = 1; i <= aIndex; ++i)
        if (points_[static_cast<std::size_t>(i)].breakBefore) ++chunkIndex;
    V3 value{}, tangent{};
    if (kind_ == "linear") {
        value   = add(position(a), mul(sub(position(b), position(a)), u));
        tangent = sub(position(b), position(a));
    } else if (kind_ == "quadraticBezier") {
        const V3    p0 = position(a), p2 = position(b);
        const V3    outgoing = add(p0, {a.outX, a.outY, a.outZ});
        const V3    incoming = add(p2, {b.inX, b.inY, b.inZ});
        const V3    p1       = mul(add(outgoing, incoming), 0.5f);
        const float v        = 1.f - u;
        value                = add(add(mul(p0, v * v), mul(p1, 2.f * v * u)), mul(p2, u * u));
        tangent              = add(mul(sub(p1, p0), 2.f * v), mul(sub(p2, p1), 2.f * u));
    } else if (kind_ == "bezier") {
        const V3    p0 = position(a), p1 = add(p0, {a.outX, a.outY, a.outZ});
        const V3    p3 = position(b), p2 = add(p3, {b.inX, b.inY, b.inZ});
        const float v = 1.f - u;
        value =
            add(add(mul(p0, v * v * v), mul(p1, 3.f * v * v * u)), add(mul(p2, 3.f * v * u * u), mul(p3, u * u * u)));
        tangent = add(add(mul(sub(p1, p0), 3.f * v * v), mul(sub(p2, p1), 6.f * v * u)), mul(sub(p3, p2), 3.f * u * u));
    } else {
        int previous = aIndex;
        if (aIndex > 0 && !points_[static_cast<std::size_t>(aIndex)].breakBefore)
            previous = aIndex - 1;
        else if (closed_ && aIndex == 0)
            previous = pointCount() - 1;
        int next = bIndex;
        if (bIndex + 1 < pointCount() && !points_[static_cast<std::size_t>(bIndex + 1)].breakBefore)
            next = bIndex + 1;
        else if (closed_ && bIndex == pointCount() - 1)
            next = 0;
        const V3    p0 = position(points_[static_cast<std::size_t>(previous)]);
        const V3    p1 = position(a), p2 = position(b);
        const V3    p3 = position(points_[static_cast<std::size_t>(next)]);
        const float u2 = u * u, u3 = u2 * u;
        value   = mul(add(add(mul(p1, 2.f), mul(sub(p2, p0), u)),
                          add(mul(add(add(mul(p0, 2.f), mul(p1, -5.f)), add(mul(p2, 4.f), mul(p3, -1.f))), u2),
                              mul(add(add(mul(p0, -1.f), mul(p1, 3.f)), add(mul(p2, -3.f), p3)), u3))),
                      0.5f);
        tangent = mul(
            add(sub(p2, p0), add(mul(add(add(mul(p0, 2.f), mul(p1, -5.f)), add(mul(p2, 4.f), mul(p3, -1.f))), 2.f * u),
                                 mul(add(add(mul(p0, -1.f), mul(p1, 3.f)), add(mul(p2, -3.f), p3)), 3.f * u2))),
            0.5f);
    }
    const float tangentLength = length(tangent);
    if (tangentLength > 1e-7f)
        tangent = mul(tangent, 1.f / tangentLength);
    else
        tangent = {1.f, 0.f, 0.f};
    return {value.x,
            value.y,
            value.z,
            tangent.x,
            tangent.y,
            tangent.z,
            normalizedDistance,
            std::lerp(a.rollDegrees, b.rollDegrees, u),
            std::lerp(a.scaleX, b.scaleX, u),
            std::lerp(a.scaleY, b.scaleY, u),
            chunkIndex,
            std::lerp(a.pitchDegrees, b.pitchDegrees, u),
            std::lerp(a.yawDegrees, b.yawDegrees, u)};
}

Result<SplineSample> SplinePath::evaluateResult(float t) const {
    auto valid = validateReady();
    if (!valid.ok()) return Result<SplineSample>::failure(valid.status());
    if (!std::isfinite(t))
        return splineFailure<SplineSample>(DiagnosticCode::InvalidArgument, "spline parameter must be finite", "t");
    return Result<SplineSample>::success(evaluateUnchecked(t));
}

Result<void> SplinePath::ensureArcTable(int samplesPerSegment) const {
    auto valid = validateReady();
    if (!valid.ok()) return valid;
    if (samplesPerSegment < 2 || samplesPerSegment > 1024)
        return splineFailure<void>(DiagnosticCode::InvalidArgument, "arc samples per segment must be in [2, 1024]",
                                   "samplesPerSegment");
    if (arcSamplesPerSegment_ == samplesPerSegment && !arcTable_.empty()) return Result<void>::success();
    arcTable_.clear();
    arcTable_.reserve(static_cast<std::size_t>(segmentCount() * samplesPerSegment + chunkCount()));
    float      distance  = 0.f;
    const auto endpoints = segmentEndpoints();
    for (int segment = 0; segment < segmentCount(); ++segment) {
        const bool startsChunk = segment == 0 || endpoints[static_cast<std::size_t>(segment - 1)].second !=
                                                     endpoints[static_cast<std::size_t>(segment)].first;
        auto       previous =
            evaluateSegmentUnchecked(segment, 0.f, static_cast<float>(segment) / static_cast<float>(segmentCount()));
        if (startsChunk) arcTable_.push_back({segment, 0.f, distance});
        for (int sample = 1; sample <= samplesPerSegment; ++sample) {
            const float u       = static_cast<float>(sample) / static_cast<float>(samplesPerSegment);
            const auto  current = evaluateSegmentUnchecked(
                segment, u, (static_cast<float>(segment) + u) / static_cast<float>(segmentCount()));
            distance += length({current.x - previous.x, current.y - previous.y, current.z - previous.z});
            arcTable_.push_back({segment, u, distance});
            previous = current;
        }
    }
    arcSamplesPerSegment_ = samplesPerSegment;
    return Result<void>::success();
}

Result<float> SplinePath::lengthResult(int samplesPerSegment) const {
    auto ready = ensureArcTable(samplesPerSegment);
    if (!ready.ok()) return Result<float>::failure(ready.status());
    return Result<float>::success(arcTable_.back().distance);
}

Result<SplineSample> SplinePath::evaluateDistanceResult(float distance, int samplesPerSegment) const {
    if (!std::isfinite(distance))
        return splineFailure<SplineSample>(DiagnosticCode::InvalidArgument, "spline distance must be finite",
                                           "distance");
    auto ready = ensureArcTable(samplesPerSegment);
    if (!ready.ok()) return Result<SplineSample>::failure(ready.status());
    const float total  = arcTable_.back().distance;
    distance           = std::clamp(distance, 0.f, total);
    const auto upper   = std::lower_bound(arcTable_.begin(), arcTable_.end(), distance,
                                          [](const ArcEntry& entry, float value) { return entry.distance < value; });
    int        segment = 0;
    float      u       = 0.f;
    if (upper == arcTable_.begin()) {
        segment = upper->segment;
        u       = upper->u;
    } else if (upper == arcTable_.end()) {
        segment = arcTable_.back().segment;
        u       = arcTable_.back().u;
    } else {
        const auto& before = *(upper - 1);
        const float span   = upper->distance - before.distance;
        if (before.segment != upper->segment || span <= 1e-7f) {
            segment = upper->segment;
            u       = upper->u;
        } else {
            segment = upper->segment;
            u       = std::lerp(before.u, upper->u, (distance - before.distance) / span);
        }
    }
    auto sample =
        evaluateSegmentUnchecked(segment, u, (static_cast<float>(segment) + u) / static_cast<float>(segmentCount()));
    sample.normalizedDistance = total > 1e-7f ? distance / total : 0.f;
    return Result<SplineSample>::success(sample);
}

Result<SplineSample> SplinePath::closestPointResult(float x, float y, float z, int samplesPerSegment) const {
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z))
        return splineFailure<SplineSample>(DiagnosticCode::InvalidArgument, "closest point query must be finite",
                                           "point");
    auto ready = ensureArcTable(samplesPerSegment);
    if (!ready.ok()) return Result<SplineSample>::failure(ready.status());
    float        bestSquared = 0.f;
    SplineSample best;
    bool         assigned = false;
    for (const auto& entry : arcTable_) {
        auto sample = evaluateSegmentUnchecked(
            entry.segment, entry.u, (static_cast<float>(entry.segment) + entry.u) / static_cast<float>(segmentCount()));
        const float dx = sample.x - x, dy = sample.y - y, dz = sample.z - z;
        const float squared = dx * dx + dy * dy + dz * dz;
        if (!assigned || squared < bestSquared) {
            assigned    = true;
            bestSquared = squared;
            best        = sample;
            best.normalizedDistance =
                arcTable_.back().distance > 1e-7f ? entry.distance / arcTable_.back().distance : 0.f;
        }
    }
    return Result<SplineSample>::success(best);
}

Result<std::vector<SplineFrameSample>> SplinePath::sampleFramesResult(int sampleCount, bool uniformByDistance,
                                                                      float rollDegrees, int samplesPerSegment) const {
    if (sampleCount < 1 || sampleCount > 4096)
        return splineFailure<std::vector<SplineFrameSample>>(DiagnosticCode::InvalidArgument,
                                                             "frame sampleCount must be in [1, 4096]", "sampleCount");
    if (!std::isfinite(rollDegrees))
        return splineFailure<std::vector<SplineFrameSample>>(DiagnosticCode::InvalidArgument,
                                                             "frame roll must be finite", "rollDegrees");
    float totalLength = 0.f;
    if (uniformByDistance) {
        auto measured = lengthResult(samplesPerSegment);
        if (!measured.ok()) return Result<std::vector<SplineFrameSample>>::failure(measured.status());
        totalLength = measured.value();
    } else {
        auto ready = validateReady();
        if (!ready.ok()) return Result<std::vector<SplineFrameSample>>::failure(ready.status());
    }

    std::vector<SplineFrameSample> frames;
    frames.reserve(static_cast<std::size_t>(sampleCount + 1));
    V3 previousTangent{}, previousSide{};
    for (int index = 0; index <= sampleCount; ++index) {
        const float ratio = static_cast<float>(index) / static_cast<float>(sampleCount);
        auto        sampled =
            uniformByDistance ? evaluateDistanceResult(totalLength * ratio, samplesPerSegment) : evaluateResult(ratio);
        if (!sampled.ok()) return Result<std::vector<SplineFrameSample>>::failure(sampled.status());
        const auto sample  = sampled.value();
        const V3   tangent = normalized({sample.tangentX, sample.tangentY, sample.tangentZ});
        V3         side;
        if (index == 0 || sample.chunkIndex != frames.back().sample.chunkIndex) {
            const V3 reference = std::abs(tangent.y) < 0.9f ? V3{0.f, 1.f, 0.f} : V3{1.f, 0.f, 0.f};
            side               = normalized(cross(reference, tangent));
        } else {
            const V3    axis     = cross(previousTangent, tangent);
            const float axisSize = length(axis);
            if (axisSize > 1e-7f) {
                const float angle = std::atan2(axisSize, std::clamp(dot(previousTangent, tangent), -1.f, 1.f));
                side              = rotateAround(previousSide, mul(axis, 1.f / axisSize), angle);
            } else if (dot(previousTangent, tangent) < 0.f) {
                side = mul(previousSide, -1.f);
            } else {
                side = previousSide;
            }
            side = normalized(sub(side, mul(tangent, dot(side, tangent))));
        }
        V3 up = normalized(cross(tangent, side));
        side  = normalized(cross(up, tangent));
        frames.push_back({sample, side.x, side.y, side.z, up.x, up.y, up.z, tangent.x, tangent.y, tangent.z});
        previousTangent = tangent;
        previousSide    = side;
    }

    if (closed_ && frames.size() > 1u) {
        const V3    firstSide{frames.front().sideX, frames.front().sideY, frames.front().sideZ};
        const V3    lastSide{frames.back().sideX, frames.back().sideY, frames.back().sideZ};
        const V3    tangent{frames.front().sample.tangentX, frames.front().sample.tangentY,
                            frames.front().sample.tangentZ};
        const float correction =
            std::atan2(dot(tangent, cross(lastSide, firstSide)), std::clamp(dot(lastSide, firstSide), -1.f, 1.f));
        for (int index = 1; index <= sampleCount; ++index) {
            auto&       frame = frames[static_cast<std::size_t>(index)];
            const float ratio = static_cast<float>(index) / static_cast<float>(sampleCount);
            const V3    currentTangent{frame.sample.tangentX, frame.sample.tangentY, frame.sample.tangentZ};
            V3 side     = rotateAround({frame.sideX, frame.sideY, frame.sideZ}, currentTangent, correction * ratio);
            V3 up       = normalized(cross(currentTangent, side));
            side        = normalized(cross(up, currentTangent));
            frame.sideX = side.x;
            frame.sideY = side.y;
            frame.sideZ = side.z;
            frame.upX   = up.x;
            frame.upY   = up.y;
            frame.upZ   = up.z;
        }
        frames.back()                           = frames.front();
        frames.back().sample.normalizedDistance = 1.f;
    }

    for (auto& frame : frames) {
        V3          side{frame.sideX, frame.sideY, frame.sideZ};
        V3          up{frame.upX, frame.upY, frame.upZ};
        V3          forward{frame.sample.tangentX, frame.sample.tangentY, frame.sample.tangentZ};
        const float yaw = frame.sample.yawDegrees * 0.01745329251994329577f;
        if (std::abs(yaw) > 1e-7f) {
            forward = rotateAround(forward, up, yaw);
            side    = rotateAround(side, up, yaw);
        }
        const float pitch = frame.sample.pitchDegrees * 0.01745329251994329577f;
        if (std::abs(pitch) > 1e-7f) {
            forward = rotateAround(forward, side, pitch);
            up      = rotateAround(up, side, pitch);
        }
        const float roll = (rollDegrees + frame.sample.rollDegrees) * 0.01745329251994329577f;
        if (std::abs(roll) > 1e-7f) {
            const float c = std::cos(roll), s = std::sin(roll);
            const V3    rolledSide = add(mul(side, c), mul(up, s));
            up                     = add(mul(up, c), mul(side, -s));
            side                   = rolledSide;
        }
        frame.sideX    = side.x;
        frame.sideY    = side.y;
        frame.sideZ    = side.z;
        frame.upX      = up.x;
        frame.upY      = up.y;
        frame.upZ      = up.z;
        frame.forwardX = forward.x;
        frame.forwardY = forward.y;
        frame.forwardZ = forward.z;
    }
    return Result<std::vector<SplineFrameSample>>::success(std::move(frames));
}

Result<SplineSample> SplineDistribution::sampleResult(int index) const {
    if (index < 0 || index >= count())
        return splineFailure<SplineSample>(DiagnosticCode::NotFound, "spline distribution index is out of range",
                                           "distribution.index");
    return Result<SplineSample>::success(frames_[static_cast<std::size_t>(index)].sample);
}

Result<SplineFrameSample> SplineDistribution::frameResult(int index) const {
    if (index < 0 || index >= count())
        return splineFailure<SplineFrameSample>(DiagnosticCode::NotFound, "spline distribution index is out of range",
                                                "distribution.index");
    return Result<SplineFrameSample>::success(frames_[static_cast<std::size_t>(index)]);
}

int SplinePolyline::count() const noexcept {
    int result = 0;
    for (const auto& chunk : chunks_) result += static_cast<int>(chunk.size());
    return result;
}

int SplinePolyline::chunkPointCount(int chunk) const noexcept {
    return chunk < 0 || chunk >= chunkCount() ? 0 : static_cast<int>(chunks_[static_cast<std::size_t>(chunk)].size());
}

Result<SplineSample> SplinePolyline::chunkPointResult(int chunk, int index) const {
    if (chunk < 0 || chunk >= chunkCount() || index < 0 || index >= chunkPointCount(chunk))
        return splineFailure<SplineSample>(DiagnosticCode::NotFound, "spline polyline index is out of range",
                                           "polyline.index");
    return Result<SplineSample>::success(chunks_[static_cast<std::size_t>(chunk)][static_cast<std::size_t>(index)]);
}

Result<SplineSample> SplinePolyline::pointResult(int index) const {
    if (index >= 0)
        for (const auto& chunk : chunks_) {
            if (index < static_cast<int>(chunk.size()))
                return Result<SplineSample>::success(chunk[static_cast<std::size_t>(index)]);
            index -= static_cast<int>(chunk.size());
        }
    return splineFailure<SplineSample>(DiagnosticCode::NotFound, "spline polyline index is out of range",
                                       "polyline.index");
}

Result<SplineSample> SplinePath::travelResult(float distance, std::string_view wrapMode, int samplesPerSegment) const {
    if (!std::isfinite(distance))
        return splineFailure<SplineSample>(DiagnosticCode::InvalidArgument, "spline travel distance must be finite",
                                           "distance");
    if (wrapMode != "clamp" && wrapMode != "loop" && wrapMode != "pingPong")
        return splineFailure<SplineSample>(DiagnosticCode::InvalidArgument,
                                           "spline travel wrap mode must be clamp, loop, or pingPong", "wrapMode");
    auto measured = lengthResult(samplesPerSegment);
    if (!measured.ok()) return Result<SplineSample>::failure(measured.status());
    const float total    = measured.value();
    float       resolved = distance;
    if (wrapMode == "loop" && total > 1e-7f) {
        resolved = std::fmod(distance, total);
        if (resolved < 0.f) resolved += total;
    } else if (wrapMode == "pingPong" && total > 1e-7f) {
        const float period = total * 2.f;
        resolved           = std::fmod(distance, period);
        if (resolved < 0.f) resolved += period;
        if (resolved > total) resolved = period - resolved;
    }
    return evaluateDistanceResult(resolved, samplesPerSegment);
}

Result<SplineFrameSample> SplinePath::travelFrameResult(float distance, std::string_view wrapMode,
                                                        int samplesPerSegment) const {
    auto sample = travelResult(distance, wrapMode, samplesPerSegment);
    if (!sample.ok()) return Result<SplineFrameSample>::failure(sample.status());
    const int frameIntervals = std::clamp(segmentCount() * samplesPerSegment, 1, 4096);
    auto      frames         = sampleFramesResult(frameIntervals, true, 0.f, samplesPerSegment);
    if (!frames.ok()) return Result<SplineFrameSample>::failure(frames.status());
    const float location   = sample.value().normalizedDistance * static_cast<float>(frameIntervals);
    const int   lowerIndex = std::clamp(static_cast<int>(std::floor(location)), 0, frameIntervals);
    const int   upperIndex = std::min(lowerIndex + 1, frameIntervals);
    const float blend      = location - static_cast<float>(lowerIndex);
    const auto& lower      = frames.value()[static_cast<std::size_t>(lowerIndex)];
    const auto& upper      = frames.value()[static_cast<std::size_t>(upperIndex)];
    V3          forward =
        normalized({std::lerp(lower.forwardX, upper.forwardX, blend), std::lerp(lower.forwardY, upper.forwardY, blend),
                    std::lerp(lower.forwardZ, upper.forwardZ, blend)});
    V3 side     = normalized({std::lerp(lower.sideX, upper.sideX, blend), std::lerp(lower.sideY, upper.sideY, blend),
                              std::lerp(lower.sideZ, upper.sideZ, blend)});
    side        = normalized(sub(side, mul(forward, dot(side, forward))));
    const V3 up = normalized(cross(forward, side));
    side        = normalized(cross(up, forward));
    return Result<SplineFrameSample>::success(
        {sample.value(), side.x, side.y, side.z, up.x, up.y, up.z, forward.x, forward.y, forward.z});
}

Result<SplineDistribution> SplinePath::distributeResult(int instanceCount, bool includeEnd,
                                                        int samplesPerSegment) const {
    if (instanceCount < 1 || instanceCount > 100000)
        return splineFailure<SplineDistribution>(DiagnosticCode::InvalidArgument,
                                                 "spline instanceCount must be in [1, 100000]", "instanceCount");
    const bool duplicateEnd = includeEnd && !closed_ && instanceCount > 1;
    const int  intervals    = duplicateEnd ? instanceCount - 1 : instanceCount;
    auto       frames       = sampleFramesResult(std::max(1, intervals), true, 0.f, samplesPerSegment);
    if (!frames.ok()) return Result<SplineDistribution>::failure(frames.status());
    frames.value().resize(static_cast<std::size_t>(instanceCount));
    return Result<SplineDistribution>::success(SplineDistribution(std::move(frames).takeValue()));
}

Result<SplinePolyline> SplinePath::polylineResult(int sampleCount, bool uniformByDistance,
                                                  int samplesPerSegment) const {
    if (sampleCount < 2 || sampleCount > 65536)
        return splineFailure<SplinePolyline>(DiagnosticCode::InvalidArgument,
                                             "spline polyline sampleCount must be in [2, 65536]", "sampleCount");
    if (sampleCount < chunkCount() * 2)
        return splineFailure<SplinePolyline>(DiagnosticCode::InvalidArgument,
                                             "spline polyline requires at least two samples per chunk", "sampleCount");
    std::vector<std::vector<SplineSample>> chunks;
    chunks.reserve(static_cast<std::size_t>(chunkCount()));
    int remaining = sampleCount;
    for (int chunk = 0; chunk < chunkCount(); ++chunk) {
        auto path = chunkPathResult(chunk);
        if (!path.ok()) return Result<SplinePolyline>::failure(path.status());
        const int chunksLeft   = chunkCount() - chunk;
        const int maximum      = remaining - (chunksLeft - 1) * 2;
        const int proposed     = sampleCount * path.value().segmentCount() / segmentCount();
        const int chunkSamples = chunksLeft == 1 ? remaining : std::clamp(proposed, 2, maximum);
        remaining -= chunkSamples;
        auto measured = uniformByDistance ? path.value().lengthResult(samplesPerSegment) : Result<float>::success(0.f);
        if (!measured.ok()) return Result<SplinePolyline>::failure(measured.status());
        std::vector<SplineSample> points;
        points.reserve(static_cast<std::size_t>(chunkSamples));
        const int denominator = path.value().isClosed() ? chunkSamples : chunkSamples - 1;
        for (int index = 0; index < chunkSamples; ++index) {
            const float ratio  = static_cast<float>(index) / static_cast<float>(denominator);
            auto        sample = uniformByDistance
                                     ? path.value().evaluateDistanceResult(measured.value() * ratio, samplesPerSegment)
                                     : path.value().evaluateResult(ratio);
            if (!sample.ok()) return Result<SplinePolyline>::failure(sample.status());
            points.push_back(sample.value());
        }
        chunks.push_back(std::move(points));
    }
    return Result<SplinePolyline>::success(SplinePolyline(std::move(chunks), closed_));
}

Result<void> SplinePath::applyShapePresetResult(std::string_view preset, int pointCount, float radius, float height,
                                                float turns) {
    if (preset != "line" && preset != "circle" && preset != "arc" && preset != "spiral" && preset != "wave")
        return splineFailure<void>(DiagnosticCode::InvalidArgument,
                                   "spline preset must be line, circle, arc, spiral, or wave", "preset");
    if (pointCount < 2 || pointCount > 4096 || (preset == "circle" && pointCount < 3))
        return splineFailure<void>(DiagnosticCode::InvalidArgument, "spline preset pointCount is invalid",
                                   "pointCount");
    if (!std::isfinite(radius) || !std::isfinite(height) || !std::isfinite(turns) || radius <= 0.f || turns <= 0.f)
        return splineFailure<void>(DiagnosticCode::InvalidArgument,
                                   "spline preset requires finite positive radius and turns", "preset.parameters");
    constexpr float          tau = 6.28318530717958647692f;
    std::vector<SplinePoint> candidate;
    candidate.reserve(static_cast<std::size_t>(pointCount));
    for (int index = 0; index < pointCount; ++index) {
        const float t = pointCount > 1 ? static_cast<float>(index) / static_cast<float>(pointCount - 1) : 0.f;
        SplinePoint point;
        if (preset == "line") {
            point = {-radius + 2.f * radius * t, height * t, 0.f};
        } else if (preset == "circle") {
            const float angle = tau * static_cast<float>(index) / static_cast<float>(pointCount);
            point             = {std::cos(angle) * radius, 0.f, std::sin(angle) * radius};
        } else if (preset == "arc") {
            const float angle = (-0.25f + 0.5f * t) * tau * turns;
            point             = {std::cos(angle) * radius, height * t, std::sin(angle) * radius};
        } else if (preset == "spiral") {
            const float angle = tau * turns * t;
            const float ring  = radius * t;
            point             = {std::cos(angle) * ring, height * t, std::sin(angle) * ring};
        } else {
            point = {-radius + 2.f * radius * t, std::sin(tau * turns * t) * height, 0.f};
        }
        candidate.push_back(point);
    }
    points_ = std::move(candidate);
    kind_   = preset == "line" ? "linear" : "catmullRom";
    closed_ = preset == "circle";
    invalidate();
    return Result<void>::success();
}

void SplinePath::invalidate() {
    ++revision_;
    arcTable_.clear();
    arcSamplesPerSegment_ = 0;
}

}  // namespace eve::procgen
