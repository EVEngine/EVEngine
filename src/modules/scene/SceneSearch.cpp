#include "scene/SceneSearch.h"

#include "common/Capability.h"
#include "scene/SceneHost.h"
#include "scene/SceneTransformInternal.h"

#ifdef _MSC_VER
// MSVC's back-end does not honor SYSTEM headers for C4701. In upstream
// addPoint, internal_id_replaced is assigned and read under the same
// is_vacant_place condition; replacement is disabled for our immutable index.
#pragma warning(push)
#pragma warning(disable : 4701)
#endif
#include <hnswlib/hnswlib.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <locale>
#include <set>
#include <sstream>

namespace eve::scene {
struct SceneHnswIndex {
    hnswlib::InnerProductSpace      space;
    hnswlib::HierarchicalNSW<float> graph;
    explicit SceneHnswIndex(std::size_t dimensions, std::size_t count)
        : space(dimensions), graph(&space, std::max(std::size_t{1}, count), 16, 200, 100) {
        graph.setEf(128);
    }
};

SceneSearch::SceneSearch()  = default;
SceneSearch::~SceneSearch() = default;

namespace {
constexpr std::size_t maxNodes = 4096, maxDimensions = 1024, maxTextBytes = 4096;

template <class T>
Result<T> failure(DiagnosticCode code, std::string message) {
    return Result<T>::failure(Diagnostic::error(code, std::move(message), "scene.search"));
}

struct Capture {
    SceneSearchSnapshot snapshot;
    std::string         source;
    std::string         geometry;
};

Result<Capture> capture(std::string_view name) {
    if (name.empty() || name.size() > 256)
        return failure<Capture>(DiagnosticCode::InvalidArgument, "host must contain 1..256 bytes");
    SceneHost* host = nullptr;
    if (ecs::current()->getManager<SceneHost>()) {
        for (auto [meta] : ecs::View<SceneHost, SceneHost::Meta>()) {
            if (meta->name == name) {
                if (host) return failure<Capture>(DiagnosticCode::Conflict, "ambiguous host name");
                host = meta->entity;
            }
        }
    }
    if (!host) return failure<Capture>(DiagnosticCode::NotFound, "scene host not found");
    const auto& nodes = host->tree()->nodes;
    if (nodes.size() > maxNodes)
        return failure<Capture>(DiagnosticCode::InvalidArgument, "snapshot node budget exceeded (4096)");
    Capture out;
    out.snapshot.host = std::string(name);
    std::ostringstream source;
    source.imbue(std::locale::classic());
    source << std::hexfloat;
    const auto handle = ecs::handle_of(host);
    // Process-local identity only; never serialized or exposed as a persistent object identity.
    source << handle.table << ':' << handle.id << ':' << handle.generation;
    std::ostringstream geometry;
    geometry.imbue(std::locale::classic());
    geometry << std::hexfloat;
    std::vector<glm::mat4>     worlds(nodes.size());
    std::vector<bool>          visibility(nodes.size());
    std::vector<unsigned char> visited(nodes.size(), 0);
    std::set<std::string>      ids;
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        std::vector<std::size_t> chain;
        int                      cursor = int(i);
        while (cursor >= 0) {
            if (std::size_t(cursor) >= nodes.size() || visited[std::size_t(cursor)] == 1)
                return failure<Capture>(DiagnosticCode::InvalidArgument, "invalid scene ancestry or cycle");
            const auto index = std::size_t(cursor);
            if (visited[index] == 2) break;
            visited[index] = 1;
            chain.push_back(index);
            cursor = nodes[index].parent;
            if (cursor < -1) return failure<Capture>(DiagnosticCode::InvalidArgument, "invalid scene parent");
        }
        for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
            const auto  index = *it;
            const auto& node  = nodes[index];
            worlds[index] =
                (node.parent < 0 ? glm::mat4(1.f) : worlds[std::size_t(node.parent)]) * detail::localMatrix(node);
            visibility[index] =
                node.visible && (node.parent < 0 ? host->meta()->visible : visibility[std::size_t(node.parent)]);
            visited[index] = 2;
        }
    }
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        const auto& node = nodes[i];
        if (node.id.empty() || !ids.insert(node.id).second)
            return failure<Capture>(DiagnosticCode::InvalidArgument, "empty or duplicate scene node id");
        SceneSearchRecord record;
        record.id           = node.id;
        record.persistentId = node.persistentId.isNil() ? "" : node.persistentId.format();
        record.name         = node.name;
        record.space        = node.space;
        record.tags         = node.tags;
        record.visible      = visibility[i];
        record.layer        = node.layer;
        record.position     = {worlds[i][3][0], worlds[i][3][1], worlds[i][3][2]};
        const auto forward  = glm::vec3(worlds[i][2]);
        const auto up       = glm::vec3(worlds[i][1]);
        if (glm::length(forward) > 1e-6f && glm::length(glm::cross(up, forward)) > 1e-6f) {
            const auto z = glm::normalize(forward);
            const auto x = glm::normalize(glm::cross(up, z));
            const auto y = glm::cross(z, x);
            record.axes  = {{{x.x, x.y, x.z}, {y.x, y.y, y.z}, {z.x, z.y, z.z}}};
        }
        if (node.hasBounds) {
            std::array<double, 6> bounds{INFINITY, INFINITY, INFINITY, -INFINITY, -INFINITY, -INFINITY};
            for (int corner = 0; corner < 8; ++corner) {
                const auto point =
                    worlds[i] * glm::vec4(corner & 1 ? node.bmaxX : node.bminX, corner & 2 ? node.bmaxY : node.bminY,
                                          corner & 4 ? node.bmaxZ : node.bminZ, 1.f);
                for (std::size_t axis = 0; axis < 3; ++axis) {
                    const double value = point[int(axis)];
                    if (!std::isfinite(value))
                        return failure<Capture>(DiagnosticCode::InvalidArgument, "nonfinite bounds");
                    bounds[axis]     = std::min(bounds[axis], value);
                    bounds[axis + 3] = std::max(bounds[axis + 3], value);
                }
            }
            record.bounds = bounds;
        }
        for (double number : record.position)
            if (!std::isfinite(number))
                return failure<Capture>(DiagnosticCode::InvalidArgument, "nonfinite scene transform");
        record.kinds.push_back("scene-node");
        for (const auto& link : node.links) record.kinds.emplace_back(linkKindName(link.kind));
        std::sort(record.kinds.begin(), record.kinds.end());
        record.kinds.erase(std::unique(record.kinds.begin(), record.kinds.end()), record.kinds.end());
        std::sort(record.tags.begin(), record.tags.end());
        record.text = record.name;
        for (const auto& tag : record.tags) record.text += "\n" + tag;
        if (record.text.size() > maxTextBytes || record.id.size() > 256 || record.kinds.size() > 64 ||
            record.tags.size() > 64)
            return failure<Capture>(DiagnosticCode::InvalidArgument, "scene search metadata budget exceeded");
        source << std::quoted(record.id) << std::quoted(record.persistentId) << std::quoted(record.text)
               << std::quoted(record.name) << std::quoted(record.space) << ':' << node.parent;
        for (const auto& tag : record.tags) source << std::quoted(tag);
        geometry << std::quoted(record.id) << record.visible << ':' << record.layer;
        for (double number : record.position) geometry << ':' << number;
        for (const auto& axis : record.axes)
            for (double number : axis) geometry << ':' << number;
        geometry << ':' << record.bounds.has_value();
        if (record.bounds)
            for (double number : *record.bounds) geometry << ':' << number;
        for (const auto& kind : record.kinds) source << std::quoted(kind);
        source << ';';
        out.snapshot.records.push_back(std::move(record));
    }
    out.source   = source.str();
    out.geometry = geometry.str();
    return Result<Capture>::success(std::move(out));
}

