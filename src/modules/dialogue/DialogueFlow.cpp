#include "dialogue/DialogueFlow.h"

#include "common/Capability.h"
#include "common/ServiceInterfaces.h"
#include "common/SquirrelBinding.h"
#include "common/SubjectRef.h"
#include "dialogue/ConversationAuthoring.h"
#include "dialogue/ConversationImporter.h"
#include "dialogue/ConversationToolchain.h"
#include "dialogue/DialogueControl.h"
#include "filesystem/Filesystem.h"
#include "i18n/I18n.h"

#include <algorithm>
#include <array>
#include <simplesquirrel/simplesquirrel.hpp>
#include <utility>

namespace eve::dialogue {

Module_IMPL(DialogueFlow, new DialogueFlow());

namespace {

template <class T = void>
eve::Result<T> flowFailure(eve::DiagnosticCode code, const std::string& message, const std::string& path) {
    return eve::Result<T>::failure(
        eve::Diagnostic::error(code, message, path, {}, "dialogue.flow"));
}

bool buildPoolWorkspace(const std::unordered_map<std::string, DataValue>& sources, DataValue& root,
                        std::string& error) {
    DataValue::Object combined;
    for (const auto& [sourceId, sourceRoot] : sources) {
        const DataValue* pools = sourceRoot.find("pools");
        if (!pools || !pools->isObject()) continue;
        for (const auto& poolId : pools->keys()) {
            if (combined.contains(poolId)) {
                error = "pool '" + poolId + "' has multiple source owners (including '" + sourceId + "')";
                return false;
            }
            combined.emplace(poolId, *pools->find(poolId));
        }
    }
    root = DataValue::object({{"pools", DataValue::object(std::move(combined))}});
    return true;
}

bool squirrelToState(HSQUIRRELVM vm, SQInteger index, StateValue& out) {
    switch (sq_gettype(vm, index)) {
        case OT_NULL: out = StateValue::null(); return true;
        case OT_INTEGER: {
            SQInteger value = 0;
            if (SQ_FAILED(sq_getinteger(vm, index, &value))) return false;
            out = StateValue::integer(value);
            return true;
        }
        case OT_FLOAT: {
            SQFloat value = 0;
            if (SQ_FAILED(sq_getfloat(vm, index, &value))) return false;
            out = StateValue::number(value);
            return true;
        }
        case OT_BOOL: {
            SQBool value = SQFalse;
            if (SQ_FAILED(sq_getbool(vm, index, &value))) return false;
            out = StateValue::boolean(value != 0);
            return true;
        }
        case OT_STRING: {
            const SQChar* value = nullptr;
            if (SQ_FAILED(sq_getstring(vm, index, &value))) return false;
            out = StateValue::string(value ? value : "");
            return true;
        }
        case OT_TABLE: {
            out                      = StateValue::object();
            const SQInteger absolute = index > 0 ? index : sq_gettop(vm) + index + 1;
            sq_pushnull(vm);
            while (SQ_SUCCEEDED(sq_next(vm, absolute))) {
                const SQChar* key = nullptr;
                StateValue    value;
                const bool    ok = sq_gettype(vm, -2) == OT_STRING && SQ_SUCCEEDED(sq_getstring(vm, -2, &key)) && key &&
                                   squirrelToState(vm, -1, value);
                sq_pop(vm, 2);
                if (!ok) {
                    sq_pop(vm, 1);
                    return false;
                }
                out.set(key, std::move(value));
            }
            sq_pop(vm, 1);
            return true;
        }
        default: return false;
    }
}

void pushState(HSQUIRRELVM vm, const StateValue& value) {
    switch (value.kind()) {
        case StateValue::Kind::Null: sq_pushnull(vm); break;
        case StateValue::Kind::Int: sq_pushinteger(vm, value.asInt()); break;
        case StateValue::Kind::Float: sq_pushfloat(vm, static_cast<SQFloat>(value.asDouble())); break;
        case StateValue::Kind::Bool: sq_pushbool(vm, value.asBool() ? SQTrue : SQFalse); break;
        case StateValue::Kind::String: sq_pushstring(vm, value.asString().c_str(), value.asString().size()); break;
        case StateValue::Kind::Array:
            sq_newarray(vm, 0);
            for (size_t i = 0; i < value.arraySize(); ++i) {
                pushState(vm, value.at(i));
                sq_arrayappend(vm, -2);
            }
            break;
        case StateValue::Kind::Object:
            sq_newtable(vm);
            for (const auto& key : value.keys()) {
                sq_pushstring(vm, key.c_str(), key.size());
                pushState(vm, *value.find(key));
                sq_newslot(vm, -3, SQFalse);
            }
            break;
    }
}

std::string kindName(ConversationAsset::Node::Kind kind) {
    switch (kind) {
        case ConversationAsset::Node::Kind::Line: return "line";
        case ConversationAsset::Node::Kind::Branch: return "branch";
        case ConversationAsset::Node::Kind::Choice: return "choice";
        case ConversationAsset::Node::Kind::Call: return "call";
        case ConversationAsset::Node::Kind::Command: return "command";
        case ConversationAsset::Node::Kind::Wait: return "wait";
        case ConversationAsset::Node::Kind::End: return "end";
    }
    return {};
}

eve::Result<void> dialogueFailure(eve::DiagnosticCode code, std::string message, std::string path = {}) {
    return eve::Result<void>::failure(
        eve::Diagnostic::error(code, std::move(message), std::move(path), {}, "dialogue.flow"));
}

class DialogueSelectionParticipant final : public eve::transaction::ITransactionParticipant {
public:
    DialogueSelectionParticipant(ConversationRunner& runner, std::string routeId, StateValue before)
        : runner_(runner), routeId_(std::move(routeId)), before_(std::move(before)) {}

    [[nodiscard]] std::string_view  name() const noexcept override { return "dialogue.choice"; }
    [[nodiscard]] eve::Result<void> prepare(const eve::transaction::TransactionContext& context) override {
        if (context.transactionId().empty())
            return dialogueFailure(eve::DiagnosticCode::InvalidArgument,
                                   "dialogue choice transaction requires a transaction id", "transactionId");
        if (phase_ != Phase::Idle)
            return dialogueFailure(eve::DiagnosticCode::Conflict, "dialogue choice is not idle",
                                   "transaction.lifecycle");
        phase_ = Phase::Prepared;
        return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
    }
    [[nodiscard]] eve::Result<void> commit(const eve::transaction::TransactionContext&) override {
        if (phase_ != Phase::Prepared)
            return dialogueFailure(eve::DiagnosticCode::Conflict, "dialogue choice has no prepared stage",
                                   "transaction.lifecycle");
        auto selected = runner_.selectRouteForTransaction(routeId_);
        if (!selected.ok()) {
            std::string error = selected.status().describe();
            auto restored = runner_.restoreStateChecked(before_);
            if (!restored && error.empty()) error = restored.status().describe();
            return dialogueFailure(eve::DiagnosticCode::Failed,
                                   error.empty() ? "dialogue choice selection failed" : error, "route");
        }
        phase_ = Phase::Committed;
        return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
    }
    [[nodiscard]] eve::Result<void> rollback(const eve::transaction::TransactionContext&) override {
        if (phase_ != Phase::Prepared)
            return dialogueFailure(eve::DiagnosticCode::Conflict, "dialogue choice has no prepared stage to roll back",
                                   "transaction.lifecycle");
        phase_ = Phase::RolledBack;
        return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
    }
    [[nodiscard]] eve::Result<void> compensate(const eve::transaction::TransactionContext&) override {
        if (phase_ != Phase::Committed)
            return dialogueFailure(eve::DiagnosticCode::Conflict, "dialogue choice is not committed",
                                   "transaction.lifecycle");
        auto restored = runner_.restoreStateChecked(before_);
        if (!restored)
            return dialogueFailure(eve::DiagnosticCode::Failed,
                                   restored.status().describe(), "route");
        phase_ = Phase::RolledBack;
        return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
    }

private:
    enum class Phase : std::uint8_t { Idle, Prepared, Committed, RolledBack };
    ConversationRunner& runner_;
    std::string         routeId_;
    StateValue          before_;
    Phase               phase_ = Phase::Idle;
};

}  // namespace

DialogueFlow::DialogueFlow() {
    runner_.setAssetResolver([this](const std::string& id) { return find(id); });
    runner_.setExpressionEvaluator([this](const std::string& expression, const StateValue& bindings,
                                          const StateValue& locals) { return evaluate(expression, bindings, locals); });
    configureIntegration({});
}

DialogueFlow::~DialogueFlow() {
    clearGameplayControls();
    clearIntegration();
    clearExpressionEvaluator();
}

eve::Result<void> DialogueFlow::startChecked(const std::string& id) {
    if (vm_ == nullptr) return startChecked(id, ssq::Object());
    ssq::Table bindings(vm_);
    return startChecked(id, ssq::Object(bindings));
}

eve::Result<void> DialogueFlow::publishGameplay(const std::string& instanceId, const std::string& ownerId) {
    if (instanceId.empty() || ownerId.empty())
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                 "publishGameplay needs an instance id and an owner id",
                                                                 "instanceId"));
    const auto instance = eve::PersistentId::parse(instanceId);
    const auto owner    = eve::PersistentId::parse(ownerId);
    if (!instance.has_value() || !owner.has_value())
        return eve::Result<void>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                   "instance and owner ids must be canonical persistent ids", "instanceId"));
    if (!gameplay_) gameplay_ = std::make_unique<DialogueControl>(*this);
    return gameplay_->publish(eve::SubjectRef::fromPersistentId(*instance), eve::SubjectRef::fromPersistentId(*owner));
}

