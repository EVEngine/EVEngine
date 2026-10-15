#pragma once
#include "common/Export.h"


/** @file SequenceRuntime.h @brief Cross-frame interpreter for compiled `.dnut` sequences. */

#include "common/Result.h"
#include "common/StateValue.h"
#include "common/Value.h"
#include "dnut_interpreter/SequenceAsset.h"
#include "dnut_interpreter/StepKindRegistry.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace eve::dnut {

/** @brief Outcome of one authored condition, as seen by the language core. */
struct SequenceConditionOutcome {
    bool        passed = false;
    /** @brief Stable machine-readable reason; empty when `passed` is true. */
    std::string reason;
    /** @brief Evaluation failure; non-empty means no route may be selected. */
    std::string error;
};

/** @brief Immutable request emitted by a generic `command` node. */
struct SequenceCommandRequest {
    std::string requestId;
    std::string name;
    eve::Value  arguments = eve::Value::Object{};
    eve::Value  bindings  = eve::Value::Object{};
    eve::Value  locals    = eve::Value::Object{};
    /** @brief Complete owning command payload for domain-specific decoding. */
    eve::Value payload = eve::Value::Object{};
};

/** @brief Structured response returned by a generic command handler. */
struct SequenceCommandResponse {
    /** @brief Status public API. */
    enum class Status : std::uint8_t { Completed, Blocked, Failed };

    Status      status = Status::Completed;
    eve::Value  value;
    std::string error;
};

/** @brief Synchronous owner-thread handler for one command request. */
using SequenceCommandHandler = std::function<SequenceCommandResponse(const SequenceCommandRequest&)>;

/**
 * @brief Caller-owned interpreter over a compiled sequence asset.
 *
 * The runtime owns only the execution cursor: the current asset, node, call
 * stack, parameter bindings and step locals. Persistent progress, completion
 * facts and gameplay state stay with the domain owner that persists them.
 *
 * The runtime never interprets a step payload. Control-flow types
 * (`branch` / `choice` / `call` / `command` / `wait` / `end`) are handled here;
 * every other type is routed through the configured `StepKindRegistry`.
 */
class EVENGINE_API_PLATFORM SequenceRuntime {
public:
    /** @brief Stable cursor save schema retained across the L1 ownership migration. */
    static constexpr std::string_view SaveSchema = "eve.dialogue.runner";
    /** @brief Current cursor save schema version. */
    static constexpr std::int64_t SaveVersion = 2;

    /** @brief Resolves a called asset id to a borrowed, externally owned asset. */
    using AssetResolver = std::function<const SequenceAsset*(const std::string&)>;
    /**
     * @brief Evaluates one authored condition value.
     *
     * Consumers adapt their own condition system (for example
     * `eve::decision::Condition`) to this contract, so the language core does
     * not depend on any domain or decision module.
     */
    using ConditionEvaluator = std::function<SequenceConditionOutcome(const eve::Value&)>;

    /** @brief Observable runtime transition. */
    enum class EventKind : std::uint8_t { Started, NodeEntered, Blocked, Stepped, Resumed, Ended, Failed };

    /** @brief One observable transition; every field is owned by the event. */
    struct Event {
        EventKind   kind = EventKind::NodeEntered;
        std::string assetId;
        std::string nodeId;
        std::string detail;
    };

    /** @brief Single event outlet; invoked synchronously without locks. */
    using EventSink = std::function<void(const Event&)>;

    /**
     * @brief Upper bound on nodes executed by one `runUntilBlocked` call.
     *
     * Authored content can contain a cycle by mistake; the budget turns that
     * into an observable failure instead of a hang.
     */
    static constexpr int kExecutionBudget = 10000;

    /**
     * @brief Begin a sequence at the asset entry node.
     * @param asset Borrowed asset; it must outlive the runtime.
     * @param bindings Owning parameter bindings; must be an object.
     * @return Success after the sequence reaches its first suspension point.
     * @remarks Validation and execution are transactional; a rejected start
     *          leaves the prior runtime state unchanged.
     * @thread Owner thread only.
     * @reentrancy Emits events synchronously; the sink must not re-enter this runtime.
     */
    [[nodiscard]] eve::Result<void> start(const SequenceAsset* asset, eve::Value bindings = eve::Value::Object{});

    /**
     * @brief Execute nodes until the sequence blocks, ends or fails.
     * @return Success on a suspension point or clean end, or a structured failure.
     * @thread Owner thread only. @reentrancy As `start`.
     */
    [[nodiscard]] eve::Result<void> runUntilBlocked();

