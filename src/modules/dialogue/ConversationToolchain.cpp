#include "dialogue/ConversationToolchain.h"

#include <algorithm>
#include <unordered_set>

namespace eve::dialogue {
namespace {

eve::Result<void> renameFailure(const std::string& message, const std::string& path) {
    return eve::Result<void>::failure(eve::Diagnostic::error(
        eve::DiagnosticCode::InvalidArgument, message, path, {}, "dialogue.rename"));
}

void rewriteNodeReference(std::string& reference, const std::string& oldId, const std::string& newId) {
    if (reference == oldId) reference = newId;
}

}  // namespace

eve::Result<void> lintConversationWorkspace(const std::vector<ConversationAsset>& assets, const std::string& label,
                                            std::vector<ConversationDiagnostic>& diagnostics) {
    const size_t                    diagnosticsBegin = diagnostics.size();
    auto                            linted           = lintConversations(assets, label, diagnostics);
    bool                            valid            = linted.ok();
    std::unordered_set<std::string> assetIds;
    for (const auto& asset : assets) assetIds.insert(asset.id);
    for (const auto& asset : assets) {
        for (const auto& node : asset.nodes) {
            if (node.kind != ConversationAsset::Node::Kind::Call || node.target.empty()) continue;
            if (assetIds.find(node.target) == assetIds.end()) {
                diagnostics.push_back({ConversationDiagnostic::Severity::Error, label, node.sourceLine,
                                       "conversation '" + asset.id + "': call node '" + node.id +
                                           "' references missing conversation '" + node.target + "'",
                                       "MissingConversation", node.sourceColumn, asset.id + "/" + node.id});
                valid = false;
                continue;
            }
            const auto target = std::find_if(assets.begin(), assets.end(), [&](const auto& candidate) {
                return candidate.id == node.target;
            });
            for (const auto& parameter : target->parameters) {
                if (parameter.required && !node.arguments.find(parameter.name)) {
                    diagnostics.push_back({ConversationDiagnostic::Severity::Error, label, node.sourceLine,
                                           "conversation '" + asset.id + "': call node '" + node.id +
                                               "' omits required argument '" + parameter.name + "'",
                                           "MissingCallArgument", node.sourceColumn, asset.id + "/" + node.id});
                    valid = false;
                }
            }
            for (const auto& argument : node.arguments.keys()) {
                if (std::none_of(target->parameters.begin(), target->parameters.end(), [&](const auto& parameter) {
                        return parameter.name == argument;
                    })) {
                    diagnostics.push_back({ConversationDiagnostic::Severity::Error, label, node.sourceLine,
                                           "conversation '" + asset.id + "': call node '" + node.id +
                                               "' passes undeclared argument '" + argument + "'",
                                           "UnexpectedCallArgument", node.sourceColumn, asset.id + "/" + node.id});
                    valid = false;
                }
            }
        }
    }
    valid = valid &&
            std::none_of(diagnostics.begin() + static_cast<std::ptrdiff_t>(diagnosticsBegin), diagnostics.end(),
                         [](const auto& diagnostic) {
                             return diagnostic.severity == ConversationDiagnostic::Severity::Error;
                         });
    if (!valid) {
        const std::string message = diagnostics.size() > diagnosticsBegin ? diagnostics[diagnosticsBegin].message
                                                                          : "dialogue workspace lint failed";
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, message, label, {}, "dialogue.workspace.lint"));
    }
    return eve::Result<void>::success();
}

eve::Result<void> renameConversationAsset(std::vector<ConversationAsset>& assets, const std::string& oldId,
                                          const std::string& newId) {
    if (oldId.empty() || newId.empty()) return renameFailure("conversation IDs must not be empty", oldId);
    auto source = std::find_if(assets.begin(), assets.end(), [&](const auto& asset) { return asset.id == oldId; });
    if (source == assets.end()) return renameFailure("conversation not found: " + oldId, oldId);
    if (std::any_of(assets.begin(), assets.end(), [&](const auto& asset) { return asset.id == newId; }))
        return renameFailure("conversation already exists: " + newId, newId);
    source->id = newId;
    ++source->version;
    for (auto& asset : assets)
        for (auto& node : asset.nodes)
            if (node.kind == ConversationAsset::Node::Kind::Call && node.target == oldId) node.target = newId;
    return eve::Result<void>::success();
}

eve::Result<void> renameConversationNode(std::vector<ConversationAsset>& assets, const std::string& assetId,
                                         const std::string& oldId, const std::string& newId) {
    if (oldId.empty() || newId.empty()) return renameFailure("node IDs must not be empty", assetId);
    auto asset = std::find_if(assets.begin(), assets.end(), [&](const auto& item) { return item.id == assetId; });
    if (asset == assets.end()) return renameFailure("conversation not found: " + assetId, assetId);
    auto node =
        std::find_if(asset->nodes.begin(), asset->nodes.end(), [&](const auto& item) { return item.id == oldId; });
    if (node == asset->nodes.end()) return renameFailure("node not found: " + oldId, assetId + "/" + oldId);
    if (asset->findNode(newId)) return renameFailure("node already exists: " + newId, assetId + "/" + newId);
    node->id = newId;
    rewriteNodeReference(asset->entry, oldId, newId);
    for (auto& item : asset->nodes) {
        rewriteNodeReference(item.next, oldId, newId);
        rewriteNodeReference(item.returnNode, oldId, newId);
        for (auto& route : item.routes) rewriteNodeReference(route.second, oldId, newId);
    }
    ++asset->version;
    return eve::Result<void>::success();
}

}  // namespace eve::dialogue