eve::Result<void> DialogueFlow::unpublishGameplay(const std::string& instanceId) {
    if (!gameplay_)
        return eve::Result<void>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::NotFound, "no dialogue instance is published", "instanceId"));
    const auto instance = eve::PersistentId::parse(instanceId);
    if (!instance.has_value())
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "the instance id must be a canonical persistent id", "instanceId"));
    return gameplay_->unpublish(eve::SubjectRef::fromPersistentId(*instance));
}

void DialogueFlow::clearGameplayControls() {
    if (gameplay_) gameplay_->clear();
}

int DialogueFlow::gameplayControlCount() const { return gameplay_ ? gameplay_->count() : 0; }

std::vector<std::string> DialogueFlow::gameplayInstances() const {
    std::vector<std::string> result;
    if (!gameplay_) return result;
    for (const auto& instance : gameplay_->instances()) result.push_back(instance.format());
    return result;
}

void DialogueFlow::configureIntegration(IntegrationConfig config) {
    stateContext_.setSubject(std::move(config.subject));
    stateContext_.setQueryProvider(config.stateQuery);
    stateContext_.setMutationProvider(config.stateMutation);
    configuredConditionEvaluator_ = std::move(config.conditionEvaluator);
    operationRequestHandler_      = std::move(config.operationHandler);
    gameplayActionHandler_        = std::move(config.gameplayActionHandler);
    commandParticipantFactory_    = std::move(config.commandParticipantFactory);
    contentReader_                = std::move(config.contentReader);
    stateMutationProvider_        = config.stateMutation;
    paymentAdapter_.setBindings(std::move(config.accounts));

    if (configuredConditionEvaluator_) {
        runner_.setConditionEvaluator(configuredConditionEvaluator_);
    } else {
        runner_.setConditionEvaluator(
            [this](const eve::Value& specification) { return stateContext_.evaluate(specification); });
    }

    if (operationRequestHandler_ || gameplayActionHandler_ || commandParticipantFactory_ || stateMutationProvider_ ||
        manualCommandMode_) {
        runner_.setCommandRequestDispatcher([this](const CommandRequest& request) { return dispatchCommand(request); });
    } else {
        runner_.clearCommandRequestDispatcher();
    }
}

void DialogueFlow::clearIntegration() { configureIntegration({}); }

eve::Result<eve::MutationReceipt> DialogueFlow::applyStateMutations(std::span<const eve::StateMutation> mutations,
                                                                    const eve::MutationContext&         context) const {
    return stateContext_.apply(mutations, context);
}

const ConversationAsset* DialogueFlow::find(const std::string& id) const {
    for (const auto& asset : assets_)
        if (asset.id == id) return &asset;
    return nullptr;
}

int DialogueFlow::loadDnutImpl(const std::string& source, const std::string& path) {
    if (const auto it = sourceTexts_.find(path); it != sourceTexts_.end() && it->second == source) {
        lastLoadChanged_ = false;
        failureMessage_.clear();
        return static_cast<int>(sourceAssets_[path].size());
    }
    diagnostics_.clear();
    auto compiledDocument = compileDnutDocument(source, path, diagnostics_);
    if (!compiledDocument) {
        failureMessage_ = diagnostics_.empty() ? "conversation compilation failed" : diagnostics_.front().message;
        return 0;
    }
    DnutDocument document = std::move(compiledDocument).takeValue();
    std::vector<ConversationAsset> compiled = std::move(document.conversations);
    for (const auto& asset : compiled) {
        const auto owner = assetSources_.find(asset.id);
        if (owner != assetSources_.end() && owner->second != path) {
            diagnostics_.push_back({ConversationDiagnostic::Severity::Error, path, 0,
                                    "conversation '" + asset.id + "' is already owned by '" + owner->second + "'"});
            failureMessage_ = diagnostics_.back().message;
            return 0;
        }
    }
    if (runner_.isActive()) {
        failureMessage_ = "cannot load a dnut source while a conversation is active; use transactional reload";
        return 0;
    }
    auto candidatePoolSources = sourcePools_;
    candidatePoolSources[path] = std::move(document.poolRoot);
    DataValue poolWorkspace;
    if (!buildPoolWorkspace(candidatePoolSources, poolWorkspace, failureMessage_)) return 0;
    Dialogue* dialogue = Dialogue::create();
    if (!dialogue || (dialogue->replacePoolsFromData(poolWorkspace) == 0 && !dialogue->getLastPoolsError().empty())) {
        failureMessage_ = dialogue ? dialogue->getLastPoolsError() : "dialogue pool runtime is unavailable";
        return 0;
    }
    runner_.stop();
    if (const auto old = sourceAssets_.find(path); old != sourceAssets_.end()) {
        assets_.erase(std::remove_if(assets_.begin(), assets_.end(),
                                     [&](const auto& asset) {
                                         return std::find(old->second.begin(), old->second.end(), asset.id) !=
                                                old->second.end();
                                     }),
                      assets_.end());
    }
    std::vector<std::string> compiledIds;
    for (auto& asset : compiled) {
        compiledIds.push_back(asset.id);
        auto it = std::find_if(assets_.begin(), assets_.end(), [&](const auto& old) { return old.id == asset.id; });
        if (it == assets_.end())
            assets_.push_back(std::move(asset));
        else
            *it = std::move(asset);
    }
    sourceTexts_[path] = source;
    sourcePools_         = std::move(candidatePoolSources);
    sourceAssets_[path] = std::move(compiledIds);
    for (const auto& id : sourceAssets_[path]) assetSources_[id] = path;
    lastLoadChanged_    = true;
    failureMessage_.clear();
    return static_cast<int>(compiled.size());
}

