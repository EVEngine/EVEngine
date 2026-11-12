#include "common/Capability.h"
#include "common/SceneSearch.h"
#include "devtools/McpJson.hpp"
#include "devtools/McpToolFamilies.hpp"

#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>

namespace eve::dev {
namespace {
using Object = Poco::JSON::Object;
using Array  = Poco::JSON::Array;

std::string string(Object::Ptr args, const char* key, bool required = false) {
    if (!args->has(key) && !required) return {};
    if (!args->has(key) || !args->get(key).isString())
        throw std::invalid_argument(std::string(key) + " must be a string");
    auto value = args->getValue<std::string>(key);
    if (value.size() > 512 || (required && value.empty()))
        throw std::invalid_argument(std::string(key) + " invalid length");
    return value;
}

double number(const Poco::Dynamic::Var& value) {
    if (!value.isNumeric() || value.isBoolean()) throw std::invalid_argument("expected numeric scalar");
    double out = value.convert<double>();
    if (!std::isfinite(out)) throw std::invalid_argument("nonfinite scalar");
    return out;
}

int integer(Object::Ptr args, const char* key, int fallback, int low, int high) {
    if (!args->has(key)) return fallback;
    if (!args->get(key).isInteger()) throw std::invalid_argument(std::string(key) + " must be an integer");
    const double value = number(args->get(key));
    if (value < low || value > high) throw std::invalid_argument(std::string(key) + " outside budget");
    return int(value);
}

Array::Ptr array(Object::Ptr args, const char* key, std::size_t maximum) {
    auto out = args->getArray(key);
    if (!out || out->size() > maximum) throw std::invalid_argument(std::string(key) + " invalid array or budget");
    return out;
}

std::vector<double> vector(Array::Ptr values) {
    if (!values || values->size() < 1 || values->size() > 1024) throw std::invalid_argument("invalid vector dimension");
    std::vector<double> out;
    for (const auto& value : *values) out.push_back(number(value));
    return out;
}

Object::Ptr envelope() {
    Object::Ptr out = new Object;
    out->set("schema", "eve.scene-search");
    out->set("schemaVersion", 1);
    out->set("ok", true);
    return out;
}

Object::Ptr record(const SceneSearchRecord& input) {
    Object::Ptr out = new Object;
    out->set("id", input.id);
    out->set("persistentId", input.persistentId);
    out->set("name", input.name);
    out->set("space", input.space);
    out->set("text", input.text);
    out->set("visible", input.visible);
    out->set("layer", input.layer);
    Array::Ptr tags = new Array, kinds = new Array, position = new Array;
    for (const auto& tag : input.tags) tags->add(tag);
    for (const auto& kind : input.kinds) kinds->add(kind);
    for (double value : input.position) position->add(value);
    out->set("tags", tags);
    out->set("kinds", kinds);
    out->set("position", position);
    Array::Ptr axes = new Array;
    for (const auto& axis : input.axes) {
        Array::Ptr row = new Array;
        for (double value : axis) row->add(value);
        axes->add(row);
    }
    out->set("axes", axes);
    if (input.bounds) {
        Array::Ptr bounds = new Array;
        for (double value : *input.bounds) bounds->add(value);
        out->set("bounds", bounds);
    }
    return out;
}

std::string failed(std::string code, std::string message) {
    auto out = envelope();
    out->set("ok", false);
    out->set("error", std::move(code));
    out->set("message", std::move(message));
    return textContentResult(mcpStringify(out), true);
}
std::string failed(const Status& status) {
    return failed(std::string(statusCodeName(status.code())), status.describe());
}
}  // namespace

std::string callSceneSearch(std::string_view name, Object::Ptr args) try {
    if (!args) throw std::invalid_argument("arguments required");
    std::set<std::string> allowed;
    if (name == "eve_scene_search_prepare")
        allowed = {"host"};
    else if (name == "eve_scene_search_status")
        allowed = {};
    else if (name == "eve_scene_search_publish")
        allowed = {"snapshot", "model", "vectors"};
    else if (name == "eve_scene_search")
        allowed = {"snapshot", "model",  "embedding", "kind",       "space",     "tags",    "visible",   "layer",
                   "center",   "radius", "limit",     "scanBudget", "pageToken", "spatial", "candidates"};
    else
        return failed("unknown_tool", "Unknown scene search tool");
    for (const auto& entry : *args)
        if (!allowed.contains(entry.first)) throw std::invalid_argument("unknown field: " + entry.first);
    auto* provider = cap::query<ISceneSearch>();
    if (!provider) return failed("scene-unavailable", "Scene module is absent or has not been initialized");
    auto out = envelope();
    if (name == "eve_scene_search_prepare" || name == "eve_scene_search_status") {
        auto value =
            name == "eve_scene_search_prepare" ? provider->prepare(string(args, "host", true)) : provider->status();
        if (!value.ok()) return failed(value.status());
        const auto& snapshot = value.value();
        out->set("snapshot", snapshot.token);
        out->set("host", snapshot.host);
        out->set("model", snapshot.model);
        out->set("backend", snapshot.model.empty() ? "none" : "hnswlib-0.8.0");
        out->set("spatialContract",
                 "Node origins; 3D world +X right, +Y up, +Z forward; 2D +Y down. Node frames use local +Z forward, "
                 "not Camera3D view -Z. Use frame=observer with an explicit observer node to resolve perspective. "
                 "inside means origin in authored world AABB, not mesh containment. Resolve ambiguous anchor names "
                 "with semantic search first; spatial.reference and observer are exact node IDs.");
        Array::Ptr records = new Array;
        for (const auto& item : snapshot.records) records->add(record(item));
        out->set("records", records);
    } else if (name == "eve_scene_search_publish") {
        SceneEmbeddingBatch batch;
        batch.snapshot    = string(args, "snapshot", true);
        batch.model       = string(args, "model", true);
        const auto values = array(args, "vectors", 4096);
        // Bound aggregate payload work before allocating normalized copies.
        std::size_t scalars = 0;
        for (std::size_t i = 0; i < values->size(); ++i) {
            auto row = values->getArray(unsigned(i));
            if (!row || (scalars += row->size()) > 4194304)
                throw std::invalid_argument("embedding scalar budget exceeded");
            batch.vectors.push_back(vector(row));
        }
        auto result = provider->publish(batch);
        if (!result.ok()) return failed(result.status());
        out->set("snapshot", batch.snapshot);
        out->set("model", batch.model);
        out->set("backend", "hnswlib-0.8.0");
    } else {
        SceneSearchQuery query;
        query.snapshot   = string(args, "snapshot", true);
        query.model      = string(args, "model");
        query.kind       = string(args, "kind");
        query.space      = string(args, "space");
        query.pageToken  = string(args, "pageToken");
        query.limit      = std::size_t(integer(args, "limit", 20, 1, 256));
        query.scanBudget = std::size_t(integer(args, "scanBudget", 4096, 1, 4096));
        query.candidates = std::size_t(integer(args, "candidates", 128, 1, 4096));
        if (args->has("spatial")) {
            auto spatial = array(args, "spatial", 16);
            for (std::size_t i = 0; i < spatial->size(); ++i) {
                auto item = spatial->getObject(unsigned(i));
                if (!item) throw std::invalid_argument("spatial predicates must be objects");
                for (const auto& entry : *item)
                    if (!std::set<std::string>{"relation", "reference", "frame", "observer", "distance", "margin"}
                             .contains(entry.first))
                        throw std::invalid_argument("unknown spatial field: " + entry.first);
                SceneSpatialConstraint constraint;
                const auto             relation = string(item, "relation", true);
                const auto             frame    = item->has("frame") ? string(item, "frame", true) : "world";
                const auto             relationAt =
                    std::find(sceneSpatialRelationNames.begin(), sceneSpatialRelationNames.end(), relation);
                const auto frameAt = std::find(sceneSpatialFrameNames.begin(), sceneSpatialFrameNames.end(), frame);
                if (relationAt == sceneSpatialRelationNames.end() || frameAt == sceneSpatialFrameNames.end())
                    throw std::invalid_argument("unknown spatial relation/frame");
                constraint.relation  = SceneSpatialRelation(relationAt - sceneSpatialRelationNames.begin());
                constraint.frame     = SceneSpatialFrame(frameAt - sceneSpatialFrameNames.begin());
                constraint.reference = string(item, "reference", true);
                constraint.observer  = string(item, "observer");
                if (item->has("distance")) constraint.distance = number(item->get("distance"));
                if (item->has("margin")) constraint.margin = number(item->get("margin"));
                query.spatial.push_back(std::move(constraint));
            }
        }
        if (args->has("visible")) {
            if (!args->get("visible").isBoolean()) throw std::invalid_argument("visible must be boolean");
            query.visible = args->getValue<bool>("visible");
        }
        if (args->has("layer")) query.layer = integer(args, "layer", 0, -2147483647, 2147483647);
        if (args->has("embedding")) query.embedding = vector(array(args, "embedding", 1024));
        if (args->has("center") != args->has("radius"))
            throw std::invalid_argument("center and radius must be supplied together");
        if (args->has("center")) {
            const auto center = vector(array(args, "center", 3));
            if (center.size() != 3 || !args->has("radius"))
                throw std::invalid_argument("center requires three axes and radius");
            query.center = std::array<double, 3>{center[0], center[1], center[2]};
        }
        if (args->has("radius")) query.radius = number(args->get("radius"));
        if (args->has("tags")) {
            auto tags = array(args, "tags", 64);
            for (const auto& tag : *tags) {
                if (!tag.isString() || tag.convert<std::string>().size() > 256)
                    throw std::invalid_argument("invalid tag");
                query.tags.push_back(tag.convert<std::string>());
            }
        }
        auto value = provider->query(query);
        if (!value.ok()) return failed(value.status());
        const auto& result = value.value();
        out->set("snapshot", result.snapshot);
        out->set("indexSnapshot", result.indexSnapshot);
        out->set("model", result.model);
        out->set("scanned", Poco::UInt64(result.scanned));
        out->set("matched", Poco::UInt64(result.matched));
        out->set("pageToken", result.pageToken);
        out->set("truncated", !result.pageToken.empty());
        out->set("consistency", "source-validated");
        out->set("backend", query.embedding.empty() ? "structured" : "hnswlib-0.8.0");
        out->set("approximate", result.approximate);
        out->set("candidateTruncated", result.candidateTruncated);
        Array::Ptr hits = new Array;
        for (const auto& hit : result.hits) {
            auto item = record(hit.record);
            item->set("score", hit.score);
            item->set("evidence", query.embedding.empty() ? "exact-filters" : "hnsw-cosine+exact-filters");
            Array::Ptr spatial = new Array;
            for (const auto& measured : hit.spatial) {
                Object::Ptr evidence = new Object;
                evidence->set("relation", std::string(sceneSpatialRelationNames[std::size_t(measured.relation)]));
                evidence->set("frame", std::string(sceneSpatialFrameNames[std::size_t(measured.frame)]));
                evidence->set("reference", measured.reference);
                evidence->set("observer", measured.observer);
                evidence->set("distance", measured.distance);
                Array::Ptr offset = new Array;
                for (double value : measured.offset) offset->add(value);
                evidence->set("offsetRightUpForward", offset);
                spatial->add(evidence);
            }
            item->set("spatialEvidence", spatial);
            hits->add(item);
        }
        out->set("hits", hits);
    }
    return textContentResult(mcpStringify(out));
} catch (const std::exception& error) {
    return failed("invalid_argument", error.what());
}

std::string_view sceneSearchSchemas() {
    return R"JSON(
{"name":"eve_scene_search_prepare","description":"Prepare a real scene semantic build snapshot (max 4096 nodes). Returns canonical model text, world pose/axes/bounds and snapshot token. Replaces the previous session. Encode all records outside the frame loop, then publish once to construct the hnswlib index. Scene entities remain unchanged.","inputSchema":{"type":"object","additionalProperties":false,"required":["host"],"properties":{"host":{"type":"string","minLength":1,"maxLength":256}}}},
{"name":"eve_scene_search_status","description":"Inspect the current semantic/HNSW build and live geometry without rebuilding. Returns snapshot/model/backend and coordinate contract; semantic source changes return an explicit conflict.","inputSchema":{"type":"object","additionalProperties":false,"properties":{}}},
{"name":"eve_scene_search_publish","description":"Scene build stage: validate all embeddings, construct an hnswlib HNSW index off to the side and publish atomically. One vector per prepared object in record order. Rejects changed semantic source, invalid vectors and mixed dimensions; prior index survives failure. Does not infer or modify scene facts.","inputSchema":{"type":"object","additionalProperties":false,"required":["snapshot","model","vectors"],"properties":{"snapshot":{"type":"string"},"model":{"type":"string","maxLength":512},"vectors":{"type":"array","maxItems":4096,"items":{"type":"array","minItems":1,"maxItems":1024,"items":{"type":"number"}}}}}},
{"name":"eve_scene_search","description":"Read-only SceneHost search. With embedding, uses a published hnswlib index with exact eligible-label filtering. Resolve semantic anchor names first, then pass exact IDs in spatial predicates. 3D world axes: +X right, +Y up, +Z forward. Node/observer axes use local +Z forward, NOT Camera3D view -Z. Directional filters compare origins with margin; near uses Euclidean distance; inside means candidate origin in authored reference world AABB. Geometry is read live; changed semantic source requires rebuild. Results include measured spatial evidence. HNSW is approximate and candidates bounds the result window; candidateTruncated is separate from pagination. Without embedding, no model is needed.","inputSchema":{"type":"object","additionalProperties":false,"required":["snapshot"],"properties":{"snapshot":{"type":"string"},"model":{"type":"string"},"embedding":{"type":"array","minItems":1,"maxItems":1024,"items":{"type":"number"}},"kind":{"type":"string"},"space":{"type":"string"},"tags":{"type":"array","maxItems":64,"items":{"type":"string"}},"visible":{"type":"boolean"},"layer":{"type":"integer"},"center":{"type":"array","minItems":3,"maxItems":3,"items":{"type":"number"}},"radius":{"type":"number","minimum":0},"limit":{"type":"integer","minimum":1,"maximum":256},"scanBudget":{"type":"integer","minimum":1,"maximum":4096},"pageToken":{"type":"string"},"candidates":{"type":"integer","minimum":1,"maximum":4096,"default":128},"spatial":{"type":"array","maxItems":16,"items":{"type":"object","additionalProperties":false,"required":["relation","reference"],"properties":{"relation":{"type":"string","enum":["left-of","right-of","above","below","in-front-of","behind","near","inside"]},"reference":{"type":"string","description":"Exact host-local reference node ID, not a guessed name"},"frame":{"type":"string","enum":["world","reference","observer"],"default":"world"},"observer":{"type":"string","description":"Required only for observer frame; exact node ID whose local +Z is forward"},"distance":{"type":"number","minimum":0,"default":5,"description":"World-unit radius for near"},"margin":{"type":"number","minimum":0,"default":0.01,"description":"World-unit dead zone for directional predicates; AABB tolerance for inside"}}}}}}}
)JSON";
}
EVE_MCP_TOOL_FAMILY(SceneSearch, sceneSearchSchemas, callSceneSearch, ContentEnvelope)
}  // namespace eve::dev
