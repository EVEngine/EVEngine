#include "procgen/editing/RoadNetworkEditTarget.h"
#include "procgen/editing/RoadNetworkEditTargetInternal.inc"

#include "editing/EditingResult.h"
#include "procgen/spline/SplinePath.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace eve::procgen_editing {
namespace detail {
using RoadCodecValue = EditorValue;
#include "procgen/road/RoadValueReaders.inc"


bool readSmallInt(const EditorValue& value, const char* name, int minimum, int maximum, int& output);

Result<RoadIntersection> findUniqueInteriorIntersection(const procgen::road::RoadEdge& first,
                                                        const procgen::road::RoadEdge& second,
                                                        float maximumHeightDelta) {
    struct ParameterSample {
        float parameter = 0.f;
        procgen::road::RoadControlPoint point;
    };
    auto sampleEdge = [](const procgen::road::RoadEdge& edge) -> Result<std::vector<ParameterSample>> {
        procgen::SplinePath path;
        auto kind = path.setKindResult("catmullRom");
        if (!kind.ok()) return Result<std::vector<ParameterSample>>::failure(kind.status());
        path.setClosed(false);
        for (const auto& point : edge.controlPoints) {
            procgen::SplinePoint splinePoint;
            splinePoint.alignStraightStart = point.alignStraightStart;
            splinePoint.alignStraightEnd   = point.alignStraightEnd;
            splinePoint.circularFromEnd    = point.circularFromEnd;
            splinePoint.spanControl        = static_cast<eve::procgen::SplineSpanControl>(point.pathControl);
            splinePoint.spanStrength       = point.splineStrength;
            splinePoint.x                  = point.x;
            splinePoint.y = point.y;
            splinePoint.z = point.z;
            auto added = path.addPointResult(splinePoint);
            if (!added.ok()) return Result<std::vector<ParameterSample>>::failure(added.status());
        }
        const int intervals = std::min(4096, std::max(32, path.segmentCount() * 24));
        std::vector<ParameterSample> samples;
        samples.reserve(static_cast<std::size_t>(intervals + 1));
        for (int i = 0; i <= intervals; ++i) {
            const float parameter = static_cast<float>(i) / static_cast<float>(intervals);
            auto evaluated = path.evaluateResult(parameter);
            if (!evaluated.ok()) return Result<std::vector<ParameterSample>>::failure(evaluated.status());
            samples.push_back({parameter, {evaluated.value().x, evaluated.value().y, evaluated.value().z}});
        }
        return Result<std::vector<ParameterSample>>::success(std::move(samples));
    };
    auto firstSamples = sampleEdge(first);
    if (!firstSamples.ok()) return Result<RoadIntersection>::failure(firstSamples.status());
    auto secondSamples = sampleEdge(second);
    if (!secondSamples.ok()) return Result<RoadIntersection>::failure(secondSamples.status());

    std::vector<RoadIntersection> crossings;
    constexpr float epsilon = 1e-5f;
    const auto cross2 = [](float ax, float az, float bx, float bz) { return ax * bz - az * bx; };
    for (std::size_t i = 0; i + 1 < firstSamples.value().size(); ++i) {
        const auto& a = firstSamples.value()[i].point;
        const auto& b = firstSamples.value()[i + 1].point;
        const float rx = b.x - a.x, rz = b.z - a.z;
        for (std::size_t j = 0; j + 1 < secondSamples.value().size(); ++j) {
            const auto& c = secondSamples.value()[j].point;
            const auto& d = secondSamples.value()[j + 1].point;
            const float sx = d.x - c.x, sz = d.z - c.z;
            const float denominator = cross2(rx, rz, sx, sz);
            if (std::fabs(denominator) <= epsilon) continue;
            const float qx = c.x - a.x, qz = c.z - a.z;
            const float t = cross2(qx, qz, sx, sz) / denominator;
            const float u = cross2(qx, qz, rx, rz) / denominator;
            if (t < -epsilon || t > 1.f + epsilon || u < -epsilon || u > 1.f + epsilon) continue;
            const float clampedT = std::clamp(t, 0.f, 1.f), clampedU = std::clamp(u, 0.f, 1.f);
            const float x = a.x + rx * clampedT, z = a.z + rz * clampedT;
            const float firstParameter = firstSamples.value()[i].parameter +
                (firstSamples.value()[i + 1].parameter - firstSamples.value()[i].parameter) * clampedT;
            const float secondParameter = secondSamples.value()[j].parameter +
                (secondSamples.value()[j + 1].parameter - secondSamples.value()[j].parameter) * clampedU;
            if (firstParameter <= epsilon || firstParameter >= 1.f - epsilon || secondParameter <= epsilon ||
                secondParameter >= 1.f - epsilon)
                continue;
            const float firstY = a.y + (b.y - a.y) * clampedT;
            const float secondY = c.y + (d.y - c.y) * clampedU;
            if (std::fabs(firstY - secondY) > maximumHeightDelta) continue;
            const bool duplicate = std::any_of(crossings.begin(), crossings.end(), [&](const auto& crossing) {
                return std::fabs(crossing.mergedPoint.x - x) <= 1e-3f &&
                       std::fabs(crossing.mergedPoint.z - z) <= 1e-3f;
            });
            if (!duplicate) crossings.push_back({1, firstParameter, secondParameter, {x, firstY, z},
                                                  {x, secondY, z}, {x, (firstY + secondY) * 0.5f, z}});
        }
    }
    if (crossings.size() == 1) return Result<RoadIntersection>::success(crossings.front());
    RoadIntersection result;
    result.count = crossings.empty() ? 0 : 2;
    return Result<RoadIntersection>::success(result);
}

EditorValue encodeNode(std::uint32_t id, float x, float y, float z) {
    return EditorValue::Object{{"id", std::int64_t{id}}, {"x", x}, {"y", y}, {"z", z}};
}

EditorValue encodeNodeRadius(std::uint32_t id, float junctionRadius) {
    return EditorValue::Object{{"id", std::int64_t{id}}, {"junctionRadius", junctionRadius}};
}

bool decodeNodeRadius(const EditorValue& value, std::uint32_t& id, float& junctionRadius) {
    return readId(value, "id", id) && readNumber(value, "junctionRadius", junctionRadius);
}

EditorValue encodeNodeControl(std::uint32_t id, procgen::road::RoadJunctionControl control) {
    return EditorValue::Object{{"id", std::int64_t{id}},
                               {"junctionControl", std::int64_t{static_cast<int>(control)}}};
}

bool decodeNodeControl(const EditorValue& value, std::uint32_t& id,
                       procgen::road::RoadJunctionControl& control) {
    int encoded = 0;
    if (!readId(value, "id", id) || !readSmallInt(value, "junctionControl", 0, 3, encoded)) return false;
    control = static_cast<procgen::road::RoadJunctionControl>(encoded);
    return true;
}


EditorValue encodeBlockedLink(const procgen::road::RoadLaneConnection& link, bool restoreActive) {
    auto object = *encodeLink(link).getIf<EditorValue::Object>();
    object["restoreActive"] = restoreActive;
    return object;
}

bool decodeBlockedLink(const EditorValue& value, procgen::road::RoadLaneConnection& link, bool& restoreActive) {
    const auto* encoded = field(value, "restoreActive");
    const auto* flag = encoded ? encoded->getIf<bool>() : nullptr;
    return flag && decodeLink(value, link) && (restoreActive = *flag, true);
}

bool sameLink(const procgen::road::RoadLaneConnection& left, const procgen::road::RoadLaneConnection& right) {
    return left.inEdge == right.inEdge && left.inLane == right.inLane && left.outEdge == right.outEdge &&
           left.outLane == right.outLane && left.inDirection == right.inDirection &&
           left.outDirection == right.outDirection;
}

EditorValue encodeLinks(const std::vector<procgen::road::RoadLaneConnection>& links) {
    EditorValue::Array encoded;
    encoded.reserve(links.size());
    for (const auto& link : links) encoded.push_back(encodeLink(link));
    return EditorValue::Object{{"links", std::move(encoded)}};
}

bool decodeLinks(const EditorValue& value, std::vector<procgen::road::RoadLaneConnection>& links) {
    const auto* item  = field(value, "links");
    const auto* array = item ? item->getIf<EditorValue::Array>() : nullptr;
    if (!array || array->size() > 65536u) return false;
    links.clear();
    links.reserve(array->size());
    for (const auto& encoded : *array) {
        procgen::road::RoadLaneConnection link;
        if (!decodeLink(encoded, link)) return false;
        links.push_back(link);
    }
    return true;
}

EditorValue encodeLaneCounts(std::uint32_t edgeId, int lanesForward, int lanesBackward,
                             const std::vector<procgen::road::RoadLaneConnection>& restoreLinks,
                             const std::vector<procgen::road::RoadLaneConnection>& restoreBlockedLinks) {
    EditorValue::Array links;
    links.reserve(restoreLinks.size());
    for (const auto& link : restoreLinks) links.push_back(encodeLink(link));
    return EditorValue::Object{{"id", std::int64_t{edgeId}},
                               {"lanesForward", std::int64_t{lanesForward}},
                               {"lanesBackward", std::int64_t{lanesBackward}},
                               {"restoreLinks", std::move(links)},
                               {"restoreBlockedLinks", encodeLinks(restoreBlockedLinks)}};
}

bool decodeLaneCounts(const EditorValue& value, std::uint32_t& edgeId, int& lanesForward, int& lanesBackward,
                      std::vector<procgen::road::RoadLaneConnection>& restoreLinks,
                      std::vector<procgen::road::RoadLaneConnection>& restoreBlockedLinks) {
    if (!readId(value, "id", edgeId) || !readSmallInt(value, "lanesForward", 0, 8, lanesForward) ||
        !readSmallInt(value, "lanesBackward", 0, 8, lanesBackward))
        return false;
    const auto* encoded = field(value, "restoreLinks");
    const auto* links   = encoded ? encoded->getIf<EditorValue::Array>() : nullptr;
    if (!links || links->size() > 65536u) return false;
    restoreLinks.clear();
    restoreLinks.reserve(links->size());
    for (const auto& entry : *links) {
        procgen::road::RoadLaneConnection link;
        if (!decodeLink(entry, link)) return false;
        restoreLinks.push_back(link);
    }
    const auto* blocked = field(value, "restoreBlockedLinks");
    return blocked && decodeLinks(*blocked, restoreBlockedLinks);
}

EditorValue encodeReconnectEndpoint(
    std::uint32_t edgeId, bool fromEndpoint, std::uint32_t nodeId,
    const std::vector<procgen::road::RoadLaneConnection>& restoreLinks,
    const std::vector<procgen::road::RoadLaneConnection>& restoreBlockedLinks) {
    return EditorValue::Object{{"id", std::int64_t{edgeId}},
                               {"fromEndpoint", fromEndpoint},
                               {"nodeId", std::int64_t{nodeId}},
                               {"restoreLinks", encodeLinks(restoreLinks)},
                               {"restoreBlockedLinks", encodeLinks(restoreBlockedLinks)}};
}

bool decodeReconnectEndpoint(EditorValue const& value, std::uint32_t& edgeId, bool& fromEndpoint,
                             std::uint32_t& nodeId,
                             std::vector<procgen::road::RoadLaneConnection>& restoreLinks,
                             std::vector<procgen::road::RoadLaneConnection>& restoreBlockedLinks) {
    const auto* endpointValue = field(value, "fromEndpoint");
    const auto* endpoint = endpointValue ? endpointValue->getIf<bool>() : nullptr;
    const auto* links = field(value, "restoreLinks");
    const auto* blocked = field(value, "restoreBlockedLinks");
    return endpoint && links && blocked && readId(value, "id", edgeId) && readId(value, "nodeId", nodeId) &&
           decodeLinks(*links, restoreLinks) && decodeLinks(*blocked, restoreBlockedLinks) &&
           (fromEndpoint = *endpoint, true);
}

EditorValue encodeDetachEndpoint(std::uint32_t edgeId, bool fromEndpoint, std::uint32_t nodeId) {
    return EditorValue::Object{{"id", std::int64_t{edgeId}},
                               {"fromEndpoint", fromEndpoint},
                               {"nodeId", std::int64_t{nodeId}}};
}

bool decodeDetachEndpoint(const EditorValue& value, std::uint32_t& edgeId, bool& fromEndpoint,
                          std::uint32_t& nodeId) {
    const auto* endpointValue = field(value, "fromEndpoint");
    const auto* endpoint = endpointValue ? endpointValue->getIf<bool>() : nullptr;
    return endpoint && readId(value, "id", edgeId) && readId(value, "nodeId", nodeId) &&
           (fromEndpoint = *endpoint, true);
}

EditorValue encodeReattachEndpoint(
    std::uint32_t edgeId, bool fromEndpoint, std::uint32_t oldNodeId, std::uint32_t detachedNodeId,
    const std::vector<procgen::road::RoadLaneConnection>& restoreLinks,
    const std::vector<procgen::road::RoadLaneConnection>& restoreBlockedLinks) {
    return EditorValue::Object{{"id", std::int64_t{edgeId}},
                               {"fromEndpoint", fromEndpoint},
                               {"oldNodeId", std::int64_t{oldNodeId}},
                               {"detachedNodeId", std::int64_t{detachedNodeId}},
                               {"restoreLinks", encodeLinks(restoreLinks)},
                               {"restoreBlockedLinks", encodeLinks(restoreBlockedLinks)}};
}

bool decodeReattachEndpoint(const EditorValue& value, std::uint32_t& edgeId, bool& fromEndpoint,
                            std::uint32_t& oldNodeId, std::uint32_t& detachedNodeId,
                            std::vector<procgen::road::RoadLaneConnection>& restoreLinks,
                            std::vector<procgen::road::RoadLaneConnection>& restoreBlockedLinks) {
    const auto* endpointValue = field(value, "fromEndpoint");
    const auto* endpoint = endpointValue ? endpointValue->getIf<bool>() : nullptr;
    const auto* links = field(value, "restoreLinks");
    const auto* blocked = field(value, "restoreBlockedLinks");
    return endpoint && links && blocked && readId(value, "id", edgeId) &&
           readId(value, "oldNodeId", oldNodeId) && readId(value, "detachedNodeId", detachedNodeId) &&
           decodeLinks(*links, restoreLinks) && decodeLinks(*blocked, restoreBlockedLinks) &&
           (fromEndpoint = *endpoint, true);
}

EditorValue encodeSplit(std::uint32_t edgeId, std::size_t controlPointIndex, float junctionRadius,
                        const procgen::road::RoadEdgeSplitResult& result) {
    return EditorValue::Object{{"id", std::int64_t{edgeId}},
                               {"controlPointIndex", std::int64_t{static_cast<std::int64_t>(controlPointIndex)}},
                               {"junctionRadius", junctionRadius},
                               {"nodeId", std::int64_t{result.nodeId}},
                               {"secondEdgeId", std::int64_t{result.secondEdgeId}}};
}

bool decodeSplit(const EditorValue& value, std::uint32_t& edgeId, std::size_t& controlPointIndex,
                 float& junctionRadius, std::uint32_t& nodeId, std::uint32_t& secondEdgeId) {
    const auto* indexValue = field(value, "controlPointIndex");
    const auto* index      = indexValue ? indexValue->getIf<std::int64_t>() : nullptr;
    if (!readId(value, "id", edgeId) || !index || *index <= 0 || *index > 4095 ||
        !readNumber(value, "junctionRadius", junctionRadius) || !readId(value, "nodeId", nodeId) ||
        !readId(value, "secondEdgeId", secondEdgeId))
        return false;
    controlPointIndex = static_cast<std::size_t>(*index);
    return true;
}

EditorValue encodePositionSplit(std::uint32_t edgeId, procgen::road::RoadControlPoint position, float maxDistance,
                                float junctionRadius, const procgen::road::RoadEdgeSplitResult& result) {
    return EditorValue::Object{{"id", std::int64_t{edgeId}},
                               {"x", position.x},
                               {"y", position.y},
                               {"z", position.z},
                               {"maxDistance", maxDistance},
                               {"junctionRadius", junctionRadius},
                               {"nodeId", std::int64_t{result.nodeId}},
                               {"secondEdgeId", std::int64_t{result.secondEdgeId}}};
}

bool decodePositionSplit(const EditorValue& value, std::uint32_t& edgeId,
                         procgen::road::RoadControlPoint& position, float& maxDistance, float& junctionRadius,
                         std::uint32_t& nodeId, std::uint32_t& secondEdgeId) {
    return readId(value, "id", edgeId) && readNumber(value, "x", position.x) &&
           readNumber(value, "y", position.y) && readNumber(value, "z", position.z) &&
           readNumber(value, "maxDistance", maxDistance) &&
           readNumber(value, "junctionRadius", junctionRadius) && readId(value, "nodeId", nodeId) &&
           readId(value, "secondEdgeId", secondEdgeId);
}

EditorValue encodeMergeNodes(std::uint32_t keepNodeId, std::uint32_t removeNodeId, float maxDistance) {
    return EditorValue::Object{{"keepNodeId", std::int64_t{keepNodeId}},
                               {"removeNodeId", std::int64_t{removeNodeId}},
                               {"maxDistance", maxDistance}};
}

bool decodeMergeNodes(const EditorValue& value, std::uint32_t& keepNodeId, std::uint32_t& removeNodeId,
                      float& maxDistance) {
    return readId(value, "keepNodeId", keepNodeId) && readId(value, "removeNodeId", removeNodeId) &&
           readNumber(value, "maxDistance", maxDistance);
}

EditorValue encodeUnmergeNodes(const procgen::road::RoadNode& removedNode,
                               const std::vector<procgen::road::RoadEdge>& affectedEdges,
                               const std::vector<procgen::road::RoadLaneConnection>& affectedLinks,
                               const std::vector<procgen::road::RoadLaneConnection>& affectedBlockedLinks) {
    EditorValue::Array edges;
    edges.reserve(affectedEdges.size());
    for (const auto& edge : affectedEdges) edges.push_back(encodeEdgeRecord(edge, {}));
    EditorValue::Array links;
    links.reserve(affectedLinks.size());
    for (const auto& link : affectedLinks) links.push_back(encodeLink(link));
    EditorValue::Array blockedLinks;
    blockedLinks.reserve(affectedBlockedLinks.size());
    for (const auto& link : affectedBlockedLinks) blockedLinks.push_back(encodeLink(link));
    return EditorValue::Object{{"node", encodeNodeRecord(removedNode)},
                               {"edges", std::move(edges)},
                               {"laneLinks", std::move(links)},
                               {"blockedLaneLinks", std::move(blockedLinks)}};
}

bool decodeUnmergeNodes(const EditorValue& value, procgen::road::RoadNode& removedNode,
                        std::vector<procgen::road::RoadEdge>& affectedEdges,
                        std::vector<procgen::road::RoadLaneConnection>& affectedLinks,
                        std::vector<procgen::road::RoadLaneConnection>& affectedBlockedLinks) {
    const auto* nodeValue = field(value, "node");
    const auto* edgesValue = field(value, "edges");
    const auto* linksValue = field(value, "laneLinks");
    const auto* blockedValue = field(value, "blockedLaneLinks");
    const auto* edges = edgesValue ? edgesValue->getIf<EditorValue::Array>() : nullptr;
    const auto* links = linksValue ? linksValue->getIf<EditorValue::Array>() : nullptr;
    const auto* blocked = blockedValue ? blockedValue->getIf<EditorValue::Array>() : nullptr;
    if (!nodeValue || !edges || !links || !blocked || edges->size() > 65536u || links->size() > 262144u ||
        blocked->size() > 262144u ||
        !decodeNodeRecord(*nodeValue, removedNode))
        return false;
    affectedEdges.clear();
    affectedEdges.reserve(edges->size());
    for (const auto& encoded : *edges) {
        procgen::road::RoadEdge edge;
        std::vector<procgen::road::RoadLaneConnection> embeddedLinks;
        if (!decodeEdgeRecord(encoded, edge, embeddedLinks) || !embeddedLinks.empty()) return false;
        affectedEdges.push_back(std::move(edge));
    }
    affectedLinks.clear();
    affectedLinks.reserve(links->size());
    for (const auto& encoded : *links) {
        procgen::road::RoadLaneConnection link;
        if (!decodeLink(encoded, link)) return false;
        affectedLinks.push_back(link);
    }
    affectedBlockedLinks.clear();
    affectedBlockedLinks.reserve(blocked->size());
    for (const auto& encoded : *blocked) {
        procgen::road::RoadLaneConnection link;
        if (!decodeLink(encoded, link)) return false;
        affectedBlockedLinks.push_back(link);
    }
    return true;
}

EditorValue encodeUnsplit(const procgen::road::RoadEdge& edge,
                          const std::vector<procgen::road::RoadLaneConnection>& links,
                          const std::vector<procgen::road::RoadLaneConnection>& blockedLinks,
                          std::uint32_t nodeId, std::uint32_t secondEdgeId) {
    return EditorValue::Object{{"nodeId", std::int64_t{nodeId}},
                               {"secondEdgeId", std::int64_t{secondEdgeId}},
                               {"edge", encodeEdgeRecord(edge, links, blockedLinks)}};
}

bool decodeUnsplit(const EditorValue& value, procgen::road::RoadEdge& edge,
                   std::vector<procgen::road::RoadLaneConnection>& links,
                   std::vector<procgen::road::RoadLaneConnection>& blockedLinks,
                   std::uint32_t& nodeId, std::uint32_t& secondEdgeId) {
    const auto* encodedEdge = field(value, "edge");
    return readId(value, "nodeId", nodeId) && readId(value, "secondEdgeId", secondEdgeId) && encodedEdge &&
           decodeEdgeRecord(*encodedEdge, edge, links, &blockedLinks);
}

EditorValue encodeConnectAtPosition(std::uint32_t edgeId, procgen::road::RoadControlPoint position,
                                    float maxDistance, float junctionRadius,
                                    const procgen::road::RoadEdgeSplitResult& split,
                                    const procgen::road::RoadEdge& branch) {
    auto object = *encodePositionSplit(edgeId, position, maxDistance, junctionRadius, split)
                       .getIf<EditorValue::Object>();
    object["branch"] = encodeEdgeRecord(branch, {});
    return object;
}

bool decodeConnectAtPosition(const EditorValue& value, std::uint32_t& edgeId,
                             procgen::road::RoadControlPoint& position, float& maxDistance, float& junctionRadius,
                             std::uint32_t& nodeId, std::uint32_t& secondEdgeId,
                             procgen::road::RoadEdge& branch) {
    const auto* branchValue = field(value, "branch");
    std::vector<procgen::road::RoadLaneConnection> embeddedLinks;
    return decodePositionSplit(value, edgeId, position, maxDistance, junctionRadius, nodeId, secondEdgeId) &&
           branchValue && decodeEdgeRecord(*branchValue, branch, embeddedLinks) && embeddedLinks.empty();
}

EditorValue encodeDisconnectAtPosition(const procgen::road::RoadEdge& original,
                                       const std::vector<procgen::road::RoadLaneConnection>& originalLinks,
                                       const std::vector<procgen::road::RoadLaneConnection>& originalBlockedLinks,
                                       std::uint32_t nodeId, std::uint32_t secondEdgeId,
                                       std::uint32_t branchEdgeId) {
    auto object = *encodeUnsplit(original, originalLinks, originalBlockedLinks, nodeId, secondEdgeId)
                       .getIf<EditorValue::Object>();
    object["branchEdgeId"] = std::int64_t{branchEdgeId};
    return object;
}

bool decodeDisconnectAtPosition(const EditorValue& value, procgen::road::RoadEdge& original,
                                std::vector<procgen::road::RoadLaneConnection>& originalLinks,
                                std::vector<procgen::road::RoadLaneConnection>& originalBlockedLinks,
                                std::uint32_t& nodeId, std::uint32_t& secondEdgeId,
                                std::uint32_t& branchEdgeId) {
    return decodeUnsplit(value, original, originalLinks, originalBlockedLinks, nodeId, secondEdgeId) &&
           readId(value, "branchEdgeId", branchEdgeId);
}

EditorValue encodeIntersectionConnect(std::uint32_t firstEdgeId, std::uint32_t secondEdgeId,
                                      float firstParameter, float secondParameter,
                                      procgen::road::RoadControlPoint mergedPoint, float maximumHeightDelta,
                                      float junctionRadius, const procgen::road::RoadEdgeSplitResult& firstSplit,
                                      const procgen::road::RoadEdgeSplitResult& secondSplit) {
    return EditorValue::Object{
        {"firstEdgeId", std::int64_t{firstEdgeId}},
        {"secondEdgeId", std::int64_t{secondEdgeId}},
        {"firstParameter", firstParameter},
        {"secondParameter", secondParameter},
        {"mergedPoint", EditorValue::Array{mergedPoint.x, mergedPoint.y, mergedPoint.z}},
        {"maximumHeightDelta", maximumHeightDelta},
        {"junctionRadius", junctionRadius},
        {"firstNodeId", std::int64_t{firstSplit.nodeId}},
        {"firstSecondEdgeId", std::int64_t{firstSplit.secondEdgeId}},
        {"secondNodeId", std::int64_t{secondSplit.nodeId}},
        {"secondSecondEdgeId", std::int64_t{secondSplit.secondEdgeId}}};
}

bool decodePoint(const EditorValue& value, const char* name, procgen::road::RoadControlPoint& point) {
    const auto* item = field(value, name);
    const auto* values = item ? item->getIf<EditorValue::Array>() : nullptr;
    if (!values || values->size() != 3) return false;
    const auto* x = (*values)[0].getIf<double>();
    const auto* y = (*values)[1].getIf<double>();
    const auto* z = (*values)[2].getIf<double>();
    if (!x || !y || !z || !std::isfinite(*x) || !std::isfinite(*y) || !std::isfinite(*z)) return false;
    point = {static_cast<float>(*x), static_cast<float>(*y), static_cast<float>(*z)};
    return true;
}

bool decodeIntersectionConnect(const EditorValue& value, std::uint32_t& firstEdgeId,
                               std::uint32_t& secondEdgeId, float& firstParameter, float& secondParameter,
                               procgen::road::RoadControlPoint& mergedPoint, float& maximumHeightDelta,
                               float& junctionRadius, std::uint32_t& firstNodeId,
                               std::uint32_t& firstSecondEdgeId, std::uint32_t& secondNodeId,
                               std::uint32_t& secondSecondEdgeId) {
    return readId(value, "firstEdgeId", firstEdgeId) && readId(value, "secondEdgeId", secondEdgeId) &&
           readNumber(value, "firstParameter", firstParameter) &&
           readNumber(value, "secondParameter", secondParameter) &&
           decodePoint(value, "mergedPoint", mergedPoint) &&
           readNumber(value, "maximumHeightDelta", maximumHeightDelta) &&
           readNumber(value, "junctionRadius", junctionRadius) && readId(value, "firstNodeId", firstNodeId) &&
           readId(value, "firstSecondEdgeId", firstSecondEdgeId) &&
           readId(value, "secondNodeId", secondNodeId) &&
           readId(value, "secondSecondEdgeId", secondSecondEdgeId);
}

EditorValue encodeIntersectionDisconnect(const procgen::road::RoadEdge& first,
                                         const procgen::road::RoadEdge& second,
                                         const std::vector<procgen::road::RoadLaneConnection>& links,
                                         const std::vector<procgen::road::RoadLaneConnection>& blockedLinks,
                                         std::uint32_t sharedNodeId, std::uint32_t firstSecondEdgeId,
                                         std::uint32_t secondSecondEdgeId) {
    EditorValue::Array encodedLinks;
    encodedLinks.reserve(links.size());
    for (const auto& link : links) encodedLinks.push_back(encodeLink(link));
    EditorValue::Array encodedBlockedLinks;
    encodedBlockedLinks.reserve(blockedLinks.size());
    for (const auto& link : blockedLinks) encodedBlockedLinks.push_back(encodeLink(link));
    return EditorValue::Object{{"first", encodeEdgeRecord(first, {})},
                               {"second", encodeEdgeRecord(second, {})},
                               {"laneLinks", std::move(encodedLinks)},
                               {"blockedLaneLinks", std::move(encodedBlockedLinks)},
                               {"sharedNodeId", std::int64_t{sharedNodeId}},
                               {"firstSecondEdgeId", std::int64_t{firstSecondEdgeId}},
                               {"secondSecondEdgeId", std::int64_t{secondSecondEdgeId}}};
}

bool decodeIntersectionDisconnect(const EditorValue& value, procgen::road::RoadEdge& first,
                                  procgen::road::RoadEdge& second,
                                  std::vector<procgen::road::RoadLaneConnection>& links,
                                  std::vector<procgen::road::RoadLaneConnection>& blockedLinks,
                                  std::uint32_t& sharedNodeId, std::uint32_t& firstSecondEdgeId,
                                  std::uint32_t& secondSecondEdgeId) {
    const auto* firstValue = field(value, "first");
    const auto* secondValue = field(value, "second");
    const auto* linksValue = field(value, "laneLinks");
    const auto* blockedValue = field(value, "blockedLaneLinks");
    const auto* encodedLinks = linksValue ? linksValue->getIf<EditorValue::Array>() : nullptr;
    const auto* encodedBlockedLinks = blockedValue ? blockedValue->getIf<EditorValue::Array>() : nullptr;
    std::vector<procgen::road::RoadLaneConnection> embedded;
    if (!firstValue || !secondValue || !encodedLinks || !encodedBlockedLinks || encodedLinks->size() > 262144u ||
        encodedBlockedLinks->size() > 262144u ||
        !decodeEdgeRecord(*firstValue, first, embedded) || !embedded.empty() ||
        !decodeEdgeRecord(*secondValue, second, embedded) || !embedded.empty() ||
        !readId(value, "sharedNodeId", sharedNodeId) ||
        !readId(value, "firstSecondEdgeId", firstSecondEdgeId) ||
        !readId(value, "secondSecondEdgeId", secondSecondEdgeId))
        return false;
    links.clear();
    links.reserve(encodedLinks->size());
    for (const auto& encoded : *encodedLinks) {
        procgen::road::RoadLaneConnection link;
        if (!decodeLink(encoded, link)) return false;
        links.push_back(link);
    }
    blockedLinks.clear();
    blockedLinks.reserve(encodedBlockedLinks->size());
    for (const auto& encoded : *encodedBlockedLinks) {
        procgen::road::RoadLaneConnection link;
        if (!decodeLink(encoded, link)) return false;
        blockedLinks.push_back(link);
    }
    return true;
}


#include "procgen/road/RoadRecordCodec.inc"

editing::GizmoPrimitive linePrimitive(std::string id, const procgen::road::RoadControlPoint& from,
                                      const procgen::road::RoadControlPoint& to) {
    const double dx     = static_cast<double>(to.x) - from.x;
    const double dy     = static_cast<double>(to.y) - from.y;
    const double dz     = static_cast<double>(to.z) - from.z;
    const double length = std::sqrt(dx * dx + dy * dy + dz * dz);
    editing::GizmoPrimitive line;
    line.id       = std::move(id);
    line.kind     = "line";
    line.position = {from.x, from.y, from.z};
    line.color    = {0.25, 0.75, 1.0, 0.9};
    line.length   = length;
    line.dashed   = true;
    if (length > 0.0) line.direction = {dx / length, dy / length, dz / length};
    return line;
}

}  // namespace detail

