#include "procgen/PointGraphScript.h"

#include "procgen/PointGraph.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <functional>

namespace eve::procgen {

void exposePointGraph(ssq::Table& table) {
    auto graph = table.addClass<PointGraph>(
        "ProcgenPointGraph", std::function<PointGraph*()>([]() -> PointGraph* { return nullptr; }),
        true);
    graph.addFunc("addNode", &PointGraph::addNode);
    graph.addFunc("removeNode", &PointGraph::removeNode);
    graph.addFunc("hasNode", &PointGraph::hasNode);
    graph.addFunc("getNodeCount", &PointGraph::getNodeCount);
    graph.addFunc("getNodeId", &PointGraph::getNodeId);
    graph.addFunc("getNodeOperation", &PointGraph::getNodeOperation);
    graph.addFunc("connect", &PointGraph::connect);
    graph.addFunc("disconnect", &PointGraph::disconnect);
    graph.addFunc("getInputNode", &PointGraph::getInputNode);
    graph.addFunc("setNodePoints", &PointGraph::setNodePoints);
    graph.addFunc("setNodeSpatial", &PointGraph::setNodeSpatial);
    graph.addFunc("setNodeBiomeRules", &PointGraph::setNodeBiomeRules);
    graph.addFunc("setNodeShapeGrammar", &PointGraph::setNodeShapeGrammar);
    graph.addFunc("setNodeFloat", &PointGraph::setNodeFloat);
    graph.addFunc("setNodeInt", &PointGraph::setNodeInt);
    graph.addFunc("setNodeString", &PointGraph::setNodeString);
    graph.addFunc("exposeParameter", &PointGraph::exposeParameter);
    graph.addFunc("getParameterCount", &PointGraph::getParameterCount);
    graph.addFunc("getParameterName", &PointGraph::getParameterName);
    graph.addFunc("getParameterKind", &PointGraph::getParameterKind);
    graph.addFunc("hasParameterOverride", &PointGraph::hasParameterOverride);
    graph.addFunc("setParameterFloat", &PointGraph::setParameterFloat);
    graph.addFunc("setParameterInt", &PointGraph::setParameterInt);
    graph.addFunc("setParameterString", &PointGraph::setParameterString);
    graph.addFunc("getParameterFloat", &PointGraph::getParameterFloat);
    graph.addFunc("getParameterInt", &PointGraph::getParameterInt);
    graph.addFunc("getParameterString", &PointGraph::getParameterString);
    graph.addFunc("clearParameterOverride", &PointGraph::clearParameterOverride);
    graph.addFunc("setNodeSubgraph", &PointGraph::setNodeSubgraph);
    graph.addFunc("execute", &PointGraph::execute);
    graph.addFunc("setExecutionNodeBudget", &PointGraph::setExecutionNodeBudget);
    graph.addFunc("getExecutionNodeBudget", &PointGraph::getExecutionNodeBudget);
    graph.addFunc("setMaxNodeOutputPoints", &PointGraph::setMaxNodeOutputPoints);
    graph.addFunc("getMaxNodeOutputPoints", &PointGraph::getMaxNodeOutputPoints);
    graph.addFunc("setComputePolicy", &PointGraph::setComputePolicy);
    graph.addFunc("getComputePolicy", &PointGraph::getComputePolicy);
    graph.addFunc("setComputeMinimumPoints", &PointGraph::setComputeMinimumPoints);
    graph.addFunc("getComputeMinimumPoints", &PointGraph::getComputeMinimumPoints);
    graph.addFunc("requestCancel", &PointGraph::requestCancel);
    graph.addFunc("resetCancellation", &PointGraph::resetCancellation);
    graph.addFunc("wasCancelled", &PointGraph::wasCancelled);
    graph.addFunc("validate", &PointGraph::validate);
    graph.addFunc("getError", &PointGraph::getError);
    graph.addFunc("clearCache", &PointGraph::clearCache);
    graph.addFunc("getRevision", &PointGraph::getRevision);
    graph.addFunc("getExecutionCount", &PointGraph::getExecutionCount);
    graph.addFunc("getCacheHitCount", &PointGraph::getCacheHitCount);
    graph.addFunc("getMetricCount", &PointGraph::getMetricCount);
    graph.addFunc("getMetricNodeId", &PointGraph::getMetricNodeId);
    graph.addFunc("getMetricOutputCount", &PointGraph::getMetricOutputCount);
    graph.addFunc("getMetricMilliseconds", &PointGraph::getMetricMilliseconds);
    graph.addFunc("isMetricCacheHit", &PointGraph::isMetricCacheHit);
    graph.addFunc("getMetricBackend", &PointGraph::getMetricBackend);
    graph.addFunc("getComputeFallbackReason", &PointGraph::getComputeFallbackReason);
    graph.addFunc("getComputeUploadCount", &PointGraph::getComputeUploadCount);
    graph.addFunc("getComputeDispatchCount", &PointGraph::getComputeDispatchCount);
    graph.addFunc("getComputeReadbackCount", &PointGraph::getComputeReadbackCount);
    graph.addFunc("getComputeBufferReuseCount", &PointGraph::getComputeBufferReuseCount);
    graph.addFunc("getComputePeakBufferBytes", &PointGraph::getComputePeakBufferBytes);
    graph.addFunc("getLastFusedTransformCount", &PointGraph::getLastFusedTransformCount);
    graph.addFunc("getMetricMinX", &PointGraph::getMetricMinX);
    graph.addFunc("getMetricMinY", &PointGraph::getMetricMinY);
    graph.addFunc("getMetricMinZ", &PointGraph::getMetricMinZ);
    graph.addFunc("getMetricMaxX", &PointGraph::getMetricMaxX);
    graph.addFunc("getMetricMaxY", &PointGraph::getMetricMaxY);
    graph.addFunc("getMetricMaxZ", &PointGraph::getMetricMaxZ);
    graph.addFunc("getMetricAverageDensity", &PointGraph::getMetricAverageDensity);
    graph.addFunc("getNodeOutput", &PointGraph::getNodeOutput);
    graph.addFunc("debugReport", &PointGraph::debugReport);
    graph.addFunc("serializeDefinition", &PointGraph::serializeDefinition);
    graph.addFunc("deserializeDefinition", &PointGraph::deserializeDefinition);
    graph.addFunc("instantiate", &PointGraph::instantiate);
    graph.addFunc("getOperationCount", [](PointGraph*) { return PointGraph::getOperationCount(); });
    graph.addFunc("getOperationId",
                  [](PointGraph*, int index) { return PointGraph::getOperationId(index); });
    graph.addFunc("getOperationInputCount", [](PointGraph*, const std::string& operation) {
        return PointGraph::getOperationInputCount(operation);
    });
    graph.addFunc("getOperationParamCount", [](PointGraph*, const std::string& operation) {
        return PointGraph::getOperationParamCount(operation);
    });
    graph.addFunc("getOperationParamKey",
                  [](PointGraph*, const std::string& operation, int index) {
                      return PointGraph::getOperationParamKey(operation, index);
                  });
    graph.addFunc("getOperationParamKind",
                  [](PointGraph*, const std::string& operation, int index) {
                      return PointGraph::getOperationParamKind(operation, index);
                  });
    graph.addFunc("getOperationParamDefault",
                  [](PointGraph*, const std::string& operation, int index) {
                      return PointGraph::getOperationParamDefault(operation, index);
                  });
}

}  // namespace eve::procgen