Result<std::vector<double>> normalized(const std::vector<double>& vector) {
    if (vector.empty() || vector.size() > maxDimensions)
        return failure<std::vector<double>>(DiagnosticCode::InvalidArgument, "embedding dimension must be 1..1024");
    double norm = 0;
    for (double number : vector) {
        if (!std::isfinite(number) || std::abs(number) > 1e10)
            return failure<std::vector<double>>(DiagnosticCode::InvalidArgument, "invalid embedding scalar");
        norm += number * number;
    }
    if (norm < 1e-20) return failure<std::vector<double>>(DiagnosticCode::InvalidArgument, "zero embedding");
    auto out = vector;
    for (auto& number : out) number /= std::sqrt(norm);
    return Result<std::vector<double>>::success(std::move(out));
}

std::string queryKey(const SceneSearchQuery& query) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::hexfloat << std::quoted(query.snapshot) << std::quoted(query.model) << std::quoted(query.kind)
        << std::quoted(query.space) << ':' << query.limit << ':' << query.scanBudget;
    for (const auto& tag : query.tags) out << std::quoted(tag);
    out << ':' << query.visible.has_value();
    if (query.visible) out << ':' << *query.visible;
    out << ':' << query.layer.has_value();
    if (query.layer) out << ':' << *query.layer;
    out << ':' << query.center.has_value();
    if (query.center)
        for (double number : *query.center) out << ':' << number;
    out << ':' << query.radius;
    out << ':' << query.candidates;
    for (const auto& relation : query.spatial) {
        out << ':' << int(relation.relation) << ':' << int(relation.frame) << std::quoted(relation.reference)
            << std::quoted(relation.observer) << ':' << relation.distance << ':' << relation.margin;
    }
    for (double number : query.embedding) out << ':' << number;
    return out.str();
}

