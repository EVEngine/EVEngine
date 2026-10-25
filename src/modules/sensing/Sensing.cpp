// eve-hot-path
#include "sensing/Sensing.h"
#include "common/Identity.h"
#include "common/SquirrelBinding.h"
#include "sensing/SensingQueryBinding.h"
#include "sensing/SensingScriptCandidateSeam.h"
#include "sensing/TargetingPipeline.h"
#include "spatial/SpatialHash2D.h"
#include "spatial/SpatialHash3D.h"

#include <algorithm>
#include <array>
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
struct SpatialCellQueryCache {
    const SensingWorld*                                       owner = nullptr;
    const eve::spatial::SpatialHash2D*                        index = nullptr;
    std::map<std::array<int, 4>, std::vector<const Subject*>> candidates;
    std::size_t                                               candidateCount = 0;

    void invalidate(const SensingWorld* world) {
        if (owner != world) return;
        candidates.clear();
        candidateCount = 0;
    }

    void select(const SensingWorld* world, const eve::spatial::SpatialHash2D* currentIndex) {
        if (owner == world && index == currentIndex) return;
        candidates.clear();
        candidateCount = 0;
        owner          = world;
        index          = currentIndex;
    }
};

// Queries are owner-thread-only; one bounded scratch cache avoids changing SensingWorld's ABI.
thread_local SpatialCellQueryCache spatialCellQueryCache;

std::set<std::string> csv(std::string_view s) {
    std::set<std::string> out;
    std::stringstream     in{std::string(s)};
    std::string           v;
    while (std::getline(in, v, ','))
        if (!v.empty()) out.insert(v);
    return out;
}
bool csvMatches(std::string_view text, const std::set<std::string>& values) {
    const auto containsToken = [text](std::string_view value) {
        auto remaining = text;
        while (!remaining.empty()) {
            const auto comma = remaining.find(',');
            if (remaining.substr(0, comma) == value) return true;
            if (comma == std::string_view::npos) break;
            remaining.remove_prefix(comma + 1);
        }
        return false;
    };
    for (const auto& value : values)
        if (!containsToken(value)) return false;
    while (!text.empty()) {
        const auto comma = text.find(',');
        const auto token = text.substr(0, comma);
        if (!token.empty() &&
            std::none_of(values.begin(), values.end(), [token](const std::string& value) { return value == token; }))
            return false;
        if (comma == std::string_view::npos) break;
        text.remove_prefix(comma + 1);
    }
    return true;
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
// Keep float-to-cell conversion within int and bound query-cell traversal.
bool indexable(float x, float y, float z, float cellSize) {
    const double limit = double(std::numeric_limits<int>::max()) - 2;
    return std::abs(double(x) / cellSize) < limit && std::abs(double(y) / cellSize) < limit &&
           std::abs(double(z) / cellSize) < limit;
}
bool boundedQuery(float x, float y, float z, float radius, float cellSize, int dimensions) {
    if (!indexable(x, y, z, cellSize) || !std::isfinite(radius)) return false;
    const double cells = 2 * std::ceil(double(radius) / cellSize) + 3;
    return std::pow(cells, dimensions) <= 262144 && indexable(x + radius, y + radius, z + radius, cellSize) &&
           indexable(x - radius, y - radius, z - radius, cellSize);
}


template <class Ref, class Proxy, class Release>
ssq::Table makeOwnedProxy(HSQUIRRELVM vm, eve::Result<Ref>&& reference, Release&& release) {
    if (!reference) return eve::script::projectStatusResult(vm, reference.status());

    const Ref ref    = std::move(reference).takeValue();
    auto      object = eve::script::makeOwnedSquirrelInstance<Proxy>(vm, std::make_unique<Proxy>(ref));
    if (!object) {
        const eve::Status status = object.status();
        object.ignore("failed to create owned sensing proxy");
        std::invoke(std::forward<Release>(release), ref).ignore("rollback failed owned sensing allocation");
        return eve::script::projectStatusResult(vm, status);
    }

    ssq::Object owned = std::move(object).takeValue();
    auto        result = eve::script::projectStatusResult(vm, eve::Status::success(eve::StatusCode::Applied));
    result.set("value", owned);
    result.set("ownership", std::string("owned"));
    result.set("ownerEpoch", static_cast<std::int64_t>(ref.ownerEpoch));
    result.set("handle", static_cast<std::int64_t>(ref.packed()));
    return result;
}
}  // namespace
SensingWorld::SensingWorld()                                   = default;
SensingWorld::SensingWorld(SensingWorld&&) noexcept            = default;
SensingWorld& SensingWorld::operator=(SensingWorld&&) noexcept = default;
SensingWorld::~SensingWorld() { spatialCellQueryCache.invalidate(this); }

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

std::pair<float, float> project(float x, float y, float z, QueryAxes axes) {
    if (axes == QueryAxes::XZ) return {x, z};
    if (axes == QueryAxes::YZ) return {y, z};
    return {x, y};
}

bool insideShape(const Subject& subject, const QueryShape& shape, QueryAxes axes) {
    const auto [px, py] = project(subject.x, subject.y, subject.z, axes);
    return std::visit(
        [&](const auto& value) -> bool {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, std::monostate>) {
                return true;
            } else if constexpr (std::is_same_v<T, QueryCircle>) {
                const float dx = px - value.x;
                const float dy = py - value.y;
                return dx * dx + dy * dy <= value.radius * value.radius;
            } else if constexpr (std::is_same_v<T, QueryBox>) {
                return px >= value.minX && px <= value.maxX && py >= value.minY && py <= value.maxY;
            } else if constexpr (std::is_same_v<T, QuerySphere>) {
                return std::hypot(double(subject.x) - value.x, double(subject.y) - value.y,
                                  double(subject.z) - value.z) <= value.radius;
            } else {
                return insideCone(px, py, value);
            }
        },
        shape);
}

