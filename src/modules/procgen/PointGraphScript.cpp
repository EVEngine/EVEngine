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
    graph.addFunc("setNodeFloat", &PointGraph::setNodeFloat);
    graph.addFunc("setNodeInt", &PointGraph::setNodeInt);
    graph.addFunc("setNodeString", &PointGraph::setNodeString);
    graph.addFunc("setNodeSubgraph", &PointGraph::setNodeSubgraph);
    graph.addFunc("execute", &PointGraph::execute);
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
    graph.addFunc("getNodeOutput", &PointGraph::getNodeOutput);
    graph.addFunc("debugReport", &PointGraph::debugReport);
    graph.addFunc("serializeDefinition", &PointGraph::serializeDefinition);
    graph.addFunc("deserializeDefinition", &PointGraph::deserializeDefinition);
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
