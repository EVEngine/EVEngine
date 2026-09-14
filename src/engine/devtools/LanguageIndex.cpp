#include "devtools/LanguageIndex.h"

#include "common/ScriptModule.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>

namespace eve::dev::lsp {
namespace {

enum class TokenKind { Identifier, String, Symbol };

struct Token {
    TokenKind   kind = TokenKind::Symbol;
    std::string text;
    size_t      offset = 0;
    Range       range;
};

struct ImportBinding {
    std::string imported;
    std::string local;
    std::string targetUri;
    size_t      importedToken   = 0;
    size_t      localToken      = 0;
    bool        namespaceImport = false;
};

bool identifierStart(char value) { return std::isalpha(static_cast<unsigned char>(value)) != 0 || value == '_'; }

bool identifierPart(char value) { return std::isalnum(static_cast<unsigned char>(value)) != 0 || value == '_'; }

bool validIdentifier(std::string_view value) {
    if (value.empty() || !identifierStart(value.front())) return false;
    return std::all_of(value.begin() + 1, value.end(), identifierPart);
}

std::vector<Token> tokenize(std::string_view source) {
    std::vector<Token> result;
    size_t             line    = 0;
    size_t             column  = 0;
    const auto         advance = [&](char value, size_t& currentLine, size_t& currentColumn) {
        if (value == '\n') {
            ++currentLine;
            currentColumn = 0;
        } else {
            ++currentColumn;
        }
    };

    for (size_t i = 0; i < source.size();) {
        const char value = source[i];
        const char next  = i + 1 < source.size() ? source[i + 1] : 0;
        if (std::isspace(static_cast<unsigned char>(value)) != 0) {
            advance(value, line, column);
            ++i;
            continue;
        }
        if (value == '/' && next == '/') {
            while (i < source.size() && source[i] != '\n') {
                ++i;
                ++column;
            }
            continue;
        }
        if (value == '/' && next == '*') {
            advance(value, line, column);
            advance(next, line, column);
            i += 2;
            while (i < source.size()) {
                const char current = source[i];
                if (current == '*' && i + 1 < source.size() && source[i + 1] == '/') {
                    advance(current, line, column);
                    advance('/', line, column);
                    i += 2;
                    break;
                }
                advance(current, line, column);
                ++i;
            }
            continue;
        }

        const size_t   tokenOffset = i;
        const Position start{line, column};
        if (identifierStart(value)) {
            ++i;
            ++column;
            while (i < source.size() && identifierPart(source[i])) {
                ++i;
                ++column;
            }
            result.push_back({TokenKind::Identifier,
                              std::string(source.substr(tokenOffset, i - tokenOffset)),
                              tokenOffset,
                              {start, {line, column}}});
            continue;
        }

        const bool verbatim = value == '@' && (next == '"' || next == '\'');
        if (value == '"' || value == '\'' || verbatim) {
            const char quote = verbatim ? next : value;
            if (verbatim) {
                i += 2;
                column += 2;
            } else {
                ++i;
                ++column;
            }
            std::string text;
            while (i < source.size()) {
                const char current = source[i];
                if (!verbatim && current == '\\' && i + 1 < source.size()) {
                    text += source[i + 1];
                    advance(current, line, column);
                    ++i;
                    advance(source[i], line, column);
                    ++i;
                    continue;
                }
                if (current == quote) {
                    if (verbatim && i + 1 < source.size() && source[i + 1] == quote) {
                        text += quote;
                        i += 2;
                        column += 2;
                        continue;
                    }
                    ++i;
                    ++column;
                    break;
                }
                text += current;
                advance(current, line, column);
                ++i;
            }
            result.push_back({TokenKind::String, std::move(text), tokenOffset, {start, {line, column}}});
            continue;
        }

        ++i;
        ++column;
        result.push_back({TokenKind::Symbol, std::string(1, value), tokenOffset, {start, {line, column}}});
    }
    return result;
}

std::string resolveImport(std::string_view importer, std::string_view specifier) {
    script::ScriptModuleRequest request{std::string(importer), std::string(specifier)};
    std::string                 canonical;
    std::string                 error;
    return script::ScriptModuleResolver::canonicalize(request, canonical, error) ? canonical : std::string{};
}

bool contains(const Range& range, Position position) {
    if (position.line < range.start.line || position.line > range.end.line) return false;
    if (position.line == range.start.line && position.character < range.start.character) return false;
    if (position.line == range.end.line && position.character > range.end.character) return false;
    return true;
}

bool isKeyword(std::string_view word) {
    static const std::unordered_set<std::string> words = {
        "if",       "else",   "switch",  "case",     "default", "while",     "do",         "for",
        "foreach",  "break",  "continue","return",   "try",     "catch",     "throw",      "yield",
        "resume",   "await",  "local",   "const",    "enum",    "class",     "function",   "constructor",
        "static",   "persist","async",   "export",   "import",  "from",      "as",         "extends",
        "in",       "typeof", "instanceof","clone",  "delete",  "this",      "base",       "self",
        "true",     "false",  "null",    "nullrel"};
    return words.count(std::string(word)) != 0;
}

}  // namespace

struct WorkspaceIndex::Unit {
    std::string                             canonicalUri;
    std::string                             clientUri;
    std::string                             source;
    std::vector<Token>                      tokens;
    std::unordered_map<std::string, size_t> declarations;
    std::unordered_map<std::string, size_t> exports;
    std::vector<ImportBinding>              imports;
    std::unordered_map<std::string, uint32_t> nameTypes;
    std::unordered_map<std::string, uint32_t> nameModifiers;
};

struct WorkspaceIndex::Target {
    std::string definingUri;
    std::string name;
    std::string localUri;
    std::string localName;
    bool        localAlias = false;
};

struct WorkspaceIndex::Impl {
    std::unordered_map<std::string, Unit> units;
};

WorkspaceIndex::WorkspaceIndex() : impl_(std::make_unique<Impl>()) {}
WorkspaceIndex::~WorkspaceIndex() = default;

void WorkspaceIndex::update(std::string canonicalUri, std::string clientUri, std::string source) {
    Unit next;
    next.canonicalUri = std::move(canonicalUri);
    next.clientUri    = std::move(clientUri);
    next.source       = std::move(source);
    next.tokens       = tokenize(next.source);

    const auto& tokens = next.tokens;
    for (size_t i = 0; i < tokens.size(); ++i) {
        if (tokens[i].kind != TokenKind::Identifier) continue;
        const std::string& word = tokens[i].text;
        const auto         remember = [&](const std::string& name, uint32_t type, uint32_t modifiers) {
            next.nameTypes.try_emplace(name, type);
            next.nameModifiers.try_emplace(name, modifiers);
        };
        if ((word == "local" || word == "function" || word == "class" || word == "const" || word == "persist") &&
            i + 1 < tokens.size() && tokens[i + 1].kind == TokenKind::Identifier &&
            !isKeyword(tokens[i + 1].text)) {
            next.declarations.try_emplace(tokens[i + 1].text, i + 1);
            if (word == "class")
                remember(tokens[i + 1].text, SemanticTypes::Class, 0);
            else if (word == "function")
                remember(tokens[i + 1].text, SemanticTypes::Function, 0);
            else if (word == "const")
                remember(tokens[i + 1].text, SemanticTypes::Variable, SemanticMods::Readonly);
            else
                remember(tokens[i + 1].text, SemanticTypes::Variable, 0);
        }
        if (word == "export" && i + 2 < tokens.size() && tokens[i + 1].kind == TokenKind::Identifier &&
            (tokens[i + 1].text == "function" || tokens[i + 1].text == "class" || tokens[i + 1].text == "const") &&
            tokens[i + 2].kind == TokenKind::Identifier) {
            next.declarations.try_emplace(tokens[i + 2].text, i + 2);
            next.exports[tokens[i + 2].text] = i + 2;
            if (tokens[i + 1].text == "class")
                remember(tokens[i + 2].text, SemanticTypes::Class, 0);
            else if (tokens[i + 1].text == "function")
                remember(tokens[i + 2].text, SemanticTypes::Function, 0);
            else
                remember(tokens[i + 2].text, SemanticTypes::Variable, SemanticMods::Readonly);
        }
        if (word != "import" || i + 1 >= tokens.size()) continue;

        size_t cursor = i + 1;
        struct Pending {
            std::string imported;
            std::string local;
            size_t      importedToken;
            size_t      localToken;
            bool        ns;
        };
        std::vector<Pending> pending;
        if (tokens[cursor].text == "{") {
            ++cursor;
            while (cursor < tokens.size() && tokens[cursor].text != "}") {
                if (tokens[cursor].kind != TokenKind::Identifier) {
                    ++cursor;
                    continue;
                }
                Pending binding{tokens[cursor].text, tokens[cursor].text, cursor, cursor, false};
                ++cursor;
                if (cursor + 1 < tokens.size() && tokens[cursor].text == "as" &&
                    tokens[cursor + 1].kind == TokenKind::Identifier) {
                    binding.local      = tokens[cursor + 1].text;
                    binding.localToken = cursor + 1;
                    cursor += 2;
                }
                pending.push_back(std::move(binding));
                if (cursor < tokens.size() && tokens[cursor].text == ",") ++cursor;
            }
            if (cursor < tokens.size()) ++cursor;
        } else if (tokens[cursor].text == "*" && cursor + 2 < tokens.size() && tokens[cursor + 1].text == "as" &&
                   tokens[cursor + 2].kind == TokenKind::Identifier) {
            pending.push_back({"*", tokens[cursor + 2].text, cursor, cursor + 2, true});
            cursor += 3;
        }
        if (pending.empty() || cursor + 1 >= tokens.size() || tokens[cursor].text != "from" ||
            tokens[cursor + 1].kind != TokenKind::String)
            continue;
        const std::string target = resolveImport(next.canonicalUri, tokens[cursor + 1].text);
        for (const Pending& binding : pending)
            next.imports.push_back(
                {binding.imported, binding.local, target, binding.importedToken, binding.localToken, binding.ns});
    }
    impl_->units[next.canonicalUri] = std::move(next);
}

void WorkspaceIndex::remove(std::string_view canonicalUri) { impl_->units.erase(std::string(canonicalUri)); }

const WorkspaceIndex::Unit* WorkspaceIndex::unit(std::string_view canonicalUri) const {
    const auto found = impl_->units.find(std::string(canonicalUri));
    return found == impl_->units.end() ? nullptr : &found->second;
}

std::optional<WorkspaceIndex::Target> WorkspaceIndex::targetAt(const Unit& current, Position position) const {
    size_t tokenIndex = current.tokens.size();
    for (size_t i = 0; i < current.tokens.size(); ++i) {
        if (current.tokens[i].kind == TokenKind::Identifier && contains(current.tokens[i].range, position)) {
            tokenIndex = i;
            break;
        }
    }
    if (tokenIndex == current.tokens.size()) return std::nullopt;
    const std::string& name = current.tokens[tokenIndex].text;
    for (const ImportBinding& binding : current.imports) {
        if (!binding.namespaceImport || tokenIndex < 2) continue;
        if (current.tokens[tokenIndex - 2].kind == TokenKind::Identifier &&
            current.tokens[tokenIndex - 2].text == binding.local && current.tokens[tokenIndex - 1].text == ".")
            return Target{binding.targetUri, name, current.canonicalUri, name, false};
    }
    for (const ImportBinding& binding : current.imports) {
        if (tokenIndex == binding.importedToken)
            return Target{binding.targetUri, binding.imported, current.canonicalUri, binding.local, false};
        if (name == binding.local) {
            const bool alias = binding.local != binding.imported;
            return Target{binding.targetUri, binding.imported, current.canonicalUri, binding.local, alias};
        }
    }
    if (current.exports.find(name) != current.exports.end())
        return Target{current.canonicalUri, name, current.canonicalUri, name, false};
    if (current.declarations.find(name) != current.declarations.end())
        return Target{current.canonicalUri, name, current.canonicalUri, name, true};
    return std::nullopt;
}

std::optional<Location> WorkspaceIndex::targetDefinition(const Target& target) const {
    const Unit* defining = unit(target.definingUri);
    if (defining == nullptr) return std::nullopt;
    const auto exported = defining->exports.find(target.name);
    const auto declared = defining->declarations.find(target.name);
    if (exported != defining->exports.end())
        return Location{defining->clientUri, defining->tokens[exported->second].range};
    if (declared != defining->declarations.end())
        return Location{defining->clientUri, defining->tokens[declared->second].range};
    return std::nullopt;
}

std::vector<Location> WorkspaceIndex::targetReferences(const Target& target, bool includeDeclaration) const {
    std::vector<Location> result;
    const auto            appendNamed = [&](const Unit& source, std::string_view name, bool declaration) {
        std::unordered_set<size_t> declarationTokens;
        for (const auto& [_, token] : source.declarations) declarationTokens.insert(token);
        for (size_t i = 0; i < source.tokens.size(); ++i) {
            if (source.tokens[i].kind != TokenKind::Identifier || source.tokens[i].text != name) continue;
            const bool isDeclaration = declarationTokens.count(i) != 0;
            if (!declaration && isDeclaration) continue;
            if (!includeDeclaration && isDeclaration) continue;
            result.push_back({source.clientUri, source.tokens[i].range});
        }
    };

    if (target.localAlias) {
        if (const Unit* local = unit(target.localUri)) appendNamed(*local, target.localName, true);
    } else {
        if (const Unit* defining = unit(target.definingUri)) appendNamed(*defining, target.name, true);
        for (const auto& [_, source] : impl_->units) {
            for (const ImportBinding& binding : source.imports) {
                if (binding.targetUri != target.definingUri) continue;
                if (binding.namespaceImport) {
                    for (size_t i = 0; i + 2 < source.tokens.size(); ++i) {
                        if (source.tokens[i].kind != TokenKind::Identifier || source.tokens[i].text != binding.local ||
                            source.tokens[i + 1].text != "." || source.tokens[i + 2].kind != TokenKind::Identifier ||
                            source.tokens[i + 2].text != target.name)
                            continue;
                        result.push_back({source.clientUri, source.tokens[i + 2].range});
                    }
                    continue;
                }
                if (binding.imported != target.name) continue;
                result.push_back({source.clientUri, source.tokens[binding.importedToken].range});
                if (binding.local != binding.imported) continue;
                for (size_t i = 0; i < source.tokens.size(); ++i) {
                    if (source.tokens[i].kind != TokenKind::Identifier || source.tokens[i].text != binding.local)
                        continue;
                    result.push_back({source.clientUri, source.tokens[i].range});
                }
            }
        }
    }
    std::sort(result.begin(), result.end(), [](const Location& a, const Location& b) {
        if (a.uri != b.uri) return a.uri < b.uri;
        if (a.range.start.line != b.range.start.line) return a.range.start.line < b.range.start.line;
        return a.range.start.character < b.range.start.character;
    });
    result.erase(std::unique(result.begin(), result.end(),
                             [](const Location& a, const Location& b) {
                                 return a.uri == b.uri && a.range.start.line == b.range.start.line &&
                                        a.range.start.character == b.range.start.character;
                             }),
                 result.end());
    return result;
}

std::vector<SemanticToken> WorkspaceIndex::semanticTokens(std::string_view canonicalUri) const {
    const Unit* current = unit(canonicalUri);
    if (current == nullptr) return {};
    const auto& tokens = current->tokens;
    std::vector<SemanticToken> result;
    for (size_t i = 0; i < tokens.size(); ++i) {
        if (tokens[i].kind != TokenKind::Identifier) continue;
        const std::string& name = tokens[i].text;
        if (name == "true" || name == "false" || name == "null" || name == "nullrel" || name == "this" ||
            name == "base" || name == "self") {
            result.push_back({tokens[i].range.start, name.size(), SemanticTypes::Keyword, 0, name});
            continue;
        }
        if (isKeyword(name)) continue;
        const Token*       prev = i > 0 ? &tokens[i - 1] : nullptr;
        const Token*       next = i + 1 < tokens.size() ? &tokens[i + 1] : nullptr;
        const bool         called = next && next->text == "(";
        const bool         member = prev && prev->text == ".";
        SemanticToken token;
        token.start    = tokens[i].range.start;
        token.length   = name.size();
        token.name     = name;
        token.type     = SemanticTypes::Variable;
        token.modifiers = 0;

        const std::string prevText = prev && prev->kind == TokenKind::Identifier ? prev->text : std::string{};
        if (prevText == "class" || prevText == "extends" || (prev && prev->text == ":")) {
            token.type      = SemanticTypes::Class;
            token.modifiers = prevText == "class" ? SemanticMods::Declaration : 0;
        } else if (prevText == "function") {
            token.type      = SemanticTypes::Function;
            token.modifiers = SemanticMods::Declaration;
        } else if (prevText == "local" || prevText == "persist") {
            token.type      = SemanticTypes::Variable;
            token.modifiers = SemanticMods::Declaration;
        } else if (prevText == "const") {
            token.type      = SemanticTypes::Variable;
            token.modifiers = SemanticMods::Declaration | SemanticMods::Readonly;
        } else if (member && called) {
            token.type = (!name.empty() && std::isupper(static_cast<unsigned char>(name.front())) != 0)
                             ? SemanticTypes::Class
                             : SemanticTypes::Method;
        } else if (member) {
            token.type = SemanticTypes::Property;
        } else if (called) {
            const auto found = current->nameTypes.find(name);
            if (found != current->nameTypes.end() && found->second == SemanticTypes::Class)
                token.type = SemanticTypes::Class;
            else if (!name.empty() && std::isupper(static_cast<unsigned char>(name.front())) != 0)
                token.type = SemanticTypes::Class;
            else
                token.type = SemanticTypes::Function;
        } else {
            const auto found = current->nameTypes.find(name);
            if (found != current->nameTypes.end()) token.type = found->second;
            const auto mods = current->nameModifiers.find(name);
            if (mods != current->nameModifiers.end()) token.modifiers = mods->second;
        }
        result.push_back(std::move(token));
    }
    return result;
}

std::optional<Location> WorkspaceIndex::definition(std::string_view canonicalUri, Position position) const {
    const Unit* current = unit(canonicalUri);
    if (current == nullptr) return std::nullopt;
    const auto target = targetAt(*current, position);
    return target ? targetDefinition(*target) : std::nullopt;
}

std::vector<Location> WorkspaceIndex::references(std::string_view canonicalUri, Position position,
                                                 bool includeDeclaration) const {
    const Unit* current = unit(canonicalUri);
    if (current == nullptr) return {};
    const auto target = targetAt(*current, position);
    return target ? targetReferences(*target, includeDeclaration) : std::vector<Location>{};
}

std::optional<std::vector<TextEdit>> WorkspaceIndex::rename(std::string_view canonicalUri, Position position,
                                                            std::string_view newName) const {
    if (!validIdentifier(newName)) return std::nullopt;
    const Unit* current = unit(canonicalUri);
    if (current == nullptr) return std::nullopt;
    const auto target = targetAt(*current, position);
    if (!target || !targetDefinition(*target)) return std::nullopt;
    std::vector<TextEdit> edits;
    for (const Location& location : targetReferences(*target, true)) edits.push_back({location, std::string(newName)});
    return edits.empty() ? std::nullopt : std::optional<std::vector<TextEdit>>(std::move(edits));
}

}  // namespace eve::dev::lsp
