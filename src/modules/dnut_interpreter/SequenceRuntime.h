#pragma once
#include "common/Export.h"


/** @file SequenceRuntime.h @brief Cross-frame interpreter for compiled `.dnut` sequences. */

#include "common/Result.h"
#include "common/Value.h"
#include "dnut_interpreter/SequenceAsset.h"
#include "dnut_interpreter/StepKindRegistry.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace eve::dnut {

/** @brief Outcome of one authored condition, as seen by the language core. */
struct SequenceConditionOutcome {
    bool        passed = false;
    /** @brief Stable machine-readable reason; empty when `passed` is true. */
    std::string reason;
};

/**
 * @brief Caller-owned interpreter over a compiled sequence asset.
 *
 * The runtime owns only the execution cursor: the current asset, node, call
 * stack, parameter bindings and step locals. Persistent progress, completion
 * facts and gameplay state stay with the domain owner that persists them.
 *
 * The runtime never interprets a step payload. Control-flow types
 * (`branch` / `choice` / `call` / `wait` / `end`) are handled here; every other
 * type is routed through the configured `StepKindRegistry`.
 */
class EVENGINE_API_PLATFORM SequenceRuntime {
public:
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
     * @remarks Every mutation is applied only after the asset validates, so a
     *          rejected start leaves the runtime stopped.
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
     * @brief Resume a step whose handler returned `Blocked`.
     * @param result Owning result value exposed to the sequence.
     * @return Success, or a failure while preserving the suspended cursor.
     * @remarks When the suspended node payload names a `result` local, the value
     *          is stored there; it is always available through `lastStepResult`.
     */
    [[nodiscard]] eve::Result<void> resumeStep(eve::Value result);

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

    [[nodiscard]] eve::Result<void> enter(const std::string& nodeId);
    [[nodiscard]] eve::Result<void> fail(eve::DiagnosticCode code, std::string message, std::string path = {});
    /** @brief Evaluate `node`'s routes and return the selected target or `node.next`. */
    [[nodiscard]] eve::Result<std::string> evaluateRoute(const SequenceNode& node);
    void emit(EventKind kind, const SequenceNode* node = nullptr, const std::string& detail = {}) const;

    const SequenceAsset*    asset_ = nullptr;
    std::string             nodeId_;
    eve::Value              bindings_ = eve::Value::Object{};
    eve::Value              locals_   = eve::Value::Object{};
    bool                    blocked_   = false;
    bool                    waitingStep_ = false;
    std::vector<Frame>      callStack_;
    AssetResolver           assetResolver_;
    ConditionEvaluator      conditionEvaluator_;
    const StepKindRegistry* registry_ = nullptr;
    void*                   hostContext_ = nullptr;
    EventSink               eventSink_;
    eve::Value              lastStepResult_;
    std::optional<SequenceConditionOutcome> lastConditionResult_;
    /** @brief Message of the most recent failure, used to restore state after a rollback. */
    std::string             failureText_;
};

}  // namespace eve::dnut