int DialogueFlow::reloadDnutImpl(const std::string& source, const std::string& path) {
    if (const auto cached = sourceTexts_.find(path); cached != sourceTexts_.end() && cached->second == source) {
        lastLoadChanged_ = false;
        failureMessage_.clear();
        return static_cast<int>(sourceAssets_[path].size());
    }
    std::vector<ConversationDiagnostic> candidateDiagnostics;
    auto compiledDocument = compileDnutDocument(source, path, candidateDiagnostics);
    if (!compiledDocument) {
        diagnostics_     = std::move(candidateDiagnostics);
        failureMessage_  = diagnostics_.empty() ? "conversation compilation failed" : diagnostics_.front().message;
        lastLoadChanged_ = false;
        return 0;
    }
    DnutDocument document = std::move(compiledDocument).takeValue();
    std::vector<ConversationAsset> compiled = std::move(document.conversations);
    for (const auto& asset : compiled) {
        const auto owner = assetSources_.find(asset.id);
        if (owner != assetSources_.end() && owner->second != path) {
            candidateDiagnostics.push_back({ConversationDiagnostic::Severity::Error, path, 0,
                                            "conversation '" + asset.id + "' is already owned by '" +
                                                owner->second + "'"});
            diagnostics_ = std::move(candidateDiagnostics);
            failureMessage_ = diagnostics_.back().message;
            lastLoadChanged_ = false;
            return 0;
        }
    }

    std::vector<ConversationAsset> candidate = assets_;
    if (const auto old = sourceAssets_.find(path); old != sourceAssets_.end()) {
        candidate.erase(std::remove_if(candidate.begin(), candidate.end(),
                                       [&](const auto& asset) {
                                           return std::find(old->second.begin(), old->second.end(), asset.id) !=
                                                  old->second.end();
                                       }),
                        candidate.end());
    }
    std::vector<std::string> compiledIds;
    for (auto& asset : compiled) {
        compiledIds.push_back(asset.id);
        auto existing =
            std::find_if(candidate.begin(), candidate.end(), [&](const auto& old) { return old.id == asset.id; });
        if (existing == candidate.end())
            candidate.push_back(std::move(asset));
        else
            *existing = std::move(asset);
    }
    if (!lintConversationWorkspace(candidate, path, candidateDiagnostics)) {
        diagnostics_     = std::move(candidateDiagnostics);
        failureMessage_  = diagnostics_.empty() ? "conversation workspace lint failed" : diagnostics_.front().message;
        lastLoadChanged_ = false;
        return 0;
    }

    StateValue activeState;
    const bool hadActive = runner_.isActive();
    if (hadActive) {
        auto captured = runner_.captureStateChecked();
        if (!captured) {
            failureMessage_ = captured.status().describe();
            return 0;
        }
        activeState = std::move(captured).takeValue();
    }
    std::vector<ConversationAsset> previous = assets_;
    auto candidatePoolSources = sourcePools_;
    candidatePoolSources[path] = std::move(document.poolRoot);
    DataValue candidatePoolWorkspace;
    if (!buildPoolWorkspace(candidatePoolSources, candidatePoolWorkspace, failureMessage_)) return 0;
    Dialogue* dialogue = Dialogue::create();
    if (!dialogue ||
        (dialogue->replacePoolsFromData(candidatePoolWorkspace) == 0 && !dialogue->getLastPoolsError().empty())) {
        failureMessage_ = dialogue ? dialogue->getLastPoolsError() : "dialogue pool runtime is unavailable";
        return 0;
    }
    runner_.stop();
    assets_ = std::move(candidate);
    if (hadActive) {
        StateValue  migrated = activeState;
        std::string restoreError;
        auto migratedResult = migrations_.migrate(
            migrated, [this](const std::string& id) { return find(id); });
        bool restored = migratedResult.ok();
        if (restored) {
            migrated = std::move(migratedResult).takeValue();
            auto restoreResult = runner_.restoreStateChecked(migrated);
            restored = restoreResult.ok();
            if (!restored) restoreError = restoreResult.status().describe();
        } else {
            restoreError = migratedResult.status().describe();
        }
        if (!restored) {
            assets_ = std::move(previous);
            runner_.restoreStateChecked(activeState).ignore("restore prior validated state after reload rollback");
            DataValue previousPools;
            std::string ignored;
            if (dialogue && buildPoolWorkspace(sourcePools_, previousPools, ignored))
                dialogue->replacePoolsFromData(previousPools);
            failureMessage_  = "conversation hot reload rolled back: " + restoreError;
            lastLoadChanged_ = false;
            return 0;
        }
    }
    diagnostics_        = std::move(candidateDiagnostics);
    if (const auto old = sourceAssets_.find(path); old != sourceAssets_.end())
        for (const auto& id : old->second) assetSources_.erase(id);
    sourceTexts_[path] = source;
    sourcePools_         = std::move(candidatePoolSources);
    sourceAssets_[path] = std::move(compiledIds);
    for (const auto& id : sourceAssets_[path]) assetSources_[id] = path;
    lastLoadChanged_    = true;
    failureMessage_.clear();
    return static_cast<int>(compiled.size());
}

eve::Result<void> DialogueFlow::removeSourceChecked(const std::string& path) {
    const auto source = sourceAssets_.find(path);
    if (source == sourceAssets_.end())
        return flowFailure(eve::DiagnosticCode::NotFound, "dnut source is not loaded", path);
    if (runner_.isActive()) {
        failureMessage_ = "cannot remove a dnut source while a conversation is active";
        return flowFailure(eve::DiagnosticCode::PreconditionViolation, failureMessage_, path);
    }
    auto candidatePoolSources = sourcePools_;
    candidatePoolSources.erase(path);
    DataValue poolWorkspace;
    if (!buildPoolWorkspace(candidatePoolSources, poolWorkspace, failureMessage_))
        return flowFailure(eve::DiagnosticCode::Conflict, failureMessage_, path);
    Dialogue* dialogue = Dialogue::create();
    if (!dialogue || (dialogue->replacePoolsFromData(poolWorkspace) == 0 && !dialogue->getLastPoolsError().empty())) {
        failureMessage_ = dialogue ? dialogue->getLastPoolsError() : "dialogue pool runtime is unavailable";
        return flowFailure(eve::DiagnosticCode::Failed, failureMessage_, path);
    }
    assets_.erase(std::remove_if(assets_.begin(), assets_.end(),
                                 [&](const auto& asset) {
                                     return std::find(source->second.begin(), source->second.end(), asset.id) !=
                                            source->second.end();
                                 }),
                  assets_.end());
    const std::vector<std::string> removedIds = source->second;
    sourceAssets_.erase(source);
    sourceTexts_.erase(path);
    sourcePools_ = std::move(candidatePoolSources);
    for (const auto& id : removedIds) assetSources_.erase(id);
    lastLoadChanged_ = true;
    return eve::Result<void>::success();
}

eve::Result<void> DialogueFlow::lintAllChecked() {
    diagnostics_.clear();
    bool valid = lintConversationWorkspace(assets_, "<dialogue-workspace>", diagnostics_).ok();
    for (const auto& asset : assets_) {
        for (const auto& node : asset.nodes) {
            const std::string assetPath = asset.id + "/" + node.id;
            if (node.kind == ConversationAsset::Node::Kind::Line) {
                if (!node.text.empty() && node.i18nKey.empty())
                    diagnostics_.push_back({ConversationDiagnostic::Severity::Warning, "<dialogue-workspace>",
                                            node.sourceLine, "line has display text but no localization key",
                                            "MissingLocalizationReference", node.sourceColumn, assetPath});
                if (!node.speaker.empty() && node.voice.empty())
                    diagnostics_.push_back({ConversationDiagnostic::Severity::Warning, "<dialogue-workspace>",
                                            node.sourceLine, "spoken line has no voice reference",
                                            "MissingVoiceReference", node.sourceColumn, assetPath});
            }
            if (node.kind == ConversationAsset::Node::Kind::Choice) {
                for (const auto& route : node.routes)
                    if (!route.text.empty() && route.i18nKey.empty())
                        diagnostics_.push_back({ConversationDiagnostic::Severity::Warning, "<dialogue-workspace>",
                                                route.sourceLine, "choice route has display text but no localization key",
                                                "MissingLocalizationReference", route.sourceColumn,
                                                assetPath + "/" + route.id});
            }
            if (node.kind != ConversationAsset::Node::Kind::Command) continue;
            const bool transactional = !node.payment.empty() || !node.stateMutations.empty();
            const bool legacyHandler = node.commandKind == CommandRequestKind::Operation
                                           ? static_cast<bool>(operationRequestHandler_)
                                           : static_cast<bool>(gameplayActionHandler_);
            bool handled = false;
            if (!transactional)
                handled = legacyHandler || manualCommandMode_ || static_cast<bool>(commandParticipantFactory_);
            else
                handled = (commandParticipantFactory_ || !node.stateMutations.empty()) &&
                          (node.stateMutations.empty() || stateMutationProvider_) &&
                          (node.payment.empty() || commandParticipantFactory_ || !node.stateMutations.empty());
            if (!handled) {
                diagnostics_.push_back({ConversationDiagnostic::Severity::Error, "<dialogue-workspace>",
                                        node.sourceLine, "command has no compatible runtime handler",
                                        "MissingCommandHandler", node.sourceColumn, assetPath});
                valid = false;
            }
        }
    }
    failureMessage_  = valid || diagnostics_.empty() ? std::string{} : diagnostics_.front().message;
    if (!valid) return flowFailure(eve::DiagnosticCode::InvalidArgument, failureMessage_, "<dialogue-workspace>");
    return eve::Result<void>::success();
}

