#pragma once

#include "common/Result.h"
#include "procgen/Grid2D.h"
#include "procgen/MeshBuild.h"
#include "procgen/PointSet.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

namespace eve::procgen {

/** @brief Strong values accepted by MeshGraph ports. */
using MeshGraphValue = std::variant<Grid2D, PointSet, MeshBuild>;

/** @brief Typed procedural mesh graph composing grid, point and topology domains. */
class EVENGINE_API_DOMAINS MeshGraph {
public:
    /** @brief Adds node. */
    [[nodiscard]] Result<void> addNode(std::string id, std::string operation);
    /** @brief Connect. */
    [[nodiscard]] Result<void> connect(std::string_view fromId, std::string_view toId, int inputIndex = 0);
    /** @brief Sets the node grid. */
    [[nodiscard]] Result<void> setNodeGrid(std::string_view id, const Grid2D& value);
    /** @brief Sets the node points. */
    [[nodiscard]] Result<void> setNodePoints(std::string_view id, const PointSet& value);
    /** @brief Sets the node mesh. */
    [[nodiscard]] Result<void> setNodeMesh(std::string_view id, const MeshBuild& value);
    /** @brief Sets the node float. */
    [[nodiscard]] Result<void> setNodeFloat(std::string_view id, std::string key, float value);
    /** @brief Sets the node string. */
    [[nodiscard]] Result<void> setNodeString(std::string_view id, std::string key, std::string value);
    /** @cost Linear in emitted vertices and indices; point instancing multiplies source mesh cost by point count. */
    /** @brief Execute. */
    [[nodiscard]] Result<MeshBuild> execute(std::string_view outputId);
    /** @brief Validate. */
    [[nodiscard]] Result<void>      validate(std::string_view outputId) const;
    /** @brief Clears cache. */
    void                            clearCache();
    /** @brief Revision. */
    [[nodiscard]] std::uint64_t     revision() const noexcept { return revision_; }
    /** @brief Serialize topology and scalar parameters using the versioned EVPCG mesh schema. */
    [[nodiscard]] std::string serializeDefinition() const;
    /** @brief Atomically replace this graph from a versioned definition. Runtime input values are not persisted. */
    [[nodiscard]] Result<void> deserializeDefinition(std::string_view definition);

private:
    struct Node {
        std::string                                  id;
        std::string                                  operation;
        std::string                                  inputs[2];
        std::unordered_map<std::string, float>       floats;
        std::unordered_map<std::string, std::string> strings;
        MeshGraphValue                               value;
        bool                                         hasValue = false;
        MeshGraphValue                               cache;
        bool                                         cacheValid = false;
    };

    [[nodiscard]] Result<MeshGraphValue> evaluate(std::string_view id, std::unordered_map<std::string, int>& states);
    void                                 invalidateFrom(std::string_view id);

    std::unordered_map<std::string, Node> nodes_;
    std::vector<std::string>              order_;
    std::uint64_t                         revision_ = 0;
};

}  // namespace eve::procgen
