#include "dialogue/ConversationAuthoring.h"

#include "dialogue/ConversationPersistence.h"
#include "dialogue/ConversationToolchain.h"

#include <algorithm>
#include <array>

namespace eve::dialogue {
namespace {

using Kind = ConversationAsset::Node::Kind;

bool parseKind(const std::string& name, Kind& out) {
    static constexpr std::array<std::pair<const char*, Kind>, 7> kinds{{
        {"line", Kind::Line},
        {"branch", Kind::Branch},
        {"choice", Kind::Choice},
        {"call", Kind::Call},
        {"command", Kind::Command},
        {"wait", Kind::Wait},
        {"end", Kind::End},
    }};
    for (const auto& [key, value] : kinds) {
        if (name == key) {
            out = value;
            return true;
        }
    }
    return false;
}

std::string kindName(Kind kind) {
    switch (kind) {
        case Kind::Line: return "line";
        case Kind::Branch: return "branch";
        case Kind::Choice: return "choice";
        case Kind::Call: return "call";
        case Kind::Command: return "command";
        case Kind::Wait: return "wait";
        case Kind::End: return "end";
    }
    return {};
}

using Field = std::pair<const char*, const char*>;
const std::vector<Field>& fieldsFor(Kind kind) {
    static const std::vector<Field> line{{"next", "node"},        {"speaker", "string"}, {"text", "multiline"},
                                         {"pool", "asset"},       {"i18nKey", "string"}, {"voice", "asset"},
                                         {"expression", "string"}};
    static const std::vector<Field> branch;
    static const std::vector<Field> choice;
    static const std::vector<Field> call{
        {"target", "asset"}, {"returnNode", "node"}, {"next", "node"}, {"arguments", "json"}};
    static const std::vector<Field> command{
        {"target", "string"}, {"expression", "string"}, {"next", "node"}, {"arguments", "json"}};
    static const std::vector<Field> wait{{"next", "node"}};
    static const std::vector<Field> end;
    switch (kind) {
        case Kind::Line: return line;
        case Kind::Branch: return branch;
        case Kind::Choice: return choice;
        case Kind::Call: return call;
        case Kind::Command: return command;
        case Kind::Wait: return wait;
        case Kind::End: return end;
    }
    return end;
}

}  // namespace

ConversationDocument::ConversationDocument(std::string id) {
    asset_.id    = std::move(id);
    asset_.entry = "end";
    asset_.nodes.push_back({"end", Kind::End});
}

ConversationDocument::ConversationDocument(ConversationAsset asset) : asset_(std::move(asset)) {}

bool ConversationDocument::fail(const std::string& message) {
    failureMessage_ = message;
    return false;
}

ConversationAsset::Node* ConversationDocument::findNode(const std::string& nodeId) {
    for (auto& node : asset_.nodes)
        if (node.id == nodeId) return &node;
    return nullptr;
}

const ConversationAsset::Node* ConversationDocument::findNode(const std::string& nodeId) const {
    return asset_.findNode(nodeId);
}

bool ConversationDocument::setId(const std::string& id) {
    if (id.empty()) return fail("conversation ID must not be empty");
    asset_.id = id;
    failureMessage_.clear();
    return true;
}

bool ConversationDocument::setVersion(int version) {
    if (version <= 0) return fail("conversation version must be positive");
    asset_.version = version;
    failureMessage_.clear();
    return true;
}

bool ConversationDocument::setEntry(const std::string& nodeId) {
    if (!findNode(nodeId)) return fail("entry node not found: " + nodeId);
    asset_.entry = nodeId;
    failureMessage_.clear();
    return true;
}

int ConversationDocument::getParameterCount() const { return static_cast<int>(asset_.parameters.size()); }

std::string ConversationDocument::getParameter(int index) const {
    return index >= 0 && index < getParameterCount() ? asset_.parameters[static_cast<size_t>(index)] : std::string{};
}

bool ConversationDocument::addParameter(const std::string& name) {
    if (name.empty()) return fail("parameter name must not be empty");
    if (std::find(asset_.parameters.begin(), asset_.parameters.end(), name) != asset_.parameters.end())
        return fail("parameter already exists: " + name);
    asset_.parameters.push_back(name);
    failureMessage_.clear();
    return true;
}

bool ConversationDocument::removeParameter(const std::string& name) {
    const auto it = std::find(asset_.parameters.begin(), asset_.parameters.end(), name);
    if (it == asset_.parameters.end()) return false;
    asset_.parameters.erase(it);
    return true;
}

int ConversationDocument::getNodeCount() const { return static_cast<int>(asset_.nodes.size()); }

std::string ConversationDocument::getNodeId(int index) const {
    return index >= 0 && index < getNodeCount() ? asset_.nodes[static_cast<size_t>(index)].id : std::string{};
}

bool ConversationDocument::hasNode(const std::string& nodeId) const { return findNode(nodeId) != nullptr; }

bool ConversationDocument::addNode(const std::string& nodeId, const std::string& kind) {
    Kind parsed;
    if (nodeId.empty()) return fail("node ID must not be empty");
    if (findNode(nodeId)) return fail("node already exists: " + nodeId);
    if (!parseKind(kind, parsed)) return fail("unknown node kind: " + kind);
    asset_.nodes.push_back({nodeId, parsed});
    failureMessage_.clear();
    return true;
}

bool ConversationDocument::removeNode(const std::string& nodeId) {
    const auto it =
        std::find_if(asset_.nodes.begin(), asset_.nodes.end(), [&](const auto& node) { return node.id == nodeId; });
    if (it == asset_.nodes.end()) return false;
    asset_.nodes.erase(it);
    if (asset_.entry == nodeId) asset_.entry.clear();
    for (auto& node : asset_.nodes) {
        if (node.next == nodeId) node.next.clear();
        if (node.returnNode == nodeId) node.returnNode.clear();
        node.routes.erase(std::remove_if(node.routes.begin(), node.routes.end(),
                                         [&](const auto& route) { return route.second == nodeId; }),
                          node.routes.end());
    }
    return true;
}

bool ConversationDocument::renameNode(const std::string& oldId, const std::string& newId) {
    std::vector<ConversationAsset> assets{asset_};
    if (!renameConversationNode(assets, asset_.id, oldId, newId, &failureMessage_)) return false;
    asset_ = std::move(assets.front());
    return true;
}

std::string ConversationDocument::getNodeKind(const std::string& nodeId) const {
    const auto* node = findNode(nodeId);
    return node ? kindName(node->kind) : std::string{};
}

bool ConversationDocument::setNodeKind(const std::string& nodeId, const std::string& kind) {
    auto* node = findNode(nodeId);
    Kind  parsed;
    if (!node) return fail("node not found: " + nodeId);
    if (!parseKind(kind, parsed)) return fail("unknown node kind: " + kind);
    node->kind = parsed;
    failureMessage_.clear();
    return true;
}

int ConversationDocument::getFieldCount(const std::string& nodeId) const {
    const auto* node = findNode(nodeId);
    return node ? static_cast<int>(fieldsFor(node->kind).size()) : 0;
}

std::string ConversationDocument::getFieldName(const std::string& nodeId, int index) const {
    const auto* node = findNode(nodeId);
    if (!node) return {};
    const auto& fields = fieldsFor(node->kind);
    return index >= 0 && index < static_cast<int>(fields.size()) ? fields[static_cast<size_t>(index)].first : "";
}

std::string ConversationDocument::getFieldKind(const std::string& nodeId, int index) const {
    const auto* node = findNode(nodeId);
    if (!node) return {};
    const auto& fields = fieldsFor(node->kind);
    return index >= 0 && index < static_cast<int>(fields.size()) ? fields[static_cast<size_t>(index)].second : "";
}

std::string ConversationDocument::getField(const std::string& nodeId, const std::string& field) const {
    const auto* node = findNode(nodeId);
    if (!node) return {};
    if (field == "next") return node->next;
    if (field == "speaker") return node->speaker;
    if (field == "text") return node->text;
    if (field == "pool") return node->pool;
    if (field == "i18nKey") return node->i18nKey;
    if (field == "voice") return node->voice;
    if (field == "expression") return node->expression;
    if (field == "target") return node->target;
    if (field == "returnNode") return node->returnNode;
    if (field == "arguments") return conversationStateToJson(node->arguments);
    return {};
}

bool ConversationDocument::setField(const std::string& nodeId, const std::string& field, const std::string& value) {
    auto* node = findNode(nodeId);
    if (!node) return fail("node not found: " + nodeId);
    if (field == "next")
        node->next = value;
    else if (field == "speaker")
        node->speaker = value;
    else if (field == "text")
        node->text = value;
    else if (field == "pool")
        node->pool = value;
    else if (field == "i18nKey")
        node->i18nKey = value;
    else if (field == "voice")
        node->voice = value;
    else if (field == "expression")
        node->expression = value;
    else if (field == "target")
        node->target = value;
    else if (field == "returnNode")
        node->returnNode = value;
    else if (field == "arguments") {
        StateValue parsed;
        if (!conversationStateFromJson(value, parsed, &failureMessage_)) return false;
        if (!parsed.isObject()) return fail("arguments must be a JSON object");
        node->arguments = std::move(parsed);
    } else {
        return fail("unknown node field: " + field);
    }
    failureMessage_.clear();
    return true;
}

int ConversationDocument::getRouteCount(const std::string& nodeId) const {
    const auto* node = findNode(nodeId);
    return node ? static_cast<int>(node->routes.size()) : 0;
}

std::string ConversationDocument::getRouteLabel(const std::string& nodeId, int index) const {
    const auto* node = findNode(nodeId);
    return node && index >= 0 && index < static_cast<int>(node->routes.size())
               ? node->routes[static_cast<size_t>(index)].first
               : std::string{};
}

std::string ConversationDocument::getRouteTarget(const std::string& nodeId, int index) const {
    const auto* node = findNode(nodeId);
    return node && index >= 0 && index < static_cast<int>(node->routes.size())
               ? node->routes[static_cast<size_t>(index)].second
               : std::string{};
}

bool ConversationDocument::addRoute(const std::string& nodeId, const std::string& label, const std::string& target) {
    auto* node = findNode(nodeId);
    if (!node) return fail("node not found: " + nodeId);
    if (node->kind != Kind::Branch && node->kind != Kind::Choice) return fail("routes require a branch or choice node");
    node->routes.emplace_back(label, target);
    failureMessage_.clear();
    return true;
}

bool ConversationDocument::setRoute(const std::string& nodeId, int index, const std::string& label,
                                    const std::string& target) {
    auto* node = findNode(nodeId);
    if (!node || index < 0 || index >= static_cast<int>(node->routes.size())) return false;
    node->routes[static_cast<size_t>(index)] = {label, target};
    return true;
}

bool ConversationDocument::removeRoute(const std::string& nodeId, int index) {
    auto* node = findNode(nodeId);
    if (!node || index < 0 || index >= static_cast<int>(node->routes.size())) return false;
    node->routes.erase(node->routes.begin() + index);
    return true;
}

bool ConversationDocument::validate() {
    diagnostics_.clear();
    const bool valid = lintConversations({asset_}, asset_.id, diagnostics_);
    failureMessage_  = valid || diagnostics_.empty() ? std::string{} : diagnostics_.front().message;
    return valid;
}

int ConversationDocument::getDiagnosticCount() const { return static_cast<int>(diagnostics_.size()); }

std::string ConversationDocument::getDiagnosticSeverity(int index) const {
    if (index < 0 || index >= getDiagnosticCount()) return {};
    return diagnostics_[static_cast<size_t>(index)].severity == ConversationDiagnostic::Severity::Error ? "error"
                                                                                                        : "warning";
}

std::string ConversationDocument::getDiagnosticPath(int index) const {
    return index >= 0 && index < getDiagnosticCount() ? diagnostics_[static_cast<size_t>(index)].path : std::string{};
}

int ConversationDocument::getDiagnosticLine(int index) const {
    return index >= 0 && index < getDiagnosticCount() ? diagnostics_[static_cast<size_t>(index)].line : 0;
}

std::string ConversationDocument::getDiagnosticMessage(int index) const {
    return index >= 0 && index < getDiagnosticCount() ? diagnostics_[static_cast<size_t>(index)].message
                                                      : std::string{};
}

}  // namespace eve::dialogue
