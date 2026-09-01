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
#include <span>
#include <unordered_map>

namespace ssq {
class Object;
}

namespace eve::dialogue {

class ConversationDocument;

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

    int         loadFromDnut(const std::string& source, const std::string& path);
    int         reloadFromDnut(const std::string& source, const std::string& path);
    int         loadFromDnutFile(const std::string& path);
    int         importYarn(const std::string& source, const std::string& path);
    int         importTwee(const std::string& source, const std::string& path);
    bool        removeSource(const std::string& path);
    bool        lintAll();
    bool        renameConversation(const std::string& oldId, const std::string& newId);
    bool        renameNode(const std::string& conversationId, const std::string& oldId, const std::string& newId);
    bool        getLastLoadChanged() const { return lastLoadChanged_; }
    void        clear();
    int         getConversationCount() const;
    std::string getConversationId(int index) const;
    bool        hasConversation(const std::string& id) const;
    std::string exportLocalizationCsv() const;
    int         importLocalizationCsv(const std::string& csv, const std::string& defaultLocale);
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
    std::string                    getDiagnosticMessage(int index) const;
    /** @brief Create an empty UI-neutral conversation document. */
    ConversationDocument* newDocument(const std::string& id) const;
    /** @brief Create an editable copy of a registered conversation. */
    ConversationDocument* getDocument(const std::string& id) const;
    /** @brief Validate and transactionally insert or replace an authored document. */
    bool applyDocument(ConversationDocument* document);

    /**
     * @brief Start a conversation after validating script bindings.
     * @return Applied on success, or a
     * structured missing-asset/binding/runner diagnostic.
     * @thread Affine to the DialogueFlow thread. @reentrancy
     * Does not invoke integration callbacks.
     */
    [[nodiscard]] eve::Result<void> startChecked(const std::string& id, ssq::Object bindings);
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
    /** @brief Compatibility-only bool projection of advanceChecked. */
    bool advance();
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

    /** @brief Register one pure Squirrel evaluator receiving {expression,bindings,locals}. */
    bool setExpressionEvaluator(ssq::Object fn);
    void clearExpressionEvaluator();

    bool        captureState(StateValue& out) const { return runner_.captureState(out); }
    bool        restoreState(const StateValue& in, std::string* error = nullptr);
    std::string captureStateJson() const;
    bool        restoreStateJson(const std::string& json);
    bool        registerMigration(const std::string& assetId, int fromVersion, const std::string& currentAssetId,
                                  const std::string& nodeMap);
    void        clearMigrations() { migrations_.clear(); }
    void        addToneRule(const std::string& expression, const std::string& prefix, const std::string& suffix,
                            const std::string& find, const std::string& replacement);
    void        clearToneRules() { textRenderer_.clearToneRules(); }

private:
    int                      mergeImported(std::vector<ConversationAsset> imported);
    const ConversationAsset* find(const std::string& id) const;
    StateValue      evaluate(const std::string& expression, const StateValue& bindings, const StateValue& locals);
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
    eve::IStateMutation*                                      stateMutationProvider_ = nullptr;
    DialoguePaymentAdapter                                    paymentAdapter_;
    std::uint64_t                                             transactionSequence_ = 1;
    std::string                                               failureMessage_;
    HSQUIRRELVM                                               vm_ = nullptr;
    HSQOBJECT                                                 evaluator_{};
    bool                                                      hasEvaluator_ = false;
    std::unordered_map<std::string, size_t>                   sourceHashes_;
    std::unordered_map<std::string, std::vector<std::string>> sourceAssets_;
    bool                                                      lastLoadChanged_ = false;
    ConversationLocalizationCatalog                           localization_;
    std::string                                               locale_;
    ConversationSaveMigrations                                migrations_;
    ConversationTextRenderer                                  textRenderer_;
};

}  // namespace eve::dialogue
