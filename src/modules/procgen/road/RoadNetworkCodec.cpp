#include "procgen/road/RoadNetworkCodec.h"
#include <cmath>
#include <limits>
#include <utility>
namespace eve::procgen::road {
namespace {
using RoadCodecValue = eve::Value;
#include "procgen/road/RoadValueReaders.inc"
bool           readSmallInt(const RoadCodecValue&, const char*, int, int, int&);
RoadCodecValue encodeStyle(std::uint32_t, const RoadStyle&);
bool           decodeStyle(const RoadCodecValue&, std::uint32_t&, RoadStyle&);
#include "procgen/road/RoadConnectionCodec.inc"
#include "procgen/road/RoadPointCodec.inc"
#include "procgen/road/RoadRecordCodec.inc"
#include "procgen/road/RoadStyleCodec.inc"
template <class T>
Result<T> invalid(const char* message) {
    return Result<T>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, message, "networkSnapshot"));
}
bool withinBudget(const eve::Value& value, std::size_t depth, std::size_t& elements, std::size_t& bytes) {
    if (depth > 64 || ++elements > 262144) return false;
    if (const auto* text = value.getIf<std::string>()) bytes += text->size();
    if (const auto* array = value.getIf<eve::Value::Array>()) {
        if (array->size() > 262144 - elements) return false;
        for (const auto& item : *array)
            if (!withinBudget(item, depth + 1, elements, bytes)) return false;
    }
    if (const auto* object = value.getIf<eve::Value::Object>()) {
        if (object->size() > 262144 - elements) return false;
        for (const auto& [key, item] : *object) {
            bytes += key.size();
            if (bytes > 8u * 1024u * 1024u || !withinBudget(item, depth + 1, elements, bytes)) return false;
        }
    }
    return bytes <= 8u * 1024u * 1024u;
}
}  // namespace
Result<eve::Value> captureRoadNetwork(const RoadNetwork& network) {
    auto valid = network.validate();
    if (!valid.ok()) return Result<eve::Value>::failure(valid.status());
    if (network.nodes().size() > 65536 || network.edges().size() > 65536 || network.laneLinks().size() > 262144 ||
        network.blockedLaneLinks().size() > 262144)
        return invalid<eve::Value>("Road snapshot exceeds the graph budget");
    for (const auto& edge : network.edges())
        if (edge.controlPoints.size() > 4096) return invalid<eve::Value>("Road snapshot exceeds the marker budget");
    RoadCodecValue::Array nodes;
    nodes.reserve(network.nodes().size());
    for (const auto& node : network.nodes()) nodes.push_back(encodeNodeRecord(node));

    RoadCodecValue::Array edges;
    edges.reserve(network.edges().size());
    for (const auto& edge : network.edges()) edges.push_back(encodeEdgeRecord(edge, {}, {}));

    RoadCodecValue::Array laneLinks;
    laneLinks.reserve(network.laneLinks().size());
    for (const auto& link : network.laneLinks()) laneLinks.push_back(encodeLink(link));
    RoadCodecValue::Array blockedLaneLinks;
    blockedLaneLinks.reserve(network.blockedLaneLinks().size());
    for (const auto& link : network.blockedLaneLinks()) blockedLaneLinks.push_back(encodeLink(link));

    eve::Value snapshot = RoadCodecValue::Object{
        {"schema", "eve.procgen.roadNetwork"}, {"schemaVersion", std::int64_t{1}},
        {"nodes", std::move(nodes)},           {"edges", std::move(edges)},
        {"laneLinks", std::move(laneLinks)},   {"blockedLaneLinks", std::move(blockedLaneLinks)}};
    std::size_t elements = 0, bytes = 0;
    if (!withinBudget(snapshot, 0, elements, bytes))
        return invalid<eve::Value>("Road snapshot exceeds the value budget");
    return Result<eve::Value>::success(std::move(snapshot));
}
Result<RoadNetwork> restoreRoadNetwork(const eve::Value& snapshot) {
    std::size_t elements = 0, bytes = 0;
    if (!withinBudget(snapshot, 0, elements, bytes))
        return invalid<RoadNetwork>("Road snapshot exceeds the value budget");
    const auto* schemaValue  = field(snapshot, "schema");
    const auto* versionValue = field(snapshot, "schemaVersion");
    const auto* nodesValue   = field(snapshot, "nodes");
    const auto* edgesValue   = field(snapshot, "edges");
    const auto* linksValue   = field(snapshot, "laneLinks");
    const auto* blockedValue = field(snapshot, "blockedLaneLinks");
    const auto* schema       = schemaValue ? schemaValue->getIf<std::string>() : nullptr;
    const auto* version      = versionValue ? versionValue->getIf<std::int64_t>() : nullptr;
    const auto* nodes        = nodesValue ? nodesValue->getIf<RoadCodecValue::Array>() : nullptr;
    const auto* edges        = edgesValue ? edgesValue->getIf<RoadCodecValue::Array>() : nullptr;
    const auto* links        = linksValue ? linksValue->getIf<RoadCodecValue::Array>() : nullptr;
    const auto* blocked      = blockedValue ? blockedValue->getIf<RoadCodecValue::Array>() : nullptr;
    if (!schema || *schema != "eve.procgen.roadNetwork" || !version || *version != 1 || !nodes || !edges || !links ||
        (blockedValue && !blocked))
        return invalid<RoadNetwork>("Road snapshot schema is unsupported");
    constexpr std::size_t maximumNodes = 65536u, maximumEdges = 65536u, maximumLinks = 262144u;
    if (nodes->size() > maximumNodes || edges->size() > maximumEdges || links->size() > maximumLinks ||
        (blocked && blocked->size() > maximumLinks))
        return invalid<RoadNetwork>("Road snapshot exceeds the graph budget");

    procgen::road::RoadNetwork candidate;
    for (const auto& encoded : *nodes) {
        procgen::road::RoadNode node;
        if (!decodeNodeRecord(encoded, node)) return invalid<RoadNetwork>("Road snapshot contains an invalid node");
        auto restored = candidate.restoreNode(node);
        if (!restored.ok()) return Result<RoadNetwork>::failure(restored.status());
    }
    for (const auto& encoded : *edges) {
        procgen::road::RoadEdge                        edge;
        std::vector<procgen::road::RoadLaneConnection> embeddedLinks;
        if (!decodeEdgeRecord(encoded, edge, embeddedLinks, nullptr) || !embeddedLinks.empty())
            return invalid<RoadNetwork>("Road snapshot contains an invalid edge");
        auto restored = candidate.restoreEdge(std::move(edge));
        if (!restored.ok()) return Result<RoadNetwork>::failure(restored.status());
    }
    for (const auto& encoded : *links) {
        procgen::road::RoadLaneConnection link;
        if (!decodeLink(encoded, link)) return invalid<RoadNetwork>("Road snapshot contains an invalid lane link");
        auto restored = candidate.addLaneLink(link);
        if (!restored.ok()) return Result<RoadNetwork>::failure(restored.status());
    }
    if (blocked) {
        for (const auto& encoded : *blocked) {
            procgen::road::RoadLaneConnection link;
            if (!decodeLink(encoded, link))
                return invalid<RoadNetwork>("Road snapshot contains an invalid blocked lane link");
            auto restored = candidate.blockLaneLink(link);
            if (!restored.ok()) return Result<RoadNetwork>::failure(restored.status());
            if (restored.value())
                return invalid<RoadNetwork>("Road snapshot marks the same lane link active and blocked");
        }
    }
    auto valid = candidate.validate();
    if (!valid.ok()) return Result<RoadNetwork>::failure(valid.status());
    return Result<RoadNetwork>::success(std::move(candidate));
}
}  // namespace eve::procgen::road
