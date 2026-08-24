#pragma once

#include "common/Module.h"
#include "dialogue/ConversationCompiler.h"
#include "dialogue/ConversationLocalization.h"
#include "dialogue/ConversationPersistence.h"
#include "dialogue/ConversationText.h"

#include <squirrel.h>
#include <unordered_map>

namespace ssq {
class Object;
}

namespace eve::dialogue {

/** @brief Script-facing registry and runner for compiled .dnut conversations. */
class DialogueFlow : public Module {
public:
    Module_REG(DialogueFlow);
    DialogueFlow();
    ~DialogueFlow() override;

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
    void        setLocale(const std::string& locale) { locale_ = locale; }
    std::string getLocale() const { return locale_; }
    int         getDiagnosticCount() const;
    std::string getDiagnosticSeverity(int index) const;
    std::string getDiagnosticMessage(int index) const;
    std::string getLastError() const { return lastError_; }

    bool        start(const std::string& id, ssq::Object bindings);
    bool        advance();
    bool        select(const std::string& routeId);
    bool        isActive() const { return runner_.isActive(); }
    bool        isBlocked() const { return runner_.isBlocked(); }
    std::string getConversationId() const;
    std::string getNodeId() const { return runner_.currentNodeId(); }
    std::string getNodeKind() const;
    std::string getSpeaker() const;
    std::string getText();
    std::string getPool() const;
    std::string getI18nKey() const;
    std::string getVoice() const;
    std::string getVoiceStatus() const;
    float       getVoiceDuration() const;
    int         getRouteCount() const;
    std::string getRouteId(int index) const;

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
    StateValue evaluate(const std::string& expression, const StateValue& bindings, const StateValue& locals);

    std::vector<ConversationAsset>                            assets_;
    std::vector<ConversationDiagnostic>                       diagnostics_;
    ConversationRunner                                        runner_;
    std::string                                               lastError_;
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
