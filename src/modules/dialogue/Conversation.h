#pragma once

#include "common/Result.h"
#include "common/StateValue.h"
#include "dialogue/DialogueState.h"

#include <functional>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace eve::dialogue {

/**
 * @brief One outgoing edge in a conversation graph.
 *
 * `first` and `second` retain the legacy label/expression and destination
 * layout. A non-null `condition` is evaluated by the shared decision system;
 * it is not interpreted by a dialogue-local condition language.
 */
struct ConversationRoute {
    int sourceLine = 0;
    int sourceColumn = 0;
    /** @brief Stable route identifier, independent from presentation text. */
    std::string id;
    /** @brief Source-language display text. */
    std::string text;
    /** @brief Optional localization key for display text. */
    std::string i18nKey;
    /** @brief Optional legacy expression evaluated as a checked boolean. */
    std::string expression;
    std::string first;
    std::string second;
    eve::Value  condition;
    /** @brief Optional money/reputation charge committed before entering the target. */
    PaymentSpec payment;
    /** @brief Optional authoritative mutations committed with this route. */
    std::vector<eve::StateMutation> stateMutations;

    /** @brief Construct an unconditional or legacy-expression route. */
    ConversationRoute(std::string label = {}, std::string target = {}, eve::Value routeCondition = {})
        : id(label), first(std::move(label)), second(std::move(target)), condition(std::move(routeCondition)) {}
};

/** @brief Short spelling used by authoring and gameplay integration code. */
using Route = ConversationRoute;

/** @brief Immutable, parameterized conversation definition. */
struct ConversationAsset {
    /** @brief Typed invocation parameter declared by the content schema. */
    struct Parameter {
        enum class Type { Any, String, Int, Float, Bool };
        std::string name;
        Type type = Type::Any;
        bool required = true;
        StateValue defaultValue = StateValue::null();
        int sourceLine = 0;
        int sourceColumn = 0;
        Parameter() = default;
        Parameter(std::string parameterName) : name(std::move(parameterName)) {}
    };
    /** @brief A serializable step in a conversation. */
    struct Node {
        enum class Kind { Line, Branch, Choice, Call, Command, Wait, End };

        std::string id;
        Kind kind = Kind::End;
        std::string next;
        std::string speaker;
        std::string text;
        std::string pool;
        std::string i18nKey;
        std::string voice;
        std::string expression;
        std::string target;
        std::string returnNode;
        StateValue arguments = StateValue::object();
        std::vector<ConversationRoute> routes;
        CommandRequestKind             commandKind = CommandRequestKind::Operation;
        /** @brief Optional money/reputation charge for a command node. */
        PaymentSpec payment;
        /** @brief Optional authoritative mutations committed with a command. */
        std::vector<eve::StateMutation> stateMutations;
        int sourceLine = 0;
        int sourceColumn = 0;
    };

    std::string id;
    int version = 1;
    std::string entry;
    std::vector<Parameter> parameters;
    std::vector<Node> nodes;
    int sourceLine = 0;
    int sourceColumn = 0;

    /** @brief Find a node by its stable identifier. */
    const Node* findNode(const std::string& nodeId) const;
    /** @brief Validate stable IDs, entry point, and node references. */
    [[nodiscard]] eve::Result<void> validate() const;
};

/** @brief Explicit conversation executor whose suspension points are node IDs. */
class ConversationRunner {
public:
    static constexpr std::string_view SaveSchema  = "eve.dialogue.runner";
    static constexpr std::int64_t     SaveVersion = 2;

