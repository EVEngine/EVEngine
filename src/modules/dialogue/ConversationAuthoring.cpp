#include "dialogue/ConversationAuthoring.h"

#include "dialogue/DialogueSequence.h"
#include "dialogue/ConversationToolchain.h"

#include <algorithm>
#include <array>
#include <string>
#include <utility>
#include <vector>

namespace eve::dialogue {
namespace {

using Field = std::pair<const char*, const char*>;

bool isKnownType(const std::string& type) {
    static constexpr std::array<const char*, 7> types{
        "line", "branch", "choice", "call", "command", "wait", "end"};
    return std::find(types.begin(), types.end(), type) != types.end();
}

const std::vector<Field>& fieldsFor(const std::string& type) {
    static const std::vector<Field> line{{"next", "node"},    {"speaker", "string"},
                                         {"text", "multiline"}, {"pool", "asset"},
                                         {"i18n", "string"},  {"voice", "asset"},
                                         {"expression", "string"}};
    static const std::vector<Field> branch;
    static const std::vector<Field> choice;
    static const std::vector<Field> call{
        {"target", "asset"}, {"return", "node"}, {"next", "node"}, {"arguments", "json"}};
    static const std::vector<Field> command{
        {"name", "string"}, {"resultLocal", "string"}, {"next", "node"}, {"arguments", "json"}};
    static const std::vector<Field> wait{{"next", "node"}};
    static const std::vector<Field> end;
    if (type == "line") return line;
    if (type == "branch") return branch;
    if (type == "choice") return choice;
    if (type == "call") return call;
    if (type == "command") return command;
    if (type == "wait") return wait;
    return end;
}

}  // namespace

ConversationDocument::ConversationDocument(std::string id) {
    asset_.id    = std::move(id);
    asset_.entry = "end";
    eve::dnut::SequenceNode end;
    end.id   = "end";
    end.type = "end";
    asset_.nodes.push_back(std::move(end));
}

ConversationDocument::ConversationDocument(eve::dnut::SequenceAsset asset)
    : asset_(std::move(asset)) {}

bool ConversationDocument::fail(const std::string& message) {
    failureMessage_ = message;
    return false;
}

eve::dnut::SequenceNode* ConversationDocument::findNode(const std::string& nodeId) {
    for (auto& node : asset_.nodes)
        if (node.id == nodeId) return &node;
    return nullptr;
}

const eve::dnut::SequenceNode* ConversationDocument::findNode(const std::string& nodeId) const {
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

int ConversationDocument::getParameterCount() const {
    return static_cast<int>(asset_.parameters.size());
}

std::string ConversationDocument::getParameter(int index) const {
    return index >= 0 && index < getParameterCount()
               ? asset_.parameters[static_cast<std::size_t>(index)].name
               : std::string{};
}

bool ConversationDocument::addParameter(const std::string& name) {
    if (name.empty()) return fail("parameter name must not be empty");
    if (std::any_of(asset_.parameters.begin(), asset_.parameters.end(),
                    [&](const auto& parameter) { return parameter.name == name; }))
        return fail("parameter already exists: " + name);
    asset_.parameters.emplace_back(name);
    failureMessage_.clear();
    return true;
}

bool ConversationDocument::removeParameter(const std::string& name) {
    const auto found = std::find_if(asset_.parameters.begin(), asset_.parameters.end(),
                                    [&](const auto& parameter) { return parameter.name == name; });
    if (found == asset_.parameters.end()) return false;
    asset_.parameters.erase(found);
    return true;
}

int ConversationDocument::getNodeCount() const { return static_cast<int>(asset_.nodes.size()); }

std::string ConversationDocument::getNodeId(int index) const {
    return index >= 0 && index < getNodeCount()
               ? asset_.nodes[static_cast<std::size_t>(index)].id
               : std::string{};
}

bool ConversationDocument::hasNode(const std::string& nodeId) const {
    return findNode(nodeId) != nullptr;
}

bool ConversationDocument::addNode(const std::string& nodeId, const std::string& kind) {
    if (nodeId.empty()) return fail("node ID must not be empty");
    if (findNode(nodeId)) return fail("node already exists: " + nodeId);
    if (!isKnownType(kind)) return fail("unknown node kind: " + kind);
    eve::dnut::SequenceNode node;
    node.id   = nodeId;
    node.type = kind;
    asset_.nodes.push_back(std::move(node));
    failureMessage_.clear();
    return true;
}

bool ConversationDocument::removeNode(const std::string& nodeId) {
    const auto found =
        std::find_if(asset_.nodes.begin(), asset_.nodes.end(),
                     [&](const auto& node) { return node.id == nodeId; });
    if (found == asset_.nodes.end()) return false;
    asset_.nodes.erase(found);
    if (asset_.entry == nodeId) asset_.entry.clear();
    for (auto& node : asset_.nodes) {
        if (node.next == nodeId) node.next.clear();
        if (node.type == "call" && sequencePayloadString(node, "return") == nodeId)
            node.payload.set("return", eve::Value::string(""));
        node.routes.erase(std::remove_if(node.routes.begin(), node.routes.end(),
                                         [&](const auto& route) { return route.target == nodeId; }),
                          node.routes.end());
    }
    return true;
}

bool ConversationDocument::renameNode(const std::string& oldId, const std::string& newId) {
    std::vector<eve::dnut::SequenceAsset> assets{asset_};
    auto renamed = renameConversationNode(assets, asset_.id, oldId, newId);
    if (!renamed.ok()) {
        failureMessage_ = renamed.status().describe();
        return false;
    }
    asset_ = std::move(assets.front());
    return true;
}

std::string ConversationDocument::getNodeKind(const std::string& nodeId) const {
    const auto* node = findNode(nodeId);
    return node ? node->type : std::string{};
}

bool ConversationDocument::setNodeKind(const std::string& nodeId, const std::string& kind) {
    auto* node = findNode(nodeId);
    if (!node) return fail("node not found: " + nodeId);
    if (!isKnownType(kind)) return fail("unknown node kind: " + kind);
    node->type = kind;
    failureMessage_.clear();
    return true;
}

int ConversationDocument::getFieldCount(const std::string& nodeId) const {
    const auto* node = findNode(nodeId);
    return node ? static_cast<int>(fieldsFor(node->type).size()) : 0;
}

std::string ConversationDocument::getFieldName(const std::string& nodeId, int index) const {
    const auto* node = findNode(nodeId);
    if (!node) return {};
    const auto& fields = fieldsFor(node->type);
    return index >= 0 && index < static_cast<int>(fields.size())
               ? fields[static_cast<std::size_t>(index)].first
               : "";
}

std::string ConversationDocument::getFieldKind(const std::string& nodeId, int index) const {
    const auto* node = findNode(nodeId);
    if (!node) return {};
    const auto& fields = fieldsFor(node->type);
    return index >= 0 && index < static_cast<int>(fields.size())
               ? fields[static_cast<std::size_t>(index)].second
               : "";
}

std::string ConversationDocument::getField(const std::string& nodeId,
                                           const std::string& field) const {
    const auto* node = findNode(nodeId);
    if (!node) return {};
    if (field == "next") return node->next;
    const eve::Value* value = node->payload.find(field);
    if (!value) return {};
    if (field == "arguments") {
        auto json = value->toJson();
        return json.ok() ? std::move(json).takeValue() : std::string{};
    }
    return value->isString() ? value->asString() : std::string{};
}

bool ConversationDocument::setField(const std::string& nodeId, const std::string& field,
                                    const std::string& value) {
    auto* node = findNode(nodeId);
    if (!node) return fail("node not found: " + nodeId);
    if (field == "next") {
        node->next = value;
    } else if (field == "arguments") {
        auto parsed = eve::Value::fromJson(value);
        if (!parsed.ok()) {
            failureMessage_ = parsed.status().describe();
            return false;
        }
        eve::Value arguments = std::move(parsed).takeValue();
        if (!arguments.isObject()) return fail("arguments must be a JSON object");
        node->payload.set("arguments", std::move(arguments));
    } else {
        bool known = false;
        for (const auto& [name, kind] : fieldsFor(node->type)) {
            (void)kind;
            if (field == name) {
                known = true;
                break;
            }
        }
        if (!known) return fail("unknown node field: " + field);
        node->payload.set(field, eve::Value::string(value));
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
               ? node->routes[static_cast<std::size_t>(index)].label
               : std::string{};
}

std::string ConversationDocument::getRouteTarget(const std::string& nodeId, int index) const {
    const auto* node = findNode(nodeId);
    return node && index >= 0 && index < static_cast<int>(node->routes.size())
               ? node->routes[static_cast<std::size_t>(index)].target
               : std::string{};
}

bool ConversationDocument::addRoute(const std::string& nodeId, const std::string& label,
                                    const std::string& target) {
    auto* node = findNode(nodeId);
    if (!node) return fail("node not found: " + nodeId);
    if (node->type != "branch" && node->type != "choice")
        return fail("routes require a branch or choice node");
    eve::dnut::SequenceRoute route;
    route.label  = label;
    route.target = target;
    node->routes.push_back(std::move(route));
    failureMessage_.clear();
    return true;
}

bool ConversationDocument::setRoute(const std::string& nodeId, int index,
                                    const std::string& label, const std::string& target) {
    auto* node = findNode(nodeId);
    if (!node || index < 0 || index >= static_cast<int>(node->routes.size())) return false;
    auto& route  = node->routes[static_cast<std::size_t>(index)];
    route.label  = label;
    route.target = target;
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
    const bool valid = lintConversations({asset_}, asset_.id, diagnostics_).ok();
    failureMessage_  = valid || diagnostics_.empty() ? std::string{} : diagnostics_.front().message;
    return valid;
}

int ConversationDocument::getDiagnosticCount() const {
    return static_cast<int>(diagnostics_.size());
}

std::string ConversationDocument::getDiagnosticSeverity(int index) const {
    if (index < 0 || index >= getDiagnosticCount()) return {};
    return diagnostics_[static_cast<std::size_t>(index)].severity ==
                   ConversationDiagnostic::Severity::Error
               ? "error"
               : "warning";
}

std::string ConversationDocument::getDiagnosticPath(int index) const {
    return index >= 0 && index < getDiagnosticCount()
               ? diagnostics_[static_cast<std::size_t>(index)].path
               : std::string{};
}

int ConversationDocument::getDiagnosticLine(int index) const {
    return index >= 0 && index < getDiagnosticCount()
               ? diagnostics_[static_cast<std::size_t>(index)].line
               : 0;
}

std::string ConversationDocument::getDiagnosticMessage(int index) const {
    return index >= 0 && index < getDiagnosticCount()
               ? diagnostics_[static_cast<std::size_t>(index)].message
               : std::string{};
}

}  // namespace eve::dialogue