eve::Result<QuerySpec> circleSpec(float x, float y, float radius, std::string_view requireTagsCsv,
                                  std::string_view excludeTagsCsv, std::string_view includeFactionsCsv,
                                  std::string_view excludeFactionsCsv, std::string_view visibleTo, int limit) {
    if (!finite(x) || !finite(y) || !finite(radius) || radius < 0.f || limit < 0) {
        return eve::Result<QuerySpec>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument,
            "circle query requires finite coordinates, non-negative radius and limit", "query", {}, "sensing"));
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
        return eve::Result<QuerySpec>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "box query requires finite ordered bounds and a non-negative limit",
            "query", {}, "sensing"));
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
    if (axes > QueryAxes::XYZ || (axes == QueryAxes::XYZ && !std::holds_alternative<std::monostate>(shape) &&
                                  !std::holds_alternative<QuerySphere>(shape)))
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "XYZ queries require a sphere or range-only shape", "query.axes",
                                                       {}, "sensing"));
    if (!std::isfinite(minRange) || minRange < 0.f) {
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                 "QuerySpec.minRange must be finite and >= 0",
                                                                 "query.minRange", {}, "sensing"));
    }
    if (!(std::isfinite(maxRange) || maxRange == std::numeric_limits<float>::infinity()) || maxRange < minRange) {
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "QuerySpec.maxRange must be >= minRange (finite or +inf)",
            "query.maxRange", {}, "sensing"));
    }
    if (minCount > maxCount) {
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "QuerySpec.minCount exceeds maxCount", "query.count", {}, "sensing"));
    }
    for (const auto& tag : requiredTags) {
        if (tag.empty())
            return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                     "requiredTags cannot contain empty keys",
                                                                     "query.requiredTags", {}, "sensing"));
    }
    for (const auto& tag : excludedTags) {
        if (tag.empty())
            return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                     "excludedTags cannot contain empty keys",
                                                                     "query.excludedTags", {}, "sensing"));
    }
    for (const auto& faction : includeFactions) {
        if (faction.empty())
            return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                     "includeFactions cannot contain empty keys",
                                                                     "query.includeFactions", {}, "sensing"));
    }
    for (const auto& faction : excludeFactions) {
        if (faction.empty())
            return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                     "excludeFactions cannot contain empty keys",
                                                                     "query.excludeFactions", {}, "sensing"));
    }
    if (visibleTo && visibleTo->empty()) {
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                 "visibleTo cannot be empty when set",
                                                                 "query.visibleTo", {}, "sensing"));
    }
    const auto shapeOk = std::visit(
        [](const auto& value) -> eve::Result<void> {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, QueryCircle> || std::is_same_v<T, QuerySphere>) {
                if constexpr (std::is_same_v<T, QuerySphere>) {
                    if (!finite(value.z))
                        return Result<void>::failure(Diagnostic::error(
                            DiagnosticCode::InvalidArgument, "sphere Z must be finite", "query.shape", {}, "sensing"));
                }
                if (!finite(value.x) || !finite(value.y) || !finite(value.radius) || value.radius < 0.f) {
                    return eve::Result<void>::failure(eve::Diagnostic::error(
                        eve::DiagnosticCode::InvalidArgument,
                        "QueryCircle requires finite center and non-negative radius", "query.shape", {}, "sensing"));
                }
            } else if constexpr (std::is_same_v<T, QueryBox>) {
                if (!finite(value.minX) || !finite(value.minY) || !finite(value.maxX) || !finite(value.maxY) ||
                    value.minX > value.maxX || value.minY > value.maxY) {
                    return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                             "QueryBox requires finite ordered bounds",
                                                                             "query.shape", {}, "sensing"));
                }
            } else if constexpr (std::is_same_v<T, QueryCone>) {
                if (!finite(value.x) || !finite(value.y) || !finite(value.dirX) || !finite(value.dirY) ||
                    !finite(value.halfAngle) || !finite(value.range) || value.halfAngle < 0.f ||
                    value.halfAngle > 3.14159265f || value.range < 0.f || !(std::hypot(value.dirX, value.dirY) > 0.f)) {
                    return eve::Result<void>::failure(eve::Diagnostic::error(
                        eve::DiagnosticCode::InvalidArgument,
                        "QueryCone requires finite apex/dir, halfAngle in [0,pi], non-negative range, non-zero dir",
                        "query.shape", {}, "sensing"));
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
    return upsertPosition(id, x, y, 0.f, f, t, v);
}

Result<void> SensingWorld::upsertPosition(std::string_view id, float x, float y, float z, std::string_view f,
                                          std::string_view t, std::string_view v) {
    if (id.empty() || !finite(x) || !finite(y) || !finite(z)) {
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                 "subject id and finite coordinates are required",
                                                                 "subject", {}, "sensing"));
    }
    const float cellSize = spatialIndex3D_ ? spatialIndex3D_->getCellSize()
                           : spatialIndex_ ? spatialIndex_->getCellSize()
                                           : 1.f;
    if (spatialIndexEnabled() && !indexable(x, y, z, cellSize))
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "position exceeds index cell range", "subject.position", {}, "sensing"));
    const std::string key(id);
    auto              found = subjects_.find(key);
    if (found != subjects_.end() && found->second.zones.empty() && found->second.faction == f &&
        csvMatches(t, found->second.tags) && csvMatches(v, found->second.visibleTo)) {
        const bool positionChanged = found->second.x != x || found->second.y != y || found->second.z != z;
        found->second.x            = x;
        found->second.y            = y;
        found->second.z            = z;
        if (spatialIndexEnabled() && positionChanged) indexSubject(found->second);
        return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
    }
    Subject    subject{key, x, y, std::string(f), csv(t), csv(v), {}, z};
    const bool positionChanged =
        found == subjects_.end() || found->second.x != x || found->second.y != y || found->second.z != z;
    if (found == subjects_.end()) {
        const auto key = subject.id;
        found          = subjects_.emplace(key, std::move(subject)).first;
    } else {
        found->second = std::move(subject);
    }
    if (spatialIndexEnabled() && positionChanged) indexSubject(found->second);
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> SensingWorld::remove(std::string_view id) {
    results_.clear();
    const std::string key(id);
    if (!subjects_.contains(key))
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::NotFound, "subject is not registered", "subject.id", {}, "sensing"));
    unindexSubject(key);
    subjects_.erase(key);
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> SensingWorld::setZones(std::string_view id, std::string_view zonesCsv) {
    const std::string key(id);
    auto              it = subjects_.find(key);
    if (it == subjects_.end())
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::NotFound, "subject is not registered", "subject.id", {}, "sensing"));
    auto zones = csv(zonesCsv);
    for (const auto& zone : zones) {
        if (zone.empty())
            return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                     "zones cannot contain empty keys", "subject.zones",
                                                                     {}, "sensing"));
        if (!eve::LogicalId::parse(zone))
            return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                     "zones entries must be valid LogicalId texts",
                                                                     "subject.zones", {}, "sensing"));
    }
    it->second.zones = std::move(zones);
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> SensingWorld::setSpatialIndexEnabled(bool enabled, float cellSize) {
    return rebuildIndex(enabled ? SpatialIndexKind::XY : SpatialIndexKind::None, cellSize);
}

