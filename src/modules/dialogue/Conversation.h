#pragma once

#include "common/StateValue.h"

#include <functional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace eve::dialogue {

/** @brief Immutable, parameterized conversation definition. */
struct ConversationAsset {
    /** @brief A serializable step in a conversation. */
    struct Node {
        enum class Kind { Line, Branch, Choice, Call, Command, Wait, End };

        std::string id;
        Kind kind = Kind::End;
        std::string next;
        std::string speaker;
        std::string text;
        std::string pool;
        std::string expression;
        std::string target;
        std::string returnNode;
        StateValue arguments = StateValue::object();
        std::vector<std::pair<std::string, std::string>> routes;
    };

    std::string id;
    int version = 1;
    std::string entry;
    std::vector<std::string> parameters;
    std::vector<Node> nodes;

    /** @brief Find a node by its stable identifier. */
    const Node* findNode(const std::string& nodeId) const;
    /** @brief Validate stable IDs, entry point, and node references. */
    bool validate(std::string* error = nullptr) const;
};

/** @brief Explicit conversation executor whose suspension points are node IDs. */
class ConversationRunner {
public:
    using AssetResolver = std::function<const ConversationAsset*(const std::string&)>;
    using ExpressionEvaluator =
        std::function<StateValue(const std::string&, const StateValue&, const StateValue&)>;

    /** @brief Start an asset with serializable parameter bindings. */
    bool start(const ConversationAsset* asset, StateValue bindings, std::string* error = nullptr);
    /** @brief Execute non-blocking nodes until a line, choice, wait, or end is reached. */
    bool runUntilBlocked(std::string* error = nullptr);
    /** @brief Continue from the current blocking node. */
    bool advance(std::string* error = nullptr);
    /** @brief Select a route on the current choice node. */
    bool select(const std::string& routeId, std::string* error = nullptr);
    /** @brief Stop and clear the active instance. */
    void stop();

    void setAssetResolver(AssetResolver resolver) { assetResolver_ = std::move(resolver); }
    void setExpressionEvaluator(ExpressionEvaluator evaluator) {
        expressionEvaluator_ = std::move(evaluator);
    }

    bool isActive() const { return asset_ != nullptr; }
    bool isBlocked() const { return blocked_; }
    const ConversationAsset* asset() const { return asset_; }
    const ConversationAsset::Node* currentNode() const;
    const std::string& currentNodeId() const { return nodeId_; }
    const StateValue& bindings() const { return bindings_; }
    const StateValue& locals() const { return locals_; }
    StateValue& locals() { return locals_; }

private:
    bool fail(std::string* error, const std::string& message) const;
    bool enter(const std::string& nodeId, std::string* error);
    std::string evaluateRoute(const ConversationAsset::Node& node, std::string* error) const;

    const ConversationAsset* asset_ = nullptr;
    std::string nodeId_;
    StateValue bindings_ = StateValue::object();
    StateValue locals_ = StateValue::object();
    bool blocked_ = false;
    AssetResolver assetResolver_;
    ExpressionEvaluator expressionEvaluator_;
};

}  // namespace eve::dialogue