using namespace detail;

RoadNetworkEditTarget::RoadNetworkEditTarget(std::string id, procgen::road::RoadNetwork& network)
    : id_(std::move(id)), network_(&network) {}

std::unique_ptr<RoadNetworkEditTarget> RoadNetworkEditTarget::createOwned(std::string id) {
    return std::unique_ptr<RoadNetworkEditTarget>(
        new RoadNetworkEditTarget(std::move(id), std::make_unique<procgen::road::RoadNetwork>()));
}

RoadNetworkEditTarget::RoadNetworkEditTarget(std::string id, std::unique_ptr<procgen::road::RoadNetwork> candidate)
    : id_(std::move(id)), candidate_(std::move(candidate)), network_(candidate_.get()) {}

editing::TargetDescriptor RoadNetworkEditTarget::describe() const {
    return {targetId(), "procgen.road-network", revision(), false,
            {editing::IEditingSnapshotProvider::editingCapabilityId()}};
}

void* RoadNetworkEditTarget::queryCapability(const editing::CapabilityId& capability) {
    if (capability == editing::IEditingSnapshotProvider::editingCapabilityId())
        return static_cast<editing::IEditingSnapshotProvider*>(this);
    return nullptr;
}

editing::GizmoSnapshot RoadNetworkEditTarget::gizmo(std::size_t maximumPrimitives, bool showLabels) const {
    editing::GizmoSnapshot snapshot;
    snapshot.target         = id_;
    snapshot.targetRevision = revision();

    std::size_t required = network_->nodes().size() * (showLabels ? 2u : 1u);
    for (const auto& edge : network_->edges()) {
        if (edge.controlPoints.size() >= 2) required += edge.controlPoints.size() - 1;
        if (edge.controlPoints.size() > 2) required += edge.controlPoints.size() - 2;
    }
    if (maximumPrimitives == 0 || required > maximumPrimitives) {
        snapshot.status = EditorStatus::Rejected;
        snapshot.diagnostics.push_back(editing::ruleDiagnostic(
            eve::DiagnosticCode::PreconditionViolation, editing::RuleId("editor.road.gizmo-budget"),
            editing::DiagnosticSeverity::Error, "Road overlay exceeds the primitive budget"));
        return snapshot;
    }

    snapshot.primitives.reserve(required);
    for (const auto& node : network_->nodes()) {
        const auto identity = std::to_string(node.id);
        editing::GizmoPrimitive handle;
        handle.id       = "road.node." + identity;
        handle.kind     = "sphere";
        handle.position = {node.x, node.y, node.z};
        handle.color    = {1.0, 0.65, 0.15, 1.0};
        handle.radius   = 0.45;
        snapshot.primitives.push_back(std::move(handle));
        if (showLabels) {
            editing::GizmoPrimitive label;
            label.id       = "road.node-label." + identity;
            label.kind     = "text";
            label.position = {node.x, static_cast<double>(node.y) + 0.7, node.z};
            label.color    = {1.0, 0.9, 0.55, 1.0};
            label.text     = "Node " + identity;
            snapshot.primitives.push_back(std::move(label));
        }
    }
    for (const auto& edge : network_->edges()) {
        const auto edgeIdentity = std::to_string(edge.id);
        for (std::size_t i = 1; i < edge.controlPoints.size(); ++i) {
            snapshot.primitives.push_back(linePrimitive("road.edge." + edgeIdentity + ".segment." +
                                                            std::to_string(i - 1),
                                                        edge.controlPoints[i - 1], edge.controlPoints[i]));
        }
        for (std::size_t i = 1; i + 1 < edge.controlPoints.size(); ++i) {
            const auto& point = edge.controlPoints[i];
            editing::GizmoPrimitive handle;
            handle.id       = "road.edge." + edgeIdentity + ".control." + std::to_string(i);
            handle.kind     = "sphere";
            handle.position = {point.x, point.y, point.z};
            handle.color    = {0.2, 0.8, 1.0, 1.0};
            handle.radius   = 0.3;
            snapshot.primitives.push_back(std::move(handle));
        }
    }
    snapshot.status = EditorStatus::Applied;
    return snapshot;
}

