#include "dialogue/ConversationImporter.h"

#include "dialogue/DialogueSequence.h"

#include <algorithm>
#include <cctype>
#include <iterator>
#include <sstream>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace eve::dialogue {
namespace {

struct Passage {
    std::string                              title;
    int                                      line = 0;
    std::vector<std::pair<int, std::string>> body;
};

std::string trim(std::string value) {
    const auto first =
        std::find_if_not(value.begin(), value.end(), [](unsigned char ch) { return std::isspace(ch) != 0; });
    const auto last =
        std::find_if_not(value.rbegin(), value.rend(), [](unsigned char ch) { return std::isspace(ch) != 0; }).base();
    return first < last ? std::string(first, last) : std::string{};
}

std::vector<std::pair<int, std::string>> splitLines(const std::string& source) {
    std::vector<std::pair<int, std::string>> output;
    std::istringstream                       stream(source);
    std::string                              line;
    int                                      number = 0;
    while (std::getline(stream, line)) {
        ++number;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        output.emplace_back(number, std::move(line));
    }
    return output;
}

std::string assetIdFromPath(const std::string& path) {
    const std::size_t slash = path.find_last_of("/\\");
    const std::size_t dot   = path.find_last_of('.');
    const std::size_t begin = slash == std::string::npos ? 0 : slash + 1;
    const std::size_t end   = dot == std::string::npos || dot < begin ? path.size() : dot;
    std::string       id    = path.substr(begin, end - begin);
    for (char& character : id)
        if (!std::isalnum(static_cast<unsigned char>(character)) && character != '_' && character != '-' &&
            character != '.')
            character = '_';
    return id.empty() ? "imported" : id;
}

bool parseLink(const std::string& text, std::string& label, std::string& target) {
    if (text.size() < 4 || text.substr(0, 2) != "[[" || text.substr(text.size() - 2) != "]]") return false;
    const std::string body  = trim(text.substr(2, text.size() - 4));
    std::size_t       split = body.find("->");
    std::size_t       width = 2;
    if (split == std::string::npos) {
        split = body.find('|');
        width = 1;
    }
    if (split == std::string::npos) {
        split = body.find("<-");
        if (split != std::string::npos) {
            target = trim(body.substr(0, split));
            label  = trim(body.substr(split + 2));
            return !label.empty() && !target.empty();
        }
    }
    if (split == std::string::npos)
        label = target = trim(body);
    else {
        label  = trim(body.substr(0, split));
        target = trim(body.substr(split + width));
    }
    return !label.empty() && !target.empty();
}

std::string commandArgument(const std::string& text, const std::string& command) {
    const std::string prefix = "<<" + command;
    if (text.rfind(prefix, 0) != 0 || text.size() < prefix.size() + 2 || text.substr(text.size() - 2) != ">>")
        return {};
    return trim(text.substr(prefix.size(), text.size() - prefix.size() - 2));
}

void consumeLineTags(std::string& text, eve::dnut::SequenceNode& node) {
    std::size_t tag = text.find(" #");
    while (tag != std::string::npos) {
        const std::size_t end   = text.find(' ', tag + 2);
        const std::string value = text.substr(tag + 2, end == std::string::npos ? std::string::npos : end - tag - 2);
        if (value.rfind("line:", 0) == 0) node.payload.set("i18n", eve::Value::string(value.substr(5)));
        if (value.rfind("voice:", 0) == 0) node.payload.set("voice", eve::Value::string(value.substr(6)));
        text.erase(tag, end == std::string::npos ? std::string::npos : end - tag);
        tag = text.find(" #", tag);
    }
    text = trim(text);
}

void addDiagnostic(std::vector<ConversationDiagnostic>& diagnostics, ConversationDiagnostic::Severity severity,
                   const std::string& path, int line, const std::string& message) {
    diagnostics.push_back({severity, path, line, message});
}

eve::dnut::SequenceRoute route(std::string label, std::string target) {
    eve::dnut::SequenceRoute value;
    value.label  = std::move(label);
    value.target = std::move(target);
    return value;
}

bool isSequential(const std::string& type) { return type == "line" || type == "command" || type == "wait"; }

bool buildAsset(const std::vector<Passage>& passages, const std::string& path,
                std::vector<eve::dnut::SequenceAsset>& assets, std::vector<ConversationDiagnostic>& diagnostics) {
    if (passages.empty()) {
        addDiagnostic(diagnostics, ConversationDiagnostic::Severity::Error, path, 0, "no dialogue passages found");
        return false;
    }
    eve::dnut::SequenceAsset asset;
    asset.id    = assetIdFromPath(path);
    asset.entry = passages.front().title;
    std::unordered_set<std::string> titles;
    for (const auto& passage : passages)
        if (passage.title.empty() || !titles.insert(passage.title).second)
            addDiagnostic(diagnostics, ConversationDiagnostic::Severity::Error, path, passage.line,
                          "empty or duplicate passage title: " + passage.title);

    for (const auto& passage : passages) {
        std::vector<eve::dnut::SequenceNode> nodes;
        std::size_t                          index = 0;
        for (std::size_t bodyIndex = 0; bodyIndex < passage.body.size();) {
            const auto& [lineNumber, raw] = passage.body[bodyIndex];
            const std::string text        = trim(raw);
            if (text.empty() || text == "---" || text == "===") {
                ++bodyIndex;
                continue;
            }
            std::string label;
            std::string target;
            if (parseLink(text, label, target) || text.rfind("->", 0) == 0) {
                eve::dnut::SequenceNode node;
                node.type = "choice";
                node.id   = index == 0 ? passage.title : passage.title + "." + std::to_string(index);
                ++index;
                while (bodyIndex < passage.body.size()) {
                    const std::string candidate = trim(passage.body[bodyIndex].second);
                    if (!parseLink(candidate, label, target)) {
                        if (candidate.rfind("->", 0) != 0) break;
                        label = trim(candidate.substr(2));
                        target.clear();
                        if (bodyIndex + 1 < passage.body.size())
                            target = commandArgument(trim(passage.body[bodyIndex + 1].second), "jump");
                        if (target.empty()) {
                            addDiagnostic(diagnostics, ConversationDiagnostic::Severity::Error, path,
                                          passage.body[bodyIndex].first,
                                          "Yarn shortcut option requires a following <<jump Target>>");
                            break;
                        }
                        ++bodyIndex;
                    }
                    node.routes.push_back(route(label, target));
                    ++bodyIndex;
                }
                nodes.push_back(std::move(node));
                continue;
            }

            eve::dnut::SequenceNode node;
            node.id = index == 0 ? passage.title : passage.title + "." + std::to_string(index);
            ++index;
            const std::string jump = commandArgument(text, "jump");
            const std::string wait = commandArgument(text, "wait");
            const std::string call = commandArgument(text, "call");
            const std::string set  = commandArgument(text, "set");
            if (!jump.empty()) {
                node.type = "branch";
                node.routes.push_back(route("else", jump));
            } else if (text == "<<stop>>") {
                node.type = "end";
            } else if (!wait.empty()) {
                node.type = "wait";
                node.payload.set("duration", eve::Value::string(wait));
            } else if (!call.empty() || !set.empty()) {
                node.type = "command";
                node.payload.set("name", eve::Value::string(!call.empty() ? "call" : "set"));
                node.payload.set("resultLocal", eve::Value::string(!call.empty() ? call : set));
            } else {
                node.type            = "line";
                std::string lineText = text;
                consumeLineTags(lineText, node);
                const std::size_t colon = lineText.find(':');
                if (colon != std::string::npos && colon > 0 && lineText.find("[[") == std::string::npos) {
                    node.payload.set("speaker", eve::Value::string(trim(lineText.substr(0, colon))));
                    node.payload.set("text", eve::Value::string(trim(lineText.substr(colon + 1))));
                } else {
                    node.payload.set("text", eve::Value::string(lineText));
                    if (lineText.find("[[") != std::string::npos)
                        addDiagnostic(diagnostics, ConversationDiagnostic::Severity::Warning, path, lineNumber,
                                      "inline passage links are imported as text");
                }
            }
            nodes.push_back(std::move(node));
            ++bodyIndex;
        }
        if (nodes.empty()) {
            eve::dnut::SequenceNode end;
            end.id   = passage.title;
            end.type = "end";
            nodes.push_back(std::move(end));
        }
        for (std::size_t nodeIndex = 0; nodeIndex + 1 < nodes.size(); ++nodeIndex)
            if (isSequential(nodes[nodeIndex].type)) nodes[nodeIndex].next = nodes[nodeIndex + 1].id;
        if (isSequential(nodes.back().type)) {
            eve::dnut::SequenceNode end;
            end.id            = passage.title + ".end";
            end.type          = "end";
            nodes.back().next = end.id;
            nodes.push_back(std::move(end));
        }
        asset.nodes.insert(asset.nodes.end(), std::make_move_iterator(nodes.begin()),
                           std::make_move_iterator(nodes.end()));
    }
    if (std::any_of(diagnostics.begin(), diagnostics.end(), [](const auto& diagnostic) {
            return diagnostic.severity == ConversationDiagnostic::Severity::Error;
        }))
        return false;
    assets.push_back(std::move(asset));
    return lintConversations(assets, path, diagnostics).ok();
}

std::vector<Passage> parseYarnPassages(const std::string& source) {
    std::vector<Passage> passages;
    Passage              current;
    bool                 inBody = false;
    for (const auto& [number, raw] : splitLines(source)) {
        const std::string text = trim(raw);
        if (text.rfind("title:", 0) == 0) {
            if (!current.title.empty()) passages.push_back(std::move(current));
            current       = {};
            current.title = trim(text.substr(6));
            current.line  = number;
            inBody        = false;
        } else if (text == "---")
            inBody = !current.title.empty();
        else if (text == "===") {
            if (!current.title.empty()) passages.push_back(std::move(current));
            current = {};
            inBody  = false;
        } else if (inBody)
            current.body.emplace_back(number, raw);
    }
    if (!current.title.empty()) passages.push_back(std::move(current));
    return passages;
}

}  // namespace