    /**
     * @brief Acknowledge a host-presented `wait` or handler-less `Await` step.
     * @return Success, or a failure while preserving the current cursor.
     * @remarks Rejects a `choice` node: a choice must be answered with `select`.
     */
    [[nodiscard]] eve::Result<void> advance();

    /**
     * @brief Answer a blocked `choice` node by route label.
     * @param routeLabel Exact authored route label on the current choice node.
     * @return Success, or a structured failure leaving the cursor unchanged.
     * @remarks A route whose condition is rejected fails without advancing, so
     *          the caller can present the choice again.
     */
    [[nodiscard]] eve::Result<void> select(std::string_view routeLabel);

    /**
     * @brief Enter one already prepared choice route transactionally.
     * @remarks Uses capture → enter/run → restore, so a failed route keeps the
     *          exact suspended choice cursor.
     */
    [[nodiscard]] eve::Result<void> selectRouteForTransaction(std::string_view routeLabel);

    /**
     * @brief Resume a step whose handler returned `Blocked`.
     * @param result Owning result value exposed to the sequence.
     * @return Success, or a failure while preserving the suspended cursor.
     * @remarks When the suspended node payload names a `result` local, the value
     *          is stored there; it is always available through `lastStepResult`.
     */
    [[nodiscard]] eve::Result<void> resumeStep(eve::Value result);

    /**
     * @brief Resume the exact blocked command with a legacy state value.
     * @param requestId Stable request id exposed by `pendingCommandRequestId`.
     * @param result Owning result stored in the command's `resultLocal`, if any.
     */
    [[nodiscard]] eve::Result<void> resumeCommand(std::string_view requestId, eve::StateValue result);

    /** @brief Resume the exact blocked command with a canonical value. */
    [[nodiscard]] eve::Result<void> resumeCommand(std::string_view requestId, eve::Value result);

    /** @brief Stop and clear the active sequence without emitting `Ended`. */
    void stop();

    /**
     * @brief Capture the complete cursor, bindings, locals and call stack.
     * @param out Receives an owning object value usable by `restoreState`.
     * @return Success; capturing a stopped runtime yields `{"active":false}`.
     */
    [[nodiscard]] eve::Result<void> captureState(eve::Value& out) const;

    /**
     * @brief Restore a captured cursor through the configured asset resolver.
     * @param in Value previously produced by `captureState`.
     * @return Success, or a structured failure that leaves the runtime unchanged.
     * @remarks A saved asset whose `version` no longer matches is rejected rather
     *          than silently replayed against changed content.
     */
    [[nodiscard]] eve::Result<void> restoreState(const eve::Value& in);

    void setAssetResolver(AssetResolver resolver) { assetResolver_ = std::move(resolver); }
    void setConditionEvaluator(ConditionEvaluator evaluator) { conditionEvaluator_ = std::move(evaluator); }
    /** @brief Bind the step vocabulary; passing nullptr disables non-core steps. */
    void setStepRegistry(const StepKindRegistry* registry) { registry_ = registry; }
    /** @brief Register a named generic command handler. */
    void registerCommand(std::string name, SequenceCommandHandler handler);
    /** @brief Remove a named generic command handler. */
    void unregisterCommand(std::string_view name);
    /** @brief Install the catch-all command dispatcher used after named lookup. */
    void setCommandDispatcher(SequenceCommandHandler dispatcher) { commandDispatcher_ = std::move(dispatcher); }
    /** @brief Remove the catch-all command dispatcher. */
    void clearCommandDispatcher() { commandDispatcher_ = {}; }
    /**
     * @brief Install the opaque consumer context forwarded to every step handler.
     * @param host Borrowed pointer the core stores but never dereferences.
     * @ownership Borrowed; the caller keeps it alive for as long as the runtime may dispatch.
     * @thread Set on the owner thread before the next `runUntilBlocked`.
     * @reentrancy Does not invoke callbacks.
     */
    void setHostContext(void* host) { hostContext_ = host; }
    void setEventSink(EventSink sink) { eventSink_ = std::move(sink); }