SpatialIndexKind SensingWorld::indexKind() const noexcept { return indexKind_; }
bool             SensingWorld::spatialIndexEnabled() const noexcept { return spatialIndex_ || spatialIndex3D_; }

Result<void> SensingWorld::rebuildIndex(SpatialIndexKind kind, float cellSize) {
    if (kind > SpatialIndexKind::XYZ || (kind != SpatialIndexKind::None && (!finite(cellSize) || cellSize <= 0)))
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "invalid spatial index kind or cell size", "spatial", {}, "sensing"));
    for (const auto& [id, subject] : subjects_) {
        if (kind != SpatialIndexKind::None && !indexable(subject.x, subject.y, subject.z, cellSize))
            return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                           "subject exceeds index cell range", id, {}, "sensing"));
    }
    // Build in a separate owner; allocation failure cannot replace the current derived index.
    SensingWorld next;
    next.subjects_  = subjects_;
    next.indexKind_ = kind;
    if (kind == SpatialIndexKind::XYZ)
        next.spatialIndex3D_ = std::make_unique<spatial::SpatialHash3D>(cellSize);
    else if (kind != SpatialIndexKind::None)
        next.spatialIndex_ = std::make_unique<spatial::SpatialHash2D>(cellSize);
    next.rebuildSpatialIndex();
    for (const auto& [id, sid] : next.subjectSpatialIds_) next.spatialIdSubjects_.at(sid) = &subjects_.at(id);
    spatialCellQueryCache.invalidate(this);
    spatialIndex_      = std::move(next.spatialIndex_);
    spatialIndex3D_    = std::move(next.spatialIndex3D_);
    indexKind_         = kind;
    subjectSpatialIds_ = std::move(next.subjectSpatialIds_);
    spatialIdSubjects_ = std::move(next.spatialIdSubjects_);
    nextSpatialId_     = next.nextSpatialId_;
    return Result<void>::success(Status::success(StatusCode::Applied));
}

void SensingWorld::clearSpatialIndex() {
    spatialCellQueryCache.invalidate(this);
    spatialIndex_.reset();
    spatialIndex3D_.reset();
    indexKind_ = SpatialIndexKind::None;
    subjectSpatialIds_.clear();
    spatialIdSubjects_.clear();
    nextSpatialId_ = 1;
}

void SensingWorld::rebuildSpatialIndex() {
    spatialCellQueryCache.invalidate(this);
    subjectSpatialIds_.clear();
    spatialIdSubjects_.clear();
    nextSpatialId_ = 1;
    if (!spatialIndexEnabled()) return;
    if (spatialIndex_) spatialIndex_->clear();
    if (spatialIndex3D_) spatialIndex3D_->clear();
    for (const auto& [id, subject] : subjects_) {
        (void)id;
        indexSubject(subject);
    }
}

void SensingWorld::indexSubject(const Subject& subject) {
    if (!spatialIndexEnabled()) return;
    spatialCellQueryCache.invalidate(this);
    auto [it, inserted] = subjectSpatialIds_.try_emplace(subject.id, nextSpatialId_);
    if (inserted) ++nextSpatialId_;
    const int sid           = it->second;
    spatialIdSubjects_[sid] = &subject;
    if (spatialIndex3D_) {
        spatialIndex3D_->insert(sid, subject.x, subject.y, subject.z, subject.x, subject.y, subject.z);
    } else {
        const auto axes   = indexKind_ == SpatialIndexKind::XZ   ? QueryAxes::XZ
                            : indexKind_ == SpatialIndexKind::YZ ? QueryAxes::YZ
                                                                 : QueryAxes::XY;
        const auto [x, y] = project(subject.x, subject.y, subject.z, axes);
        spatialIndex_->insert(sid, x, y, x, y);
    }
}