eve::Result<std::vector<eve::dnut::SequenceAsset>> importYarnConversation(
    const std::string& source, const std::string& path, std::vector<ConversationDiagnostic>& diagnostics) {
    std::vector<eve::dnut::SequenceAsset> assets;
    if (!buildAsset(parseYarnPassages(source), path, assets, diagnostics)) {
        const std::string message = diagnostics.empty() ? "Yarn import failed" : diagnostics.front().message;
        return eve::Result<std::vector<eve::dnut::SequenceAsset>>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::ParseError, message, path, {}, "dialogue.import.yarn"));
    }
    return eve::Result<std::vector<eve::dnut::SequenceAsset>>::success(std::move(assets));
}

eve::Result<std::vector<eve::dnut::SequenceAsset>> importTweeConversation(
    const std::string& source, const std::string& path, std::vector<ConversationDiagnostic>& diagnostics) {
    std::vector<Passage> passages;
    Passage              current;
    for (const auto& [number, raw] : splitLines(source)) {
        const std::string text = trim(raw);
        if (text.rfind("::", 0) == 0) {
            if (!current.title.empty()) passages.push_back(std::move(current));
            current                = {};
            current.title          = trim(text.substr(2));
            const std::size_t tags = current.title.find(" [");
            if (tags != std::string::npos) current.title = trim(current.title.substr(0, tags));
            current.line = number;
        } else if (!current.title.empty()) {
            current.body.emplace_back(number, raw);
        }
    }
    if (!current.title.empty()) passages.push_back(std::move(current));
    passages.erase(std::remove_if(passages.begin(), passages.end(),
                                  [](const Passage& passage) {
                                      return passage.title == "StoryTitle" || passage.title == "StoryData";
                                  }),
                   passages.end());
    std::vector<eve::dnut::SequenceAsset> assets;
    if (!buildAsset(passages, path, assets, diagnostics)) {
        const std::string message = diagnostics.empty() ? "Twee import failed" : diagnostics.front().message;
        return eve::Result<std::vector<eve::dnut::SequenceAsset>>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::ParseError, message, path, {}, "dialogue.import.twee"));
    }
    return eve::Result<std::vector<eve::dnut::SequenceAsset>>::success(std::move(assets));
}

}  // namespace eve::dialogue