struct ResolvedSpatial {
    SceneSpatialConstraint               constraint;
    SceneSearchRecord                    reference;
    std::array<std::array<double, 3>, 3> axes{{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}};
};

Result<std::vector<ResolvedSpatial>> resolveSpatial(const std::vector<SceneSearchRecord>&      records,
                                                    const std::vector<SceneSpatialConstraint>& constraints) {
    if (constraints.size() > 16)
        return failure<std::vector<ResolvedSpatial>>(DiagnosticCode::InvalidArgument, "at most 16 spatial predicates");
    std::vector<ResolvedSpatial> out;
    for (const auto& constraint : constraints) {
        if (!std::isfinite(constraint.distance) || constraint.distance < 0 || !std::isfinite(constraint.margin) ||
            constraint.margin < 0 || int(constraint.relation) < 0 || int(constraint.relation) > 7 ||
            int(constraint.frame) < 0 || int(constraint.frame) > 2)
            return failure<std::vector<ResolvedSpatial>>(DiagnosticCode::InvalidArgument, "invalid spatial predicate");
        const auto reference = std::find_if(records.begin(), records.end(),
                                            [&](const auto& record) { return record.id == constraint.reference; });
        if (reference == records.end())
            return failure<std::vector<ResolvedSpatial>>(DiagnosticCode::NotFound, "spatial reference not found");
        ResolvedSpatial resolved;
        resolved.constraint = constraint;
        resolved.reference  = *reference;
        if (constraint.frame == SceneSpatialFrame::Reference) resolved.axes = reference->axes;
        if (constraint.frame == SceneSpatialFrame::Observer) {
            const auto observer = std::find_if(records.begin(), records.end(),
                                               [&](const auto& record) { return record.id == constraint.observer; });
            if (observer == records.end() || observer->space != reference->space)
                return failure<std::vector<ResolvedSpatial>>(DiagnosticCode::NotFound, "compatible observer not found");
            resolved.axes = observer->axes;
        } else if (!constraint.observer.empty()) {
            return failure<std::vector<ResolvedSpatial>>(DiagnosticCode::InvalidArgument,
                                                         "observer requires observer frame");
        }
        for (const auto& axis : resolved.axes) {
            double length = 0;
            for (double value : axis) length += value * value;
            if (!std::isfinite(length) || length < .5)
                return failure<std::vector<ResolvedSpatial>>(DiagnosticCode::InvalidArgument,
                                                             "degenerate reference frame");
        }
        if (constraint.relation == SceneSpatialRelation::Inside && !reference->bounds)
            return failure<std::vector<ResolvedSpatial>>(DiagnosticCode::Unsupported,
                                                         "inside requires authored reference bounds");
        out.push_back(std::move(resolved));
    }
    return Result<std::vector<ResolvedSpatial>>::success(std::move(out));
}