void SensingWorld::unindexSubject(const std::string& id) {
    auto it = subjectSpatialIds_.find(id);
    if (it == subjectSpatialIds_.end()) return;
    spatialCellQueryCache.invalidate(this);
    const int sid = it->second;
    if (spatialIndex_) spatialIndex_->remove(sid);
    if (spatialIndex3D_) spatialIndex3D_->remove(sid);
    spatialIdSubjects_.erase(sid);
    subjectSpatialIds_.erase(it);
}

bool SensingWorld::trySpatialBroadphase(const QueryOrigin& origin, const QuerySpec& spec,
                                        std::vector<const Subject*>& out) const {
    if (!spatialIndexEnabled()) return false;
    // A bounded 3D sphere can be conservatively projected into any planar index.
    const auto* sphere  = std::get_if<QuerySphere>(&spec.shape);
    const bool  range3D = spec.axes == QueryAxes::XYZ && std::isfinite(spec.maxRange);
    if (sphere || range3D) {
        const float x        = sphere ? sphere->x : origin.x;
        const float y        = sphere ? sphere->y : origin.y;
        const float z        = sphere ? sphere->z : origin.z;
        const float r        = sphere ? sphere->radius : spec.maxRange;
        const float cellSize = spatialIndex3D_ ? spatialIndex3D_->getCellSize() : spatialIndex_->getCellSize();
        if (!boundedQuery(x, y, z, r, cellSize, spatialIndex3D_ ? 3 : 2)) return false;
        if (spatialIndex3D_)
            spatialIndex3D_->querySphere(x, y, z, r);
        else {
            const auto axes   = indexKind_ == SpatialIndexKind::XZ   ? QueryAxes::XZ
                                : indexKind_ == SpatialIndexKind::YZ ? QueryAxes::YZ
                                                                     : QueryAxes::XY;
            const auto [u, v] = project(x, y, z, axes);
            spatialIndex_->queryCircle(u, v, r);
        }
        const int count = spatialIndex3D_ ? spatialIndex3D_->getResultCount() : spatialIndex_->getResultCount();
        for (int i = 0; i < count; ++i) {
            const int id = spatialIndex3D_ ? spatialIndex3D_->getResultId(i) : spatialIndex_->getResultId(i);
            out.push_back(spatialIdSubjects_.at(id));
        }
        return true;
    }
    // Unbounded height cannot be culled by a different plane or a finite 3D volume.
    const auto axes = indexKind_ == SpatialIndexKind::XZ   ? QueryAxes::XZ
                      : indexKind_ == SpatialIndexKind::YZ ? QueryAxes::YZ
                                                           : QueryAxes::XY;
    if (!spatialIndex_ || spec.axes != axes) return false;
    const auto [originU, originV] = project(origin.x, origin.y, origin.z, axes);
    const bool bounded            = std::visit(
        [&](const auto& shape) {
            using T          = std::decay_t<decltype(shape)>;
            const float size = spatialIndex_->getCellSize();
            if constexpr (std::is_same_v<T, std::monostate>)
                return boundedQuery(originU, originV, 0, spec.maxRange, size, 2);
            else if constexpr (std::is_same_v<T, QueryBox>)
                return boundedQuery(shape.minX, shape.minY, 0,
                                               std::max(shape.maxX - shape.minX, shape.maxY - shape.minY), size, 2);
            else if constexpr (std::is_same_v<T, QueryCone>)
                return boundedQuery(shape.x, shape.y, 0, shape.range, size, 2);
            else
                return boundedQuery(shape.x, shape.y, 0, shape.radius, size, 2);
        },
        spec.shape);
    if (!bounded) return false;
    if (const auto* circle = std::get_if<QueryCircle>(&spec.shape); circle && axes == QueryAxes::XY) {
        spatialCellQueryCache.select(this, spatialIndex_.get());
        auto&                              cache    = spatialCellQueryCache;
        const float                        cellSize = spatialIndex_->getCellSize();
        const std::array<int, 4>           cells{static_cast<int>(std::floor((circle->x - circle->radius) / cellSize)),
                                       static_cast<int>(std::floor((circle->y - circle->radius) / cellSize)),
                                       static_cast<int>(std::floor((circle->x + circle->radius) / cellSize)),
                                       static_cast<int>(std::floor((circle->y + circle->radius) / cellSize))};
        constexpr std::size_t              maxCachedCells           = 256;
        constexpr std::size_t              maxCachedIdsPerCellRange = 512;
        constexpr std::size_t              maxCachedIds             = 32768;
        std::vector<const Subject*>        uncached;
        const std::vector<const Subject*>* subjects = nullptr;
        const auto                         cached   = cache.candidates.find(cells);
        if (cached != cache.candidates.end()) {
            subjects = &cached->second;
        } else {
            spatialIndex_->queryRect(cells[0] * cellSize, cells[1] * cellSize,
                                     static_cast<float>((static_cast<double>(cells[2]) + 1.0) * cellSize),
                                     static_cast<float>((static_cast<double>(cells[3]) + 1.0) * cellSize));
            uncached.reserve(static_cast<std::size_t>(spatialIndex_->getResultCount()));
            for (int i = 0; i < spatialIndex_->getResultCount(); ++i) {
                const auto found = spatialIdSubjects_.find(spatialIndex_->getResultId(i));
                if (found != spatialIdSubjects_.end()) uncached.push_back(found->second);
            }
            if (uncached.size() <= maxCachedIdsPerCellRange) {
                if (cache.candidates.size() == maxCachedCells ||
                    cache.candidateCount + uncached.size() > maxCachedIds) {
                    cache.candidates.clear();
                    cache.candidateCount = 0;
                }
                cache.candidateCount += uncached.size();
                subjects = &cache.candidates.emplace(cells, std::move(uncached)).first->second;
            } else {
                subjects = &uncached;
            }
        }
        out.clear();
        out.reserve(subjects->size());
        for (const Subject* subject : *subjects) {
            if (eve::spatial::AABB2{subject->x, subject->y, subject->x, subject->y}.intersectsCircle(
                    circle->x, circle->y, circle->radius))
                out.push_back(subject);
        }
        return true;
    }
    const bool ran = std::visit(
        [&](const auto& value) -> bool {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, std::monostate>) {
                if (!(std::isfinite(spec.maxRange))) return false;
                spatialIndex_->queryCircle(originU, originV, spec.maxRange);
                return true;
            } else if constexpr (std::is_same_v<T, QueryCircle>) {
                spatialIndex_->queryCircle(value.x, value.y, value.radius);
                return true;
            } else if constexpr (std::is_same_v<T, QueryBox>) {
                spatialIndex_->queryRect(value.minX, value.minY, value.maxX, value.maxY);
                return true;
            } else if constexpr (std::is_same_v<T, QuerySphere>) {
                return false;
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
        out.push_back(map->second);
    }
    return true;
}

bool SensingWorld::accepts(const Subject& subject, const QuerySpec& spec) const {
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
    for (const auto& tag : spec.requiredTags)
        if (!subject.tags.count(tag)) return false;
    for (const auto& tag : spec.excludedTags)
        if (subject.tags.count(tag)) return false;
    if (spec.visibleTo && !subject.visibleTo.count(*spec.visibleTo)) return false;
    return insideShape(subject, spec.shape, spec.axes);
}

void SensingWorld::publishResults(std::vector<RankedCandidate> ranked) {
    results_.clear();
    results_.reserve(ranked.size());
    for (const auto& candidate : ranked)
        results_.push_back(Candidate{candidate.id, candidate.x, candidate.y, candidate.distance, candidate.z});
    lastQuery_.ranked   = ranked;
    lastQuery_.accepted = static_cast<std::uint32_t>(ranked.size());
}

eve::Result<CandidateCollection> SensingWorld::collectCandidates(const QueryOrigin& origin,
                                                                 const QuerySpec&   spec) const {
    auto valid = spec.validate();
    if (!valid) return eve::Result<CandidateCollection>::failure(valid.status());
    if (!finite(origin.x) || !finite(origin.y) || !finite(origin.z) ||
        (origin.space != CoordinateSpace::World2D && origin.space != CoordinateSpace::World3D) ||
        (origin.space == CoordinateSpace::World2D &&
         (origin.z != 0 || spec.axes != QueryAxes::XY || std::holds_alternative<QuerySphere>(spec.shape)))) {
        return eve::Result<CandidateCollection>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "QueryOrigin coordinates must be finite",
                                   "query.origin", {}, "sensing"));
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
        const auto [sx, sy]  = project(subject.x, subject.y, subject.z, spec.axes);
        const auto [ox, oy]  = project(origin.x, origin.y, origin.z, spec.axes);
        const float distance = spec.axes == QueryAxes::XYZ
                                   ? std::hypot(subject.x - origin.x, subject.y - origin.y, subject.z - origin.z)
                                   : std::hypot(sx - ox, sy - oy);
        if (distance < spec.minRange || distance > spec.maxRange) continue;
        RankedCandidate candidate;
        candidate.id          = subject.id;
        candidate.x           = subject.x;
        candidate.y           = subject.y;
        candidate.z           = subject.z;
        candidate.space       = origin.space;
        candidate.distance    = distance;
        candidate.score       = -distance;
        candidate.scoreReason = "distance";
        ranked.push_back(std::move(candidate));
    }

    return eve::Result<CandidateCollection>::success(
        CandidateCollection{std::move(ranked), usedSpatial, static_cast<std::uint32_t>(candidates.size())});
}

