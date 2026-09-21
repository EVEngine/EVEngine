#pragma once

#include "common/Module.h"
#include "common/Result.h"
#include "dialogue/ConversationCompiler.h"
#include "dialogue/ConversationLocalization.h"
#include "dialogue/ConversationPersistence.h"
#include "dialogue/ConversationText.h"
#include "dialogue/DialogueState.h"

#include <squirrel.h>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace ssq {
class Object;
}

namespace eve::dialogue {

class ConversationDocument;
class DialogueControl;

}  // namespace eve::dialogue

namespace eve::i18n {
class I18n;
}

namespace eve::dialogue {

/** @brief Script-facing registry and runner for compiled .dnut conversations. */
class DialogueFlow : public Module {
public:
    /**
     * @brief Borrowed world and cross-domain callbacks used by all conversations in this facade.
     *
     * `stateQuery` and `stateMutation` are never owned or retained by the
     * configuration object; DialogueFlow borrows them until the next
     * configure/clear call or destruction. The callbacks are copied into the
     * facade and invoked synchronously on the DialogueFlow thread. An empty
     * `conditionEvaluator` selects the shared DialogueState/decision adapter.
     * An empty operation or gameplay handler rejects requests of that kind.
     */
    struct IntegrationConfig {
        /** @brief Stable world subject used by state-backed conditions. */
        std::string subject;
        /** @brief Borrowed read-only world state provider. */
        eve::IStateQuery* stateQuery = nullptr;
        /** @brief Borrowed all-or-nothing world mutation provider. */
        eve::IStateMutation* stateMutation = nullptr;
        /** @brief Optional custom condition evaluator; empty uses the shared adapter. */
        ConversationRunner::ConditionEvaluator conditionEvaluator;
        /** @brief Handler for requests whose kind is Operation. */
        CommandRequestHandler operationHandler;
        /** @brief Handler for requests whose kind is GameplayAction. */
        CommandRequestHandler gameplayActionHandler;
        /** @brief Borrowed authoritative money/reputation account adapters. */
        DialogueAccountBindings accounts;
        /**
         * @brief Factory for transaction participants that stage an operation or Action.
         * @remarks The returned participant is owned for the synchronous transaction only;
         *          it must implement compensation when later state effects can fail.
         */
        using CommandParticipantFactory =
            std::function<eve::Result<std::unique_ptr<eve::transaction::ITransactionParticipant>>(
                const CommandRequest&)>;
        /** @brief Optional factory used to stage an atomic operation or Action. */
        CommandParticipantFactory commandParticipantFactory;
        /**
         * @brief Injected UTF-8 content reader used by loadDnutFileChecked.
         * @remarks The callback runs synchronously on the owner thread and must
         *          not re-enter DialogueFlow. Dialogue has no filesystem-module dependency.
         */
        std::function<eve::Result<std::string>(const std::string&)> contentReader;
    };

    Module_REG(DialogueFlow);
    DialogueFlow();
    ~DialogueFlow() override;

    /**
     * @brief Configure every runner in this module facade in one operation.
     *
     * Call while the flow is inactive, before `start`, and on the same thread
     * that will call `start`, `advance`, `select`, or
     * `applyStateMutations`. The provider pointers are borrowed and must
     * outlive this facade, or the caller must call `clearIntegration()` first.
     * Handlers receive an owning request snapshot and must not retain it or
     * re-enter this facade. DialogueFlow owns no world state and creates no
     * second state mirror.
     */
    void configureIntegration(IntegrationConfig config);
    /**
     * @brief Remove explicit providers and callbacks from the facade.
     * @remarks Capability-discovered query providers remain available to the
     *          shared condition adapter after this call.
     */
    void clearIntegration();
    /** @brief Return the facade-owned state boundary; providers remain borrowed. */
    const DialogueStateContext& stateContext() const noexcept { return stateContext_; }
    /** @brief Apply mutations through the configured authoritative provider. */
    [[nodiscard]] eve::Result<eve::MutationReceipt> applyStateMutations(std::span<const eve::StateMutation> mutations,
                                                                        const eve::MutationContext& context) const;