std::optional<SceneSpatialEvidence> spatialEvidence(const SceneSearchRecord& record, const ResolvedSpatial& filter) {
    if (record.space != filter.reference.space) return std::nullopt;
    SceneSpatialEvidence out;
    out.reference = filter.constraint.reference;
    out.observer  = filter.constraint.observer;
    out.relation  = filter.constraint.relation;
    out.frame     = filter.constraint.frame;
    for (std::size_t coordinate = 0; coordinate < 3; ++coordinate) {
        const double delta = record.position[coordinate] - filter.reference.position[coordinate];
        out.distance       = std::hypot(out.distance, delta);
        for (std::size_t axis = 0; axis < 3; ++axis) out.offset[axis] += delta * filter.axes[axis][coordinate];
    }
    // Scene's 2D coordinates have +Y down; 3D has +Y up. Direction evidence always reports +up.
    if (record.space == "2d") out.offset[1] = -out.offset[1];
    const double margin  = filter.constraint.margin;
    bool         matches = false;
    switch (filter.constraint.relation) {
        case SceneSpatialRelation::Left: matches = out.offset[0] < -margin; break;
        case SceneSpatialRelation::Right: matches = out.offset[0] > margin; break;
        case SceneSpatialRelation::Above: matches = out.offset[1] > margin; break;
        case SceneSpatialRelation::Below: matches = out.offset[1] < -margin; break;
        case SceneSpatialRelation::Front: matches = out.offset[2] > margin; break;
        case SceneSpatialRelation::Behind: matches = out.offset[2] < -margin; break;
        case SceneSpatialRelation::Near: matches = out.distance <= filter.constraint.distance; break;
        case SceneSpatialRelation::Inside: {
            const auto& bounds = *filter.reference.bounds;
            matches            = true;
            for (std::size_t axis = 0; axis < 3; ++axis)
                matches = matches && record.position[axis] >= bounds[axis] - margin &&
                          record.position[axis] <= bounds[axis + 3] + margin;
            break;
        }
    }
    return matches ? std::optional(out) : std::nullopt;
}

struct EligibleLabels final : hnswlib::BaseFilterFunctor {
    std::vector<bool> eligible;
    bool operator()(hnswlib::labeltype label) override { return label < eligible.size() && eligible[label]; }
};
}  // namespace

void SceneSearch::invalidate() {
    snapshot_ = {};
    source_.clear();
    model_.clear();
    vectors_.clear();
    index_.reset();
    cursor_.clear();
    queryKey_.clear();
    offset_ = 0;
}

Result<SceneSearchSnapshot> SceneSearch::prepare(std::string_view host) {
    auto result = capture(host);
    if (!result.ok()) return Result<SceneSearchSnapshot>::failure(result.status());
    auto candidate           = std::move(result).takeValue();
    candidate.snapshot.token = "scene-search:" + std::to_string(++sequence_);
    snapshot_                = std::move(candidate.snapshot);
    source_                  = std::move(candidate.source);
    vectors_.clear();
    index_.reset();
    model_.clear();
    cursor_.clear();
    return Result<SceneSearchSnapshot>::success(snapshot_);
}

Result<SceneSearchSnapshot> SceneSearch::status() {
    if (snapshot_.token.empty())
        return failure<SceneSearchSnapshot>(DiagnosticCode::NotFound, "no prepared scene index");
    auto current = capture(snapshot_.host);
    if (!current.ok()) return Result<SceneSearchSnapshot>::failure(current.status());
    if (current.value().source != source_)
        return failure<SceneSearchSnapshot>(DiagnosticCode::Conflict, "semantic source changed; rebuild index");
    auto out  = std::move(current).takeValue().snapshot;
    out.token = snapshot_.token;
    out.model = model_;
    return Result<SceneSearchSnapshot>::success(std::move(out));
}