eve::Result<CandidateQueryResult> SensingWorld::query(const QueryOrigin& origin, const QuerySpec& spec) {
    return query(origin, spec, {});
}

Result<CandidateQueryResult> SensingWorld::query(const QueryOrigin& origin, const QuerySpec& spec,
                                                 OptionalRef<const ILineOfSightQuery> lineOfSight) {
    if (spec.requireLineOfSight && !lineOfSight)
        return Result<CandidateQueryResult>::failure(Diagnostic::error(DiagnosticCode::Unsupported,
                                                                       "query requires a line-of-sight provider",
                                                                       "query.lineOfSight", {}, "sensing"));
    auto collected = collectCandidates(origin, spec);
    if (!collected) return eve::Result<CandidateQueryResult>::failure(collected.status());
    auto collection = std::move(collected).takeValue();
    auto ranked     = std::move(collection.candidates);
    if (spec.requireLineOfSight) {
        auto from = origin.space == CoordinateSpace::World3D ? WorldPoint::world3D(origin.x, origin.y, origin.z)
                                                             : WorldPoint::world2D(origin.x, origin.y);
        if (!from) return Result<CandidateQueryResult>::failure(from.status());
        std::vector<RankedCandidate> visible;
        for (auto& candidate : ranked) {
            auto to = candidate.space == CoordinateSpace::World3D
                          ? WorldPoint::world3D(candidate.x, candidate.y, candidate.z)
                          : WorldPoint::world2D(candidate.x, candidate.y);
            if (!to) return Result<CandidateQueryResult>::failure(to.status());
            try {
                auto sight = lineOfSight->get().query(TargetLocation{from.value()}, TargetLocation{to.value()});
                if (!sight) return Result<CandidateQueryResult>::failure(sight.status());
                if (sight.value().visible) visible.push_back(std::move(candidate));
            } catch (const std::exception& error) {
                return Result<CandidateQueryResult>::failure(
                    Diagnostic::error(DiagnosticCode::Failed, error.what(), "query.lineOfSight", {}, "sensing"));
            }
        }
        ranked = std::move(visible);
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
            return eve::Result<CandidateQueryResult>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::PreconditionViolation,
                "candidate count is below QuerySpec.minCount after truncation", "query.count", {}, "sensing"));
        }
    } else if (ranked.size() < static_cast<std::size_t>(spec.minCount) ||
               ranked.size() > static_cast<std::size_t>(spec.maxCount)) {
        return eve::Result<CandidateQueryResult>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::PreconditionViolation,
                                   "candidate count violates QuerySpec count policy", "query.count", {}, "sensing"));
    }

    lastQuery_.usedSpatial = collection.usedSpatial;
    lastQuery_.scanned     = collection.scanned;
    lastQuery_.originX     = origin.x;
    lastQuery_.originY     = origin.y;
    lastQuery_.originZ     = origin.z;
    lastQuery_.space       = origin.space;
    lastQuery_.shapeKind   = std::visit(
        [](const auto& value) -> std::string {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, QueryCircle>) return "circle";
            if constexpr (std::is_same_v<T, QueryBox>) return "box";
            if constexpr (std::is_same_v<T, QueryCone>) return "cone";
            if constexpr (std::is_same_v<T, QuerySphere>) return "sphere";
            return "none";
        },
        spec.shape);

    publishResults(ranked);
    return eve::Result<CandidateQueryResult>::success(CandidateQueryResult{std::move(ranked)});
}

