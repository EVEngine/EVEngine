#include "sensing/Sensing.h"
#include "common/Identity.h"
#include "sensing/TargetingPipeline.h"
#include "spatial/SpatialHash2D.h"
#include "common/SquirrelBinding.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <iomanip>
#include <limits>
#include <set>
#include <simplesquirrel/simplesquirrel.hpp>
#include <sstream>
#include <type_traits>
#include <utility>
#include <variant>
#include "common/Json.h"

namespace eve::sensing {
namespace {
std::set<std::string> csv(std::string_view s) {
    std::set<std::string> out;
    std::stringstream     in{std::string(s)};
    std::string           v;
    while (std::getline(in, v, ','))
        if (!v.empty()) out.insert(v);
    return out;
}
std::string quote(std::string_view s) {
    std::ostringstream o;
    o << '"';
    for (char c : s) {
        if (c == '"' || c == '\\') o << '\\';
        o << c;
    }
    return o.str() + '"';
}
std::string join(const std::set<std::string>& s) {
    std::string o;
    for (auto& v : s) {
        if (!o.empty()) o += ',';
        o += v;
    }
    return o;
}
bool finite(float v) { return std::isfinite(v); }

template <class T>
eve::Result<T> sensingFailure(eve::DiagnosticCode code, std::string message, std::string path = {}) {
    return eve::Result<T>::failure(eve::Diagnostic::error(code, std::move(message), std::move(path), {}, "sensing"));
}

template <class T>
eve::Result<T> sensingFailure(const eve::Status& status) {
    return eve::Result<T>::failure(status);
}

template <class Ref, class Proxy, class Release>
ssq::Table makeOwnedProxy(HSQUIRRELVM vm, eve::Result<Ref>&& reference, Release&& release) {
    if (!reference) return eve::script::projectStatusResult(vm, reference.status(), false, false);

    const Ref ref    = std::move(reference).takeValue();
    auto      object = eve::script::makeOwnedSquirrelInstance<Proxy>(vm, std::make_unique<Proxy>(ref));
    if (!object) {
        const eve::Status status = object.status();
        object.ignore("failed to create owned sensing proxy");
        std::invoke(std::forward<Release>(release), ref).ignore("rollback failed owned sensing allocation");
        return eve::script::projectStatusResult(vm, status, false, false);
    }

    ssq::Object owned = std::move(object).takeValue();
    auto result = eve::script::projectStatusResult(vm, eve::Status::success(eve::StatusCode::Applied), true, false);
    result.set("value", owned);
    result.set("ownership", std::string("owned"));
    result.set("ownerEpoch", static_cast<std::int64_t>(ref.ownerEpoch));
    result.set("handle", static_cast<std::int64_t>(ref.packed()));
    return result;
}
}  // namespace
namespace {
std::vector<std::string> toVector(const std::set<std::string>& values) {
    return std::vector<std::string>(values.begin(), values.end());
}

bool insideCone(float px, float py, const QueryCone& cone) {
    const float dx     = px - cone.x;
    const float dy     = py - cone.y;
    const float distSq = dx * dx + dy * dy;
    if (distSq > cone.range * cone.range) return false;
    if (distSq == 0.f) return true;
    const float facingLen = std::hypot(cone.dirX, cone.dirY);
    if (!(facingLen > 0.f)) return false;
    const float invDist = 1.f / std::sqrt(distSq);
    const float nx      = dx * invDist;
    const float ny      = dy * invDist;
    const float fx      = cone.dirX / facingLen;
    const float fy      = cone.dirY / facingLen;
    const float dot     = std::clamp(nx * fx + ny * fy, -1.f, 1.f);
    return std::acos(dot) <= cone.halfAngle;
}

bool insideShape(const Subject& subject, const QueryShape& shape) {
    return std::visit(
        [&](const auto& value) -> bool {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, std::monostate>) {
                return true;
            } else if constexpr (std::is_same_v<T, QueryCircle>) {
                const float dx = subject.x - value.x;
                const float dy = subject.y - value.y;
                return dx * dx + dy * dy <= value.radius * value.radius;
            } else if constexpr (std::is_same_v<T, QueryBox>) {
                return subject.x >= value.minX && subject.x <= value.maxX && subject.y >= value.minY &&
                       subject.y <= value.maxY;
            } else {
                return insideCone(subject.x, subject.y, value);
            }
        },
        shape);
}

eve::Result<QuerySpec> circleSpec(float x, float y, float radius, std::string_view requireTagsCsv,
                                  std::string_view excludeTagsCsv, std::string_view includeFactionsCsv,
                                  std::string_view excludeFactionsCsv, std::string_view visibleTo, int limit) {
    if (!finite(x) || !finite(y) || !finite(radius) || radius < 0.f || limit < 0) {
        return sensingFailure<QuerySpec>(eve::DiagnosticCode::InvalidArgument,
                                         "circle query requires finite coordinates, non-negative radius and limit",
                                         "query");
    }
    QuerySpec spec;
    spec.shape            = QueryCircle{x, y, radius};
    spec.requiredTags     = toVector(csv(requireTagsCsv));
    spec.excludedTags     = toVector(csv(excludeTagsCsv));
    spec.includeFactions  = toVector(csv(includeFactionsCsv));
    spec.excludeFactions  = toVector(csv(excludeFactionsCsv));
    if (!visibleTo.empty()) spec.visibleTo = std::string(visibleTo);
    spec.minRange         = 0.f;
    spec.maxRange         = radius;
    spec.minCount         = 0;
    spec.maxCount         = static_cast<std::uint32_t>(limit);
    spec.countPolicy      = CountPolicy::TruncateToMax;
    spec.sortKey          = SortKey::DistanceAscending;
    return eve::Result<QuerySpec>::success(std::move(spec));
}

eve::Result<QuerySpec> boxSpec(float minX, float minY, float maxX, float maxY, std::string_view requireTagsCsv,
                               std::string_view excludeTagsCsv, std::string_view includeFactionsCsv,
                               std::string_view excludeFactionsCsv, std::string_view visibleTo, int limit) {
    if (!finite(minX) || !finite(minY) || !finite(maxX) || !finite(maxY) || minX > maxX || minY > maxY || limit < 0) {
        return sensingFailure<QuerySpec>(eve::DiagnosticCode::InvalidArgument,
                                         "box query requires finite ordered bounds and a non-negative limit", "query");
    }
    QuerySpec spec;
    spec.shape           = QueryBox{minX, minY, maxX, maxY};
    spec.requiredTags    = toVector(csv(requireTagsCsv));
    spec.excludedTags    = toVector(csv(excludeTagsCsv));
    spec.includeFactions = toVector(csv(includeFactionsCsv));
    spec.excludeFactions = toVector(csv(excludeFactionsCsv));
    if (!visibleTo.empty()) spec.visibleTo = std::string(visibleTo);
    spec.minCount        = 0;
    spec.maxCount        = static_cast<std::uint32_t>(limit);
    spec.countPolicy     = CountPolicy::TruncateToMax;
    spec.sortKey         = SortKey::DistanceAscending;
    return eve::Result<QuerySpec>::success(std::move(spec));
}
}  // namespace