    /** @brief Compile, validate, and atomically commit one versioned dnut document. */
    [[nodiscard]] eve::Result<int> loadDnutChecked(const std::string& source, const std::string& sourceId);
    /** @brief Transactionally replace one source while preserving a migratable active runner. */
    [[nodiscard]] eve::Result<int> reloadDnutChecked(const std::string& source, const std::string& sourceId);
    /** @brief Read through the injected filesystem capability and call loadDnutChecked. */
    [[nodiscard]] eve::Result<int> loadDnutFileChecked(const std::string& path);
    [[nodiscard]] eve::Result<int> importYarnChecked(const std::string& source, const std::string& path);
    [[nodiscard]] eve::Result<int> importTweeChecked(const std::string& source, const std::string& path);
    [[nodiscard]] eve::Result<void> removeSourceChecked(const std::string& path);
    [[nodiscard]] eve::Result<void> lintAllChecked();
    [[nodiscard]] eve::Result<void> renameConversationChecked(const std::string& oldId, const std::string& newId);
    [[nodiscard]] eve::Result<void> renameNodeChecked(const std::string& conversationId, const std::string& oldId,
                                                      const std::string& newId);
    bool        getLastLoadChanged() const { return lastLoadChanged_; }
    void        clear();
    int         getConversationCount() const;
    std::string getConversationId(int index) const;
    bool        hasConversation(const std::string& id) const;
    std::string exportLocalizationCsv() const;
    /** @brief Import localized strings transactionally and return the imported row count. */
    [[nodiscard]] eve::Result<int> importLocalizationCsvChecked(const std::string& csv,
                                                                const std::string& defaultLocale);
    std::string exportMissingLocalizationCsv(const std::string& locale) const;
    std::string exportVoiceRecordingCsv(const std::string& locale) const;
    /**
     * @brief Validate that every referenced dialogue localization key exists in one exact locale.
     * @param
     * localization Borrowed localization authority used only during this synchronous call.
     * @param locale Exact
     * locale to inspect; default-language substitution is intentionally not accepted.
     * @return Number of
     * validated localized nodes, or a path-scoped missing-key diagnostic.
     * @thread Main-thread only; neither
     * module may be mutated concurrently.
     * @reentrancy Does not invoke script or user callbacks.
     */
    [[nodiscard]] eve::Result<int> validateLocalization(const eve::i18n::I18n& localization,
                                                        const std::string&     locale) const;
    void                           setLocale(const std::string& locale) { locale_ = locale; }
    std::string                    getLocale() const { return locale_; }
    int                            getDiagnosticCount() const;
    std::string                    getDiagnosticSeverity(int index) const;
    std::string                    getDiagnosticPath(int index) const;
    int                            getDiagnosticLine(int index) const;
    /** @brief Return the one-based source column for a diagnostic, or zero when unavailable. */
    int                            getDiagnosticColumn(int index) const;
    /** @brief Return the stable machine-readable diagnostic code. */
    std::string                    getDiagnosticCode(int index) const;
    /** @brief Return the stable asset/node/route path associated with a diagnostic. */
    std::string                    getDiagnosticAssetPath(int index) const;
    std::string                    getDiagnosticMessage(int index) const;
    /** @brief Create an empty UI-neutral conversation document. */
    ConversationDocument* newDocument(const std::string& id) const;
    /** @brief Create an editable copy of a registered conversation. */
    ConversationDocument* getDocument(const std::string& id) const;
    /** @brief Validate and transactionally insert or replace an authored document. */
    [[nodiscard]] eve::Result<void> applyDocumentChecked(ConversationDocument* document);

