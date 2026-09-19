#include "dialogue/ConversationCompiler.h"
#include "dialogue/DnutParser.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <deque>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace eve::dialogue {
namespace {

std::string csv(std::string value) {
    size_t pos = 0;
    while ((pos = value.find('"', pos)) != std::string::npos) {
        value.insert(pos, 1, '"');
        pos += 2;
    }
    return '"' + value + '"';
}

}  // namespace

bool compileDnutConversations(const std::string& source, const std::string& path,
                              std::vector<ConversationAsset>& assets,
                              std::vector<ConversationDiagnostic>& diagnostics) {
    DnutDocument document;
    if (!parseDnutDocument(source, path, document, diagnostics)) return false;
    assets = std::move(document.conversations);
    return lintConversations(assets, path, diagnostics);
}

bool lintConversations(const std::vector<ConversationAsset>& assets, const std::string& path,
                       std::vector<ConversationDiagnostic>& diagnostics) {
    bool valid = true;
    std::unordered_set<std::string> assetIds;
    for (const auto& asset : assets) {
        if (!assetIds.insert(asset.id).second) {
            diagnostics.push_back({ConversationDiagnostic::Severity::Error, path, 0,
                                   "duplicate conversation id '" + asset.id + "'"});
            valid = false;
        }
        std::string error;
        if (!asset.validate(&error)) {
            diagnostics.push_back({ConversationDiagnostic::Severity::Error, path, 0, error});
            valid = false;
            continue;
        }
        std::unordered_set<std::string> reached;
        std::deque<std::string> pending{asset.entry};
        while (!pending.empty()) {
            const std::string id = pending.front();
            pending.pop_front();
            if (!reached.insert(id).second) continue;
            const auto* node = asset.findNode(id);
            if (!node) continue;
            if (!node->next.empty()) pending.push_back(node->next);
            if (!node->returnNode.empty()) pending.push_back(node->returnNode);
            for (const auto& route : node->routes) pending.push_back(route.second);
        }
        for (const auto& node : asset.nodes) {
            if (reached.find(node.id) == reached.end())
                diagnostics.push_back({ConversationDiagnostic::Severity::Warning, path, 0,
                                       "conversation '" + asset.id + "': unreachable node '" +
                                           node.id + "'"});
        }
    }
    return valid && std::none_of(diagnostics.begin(), diagnostics.end(), [](const auto& item) {
               return item.severity == ConversationDiagnostic::Severity::Error;
           });
}

std::string exportConversationLocalizationCsv(const std::vector<ConversationAsset>& assets) {
    std::string out = "conversation_id,node_id,i18n_key,speaker,source_text,voice\r\n";
    for (const auto& asset : assets) {
        for (const auto& node : asset.nodes) {
            if (node.kind != ConversationAsset::Node::Kind::Line) continue;
            out += csv(asset.id) + ',' + csv(node.id) + ',' + csv(node.i18nKey) + ',' +
                   csv(node.speaker) + ',' + csv(node.text) + ',' + csv(node.voice) + "\r\n";
        }
    }
    return out;
}

}  // namespace eve::dialogue