eve::Result<void> QuerySpec::validate() const {
    if (!std::isfinite(minRange) || minRange < 0.f) {
        return sensingFailure<void>(eve::DiagnosticCode::InvalidArgument, "QuerySpec.minRange must be finite and >= 0",
                                    "query.minRange");
    }
    if (!(std::isfinite(maxRange) || maxRange == std::numeric_limits<float>::infinity()) || maxRange < minRange) {
        return sensingFailure<void>(eve::DiagnosticCode::InvalidArgument,
                                    "QuerySpec.maxRange must be >= minRange (finite or +inf)", "query.maxRange");
    }
    if (minCount > maxCount) {
        return sensingFailure<void>(eve::DiagnosticCode::InvalidArgument, "QuerySpec.minCount exceeds maxCount",
                                    "query.count");
    }
    for (const auto& tag : requiredTags) {
        if (tag.empty())
            return sensingFailure<void>(eve::DiagnosticCode::InvalidArgument, "requiredTags cannot contain empty keys",
                                        "query.requiredTags");
    }
    for (const auto& tag : excludedTags) {
        if (tag.empty())
            return sensingFailure<void>(eve::DiagnosticCode::InvalidArgument, "excludedTags cannot contain empty keys",
                                        "query.excludedTags");
    }
    for (const auto& faction : includeFactions) {
        if (faction.empty())
            return sensingFailure<void>(eve::DiagnosticCode::InvalidArgument,
                                        "includeFactions cannot contain empty keys", "query.includeFactions");
    }
    for (const auto& faction : excludeFactions) {
        if (faction.empty())
            return sensingFailure<void>(eve::DiagnosticCode::InvalidArgument,
                                        "excludeFactions cannot contain empty keys", "query.excludeFactions");
    }
    if (visibleTo && visibleTo->empty()) {
        return sensingFailure<void>(eve::DiagnosticCode::InvalidArgument, "visibleTo cannot be empty when set",
                                    "query.visibleTo");
    }
    const auto shapeOk = std::visit(
        [](const auto& value) -> eve::Result<void> {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, QueryCircle>) {
                if (!finite(value.x) || !finite(value.y) || !finite(value.radius) || value.radius < 0.f) {
                    return sensingFailure<void>(eve::DiagnosticCode::InvalidArgument,
                                                "QueryCircle requires finite center and non-negative radius",
                                                "query.shape");
                }
            } else if constexpr (std::is_same_v<T, QueryBox>) {
                if (!finite(value.minX) || !finite(value.minY) || !finite(value.maxX) || !finite(value.maxY) ||
                    value.minX > value.maxX || value.minY > value.maxY) {
                    return sensingFailure<void>(eve::DiagnosticCode::InvalidArgument,
                                                "QueryBox requires finite ordered bounds", "query.shape");
                }
            } else if constexpr (std::is_same_v<T, QueryCone>) {
                if (!finite(value.x) || !finite(value.y) || !finite(value.dirX) || !finite(value.dirY) ||
                    !finite(value.halfAngle) || !finite(value.range) || value.halfAngle < 0.f ||
                    value.halfAngle > 3.14159265f || value.range < 0.f || !(std::hypot(value.dirX, value.dirY) > 0.f)) {
                    return sensingFailure<void>(
                        eve::DiagnosticCode::InvalidArgument,
                        "QueryCone requires finite apex/dir, halfAngle in [0,pi], non-negative range, non-zero dir",
                        "query.shape");
                }
            }
            return eve::Result<void>::success();
        },
        shape);
    if (!shapeOk) return eve::Result<void>::failure(shapeOk.status());
    return eve::Result<void>::success();
}