Result<void> SceneSearch::publish(const SceneEmbeddingBatch& batch) {
    if (snapshot_.token.empty() || batch.snapshot != snapshot_.token)
        return failure<void>(DiagnosticCode::StaleHandle, "stale snapshot token");
    auto current = capture(snapshot_.host);
    if (!current.ok()) return Result<void>::failure(current.status());
    if (current.value().source != source_)
        return failure<void>(DiagnosticCode::Conflict, "scene source changed; prepare again");
    if (batch.model.empty() || batch.model.size() > 512 || batch.vectors.size() != snapshot_.records.size())
        return failure<void>(DiagnosticCode::InvalidArgument,
                             "model identity and exactly one vector per record required");
    std::vector<std::vector<double>> candidate;
    for (const auto& vector : batch.vectors) {
        if (!candidate.empty() && vector.size() != candidate.front().size())
            return failure<void>(DiagnosticCode::InvalidArgument, "mixed embedding dimensions");
        auto value = normalized(vector);
        if (!value.ok()) return Result<void>::failure(value.status());
        candidate.push_back(std::move(value).takeValue());
    }
    std::unique_ptr<SceneHnswIndex> index;
    try {
        if (!candidate.empty()) {
            index = std::make_unique<SceneHnswIndex>(candidate.front().size(), candidate.size());
            for (std::size_t i = 0; i < candidate.size(); ++i) {
                std::vector<float> values;
                for (double value : candidate[i]) values.push_back(static_cast<float>(value));
                index->graph.addPoint(values.data(), i);
            }
        }
    } catch (const std::exception& error) {
        return failure<void>(DiagnosticCode::Failed, std::string("HNSW build failed: ") + error.what());
    }
    vectors_ = std::move(candidate);
    index_   = std::move(index);
    model_   = batch.model;
    cursor_.clear();
    return Result<void>::success();
}

