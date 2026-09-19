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

eve::Result<std::vector<ConversationAsset>> compileDnutConversations(
    const std::string& source, const std::string& path, std::vector<ConversationDiagnostic>& diagnostics) {
    auto compiled = compileDnutDocument(source, path, diagnostics);
    if (!compiled) return eve::Result<std::vector<ConversationAsset>>::failure(compiled.status());
    DnutDocument document = std::move(compiled).takeValue();
    return eve::Result<std::vector<ConversationAsset>>::success(std::move(document.conversations));
}

eve::Result<DnutDocument> compileDnutDocument(const std::string& source, const std::string& path,
                                              std::vector<ConversationDiagnostic>& diagnostics) {
    auto parsed = parseDnutDocument(source, path, diagnostics);
    if (!parsed) {
        return eve::Result<DnutDocument>::failure(parsed.status());
    }
    DnutDocument document = std::move(parsed).takeValue();
    auto linted = lintConversations(document.conversations, path, diagnostics);
    if (!linted) return eve::Result<DnutDocument>::failure(linted.status());
    return eve::Result<DnutDocument>::success(std::move(document));
}

eve::Result<void> lintConversations(const std::vector<ConversationAsset>& assets, const std::string& path,
                                    std::vector<ConversationDiagnostic>& diagnostics) {
    bool valid = true;
    std::unordered_set<std::string> assetIds;
    for (const auto& asset : assets) {
        if (!assetIds.insert(asset.id).second) {
            diagnostics.push_back({ConversationDiagnostic::Severity::Error, path, asset.sourceLine,
                                   "duplicate conversation id '" + asset.id + "'", "DuplicateAssetId",
                                   asset.sourceColumn,
                                   asset.id});
            valid = false;
        }
        auto validated = asset.validate();
        if (!validated) {
            diagnostics.push_back(
                {ConversationDiagnostic::Severity::Error, path, asset.sourceLine, validated.status().describe(),
                 "InvalidConversation", asset.sourceColumn, asset.id});
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
                diagnostics.push_back({ConversationDiagnostic::Severity::Warning, path, node.sourceLine,
                                       "conversation '" + asset.id + "': unreachable node '" +
                                           node.id + "'",
                                       "UnreachableNode", node.sourceColumn, asset.id + "/" + node.id});
        }

        std::unordered_set<std::string> canExit;
        for (const auto& node : asset.nodes)
            if (node.kind == ConversationAsset::Node::Kind::End) canExit.insert(node.id);
        bool changed = true;
        while (changed) {
            changed = false;
            for (const auto& node : asset.nodes) {
                if (canExit.contains(node.id)) continue;
                bool exits = (!node.next.empty() && canExit.contains(node.next)) ||
                             (!node.returnNode.empty() && canExit.contains(node.returnNode));
                for (const auto& route : node.routes) exits = exits || canExit.contains(route.second);
                if (exits) changed = canExit.insert(node.id).second;
            }
        }
        bool allReachableHaveOutgoing = true;
        for (const auto& node : asset.nodes) {
            if (!reached.contains(node.id) || canExit.contains(node.id)) continue;
            if (node.next.empty() && node.returnNode.empty() && node.routes.empty()) {
                allReachableHaveOutgoing = false;
                break;
            }
        }
        if (!canExit.contains(asset.entry) && allReachableHaveOutgoing) {
            diagnostics.push_back({ConversationDiagnostic::Severity::Error, path, asset.sourceLine,
                                   "conversation '" + asset.id + "' contains a reachable loop with no exit",
                                   "NoExitLoop", asset.sourceColumn, asset.id});
            valid = false;
        }
    }
    valid = valid && std::none_of(diagnostics.begin(), diagnostics.end(), [](const auto& item) {
        return item.severity == ConversationDiagnostic::Severity::Error;
    });
    if (!valid) {
        const std::string message = diagnostics.empty() ? "conversation lint failed" : diagnostics.front().message;
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, message, path, {}, "dialogue.lint"));
    }
    return eve::Result<void>::success();
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
