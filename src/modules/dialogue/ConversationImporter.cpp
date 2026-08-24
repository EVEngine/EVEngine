#include "dialogue/ConversationImporter.h"

#include <algorithm>
#include <cctype>
#include <iterator>
#include <sstream>
#include <unordered_set>

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
    std::vector<std::pair<int, std::string>> out;
    std::istringstream                       stream(source);
    std::string                              line;
    int                                      number = 0;
    while (std::getline(stream, line)) {
        ++number;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        out.emplace_back(number, std::move(line));
    }
    return out;
}

std::string assetIdFromPath(const std::string& path) {
    const size_t slash = path.find_last_of("/\\");
    const size_t dot   = path.find_last_of('.');
    const size_t begin = slash == std::string::npos ? 0 : slash + 1;
    const size_t end   = dot == std::string::npos || dot < begin ? path.size() : dot;
    std::string  id    = path.substr(begin, end - begin);
    for (char& ch : id)
        if (!std::isalnum(static_cast<unsigned char>(ch)) && ch != '_' && ch != '-' && ch != '.') ch = '_';
    return id.empty() ? "imported" : id;
}

bool parseLink(const std::string& text, std::string& label, std::string& target) {
    if (text.size() < 4 || text.substr(0, 2) != "[[" || text.substr(text.size() - 2) != "]]") return false;
    const std::string body  = trim(text.substr(2, text.size() - 4));
    size_t            split = body.find("->");
    size_t            width = 2;
    if (split == std::string::npos) {
        split = body.find('|');
        width = 1;
    }
    if (split == std::string::npos)
        label = target = trim(body);
    else {
        label  = trim(body.substr(0, split));
        target = trim(body.substr(split + width));
    }
    return !label.empty() && !target.empty();
}

void addDiagnostic(std::vector<ConversationDiagnostic>& diagnostics, ConversationDiagnostic::Severity severity,
                   const std::string& path, int line, const std::string& message) {
    diagnostics.push_back({severity, path, line, message});
}

bool buildAsset(const std::vector<Passage>& passages, const std::string& path, std::vector<ConversationAsset>& assets,
                std::vector<ConversationDiagnostic>& diagnostics) {
    if (passages.empty()) {
        addDiagnostic(diagnostics, ConversationDiagnostic::Severity::Error, path, 0, "no dialogue passages found");
        return false;
    }
    ConversationAsset asset;
    asset.id    = assetIdFromPath(path);
    asset.entry = passages.front().title;
    std::unordered_set<std::string> titles;
    for (const auto& passage : passages) {
        if (passage.title.empty() || !titles.insert(passage.title).second)
            addDiagnostic(diagnostics, ConversationDiagnostic::Severity::Error, path, passage.line,
                          "empty or duplicate passage title: " + passage.title);
    }
    for (const auto& passage : passages) {
        std::vector<ConversationAsset::Node> nodes;
        size_t                               index = 0;
        for (size_t bodyIndex = 0; bodyIndex < passage.body.size();) {
            const auto& [lineNumber, raw] = passage.body[bodyIndex];
            const std::string text        = trim(raw);
            if (text.empty() || text == "---" || text == "===") {
                ++bodyIndex;
                continue;
            }
            std::string label;
            std::string target;
            if (parseLink(text, label, target)) {
                ConversationAsset::Node node;
                node.kind = ConversationAsset::Node::Kind::Choice;
                node.id   = index == 0 ? passage.title : passage.title + "." + std::to_string(index);
                ++index;
                while (bodyIndex < passage.body.size()) {
                    const std::string candidate = trim(passage.body[bodyIndex].second);
                    if (!parseLink(candidate, label, target)) break;
                    node.routes.emplace_back(label, target);
                    ++bodyIndex;
                }
                nodes.push_back(std::move(node));
                continue;
            }
            ConversationAsset::Node node;
            node.id = index == 0 ? passage.title : passage.title + "." + std::to_string(index);
            ++index;
            if (text.rfind("<<jump ", 0) == 0 && text.size() > 9 && text.substr(text.size() - 2) == ">>") {
                node.kind = ConversationAsset::Node::Kind::Branch;
                node.routes.emplace_back("else", trim(text.substr(7, text.size() - 9)));
            } else {
                node.kind          = ConversationAsset::Node::Kind::Line;
                const size_t colon = text.find(':');
                if (colon != std::string::npos && colon > 0 && text.find("[[") == std::string::npos) {
                    node.speaker = trim(text.substr(0, colon));
                    node.text    = trim(text.substr(colon + 1));
                } else {
                    node.text = text;
                    if (text.find("[[") != std::string::npos)
                        addDiagnostic(diagnostics, ConversationDiagnostic::Severity::Warning, path, lineNumber,
                                      "inline passage links are imported as text");
                }
            }
            nodes.push_back(std::move(node));
            ++bodyIndex;
        }
        if (nodes.empty()) {
            ConversationAsset::Node end;
            end.id   = passage.title;
            end.kind = ConversationAsset::Node::Kind::End;
            nodes.push_back(std::move(end));
        }
        for (size_t i = 0; i + 1 < nodes.size(); ++i)
            if (nodes[i].kind == ConversationAsset::Node::Kind::Line) nodes[i].next = nodes[i + 1].id;
        if (nodes.back().kind == ConversationAsset::Node::Kind::Line) {
            ConversationAsset::Node end;
            end.id            = passage.title + ".end";
            end.kind          = ConversationAsset::Node::Kind::End;
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
    return lintConversations(assets, path, diagnostics);
}

}  // namespace

bool importYarnConversation(const std::string& source, const std::string& path, std::vector<ConversationAsset>& assets,
                            std::vector<ConversationDiagnostic>& diagnostics) {
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
    return buildAsset(passages, path, assets, diagnostics);
}

bool importTweeConversation(const std::string& source, const std::string& path, std::vector<ConversationAsset>& assets,
                            std::vector<ConversationDiagnostic>& diagnostics) {
    std::vector<Passage> passages;
    Passage              current;
    for (const auto& [number, raw] : splitLines(source)) {
        const std::string text = trim(raw);
        if (text.rfind("::", 0) == 0) {
            if (!current.title.empty()) passages.push_back(std::move(current));
            current           = {};
            current.title     = trim(text.substr(2));
            const size_t tags = current.title.find(" [");
            if (tags != std::string::npos) current.title = trim(current.title.substr(0, tags));
            current.line = number;
        } else if (!current.title.empty())
            current.body.emplace_back(number, raw);
    }
    if (!current.title.empty()) passages.push_back(std::move(current));
    return buildAsset(passages, path, assets, diagnostics);
}

}  // namespace eve::dialogue