eve::Result<void> DialogueFlow::renameConversationChecked(const std::string& oldId, const std::string& newId) {
    if (runner_.isActive()) {
        failureMessage_ = "cannot rename a conversation while a conversation is active";
        return flowFailure(eve::DiagnosticCode::PreconditionViolation, failureMessage_, oldId);
    }
    std::vector<ConversationAsset> candidate = assets_;
    auto renamed = renameConversationAsset(candidate, oldId, newId);
    if (!renamed) {
        failureMessage_ = renamed.status().describe();
        return eve::Result<void>::failure(renamed.status());
    }
    std::vector<ConversationDiagnostic> candidateDiagnostics;
    if (!lintConversationWorkspace(candidate, "<dialogue-workspace>", candidateDiagnostics)) {
        diagnostics_ = std::move(candidateDiagnostics);
        failureMessage_ = diagnostics_.empty() ? "conversation rename validation failed" : diagnostics_.front().message;
        return flowFailure(eve::DiagnosticCode::InvalidArgument, failureMessage_, oldId);
    }
    assets_ = std::move(candidate);
    if (const auto owner = assetSources_.find(oldId); owner != assetSources_.end()) {
        const std::string path = owner->second;
        assetSources_.erase(owner);
        assetSources_[newId] = path;
    }
    for (auto& [path, ids] : sourceAssets_)
        for (auto& id : ids)
            if (id == oldId) id = newId;
    return eve::Result<void>::success();
}

eve::Result<void> DialogueFlow::renameNodeChecked(const std::string& conversationId, const std::string& oldId,
                                                  const std::string& newId) {
    if (runner_.isActive()) {
        failureMessage_ = "cannot rename a node while a conversation is active";
        return flowFailure(eve::DiagnosticCode::PreconditionViolation, failureMessage_, conversationId + "/" + oldId);
    }
    std::vector<ConversationAsset> candidate = assets_;
    auto renamed = renameConversationNode(candidate, conversationId, oldId, newId);
    if (!renamed) {
        failureMessage_ = renamed.status().describe();
        return eve::Result<void>::failure(renamed.status());
    }
    std::vector<ConversationDiagnostic> candidateDiagnostics;
    if (!lintConversationWorkspace(candidate, "<dialogue-workspace>", candidateDiagnostics)) {
        diagnostics_ = std::move(candidateDiagnostics);
        failureMessage_ = diagnostics_.empty() ? "node rename validation failed" : diagnostics_.front().message;
        return flowFailure(eve::DiagnosticCode::InvalidArgument, failureMessage_, conversationId + "/" + oldId);
    }
    assets_ = std::move(candidate);
    return eve::Result<void>::success();
}

int DialogueFlow::loadDnutFileImpl(const std::string& path) {
    std::string source;
    if (contentReader_) {
        auto content = contentReader_(path);
        if (!content) {
            failureMessage_ = content.status().describe();
            return 0;
        }
        source = std::move(content).takeValue();
    } else {
        auto* filesystem = eve::cap::query<eve::service::IFileSystem>();
        std::vector<std::uint8_t> bytes;
        if (!filesystem || !filesystem->readFile(path, bytes)) {
            failureMessage_ = path + ": dialogue content read failed";
            return 0;
        }
        source.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    }
    return loadDnutImpl(source, path);
}

namespace {
eve::Result<int> dnutLoadResult(int count, const std::string& error, const std::string& sourceId) {
    if (count > 0 || error.empty()) return eve::Result<int>::success(count);
    return eve::Result<int>::failure(eve::Diagnostic::error(
        eve::DiagnosticCode::Failed, error.empty() ? "dnut load failed" : error, sourceId, {}, "dialogue.dnut"));
}
}  // namespace

eve::Result<int> DialogueFlow::loadDnutChecked(const std::string& source, const std::string& sourceId) {
    return dnutLoadResult(loadDnutImpl(source, sourceId), failureMessage_, sourceId);
}

eve::Result<int> DialogueFlow::reloadDnutChecked(const std::string& source, const std::string& sourceId) {
    return dnutLoadResult(reloadDnutImpl(source, sourceId), failureMessage_, sourceId);
}

eve::Result<int> DialogueFlow::loadDnutFileChecked(const std::string& path) {
    return dnutLoadResult(loadDnutFileImpl(path), failureMessage_, path);
}

eve::Result<int> DialogueFlow::mergeImported(std::vector<ConversationAsset> imported) {
    runner_.stop();
    const int count = static_cast<int>(imported.size());
    for (auto& asset : imported) {
        auto it = std::find_if(assets_.begin(), assets_.end(), [&](const auto& old) { return old.id == asset.id; });
        if (it == assets_.end())
            assets_.push_back(std::move(asset));
        else
            *it = std::move(asset);
    }
    failureMessage_.clear();
    return eve::Result<int>::success(count);
}

eve::Result<int> DialogueFlow::importYarnChecked(const std::string& source, const std::string& path) {
    diagnostics_.clear();
    auto imported = importYarnConversation(source, path, diagnostics_);
    if (!imported) {
        failureMessage_ = diagnostics_.empty() ? "Yarn import failed" : diagnostics_.front().message;
        return eve::Result<int>::failure(imported.status());
    }
    return mergeImported(std::move(imported).takeValue());
}

eve::Result<int> DialogueFlow::importTweeChecked(const std::string& source, const std::string& path) {
    diagnostics_.clear();
    auto imported = importTweeConversation(source, path, diagnostics_);
    if (!imported) {
        failureMessage_ = diagnostics_.empty() ? "Twee import failed" : diagnostics_.front().message;
        return eve::Result<int>::failure(imported.status());
    }
    return mergeImported(std::move(imported).takeValue());
}

void DialogueFlow::clear() {
    runner_.stop();
    assets_.clear();
    sourceTexts_.clear();
    sourceAssets_.clear();
    assetSources_.clear();
    sourcePools_.clear();
    if (Dialogue* dialogue = Dialogue::create())
        dialogue->replacePoolsFromData(DataValue::object({{"pools", DataValue::object({})}}));
    localization_.clear();
    migrations_.clear();
    textRenderer_.clearToneRules();
    locale_.clear();
    diagnostics_.clear();
    failureMessage_.clear();
}

int DialogueFlow::getConversationCount() const { return static_cast<int>(assets_.size()); }

std::string DialogueFlow::getConversationId(int index) const {
    return index >= 0 && static_cast<size_t>(index) < assets_.size() ? assets_[static_cast<size_t>(index)].id
                                                                     : std::string{};
}

bool DialogueFlow::hasConversation(const std::string& id) const { return find(id) != nullptr; }

std::string DialogueFlow::exportLocalizationCsv() const { return exportConversationLocalizationCsv(assets_); }

eve::Result<int> DialogueFlow::importLocalizationCsvChecked(const std::string& csv,
                                                            const std::string& defaultLocale) {
    diagnostics_.clear();
    const int count = localization_.importCsv(csv, defaultLocale, diagnostics_);
    failureMessage_ = count > 0 || diagnostics_.empty() ? std::string{} : diagnostics_.front().message;
    if (!failureMessage_.empty())
        return flowFailure<int>(eve::DiagnosticCode::ParseError, failureMessage_, "dialogue.localization.csv");
    return eve::Result<int>::success(count);
}