eve::Result<void> SensingWorld::upsert(std::string_view id, float x, float y, std::string_view f, std::string_view t,
                                       std::string_view v) {
    if (id.empty() || !finite(x) || !finite(y)) {
        return sensingFailure<void>(eve::DiagnosticCode::InvalidArgument,
                                    "subject id and finite coordinates are required", "subject");
    }
    Subject subject{std::string(id), x, y, std::string(f), csv(t), csv(v)};
    subjects_[subject.id] = subject;
    if (spatialIndex_) indexSubject(subjects_[subject.id]);
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> SensingWorld::remove(std::string_view id) {
    results_.clear();
    const std::string key(id);
    if (subjects_.erase(key) == 0)
        return sensingFailure<void>(eve::DiagnosticCode::NotFound, "subject is not registered", "subject.id");
    unindexSubject(key);
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> SensingWorld::setZones(std::string_view id, std::string_view zonesCsv) {
    const std::string key(id);
    auto              it = subjects_.find(key);
    if (it == subjects_.end())
        return sensingFailure<void>(eve::DiagnosticCode::NotFound, "subject is not registered", "subject.id");
    auto zones = csv(zonesCsv);
    for (const auto& zone : zones) {
        if (zone.empty())
            return sensingFailure<void>(eve::DiagnosticCode::InvalidArgument, "zones cannot contain empty keys",
                                        "subject.zones");
        if (!eve::LogicalId::parse(zone))
            return sensingFailure<void>(eve::DiagnosticCode::InvalidArgument,
                                        "zones entries must be valid LogicalId texts", "subject.zones");
    }
    it->second.zones = std::move(zones);
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> SensingWorld::setSpatialIndexEnabled(bool enabled, float cellSize) {
    if (!enabled) {
        clearSpatialIndex();
        return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
    }
    if (!finite(cellSize) || !(cellSize > 0.f)) {
        return sensingFailure<void>(eve::DiagnosticCode::InvalidArgument,
                                    "spatial index cellSize must be finite and > 0", "spatial.cellSize");
    }
    clearSpatialIndex();
    spatialIndex_ = std::make_unique<eve::spatial::SpatialHash2D>(cellSize);
    rebuildSpatialIndex();
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

bool SensingWorld::spatialIndexEnabled() const noexcept { return spatialIndex_ != nullptr; }

void SensingWorld::clearSpatialIndex() {
    spatialIndex_.reset();
    subjectSpatialIds_.clear();
    spatialIdSubjects_.clear();
    nextSpatialId_ = 1;
}

void SensingWorld::rebuildSpatialIndex() {
    subjectSpatialIds_.clear();
    spatialIdSubjects_.clear();
    nextSpatialId_ = 1;
    if (!spatialIndex_) return;
    spatialIndex_->clear();
    for (const auto& [id, subject] : subjects_) {
        (void)id;
        indexSubject(subject);
    }
}

void SensingWorld::indexSubject(const Subject& subject) {
    if (!spatialIndex_) return;
    auto it = subjectSpatialIds_.find(subject.id);
    if (it == subjectSpatialIds_.end()) {
        const int sid = nextSpatialId_++;
        subjectSpatialIds_.emplace(subject.id, sid);
        spatialIdSubjects_.emplace(sid, subject.id);
        spatialIndex_->insert(sid, subject.x, subject.y, subject.x, subject.y);
    } else {
        spatialIndex_->update(it->second, subject.x, subject.y, subject.x, subject.y);
    }
}

void SensingWorld::unindexSubject(const std::string& id) {
    auto it = subjectSpatialIds_.find(id);
    if (it == subjectSpatialIds_.end()) return;
    const int sid = it->second;
    if (spatialIndex_) spatialIndex_->remove(sid);
    spatialIdSubjects_.erase(sid);
    subjectSpatialIds_.erase(it);
}

bool SensingWorld::trySpatialBroadphase(const QueryOrigin& origin, const QuerySpec& spec,
                                        std::vector<const Subject*>& out) const {
    if (!spatialIndex_) return false;
    const bool ran = std::visit(
        [&](const auto& value) -> bool {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, std::monostate>) {
                if (!(std::isfinite(spec.maxRange))) return false;
                spatialIndex_->queryCircle(origin.x, origin.y, spec.maxRange);
                return true;
            } else if constexpr (std::is_same_v<T, QueryCircle>) {
                spatialIndex_->queryCircle(value.x, value.y, value.radius);
                return true;
            } else if constexpr (std::is_same_v<T, QueryBox>) {
                spatialIndex_->queryRect(value.minX, value.minY, value.maxX, value.maxY);
                return true;
            } else {
                spatialIndex_->queryCircle(value.x, value.y, value.range);
                return true;
            }
        },
        spec.shape);
    if (!ran) return false;
    out.clear();
    out.reserve(static_cast<std::size_t>(spatialIndex_->getResultCount()));
    for (int i = 0; i < spatialIndex_->getResultCount(); ++i) {
        const int  sid = spatialIndex_->getResultId(i);
        const auto map = spatialIdSubjects_.find(sid);
        if (map == spatialIdSubjects_.end()) continue;
        const auto subject = subjects_.find(map->second);
        if (subject == subjects_.end()) continue;
        out.push_back(&subject->second);
    }
    return true;
}

bool SensingWorld::accepts(const Subject& subject, const QuerySpec& spec) const {
    for (const auto& tag : spec.requiredTags)
        if (!subject.tags.count(tag)) return false;
    for (const auto& tag : spec.excludedTags)
        if (subject.tags.count(tag)) return false;
    if (!spec.includeFactions.empty()) {
        bool included = false;
        for (const auto& faction : spec.includeFactions)
            if (subject.faction == faction) {
                included = true;
                break;
            }
        if (!included) return false;
    }
    for (const auto& faction : spec.excludeFactions)
        if (subject.faction == faction) return false;
    if (spec.visibleTo && !subject.visibleTo.count(*spec.visibleTo)) return false;
    return insideShape(subject, spec.shape);
}

void SensingWorld::publishResults(std::vector<RankedCandidate> ranked) {
    results_.clear();
    results_.reserve(ranked.size());
    for (const auto& candidate : ranked)
        results_.push_back(Candidate{candidate.id, candidate.x, candidate.y, candidate.distance});
    lastQuery_.ranked   = ranked;
    lastQuery_.accepted = static_cast<std::uint32_t>(ranked.size());
}

eve::Result<CandidateQueryResult> SensingWorld::query(const QueryOrigin& origin, const QuerySpec& spec) {
    auto valid = spec.validate();
    if (!valid) return sensingFailure<CandidateQueryResult>(valid.status());
    if (!finite(origin.x) || !finite(origin.y)) {
        return sensingFailure<CandidateQueryResult>(eve::DiagnosticCode::InvalidArgument,
                                                    "QueryOrigin coordinates must be finite", "query.origin");
    }

    std::vector<const Subject*> candidates;
    const bool usedSpatial = trySpatialBroadphase(origin, spec, candidates);
    if (!usedSpatial) {
        candidates.clear();
        candidates.reserve(subjects_.size());
        for (const auto& [id, subject] : subjects_) {
            (void)id;
            candidates.push_back(&subject);
        }
    }

    std::vector<RankedCandidate> ranked;
    ranked.reserve(candidates.size());
    for (const Subject* subjectPtr : candidates) {
        const Subject& subject = *subjectPtr;
        if (origin.subjectId && subject.id == *origin.subjectId) continue;
        if (!accepts(subject, spec)) continue;
        const float distance = std::hypot(subject.x - origin.x, subject.y - origin.y);
        if (distance < spec.minRange || distance > spec.maxRange) continue;
        RankedCandidate candidate;
        candidate.id          = subject.id;
        candidate.x           = subject.x;
        candidate.y           = subject.y;
        candidate.distance    = distance;
        candidate.score       = -distance;
        candidate.scoreReason = "distance";
        ranked.push_back(std::move(candidate));
    }

    if (spec.sortKey == SortKey::DistanceAscending) {
        std::sort(ranked.begin(), ranked.end(), [](const RankedCandidate& a, const RankedCandidate& b) {
            if (a.distance != b.distance) return a.distance < b.distance;
            return a.id < b.id;
        });
    } else {
        std::sort(ranked.begin(), ranked.end(),
                  [](const RankedCandidate& a, const RankedCandidate& b) { return a.id < b.id; });
    }

    if (spec.countPolicy == CountPolicy::TruncateToMax) {
        if (ranked.size() > static_cast<std::size_t>(spec.maxCount)) ranked.resize(static_cast<std::size_t>(spec.maxCount));
        if (ranked.size() < static_cast<std::size_t>(spec.minCount)) {
            return sensingFailure<CandidateQueryResult>(
                eve::DiagnosticCode::PreconditionViolation,
                "candidate count is below QuerySpec.minCount after truncation", "query.count");
        }
    } else if (ranked.size() < static_cast<std::size_t>(spec.minCount) ||
               ranked.size() > static_cast<std::size_t>(spec.maxCount)) {
        return sensingFailure<CandidateQueryResult>(eve::DiagnosticCode::PreconditionViolation,
                                                    "candidate count violates QuerySpec count policy", "query.count");
    }

    lastQuery_.usedSpatial = usedSpatial;
    lastQuery_.scanned     = static_cast<std::uint32_t>(candidates.size());
    lastQuery_.originX     = origin.x;
    lastQuery_.originY     = origin.y;
    lastQuery_.shapeKind   = std::visit(
        [](const auto& value) -> std::string {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, QueryCircle>) return "circle";
            if constexpr (std::is_same_v<T, QueryBox>) return "box";
            if constexpr (std::is_same_v<T, QueryCone>) return "cone";
            return "none";
        },
        spec.shape);

    publishResults(ranked);
    return eve::Result<CandidateQueryResult>::success(CandidateQueryResult{std::move(ranked)});
}

eve::Result<int> SensingWorld::circle(float x, float y, float radius, std::string_view r, std::string_view e,
                                      std::string_view i, std::string_view ef, std::string_view v, int limit) {
    auto spec = circleSpec(x, y, radius, r, e, i, ef, v, limit);
    if (!spec) return sensingFailure<int>(spec.status());
    auto result = query(QueryOrigin{x, y, std::nullopt}, std::move(spec).takeValue());
    if (!result) return sensingFailure<int>(result.status());
    return eve::Result<int>::success(static_cast<int>(std::move(result).takeValue().size()));
}

eve::Result<int> SensingWorld::box(float a, float b, float c, float d, std::string_view r, std::string_view e,
                                   std::string_view i, std::string_view ef, std::string_view v, int limit) {
    auto spec = boxSpec(a, b, c, d, r, e, i, ef, v, limit);
    if (!spec) return sensingFailure<int>(spec.status());
    const float originX = (a + c) * 0.5f;
    const float originY = (b + d) * 0.5f;
    auto        result  = query(QueryOrigin{originX, originY, std::nullopt}, std::move(spec).takeValue());
    if (!result) return sensingFailure<int>(result.status());
    return eve::Result<int>::success(static_cast<int>(std::move(result).takeValue().size()));
}

eve::Result<int> SensingWorld::executePreset(std::string_view presetId, float originX, float originY, float dirX,
                                             float dirY) {
    TargetingSourceContext context;
    context.world  = this;
    context.origin = QueryOrigin{originX, originY, std::nullopt};
    context.dirX   = dirX;
    context.dirY   = dirY;
    auto executed  = TargetingPipeline::sharedBuiltins().executePreset(context, presetId);
    if (!executed) return sensingFailure<int>(executed.status());
    auto rankedResult = std::move(executed).takeValue();
    std::vector<RankedCandidate> ranked(rankedResult.ranked().begin(), rankedResult.ranked().end());
    const int count = static_cast<int>(ranked.size());
    publishResults(std::move(ranked));
    return eve::Result<int>::success(count);
}

eve::OptionalRef<const Candidate> SensingWorld::resultAt(int i) const {
    return i >= 0 && size_t(i) < results_.size() ? eve::OptionalRef<const Candidate>(std::cref(results_[size_t(i)]))
                                                 : eve::OptionalRef<const Candidate>{};
}
std::string SensingWorld::debugLastQueryJson() const {
    std::ostringstream o;
    o << std::setprecision(9);
    o << "{\"schema\":\"eve.sensing.lastQuery\",\"version\":1"
      << ",\"origin\":{\"x\":" << lastQuery_.originX << ",\"y\":" << lastQuery_.originY << '}'
      << ",\"shape\":" << quote(lastQuery_.shapeKind)
      << ",\"spatial\":{\"enabled\":" << (spatialIndex_ ? "true" : "false")
      << ",\"used\":" << (lastQuery_.usedSpatial ? "true" : "false")
      << ",\"scanned\":" << lastQuery_.scanned
      << ",\"accepted\":" << lastQuery_.accepted << '}'
      << ",\"ranked\":[";
    bool first = true;
    for (const auto& candidate : lastQuery_.ranked) {
        if (!first) o << ',';
        first = false;
        o << "{\"id\":" << quote(candidate.id) << ",\"x\":" << candidate.x << ",\"y\":" << candidate.y
          << ",\"distance\":" << candidate.distance << ",\"score\":" << candidate.score
          << ",\"scoreReason\":" << quote(candidate.scoreReason) << '}';
    }
    o << "]}";
    return o.str();
}

std::string SensingWorld::snapshotJson() const {
    std::ostringstream o;
    o << "{\"schema\":\"eve.sensing.world\",\"version\":1,\"subjects\":[";
    bool first = true;
    for (auto& [id, s] : subjects_) {
        if (!first) o << ',';
        first = false;
        o << "{\"id\":" << quote(id) << ",\"x\":" << std::setprecision(9) << s.x << ",\"y\":" << s.y
          << ",\"faction\":" << quote(s.faction) << ",\"tags\":" << quote(join(s.tags))
          << ",\"visibleTo\":" << quote(join(s.visibleTo)) << ",\"zones\":" << quote(join(s.zones)) << '}';
    }
    return o.str() + "]}";
}
eve::Result<void> SensingWorld::restoreJson(const std::string& j) {
    std::string err;
    auto        d = eve::json::Document::parse(j, &err);
    if (!d.valid() || !d.root().isObject() || d.root().getString("schema") != "eve.sensing.world" ||
        d.root().getInt("version") != 1 || !d.root().get("subjects").isArray()) {
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::ParseError, err.empty() ? "invalid snapshot" : err, {}, {}, "sensing.restore"));
    }
    SensingWorld next;
    auto         a = d.root().get("subjects");
    for (size_t n = 0; n < a.size(); ++n) {
        auto v = a.at(n);
        if (!v.isObject()) {
            return eve::Result<void>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvalidArgument, "invalid snapshot subject", {}, {}, "sensing.restore"));
        }
        auto subject = next.upsert(v.getString("id"), float(v.getDouble("x")), float(v.getDouble("y")),
                                   v.getString("faction"), v.getString("tags"), v.getString("visibleTo"));
        if (!subject.ok())
            return eve::Result<void>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvalidArgument, "invalid snapshot subject", {}, {}, "sensing.restore"));
        if (v.has("zones")) {
            auto zones = next.setZones(v.getString("id"), v.getString("zones"));
            if (!zones.ok())
                return eve::Result<void>::failure(eve::Diagnostic::error(
                    eve::DiagnosticCode::InvalidArgument, "invalid snapshot subject zones", {}, {},
                    "sensing.restore"));
        }
    }
    subjects_ = std::move(next.subjects_);
    results_.clear();
    lastQuery_ = {};
    if (spatialIndex_) rebuildSpatialIndex();
    return eve::Result<void>::success();
}
eve::Result<SensingWorldHandleRef> Sensing::newWorld() {
    return Sensing::create()->worlds_.emplace(std::make_unique<SensingWorld>());
}