    using AssetResolver = std::function<const ConversationAsset*(const std::string&)>;
    using ExpressionEvaluator =
        std::function<eve::Result<StateValue>(const std::string&, const StateValue&, const StateValue&)>;
    struct CommandResult {
        enum class Status { Completed, Blocked, Failed };
        Status status = Status::Completed;
        StateValue value = StateValue::null();
        std::string error;
    };
    struct Event {
        enum class Kind { Started, NodeEntered, Line, Choice, Command, Ended, Failed };
        Kind kind = Kind::NodeEntered;
        std::string assetId;
        std::string nodeId;
        std::string name;
    };
    using CommandHandler =
        std::function<CommandResult(const StateValue&, const StateValue&, const StateValue&)>;
    using ConditionEvaluator       = std::function<eve::decision::ConditionResult(const eve::Value&)>;
    using CommandRequestDispatcher = std::function<CommandResponse(const CommandRequest&)>;
    using EventSink = std::function<void(const Event&)>;

    /** @brief Start an asset with validated serializable parameter bindings. */
    [[nodiscard]] eve::Result<void> startChecked(const ConversationAsset* asset, StateValue bindings);
    /** @brief Execute non-blocking nodes until a line, choice, wait, or end is reached. */
    [[nodiscard]] eve::Result<void> runUntilBlockedChecked();
    /** @brief Continue from the current blocking node. */
    [[nodiscard]] eve::Result<void> advanceChecked();
    /** @brief Select a route on the current choice node. */
    [[nodiscard]] eve::Result<void> selectChecked(const std::string& routeId);
    /**
     * @brief Select a route from an already prepared Dialogue transaction.
     * @param routeId Stable route identifier on the current choice node.
     * @return Applied on success, or a stable diagnostic without changing the
     *         runner cursor when selection cannot be completed.
     * @remarks This low-level path is reserved for the DialogueFlow transaction
     *          participant. It runs on the runner's owner thread and does not
     *          retain routeId or invoke callbacks while holding a lock.
     */
    [[nodiscard]] eve::Result<void> selectRouteForTransaction(const std::string& routeId);
    /**
     * @brief Resume an asynchronous command node with its serialized result.
     * @param result Owned result value; it is copied or moved into runner locals.
     * @return Applied on success, or a stable diagnostic while preserving the
     *         suspended runner state on failure.
     */
    [[nodiscard]] eve::Result<void> resumeCommand(const std::string& requestId, StateValue result);
    /**
     * @brief Resume an asynchronous request command with a canonical result.
     * @param result Owned canonical result value converted to dialogue state.
     * @return Applied on success, or a stable diagnostic while preserving the
     *         suspended runner state on failure.
     */
    [[nodiscard]] eve::Result<void> resumeCommand(const std::string& requestId, eve::Value result);
    /** @brief Stop and clear the active instance. */
    void stop();
    /** @brief Capture the complete cursor, locals, bindings, pending command and call stack. */
    [[nodiscard]] eve::Result<StateValue> captureStateChecked() const;
    /** @brief Restore a captured runner using the configured asset resolver. */
    [[nodiscard]] eve::Result<void> restoreStateChecked(const StateValue& in);

    void setAssetResolver(AssetResolver resolver) { assetResolver_ = std::move(resolver); }
    void setExpressionEvaluator(ExpressionEvaluator evaluator) {
        expressionEvaluator_ = std::move(evaluator);
    }
    /** @brief Evaluate structured route conditions through decision::Condition. */
    void setConditionEvaluator(ConditionEvaluator evaluator) { conditionEvaluator_ = std::move(evaluator); }
    /** @brief Register a cross-module command without introducing module includes. */
    void registerCommand(const std::string& name, CommandHandler handler);
    void unregisterCommand(const std::string& name);
    /** @brief Register a request-only command owned by an operation/gameplay domain. */
    void registerCommandRequest(const std::string& name, CommandRequestHandler handler);
    /** @brief Remove a request-only command handler. */
    void unregisterCommandRequest(const std::string& name);
    /**
     * @brief Install one cross-module dispatcher used when no name-specific request handler exists.
     *
     * The dispatcher is retained as a callable and runs synchronously on the
     * runner's thread. It must not retain the request or re-enter this runner.
     * DialogueFlow uses this hook to configure operation and gameplay-action
     * handlers once for its whole facade.
     */
    void setCommandRequestDispatcher(CommandRequestDispatcher dispatcher) {
        commandRequestDispatcher_ = std::move(dispatcher);
    }
    /** @brief Remove the facade-level request dispatcher. */
    void clearCommandRequestDispatcher() { commandRequestDispatcher_ = {}; }
    void setEventSink(EventSink sink) { eventSink_ = std::move(sink); }