    /**
     * @brief Start a conversation after validating script bindings.
     * @return Applied on success, or a
     * structured missing-asset/binding/runner diagnostic.
     * @thread Affine to the DialogueFlow thread. @reentrancy
     * Does not invoke integration callbacks.
     */
    [[nodiscard]] eve::Result<void> startChecked(const std::string& id, ssq::Object bindings);
    /**
     * @brief Start a conversation with an empty binding table.
     * @param id Conversation id to start.
     * @return Applied on success, or the same structured diagnostics as the
     *         binding-carrying overload.
     * @remarks Entry point for hosts without a Squirrel call frame (MCP /
     *          `eve_gameplay`). When a VM is attached the empty table is a real
     *          Squirrel table, so conditions that read bindings resolve as
     *          absent instead of reading an invalid object.
     */
    [[nodiscard]] eve::Result<void> startChecked(const std::string& id);
    /** @brief Compatibility-only bool projection of startChecked. */
    bool start(const std::string& id, ssq::Object bindings);
    /**
     * @brief Advance the active conversation with a structured runner diagnostic.
     * @return Applied on
     * success; failure preserves the runner's validated state.
     * @thread Affine to the DialogueFlow thread.
     * @reentrancy May synchronously invoke configured command handlers.
     */
    [[nodiscard]] eve::Result<void> advanceChecked();
    /**
     * @brief Resume the exact asynchronous command currently owned by the runner.
     * @param requestId Stable id returned by getPendingCommandRequestId().
     * @param value Owning command result converted synchronously into runner locals.
     * @return Applied on success; stale, duplicate, or mismatched ids leave state unchanged.
     * @thread Affine to the DialogueFlow owner thread.
     * @reentrancy Does not invoke integration callbacks while mutating runner state.
     */
    [[nodiscard]] eve::Result<void> resumeCommandChecked(const std::string& requestId, eve::Value value);
    /** @brief Return the current pending command id, or an empty string when none is pending. */
    std::string getPendingCommandRequestId() const { return runner_.pendingCommandRequestId(); }
    /**
     * @brief Select a route, atomically applying configured payment/state effects.
     * @return Applied on success, or the canonical dialogue/transaction diagnostic.
     * @thread Affine to the configured DialogueFlow thread.
     * @reentrancy Providers are invoked synchronously and must not re-enter this flow.
     */
    [[nodiscard]] eve::Result<void> select(const std::string& routeId);
    bool                            isActive() const { return runner_.isActive(); }
    bool                            isBlocked() const { return runner_.isBlocked(); }
    std::string                     getConversationId() const;
    std::string                     getNodeId() const { return runner_.currentNodeId(); }
    std::string                     getNodeKind() const;
    std::string                     getSpeaker() const;
    std::string                     getText();
    std::string                     getPool() const;
    std::string                     getI18nKey() const;
    std::string                     getVoice() const;
    std::string                     getVoiceStatus() const;
    float                           getVoiceDuration() const;
    int                             getRouteCount() const;
    std::string                     getRouteId(int index) const;
    /** @brief Return localized presentation text independently from the stable route id. */
    std::string                     getRouteText(int index) const;
    /** @brief Suspend handler-less commands until resumeCommandChecked is called. */
    void                            setManualCommandMode(bool enabled);

    /** @brief Register one pure Squirrel evaluator receiving {expression,bindings,locals}. */
    /** @brief Install the checked pure-expression callback used by branch evaluation. */
    [[nodiscard]] eve::Result<void> setExpressionEvaluatorChecked(ssq::Object fn);
    void clearExpressionEvaluator();

    [[nodiscard]] eve::Result<StateValue> captureStateChecked() const { return runner_.captureStateChecked(); }
    [[nodiscard]] eve::Result<void> restoreStateChecked(const StateValue& in);
    [[nodiscard]] eve::Result<std::string> captureStateJsonChecked() const;
    [[nodiscard]] eve::Result<void> restoreStateJsonChecked(const std::string& json);
    [[nodiscard]] eve::Result<void> registerMigrationChecked(const std::string& assetId, int fromVersion,
                                                             const std::string& currentAssetId,
                                                             const std::string& nodeMap);
    void        clearMigrations() { migrations_.clear(); }
    void        addToneRule(const std::string& expression, const std::string& prefix, const std::string& suffix,
                            const std::string& find, const std::string& replacement);
    void        clearToneRules() { textRenderer_.clearToneRules(); }