eve::script::Borrowed<SensingWorld> Sensing::resolve(SensingWorldHandleRef reference) noexcept {
    Sensing* module = ModuleManager::getInstance<Sensing>("Sensing");
    return module ? module->worlds_.resolve(reference) : eve::script::Borrowed<SensingWorld>();
}

eve::Result<void> Sensing::release(SensingWorldHandleRef reference) {
    Sensing* module = ModuleManager::getInstance<Sensing>("Sensing");
    if (!module)
        return sensingFailure<void>(eve::DiagnosticCode::StaleHandle, "Sensing module is no longer loaded", "world");
    return module->worlds_.erase(reference);
}

bool Sensing::isStale(SensingWorldHandleRef reference) noexcept {
    if (!reference.isValid()) return false;
    Sensing* module = ModuleManager::getInstance<Sensing>("Sensing");
    return !module || module->worlds_.isStale(reference);
}

struct ScriptSensingWorld {
    explicit ScriptSensingWorld(SensingWorldHandleRef value) : reference(value) {}
    ~ScriptSensingWorld() noexcept { Sensing::release(reference).ignore("script sensing world proxy destruction"); }
    SensingWorldHandleRef reference;
};

Sensing::Sensing() { eve::cap::provide<eve::ISensingQuery>(this); }