std::string DialogueFlow::exportMissingLocalizationCsv(const std::string& locale) const {
    return localization_.exportMissingCsv(assets_, locale);
}

std::string DialogueFlow::exportVoiceRecordingCsv(const std::string& locale) const {
    return localization_.exportVoiceRecordingCsv(assets_, locale);
}

eve::Result<int> DialogueFlow::validateLocalization(const eve::i18n::I18n& localization,
                                                    const std::string&     locale) const {
    if (locale.empty() || !localization.hasLanguage(locale))
        return eve::Result<int>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::NotFound, "dialogue localization locale is unavailable",
                                   "dialogue.localization." + locale, {}, "dialogue.localization"));
    int validated = 0;
    for (const auto& asset : assets_)
        for (const auto& node : asset.nodes) {
            if (node.i18nKey.empty()) continue;
            if (!localization.hasInLanguage(locale, node.i18nKey))
                return eve::Result<int>::failure(eve::Diagnostic::error(
                    eve::DiagnosticCode::NotFound,
                    "dialogue localization key is missing from the exact locale: " + node.i18nKey,
                    "dialogue." + asset.id + "." + node.id + ".i18n", {}, "dialogue.localization"));
            ++validated;
        }
    return eve::Result<int>::success(validated, eve::Status::success(eve::StatusCode::Ok));
}

int DialogueFlow::getDiagnosticCount() const { return static_cast<int>(diagnostics_.size()); }

std::string DialogueFlow::getDiagnosticSeverity(int index) const {
    if (index < 0 || static_cast<size_t>(index) >= diagnostics_.size()) return {};
    return diagnostics_[static_cast<size_t>(index)].severity == ConversationDiagnostic::Severity::Error ? "error"
                                                                                                        : "warning";
}

std::string DialogueFlow::getDiagnosticPath(int index) const {
    return index >= 0 && index < getDiagnosticCount() ? diagnostics_[static_cast<size_t>(index)].path : std::string{};
}

int DialogueFlow::getDiagnosticLine(int index) const {
    return index >= 0 && index < getDiagnosticCount() ? diagnostics_[static_cast<size_t>(index)].line : 0;
}

ConversationDocument* DialogueFlow::newDocument(const std::string& id) const { return new ConversationDocument(id); }

ConversationDocument* DialogueFlow::getDocument(const std::string& id) const {
    const auto* asset = find(id);
    return asset ? new ConversationDocument(*asset) : nullptr;
}

eve::Result<void> DialogueFlow::applyDocumentChecked(ConversationDocument* document) {
    if (!document) {
        failureMessage_ = "conversation document must not be null";
        return flowFailure(eve::DiagnosticCode::InvalidArgument, failureMessage_, "authoring");
    }
    std::vector<ConversationAsset> candidate = assets_;
    const auto existing = std::find_if(candidate.begin(), candidate.end(),
                                       [&](const auto& asset) { return asset.id == document->getId(); });
    if (existing == candidate.end())
        candidate.push_back(document->asset());
    else
        *existing = document->asset();
    std::vector<ConversationDiagnostic> candidateDiagnostics;
    if (!lintConversationWorkspace(candidate, "authoring", candidateDiagnostics)) {
        diagnostics_ = std::move(candidateDiagnostics);
        failureMessage_ =
            diagnostics_.empty() ? "conversation document validation failed" : diagnostics_.front().message;
        return flowFailure(eve::DiagnosticCode::InvalidArgument, failureMessage_, "authoring");
    }
    assets_ = std::move(candidate);
    diagnostics_.clear();
    failureMessage_.clear();
    return eve::Result<void>::success();
}

int DialogueFlow::getDiagnosticColumn(int index) const {
    return index >= 0 && index < getDiagnosticCount() ? diagnostics_[static_cast<size_t>(index)].column : 0;
}

std::string DialogueFlow::getDiagnosticCode(int index) const {
    return index >= 0 && index < getDiagnosticCount() ? diagnostics_[static_cast<size_t>(index)].code : std::string{};
}

std::string DialogueFlow::getDiagnosticAssetPath(int index) const {
    return index >= 0 && index < getDiagnosticCount() ? diagnostics_[static_cast<size_t>(index)].assetPath
                                                       : std::string{};
}

std::string DialogueFlow::getDiagnosticMessage(int index) const {
    return index >= 0 && static_cast<size_t>(index) < diagnostics_.size()
               ? diagnostics_[static_cast<size_t>(index)].message
               : std::string{};
}

