#pragma once

#include "editing/EditingProtocol.h"

#include <map>
#include <string>
#include <vector>

namespace eve::editing {

/** @brief Direction of a typed graph pin. */
enum class GraphPinDirection { Input, Output };

/** @brief Stable typed pin owned by a graph node. */
struct GraphPinRecord {
    GraphPinId        id;
    GraphNodeId       node;
    std::string       type;
    GraphPinDirection direction = GraphPinDirection::Input;
};

/** @brief Generic graph node independent of its domain. */
struct GraphNodeRecord {
    GraphNodeId                 id;
    std::string                 type;
    Value                       properties;
    std::vector<GraphPinRecord> pins;
};

/** @brief Directed connection between an output and input pin. */
struct GraphEdgeRecord {
    StableId   id;
    GraphPinId from;
    GraphPinId to;
};

/** @brief Immutable graph document value sent to a domain compiler. */
struct GraphDocumentData {
    std::string                  domain;
    std::uint32_t                schemaVersion = 1;
    Revision                     revision      = 0;
    std::vector<GraphNodeRecord> nodes;
    std::vector<GraphEdgeRecord> edges;
    Value                        parameters;
};

/** @brief Result of domain-specific connection validation. */
struct GraphConnectionDecision {
    bool                    allowed = false;
    std::vector<Diagnostic> diagnostics;
};

/** @brief Generic mutable graph model with one authoritative revision clock. */
class GraphDocument {
public:
    [[nodiscard]] Result<void> createNode(GraphNodeRecord node);
    [[nodiscard]] Result<void> deleteNode(const GraphNodeId& node);
    [[nodiscard]] Result<void> connect(GraphEdgeRecord edge, const GraphConnectionDecision& decision);
    [[nodiscard]] Result<void> disconnect(const StableId& edge);
    [[nodiscard]] Result<void> setNodeProperties(const GraphNodeId& node, Value properties);
    [[nodiscard]] Result<void> setParameters(Value parameters);
    [[nodiscard]] GraphDocumentData snapshot(std::string domain) const;
    [[nodiscard]] Revision revision() const { return revision_; }
    /** @return Borrowed non-owning pin pointer, or null. @lifetime Valid until this document is mutated or destroyed. */
    [[nodiscard]] const GraphPinRecord* findPin(const GraphPinId& pin) const;

private:
    std::map<GraphNodeId, GraphNodeRecord> nodes_;
    std::map<StableId, GraphEdgeRecord>    edges_;
    Value                                  parameters_;
    Revision                               revision_ = 0;
};

/** @brief Domain-owned typed connection policy. */
class IGraphDomainProvider {
public:
    virtual ~IGraphDomainProvider() = default;
    [[nodiscard]] virtual std::string domain() const = 0;
    [[nodiscard]] virtual GraphConnectionDecision canConnect(
        const GraphPinRecord& from, const GraphPinRecord& to) const = 0;
};

}  // namespace eve::editing