    bool isActive() const { return asset_ != nullptr; }
    bool isBlocked() const { return blocked_; }
    const ConversationAsset* asset() const { return asset_; }
    const ConversationAsset::Node* currentNode() const;
    const std::string& currentNodeId() const { return nodeId_; }
    const StateValue& bindings() const { return bindings_; }
    const StateValue& locals() const { return locals_; }
    StateValue& locals() { return locals_; }
    /**
     * @brief Return the last structured condition explanation, if evaluated.
     * @return A nullable borrowed pointer into this runner's cached result.
     * @ownership Borrowed; the runner owns the optional result.
     * @lifetime Valid until the next condition evaluation, stop, or runner
     *           destruction.
     * @thread Affine to the runner's owner thread.
     */
    const eve::decision::ConditionResult* lastConditionResult() const {
        return lastConditionResult_ ? &*lastConditionResult_ : nullptr;
    }
    /**
     * @brief Return the last request emitted by a command node, if any.
     * @return A nullable borrowed pointer into this runner's cached request.
     * @ownership Borrowed; the runner owns the optional request.
     * @lifetime Valid until another request is emitted, stop, or runner
     *           destruction.
     * @thread Affine to the runner's owner thread.
     */
    const CommandRequest* lastCommandRequest() const { return lastCommandRequest_ ? &*lastCommandRequest_ : nullptr; }
    /** @brief Return the stable id of the currently suspended command, or an empty string. */
    const std::string& pendingCommandRequestId() const noexcept { return pendingCommandRequestId_; }

private:
    struct Frame {
        const ConversationAsset* asset = nullptr;
        std::string returnNode;
        StateValue bindings = StateValue::object();
        StateValue locals = StateValue::object();
    };

    bool fail(std::string* error, const std::string& message) const;
    bool startImpl(const ConversationAsset* asset, StateValue bindings, std::string* error);
    bool runUntilBlockedImpl(std::string* error);
    bool advanceImpl(std::string* error);
    bool selectImpl(const std::string& routeId, std::string* error);
    bool captureStateImpl(StateValue& out) const;
    bool restoreStateImpl(const StateValue& in, std::string* error);
    bool enter(const std::string& nodeId, std::string* error);
    std::string evaluateRoute(const ConversationAsset::Node& node, std::string* error);
    void emit(Event::Kind kind, const ConversationAsset::Node* node = nullptr,
              const std::string& name = {}) const;

    const ConversationAsset* asset_ = nullptr;
    std::string nodeId_;
    StateValue bindings_ = StateValue::object();
    StateValue locals_ = StateValue::object();
    bool blocked_ = false;
    std::vector<Frame> callStack_;
    AssetResolver assetResolver_;
    ExpressionEvaluator expressionEvaluator_;
    ConditionEvaluator                                     conditionEvaluator_;
    std::unordered_map<std::string, CommandHandler> commandHandlers_;
    std::unordered_map<std::string, CommandRequestHandler> commandRequestHandlers_;
    CommandRequestDispatcher                               commandRequestDispatcher_;
    EventSink eventSink_;
    bool waitingCommand_ = false;
    std::uint64_t commandSequence_ = 1;
    std::string pendingCommandRequestId_;
    std::optional<eve::decision::ConditionResult>          lastConditionResult_;
    std::optional<CommandRequest>                          lastCommandRequest_;
};

}  // namespace eve::dialogue
