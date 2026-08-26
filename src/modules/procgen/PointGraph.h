#pragma once

#include "procgen/PointSet.h"
#include "procgen/SpatialData.h"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace eve::procgen {

class BiomeRules;
class ShapeGrammar;

/** @brief Per-node execution information from the most recent graph run. */
struct PointGraphNodeMetric {
    std::string id;
    int         outputCount  = 0;
    float       milliseconds = 0.f;
    bool        cacheHit     = false;
    float       minX           = 0.f;
    float       minY           = 0.f;
    float       minZ           = 0.f;
    float       maxX           = 0.f;
    float       maxY           = 0.f;
    float       maxZ           = 0.f;
    float       averageDensity = 0.f;
};

/**
 * @brief A data-flow graph for attributed procedural points.
 *
 * Nodes are addressed by stable string ids and evaluated lazily from a chosen
 * output. Graph mutations invalidate cached results. Execution rejects missing
 * inputs and cycles, retains inspectable node outputs, and supports nested
 * graphs through the `subgraph` operation.
 */
class PointGraph {
public:
    /** @brief Add a node using one of the operations returned by operationAt(). */
    bool addNode(const std::string& id, const std::string& operation);
    /** @brief Remove a node and every edge that references it. */
    bool removeNode(const std::string& id);
    bool hasNode(const std::string& id) const;
    int  getNodeCount() const;
    std::string getNodeId(int index) const;
    std::string getNodeOperation(const std::string& id) const;

    /** @brief Connect an output to input slot 0 or 1. Replaces that input connection. */
    bool connect(const std::string& fromId, const std::string& toId, int inputIndex = 0);
    /** @brief Remove one input connection. */
    bool disconnect(const std::string& toId, int inputIndex = 0);
    std::string getInputNode(const std::string& nodeId, int inputIndex) const;

    /** @brief Assign a copied PointSet to an `input` node. */
    bool setNodePoints(const std::string& id, PointSet* points);
    /** @brief Assign copied spatial data to spatial.sample or spatial.filter. */
    bool setNodeSpatial(const std::string& id, SpatialData* spatial);
    /** @brief Assign copied biome rules to a `biome.generate` node. */
    bool setNodeBiomeRules(const std::string& id, BiomeRules* rules);
    /** @brief Assign a copied shape grammar to a `grammar.generate` node. */
    bool setNodeShapeGrammar(const std::string& id, ShapeGrammar* grammar);
    bool setNodeFloat(const std::string& id, const std::string& key, float value);
    bool setNodeInt(const std::string& id, const std::string& key, int value);
    bool setNodeString(const std::string& id, const std::string& key, const std::string& value);
    /** @brief Expose one reflected node parameter for per-instance overrides. */
    bool exposeParameter(const std::string& name, const std::string& nodeId,
                         const std::string& key);
    /** @brief Return the number of exposed graph parameters. */
    int         getParameterCount() const;
    /** @brief Return an exposed parameter name by stable insertion index. */
    std::string getParameterName(int index) const;
    /** @brief Return the reflected kind of an exposed parameter. */
    std::string getParameterKind(const std::string& name) const;
    /** @brief Report whether an instance override is active. */
    bool        hasParameterOverride(const std::string& name) const;
    /** @brief Override an exposed floating-point parameter for this graph instance. */
    bool        setParameterFloat(const std::string& name, float value);
    /** @brief Override an exposed integer or Boolean parameter for this graph instance. */
    bool        setParameterInt(const std::string& name, int value);
    /** @brief Override an exposed string parameter for this graph instance. */
    bool        setParameterString(const std::string& name, const std::string& value);
    /** @brief Return the effective floating-point parameter value. */
    float       getParameterFloat(const std::string& name, float fallback) const;
    /** @brief Return the effective integer or Boolean parameter value. */
    int         getParameterInt(const std::string& name, int fallback) const;
    /** @brief Return the effective string parameter value. */
    std::string getParameterString(const std::string& name, const std::string& fallback) const;
    /** @brief Remove an instance override and restore the asset default. */
    bool        clearParameterOverride(const std::string& name);
    /**
     * @brief Assign an immutable nested graph.
     * @param inputNode Nested `input` node receiving this node's first input.
     * @param outputNode Nested node returned by the subgraph.
     */
    bool setNodeSubgraph(const std::string& id, PointGraph* graph, const std::string& inputNode,
                         const std::string& outputNode);