Result<SceneSearchResult> SceneSearch::query(const SceneSearchQuery& query) {
    if (snapshot_.token.empty() || query.snapshot != snapshot_.token)
        return failure<SceneSearchResult>(DiagnosticCode::StaleHandle, "stale snapshot token");
    if (query.tags.size() > 64 || query.model.size() > 512 || query.kind.size() > 256 || query.space.size() > 256 ||
        std::any_of(query.tags.begin(), query.tags.end(), [](const auto& tag) { return tag.size() > 256; }))
        return failure<SceneSearchResult>(DiagnosticCode::InvalidArgument, "query metadata budget exceeded");
    if (query.limit < 1 || query.limit > 256 || query.scanBudget < 1 || query.scanBudget > maxNodes ||
        !std::isfinite(query.radius) || query.radius < 0 || (!query.center && query.radius != 0))
        return failure<SceneSearchResult>(DiagnosticCode::InvalidArgument, "invalid query budget or spatial filter");
    if (query.center)
        for (double number : *query.center)
            if (!std::isfinite(number))
                return failure<SceneSearchResult>(DiagnosticCode::InvalidArgument, "nonfinite center");
    auto current = capture(snapshot_.host);
    if (!current.ok()) return Result<SceneSearchResult>::failure(current.status());
    if (current.value().source != source_)
        return failure<SceneSearchResult>(DiagnosticCode::Conflict, "scene source changed; prepare again");
    if (snapshot_.records.size() > query.scanBudget)
        return failure<SceneSearchResult>(DiagnosticCode::InvalidArgument,
                                          "scan budget exceeded; no partial results returned");
    std::vector<double> embedding;
    if (!query.embedding.empty()) {
        if (model_.empty()) return failure<SceneSearchResult>(DiagnosticCode::Unsupported, "vector-index-unavailable");
        if (query.model != model_)
            return failure<SceneSearchResult>(DiagnosticCode::Conflict, "embedding model mismatch");
        if (!vectors_.empty() && query.embedding.size() != vectors_.front().size())
            return failure<SceneSearchResult>(DiagnosticCode::InvalidArgument, "query embedding dimension mismatch");
        auto vector = normalized(query.embedding);
        if (!vector.ok()) return Result<SceneSearchResult>::failure(vector.status());
        embedding = std::move(vector).takeValue();
    }
    if (query.candidates < 1 || query.candidates > maxNodes)
        return failure<SceneSearchResult>(DiagnosticCode::InvalidArgument, "candidate budget must be 1..4096");
    auto spatial = resolveSpatial(current.value().snapshot.records, query.spatial);
    if (!spatial.ok()) return Result<SceneSearchResult>::failure(spatial.status());
    const auto  key   = queryKey(query) + current.value().geometry;
    std::size_t start = 0;
    if (!query.pageToken.empty()) {
        if (query.pageToken != cursor_ || key != queryKey_)
            return failure<SceneSearchResult>(DiagnosticCode::StaleHandle, "stale or cross-query page token");
        start = offset_;
    }
    SceneSearchResult out;
    out.snapshot      = snapshot_.token;
    out.indexSnapshot = model_.empty() ? "" : snapshot_.token;
    out.model         = model_;
    out.scanned       = snapshot_.records.size();
    EligibleLabels labels;
    labels.eligible.resize(snapshot_.records.size());
    for (std::size_t i = 0; i < snapshot_.records.size(); ++i) {
        const auto& record = current.value().snapshot.records[i];
        if ((!query.kind.empty() &&
             std::find(record.kinds.begin(), record.kinds.end(), query.kind) == record.kinds.end()) ||
            (!query.space.empty() && query.space != record.space) ||
            (query.visible && record.visible != *query.visible) || (query.layer && record.layer != *query.layer))
            continue;
        if (!std::all_of(query.tags.begin(), query.tags.end(), [&](const auto& tag) {
                return std::find(record.tags.begin(), record.tags.end(), tag) != record.tags.end();
            }))
            continue;
        if (query.center) {
            double distance = 0;
            for (std::size_t axis = 0; axis < 3; ++axis)
                distance = std::hypot(distance, record.position[axis] - (*query.center)[axis]);
            if (distance > query.radius) continue;
        }
        std::vector<SceneSpatialEvidence> evidence;
        for (const auto& filter : spatial.value()) {
            auto measured = spatialEvidence(record, filter);
            if (!measured) break;
            evidence.push_back(std::move(*measured));
        }
        if (evidence.size() != spatial.value().size()) continue;
        labels.eligible[i] = true;
        out.hits.push_back({record, 0, std::move(evidence)});
    }
    out.matched = out.hits.size();
    if (!embedding.empty() && !out.hits.empty()) {
        const auto         window = std::min(out.hits.size(), std::max(query.limit, query.candidates));
        std::vector<float> values;
        for (double value : embedding) values.push_back(static_cast<float>(value));
        try {
            index_->graph.setEf(std::max(std::size_t{128}, window));
            auto                        nearest = index_->graph.searchKnn(values.data(), window, &labels);
            std::vector<SceneSearchHit> hits;
            while (!nearest.empty()) {
                const auto [distance, label] = nearest.top();
                nearest.pop();
                const auto found = std::find_if(out.hits.begin(), out.hits.end(), [&](const auto& hit) {
                    return hit.record.id == current.value().snapshot.records[label].id;
                });
                if (found != out.hits.end()) {
                    auto hit  = *found;
                    hit.score = std::clamp(1.0 - double(distance), -1.0, 1.0);
                    hits.push_back(std::move(hit));
                }
            }
            out.hits = std::move(hits);
        } catch (const std::exception& error) {
            return failure<SceneSearchResult>(DiagnosticCode::Failed,
                                              std::string("HNSW query failed: ") + error.what());
        }
        out.approximate        = true;
        out.candidateTruncated = out.hits.size() < out.matched;
    }
    std::sort(out.hits.begin(), out.hits.end(), [](const auto& a, const auto& b) {
        return a.score != b.score ? a.score > b.score : a.record.id < b.record.id;
    });
    const auto windowSize = out.hits.size();
    const auto end        = std::min(windowSize, start + query.limit);
    if (start > windowSize) return failure<SceneSearchResult>(DiagnosticCode::StaleHandle, "invalid page offset");
    if (end < windowSize) {
        cursor_       = "scene-page:" + std::to_string(++sequence_);
        queryKey_     = key;
        offset_       = end;
        out.pageToken = cursor_;
    } else
        cursor_.clear();
    out.hits =
        std::vector<SceneSearchHit>(out.hits.begin() + std::ptrdiff_t(start), out.hits.begin() + std::ptrdiff_t(end));
    return Result<SceneSearchResult>::success(std::move(out));
}

namespace {
SceneSearch& processSearch() {
    static SceneSearch provider;
    return provider;
}
}  // namespace

void registerSceneSearch() {
    auto& provider = processSearch();
    provider.invalidate();
    cap::provide<ISceneSearch>(&provider);
}

void invalidateSceneSearch() { processSearch().invalidate(); }
}  // namespace eve::scene
