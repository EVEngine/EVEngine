#include "procgen/editing/SplinePathDocument.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <utility>

namespace eve::procgen_editing {
namespace {

template <class T>
EditorResult<T> splineError(EditorStatus status, const char* rule, std::string message) {
    return eve::editing::failed<T>(status, RuleId(rule), std::move(message));
}

const EditorValue* field(const EditorValue& value, const char* key) {
    const auto* object = value.getIf<EditorValue::Object>();
    if (!object) return nullptr;
    const auto found = object->find(key);
    return found == object->end() ? nullptr : &found->second;
}

EditorValue pointValue(const SplinePathControlPoint& point) {
    return EditorValue::Object{{"id", point.id.value()},
                               {"order", point.order},
                               {"x", point.x},
                               {"y", point.y},
                               {"z", point.z},
                               {"inX", point.inX},
                               {"inY", point.inY},
                               {"inZ", point.inZ},
                               {"outX", point.outX},
                               {"outY", point.outY},
                               {"outZ", point.outZ},
                               {"rollDegrees", point.rollDegrees},
                               {"scaleX", point.scaleX},
                               {"scaleY", point.scaleY},
                               {"breakBefore", point.breakBefore},
                               {"pitchDegrees", point.pitchDegrees},
                               {"yawDegrees", point.yawDegrees}};
}

EditorValue settingsValue(const SplinePathSettings& settings) {
    return EditorValue::Object{{"kind", settings.kind}, {"closed", settings.closed}};
}

EditorResult<SplinePathControlPoint> parsePoint(const EditorValue& value) {
    const auto string = [&](const char* name) {
        const auto* entry = field(value, name);
        return entry ? entry->getIf<std::string>() : nullptr;
    };
    const auto integer = [&](const char* name) {
        const auto* entry = field(value, name);
        return entry ? entry->getIf<std::int64_t>() : nullptr;
    };
    const auto number = [&](const char* name) {
        const auto* entry = field(value, name);
        return entry ? entry->getIf<double>() : nullptr;
    };
    const auto*  id             = string("id");
    const auto*  order          = integer("order");
    const auto*  x              = number("x");
    const auto*  y              = number("y");
    const auto*  z              = number("z");
    const auto*  inX            = number("inX");
    const auto*  inY            = number("inY");
    const auto*  inZ            = number("inZ");
    const auto*  outX           = number("outX");
    const auto*  outY           = number("outY");
    const auto*  outZ           = number("outZ");
    const auto*  rollValue      = field(value, "rollDegrees");
    const auto*  scaleXValue    = field(value, "scaleX");
    const auto*  scaleYValue    = field(value, "scaleY");
    const auto*  breakValue     = field(value, "breakBefore");
    const auto*  pitchValue     = field(value, "pitchDegrees");
    const auto*  yawValue       = field(value, "yawDegrees");
    const auto*  roll           = rollValue ? rollValue->getIf<double>() : nullptr;
    const auto*  scaleX         = scaleXValue ? scaleXValue->getIf<double>() : nullptr;
    const auto*  scaleY         = scaleYValue ? scaleYValue->getIf<double>() : nullptr;
    const double resolvedRoll   = roll ? *roll : 0.0;
    const double resolvedScaleX = scaleX ? *scaleX : 1.0;
    const double resolvedScaleY = scaleY ? *scaleY : 1.0;
    const auto*  breakBefore    = breakValue ? breakValue->getIf<bool>() : nullptr;
    const auto*  pitch          = pitchValue ? pitchValue->getIf<double>() : nullptr;
    const auto*  yaw            = yawValue ? yawValue->getIf<double>() : nullptr;
    const double resolvedPitch  = pitch ? *pitch : 0.0;
    const double resolvedYaw    = yaw ? *yaw : 0.0;
    if (!id || id->empty() || !order || !x || !y || !z || !inX || !inY || !inZ || !outX || !outY || !outZ ||
        !std::isfinite(*x) || !std::isfinite(*y) || !std::isfinite(*z) || !std::isfinite(*inX) ||
        !std::isfinite(*inY) || !std::isfinite(*inZ) || !std::isfinite(*outX) || !std::isfinite(*outY) ||
        !std::isfinite(*outZ) || !std::isfinite(resolvedRoll) || !std::isfinite(resolvedScaleX) ||
        !std::isfinite(resolvedScaleY) || !std::isfinite(resolvedPitch) || !std::isfinite(resolvedYaw) ||
        resolvedScaleX <= 0.0 || resolvedScaleY <= 0.0)
        return splineError<SplinePathControlPoint>(EditorStatus::Rejected, "editor.spline.invalid-point",
                                                   "Spline point requires a stable id, order and finite coordinates");
    return eve::editing::applied<SplinePathControlPoint>(
        {StableId(*id), *order, *x, *y, *z, *inX, *inY, *inZ, *outX, *outY, *outZ, resolvedRoll, resolvedScaleX,
         resolvedScaleY, breakBefore ? *breakBefore : false, resolvedPitch, resolvedYaw});
}

EditorResult<SplinePathSettings> parseSettings(const EditorValue& value) {
    const auto*                 kindValue   = field(value, "kind");
    const auto*                 closedValue = field(value, "closed");
    const auto*                 kind        = kindValue ? kindValue->getIf<std::string>() : nullptr;
    const auto*                 closed      = closedValue ? closedValue->getIf<bool>() : nullptr;
    const std::set<std::string> kinds{"linear", "catmullRom", "bezier"};
    if (!kind || !kinds.contains(*kind) || !closed)
        return splineError<SplinePathSettings>(EditorStatus::Rejected, "editor.spline.invalid-settings",
                                               "Spline settings require a supported kind and closure flag");
    return eve::editing::applied<SplinePathSettings>({*kind, *closed});
}

DomainOperation operation(const char* type, const char* inverseType, const std::string& target, EditorValue payload,
                          EditorValue inverse, const StableId& affected) {
    DomainOperation result;
    result.type        = type;
    result.inverseType = inverseType;
    result.target      = TargetId(target);
    result.payload     = std::move(payload);
    result.inverse     = std::move(inverse);
    result.hasInverse  = true;
    result.affectedObjects.push_back({TargetId(target), affected.value(), 0});
    return result;
}

}  // namespace

SplinePathDocument::SplinePathDocument(std::string id) : id_(std::move(id)) {}

TargetDescriptor SplinePathDocument::describe() const {
    return {TargetId(id_),
            "spline-path-document",
            revision_,
            false,
            {ISplinePathDocumentEditTarget::editingCapabilityId()}};
}

void* SplinePathDocument::queryCapability(const CapabilityId& capability) {
    return capability == ISplinePathDocumentEditTarget::editingCapabilityId()
               ? static_cast<ISplinePathDocumentEditTarget*>(this)
               : nullptr;
}

EditorResult<void> SplinePathDocument::applyDomainOperation(const DomainOperation& value) {
    if (value.target != TargetId(id_))
        return splineError<void>(EditorStatus::Rejected, "editor.spline.target-mismatch",
                                 "Spline operation targets another document");
    if (value.type == "spline.snapshot.replace.v1") {
        SplinePathDocument candidate(id_);
        auto               loaded = candidate.loadSnapshot(value.payload);
        if (!loaded.ok())
            return splineError<void>(loaded.code(), "editor.spline.invalid-replacement", loaded.status().describe());
        candidate.revision_ = revision_ + 1;
        candidate.dirty_.include(0, 0);
        *this = std::move(candidate);
        return eve::editing::applied<void>();
    }
    if (value.type == "spline.point.set.v1") {
        auto parsed = parsePoint(value.payload);
        if (!parsed.ok())
            return splineError<void>(parsed.code(), "editor.spline.invalid-point", "Invalid spline point");
        points_[parsed.value().id] = std::move(parsed.value());
    } else if (value.type == "spline.point.delete.v1") {
        auto parsed = parsePoint(value.payload);
        if (!parsed.ok() || !points_.erase(parsed.value().id))
            return splineError<void>(EditorStatus::NotFound, "editor.spline.point-not-found",
                                     "Spline point was not found");
    } else if (value.type == "spline.settings.set.v1") {
        auto parsed = parseSettings(value.payload);
        if (!parsed.ok())
            return splineError<void>(parsed.code(), "editor.spline.invalid-settings", "Invalid spline settings");
        settings_ = std::move(parsed.value());
    } else {
        return splineError<void>(EditorStatus::Unsupported, "editor.spline.operation-unsupported",
                                 "Spline operation is unsupported");
    }
    ++revision_;
    dirty_.include(0, 0);
    return eve::editing::applied<void>();
}

EditorResult<DomainOperation> SplinePathDocument::makeInsertPointAt(int segment, double t,
                                                                    const StableId& point) const {
    const auto                       ordered = points();
    std::vector<std::pair<int, int>> segments;
    for (int i = 0; i + 1 < static_cast<int>(ordered.size()); ++i)
        if (!ordered[static_cast<std::size_t>(i + 1)].breakBefore) segments.emplace_back(i, i + 1);
    if (settings_.closed && ordered.size() > 1u) segments.emplace_back(static_cast<int>(ordered.size()) - 1, 0);
    const int segmentCount = static_cast<int>(segments.size());
    if (point.empty() || points_.contains(point) || segment < 0 || segment >= segmentCount || !std::isfinite(t) ||
        t <= 0.0 || t >= 1.0)
        return splineError<DomainOperation>(EditorStatus::Rejected, "editor.spline.insert-invalid",
                                            "Spline insertion requires a unique id, valid segment and t in (0,1)");
    auto runtime = compilePath();
    if (!runtime.ok())
        return splineError<DomainOperation>(runtime.code(), "editor.spline.insert-incomplete",
                                            runtime.status().describe());
    auto sampled = runtime.value().evaluateResult((static_cast<float>(segment) + static_cast<float>(t)) /
                                                  static_cast<float>(segmentCount));
    if (!sampled.ok())
        return splineError<DomainOperation>(EditorStatus::Rejected, "editor.spline.insert-sample-failed",
                                            sampled.status().describe());
    SplinePathDocument     candidate(*this);
    auto                   candidatePoints = ordered;
    const int              firstIndex      = segments[static_cast<std::size_t>(segment)].first;
    const int              nextIndex       = segments[static_cast<std::size_t>(segment)].second;
    SplinePathControlPoint inserted;
    inserted.id          = point;
    inserted.x           = sampled.value().x;
    inserted.y           = sampled.value().y;
    inserted.z           = sampled.value().z;
    inserted.rollDegrees = sampled.value().rollDegrees;
    inserted.scaleX      = sampled.value().scaleX;
    inserted.scaleY      = sampled.value().scaleY;
    if (settings_.kind == "bezier") {
        auto&        first  = candidatePoints[static_cast<std::size_t>(firstIndex)];
        auto&        second = candidatePoints[static_cast<std::size_t>(nextIndex)];
        const auto   lerp   = [t](double a, double b) { return a + (b - a) * t; };
        const double q0x = lerp(first.x, first.x + first.outX), q0y = lerp(first.y, first.y + first.outY),
                     q0z = lerp(first.z, first.z + first.outZ);
        const double q1x = lerp(first.x + first.outX, second.x + second.inX),
                     q1y = lerp(first.y + first.outY, second.y + second.inY),
                     q1z = lerp(first.z + first.outZ, second.z + second.inZ);
        const double q2x = lerp(second.x + second.inX, second.x), q2y = lerp(second.y + second.inY, second.y),
                     q2z = lerp(second.z + second.inZ, second.z);
        const double r0x = lerp(q0x, q1x), r0y = lerp(q0y, q1y), r0z = lerp(q0z, q1z);
        const double r1x = lerp(q1x, q2x), r1y = lerp(q1y, q2y), r1z = lerp(q1z, q2z);
        inserted.x    = lerp(r0x, r1x);
        inserted.y    = lerp(r0y, r1y);
        inserted.z    = lerp(r0z, r1z);
        first.outX    = q0x - first.x;
        first.outY    = q0y - first.y;
        first.outZ    = q0z - first.z;
        inserted.inX  = r0x - inserted.x;
        inserted.inY  = r0y - inserted.y;
        inserted.inZ  = r0z - inserted.z;
        inserted.outX = r1x - inserted.x;
        inserted.outY = r1y - inserted.y;
        inserted.outZ = r1z - inserted.z;
        second.inX    = q2x - second.x;
        second.inY    = q2y - second.y;
        second.inZ    = q2z - second.z;
    }
    const int insertionIndex = nextIndex == 0 ? static_cast<int>(candidatePoints.size()) : nextIndex;
    candidatePoints.insert(candidatePoints.begin() + insertionIndex, inserted);
    candidate.points_.clear();
    for (std::size_t i = 0; i < candidatePoints.size(); ++i) {
        candidatePoints[i].order                 = static_cast<std::int64_t>(i);
        candidate.points_[candidatePoints[i].id] = candidatePoints[i];
    }
    return eve::editing::applied<DomainOperation>(operation("spline.snapshot.replace.v1", "spline.snapshot.replace.v1",
                                                            id_, candidate.snapshotValue(), snapshotValue(), point));
}

EditorResult<DomainOperation> SplinePathDocument::makeSnapPoint(const StableId& point, double gridSize) const {
    const auto found = points_.find(point);
    if (found == points_.end())
        return splineError<DomainOperation>(EditorStatus::NotFound, "editor.spline.snap-point-missing",
                                            "Spline point was not found");
    if (!std::isfinite(gridSize) || gridSize <= 0.0)
        return splineError<DomainOperation>(EditorStatus::Rejected, "editor.spline.snap-grid-invalid",
                                            "Spline snap grid size must be finite and positive");
    auto snapped = found->second;
    snapped.x    = std::round(snapped.x / gridSize) * gridSize;
    snapped.y    = std::round(snapped.y / gridSize) * gridSize;
    snapped.z    = std::round(snapped.z / gridSize) * gridSize;
    return makeSetPoint(snapped);
}

EditorResult<DomainOperation> SplinePathDocument::makeSnapAll(double gridSize) const {
    if (!std::isfinite(gridSize) || gridSize <= 0.0)
        return splineError<DomainOperation>(EditorStatus::Rejected, "editor.spline.snap-grid-invalid",
                                            "Spline snap grid size must be finite and positive");
    SplinePathDocument candidate(*this);
    for (auto& [unused, point] : candidate.points_) {
        (void)unused;
        point.x = std::round(point.x / gridSize) * gridSize;
        point.y = std::round(point.y / gridSize) * gridSize;
        point.z = std::round(point.z / gridSize) * gridSize;
    }
    return eve::editing::applied<DomainOperation>(operation("spline.snapshot.replace.v1", "spline.snapshot.replace.v1",
                                                            id_, candidate.snapshotValue(), snapshotValue(),
                                                            StableId("snap-all")));
}

EditorResult<DomainOperation> SplinePathDocument::makeFlipDirection() const {
    if (points_.size() < 2u)
        return splineError<DomainOperation>(EditorStatus::Rejected, "editor.spline.flip-incomplete",
                                            "Spline direction requires at least two points");
    std::vector<std::vector<SplinePathControlPoint>> chunks(1);
    for (const auto& point : points()) {
        if (point.breakBefore) chunks.emplace_back();
        chunks.back().push_back(point);
    }
    std::reverse(chunks.begin(), chunks.end());
    std::vector<SplinePathControlPoint> reversed;
    for (auto& chunk : chunks) {
        std::reverse(chunk.begin(), chunk.end());
        for (std::size_t i = 0; i < chunk.size(); ++i) {
            chunk[i].breakBefore = !reversed.empty() && i == 0;
            reversed.push_back(chunk[i]);
        }
    }
    SplinePathDocument candidate(*this);
    candidate.points_.clear();
    for (std::size_t i = 0; i < reversed.size(); ++i) {
        auto& point = reversed[i];
        point.order = static_cast<std::int64_t>(i);
        std::swap(point.inX, point.outX);
        std::swap(point.inY, point.outY);
        std::swap(point.inZ, point.outZ);
        candidate.points_[point.id] = point;
    }
    return eve::editing::applied<DomainOperation>(operation("spline.snapshot.replace.v1", "spline.snapshot.replace.v1",
                                                            id_, candidate.snapshotValue(), snapshotValue(),
                                                            reversed.front().id));
}

EditorResult<DomainOperation> SplinePathDocument::makeSetChunkBreak(const StableId& point, bool disconnected) const {
    if (settings_.closed && disconnected)
        return splineError<DomainOperation>(EditorStatus::Rejected, "editor.spline.chunk-break-closed",
                                            "Closed splines cannot contain disconnected chunks");
    auto       ordered = points();
    const auto found =
        std::find_if(ordered.begin(), ordered.end(), [&](const auto& value) { return value.id == point; });
    if (found == ordered.end() || found == ordered.begin())
        return splineError<DomainOperation>(EditorStatus::NotFound, "editor.spline.chunk-break-point-invalid",
                                            "Chunk boundary requires an existing non-first point");
    const auto index = static_cast<std::size_t>(found - ordered.begin());
    if (disconnected && !found->breakBefore) {
        std::size_t chunkStart = index;
        while (chunkStart > 0u && !ordered[chunkStart].breakBefore) --chunkStart;
        std::size_t chunkEnd = index;
        while (chunkEnd + 1u < ordered.size() && !ordered[chunkEnd + 1u].breakBefore) ++chunkEnd;
        if (index - chunkStart < 2u || chunkEnd - index + 1u < 2u)
            return splineError<DomainOperation>(EditorStatus::Rejected, "editor.spline.chunk-break-singleton",
                                                "Splitting must leave at least two points in each chunk");
    }
    auto updated        = *found;
    updated.breakBefore = disconnected;
    return makeSetPoint(updated);
}

EditorResult<DomainOperation> SplinePathDocument::makeAppendChunk(const SplinePathControlPoint& first,
                                                                  const SplinePathControlPoint& second) const {
    if (settings_.closed)
        return splineError<DomainOperation>(EditorStatus::Rejected, "editor.spline.append-chunk-closed",
                                            "Closed splines cannot append disconnected chunks");
    if (first.id.empty() || second.id.empty() || first.id == second.id || points_.contains(first.id) ||
        points_.contains(second.id))
        return splineError<DomainOperation>(EditorStatus::Rejected, "editor.spline.append-chunk-id-invalid",
                                            "Appended chunk points require two new distinct stable ids");
    auto parsedFirst  = parsePoint(pointValue(first));
    auto parsedSecond = parsePoint(pointValue(second));
    if (!parsedFirst.ok() || !parsedSecond.ok())
        return splineError<DomainOperation>(EditorStatus::Rejected, "editor.spline.append-chunk-point-invalid",
                                            "Appended chunk points must be finite and valid");
    SplinePathDocument candidate(*this);
    auto               ordered           = candidate.points();
    auto               appendedFirst     = parsedFirst.value();
    auto               appendedSecond    = parsedSecond.value();
    appendedFirst.order                  = static_cast<std::int64_t>(ordered.size());
    appendedFirst.breakBefore            = !ordered.empty();
    appendedSecond.order                 = appendedFirst.order + 1;
    appendedSecond.breakBefore           = false;
    candidate.points_[appendedFirst.id]  = appendedFirst;
    candidate.points_[appendedSecond.id] = appendedSecond;
    return eve::editing::applied<DomainOperation>(operation("spline.snapshot.replace.v1", "spline.snapshot.replace.v1",
                                                            id_, candidate.snapshotValue(), snapshotValue(),
                                                            appendedFirst.id));
}

EditorResult<DomainOperation> SplinePathDocument::makeDeleteChunk(int chunk) const {
    if (chunk < 0)
        return splineError<DomainOperation>(EditorStatus::NotFound, "editor.spline.chunk-not-found",
                                            "Spline chunk was not found");
    auto                                             ordered = points();
    std::vector<std::vector<SplinePathControlPoint>> chunks;
    for (const auto& point : ordered) {
        if (chunks.empty() || point.breakBefore) chunks.emplace_back();
        chunks.back().push_back(point);
    }
    if (static_cast<std::size_t>(chunk) >= chunks.size())
        return splineError<DomainOperation>(EditorStatus::NotFound, "editor.spline.chunk-not-found",
                                            "Spline chunk was not found");
    chunks.erase(chunks.begin() + chunk);
    SplinePathDocument candidate(*this);
    candidate.points_.clear();
    std::int64_t order = 0;
    for (std::size_t chunkIndex = 0; chunkIndex < chunks.size(); ++chunkIndex) {
        for (std::size_t pointIndex = 0; pointIndex < chunks[chunkIndex].size(); ++pointIndex) {
            auto point                  = chunks[chunkIndex][pointIndex];
            point.order                 = order++;
            point.breakBefore           = chunkIndex > 0u && pointIndex == 0u;
            candidate.points_[point.id] = point;
        }
    }
    return eve::editing::applied<DomainOperation>(operation("spline.snapshot.replace.v1", "spline.snapshot.replace.v1",
                                                            id_, candidate.snapshotValue(), snapshotValue(),
                                                            StableId("delete-chunk")));
}

EditorResult<DomainOperation> SplinePathDocument::makeSetPointRotation(const StableId& point, double pitchDegrees,
                                                                       double yawDegrees, double rollDegrees) const {
    const auto found = points_.find(point);
    if (found == points_.end())
        return splineError<DomainOperation>(EditorStatus::NotFound, "editor.spline.rotation-point-missing",
                                            "Spline point was not found");
    if (!std::isfinite(pitchDegrees) || !std::isfinite(yawDegrees) || !std::isfinite(rollDegrees))
        return splineError<DomainOperation>(EditorStatus::Rejected, "editor.spline.rotation-invalid",
                                            "Spline point rotation must be finite");
    auto updated         = found->second;
    updated.pitchDegrees = pitchDegrees;
    updated.yawDegrees   = yawDegrees;
    updated.rollDegrees  = rollDegrees;
    return makeSetPoint(updated);
}

EditorResult<DomainOperation> SplinePathDocument::makeResetPointRotation(const StableId& point) const {
    return makeSetPointRotation(point, 0.0, 0.0, 0.0);
}

EditorResult<DomainOperation> SplinePathDocument::makeCenterPoint(const StableId& point) const {
    const auto ordered = points();
    const auto found =
        std::find_if(ordered.begin(), ordered.end(), [&](const auto& value) { return value.id == point; });
    if (found == ordered.end())
        return splineError<DomainOperation>(EditorStatus::NotFound, "editor.spline.center-point-missing",
                                            "Spline point was not found");
    const auto index = static_cast<std::size_t>(found - ordered.begin());
    if (index == 0u || index + 1u >= ordered.size() || found->breakBefore || ordered[index + 1u].breakBefore)
        return splineError<DomainOperation>(EditorStatus::Rejected, "editor.spline.center-point-endpoint",
                                            "Only an interior point of one connected chunk can be centered");
    auto updated = *found;
    updated.x    = (ordered[index - 1u].x + ordered[index + 1u].x) * 0.5;
    updated.y    = (ordered[index - 1u].y + ordered[index + 1u].y) * 0.5;
    updated.z    = (ordered[index - 1u].z + ordered[index + 1u].z) * 0.5;
    return makeSetPoint(updated);
}

EditorResult<DomainOperation> SplinePathDocument::makeMirrorAxis(std::string_view axis) const {
    if (axis != "x" && axis != "y" && axis != "z")
        return splineError<DomainOperation>(EditorStatus::Rejected, "editor.spline.mirror-axis-invalid",
                                            "Spline mirror axis must be x, y, or z");
    SplinePathDocument candidate(*this);
    for (auto& [unused, point] : candidate.points_) {
        (void)unused;
        double* position = axis == "x" ? &point.x : axis == "y" ? &point.y : &point.z;
        double* incoming = axis == "x" ? &point.inX : axis == "y" ? &point.inY : &point.inZ;
        double* outgoing = axis == "x" ? &point.outX : axis == "y" ? &point.outY : &point.outZ;
        *position        = -*position;
        *incoming        = -*incoming;
        *outgoing        = -*outgoing;
    }
    return eve::editing::applied<DomainOperation>(operation("spline.snapshot.replace.v1", "spline.snapshot.replace.v1",
                                                            id_, candidate.snapshotValue(), snapshotValue(),
                                                            StableId(std::string("mirror-") + std::string(axis))));
}

std::unique_ptr<IDomainOperationTarget> SplinePathDocument::cloneDomainState() const {
    return std::make_unique<SplinePathDocument>(*this);
}

EditorResult<void> SplinePathDocument::commitDomainState(std::unique_ptr<IDomainOperationTarget> candidate) {
    auto* spline = dynamic_cast<SplinePathDocument*>(candidate.get());
    if (!spline || spline->id_ != id_)
        return splineError<void>(EditorStatus::Rejected, "editor.spline.invalid-candidate",
                                 "Spline candidate does not match this document");
    *this = std::move(*spline);
    return eve::editing::applied<void>();
}

EditorResult<DomainOperation> SplinePathDocument::makeSetPoint(const SplinePathControlPoint& point) const {
    auto parsed = parsePoint(pointValue(point));
    if (!parsed.ok())
        return splineError<DomainOperation>(parsed.code(), "editor.spline.invalid-point", "Invalid spline point");
    const auto found  = points_.find(point.id);
    const bool exists = found != points_.end();
    return eve::editing::applied<DomainOperation>(operation(
        "spline.point.set.v1", exists ? "spline.point.set.v1" : "spline.point.delete.v1", id_,
        pointValue(parsed.value()), exists ? pointValue(found->second) : pointValue(parsed.value()), point.id));
}

EditorResult<DomainOperation> SplinePathDocument::makeDeletePoint(const StableId& point) const {
    const auto found = points_.find(point);
    if (found == points_.end())
        return splineError<DomainOperation>(EditorStatus::NotFound, "editor.spline.point-not-found",
                                            "Spline point was not found");
    return eve::editing::applied<DomainOperation>(operation("spline.point.delete.v1", "spline.point.set.v1", id_,
                                                            pointValue(found->second), pointValue(found->second),
                                                            point));
}

EditorResult<DomainOperation> SplinePathDocument::makeSetSettings(const SplinePathSettings& settings) const {
    auto parsed = parseSettings(settingsValue(settings));
    if (!parsed.ok())
        return splineError<DomainOperation>(parsed.code(), "editor.spline.invalid-settings", "Invalid spline settings");
    if (settings.closed &&
        std::any_of(points_.begin(), points_.end(), [](const auto& entry) { return entry.second.breakBefore; }))
        return splineError<DomainOperation>(EditorStatus::Rejected, "editor.spline.closed-has-breaks",
                                            "Reconnect all spline chunks before closing the path");
    return eve::editing::applied<DomainOperation>(operation("spline.settings.set.v1", "spline.settings.set.v1", id_,
                                                            settingsValue(parsed.value()), settingsValue(settings_),
                                                            StableId("settings")));
}

std::vector<SplinePathControlPoint> SplinePathDocument::points() const {
    std::vector<SplinePathControlPoint> result;
    result.reserve(points_.size());
    for (const auto& [id, point] : points_) {
        static_cast<void>(id);
        result.push_back(point);
    }
    std::sort(result.begin(), result.end(), [](const auto& left, const auto& right) {
        return left.order == right.order ? left.id < right.id : left.order < right.order;
    });
    return result;
}

EditorResult<procgen::SplinePath> SplinePathDocument::compilePath() const {
    procgen::SplinePath path;
    auto                kind = path.setKindResult(settings_.kind);
    if (!kind.ok())
        return splineError<procgen::SplinePath>(EditorStatus::Rejected, "editor.spline.compile-kind",
                                                kind.status().describe());
    for (const auto& point : points()) {
        auto added = path.addPointResult(
            {static_cast<float>(point.x), static_cast<float>(point.y), static_cast<float>(point.z),
             static_cast<float>(point.inX), static_cast<float>(point.inY), static_cast<float>(point.inZ),
             static_cast<float>(point.outX), static_cast<float>(point.outY), static_cast<float>(point.outZ),
             static_cast<float>(point.rollDegrees), static_cast<float>(point.scaleX), static_cast<float>(point.scaleY),
             point.breakBefore, static_cast<float>(point.pitchDegrees), static_cast<float>(point.yawDegrees)});
        if (!added.ok())
            return splineError<procgen::SplinePath>(EditorStatus::Rejected, "editor.spline.compile-point",
                                                    added.status().describe());
    }
    path.setClosed(settings_.closed);
    auto ready = path.evaluateResult(0.f);
    if (!ready.ok())
        return splineError<procgen::SplinePath>(EditorStatus::Rejected, "editor.spline.compile-incomplete",
                                                ready.status().describe());
    return eve::editing::applied<procgen::SplinePath>(std::move(path));
}

EditorValue SplinePathDocument::snapshotValue() const {
    EditorValue::Array points;
    for (const auto& point : this->points()) points.push_back(pointValue(point));
    return EditorValue::Object{{"schema", "eve.procgen.splinePath"},
                               {"schemaVersion", std::int64_t{1}},
                               {"kind", settings_.kind},
                               {"closed", settings_.closed},
                               {"points", std::move(points)}};
}

EditorResult<void> SplinePathDocument::loadSnapshot(const EditorValue& snapshot) {
    const auto* schemaValue  = field(snapshot, "schema");
    const auto* versionValue = field(snapshot, "schemaVersion");
    const auto* kindValue    = field(snapshot, "kind");
    const auto* closedValue  = field(snapshot, "closed");
    const auto* pointsValue  = field(snapshot, "points");
    const auto* schema       = schemaValue ? schemaValue->getIf<std::string>() : nullptr;
    const auto* version      = versionValue ? versionValue->getIf<std::int64_t>() : nullptr;
    const auto* points       = pointsValue ? pointsValue->getIf<EditorValue::Array>() : nullptr;
    if (!schema || *schema != "eve.procgen.splinePath" || !version || *version != 1 || !kindValue || !closedValue ||
        !points)
        return splineError<void>(EditorStatus::Unsupported, "editor.spline.invalid-snapshot",
                                 "Spline snapshot schema is unsupported");
    auto settings = parseSettings(EditorValue::Object{{"kind", *kindValue}, {"closed", *closedValue}});
    if (!settings.ok())
        return splineError<void>(settings.code(), "editor.spline.invalid-snapshot-settings",
                                 "Spline snapshot settings are invalid");
    SplinePathDocument candidate(id_);
    candidate.settings_ = std::move(settings.value());
    for (const auto& value : *points) {
        auto parsed = parsePoint(value);
        if (!parsed.ok() || candidate.points_.contains(parsed.value().id))
            return splineError<void>(EditorStatus::Rejected, "editor.spline.invalid-snapshot-point",
                                     "Spline snapshot contains invalid or duplicate points");
        candidate.points_[parsed.value().id] = std::move(parsed.value());
    }
    if (candidate.settings_.closed && std::any_of(candidate.points_.begin(), candidate.points_.end(),
                                                  [](const auto& entry) { return entry.second.breakBefore; }))
        return splineError<void>(EditorStatus::Rejected, "editor.spline.invalid-snapshot-breaks",
                                 "Closed spline snapshot cannot contain disconnected chunks");
    candidate.revision_ = revision_ + 1;
    candidate.dirty_.clear();
    *this = std::move(candidate);
    return eve::editing::applied<void>();
}

EditorResult<procgen::SplinePath> compileSplinePathSnapshot(const EditorValue& snapshot) {
    SplinePathDocument document("spline-preview");
    auto               loaded = document.loadSnapshot(snapshot);
    if (!loaded.ok())
        return splineError<procgen::SplinePath>(loaded.code(), "editor.spline.snapshot-compile",
                                                loaded.status().describe());
    return document.compilePath();
}

}  // namespace eve::procgen_editing
