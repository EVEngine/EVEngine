#include "dialogue/DialogueFlow.h"

#include "dialogue/ConversationImporter.h"
#include "dialogue/ConversationToolchain.h"
#include "filesystem/Filesystem.h"

#include <algorithm>
#include <simplesquirrel/simplesquirrel.hpp>

namespace eve::dialogue {

Module_IMPL(DialogueFlow, new DialogueFlow());

namespace {

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

}  // namespace

DialogueFlow::DialogueFlow() {
    runner_.setAssetResolver([this](const std::string& id) { return find(id); });
    runner_.setExpressionEvaluator([this](const std::string& expression, const StateValue& bindings,
                                          const StateValue& locals) { return evaluate(expression, bindings, locals); });
}

DialogueFlow::~DialogueFlow() { clearExpressionEvaluator(); }

const ConversationAsset* DialogueFlow::find(const std::string& id) const {
    for (const auto& asset : assets_)
        if (asset.id == id) return &asset;
    return nullptr;
}

int DialogueFlow::loadFromDnut(const std::string& source, const std::string& path) {
    const size_t hash = std::hash<std::string>{}(source);
    if (const auto it = sourceHashes_.find(path); it != sourceHashes_.end() && it->second == hash) {
        lastLoadChanged_ = false;
        lastError_.clear();
        return static_cast<int>(sourceAssets_[path].size());
    }
    runner_.stop();
    std::vector<ConversationAsset> compiled;
    diagnostics_.clear();
    if (!compileDnutConversations(source, path, compiled, diagnostics_)) {
        lastError_ = diagnostics_.empty() ? "conversation compilation failed" : diagnostics_.front().message;
        return 0;
    }
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
    sourceHashes_[path] = hash;
    sourceAssets_[path] = std::move(compiledIds);
    lastLoadChanged_    = true;
    lastError_.clear();
    return static_cast<int>(compiled.size());
}

bool DialogueFlow::removeSource(const std::string& path) {
    const auto source = sourceAssets_.find(path);
    if (source == sourceAssets_.end()) return false;
    runner_.stop();
    assets_.erase(std::remove_if(assets_.begin(), assets_.end(),
                                 [&](const auto& asset) {
                                     return std::find(source->second.begin(), source->second.end(), asset.id) !=
                                            source->second.end();
                                 }),
                  assets_.end());
    sourceAssets_.erase(source);
    sourceHashes_.erase(path);
    lastLoadChanged_ = true;
    return true;
}

bool DialogueFlow::lintAll() {
    diagnostics_.clear();
    const bool valid = lintConversationWorkspace(assets_, "<dialogue-workspace>", diagnostics_);
    lastError_       = valid || diagnostics_.empty() ? std::string{} : diagnostics_.front().message;
    return valid;
}

bool DialogueFlow::renameConversation(const std::string& oldId, const std::string& newId) {
    runner_.stop();
    if (!renameConversationAsset(assets_, oldId, newId, &lastError_)) return false;
    for (auto& [path, ids] : sourceAssets_)
        for (auto& id : ids)
            if (id == oldId) id = newId;
    return true;
}

bool DialogueFlow::renameNode(const std::string& conversationId, const std::string& oldId, const std::string& newId) {
    runner_.stop();
    return renameConversationNode(assets_, conversationId, oldId, newId, &lastError_);
}

int DialogueFlow::loadFromDnutFile(const std::string& path) {
    auto* filesystem = eve::ModuleManager::getInstance<eve::filesystem::Filesystem>("Filesystem");
    if (!filesystem) filesystem = eve::filesystem::Filesystem::create();
    eve::filesystem::FileData* data = nullptr;
    try {
        data = filesystem->read(path);
    } catch (...) {
        delete data;
        lastError_ = path + ": read failed";
        return 0;
    }
    if (!data || !data->getData()) {
        delete data;
        lastError_ = path + ": read failed";
        return 0;
    }
    const std::string text(static_cast<const char*>(data->getData()), data->getSize());
    delete data;
    return loadFromDnut(text, path);
}

int DialogueFlow::mergeImported(std::vector<ConversationAsset> imported) {
    runner_.stop();
    const int count = static_cast<int>(imported.size());
    for (auto& asset : imported) {
        auto it = std::find_if(assets_.begin(), assets_.end(), [&](const auto& old) { return old.id == asset.id; });
        if (it == assets_.end())
            assets_.push_back(std::move(asset));
        else
            *it = std::move(asset);
    }
    lastError_.clear();
    return count;
}

int DialogueFlow::importYarn(const std::string& source, const std::string& path) {
    std::vector<ConversationAsset> imported;
    diagnostics_.clear();
    if (!importYarnConversation(source, path, imported, diagnostics_)) {
        lastError_ = diagnostics_.empty() ? "Yarn import failed" : diagnostics_.front().message;
        return 0;
    }
    return mergeImported(std::move(imported));
}

int DialogueFlow::importTwee(const std::string& source, const std::string& path) {
    std::vector<ConversationAsset> imported;
    diagnostics_.clear();
    if (!importTweeConversation(source, path, imported, diagnostics_)) {
        lastError_ = diagnostics_.empty() ? "Twee import failed" : diagnostics_.front().message;
        return 0;
    }
    return mergeImported(std::move(imported));
}

void DialogueFlow::clear() {
    runner_.stop();
    assets_.clear();
    sourceHashes_.clear();
    sourceAssets_.clear();
    localization_.clear();
    migrations_.clear();
    locale_.clear();
    diagnostics_.clear();
    lastError_.clear();
}

int DialogueFlow::getConversationCount() const { return static_cast<int>(assets_.size()); }

std::string DialogueFlow::getConversationId(int index) const {
    return index >= 0 && static_cast<size_t>(index) < assets_.size() ? assets_[static_cast<size_t>(index)].id
                                                                     : std::string{};
}

bool DialogueFlow::hasConversation(const std::string& id) const { return find(id) != nullptr; }

std::string DialogueFlow::exportLocalizationCsv() const { return exportConversationLocalizationCsv(assets_); }

int DialogueFlow::importLocalizationCsv(const std::string& csv, const std::string& defaultLocale) {
    diagnostics_.clear();
    const int count = localization_.importCsv(csv, defaultLocale, diagnostics_);
    lastError_      = count > 0 || diagnostics_.empty() ? std::string{} : diagnostics_.front().message;
    return count;
}

std::string DialogueFlow::exportMissingLocalizationCsv(const std::string& locale) const {
    return localization_.exportMissingCsv(assets_, locale);
}

std::string DialogueFlow::exportVoiceRecordingCsv(const std::string& locale) const {
    return localization_.exportVoiceRecordingCsv(assets_, locale);
}

int DialogueFlow::getDiagnosticCount() const { return static_cast<int>(diagnostics_.size()); }

std::string DialogueFlow::getDiagnosticSeverity(int index) const {
    if (index < 0 || static_cast<size_t>(index) >= diagnostics_.size()) return {};
    return diagnostics_[static_cast<size_t>(index)].severity == ConversationDiagnostic::Severity::Error ? "error"
                                                                                                        : "warning";
}

std::string DialogueFlow::getDiagnosticMessage(int index) const {
    return index >= 0 && static_cast<size_t>(index) < diagnostics_.size()
               ? diagnostics_[static_cast<size_t>(index)].message
               : std::string{};
}

bool DialogueFlow::start(const std::string& id, ssq::Object bindings) {
    StateValue converted = StateValue::object();
    if (vm_) {
        const SQInteger top = sq_gettop(vm_);
        sq_pushobject(vm_, bindings.getRaw());
        const bool ok = squirrelToState(vm_, -1, converted) && converted.isObject();
        sq_settop(vm_, top);
        if (!ok) {
            lastError_ = "conversation bindings must be a scalar-only table";
            return false;
        }
    }
    const ConversationAsset* asset = find(id);
    return runner_.start(asset, std::move(converted), &lastError_);
}

bool DialogueFlow::advance() { return runner_.advance(&lastError_); }
bool DialogueFlow::select(const std::string& routeId) { return runner_.select(routeId, &lastError_); }

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

std::string DialogueFlow::getText() const {
    const auto* node = runner_.currentNode();
    return node ? localization_.resolveText(node->i18nKey, locale_, node->text) : std::string{};
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
               ? node->routes[static_cast<size_t>(index)].first
               : std::string{};
}

bool DialogueFlow::setExpressionEvaluator(ssq::Object fn) {
    if (!vm_ || fn.getRaw()._type != OT_CLOSURE) return false;
    clearExpressionEvaluator();
    evaluator_ = fn.getRaw();
    sq_addref(vm_, &evaluator_);
    hasEvaluator_ = true;
    return true;
}

void DialogueFlow::clearExpressionEvaluator() {
    if (vm_ && hasEvaluator_) sq_release(vm_, &evaluator_);
    evaluator_    = {};
    hasEvaluator_ = false;
}

StateValue DialogueFlow::evaluate(const std::string& expression, const StateValue& bindings, const StateValue& locals) {
    if (expression == "else") return StateValue::boolean(true);
    if (!vm_ || !hasEvaluator_) return StateValue::boolean(false);
    const SQInteger top = sq_gettop(vm_);
    sq_pushobject(vm_, evaluator_);
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
    if (SQ_FAILED(sq_call(vm_, 1, SQTrue, SQTrue))) {
        sq_settop(vm_, top);
        return StateValue::boolean(false);
    }
    StateValue result;
    if (!squirrelToState(vm_, -1, result)) result = StateValue::boolean(false);
    sq_settop(vm_, top);
    return result;
}

bool DialogueFlow::restoreState(const StateValue& in, std::string* error) { return runner_.restoreState(in, error); }

std::string DialogueFlow::captureStateJson() const {
    StateValue state;
    if (!runner_.captureState(state)) return {};
    return conversationStateToJson(state);
}

bool DialogueFlow::restoreStateJson(const std::string& json) {
    StateValue state;
    if (!conversationStateFromJson(json, state, &lastError_)) return false;
    if (!migrations_.migrate(state, [this](const std::string& id) { return find(id); }, &lastError_)) return false;
    return runner_.restoreState(state, &lastError_);
}

bool DialogueFlow::registerMigration(const std::string& assetId, int fromVersion, const std::string& currentAssetId,
                                     const std::string& nodeMap) {
    return migrations_.registerMigration(assetId, fromVersion, currentAssetId, nodeMap, &lastError_);
}

void DialogueFlow::expose(ssq::Table& table) {
    if (DialogueFlow* self = DialogueFlow::create()) self->vm_ = table.getHandle();
    auto cls = table.addClass(name, DialogueFlow::create, false);
    expose(cls);
}

void DialogueFlow::expose(ssq::Class& cls) {
    cls.addFunc("getName", &DialogueFlow::getName);
    cls.addFunc("loadFromDnut", &DialogueFlow::loadFromDnut);
    cls.addFunc("loadFromDnutFile", &DialogueFlow::loadFromDnutFile);
    cls.addFunc("importYarn", &DialogueFlow::importYarn);
    cls.addFunc("importTwee", &DialogueFlow::importTwee);
    cls.addFunc("removeSource", &DialogueFlow::removeSource);
    cls.addFunc("lintAll", &DialogueFlow::lintAll);
    cls.addFunc("renameConversation", &DialogueFlow::renameConversation);
    cls.addFunc("renameNode", &DialogueFlow::renameNode);
    cls.addFunc("getLastLoadChanged", &DialogueFlow::getLastLoadChanged);
    cls.addFunc("clear", &DialogueFlow::clear);
    cls.addFunc("getConversationCount", &DialogueFlow::getConversationCount);
    cls.addFunc("getConversationId",
                static_cast<std::string (DialogueFlow::*)(int) const>(&DialogueFlow::getConversationId));
    cls.addFunc("hasConversation", &DialogueFlow::hasConversation);
    cls.addFunc("exportLocalizationCsv", &DialogueFlow::exportLocalizationCsv);
    cls.addFunc("importLocalizationCsv", &DialogueFlow::importLocalizationCsv);
    cls.addFunc("exportMissingLocalizationCsv", &DialogueFlow::exportMissingLocalizationCsv);
    cls.addFunc("exportVoiceRecordingCsv", &DialogueFlow::exportVoiceRecordingCsv);
    cls.addFunc("setLocale", &DialogueFlow::setLocale);
    cls.addFunc("getLocale", &DialogueFlow::getLocale);
    cls.addFunc("getDiagnosticCount", &DialogueFlow::getDiagnosticCount);
    cls.addFunc("getDiagnosticSeverity", &DialogueFlow::getDiagnosticSeverity);
    cls.addFunc("getDiagnosticMessage", &DialogueFlow::getDiagnosticMessage);
    cls.addFunc("getLastError", &DialogueFlow::getLastError);
    cls.addFunc("start", &DialogueFlow::start);
    cls.addFunc("advance", &DialogueFlow::advance);
    cls.addFunc("select", &DialogueFlow::select);
    cls.addFunc("isActive", &DialogueFlow::isActive);
    cls.addFunc("isBlocked", &DialogueFlow::isBlocked);
    cls.addFunc("getActiveConversationId",
                static_cast<std::string (DialogueFlow::*)() const>(&DialogueFlow::getConversationId));
    cls.addFunc("getNodeId", &DialogueFlow::getNodeId);
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
    cls.addFunc("setExpressionEvaluator", &DialogueFlow::setExpressionEvaluator);
    cls.addFunc("clearExpressionEvaluator", &DialogueFlow::clearExpressionEvaluator);
    cls.addFunc("captureStateJson", &DialogueFlow::captureStateJson);
    cls.addFunc("restoreStateJson", &DialogueFlow::restoreStateJson);
    cls.addFunc("registerMigration", &DialogueFlow::registerMigration);
    cls.addFunc("clearMigrations", &DialogueFlow::clearMigrations);
}

}  // namespace eve::dialogue
