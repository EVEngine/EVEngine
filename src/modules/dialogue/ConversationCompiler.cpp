#include "dialogue/ConversationCompiler.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <deque>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace eve::dialogue {
namespace {

std::string trim(std::string value) {
    const auto first = std::find_if_not(value.begin(), value.end(), [](unsigned char c) {
        return std::isspace(c) != 0;
    });
    const auto last = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char c) {
                          return std::isspace(c) != 0;
                      }).base();
    return first < last ? std::string(first, last) : std::string{};
}

std::vector<std::string> words(const std::string& line) {
    std::vector<std::string> out;
    std::string word;
    bool quoted = false;
    bool escaped = false;
    for (char c : line) {
        if (escaped) {
            word.push_back(c);
            escaped = false;
        } else if (c == '\\' && quoted) {
            escaped = true;
        } else if (c == '"') {
            quoted = !quoted;
        } else if (std::isspace(static_cast<unsigned char>(c)) && !quoted) {
            if (!word.empty()) {
                out.push_back(std::move(word));
                word.clear();
            }
        } else {
            word.push_back(c);
        }
    }
    if (!word.empty()) out.push_back(std::move(word));
    return out;
}

std::unordered_map<std::string, std::string> attributes(const std::vector<std::string>& tokens,
                                                        size_t begin) {
    std::unordered_map<std::string, std::string> out;
    for (size_t i = begin; i < tokens.size(); ++i) {
        const size_t equal = tokens[i].find('=');
        if (equal != std::string::npos)
            out[tokens[i].substr(0, equal)] = tokens[i].substr(equal + 1);
    }
    return out;
}

ConversationAsset::Node::Kind nodeKind(const std::string& kind, bool& ok) {
    ok = true;
    if (kind == "line") return ConversationAsset::Node::Kind::Line;
    if (kind == "branch") return ConversationAsset::Node::Kind::Branch;
    if (kind == "choice") return ConversationAsset::Node::Kind::Choice;
    if (kind == "call") return ConversationAsset::Node::Kind::Call;
    if (kind == "command") return ConversationAsset::Node::Kind::Command;
    if (kind == "wait") return ConversationAsset::Node::Kind::Wait;
    if (kind == "end") return ConversationAsset::Node::Kind::End;
    ok = false;
    return ConversationAsset::Node::Kind::End;
}

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
    assets.clear();
    ConversationAsset* asset = nullptr;
    ConversationAsset::Node* node = nullptr;
    std::istringstream input(source);
    std::string raw;
    int lineNumber = 0;
    const auto error = [&](const std::string& message) {
        diagnostics.push_back(
            {ConversationDiagnostic::Severity::Error, path, lineNumber, message});
    };
    while (std::getline(input, raw)) {
        ++lineNumber;
        const size_t comment = raw.find("//");
        const std::string line = trim(raw.substr(0, comment));
        if (line.empty()) continue;
        const auto tokens = words(line);
        if (tokens.empty()) continue;
        if (tokens[0] == "conversation") {
            if (tokens.size() < 2) {
                error("conversation requires an id");
                continue;
            }
            assets.push_back({});
            asset = &assets.back();
            asset->id = tokens[1];
            const auto attrs = attributes(tokens, 2);
            if (const auto it = attrs.find("entry"); it != attrs.end()) asset->entry = it->second;
            if (const auto it = attrs.find("version"); it != attrs.end()) {
                int version = 0;
                std::from_chars(it->second.data(), it->second.data() + it->second.size(), version);
                asset->version = version;
            }
            if (const auto it = attrs.find("params"); it != attrs.end()) {
                std::istringstream params(it->second);
                std::string parameter;
                while (std::getline(params, parameter, ',')) asset->parameters.push_back(parameter);
            }
            node = nullptr;
        } else if (tokens[0] == "endconversation") {
            asset = nullptr;
            node = nullptr;
        } else if (!asset) {
            // Pool syntax is compiled by DnutParser; conversation blocks may coexist in one file.
            continue;
        } else if (tokens[0] == "node") {
            if (!asset || tokens.size() < 3) {
                error("node requires an active conversation, id, and kind");
                continue;
            }
            bool validKind = false;
            asset->nodes.push_back({});
            node = &asset->nodes.back();
            node->id = tokens[1];
            node->kind = nodeKind(tokens[2], validKind);
            if (!validKind) error("unknown node kind '" + tokens[2] + "'");
            const auto attrs = attributes(tokens, 3);
            const auto set = [&](const char* name, std::string& value) {
                if (const auto it = attrs.find(name); it != attrs.end()) value = it->second;
            };
            set("next", node->next);
            set("speaker", node->speaker);
            set("text", node->text);
            set("pool", node->pool);
            set("i18n", node->i18nKey);
            set("voice", node->voice);
            set("target", node->target);
            set("return", node->returnNode);
            set("result", node->expression);
        } else if (tokens[0] == "when" || tokens[0] == "else" || tokens[0] == "option") {
            if (!node) {
                error("route requires a preceding node");
                continue;
            }
            const size_t arrow = line.rfind("->");
            if (arrow == std::string::npos) {
                error("route requires '-> target'");
                continue;
            }
            const std::string target = trim(line.substr(arrow + 2));
            std::string expression;
            if (tokens[0] == "else") expression = "else";
            else expression = trim(line.substr(tokens[0].size(), arrow - tokens[0].size()));
            node->routes.emplace_back(std::move(expression), target);
        } else {
            error("unexpected conversation statement '" + tokens[0] + "'");
        }
    }
    if (asset) error("conversation is missing endconversation");
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
