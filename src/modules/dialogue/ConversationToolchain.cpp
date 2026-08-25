#include "dialogue/ConversationToolchain.h"

#include <algorithm>
#include <unordered_set>

namespace eve::dialogue {
namespace {

bool fail(std::string* error, const std::string& message) {
    if (error) *error = message;
    return false;
}

void rewriteNodeReference(std::string& reference, const std::string& oldId, const std::string& newId) {
    if (reference == oldId) reference = newId;
}

}  // namespace

bool lintConversationWorkspace(const std::vector<ConversationAsset>& assets, const std::string& label,
                               std::vector<ConversationDiagnostic>& diagnostics) {
    const size_t                    diagnosticsBegin = diagnostics.size();
    bool                            valid            = lintConversations(assets, label, diagnostics);
    std::unordered_set<std::string> assetIds;
    for (const auto& asset : assets) assetIds.insert(asset.id);
    for (const auto& asset : assets) {
        for (const auto& node : asset.nodes) {
            if (node.kind != ConversationAsset::Node::Kind::Call || node.target.empty()) continue;
            if (assetIds.find(node.target) == assetIds.end()) {
                diagnostics.push_back({ConversationDiagnostic::Severity::Error, label, 0,
                                       "conversation '" + asset.id + "': call node '" + node.id +
                                           "' references missing conversation '" + node.target + "'"});
                valid = false;
            }
        }
    }
    return valid && std::none_of(diagnostics.begin() + static_cast<std::ptrdiff_t>(diagnosticsBegin), diagnostics.end(),
                                 [](const auto& diagnostic) {
                                     return diagnostic.severity == ConversationDiagnostic::Severity::Error;
                                 });
}

bool renameConversationAsset(std::vector<ConversationAsset>& assets, const std::string& oldId, const std::string& newId,
                             std::string* error) {
    if (oldId.empty() || newId.empty()) return fail(error, "conversation IDs must not be empty");
    auto source = std::find_if(assets.begin(), assets.end(), [&](const auto& asset) { return asset.id == oldId; });
    if (source == assets.end()) return fail(error, "conversation not found: " + oldId);
    if (std::any_of(assets.begin(), assets.end(), [&](const auto& asset) { return asset.id == newId; }))
        return fail(error, "conversation already exists: " + newId);
    source->id = newId;
    ++source->version;
    for (auto& asset : assets)
        for (auto& node : asset.nodes)
            if (node.kind == ConversationAsset::Node::Kind::Call && node.target == oldId) node.target = newId;
    return true;
}

bool renameConversationNode(std::vector<ConversationAsset>& assets, const std::string& assetId,
                            const std::string& oldId, const std::string& newId, std::string* error) {
    if (oldId.empty() || newId.empty()) return fail(error, "node IDs must not be empty");
    auto asset = std::find_if(assets.begin(), assets.end(), [&](const auto& item) { return item.id == assetId; });
    if (asset == assets.end()) return fail(error, "conversation not found: " + assetId);
    auto node =
        std::find_if(asset->nodes.begin(), asset->nodes.end(), [&](const auto& item) { return item.id == oldId; });
    if (node == asset->nodes.end()) return fail(error, "node not found: " + oldId);
    if (asset->findNode(newId)) return fail(error, "node already exists: " + newId);
    node->id = newId;
    rewriteNodeReference(asset->entry, oldId, newId);
    for (auto& item : asset->nodes) {
        rewriteNodeReference(item.next, oldId, newId);
        rewriteNodeReference(item.returnNode, oldId, newId);
        for (auto& route : item.routes) rewriteNodeReference(route.second, oldId, newId);
    }
    ++asset->version;
    return true;
}

}  // namespace eve::dialogue
