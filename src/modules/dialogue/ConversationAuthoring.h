#pragma once

#include "dialogue/ConversationCompiler.h"

#include <string>
#include <vector>

namespace eve::dialogue {

/** @brief Mutable, UI-neutral conversation document for custom editor composition. */
class ConversationDocument {
public:
    /** @brief Create an empty document with one end node. */
    explicit ConversationDocument(std::string id = {});
    /** @brief Create an editable copy of a runtime asset. */
    explicit ConversationDocument(ConversationAsset asset);

    /** @brief Return the editable runtime asset. */
    const ConversationAsset& asset() const { return asset_; }
    /** @brief Return the stable conversation identifier. */
    const std::string& getId() const { return asset_.id; }
    /** @brief Rename the document identifier. */
    bool setId(const std::string& id);
    /** @brief Return the asset schema version. */
    int getVersion() const { return asset_.version; }
    /** @brief Set the asset schema version to a positive value. */
    bool setVersion(int version);
    /** @brief Return the entry node identifier. */
    const std::string& getEntry() const { return asset_.entry; }
    /** @brief Set the entry node identifier. */
    bool setEntry(const std::string& nodeId);

    /** @brief Return the number of declared parameters. */
    int getParameterCount() const;
    /** @brief Return a declared parameter by insertion order. */
    std::string getParameter(int index) const;
    /** @brief Add a unique parameter. */
    bool addParameter(const std::string& name);
    /** @brief Remove a parameter. */
    bool removeParameter(const std::string& name);

    /** @brief Return the number of graph nodes. */
    int getNodeCount() const;
    /** @brief Return a stable node identifier by insertion order. */
    std::string getNodeId(int index) const;
    /** @brief Test whether a node exists. */
    bool hasNode(const std::string& nodeId) const;
    /** @brief Add a node of kind line, branch, choice, call, command, wait, or end. */
    bool addNode(const std::string& nodeId, const std::string& kind);
    /** @brief Remove a node and references to it. */
    bool removeNode(const std::string& nodeId);
    /** @brief Rename a node and rewrite graph references. */
    bool renameNode(const std::string& oldId, const std::string& newId);
    /** @brief Return a node kind name. */
    std::string getNodeKind(const std::string& nodeId) const;
    /** @brief Change a node kind while preserving compatible fields. */
    bool setNodeKind(const std::string& nodeId, const std::string& kind);

    /** @brief Return the reflected inspector field count for a node kind. */
    int getFieldCount(const std::string& nodeId) const;
    /** @brief Return a reflected field key. */
    std::string getFieldName(const std::string& nodeId, int index) const;
    /** @brief Return a field UI hint such as string, multiline, expression, asset, or json. */
    std::string getFieldKind(const std::string& nodeId, int index) const;
    /** @brief Return a scalar field value by key. */
    std::string getField(const std::string& nodeId, const std::string& field) const;
    /** @brief Set a scalar field value by key. */
    bool setField(const std::string& nodeId, const std::string& field, const std::string& value);

    /** @brief Return the number of outgoing conditional or choice routes. */
    int getRouteCount(const std::string& nodeId) const;
    /** @brief Return a route label or expression. */
    std::string getRouteLabel(const std::string& nodeId, int index) const;
    /** @brief Return a route destination. */
    std::string getRouteTarget(const std::string& nodeId, int index) const;
    /** @brief Append an outgoing route. */
    bool addRoute(const std::string& nodeId, const std::string& label, const std::string& target);
    /** @brief Replace an outgoing route. */
    bool setRoute(const std::string& nodeId, int index, const std::string& label, const std::string& target);
    /** @brief Remove an outgoing route. */
    bool removeRoute(const std::string& nodeId, int index);

    /** @brief Validate the graph and refresh structured diagnostics. */
    bool validate();
    /** @brief Return the current diagnostic count. */
    int getDiagnosticCount() const;
    /** @brief Return error or warning. */
    std::string getDiagnosticSeverity(int index) const;
    /** @brief Return a diagnostic source path. */
    std::string getDiagnosticPath(int index) const;
    /** @brief Return a diagnostic line, or zero for graph diagnostics. */
    int getDiagnosticLine(int index) const;
    /** @brief Return a diagnostic message. */
    std::string getDiagnosticMessage(int index) const;
private:
    ConversationAsset::Node*       findNode(const std::string& nodeId);
    const ConversationAsset::Node* findNode(const std::string& nodeId) const;
    bool                           fail(const std::string& message);

    ConversationAsset                   asset_;
    std::vector<ConversationDiagnostic> diagnostics_;
    std::string                         failureMessage_;
};

}  // namespace eve::dialogue