    /**
     * @brief 把本模块的对话运行器发布到共享玩法协议（`eve_gameplay` / MCP）。
     *
     * 运行器一次只跑一个对话，因此领域最多发布一个实例：重复发布返回
     * Conflict，避免两个身份指向同一个运行器。动作词表就是本模块自己的操作
     * （start/advance/select），观察结果是当前节点、说话人、文本与路由。
     * @param instanceId 实例稳定标识，必须是规范持久 id（UUID 文本）。
     * @param ownerId 控制该实例的玩家/角色稳定标识，同为规范持久 id。
     * @return 成功返回空结果；id 非法或运行器已发布时返回诊断。
     * @ownership 适配器由本模块持有并随模块销毁；运行器所有权不变。
     * @thread 所有者模拟线程。
     */
    [[nodiscard]] eve::Result<void> publishGameplay(const std::string& instanceId, const std::string& ownerId);
    /** @brief 取消发布一个实例；该实例未发布（或 id 非法）时返回诊断。 */
    [[nodiscard]] eve::Result<void> unpublishGameplay(const std::string& instanceId);
    /** @brief 取消发布本模块持有的全部玩法实例。 */
    void clearGameplayControls();
    /** @brief 已发布的玩法实例数量（0 或 1）。 */
    [[nodiscard]] int gameplayControlCount() const;
    /** @brief 已发布的玩法实例标识（发布顺序）。 */
    [[nodiscard]] std::vector<std::string> gameplayInstances() const;

private:
    int loadDnutImpl(const std::string& source, const std::string& sourceId);
    int reloadDnutImpl(const std::string& source, const std::string& sourceId);
    int loadDnutFileImpl(const std::string& path);
    [[nodiscard]] eve::Result<int> mergeImported(std::vector<ConversationAsset> imported);
    const ConversationAsset* find(const std::string& id) const;
    [[nodiscard]] eve::Result<StateValue> evaluate(const std::string& expression, const StateValue& bindings,
                                                   const StateValue& locals);
    CommandResponse dispatchCommand(const CommandRequest& request);
    std::string     nextTransactionId(const char* purpose);

    std::vector<ConversationAsset>                            assets_;
    std::vector<ConversationDiagnostic>                       diagnostics_;
    ConversationRunner                                        runner_;
    DialogueStateContext                                      stateContext_;
    ConversationRunner::ConditionEvaluator                    configuredConditionEvaluator_;
    CommandRequestHandler                                     operationRequestHandler_;
    CommandRequestHandler                                     gameplayActionHandler_;
    IntegrationConfig::CommandParticipantFactory              commandParticipantFactory_;
    std::function<eve::Result<std::string>(const std::string&)> contentReader_;
    eve::IStateMutation*                                      stateMutationProvider_ = nullptr;
    DialoguePaymentAdapter                                    paymentAdapter_;
    std::uint64_t                                             transactionSequence_ = 1;
    std::string                                               failureMessage_;
    HSQUIRRELVM                                               vm_ = nullptr;
    HSQOBJECT                                                 evaluator_{};
    bool                                                      hasEvaluator_ = false;
    bool                                                      manualCommandMode_ = false;
    std::unordered_map<std::string, std::string>              sourceTexts_;
    std::unordered_map<std::string, std::vector<std::string>> sourceAssets_;
    std::unordered_map<std::string, std::string>              assetSources_;
    std::unordered_map<std::string, DataValue>                sourcePools_;
    bool                                                      lastLoadChanged_ = false;
    ConversationLocalizationCatalog                           localization_;
    std::string                                               locale_;
    ConversationSaveMigrations                                migrations_;
    ConversationTextRenderer                                  textRenderer_;
    /** 惰性创建的玩法适配器；模块析构时随之注销。 */
    std::unique_ptr<DialogueControl> gameplay_;
};

}  // namespace eve::dialogue
