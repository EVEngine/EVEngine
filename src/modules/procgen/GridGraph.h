#pragma once

#include "common/BorrowedRef.h"
#include "procgen/Grid2D.h"
#include "procgen/PointSet.h"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

namespace eve::procgen {

class PointGraph;

/** @brief Strong runtime value passed between typed GridGraph ports. */
using GridGraphValue = std::variant<Grid2D, PointSet>;

/** @brief Declared output kind of a GridGraph operation. */
enum class GridGraphValueType : std::uint8_t { Grid, PointSet };

/**
 * @brief Deterministic data-flow graph for grid generation and explicit grid-to-point composition.
 *
 * Grid nodes own dense intermediate values rather than ECS entities. Mutations invalidate the
 * affected node and its downstream caches. `point.subgraph` is the first typed cross-graph bridge:
 * it consumes a PointSet and executes an independently owned PointGraph instance.
 *
 * @thread Graph-owning thread only.
 * @reentrant Not reentrant for one graph instance.
 */
class GridGraph {
public:
    /** @brief Add a reflected operation under a stable, non-empty node id. */
    [[nodiscard]] Result<void> addNode(std::string id, std::string operation);
    /** @brief Connect one node output to a typed input slot. */
    [[nodiscard]] Result<void> connect(std::string_view fromId, std::string_view toId, int inputIndex = 0);
    /** @brief Assign an owning copy to a `grid.input` node. */
    [[nodiscard]] Result<void> setNodeGrid(std::string_view id, const Grid2D& grid);
    /** @brief Set one integer parameter declared by the node operation. */
    [[nodiscard]] Result<void> setNodeInt(std::string_view id, std::string key, int value);
    /** @brief Set one floating-point parameter declared by the node operation. */
    [[nodiscard]] Result<void> setNodeFloat(std::string_view id, std::string key, float value);
    /** @brief Set one string parameter declared by the node operation. */
    [[nodiscard]] Result<void> setNodeString(std::string_view id, std::string key, std::string value);
    /**
     * @brief Bind an owning PointGraph copy to a `point.subgraph` bridge.
     * @param inputNode Nested `input` node receiving the upstream PointSet.
     * @param outputNode Nested output node to evaluate.
     */
    [[nodiscard]] Result<void> setNodePointSubgraph(std::string_view id, const PointGraph& graph, std::string inputNode,
                                                    std::string outputNode);

    /**
     * @brief Validate and evaluate one output into an independent owning value.
     * @return Grid2D or PointSet according to the selected node's reflected output type.
     * @cost Linear in visited cells for local grid operations; nested PointGraph cost is additive.
     */
    [[nodiscard]] Result<GridGraphValue> execute(std::string_view outputId);
    /** @brief Validate topology, required inputs and cross-graph bindings without evaluating values. */
    [[nodiscard]] Result<void> validate(std::string_view outputId) const;
    /** @brief Remove all cached node values while retaining topology and parameters. */
    void clearCache();
    /** @brief Monotonic authoring revision; successful mutations increment it. */
    [[nodiscard]] std::uint64_t revision() const noexcept { return revision_; }
    /** @brief Serialize topology and scalar parameters using the versioned EVPCG grid schema. */
    [[nodiscard]] std::string serializeDefinition() const;
    /** @brief Atomically replace this graph from a versioned definition. */
    [[nodiscard]] Result<void> deserializeDefinition(std::string_view definition);

    /** @brief Return the number of reflected operations. */
    [[nodiscard]] static int operationCount();
    /** @brief Return a reflected operation id, or empty for an invalid index. */
    [[nodiscard]] static std::string operationId(int index);
    /** @brief Return a reflected operation's output kind. */
    [[nodiscard]] static Result<GridGraphValueType> operationOutputType(std::string_view operation);

private:
    struct Node {
        std::string                                  id;
        std::string                                  operation;
        std::string                                  inputs[3];
        std::unordered_map<std::string, int>         ints;
        std::unordered_map<std::string, float>       floats;
        std::unordered_map<std::string, std::string> strings;
        Grid2D                                       inputGrid;
        bool                                         hasInputGrid = false;
        std::shared_ptr<PointGraph>                  pointSubgraph;
        std::string                                  pointInputNode;
        std::string                                  pointOutputNode;
        GridGraphValue                               cache;
        bool                                         cacheValid = false;
    };

    [[nodiscard]] ResultRef<const GridGraphValue> evaluate(std::string_view                      id,
                                                           std::unordered_map<std::string, int>& states);
    void                                          invalidateFrom(std::string_view id);

    std::unordered_map<std::string, Node> nodes_;
    std::vector<std::string>              order_;
    std::uint64_t                         revision_ = 0;
};

}  // namespace eve::procgen