eve::Result<void> DialogueFlow::startChecked(const std::string& id, ssq::Object bindings) {
    StateValue converted = StateValue::object();
    if (vm_) {
        const SQInteger top = sq_gettop(vm_);
        sq_pushobject(vm_, bindings.getRaw());
        const bool ok = squirrelToState(vm_, -1, converted) && converted.isObject();
        sq_settop(vm_, top);
        if (!ok) {
            failureMessage_ = "conversation bindings must be a scalar-only table";
            return dialogueFailure(eve::DiagnosticCode::InvalidArgument, failureMessage_, "dialogue.bindings");
        }
    }
    const ConversationAsset* asset = find(id);
    if (!asset)
        return dialogueFailure(eve::DiagnosticCode::NotFound, "conversation was not found: " + id, "dialogue." + id);
    auto started = runner_.startChecked(asset, std::move(converted));
    if (!started) return eve::Result<void>::failure(started.status());
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> DialogueFlow::advanceChecked() {
    auto advanced = runner_.advanceChecked();
    if (!advanced) return eve::Result<void>::failure(advanced.status());
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> DialogueFlow::resumeCommandChecked(const std::string& requestId, eve::Value value) {
    return runner_.resumeCommand(requestId, std::move(value));
}

eve::Result<void> DialogueFlow::select(const std::string& routeId) {
    const auto* node = runner_.currentNode();
    if (!node) return runner_.selectRouteForTransaction(routeId);
    const ConversationRoute* route = nullptr;
    for (const auto& candidate : node->routes) {
        if (candidate.first == routeId) {
            route = &candidate;
            break;
        }
    }
    if (!route || (route->payment.empty() && route->stateMutations.empty()))
        return runner_.selectRouteForTransaction(routeId);
    if (!stateMutationProvider_ && !route->stateMutations.empty()) {
        return dialogueFailure(eve::DiagnosticCode::Unsupported,
                               "dialogue choice state mutations require a StatePatch-compatible provider",
                               "route.stateMutations");
    }

    auto captured = runner_.captureStateChecked();
    if (!captured) return eve::Result<void>::failure(captured.status());
    StateValue before = std::move(captured).takeValue();
    DialogueSelectionParticipant                            selection(runner_, routeId, std::move(before));
    std::unique_ptr<DialogueStateMutationParticipant>       state;
    std::vector<eve::transaction::ITransactionParticipant*> effects{&selection};
    if (!route->stateMutations.empty()) {
        state = std::make_unique<DialogueStateMutationParticipant>(*stateMutationProvider_, route->stateMutations);
        effects.push_back(state.get());
    }
    auto committed = paymentAdapter_.execute(nextTransactionId("choice"), route->payment, effects);
    if (!committed) return eve::Result<void>::failure(committed.status());
    (void)std::move(committed).takeValue();
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

std::string DialogueFlow::nextTransactionId(const char* purpose) {
    return std::string("dialogue-") + purpose + "-" + std::to_string(transactionSequence_++);
}

CommandResponse DialogueFlow::dispatchCommand(const CommandRequest& request) {
    const bool             transactional = !request.payment.empty() || !request.stateMutations.empty();
    CommandRequestHandler* legacy =
        request.kind == CommandRequestKind::Operation ? &operationRequestHandler_ : &gameplayActionHandler_;
    if (!transactional && *legacy) return (*legacy)(request);

    if (!transactional && manualCommandMode_) {
        CommandResponse response;
        response.status = CommandResponse::Status::Blocked;
        return response;
    }

    if (!transactional && !commandParticipantFactory_) {
        CommandResponse response;
        response.status = CommandResponse::Status::Failed;
        response.error  = request.kind == CommandRequestKind::Operation
                              ? "dialogue operation handler is not configured"
                              : "dialogue gameplay-action handler is not configured";
        return response;
    }

    if (!request.payment.empty() && !commandParticipantFactory_ && request.stateMutations.empty()) {
        CommandResponse response;
        response.status = CommandResponse::Status::Failed;
        response.error  = "payment-bearing dialogue command requires an Action/operation participant";
        return response;
    }

    std::vector<std::unique_ptr<eve::transaction::ITransactionParticipant>> owned;
    std::vector<eve::transaction::ITransactionParticipant*>                 effects;
    if (commandParticipantFactory_) {
        auto participant = commandParticipantFactory_(request);
        if (!participant) {
            CommandResponse response;
            response.status = CommandResponse::Status::Failed;
            response.error  = participant.status().describe();
            return response;
        }
        auto action = std::move(participant).takeValue();
        if (!action) {
            CommandResponse response;
            response.status = CommandResponse::Status::Failed;
            response.error  = "dialogue command participant factory returned null";
            return response;
        }
        effects.push_back(action.get());
        owned.push_back(std::move(action));
    }
    if (!request.stateMutations.empty()) {
        if (!stateMutationProvider_) {
            CommandResponse response;
            response.status = CommandResponse::Status::Failed;
            response.error  = "dialogue command state mutations require a StatePatch-compatible provider";
            return response;
        }
        auto state =
            std::make_unique<DialogueStateMutationParticipant>(*stateMutationProvider_, request.stateMutations);
        effects.push_back(state.get());
        owned.push_back(std::move(state));
    }
    auto committed = paymentAdapter_.execute(nextTransactionId("command"), request.payment, effects);
    if (!committed) {
        CommandResponse response;
        response.status = CommandResponse::Status::Failed;
        response.error  = committed.status().describe();
        return response;
    }
    (void)std::move(committed).takeValue();
    CommandResponse response;
    response.status = CommandResponse::Status::Completed;
    return response;
}

std::string DialogueFlow::getConversationId() const { return runner_.asset() ? runner_.asset()->id : std::string{}; }

std::string DialogueFlow::getNodeKind() const {
    const auto* node = runner_.currentNode();
    return node ? kindName(node->kind) : std::string{};
}

#define EVE_FLOW_NODE_STRING(method, field)        \
    std::string DialogueFlow::method() const {     \
        const auto* node = runner_.currentNode();  \
        return node ? node->field : std::string{}; \
    }
EVE_FLOW_NODE_STRING(getSpeaker, speaker)
EVE_FLOW_NODE_STRING(getPool, pool)
EVE_FLOW_NODE_STRING(getI18nKey, i18nKey)
#undef EVE_FLOW_NODE_STRING

std::string DialogueFlow::getText() {
    const auto* node = runner_.currentNode();
    if (!node) return {};
    const std::string localized = localization_.resolveText(node->i18nKey, locale_, node->text);
    return textRenderer_.render(localized, runner_.bindings(), runner_.locals(), [this](const std::string& rule) {
        auto result = evaluate(rule, runner_.bindings(), runner_.locals());
        if (!result) {
            failureMessage_ = result.status().describe();
            return false;
        }
        StateValue value = std::move(result).takeValue();
        return value.isBool() && value.asBool();
    });
}

std::string DialogueFlow::getVoice() const {
    const auto* node = runner_.currentNode();
    return node ? localization_.resolveVoice(node->i18nKey, locale_, node->voice) : std::string{};
}

std::string DialogueFlow::getVoiceStatus() const {
    const auto* node = runner_.currentNode();
    return node ? localization_.resolveStatus(node->i18nKey, locale_) : std::string{};
}

float DialogueFlow::getVoiceDuration() const {
    const auto* node = runner_.currentNode();
    return node ? static_cast<float>(localization_.resolveDuration(node->i18nKey, locale_)) : 0.0F;
}

int DialogueFlow::getRouteCount() const {
    const auto* node = runner_.currentNode();
    return node ? static_cast<int>(node->routes.size()) : 0;
}

std::string DialogueFlow::getRouteId(int index) const {
    const auto* node = runner_.currentNode();
    return node && index >= 0 && static_cast<size_t>(index) < node->routes.size()
               ? node->routes[static_cast<size_t>(index)].id
               : std::string{};
}

eve::Result<void> DialogueFlow::setExpressionEvaluatorChecked(ssq::Object fn) {
    if (!vm_ || fn.getRaw()._type != OT_CLOSURE)
        return flowFailure(eve::DiagnosticCode::InvalidArgument,
                           "dialogue expression evaluator must be a closure owned by the active VM",
                           "dialogue.expressionEvaluator");
    clearExpressionEvaluator();
    evaluator_ = fn.getRaw();
    sq_addref(vm_, &evaluator_);
    hasEvaluator_ = true;
    return eve::Result<void>::success();
}

void DialogueFlow::clearExpressionEvaluator() {
    if (vm_ && hasEvaluator_) sq_release(vm_, &evaluator_);
    evaluator_    = {};
    hasEvaluator_ = false;
}

eve::Result<StateValue> DialogueFlow::evaluate(const std::string& expression, const StateValue& bindings,
                                               const StateValue& locals) {
    if (expression == "else") return eve::Result<StateValue>::success(StateValue::boolean(true));
    if (!vm_ || !hasEvaluator_)
        return eve::Result<StateValue>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Unsupported, "dialogue expression evaluator is not configured", "expression", {},
            "dialogue.expression"));
    const SQInteger top = sq_gettop(vm_);
    sq_pushobject(vm_, evaluator_);
    sq_pushroottable(vm_);
    sq_newtable(vm_);
    sq_pushstring(vm_, "expression", -1);
    sq_pushstring(vm_, expression.c_str(), expression.size());
    sq_newslot(vm_, -3, SQFalse);
    sq_pushstring(vm_, "bindings", -1);
    pushState(vm_, bindings);
    sq_newslot(vm_, -3, SQFalse);
    sq_pushstring(vm_, "locals", -1);
    pushState(vm_, locals);
    sq_newslot(vm_, -3, SQFalse);
    if (SQ_FAILED(sq_call(vm_, 2, SQTrue, SQTrue))) {
        sq_settop(vm_, top);
        return eve::Result<StateValue>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Failed, "dialogue expression evaluator raised an exception", "expression", {},
            "dialogue.expression"));
    }
    StateValue result;
    if (!squirrelToState(vm_, -1, result)) {
        sq_settop(vm_, top);
        return eve::Result<StateValue>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "dialogue expression returned an unsupported value", "expression",
            {}, "dialogue.expression"));
    }
    sq_settop(vm_, top);
    return eve::Result<StateValue>::success(std::move(result));
}

std::string DialogueFlow::getRouteText(int index) const {
    const auto* node = runner_.currentNode();
    if (!node || index < 0 || static_cast<size_t>(index) >= node->routes.size()) return {};
    const auto& route = node->routes[static_cast<size_t>(index)];
    return localization_.resolveText(route.i18nKey, locale_, route.text.empty() ? route.id : route.text);
}

void DialogueFlow::setManualCommandMode(bool enabled) {
    manualCommandMode_ = enabled;
    if (operationRequestHandler_ || gameplayActionHandler_ || commandParticipantFactory_ || stateMutationProvider_ ||
        manualCommandMode_)
        runner_.setCommandRequestDispatcher([this](const CommandRequest& request) { return dispatchCommand(request); });
    else
        runner_.clearCommandRequestDispatcher();
}

eve::Result<void> DialogueFlow::restoreStateChecked(const StateValue& in) {
    return runner_.restoreStateChecked(in);
}

eve::Result<std::string> DialogueFlow::captureStateJsonChecked() const {
    auto captured = runner_.captureStateChecked();
    if (!captured) return eve::Result<std::string>::failure(captured.status());
    return conversationStateToJson(std::move(captured).takeValue());
}