eve::Result<int> SensingWorld::circle(float x, float y, float radius, std::string_view r, std::string_view e,
                                      std::string_view i, std::string_view ef, std::string_view v, int limit) {
    auto spec = circleSpec(x, y, radius, r, e, i, ef, v, limit);
    if (!spec) return eve::Result<int>::failure(spec.status());
    auto result = query(QueryOrigin{x, y, std::nullopt}, std::move(spec).takeValue());
    if (!result) return eve::Result<int>::failure(result.status());
    return eve::Result<int>::success(static_cast<int>(std::move(result).takeValue().size()));
}

eve::Result<int> SensingWorld::box(float a, float b, float c, float d, std::string_view r, std::string_view e,
                                   std::string_view i, std::string_view ef, std::string_view v, int limit) {
    auto spec = boxSpec(a, b, c, d, r, e, i, ef, v, limit);
    if (!spec) return eve::Result<int>::failure(spec.status());
    const float originX = (a + c) * 0.5f;
    const float originY = (b + d) * 0.5f;
    auto        result  = query(QueryOrigin{originX, originY, std::nullopt}, std::move(spec).takeValue());
    if (!result) return eve::Result<int>::failure(result.status());
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
    if (!executed) return eve::Result<int>::failure(executed.status());
    auto rankedResult = std::move(executed).takeValue();
    std::vector<RankedCandidate> ranked(rankedResult.ranked().begin(), rankedResult.ranked().end());
    const int count = static_cast<int>(ranked.size());
    lastQuery_.usedSpatial             = context.usedSpatial;
    lastQuery_.scanned                 = context.scanned;
    lastQuery_.originX                 = originX;
    lastQuery_.originY                 = originY;
    lastQuery_.originZ                 = 0.f;
    lastQuery_.space                   = CoordinateSpace::World2D;
    lastQuery_.shapeKind               = "preset";
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
      << ",\"origin\":{\"x\":" << lastQuery_.originX << ",\"y\":" << lastQuery_.originY
      << ",\"z\":" << lastQuery_.originZ << ",\"space\":" << quote(coordinateSpaceName(lastQuery_.space)) << '}'
      << ",\"shape\":" << quote(lastQuery_.shapeKind)
      << ",\"spatial\":{\"enabled\":" << (spatialIndexEnabled() ? "true" : "false")
      << ",\"used\":" << (lastQuery_.usedSpatial ? "true" : "false") << ",\"scanned\":" << lastQuery_.scanned
      << ",\"accepted\":" << lastQuery_.accepted << '}' << ",\"ranked\":[";
    bool first = true;
    for (const auto& candidate : lastQuery_.ranked) {
        if (!first) o << ',';
        first = false;
        o << "{\"id\":" << quote(candidate.id) << ",\"x\":" << candidate.x << ",\"y\":" << candidate.y
          << ",\"z\":" << candidate.z << ",\"space\":" << quote(coordinateSpaceName(candidate.space))
          << ",\"distance\":" << candidate.distance << ",\"score\":" << candidate.score
          << ",\"scoreReason\":" << quote(candidate.scoreReason) << '}';
    }
    o << "],\"environment\":" << lastEnvironmentQuery_ << '}';
    return o.str();
}