Sensing::~Sensing() { eve::cap::revoke<eve::ISensingQuery>(this); }

int Sensing::worldCount() const {
    int count = 0;
    worlds_.forEachLive([&](std::uint32_t, const SensingWorld&) { ++count; });
    return count;
}

std::vector<eve::SensingWorldQuery> Sensing::lastQueries() const {
    std::vector<eve::SensingWorldQuery> queries;
    worlds_.forEachLive([&](std::uint32_t index, const SensingWorld& world) {
        eve::SensingWorldQuery entry;
        entry.index         = static_cast<int>(index);
        entry.lastQueryJson = world.debugLastQueryJson();
        queries.push_back(std::move(entry));
    });
    return queries;
}

Module_IMPL(Sensing, new Sensing());
void Sensing::expose(ssq::Table& t) {
    auto c = t.addClass<Candidate>("SensingCandidate", std::function<Candidate*()>([]() { return nullptr; }), false);
    c.addFunc("getId", [](Candidate* v) { return v ? v->id : std::string{}; });
    c.addFunc("getX", [](Candidate* v) { return v ? v->x : 0.f; });
    c.addFunc("getY", [](Candidate* v) { return v ? v->y : 0.f; });
    c.addFunc("getDistance", [](Candidate* v) { return v ? v->distance : 0.f; });
    const HSQUIRRELVM vm = t.getHandle();
    auto              w  = t.addClass<ScriptSensingWorld>("SensingWorld",
                                                          std::function<ScriptSensingWorld*()>([]() { return nullptr; }), false);
    w.addFunc("ownership", [](ScriptSensingWorld*) { return std::string("owned"); });
    w.addFunc("ownerEpoch", [](ScriptSensingWorld* value) {
        return value ? static_cast<std::int64_t>(value->reference.ownerEpoch) : std::int64_t{0};
    });
    w.addFunc("handle", [](ScriptSensingWorld* value) {
        return value ? static_cast<std::int64_t>(value->reference.packed()) : std::int64_t{0};
    });
    w.addFunc("isStale", [](ScriptSensingWorld* value) { return !value || Sensing::isStale(value->reference); });
    w.addFunc("release", [vm](ScriptSensingWorld* value) {
        if (!value)
            return eve::script::projectResult(
                vm, sensingFailure<void>(eve::DiagnosticCode::InvalidArgument, "sensing world proxy must not be null",
                                         "world"));
        return eve::script::projectResult(vm, Sensing::release(value->reference));
    });
    w.addFunc("upsert", [vm](ScriptSensingWorld* value, const std::string& id, float x, float y,
                             const std::string& faction, const std::string& tags, const std::string& visibleTo) {
        if (!value)
            return eve::script::projectResult(
                vm, sensingFailure<void>(eve::DiagnosticCode::InvalidArgument, "sensing world proxy must not be null",
                                         "world"));
        auto world = Sensing::resolve(value->reference);
        if (!world.isBound())
            return eve::script::projectResult(
                vm, sensingFailure<void>(eve::DiagnosticCode::StaleHandle, "sensing world handle is stale", "world"));
        return eve::script::projectResult(vm, world->upsert(id, x, y, faction, tags, visibleTo));
    });
    w.addFunc("remove", [vm](ScriptSensingWorld* value, const std::string& id) {
        if (!value)
            return eve::script::projectResult(
                vm, sensingFailure<void>(eve::DiagnosticCode::InvalidArgument, "sensing world proxy must not be null",
                                         "world"));
        auto world = Sensing::resolve(value->reference);
        if (!world.isBound())
            return eve::script::projectResult(
                vm, sensingFailure<void>(eve::DiagnosticCode::StaleHandle, "sensing world handle is stale", "world"));
        return eve::script::projectResult(vm, world->remove(id));
    });
    w.addFunc("setZones", [vm](ScriptSensingWorld* value, const std::string& id, const std::string& zones) {
        if (!value)
            return eve::script::projectResult(
                vm, sensingFailure<void>(eve::DiagnosticCode::InvalidArgument, "sensing world proxy must not be null",
                                         "world"));
        auto world = Sensing::resolve(value->reference);
        if (!world.isBound())
            return eve::script::projectResult(
                vm, sensingFailure<void>(eve::DiagnosticCode::StaleHandle, "sensing world handle is stale", "world"));
        return eve::script::projectResult(vm, world->setZones(id, zones));
    });
    auto projectCount = [vm](eve::Result<int>&& result) {
        return eve::script::projectResult(vm, std::move(result),
                                          [](int count) { return eve::Value(static_cast<std::int64_t>(count)); });
    };
    w.addFunc("circle", [vm, projectCount](ScriptSensingWorld* value, float x, float y, float radius,
                                           const std::string& required, const std::string& excluded,
                                           const std::string& includedFactions, const std::string& excludedFactions,
                                           const std::string& visibleTo, int limit) {
        if (!value)
            return eve::script::projectResult(vm,
                                              sensingFailure<int>(eve::DiagnosticCode::InvalidArgument,
                                                                  "sensing world proxy must not be null", "world"),
                                              [](int count) { return eve::Value(static_cast<std::int64_t>(count)); });
        auto world = Sensing::resolve(value->reference);
        if (!world.isBound())
            return eve::script::projectResult(
                vm, sensingFailure<int>(eve::DiagnosticCode::StaleHandle, "sensing world handle is stale", "world"),
                [](int count) { return eve::Value(static_cast<std::int64_t>(count)); });
        return projectCount(
            world->circle(x, y, radius, required, excluded, includedFactions, excludedFactions, visibleTo, limit));
    });
    w.addFunc("box", [vm, projectCount](ScriptSensingWorld* value, float minX, float minY, float maxX, float maxY,
                                        const std::string& required, const std::string& excluded,
                                        const std::string& includedFactions, const std::string& excludedFactions,
                                        const std::string& visibleTo, int limit) {
        if (!value)
            return eve::script::projectResult(vm,
                                              sensingFailure<int>(eve::DiagnosticCode::InvalidArgument,
                                                                  "sensing world proxy must not be null", "world"),
                                              [](int count) { return eve::Value(static_cast<std::int64_t>(count)); });
        auto world = Sensing::resolve(value->reference);
        if (!world.isBound())
            return eve::script::projectResult(
                vm, sensingFailure<int>(eve::DiagnosticCode::StaleHandle, "sensing world handle is stale", "world"),
                [](int count) { return eve::Value(static_cast<std::int64_t>(count)); });
        return projectCount(world->box(minX, minY, maxX, maxY, required, excluded, includedFactions, excludedFactions,
                                       visibleTo, limit));
    });
    w.addFunc("executePreset", [vm, projectCount](ScriptSensingWorld* value, const std::string& presetId, float originX,
                                                  float originY, float dirX, float dirY) {
        if (!value)
            return eve::script::projectResult(vm,
                                              sensingFailure<int>(eve::DiagnosticCode::InvalidArgument,
                                                                  "sensing world proxy must not be null", "world"),
                                              [](int count) { return eve::Value(static_cast<std::int64_t>(count)); });
        auto world = Sensing::resolve(value->reference);
        if (!world.isBound())
            return eve::script::projectResult(
                vm, sensingFailure<int>(eve::DiagnosticCode::StaleHandle, "sensing world handle is stale", "world"),
                [](int count) { return eve::Value(static_cast<std::int64_t>(count)); });
        return projectCount(world->executePreset(presetId, originX, originY, dirX, dirY));
    });
    w.addFunc("resultAt", [](ScriptSensingWorld* value, int i) -> Candidate* {
        if (!value) return nullptr;
        auto world = Sensing::resolve(value->reference);
        if (!world.isBound()) return nullptr;
        auto candidate = world->resultAt(i);
        return candidate ? const_cast<Candidate*>(&candidate->get()) : nullptr;
    });
    w.addFunc("snapshotJson", [vm](ScriptSensingWorld* value) {
        if (!value)
            return eve::script::projectResult(
                vm,
                sensingFailure<std::string>(eve::DiagnosticCode::InvalidArgument,
                                            "sensing world proxy must not be null", "world"),
                [](std::string text) { return eve::Value(std::move(text)); });
        auto world = Sensing::resolve(value->reference);
        if (!world.isBound())
            return eve::script::projectResult(
                vm,
                sensingFailure<std::string>(eve::DiagnosticCode::StaleHandle, "sensing world handle is stale", "world"),
                [](std::string text) { return eve::Value(std::move(text)); });
        return eve::script::projectResult(vm, eve::Result<std::string>::success(world->snapshotJson()),
                                          [](std::string text) { return eve::Value(std::move(text)); });
    });
    w.addFunc("restoreJson", [vm](ScriptSensingWorld* value, const std::string& json) {
        if (!value)
            return eve::script::projectResult(
                vm, sensingFailure<void>(eve::DiagnosticCode::InvalidArgument, "sensing world proxy must not be null",
                                         "world"));
        auto world = Sensing::resolve(value->reference);
        if (!world.isBound())
            return eve::script::projectResult(
                vm, sensingFailure<void>(eve::DiagnosticCode::StaleHandle, "sensing world handle is stale", "world"));
        return eve::script::projectResult(vm, world->restoreJson(json));
    });
    w.addFunc("setSpatialIndexEnabled", [vm](ScriptSensingWorld* value, bool enabled, float cellSize) {
        if (!value)
            return eve::script::projectResult(
                vm, sensingFailure<void>(eve::DiagnosticCode::InvalidArgument, "sensing world proxy must not be null",
                                         "world"));
        auto world = Sensing::resolve(value->reference);
        if (!world.isBound())
            return eve::script::projectResult(
                vm, sensingFailure<void>(eve::DiagnosticCode::StaleHandle, "sensing world handle is stale", "world"));
        return eve::script::projectResult(vm, world->setSpatialIndexEnabled(enabled, cellSize));
    });
    w.addFunc("spatialIndexEnabled", [](ScriptSensingWorld* value) {
        if (!value) return false;
        auto world = Sensing::resolve(value->reference);
        return world.isBound() && world->spatialIndexEnabled();
    });
    w.addFunc("debugLastQueryJson", [vm](ScriptSensingWorld* value) {
        if (!value)
            return eve::script::projectResult(
                vm,
                sensingFailure<std::string>(eve::DiagnosticCode::InvalidArgument,
                                            "sensing world proxy must not be null", "world"),
                [](std::string text) { return eve::Value(std::move(text)); });
        auto world = Sensing::resolve(value->reference);
        if (!world.isBound())
            return eve::script::projectResult(
                vm,
                sensingFailure<std::string>(eve::DiagnosticCode::StaleHandle, "sensing world handle is stale", "world"),
                [](std::string text) { return eve::Value(std::move(text)); });
        return eve::script::projectResult(vm, eve::Result<std::string>::success(world->debugLastQueryJson()),
                                          [](std::string text) { return eve::Value(std::move(text)); });
    });
    auto cls = t.addClass(name, Sensing::create, false);
    expose(cls);
}
void Sensing::expose(ssq::Class& c) {
    c.addFunc("getName", &Sensing::getName);
    c.addFunc("newWorld", [vm = c.getHandle()](Sensing*) {
        return makeOwnedProxy<SensingWorldHandleRef, ScriptSensingWorld>(
            vm, Sensing::newWorld(), [](SensingWorldHandleRef reference) { return Sensing::release(reference); });
    });
}
}  // namespace eve::sensing