eve::Result<void> DialogueFlow::restoreStateJsonChecked(const std::string& json) {
    auto parsed = conversationStateFromJson(json);
    if (!parsed) return eve::Result<void>::failure(parsed.status());
    auto migrated = migrations_.migrate(std::move(parsed).takeValue(),
                                        [this](const std::string& id) { return find(id); });
    if (!migrated) return eve::Result<void>::failure(migrated.status());
    return runner_.restoreStateChecked(std::move(migrated).takeValue());
}

eve::Result<void> DialogueFlow::registerMigrationChecked(const std::string& assetId, int fromVersion,
                                                         const std::string& currentAssetId,
                                                         const std::string& nodeMap) {
    return migrations_.registerMigration(assetId, fromVersion, currentAssetId, nodeMap);
}

void DialogueFlow::addToneRule(const std::string& expression, const std::string& prefix, const std::string& suffix,
                               const std::string& find, const std::string& replacement) {
    textRenderer_.addToneRule(expression, prefix, suffix, find, replacement);
}

void DialogueFlow::expose(ssq::Table& table) {
    if (DialogueFlow* self = DialogueFlow::create()) self->vm_ = table.getHandle();
    auto document = table.addClass<ConversationDocument>(
        "ConversationDocument",
        std::function<ConversationDocument*()>([]() -> ConversationDocument* { return new ConversationDocument(); }),
        true);
    document.addFunc("getId", &ConversationDocument::getId);
    document.addFunc("setId", &ConversationDocument::setId);
    document.addFunc("getVersion", &ConversationDocument::getVersion);
    document.addFunc("setVersion", &ConversationDocument::setVersion);
    document.addFunc("getEntry", &ConversationDocument::getEntry);
    document.addFunc("setEntry", &ConversationDocument::setEntry);
    document.addFunc("getParameterCount", &ConversationDocument::getParameterCount);
    document.addFunc("getParameter", &ConversationDocument::getParameter);
    document.addFunc("addParameter", &ConversationDocument::addParameter);
    document.addFunc("removeParameter", &ConversationDocument::removeParameter);
    document.addFunc("getNodeCount", &ConversationDocument::getNodeCount);
    document.addFunc("getNodeId", &ConversationDocument::getNodeId);
    document.addFunc("hasNode", &ConversationDocument::hasNode);
    document.addFunc("addNode", &ConversationDocument::addNode);
    document.addFunc("removeNode", &ConversationDocument::removeNode);
    document.addFunc("renameNode", &ConversationDocument::renameNode);
    document.addFunc("getNodeKind", &ConversationDocument::getNodeKind);
    document.addFunc("setNodeKind", &ConversationDocument::setNodeKind);
    document.addFunc("getFieldCount", &ConversationDocument::getFieldCount);
    document.addFunc("getFieldName", &ConversationDocument::getFieldName);
    document.addFunc("getFieldKind", &ConversationDocument::getFieldKind);
    document.addFunc("getField", &ConversationDocument::getField);
    document.addFunc("setField", &ConversationDocument::setField);
    document.addFunc("getRouteCount", &ConversationDocument::getRouteCount);
    document.addFunc("getRouteLabel", &ConversationDocument::getRouteLabel);
    document.addFunc("getRouteTarget", &ConversationDocument::getRouteTarget);
    document.addFunc("addRoute", &ConversationDocument::addRoute);
    document.addFunc("setRoute", &ConversationDocument::setRoute);
    document.addFunc("removeRoute", &ConversationDocument::removeRoute);
    document.addFunc("validate", &ConversationDocument::validate);
    document.addFunc("getDiagnosticCount", &ConversationDocument::getDiagnosticCount);
    document.addFunc("getDiagnosticSeverity", &ConversationDocument::getDiagnosticSeverity);
    document.addFunc("getDiagnosticPath", &ConversationDocument::getDiagnosticPath);
    document.addFunc("getDiagnosticLine", &ConversationDocument::getDiagnosticLine);
    document.addFunc("getDiagnosticMessage", &ConversationDocument::getDiagnosticMessage);
    auto cls = table.addClass(name, DialogueFlow::create, false);
    expose(cls);
}