std::string SensingWorld::snapshotJson() const {
    std::ostringstream o;
    o << "{\"schema\":\"eve.sensing.world\",\"version\":2,\"subjects\":[";
    bool first = true;
    for (auto& [id, s] : subjects_) {
        if (!first) o << ',';
        first = false;
        o << "{\"id\":" << quote(id) << ",\"x\":" << std::setprecision(9) << s.x << ",\"y\":" << s.y << ",\"z\":" << s.z
          << ",\"faction\":" << quote(s.faction) << ",\"tags\":" << quote(join(s.tags))
          << ",\"visibleTo\":" << quote(join(s.visibleTo)) << ",\"zones\":" << quote(join(s.zones)) << '}';
    }
    return o.str() + "]}";
}
eve::Result<void> SensingWorld::restoreJson(const std::string& j) {
    std::string err;
    auto        d = eve::json::Document::parse(j, &err);
    if (!d.valid() || !d.root().isObject() || d.root().getString("schema") != "eve.sensing.world" ||
        (d.root().getInt("version") != 1 && d.root().getInt("version") != 2) || !d.root().get("subjects").isArray()) {
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
        if (!v.get("x").isNumber() || !v.get("y").isNumber() ||
            (d.root().getInt("version") == 2 && !v.get("z").isNumber()) || next.subjects_.contains(v.getString("id")))
            return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                           "invalid or duplicate snapshot position", "subjects", {},
                                                           "sensing.restore"));
        auto subject = next.upsertPosition(v.getString("id"), float(v.getDouble("x")), float(v.getDouble("y")),
                                           d.root().getInt("version") == 1 ? 0.f : float(v.getDouble("z")),
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
    const float size    = spatialIndex3D_ ? spatialIndex3D_->getCellSize()
                          : spatialIndex_ ? spatialIndex_->getCellSize()
                                          : 1.f;
    auto        rebuilt = next.rebuildIndex(indexKind_, size);
    if (!rebuilt) return Result<void>::failure(rebuilt.status());
    spatialCellQueryCache.invalidate(this);
    *this = std::move(next);
    results_.clear();
    lastQuery_ = {};
    lastEnvironmentQuery_ = "null";
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
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::StaleHandle, "Sensing module is no longer loaded", "world", {}, "sensing"));
    return module->worlds_.erase(reference);
}

bool Sensing::isStale(SensingWorldHandleRef reference) noexcept {
    if (!reference.isValid()) return false;
    Sensing* module = ModuleManager::getInstance<Sensing>("Sensing");
    return !module || module->worlds_.isStale(reference);
}


Sensing::Sensing() {
    eve::cap::provide<eve::ISensingQuery>(this);
    declareScriptSensingCandidateSeam();
}