    /** @brief Evaluate one node and return a caller-owned result, or nullptr on failure. */
    PointSet* execute(const std::string& outputId);
    /** @brief Limit uncached nodes evaluated by one execute call; zero disables the limit. */
    void setExecutionNodeBudget(int nodes);
    int  getExecutionNodeBudget() const;
    /** @brief Limit points produced by any node; zero disables the limit. */
    void setMaxNodeOutputPoints(int points);
    int  getMaxNodeOutputPoints() const;
    /** @brief Cancel subsequent execution until resetCancellation is called. */
    void requestCancel();
    void resetCancellation();
    bool wasCancelled() const;
    /** @brief Validate ids, operations, required inputs and graph acyclicity. */
    bool validate();
    std::string getError() const;
    void        clearCache();
    /** @brief Monotonic topology/parameter revision used by asset and preview caches. */
    uint64_t    getRevision() const;
    int         getExecutionCount() const;
    int         getCacheHitCount() const;

    int         getMetricCount() const;
    std::string getMetricNodeId(int index) const;
    int         getMetricOutputCount(int index) const;
    float       getMetricMilliseconds(int index) const;
    bool        isMetricCacheHit(int index) const;
    /** @brief Return cached output bounds for editor/debug visualization. */
    float getMetricMinX(int index) const;
    /** @brief Return the cached minimum output Y coordinate. */
    float getMetricMinY(int index) const;
    /** @brief Return the cached minimum output Z coordinate. */
    float getMetricMinZ(int index) const;
    /** @brief Return the cached maximum output X coordinate. */
    float getMetricMaxX(int index) const;
    /** @brief Return the cached maximum output Y coordinate. */
    float getMetricMaxY(int index) const;
    /** @brief Return the cached maximum output Z coordinate. */
    float getMetricMaxZ(int index) const;
    /** @brief Return the mean point density of one node output. */
    float getMetricAverageDensity(int index) const;
    /** @brief Copy the cached/debug output of a node after execution. */
    PointSet* getNodeOutput(const std::string& id) const;
    std::string debugReport() const;
    /**
     * @brief Serialize graph topology, parameters and nested graphs.
     *
     * Runtime PointSet, SpatialData, BiomeRules and ShapeGrammar values are
     * external slots and are not embedded; callers rebind them after loading.
     */
    std::string serializeDefinition() const;
    /** @brief Transactionally replace this graph from serializeDefinition output. */
    bool deserializeDefinition(const std::string& definition);
    /** @brief Create an independent runtime instance without external inputs or overrides. */
    PointGraph* instantiate() const;

    static int         getOperationCount();
    static std::string getOperationId(int index);
    static int         getOperationInputCount(const std::string& operation);
    static int         getOperationParamCount(const std::string& operation);
    static std::string getOperationParamKey(const std::string& operation, int index);
    static std::string getOperationParamKind(const std::string& operation, int index);
    static std::string getOperationParamDefault(const std::string& operation, int index);

private:
    struct Node {
        std::string id;
        std::string operation;
        std::string inputs[2];
        std::unordered_map<std::string, float>       floats;
        std::unordered_map<std::string, int>         ints;
        std::unordered_map<std::string, std::string> strings;
        PointSet                                    points;
        bool                                        hasPoints = false;
        std::shared_ptr<SpatialData>                 spatial;
        std::shared_ptr<BiomeRules>                  biomeRules;
        std::shared_ptr<ShapeGrammar>                shapeGrammar;
        std::shared_ptr<PointGraph>                  subgraph;
        std::string                                  subgraphInput;
        std::string                                  subgraphOutput;
        PointSet                                    cache;
        PointGraphNodeMetric                        cacheMetric;
        bool                                        cacheValid = false;
    };
    struct ParameterBinding {
        std::string nodeId;
        std::string key;
        std::string kind;
    };

    const PointSet* evaluate(const std::string& id, std::unordered_map<std::string, int>& states);
    bool            validateNode(const std::string& id,
                                 std::unordered_map<std::string, int>& states);
    void            invalidate();
    void            invalidateFrom(const std::string& id);
    float           floatValue(const Node& node, const std::string& key, float fallback) const;
    int             intValue(const Node& node, const std::string& key, int fallback) const;
    std::string     stringValue(const Node& node, const std::string& key,
                                const std::string& fallback = {}) const;

    std::unordered_map<std::string, Node>             nodes_;
    std::vector<std::string>                          nodeOrder_;
    std::unordered_map<std::string, ParameterBinding> parameters_;
    std::vector<std::string>                          parameterOrder_;
    std::unordered_map<std::string, float>            floatOverrides_;
    std::unordered_map<std::string, int>              intOverrides_;
    std::unordered_map<std::string, std::string>      stringOverrides_;
    std::vector<PointGraphNodeMetric>                 metrics_;
    std::string                                       error_;
    int                                               executionCount_      = 0;
    int                                               cacheHitCount_       = 0;
    uint64_t                                          revision_            = 0;
    int                                               executionNodeBudget_ = 0;
    int                                               maxNodeOutputPoints_ = 0;
    int                                               evaluatedNodes_      = 0;
    bool                                              cancelRequested_     = false;
    bool                                              lastCancelled_       = false;
};

}  // namespace eve::procgen