void DialogueFlow::expose(ssq::Class& cls) {
    cls.addFunc("getName", &DialogueFlow::getName);
    const auto projectCount = [](int count) { return eve::Value(count); };
    cls.addFunc("loadDnutChecked", [vm = cls.getHandle(), projectCount](DialogueFlow* value,
                                                                        const std::string& source,
                                                                        const std::string& sourceId) {
        return eve::script::projectResult(vm, value->loadDnutChecked(source, sourceId), projectCount);
    });
    cls.addFunc("reloadDnutChecked", [vm = cls.getHandle(), projectCount](DialogueFlow* value,
                                                                          const std::string& source,
                                                                          const std::string& sourceId) {
        return eve::script::projectResult(vm, value->reloadDnutChecked(source, sourceId), projectCount);
    });
    cls.addFunc("loadDnutFileChecked", [vm = cls.getHandle(), projectCount](DialogueFlow* value,
                                                                            const std::string& path) {
        return eve::script::projectResult(vm, value->loadDnutFileChecked(path), projectCount);
    });
    cls.addFunc("importYarnChecked", [vm = cls.getHandle(), projectCount](DialogueFlow* value,
                                                                          const std::string& source,
                                                                          const std::string& path) {
        return eve::script::projectResult(vm, value->importYarnChecked(source, path), projectCount);
    });
    cls.addFunc("importTweeChecked", [vm = cls.getHandle(), projectCount](DialogueFlow* value,
                                                                          const std::string& source,
                                                                          const std::string& path) {
        return eve::script::projectResult(vm, value->importTweeChecked(source, path), projectCount);
    });
    cls.addFunc("removeSourceChecked", [vm = cls.getHandle()](DialogueFlow* value, const std::string& path) {
        return eve::script::projectResult(vm, value->removeSourceChecked(path));
    });
    cls.addFunc("lintAllChecked", [vm = cls.getHandle()](DialogueFlow* value) {
        return eve::script::projectResult(vm, value->lintAllChecked());
    });
    cls.addFunc("renameConversationChecked", [vm = cls.getHandle()](DialogueFlow* value,
                                                                     const std::string& oldId,
                                                                     const std::string& newId) {
        return eve::script::projectResult(vm, value->renameConversationChecked(oldId, newId));
    });
    cls.addFunc("renameNodeChecked", [vm = cls.getHandle()](DialogueFlow* value,
                                                             const std::string& conversationId,
                                                             const std::string& oldId,
                                                             const std::string& newId) {
        return eve::script::projectResult(vm, value->renameNodeChecked(conversationId, oldId, newId));
    });
    cls.addFunc("getLastLoadChanged", &DialogueFlow::getLastLoadChanged);
    cls.addFunc("clear", &DialogueFlow::clear);
    cls.addFunc("getConversationCount", &DialogueFlow::getConversationCount);
    cls.addFunc("getConversationId",
                static_cast<std::string (DialogueFlow::*)(int) const>(&DialogueFlow::getConversationId));
    cls.addFunc("hasConversation", &DialogueFlow::hasConversation);
    cls.addFunc("exportLocalizationCsv", &DialogueFlow::exportLocalizationCsv);
    cls.addFunc("importLocalizationCsvChecked", [vm = cls.getHandle()](DialogueFlow* value, const std::string& csv,
                                                                        const std::string& defaultLocale) {
        return eve::script::projectResult(vm, value->importLocalizationCsvChecked(csv, defaultLocale),
                                          [](int count) { return eve::Value(count); });
    });
    cls.addFunc("exportMissingLocalizationCsv", &DialogueFlow::exportMissingLocalizationCsv);
    cls.addFunc("exportVoiceRecordingCsv", &DialogueFlow::exportVoiceRecordingCsv);
    cls.addFunc("validateLocalization", [vm = cls.getHandle()](DialogueFlow* value, eve::i18n::I18n* localization,
                                                               const std::string& locale) {
        if (!value || !localization)
            return eve::script::projectResult(
                vm,
                eve::Result<int>::failure(eve::Diagnostic::error(
                    eve::DiagnosticCode::InvalidArgument, "validateLocalization requires dialogue and i18n modules",
                    "dialogue.localization", {}, "dialogue.squirrel")),
                [](int count) { return eve::Value(count); });
        return eve::script::projectResult(vm, value->validateLocalization(*localization, locale),
                                          [](int count) { return eve::Value(count); });
    });
    cls.addFunc("setLocale", &DialogueFlow::setLocale);
    cls.addFunc("getLocale", &DialogueFlow::getLocale);
    cls.addFunc("getDiagnosticCount", &DialogueFlow::getDiagnosticCount);
    cls.addFunc("getDiagnosticSeverity", &DialogueFlow::getDiagnosticSeverity);
    cls.addFunc("getDiagnosticPath", &DialogueFlow::getDiagnosticPath);
    cls.addFunc("getDiagnosticLine", &DialogueFlow::getDiagnosticLine);
    cls.addFunc("getDiagnosticColumn", &DialogueFlow::getDiagnosticColumn);
    cls.addFunc("getDiagnosticCode", &DialogueFlow::getDiagnosticCode);
    cls.addFunc("getDiagnosticAssetPath", &DialogueFlow::getDiagnosticAssetPath);
    cls.addFunc("getDiagnosticMessage", &DialogueFlow::getDiagnosticMessage);
    cls.addFunc("newDocument", &DialogueFlow::newDocument);
    cls.addFunc("getDocument", &DialogueFlow::getDocument);
    cls.addFunc("applyDocumentChecked", [vm = cls.getHandle()](DialogueFlow* value,
                                                                ConversationDocument* document) {
        return eve::script::projectResult(vm, value->applyDocumentChecked(document));
    });
    cls.addFunc(
        "startChecked", [vm = cls.getHandle()](DialogueFlow* value, const std::string& id, ssq::Object bindings) {
            if (!value)
                return eve::script::projectResult(vm, dialogueFailure(eve::DiagnosticCode::InvalidArgument,
                                                                      "dialogue flow must not be null", "dialogue"));
            return eve::script::projectResult(vm, value->startChecked(id, std::move(bindings)));
        });
    cls.addFunc("advanceChecked", [vm = cls.getHandle()](DialogueFlow* value) {
        if (!value)
            return eve::script::projectResult(vm, dialogueFailure(eve::DiagnosticCode::InvalidArgument,
                                                                  "dialogue flow must not be null", "dialogue"));
        return eve::script::projectResult(vm, value->advanceChecked());
    });
    cls.addFunc("resumeCommandChecked", [vm = cls.getHandle()](DialogueFlow* value,
                                                               const std::string& requestId,
                                                               ssq::Object result) {
        if (!value)
            return eve::script::projectResult(vm, dialogueFailure(eve::DiagnosticCode::InvalidArgument,
                                                                  "dialogue flow must not be null", "dialogue"));
        auto converted = eve::script::valueFromSquirrel(result, {.source = "dialogue.resumeCommand"});
        if (!converted)
            return eve::script::projectResult(vm, eve::Result<void>::failure(converted.status()));
        return eve::script::projectResult(vm,
                                          value->resumeCommandChecked(requestId, std::move(converted).takeValue()));
    });
    cls.addFunc("select", [vm = cls.getHandle()](DialogueFlow* value, const std::string& routeId) {
        if (!value)
            return eve::script::projectResult(vm, dialogueFailure(eve::DiagnosticCode::InvalidArgument,
                                                                  "dialogue flow must not be null", "dialogue"));
        return eve::script::projectResult(vm, value->select(routeId));
    });
    cls.addFunc("isActive", &DialogueFlow::isActive);
    cls.addFunc("isBlocked", &DialogueFlow::isBlocked);
    cls.addFunc("getActiveConversationId",
                static_cast<std::string (DialogueFlow::*)() const>(&DialogueFlow::getConversationId));
    cls.addFunc("getNodeId", &DialogueFlow::getNodeId);
    cls.addFunc("getPendingCommandRequestId", &DialogueFlow::getPendingCommandRequestId);
    cls.addFunc("getNodeKind", &DialogueFlow::getNodeKind);
    cls.addFunc("getSpeaker", &DialogueFlow::getSpeaker);
    cls.addFunc("getText", &DialogueFlow::getText);
    cls.addFunc("getPool", &DialogueFlow::getPool);
    cls.addFunc("getI18nKey", &DialogueFlow::getI18nKey);
    cls.addFunc("getVoice", &DialogueFlow::getVoice);
    cls.addFunc("getVoiceStatus", &DialogueFlow::getVoiceStatus);
    cls.addFunc("getVoiceDuration", &DialogueFlow::getVoiceDuration);
    cls.addFunc("getRouteCount", &DialogueFlow::getRouteCount);
    cls.addFunc("getRouteId", &DialogueFlow::getRouteId);
    cls.addFunc("getRouteText", &DialogueFlow::getRouteText);
    cls.addFunc("setManualCommandMode", &DialogueFlow::setManualCommandMode);
    cls.addFunc("setExpressionEvaluatorChecked", [vm = cls.getHandle()](DialogueFlow* value, ssq::Object fn) {
        return eve::script::projectResult(vm, value->setExpressionEvaluatorChecked(fn));
    });
    cls.addFunc("clearExpressionEvaluator", &DialogueFlow::clearExpressionEvaluator);
    cls.addFunc("captureStateJsonChecked", [vm = cls.getHandle()](DialogueFlow* value) {
        return eve::script::projectResult(vm, value->captureStateJsonChecked(),
                                          [](std::string json) { return eve::Value(std::move(json)); });
    });
    cls.addFunc("restoreStateJsonChecked", [vm = cls.getHandle()](DialogueFlow* value, const std::string& json) {
        return eve::script::projectResult(vm, value->restoreStateJsonChecked(json));
    });
    cls.addFunc("registerMigrationChecked", [vm = cls.getHandle()](DialogueFlow* value,
                                                                    const std::string& assetId, int fromVersion,
                                                                    const std::string& currentAssetId,
                                                                    const std::string& nodeMap) {
        return eve::script::projectResult(
            vm, value->registerMigrationChecked(assetId, fromVersion, currentAssetId, nodeMap));
    });
    cls.addFunc("clearMigrations", &DialogueFlow::clearMigrations);
    cls.addFunc("addToneRule", &DialogueFlow::addToneRule);
    cls.addFunc("clearToneRules", &DialogueFlow::clearToneRules);

    // 把对话运行器发布到共享玩法协议（`eve_gameplay` / MCP）。返回 {ok, message}：
    // 失败原因（非规范持久 id、运行器已发布）不被丢弃。
    cls.addFunc("publishGameplay",
                [vm = cls.getHandle()](DialogueFlow* self, const std::string& instanceId, const std::string& ownerId) {
                    ssq::Table result(vm);
                    if (self == nullptr) {
                        result.set("ok", false);
                        result.set("message", std::string("dialogue flow unavailable"));
                        return result;
                    }
                    const auto published = self->publishGameplay(instanceId, ownerId);
                    result.set("ok", published.ok());
                    result.set("message", published.ok() ? std::string("published") : published.status().describe());
                    return result;
                });
    cls.addFunc("unpublishGameplay", [vm = cls.getHandle()](DialogueFlow* self, const std::string& instanceId) {
        ssq::Table result(vm);
        const auto unpublished = self == nullptr
                                     ? eve::Result<void>::failure(eve::Diagnostic::error(
                                           eve::DiagnosticCode::Failed, "dialogue flow unavailable", "self"))
                                     : self->unpublishGameplay(instanceId);
        result.set("ok", unpublished.ok());
        result.set("message", unpublished.ok() ? std::string("unpublished") : unpublished.status().describe());
        return result;
    });
    cls.addFunc("clearGameplayControls", &DialogueFlow::clearGameplayControls);
    cls.addFunc("getGameplayControlCount", &DialogueFlow::gameplayControlCount);
}

}  // namespace eve::dialogue
