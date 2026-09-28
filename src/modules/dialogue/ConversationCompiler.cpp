#include "dialogue/ConversationCompiler.h"

#include "dialogue/DialogueSequence.h"
#include "dialogue/DnutParser.h"

#include <algorithm>
#include <deque>
#include <string>
#include <unordered_set>
#include <utility>

namespace eve::dialogue {
namespace {

std::string csv(std::string value) {
    std::size_t position = 0;
    while ((position = value.find('"', position)) != std::string::npos) {
        value.insert(position, 1, '"');
        position += 2;
    }
    return '"' + value + '"';
}

}  // namespace

eve::Result<DnutDocument> compileDnutDocument(const std::string& source, const std::string& path,
                                              std::vector<ConversationDiagnostic>& diagnostics) {
    auto parsed = parseDnutDocument(source, path, diagnostics);
    if (!parsed.ok()) return eve::Result<DnutDocument>::failure(parsed.status());
    DnutDocument document = std::move(parsed).takeValue();
    auto linted = lintConversations(document.conversations, path, diagnostics);
    if (!linted.ok()) return eve::Result<DnutDocument>::failure(linted.status());
    return eve::Result<DnutDocument>::success(std::move(document));
}

eve::Result<void> lintConversations(const std::vector<eve::dnut::SequenceAsset>& assets,
                                    const std::string& path,
                                    std::vector<ConversationDiagnostic>& diagnostics) {
    bool                            valid = true;
    std::unordered_set<std::string> assetIds;
    eve::dnut::StepKindRegistry     registry;
    auto registered = registerDialogueSequenceSteps(registry);
    if (!registered.ok()) return registered;

    for (const auto& asset : assets) {
        if (!assetIds.insert(asset.id).second) {
            diagnostics.push_back({ConversationDiagnostic::Severity::Error, path, asset.sourceLine,
                                   "duplicate conversation id '" + asset.id + "'", "DuplicateAssetId",
                                   asset.sourceColumn, asset.id});
            valid = false;
        }
        auto validated = validateDialogueSequenceAsset(asset, registry);
        if (!validated.ok()) {
            diagnostics.push_back(
                {ConversationDiagnostic::Severity::Error, path, asset.sourceLine,
                 validated.status().describe(), "InvalidConversation", asset.sourceColumn, asset.id});
            valid = false;
            continue;
        }

        std::unordered_set<std::string> reached;
        std::deque<std::string>         pending{asset.entry};
        while (!pending.empty()) {
            const std::string id = pending.front();
            pending.pop_front();
            if (!reached.insert(id).second) continue;
            const auto* node = asset.findNode(id);
            if (!node) continue;
            if (!node->next.empty()) pending.push_back(node->next);
            if (node->type == "call") {
                const std::string returnNode = sequencePayloadString(*node, "return");
                if (!returnNode.empty()) pending.push_back(returnNode);
            }
            for (const auto& route : node->routes) pending.push_back(route.target);
        }
        for (const auto& node : asset.nodes)
            if (!reached.contains(node.id))
                diagnostics.push_back({ConversationDiagnostic::Severity::Warning, path, node.sourceLine,
                                       "conversation '" + asset.id + "': unreachable node '" + node.id + "'",
                                       "UnreachableNode", node.sourceColumn, asset.id + "/" + node.id});

        std::unordered_set<std::string> canExit;
        for (const auto& node : asset.nodes)
            if (node.type == "end") canExit.insert(node.id);
        bool changed = true;
        while (changed) {
            changed = false;
            for (const auto& node : asset.nodes) {
                if (canExit.contains(node.id)) continue;
                bool exits = !node.next.empty() && canExit.contains(node.next);
                if (node.type == "call") {
                    const std::string returnNode = sequencePayloadString(node, "return");
                    exits = exits || (!returnNode.empty() && canExit.contains(returnNode));
                }
                for (const auto& route : node.routes) exits = exits || canExit.contains(route.target);
                if (exits) changed = canExit.insert(node.id).second;
            }
        }
        bool allReachableHaveOutgoing = true;
        for (const auto& node : asset.nodes) {
            if (!reached.contains(node.id) || canExit.contains(node.id)) continue;
            const bool hasReturn = node.type == "call" && !sequencePayloadString(node, "return").empty();
            if (node.next.empty() && !hasReturn && node.routes.empty()) {
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

std::string exportConversationLocalizationCsv(const std::vector<eve::dnut::SequenceAsset>& assets) {
    std::string output = "conversation_id,node_id,i18n_key,speaker,source_text,voice\r\n";
    for (const auto& asset : assets)
        for (const auto& node : asset.nodes) {
            if (node.type != "line") continue;
            output += csv(asset.id) + ',' + csv(node.id) + ',' + csv(sequencePayloadString(node, "i18n")) + ',' +
                      csv(sequencePayloadString(node, "speaker")) + ',' +
                      csv(sequencePayloadString(node, "text")) + ',' +
                      csv(sequencePayloadString(node, "voice")) + "\r\n";
        }
    return output;
}

}  // namespace eve::dialogue
