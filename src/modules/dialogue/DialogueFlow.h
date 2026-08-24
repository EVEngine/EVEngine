#pragma once

#include "common/Module.h"
#include "dialogue/ConversationCompiler.h"

#include <squirrel.h>

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

    int loadFromDnut(const std::string& source, const std::string& path);
    int loadFromDnutFile(const std::string& path);
    int importYarn(const std::string& source, const std::string& path);
    int importTwee(const std::string& source, const std::string& path);
    void clear();
    int getConversationCount() const;
    std::string getConversationId(int index) const;
    bool hasConversation(const std::string& id) const;
    std::string exportLocalizationCsv() const;
    int getDiagnosticCount() const;
    std::string getDiagnosticSeverity(int index) const;
    std::string getDiagnosticMessage(int index) const;
    std::string getLastError() const { return lastError_; }

    bool start(const std::string& id, ssq::Object bindings);
    bool advance();
    bool select(const std::string& routeId);
    bool isActive() const { return runner_.isActive(); }
    bool isBlocked() const { return runner_.isBlocked(); }
    std::string getConversationId() const;
    std::string getNodeId() const { return runner_.currentNodeId(); }
    std::string getNodeKind() const;
    std::string getSpeaker() const;
    std::string getText() const;
    std::string getPool() const;
    std::string getI18nKey() const;
    std::string getVoice() const;
    int getRouteCount() const;
    std::string getRouteId(int index) const;

    /** @brief Register one pure Squirrel evaluator receiving {expression,bindings,locals}. */
    bool setExpressionEvaluator(ssq::Object fn);
    void clearExpressionEvaluator();

    bool captureState(StateValue& out) const { return runner_.captureState(out); }
    bool restoreState(const StateValue& in, std::string* error = nullptr);

private:
    int mergeImported(std::vector<ConversationAsset> imported);
    const ConversationAsset* find(const std::string& id) const;
    StateValue evaluate(const std::string& expression, const StateValue& bindings,
                        const StateValue& locals);

    std::vector<ConversationAsset> assets_;
    std::vector<ConversationDiagnostic> diagnostics_;
    ConversationRunner runner_;
    std::string lastError_;
    HSQUIRRELVM vm_ = nullptr;
    HSQOBJECT evaluator_{};
    bool hasEvaluator_ = false;
};

}  // namespace eve::dialogue