Sensing::~Sensing() {
    withdrawScriptSensingCandidateSeam();
    eve::cap::revoke<eve::ISensingQuery>(this);
}

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
    c.addFunc("getZ", [](Candidate* v) { return v ? v->z : 0.f; });
    c.addFunc("getDistance", [](Candidate* v) { return v ? v->distance : 0.f; });
    const HSQUIRRELVM vm = t.getHandle();
    auto              w  = t.addClass<ScriptSensingWorld>("SensingWorld",
                                                          std::function<ScriptSensingWorld*()>([]() { return nullptr; }), false);
    exposeSensingQueries(t, w);
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
            return eve::script::projectResult(vm, eve::Result<void>::failure(eve::Diagnostic::error(
                                                      eve::DiagnosticCode::InvalidArgument,
                                                      "sensing world proxy must not be null", "world", {}, "sensing")));
        return eve::script::projectResult(vm, Sensing::release(value->reference));
    });
    w.addFunc("upsert", [vm](ScriptSensingWorld* value, const std::string& id, float x, float y,
                             const std::string& faction, const std::string& tags, const std::string& visibleTo) {
        if (!value)
            return eve::script::projectResult(vm, eve::Result<void>::failure(eve::Diagnostic::error(
                                                      eve::DiagnosticCode::InvalidArgument,
                                                      "sensing world proxy must not be null", "world", {}, "sensing")));
        auto world = Sensing::resolve(value->reference);
        if (!world.isBound())
            return eve::script::projectResult(
                vm, eve::Result<void>::failure(eve::Diagnostic::error(
                        eve::DiagnosticCode::StaleHandle, "sensing world handle is stale", "world", {}, "sensing")));
        return eve::script::projectResult(vm, world->upsert(id, x, y, faction, tags, visibleTo));
    });
    w.addFunc("remove", [vm](ScriptSensingWorld* value, const std::string& id) {
        if (!value)
            return eve::script::projectResult(vm, eve::Result<void>::failure(eve::Diagnostic::error(
                                                      eve::DiagnosticCode::InvalidArgument,
                                                      "sensing world proxy must not be null", "world", {}, "sensing")));
        auto world = Sensing::resolve(value->reference);
        if (!world.isBound())
            return eve::script::projectResult(
                vm, eve::Result<void>::failure(eve::Diagnostic::error(
                        eve::DiagnosticCode::StaleHandle, "sensing world handle is stale", "world", {}, "sensing")));
        return eve::script::projectResult(vm, world->remove(id));
    });
    w.addFunc("setZones", [vm](ScriptSensingWorld* value, const std::string& id, const std::string& zones) {
        if (!value)
            return eve::script::projectResult(vm, eve::Result<void>::failure(eve::Diagnostic::error(
                                                      eve::DiagnosticCode::InvalidArgument,
                                                      "sensing world proxy must not be null", "world", {}, "sensing")));
        auto world = Sensing::resolve(value->reference);
        if (!world.isBound())
            return eve::script::projectResult(
                vm, eve::Result<void>::failure(eve::Diagnostic::error(
                        eve::DiagnosticCode::StaleHandle, "sensing world handle is stale", "world", {}, "sensing")));
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
                                              eve::Result<int>::failure(eve::Diagnostic::error(
                                                  eve::DiagnosticCode::InvalidArgument,
                                                  "sensing world proxy must not be null", "world", {}, "sensing")),
                                              [](int count) { return eve::Value(static_cast<std::int64_t>(count)); });
        auto world = Sensing::resolve(value->reference);
        if (!world.isBound())
            return eve::script::projectResult(
                vm,
                eve::Result<int>::failure(eve::Diagnostic::error(
                    eve::DiagnosticCode::StaleHandle, "sensing world handle is stale", "world", {}, "sensing")),
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
                                              eve::Result<int>::failure(eve::Diagnostic::error(
                                                  eve::DiagnosticCode::InvalidArgument,
                                                  "sensing world proxy must not be null", "world", {}, "sensing")),
                                              [](int count) { return eve::Value(static_cast<std::int64_t>(count)); });
        auto world = Sensing::resolve(value->reference);
        if (!world.isBound())
            return eve::script::projectResult(
                vm,
                eve::Result<int>::failure(eve::Diagnostic::error(
                    eve::DiagnosticCode::StaleHandle, "sensing world handle is stale", "world", {}, "sensing")),
                [](int count) { return eve::Value(static_cast<std::int64_t>(count)); });
        return projectCount(world->box(minX, minY, maxX, maxY, required, excluded, includedFactions, excludedFactions,
                                       visibleTo, limit));
    });
    w.addFunc("executePreset", [vm, projectCount](ScriptSensingWorld* value, const std::string& presetId, float originX,
                                                  float originY, float dirX, float dirY) {
        if (!value)
            return eve::script::projectResult(vm,
                                              eve::Result<int>::failure(eve::Diagnostic::error(
                                                  eve::DiagnosticCode::InvalidArgument,
                                                  "sensing world proxy must not be null", "world", {}, "sensing")),
                                              [](int count) { return eve::Value(static_cast<std::int64_t>(count)); });
        auto world = Sensing::resolve(value->reference);
        if (!world.isBound())
            return eve::script::projectResult(
                vm,
                eve::Result<int>::failure(eve::Diagnostic::error(
                    eve::DiagnosticCode::StaleHandle, "sensing world handle is stale", "world", {}, "sensing")),
                [](int count) { return eve::Value(static_cast<std::int64_t>(count)); });
        return projectCount(world->executePreset(presetId, originX, originY, dirX, dirY));
    });
    w.addFunc("queryPositions", [vm](ScriptSensingWorld* value, float originX, float originY, float targetX,
                                     float targetY, float radius, int samples, int limit, bool requireLineOfSight) {
        if (!value)
            return eve::script::projectResult(
                vm,
                Result<Value>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "sensing world proxy is null",
                                                         "world", {}, "sensing")),
                [](Value data) { return data; });
        auto world = Sensing::resolve(value->reference);
        if (!world.isBound())
            return eve::script::projectResult(
                vm,
                Result<Value>::failure(Diagnostic::error(DiagnosticCode::StaleHandle, "sensing world handle is stale",
                                                         "world", {}, "sensing")),
                [](Value data) { return data; });
        return eve::script::projectResult(
            vm, world->queryPositions(originX, originY, targetX, targetY, radius, samples, limit, requireLineOfSight),
            [](Value data) { return data; });
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
            return eve::script::projectResult(vm,
                                              eve::Result<std::string>::failure(eve::Diagnostic::error(
                                                  eve::DiagnosticCode::InvalidArgument,
                                                  "sensing world proxy must not be null", "world", {}, "sensing")),
                                              [](std::string text) { return eve::Value(std::move(text)); });
        auto world = Sensing::resolve(value->reference);
        if (!world.isBound())
            return eve::script::projectResult(
                vm,
                eve::Result<std::string>::failure(eve::Diagnostic::error(
                    eve::DiagnosticCode::StaleHandle, "sensing world handle is stale", "world", {}, "sensing")),
                [](std::string text) { return eve::Value(std::move(text)); });
        return eve::script::projectResult(vm, eve::Result<std::string>::success(world->snapshotJson()),
                                          [](std::string text) { return eve::Value(std::move(text)); });
    });
    w.addFunc("restoreJson", [vm](ScriptSensingWorld* value, const std::string& json) {
        if (!value)
            return eve::script::projectResult(vm, eve::Result<void>::failure(eve::Diagnostic::error(
                                                      eve::DiagnosticCode::InvalidArgument,
                                                      "sensing world proxy must not be null", "world", {}, "sensing")));
        auto world = Sensing::resolve(value->reference);
        if (!world.isBound())
            return eve::script::projectResult(
                vm, eve::Result<void>::failure(eve::Diagnostic::error(
                        eve::DiagnosticCode::StaleHandle, "sensing world handle is stale", "world", {}, "sensing")));
        return eve::script::projectResult(vm, world->restoreJson(json));
    });
    w.addFunc("setSpatialIndexEnabled", [vm](ScriptSensingWorld* value, bool enabled, float cellSize) {
        if (!value)
            return eve::script::projectResult(vm, eve::Result<void>::failure(eve::Diagnostic::error(
                                                      eve::DiagnosticCode::InvalidArgument,
                                                      "sensing world proxy must not be null", "world", {}, "sensing")));
        auto world = Sensing::resolve(value->reference);
        if (!world.isBound())
            return eve::script::projectResult(
                vm, eve::Result<void>::failure(eve::Diagnostic::error(
                        eve::DiagnosticCode::StaleHandle, "sensing world handle is stale", "world", {}, "sensing")));
        return eve::script::projectResult(vm, world->setSpatialIndexEnabled(enabled, cellSize));
    });
    w.addFunc("spatialIndexEnabled", [](ScriptSensingWorld* value) {
        if (!value) return false;
        auto world = Sensing::resolve(value->reference);
        return world.isBound() && world->spatialIndexEnabled();
    });
    w.addFunc("debugLastQueryJson", [vm](ScriptSensingWorld* value) {
        if (!value)
            return eve::script::projectResult(vm,
                                              eve::Result<std::string>::failure(eve::Diagnostic::error(
                                                  eve::DiagnosticCode::InvalidArgument,
                                                  "sensing world proxy must not be null", "world", {}, "sensing")),
                                              [](std::string text) { return eve::Value(std::move(text)); });
        auto world = Sensing::resolve(value->reference);
        if (!world.isBound())
            return eve::script::projectResult(
                vm,
                eve::Result<std::string>::failure(eve::Diagnostic::error(
                    eve::DiagnosticCode::StaleHandle, "sensing world handle is stale", "world", {}, "sensing")),
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