EditorValue RoadNetworkEditTarget::snapshotValue() const {
    EditorValue::Array nodes;
    nodes.reserve(network_->nodes().size());
    for (const auto& node : network_->nodes()) nodes.push_back(encodeNodeRecord(node));

    EditorValue::Array edges;
    edges.reserve(network_->edges().size());
    for (const auto& edge : network_->edges()) edges.push_back(encodeEdgeRecord(edge, {}));

    EditorValue::Array laneLinks;
    laneLinks.reserve(network_->laneLinks().size());
    for (const auto& link : network_->laneLinks()) laneLinks.push_back(encodeLink(link));
    EditorValue::Array blockedLaneLinks;
    blockedLaneLinks.reserve(network_->blockedLaneLinks().size());
    for (const auto& link : network_->blockedLaneLinks()) blockedLaneLinks.push_back(encodeLink(link));

    return EditorValue::Object{{"schema", "eve.procgen.roadNetwork"},
                               {"schemaVersion", std::int64_t{1}},
                               {"nodes", std::move(nodes)},
                               {"edges", std::move(edges)},
                               {"laneLinks", std::move(laneLinks)},
                               {"blockedLaneLinks", std::move(blockedLaneLinks)}};
}

EditorResult<void> RoadNetworkEditTarget::loadSnapshot(const EditorValue& snapshot) {
    const auto* schemaValue  = field(snapshot, "schema");
    const auto* versionValue = field(snapshot, "schemaVersion");
    const auto* nodesValue   = field(snapshot, "nodes");
    const auto* edgesValue   = field(snapshot, "edges");
    const auto* linksValue   = field(snapshot, "laneLinks");
    const auto* blockedValue = field(snapshot, "blockedLaneLinks");
    const auto* schema       = schemaValue ? schemaValue->getIf<std::string>() : nullptr;
    const auto* version      = versionValue ? versionValue->getIf<std::int64_t>() : nullptr;
    const auto* nodes        = nodesValue ? nodesValue->getIf<EditorValue::Array>() : nullptr;
    const auto* edges        = edgesValue ? edgesValue->getIf<EditorValue::Array>() : nullptr;
    const auto* links        = linksValue ? linksValue->getIf<EditorValue::Array>() : nullptr;
    const auto* blocked      = blockedValue ? blockedValue->getIf<EditorValue::Array>() : nullptr;
    if (!schema || *schema != "eve.procgen.roadNetwork" || !version || *version != 1 || !nodes || !edges ||
        !links)
        return reject<void>("editor.road.invalid-snapshot", "Road snapshot schema is unsupported",
                            EditorStatus::Unsupported);
    constexpr std::size_t maximumNodes = 65536u, maximumEdges = 65536u, maximumLinks = 262144u;
    if (nodes->size() > maximumNodes || edges->size() > maximumEdges || links->size() > maximumLinks ||
        (blocked && blocked->size() > maximumLinks))
        return reject<void>("editor.road.snapshot-budget", "Road snapshot exceeds the graph budget");

    procgen::road::RoadNetwork candidate;
    for (const auto& encoded : *nodes) {
        procgen::road::RoadNode node;
        if (!decodeNodeRecord(encoded, node))
            return reject<void>("editor.road.invalid-snapshot-node", "Road snapshot contains an invalid node");
        auto restored = candidate.restoreNode(node);
        if (!restored.ok()) return roadFailure<void>(restored.status());
    }
    for (const auto& encoded : *edges) {
        procgen::road::RoadEdge                        edge;
        std::vector<procgen::road::RoadLaneConnection> embeddedLinks;
        if (!decodeEdgeRecord(encoded, edge, embeddedLinks) || !embeddedLinks.empty())
            return reject<void>("editor.road.invalid-snapshot-edge", "Road snapshot contains an invalid edge");
        auto restored = candidate.restoreEdge(std::move(edge));
        if (!restored.ok()) return roadFailure<void>(restored.status());
    }
    for (const auto& encoded : *links) {
        procgen::road::RoadLaneConnection link;
        if (!decodeLink(encoded, link))
            return reject<void>("editor.road.invalid-snapshot-link", "Road snapshot contains an invalid lane link");
        auto restored = candidate.addLaneLink(link);
        if (!restored.ok()) return roadFailure<void>(restored.status());
    }
    if (blocked) {
        for (const auto& encoded : *blocked) {
            procgen::road::RoadLaneConnection link;
            if (!decodeLink(encoded, link))
                return reject<void>("editor.road.invalid-snapshot-block",
                                    "Road snapshot contains an invalid blocked lane link");
            auto restored = candidate.blockLaneLink(link);
            if (!restored.ok()) return roadFailure<void>(restored.status());
            if (restored.value())
                return reject<void>("editor.road.invalid-snapshot-block",
                                    "Road snapshot marks the same lane link active and blocked");
        }
    }
    auto valid = candidate.validate();
    if (!valid.ok()) return roadFailure<void>(valid.status());
    *network_ = std::move(candidate);
    dirty_.include(0, 0);
    return editing::applied();
}

std::unique_ptr<editing::IDomainOperationTarget> RoadNetworkEditTarget::cloneDomainState() const {
    return std::unique_ptr<editing::IDomainOperationTarget>(
        new RoadNetworkEditTarget(id_, std::make_unique<procgen::road::RoadNetwork>(*network_)));
}

EditorResult<void> RoadNetworkEditTarget::commitDomainState(
    std::unique_ptr<editing::IDomainOperationTarget> candidate) {
    auto* road = dynamic_cast<RoadNetworkEditTarget*>(candidate.get());
    if (!road || road->id_ != id_ || !road->candidate_)
        return reject<void>("editor.road.candidate", "Road transaction candidate does not match",
                            EditorStatus::Conflict);
    auto valid = road->network_->validate();
    if (!valid.ok()) return roadFailure<void>(valid.status());
    *network_ = std::move(*road->candidate_);
    dirty_.include(0, 0);
    return editing::applied();
}

}  // namespace eve::procgen_editing
