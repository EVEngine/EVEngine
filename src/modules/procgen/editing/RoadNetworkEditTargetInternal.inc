#pragma once

#include "editing/EditingResult.h"
#include "procgen/editing/RoadNetworkEditTarget.h"

#include <string>
#include <vector>

namespace eve::procgen_editing::detail {

template <class T>
EditorResult<T> reject(const char* rule, std::string message,
                       EditorStatus status = EditorStatus::Rejected) {
    return editing::failed<T>(status, editing::RuleId(rule), std::move(message));
}

template <class T>
EditorResult<T> commandError(const char* rule, std::string message) {
    return reject<T>(rule, std::move(message), EditorStatus::Rejected);
}

template <class T>
EditorResult<T> roadFailure(const eve::Status& status) {
    const auto  code       = status.code();
    const auto  editStatus = code == eve::StatusCode::NotFound ? EditorStatus::NotFound : EditorStatus::Rejected;
    const auto* diagnostic = status.primaryDiagnostic();
    return reject<T>("editor.road.operation", diagnostic ? std::string(diagnostic->message()) : "Road edit failed",
                     editStatus);
}

const EditorValue* field(const EditorValue& value, const char* name);
bool readId(const EditorValue& value, const char* name, std::uint32_t& output);
bool readNumber(const EditorValue& value, const char* name, float& output);
bool readSmallInt(const EditorValue& value, const char* name, int minimum, int maximum, int& output);
struct RoadIntersection {
    int count = 0;
    float firstParameter = 0.f;
    float secondParameter = 0.f;
    procgen::road::RoadControlPoint firstPoint;
    procgen::road::RoadControlPoint secondPoint;
    procgen::road::RoadControlPoint mergedPoint;
};
Result<RoadIntersection> findUniqueInteriorIntersection(const procgen::road::RoadEdge& first,
                                                        const procgen::road::RoadEdge& second,
                                                        float maximumHeightDelta);
EditorValue encodeNode(std::uint32_t id, float x, float y, float z);
EditorValue encodeNodeRadius(std::uint32_t id, float junctionRadius);
bool decodeNodeRadius(const EditorValue& value, std::uint32_t& id, float& junctionRadius);
EditorValue encodeNodeControl(std::uint32_t id, procgen::road::RoadJunctionControl control);
bool decodeNodeControl(const EditorValue& value, std::uint32_t& id, procgen::road::RoadJunctionControl& control);
EditorValue encodeEdge(std::uint32_t id, const std::vector<procgen::road::RoadControlPoint>& points);
EditorValue encodeStyle(std::uint32_t id, const procgen::road::RoadStyle& style);
bool decodeStyle(const EditorValue& value, std::uint32_t& id, procgen::road::RoadStyle& style);
bool decodeEdge(const EditorValue& value, std::uint32_t& id, std::vector<procgen::road::RoadControlPoint>& points);
EditorValue encodeNodeRecord(const procgen::road::RoadNode& node);
bool decodeNodeRecord(const EditorValue& value, procgen::road::RoadNode& node);
EditorValue encodeLink(const procgen::road::RoadLaneConnection& link);
bool decodeLink(const EditorValue& value, procgen::road::RoadLaneConnection& link);
EditorValue encodeBlockedLink(const procgen::road::RoadLaneConnection& link, bool restoreActive);
bool decodeBlockedLink(const EditorValue& value, procgen::road::RoadLaneConnection& link, bool& restoreActive);
bool sameLink(const procgen::road::RoadLaneConnection& left, const procgen::road::RoadLaneConnection& right);
EditorValue encodeLinks(const std::vector<procgen::road::RoadLaneConnection>& links);
bool decodeLinks(const EditorValue& value, std::vector<procgen::road::RoadLaneConnection>& links);
EditorValue encodeLaneCounts(std::uint32_t edgeId, int lanesForward, int lanesBackward,
                             const std::vector<procgen::road::RoadLaneConnection>& restoreLinks,
                             const std::vector<procgen::road::RoadLaneConnection>& restoreBlockedLinks);
bool decodeLaneCounts(const EditorValue& value, std::uint32_t& edgeId, int& lanesForward, int& lanesBackward,
                      std::vector<procgen::road::RoadLaneConnection>& restoreLinks,
                      std::vector<procgen::road::RoadLaneConnection>& restoreBlockedLinks);
EditorValue encodeReconnectEndpoint(
    std::uint32_t edgeId, bool fromEndpoint, std::uint32_t nodeId,
    const std::vector<procgen::road::RoadLaneConnection>& restoreLinks,
    const std::vector<procgen::road::RoadLaneConnection>& restoreBlockedLinks);
bool decodeReconnectEndpoint(EditorValue const& value, std::uint32_t& edgeId, bool& fromEndpoint,
                             std::uint32_t& nodeId,
                             std::vector<procgen::road::RoadLaneConnection>& restoreLinks,
                             std::vector<procgen::road::RoadLaneConnection>& restoreBlockedLinks);
EditorValue encodeDetachEndpoint(std::uint32_t edgeId, bool fromEndpoint, std::uint32_t nodeId);
bool decodeDetachEndpoint(const EditorValue& value, std::uint32_t& edgeId, bool& fromEndpoint,
                          std::uint32_t& nodeId);
EditorValue encodeReattachEndpoint(
    std::uint32_t edgeId, bool fromEndpoint, std::uint32_t oldNodeId, std::uint32_t detachedNodeId,
    const std::vector<procgen::road::RoadLaneConnection>& restoreLinks,
    const std::vector<procgen::road::RoadLaneConnection>& restoreBlockedLinks);
bool decodeReattachEndpoint(const EditorValue& value, std::uint32_t& edgeId, bool& fromEndpoint,
                            std::uint32_t& oldNodeId, std::uint32_t& detachedNodeId,
                            std::vector<procgen::road::RoadLaneConnection>& restoreLinks,
                            std::vector<procgen::road::RoadLaneConnection>& restoreBlockedLinks);
EditorValue encodeSplit(std::uint32_t edgeId, std::size_t controlPointIndex, float junctionRadius,
                        const procgen::road::RoadEdgeSplitResult& result);
bool decodeSplit(const EditorValue& value, std::uint32_t& edgeId, std::size_t& controlPointIndex,
                 float& junctionRadius, std::uint32_t& nodeId, std::uint32_t& secondEdgeId);
EditorValue encodePositionSplit(std::uint32_t edgeId, procgen::road::RoadControlPoint position, float maxDistance,
                                float junctionRadius, const procgen::road::RoadEdgeSplitResult& result);
bool decodePositionSplit(const EditorValue& value, std::uint32_t& edgeId,
                         procgen::road::RoadControlPoint& position, float& maxDistance, float& junctionRadius,
                         std::uint32_t& nodeId, std::uint32_t& secondEdgeId);
EditorValue encodeMergeNodes(std::uint32_t keepNodeId, std::uint32_t removeNodeId, float maxDistance);
bool decodeMergeNodes(const EditorValue& value, std::uint32_t& keepNodeId, std::uint32_t& removeNodeId,
                      float& maxDistance);
EditorValue encodeUnmergeNodes(const procgen::road::RoadNode& removedNode,
                               const std::vector<procgen::road::RoadEdge>& affectedEdges,
                               const std::vector<procgen::road::RoadLaneConnection>& affectedLinks,
                               const std::vector<procgen::road::RoadLaneConnection>& affectedBlockedLinks);
bool decodeUnmergeNodes(const EditorValue& value, procgen::road::RoadNode& removedNode,
                        std::vector<procgen::road::RoadEdge>& affectedEdges,
                        std::vector<procgen::road::RoadLaneConnection>& affectedLinks,
                        std::vector<procgen::road::RoadLaneConnection>& affectedBlockedLinks);
EditorValue encodeUnsplit(const procgen::road::RoadEdge& edge,
                          const std::vector<procgen::road::RoadLaneConnection>& links,
                          const std::vector<procgen::road::RoadLaneConnection>& blockedLinks,
                          std::uint32_t nodeId, std::uint32_t secondEdgeId);
bool decodeUnsplit(const EditorValue& value, procgen::road::RoadEdge& edge,
                   std::vector<procgen::road::RoadLaneConnection>& links,
                   std::vector<procgen::road::RoadLaneConnection>& blockedLinks,
                   std::uint32_t& nodeId, std::uint32_t& secondEdgeId);
EditorValue encodeConnectAtPosition(std::uint32_t edgeId, procgen::road::RoadControlPoint position,
                                    float maxDistance, float junctionRadius,
                                    const procgen::road::RoadEdgeSplitResult& split,
                                    const procgen::road::RoadEdge& branch);
bool decodeConnectAtPosition(const EditorValue& value, std::uint32_t& edgeId,
                             procgen::road::RoadControlPoint& position, float& maxDistance,
                             float& junctionRadius, std::uint32_t& nodeId, std::uint32_t& secondEdgeId,
                             procgen::road::RoadEdge& branch);
EditorValue encodeDisconnectAtPosition(const procgen::road::RoadEdge& original,
                                       const std::vector<procgen::road::RoadLaneConnection>& originalLinks,
                                       const std::vector<procgen::road::RoadLaneConnection>& originalBlockedLinks,
                                       std::uint32_t nodeId, std::uint32_t secondEdgeId,
                                       std::uint32_t branchEdgeId);
bool decodeDisconnectAtPosition(const EditorValue& value, procgen::road::RoadEdge& original,
                                std::vector<procgen::road::RoadLaneConnection>& originalLinks,
                                std::vector<procgen::road::RoadLaneConnection>& originalBlockedLinks,
                                std::uint32_t& nodeId, std::uint32_t& secondEdgeId,
                                std::uint32_t& branchEdgeId);
EditorValue encodeIntersectionConnect(std::uint32_t firstEdgeId, std::uint32_t secondEdgeId,
                                      float firstParameter, float secondParameter,
                                      procgen::road::RoadControlPoint mergedPoint,
                                      float maximumHeightDelta, float junctionRadius,
                                      const procgen::road::RoadEdgeSplitResult& firstSplit,
                                      const procgen::road::RoadEdgeSplitResult& secondSplit);
bool decodeIntersectionConnect(const EditorValue& value, std::uint32_t& firstEdgeId,
                               std::uint32_t& secondEdgeId, float& firstParameter, float& secondParameter,
                               procgen::road::RoadControlPoint& mergedPoint, float& maximumHeightDelta,
                               float& junctionRadius, std::uint32_t& firstNodeId,
                               std::uint32_t& firstSecondEdgeId, std::uint32_t& secondNodeId,
                               std::uint32_t& secondSecondEdgeId);
EditorValue encodeIntersectionDisconnect(const procgen::road::RoadEdge& first,
                                         const procgen::road::RoadEdge& second,
                                         const std::vector<procgen::road::RoadLaneConnection>& links,
                                         const std::vector<procgen::road::RoadLaneConnection>& blockedLinks,
                                         std::uint32_t sharedNodeId, std::uint32_t firstSecondEdgeId,
                                         std::uint32_t secondSecondEdgeId);
bool decodeIntersectionDisconnect(const EditorValue& value, procgen::road::RoadEdge& first,
                                  procgen::road::RoadEdge& second,
                                  std::vector<procgen::road::RoadLaneConnection>& links,
                                  std::vector<procgen::road::RoadLaneConnection>& blockedLinks,
                                  std::uint32_t& sharedNodeId, std::uint32_t& firstSecondEdgeId,
                                  std::uint32_t& secondSecondEdgeId);
EditorValue encodeEdgeRecord(const procgen::road::RoadEdge& edge,
                             const std::vector<procgen::road::RoadLaneConnection>& links,
                             const std::vector<procgen::road::RoadLaneConnection>& blockedLinks = {});
bool decodeEdgeRecord(const EditorValue& value, procgen::road::RoadEdge& edge,
                      std::vector<procgen::road::RoadLaneConnection>& links,
                      std::vector<procgen::road::RoadLaneConnection>* blockedLinks = nullptr);

}  // namespace eve::procgen_editing::detail