    /** @brief Whether a sequence is currently loaded. */
    [[nodiscard]] bool isActive() const { return asset_ != nullptr; }
    /** @brief Whether execution is suspended and awaiting host acknowledgement. */
    [[nodiscard]] bool isBlocked() const { return blocked_; }
    /** @brief Whether the suspension is a step awaiting `resumeStep`. */
    [[nodiscard]] bool isWaitingStep() const { return waitingStep_; }
    /** @brief Whether the suspension is a command awaiting `resumeCommand`. */
    [[nodiscard]] bool isWaitingCommand() const { return waitingCommand_; }
    /** @brief Borrowed active asset, or nullptr. @lifetime Owned by the caller of `start`. */
    [[nodiscard]] const SequenceAsset* asset() const { return asset_; }
    /** @brief Borrowed current node, or nullptr. @lifetime Invalidated by the next transition. */
    [[nodiscard]] const SequenceNode* currentNode() const;
    [[nodiscard]] const std::string& currentNodeId() const { return nodeId_; }
    [[nodiscard]] const eve::Value& bindings() const { return bindings_; }
    [[nodiscard]] const eve::Value& locals() const { return locals_; }
    [[nodiscard]] eve::Value& locals() { return locals_; }
    /** @brief Value supplied by the most recent `resumeStep`. */
    [[nodiscard]] const eve::Value& lastStepResult() const { return lastStepResult_; }
    /** @brief Last emitted command request, or nullptr when none has been emitted. */
    [[nodiscard]] const SequenceCommandRequest* lastCommandRequest() const {
        return lastCommandRequest_ ? &*lastCommandRequest_ : nullptr;
    }
    /** @brief Stable id of the blocked command, or an empty string. */
    [[nodiscard]] const std::string& pendingCommandRequestId() const noexcept { return pendingCommandRequestId_; }
    /**
     * @brief Last evaluated condition outcome.
     * @return Borrowed nullable pointer owned by this runtime.
     * @lifetime Valid until the next condition evaluation, `stop`, or destruction.
     */
    [[nodiscard]] const SequenceConditionOutcome* lastConditionResult() const {
        return lastConditionResult_ ? &*lastConditionResult_ : nullptr;
    }

private:
    struct Frame {
        const SequenceAsset* asset = nullptr;
        std::string          returnNode;
        eve::Value           bindings = eve::Value::Object{};
        eve::Value           locals   = eve::Value::Object{};
    };

    /** @brief Owning in-process snapshot used to roll back one failed transition. */
    struct ExecutionState {
        const SequenceAsset*                    asset = nullptr;
        std::string                             nodeId;
        eve::Value                              bindings       = eve::Value::Object{};
        eve::Value                              locals         = eve::Value::Object{};
        bool                                    blocked        = false;
        bool                                    waitingStep    = false;
        bool                                    waitingCommand = false;
        std::vector<Frame>                      callStack;
        eve::Value                              lastStepResult;
        std::optional<SequenceCommandRequest>   lastCommandRequest;
        std::string                             pendingCommandRequestId;
        std::uint64_t                           commandSequence = 1;
        std::optional<SequenceConditionOutcome> lastConditionResult;
    };

    [[nodiscard]] ExecutionState    captureExecutionState() const;
    void                            restoreExecutionState(ExecutionState state);
    [[nodiscard]] eve::Result<void> runUntilBlockedImpl();
    [[nodiscard]] eve::Result<void> enter(const std::string& nodeId);
    [[nodiscard]] eve::Result<void> fail(eve::DiagnosticCode code, std::string message, std::string path = {});
    /** @brief Evaluate `node`'s routes and return the selected target or `node.next`. */
    [[nodiscard]] eve::Result<std::string> evaluateRoute(const SequenceNode& node);
    [[nodiscard]] eve::Result<void>        selectImpl(std::string_view routeLabel);
    [[nodiscard]] eve::Result<void>        resumeCommandImpl(std::string_view requestId, eve::Value result);
    void emit(EventKind kind, const SequenceNode* node = nullptr, const std::string& detail = {}) const;

    const SequenceAsset*    asset_ = nullptr;
    std::string             nodeId_;
    eve::Value              bindings_ = eve::Value::Object{};
    eve::Value              locals_   = eve::Value::Object{};
    bool                    blocked_   = false;
    bool                    waitingStep_ = false;
    bool                                                    waitingCommand_ = false;
    std::vector<Frame>      callStack_;
    AssetResolver           assetResolver_;
    ConditionEvaluator      conditionEvaluator_;
    const StepKindRegistry* registry_ = nullptr;
    std::unordered_map<std::string, SequenceCommandHandler> commandHandlers_;
    SequenceCommandHandler                                  commandDispatcher_;
    void*                   hostContext_ = nullptr;
    EventSink               eventSink_;
    eve::Value              lastStepResult_;
    std::optional<SequenceCommandRequest>                   lastCommandRequest_;
    std::string                                             pendingCommandRequestId_;
    std::uint64_t                                           commandSequence_ = 1;
    std::optional<SequenceConditionOutcome> lastConditionResult_;
    /** @brief Message of the most recent failure, used to restore state after a rollback. */
    std::string             failureText_;
};

}  // namespace eve::dnut
