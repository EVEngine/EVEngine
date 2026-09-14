#include "devtools/LanguageServer.hpp"
#include "devtools/LanguageText.h"

#include "common/Runtime.h"
#include "common/ScriptModule.h"
#include "scripts.h"

#include <Poco/Dynamic/Var.h>
#include <Poco/JSON/Array.h>
#include <Poco/JSON/Object.h>
#include <Poco/JSON/Parser.h>
#include <Poco/JSON/Stringifier.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <regex>
#include <sstream>
#include <unordered_map>
#include <utility>

namespace eve::dev {
namespace {

std::string stringify(const Poco::Dynamic::Var& value) {
    std::ostringstream output;
    Poco::JSON::Stringifier::stringify(value, output);
    return output.str();
}

void writeFrame(std::ostream& output, const Poco::JSON::Object::Ptr& message) {
    const std::string body = stringify(Poco::Dynamic::Var(message));
    output << "Content-Length: " << body.size() << "\r\n\r\n" << body << std::flush;
}

bool readFrame(std::istream& input, std::string& body) {
    std::string line;
    size_t      length = 0;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) break;
        constexpr std::string_view prefix = "Content-Length:";
        if (line.rfind(prefix, 0) == 0) {
            try {
                length = static_cast<size_t>(std::stoull(line.substr(prefix.size())));
            } catch (const std::exception&) {
                return false;
            }
        }
    }
    if (length == 0 || !input) return false;
    body.resize(length);
    input.read(body.data(), static_cast<std::streamsize>(length));
    return static_cast<size_t>(input.gcount()) == length;
}

Poco::JSON::Object::Ptr rpcResponse(const Poco::Dynamic::Var& id, const Poco::Dynamic::Var& result) {
    Poco::JSON::Object::Ptr message = new Poco::JSON::Object();
    message->set("jsonrpc", "2.0");
    message->set("id", id);
    message->set("result", result);
    return message;
}

Poco::JSON::Object::Ptr rpcError(const Poco::Dynamic::Var& id, int code, std::string messageText) {
    Poco::JSON::Object::Ptr error = new Poco::JSON::Object();
    error->set("code", code);
    error->set("message", std::move(messageText));
    Poco::JSON::Object::Ptr message = new Poco::JSON::Object();
    message->set("jsonrpc", "2.0");
    message->set("id", id);
    message->set("error", error);
    return message;
}

int clampNonNegative(int value) { return value < 0 ? 0 : value; }

Poco::JSON::Object::Ptr rangeObject(const lsp::Range& value) {
    Poco::JSON::Object::Ptr start = new Poco::JSON::Object();
    start->set("line", static_cast<int>(value.start.line));
    start->set("character", static_cast<int>(value.start.character));
    Poco::JSON::Object::Ptr end = new Poco::JSON::Object();
    end->set("line", static_cast<int>(value.end.line));
    end->set("character", static_cast<int>(value.end.character));
    Poco::JSON::Object::Ptr result = new Poco::JSON::Object();
    result->set("start", start);
    result->set("end", end);
    return result;
}

Poco::JSON::Object::Ptr locationObject(const lsp::Location& value) {
    Poco::JSON::Object::Ptr result = new Poco::JSON::Object();
    result->set("uri", value.uri);
    result->set("range", rangeObject(value.range));
    return result;
}

lsp::Range parseRange(const Poco::JSON::Object::Ptr& value) {
    lsp::Range range;
    if (!value) return range;
    if (auto start = value->getObject("start")) {
        range.start.line      = static_cast<size_t>(clampNonNegative(start->optValue<int>("line", 0)));
        range.start.character = static_cast<size_t>(clampNonNegative(start->optValue<int>("character", 0)));
    }
    if (auto end = value->getObject("end")) {
        range.end.line      = static_cast<size_t>(clampNonNegative(end->optValue<int>("line", 0)));
        range.end.character = static_cast<size_t>(clampNonNegative(end->optValue<int>("character", 0)));
    }
    return range;
}

lsp::FormatOptions parseFormatOptions(const Poco::JSON::Object::Ptr& value) {
    lsp::FormatOptions options;
    if (!value) return options;
    options.tabSize      = static_cast<unsigned>(clampNonNegative(value->optValue<int>("tabSize", 4)));
    options.insertSpaces = value->optValue<bool>("insertSpaces", true);
    if (options.tabSize == 0) options.tabSize = 4;
    return options;
}

Poco::JSON::Array::Ptr textEditsArray(const std::vector<lsp::TextEdit>& edits) {
    Poco::JSON::Array::Ptr items = new Poco::JSON::Array();
    for (const lsp::TextEdit& edit : edits) {
        Poco::JSON::Object::Ptr item = new Poco::JSON::Object();
        item->set("range", rangeObject(edit.location.range));
        item->set("newText", edit.newText);
        items->add(item);
    }
    return items;
}

Poco::JSON::Object::Ptr semanticTokensLegend() {
    Poco::JSON::Array::Ptr types = new Poco::JSON::Array();
    types->add("namespace");
    types->add("class");
    types->add("function");
    types->add("method");
    types->add("variable");
    types->add("parameter");
    types->add("property");
    types->add("keyword");
    Poco::JSON::Array::Ptr modifiers = new Poco::JSON::Array();
    modifiers->add("declaration");
    modifiers->add("readonly");
    modifiers->add("defaultLibrary");
    Poco::JSON::Object::Ptr legend = new Poco::JSON::Object();
    legend->set("tokenTypes", types);
    legend->set("tokenModifiers", modifiers);
    return legend;
}

Poco::JSON::Array::Ptr semanticTokensData(const std::vector<lsp::SemanticToken>& tokens) {
    Poco::JSON::Array::Ptr data = new Poco::JSON::Array();
    uint32_t               prevLine = 0;
    uint32_t               prevChar = 0;
    for (const lsp::SemanticToken& token : tokens) {
        const auto line  = static_cast<uint32_t>(token.start.line);
        const auto start = static_cast<uint32_t>(token.start.character);
        const auto dLine = line - prevLine;
        const auto dStart = dLine == 0 ? start - prevChar : start;
        data->add(static_cast<int>(dLine));
        data->add(static_cast<int>(dStart));
        data->add(static_cast<int>(token.length));
        data->add(static_cast<int>(token.type));
        data->add(static_cast<int>(token.modifiers));
        prevLine = line;
        prevChar = start;
    }
    return data;
}

int completionKind(std::string_view kind) {
    if (kind == "function" || kind == "async") return 3;
    if (kind == "class") return 7;
    if (kind == "module" || kind == "slot") return 9;
    if (kind == "keyword") return 14;
    if (kind == "const" || kind == "export") return 21;
    return 6;
}

int symbolKind(std::string_view kind) {
    if (kind == "function" || kind == "async") return 12;
    if (kind == "class") return 5;
    if (kind == "const") return 14;
    return 13;
}

std::string percentDecode(std::string path) {
    for (size_t cursor = 0; cursor + 2 < path.size();) {
        if (path[cursor] != '%') {
            ++cursor;
            continue;
        }
        const auto hex = [](char value) -> int {
            if (value >= '0' && value <= '9') return value - '0';
            if (value >= 'a' && value <= 'f') return value - 'a' + 10;
            if (value >= 'A' && value <= 'F') return value - 'A' + 10;
            return -1;
        };
        const int high = hex(path[cursor + 1]);
        const int low  = hex(path[cursor + 2]);
        if (high < 0 || low < 0) {
            ++cursor;
            continue;
        }
        path.replace(cursor, 3, 1, static_cast<char>((high << 4) | low));
        ++cursor;
    }
    return path;
}

std::string pathFromFileUri(std::string_view uri) {
    std::string path;
    if (uri.rfind("file:///", 0) == 0)
        path = std::string(uri.substr(8));
    else if (uri.rfind("file://", 0) == 0)
        path = std::string(uri.substr(7));
    else
        return {};
    path = percentDecode(std::move(path));
#if defined(_WIN32)
    if (path.size() >= 3 && path[0] == '/' && std::isalpha(static_cast<unsigned char>(path[1])) != 0 && path[2] == ':')
        path.erase(path.begin());
#else
    if (!path.empty() && path.front() != '/') path.insert(path.begin(), '/');
#endif
    return path;
}

struct CursorWord {
    std::string word;
    std::string qualifier;
    bool        memberAccess = false;
};

CursorWord wordAt(std::string_view source, size_t line, size_t character) {
    size_t offset = 0;
    for (size_t current = 0; current < line && offset < source.size(); ++current) {
        const size_t newline = source.find('\n', offset);
        offset               = newline == std::string_view::npos ? source.size() : newline + 1;
    }
    offset = std::min(offset + character, source.size());
    size_t begin = offset;
    size_t end   = offset;
    while (begin > 0 && (std::isalnum(static_cast<unsigned char>(source[begin - 1])) != 0 || source[begin - 1] == '_'))
        --begin;
    while (end < source.size() && (std::isalnum(static_cast<unsigned char>(source[end])) != 0 || source[end] == '_'))
        ++end;
    CursorWord result;
    result.word = std::string(source.substr(begin, end - begin));
    size_t scan = begin;
    if (result.word.empty() && offset > 0 && source[offset - 1] == '.') scan = offset;
    while (scan > 0 && std::isspace(static_cast<unsigned char>(source[scan - 1])) != 0) --scan;
    if (scan > 0 && source[scan - 1] == '.') {
        result.memberAccess = true;
        size_t qualEnd      = scan - 1;
        while (qualEnd > 0 && std::isspace(static_cast<unsigned char>(source[qualEnd - 1])) != 0) --qualEnd;
        size_t qualBegin = qualEnd;
        while (qualBegin > 0 &&
               (std::isalnum(static_cast<unsigned char>(source[qualBegin - 1])) != 0 || source[qualBegin - 1] == '_'))
            --qualBegin;
        result.qualifier = std::string(source.substr(qualBegin, qualEnd - qualBegin));
    }
    return result;
}

int activeParameterAt(std::string_view source, size_t offset) {
    int    commas = 0;
    size_t depth  = 0;
    for (size_t i = offset; i > 0; --i) {
        const char value = source[i - 1];
        if (value == ')') {
            ++depth;
            continue;
        }
        if (value == '(') {
            if (depth == 0) return commas;
            --depth;
            continue;
        }
        if (value == ',' && depth == 0) ++commas;
        if (value == '\n' && depth == 0) break;
    }
    return commas;
}

std::string calleeBeforeParen(std::string_view source, size_t offset) {
    size_t i = offset;
    while (i > 0) {
        if (source[i - 1] == ')') {
            size_t depth = 1;
            --i;
            while (i > 0 && depth > 0) {
                --i;
                if (source[i] == ')') ++depth;
                else if (source[i] == '(')
                    --depth;
            }
            continue;
        }
        if (source[i - 1] == '(') {
            size_t end = i - 1;
            while (end > 0 && std::isspace(static_cast<unsigned char>(source[end - 1])) != 0) --end;
            size_t begin = end;
            while (begin > 0 &&
                   (std::isalnum(static_cast<unsigned char>(source[begin - 1])) != 0 || source[begin - 1] == '_'))
                --begin;
            return std::string(source.substr(begin, end - begin));
        }
        --i;
    }
    return {};
}

bool identifierStart(char value) { return std::isalpha(static_cast<unsigned char>(value)) != 0 || value == '_'; }
bool identifierPart(char value) { return std::isalnum(static_cast<unsigned char>(value)) != 0 || value == '_'; }

std::string trimTypeName(std::string_view value) {
    size_t begin = 0;
    while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin])) != 0) ++begin;
    size_t end = value.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) --end;
    if (end > begin && value[end - 1] == '?') --end;
    return std::string(value.substr(begin, end - begin));
}

bool isPrimitiveType(std::string_view type) {
    return type.empty() || type == "dynamic" || type == "void" || type == "null" || type == "int" || type == "float" ||
           type == "bool" || type == "string" || type.rfind("Array", 0) == 0 || type.rfind("Table", 0) == 0;
}

enum class ExprTokKind { Ident, Dot, LParen, RParen };

struct ExprTok {
    ExprTokKind kind = ExprTokKind::Ident;
    std::string text;
};

std::vector<ExprTok> tokenizeExpr(std::string_view source, size_t begin, size_t end) {
    std::vector<ExprTok> result;
    for (size_t i = begin; i < end && i < source.size();) {
        const char value = source[i];
        const char next  = i + 1 < source.size() ? source[i + 1] : 0;
        if (std::isspace(static_cast<unsigned char>(value)) != 0) {
            ++i;
            continue;
        }
        if (value == '/' && next == '/') {
            while (i < end && i < source.size() && source[i] != '\n') ++i;
            continue;
        }
        if (value == '/' && next == '*') {
            i += 2;
            while (i + 1 < source.size() && i < end && !(source[i] == '*' && source[i + 1] == '/')) ++i;
            i = std::min(i + 2, end);
            continue;
        }
        if (value == '"' || value == '\'') {
            const char quote = value;
            ++i;
            while (i < end && i < source.size() && source[i] != quote) {
                if (source[i] == '\\' && i + 1 < source.size()) i += 2;
                else
                    ++i;
            }
            if (i < end) ++i;
            continue;
        }
        if (identifierStart(value)) {
            size_t start = i++;
            while (i < end && i < source.size() && identifierPart(source[i])) ++i;
            result.push_back({ExprTokKind::Ident, std::string(source.substr(start, i - start))});
            continue;
        }
        if (value == '.') {
            result.push_back({ExprTokKind::Dot, "."});
            ++i;
            continue;
        }
        if (value == '(') {
            result.push_back({ExprTokKind::LParen, "("});
            ++i;
            continue;
        }
        if (value == ')') {
            result.push_back({ExprTokKind::RParen, ")"});
            ++i;
            continue;
        }
        ++i;
    }
    return result;
}

size_t skipCallTokens(const std::vector<ExprTok>& tokens, size_t index) {
    if (index >= tokens.size() || tokens[index].kind != ExprTokKind::LParen) return index;
    int depth = 0;
    while (index < tokens.size()) {
        if (tokens[index].kind == ExprTokKind::LParen) ++depth;
        else if (tokens[index].kind == ExprTokKind::RParen) {
            --depth;
            ++index;
            if (depth == 0) return index;
            continue;
        }
        ++index;
    }
    return index;
}

using SlotClassMap = std::unordered_map<std::string, std::string>;

std::string applyRootIdent(std::string_view name, bool called, const SlotClassMap& slots,
                           const script::BindingContractRegistry& bindings,
                           const std::function<std::string(std::string_view)>& localType) {
    if (name == "eve") return called ? std::string{} : std::string{"eve"};
    const auto slot = slots.find(std::string(name));
    if (slot != slots.end()) return slot->second;
    if (bindings.hasScriptClass(name)) return std::string(name);
    return localType(name);
}

std::string applyMember(std::string type, std::string_view name, bool called,
                        const script::BindingContractRegistry& bindings) {
    if (type == "eve") {
        return bindings.hasScriptClass(name) ? std::string(name) : std::string{};
    }
    if (isPrimitiveType(type)) return {};
    const script::BindingContract* contract = bindings.findMethod(type, name);
    if (contract == nullptr) return {};
    if (!called) return type;
    const std::string returned = trimTypeName(contract->returnType);
    if (isPrimitiveType(returned) || !bindings.hasScriptClass(returned)) return {};
    return returned;
}

std::string typeOfTokens(const std::vector<ExprTok>& tokens, const SlotClassMap& slots,
                         const script::BindingContractRegistry& bindings,
                         const std::function<std::string(std::string_view)>& localType) {
    std::string type;
    for (size_t i = 0; i < tokens.size();) {
        if (tokens[i].kind == ExprTokKind::Dot) {
            ++i;
            continue;
        }
        if (tokens[i].kind != ExprTokKind::Ident) {
            ++i;
            continue;
        }
        const std::string name = tokens[i].text;
        ++i;
        const bool called = i < tokens.size() && tokens[i].kind == ExprTokKind::LParen;
        if (called) i = skipCallTokens(tokens, i);
        type = type.empty() ? applyRootIdent(name, called, slots, bindings, localType)
                            : applyMember(std::move(type), name, called, bindings);
        if (type.empty()) return {};
    }
    return type;
}

size_t statementStart(std::string_view source, size_t end) {
    size_t i = end;
    while (i > 0) {
        const char value = source[i - 1];
        if (value == '\n' || value == ';' || value == '{' || value == '}') break;
        --i;
    }
    return i;
}

std::string inferAssignedType(std::string_view source, size_t offset, std::string_view name, const SlotClassMap& slots,
                              const script::BindingContractRegistry& bindings) {
    size_t match = std::string::npos;
    for (size_t i = 0; i + name.size() <= offset;) {
        if (!identifierStart(source[i]) || source.compare(i, name.size(), name) != 0) {
            ++i;
            continue;
        }
        const size_t after = i + name.size();
        if ((i > 0 && identifierPart(source[i - 1])) || (after < source.size() && identifierPart(source[after]))) {
            ++i;
            continue;
        }
        size_t cursor = after;
        while (cursor < offset && std::isspace(static_cast<unsigned char>(source[cursor])) != 0) ++cursor;
        if (cursor < offset && source[cursor] == ':') {
            ++cursor;
            while (cursor < offset && std::isspace(static_cast<unsigned char>(source[cursor])) != 0) ++cursor;
            size_t typeEnd = cursor;
            while (typeEnd < offset && (identifierPart(source[typeEnd]) || source[typeEnd] == '?' || source[typeEnd] == '.'))
                ++typeEnd;
            const std::string annotated = trimTypeName(source.substr(cursor, typeEnd - cursor));
            if (bindings.hasScriptClass(annotated)) {
                match = i;
                i     = typeEnd;
                continue;
            }
            cursor = typeEnd;
            while (cursor < offset && std::isspace(static_cast<unsigned char>(source[cursor])) != 0) ++cursor;
        }
        const bool assign = cursor < offset && (source[cursor] == '=' ||
                                                (source[cursor] == '<' && cursor + 1 < offset && source[cursor + 1] == '-'));
        if (!assign || (source[cursor] == '=' && cursor + 1 < offset && source[cursor + 1] == '=')) {
            ++i;
            continue;
        }
        match = i;
        i     = cursor + 1;
    }
    if (match == std::string::npos) return {};
    size_t cursor = match + name.size();
    while (cursor < offset && std::isspace(static_cast<unsigned char>(source[cursor])) != 0) ++cursor;
    if (cursor < offset && source[cursor] == ':') {
        ++cursor;
        while (cursor < offset && std::isspace(static_cast<unsigned char>(source[cursor])) != 0) ++cursor;
        size_t typeEnd = cursor;
        while (typeEnd < offset && identifierPart(source[typeEnd])) ++typeEnd;
        const std::string annotated = trimTypeName(source.substr(cursor, typeEnd - cursor));
        if (bindings.hasScriptClass(annotated)) return annotated;
        cursor = typeEnd;
        while (cursor < offset && std::isspace(static_cast<unsigned char>(source[cursor])) != 0) ++cursor;
    }
    if (cursor < offset && source[cursor] == '<' && cursor + 1 < offset && source[cursor + 1] == '-') cursor += 2;
    else if (cursor < offset && source[cursor] == '=')
        ++cursor;
    while (cursor < offset && std::isspace(static_cast<unsigned char>(source[cursor])) != 0) ++cursor;
    size_t rhsEnd = cursor;
    int    depth  = 0;
    while (rhsEnd < offset) {
        const char value = source[rhsEnd];
        if (value == '(' || value == '[' || value == '{') ++depth;
        else if (value == ')' || value == ']' || value == '}') {
            if (depth == 0) break;
            --depth;
        } else if (depth == 0 && (value == ';' || value == '\n'))
            break;
        ++rhsEnd;
    }
    const auto noopLocal = [](std::string_view) { return std::string{}; };
    return typeOfTokens(tokenizeExpr(source, cursor, rhsEnd), slots, bindings, noopLocal);
}

script::ScriptCompletion completionFromContract(const script::BindingContract& contract) {
    std::string signature = contract.method + "(";
    std::string insertion = contract.method + "(";
    for (size_t i = 0; i < contract.parameters.size(); ++i) {
        if (i != 0) {
            signature += ", ";
            insertion += ", ";
        }
        signature += contract.parameters[i].name + ": " + contract.parameters[i].type;
        insertion += contract.parameters[i].name + ": ";
    }
    signature += ") -> " + contract.returnType;
    insertion += ')';
    return {contract.method, "function", std::move(signature), std::move(insertion)};
}

class ProjectDirectoryProvider final : public script::IScriptModuleProvider {
public:
    explicit ProjectDirectoryProvider(std::filesystem::path root)
        : root_(std::filesystem::weakly_canonical(std::move(root))) {}

    script::ScriptModuleStatus resolve(const script::ScriptModuleRequest& request, std::string& canonicalUri,
                                       std::string& error) override {
        return script::ScriptModuleResolver::canonicalize(request, canonicalUri, error)
                   ? script::ScriptModuleStatus::Found
                   : script::ScriptModuleStatus::Error;
    }

    script::ScriptModuleStatus load(std::string_view canonicalUri, script::ScriptModuleSource& source,
                                    std::string& error) override {
        constexpr std::string_view prefix = "game:/";
        if (canonicalUri.rfind(prefix, 0) != 0) return script::ScriptModuleStatus::NotHandled;
        const auto path     = std::filesystem::weakly_canonical(root_ / std::string(canonicalUri.substr(prefix.size())));
        const auto relative = path.lexically_relative(root_).generic_string();
        if (relative.empty() || relative.rfind("..", 0) == 0) {
            error = "module escapes the project root: " + std::string(canonicalUri);
            return script::ScriptModuleStatus::Error;
        }
        std::ifstream input(path, std::ios::binary);
        if (!input) return script::ScriptModuleStatus::NotHandled;
        source.canonicalUri = std::string(canonicalUri);
        source.utf8Source.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
        source.debugOrigin = path.string();
        return script::ScriptModuleStatus::Found;
    }

private:
    std::filesystem::path root_;
};

std::vector<std::string> moduleNames(std::string_view source, const std::regex& pattern) {
    const std::string        owned(source);
    std::vector<std::string> result;
    for (std::sregex_iterator it(owned.begin(), owned.end(), pattern), end; it != end; ++it)
        result.push_back((*it)[1].str());
    return result;
}

}  // namespace

struct LanguageServer::Impl {
    explicit Impl(std::string root)
        : root_(std::filesystem::absolute(std::move(root)).lexically_normal().string()),
          runtime_(2048, ssq::Libs::ALL) {
        runtime_.scriptModules().registerProvider(std::make_shared<ProjectDirectoryProvider>(std::filesystem::path(root_)),
                                                  100);
        const std::string       contract = module_list_content ? module_list_content : "";
        const size_t            split    = contract.find("eve_module_contract");
        static const std::regex slotPattern(R"(\bslot\s*=\s*[\"']([^\"']+)[\"'])");
        static const std::regex namePattern(R"(\bname\s*=\s*[\"']([^\"']+)[\"'])");
        static const std::regex slotClassPattern(
            R"(\{\s*slot\s*=\s*[\"']([^\"']+)[\"']\s*,\s*cls\s*=\s*[\"']([^\"']+)[\"'])");
        activeModules_ = moduleNames(contract.substr(0, split), slotPattern);
        knownModules_  = moduleNames(
            split == std::string::npos ? std::string_view{} : std::string_view(contract).substr(split), namePattern);
        const std::string owned(contract.substr(0, split));
        for (std::sregex_iterator it(owned.begin(), owned.end(), slotClassPattern), end; it != end; ++it)
            slotClass_[(*it)[1].str()] = (*it)[2].str();
        indexProject();
    }

    std::string canonicalUri(const std::string& uri) const {
        if (uri.rfind("game:/", 0) == 0) return uri;
        const std::string path = pathFromFileUri(uri);
        if (path.empty()) return uri;
        std::error_code error;
        const auto      relative = std::filesystem::path(path).lexically_normal().lexically_relative(
            std::filesystem::path(root_).lexically_normal());
        if (error || relative.empty() || *relative.begin() == "..") return uri;
        return "game:/" + relative.generic_string();
    }

    std::string identity(std::string_view uri) const {
        const std::string owned(uri);
        const auto        found = canonical_.find(owned);
        return found == canonical_.end() ? canonicalUri(owned) : found->second;
    }

    const std::string* sourceFor(std::string_view uri) const {
        const auto found = documents_.find(std::string(uri));
        return found == documents_.end() ? nullptr : &found->second;
    }

    void update(const std::string& uri, std::string source) {
        if (uri.empty()) return;
        documents_[uri] = std::move(source);
        canonical_[uri] = canonicalUri(uri);
        index_.update(canonical_[uri], uri, documents_[uri]);
        try {
            runtime_.compileSource(documents_[uri], canonical_[uri]);
        } catch (const std::exception&) {
        }
    }

    void close(const std::string& uri) {
        const auto canonical = canonical_.find(uri);
        if (canonical != canonical_.end()) {
            const std::filesystem::path path = pathForCanonical(canonical->second);
            std::ifstream               input(path, std::ios::binary);
            if (input) {
                std::string source{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
                index_.update(canonical->second, fileUriFromPath(path.string()), std::move(source));
            } else {
                index_.remove(canonical->second);
            }
        }
        documents_.erase(uri);
        canonical_.erase(uri);
    }

    std::vector<script::ScriptDiagnostic> diagnostics(std::string_view uri) const {
        const std::string owned(uri);
        const std::string ident = identity(owned);
        auto              list  = runtime_.scriptCompiler().diagnostics(ident);
        const auto        document = documents_.find(owned);
        const bool        isProjectConfig =
            ident == "game:/config.nut" || (owned.size() >= 11 && owned.compare(owned.size() - 11, 11, "/config.nut") == 0);
        if (document != documents_.end() && isProjectConfig) {
            auto configDiagnostics = script::ScriptCompiler::validateProjectConfig(document->second, ident, knownModules_,
                                                                                   activeModules_);
            list.insert(list.end(), configDiagnostics.begin(), configDiagnostics.end());
        }
        return list;
    }

    std::vector<script::ScriptCompletion> complete(std::string_view uri, lsp::Position position) const {
        const std::string* source = sourceFor(uri);
        CursorWord         cursor;
        if (source)
            cursor = wordAt(*source, position.line, position.character);
        const std::string ident = identity(uri);
        std::vector<script::ScriptCompletion> result;
        if (cursor.memberAccess) {
            size_t wordBegin = 0;
            if (source) {
                size_t offset = 0;
                for (size_t current = 0; current < position.line && offset < source->size(); ++current) {
                    const size_t newline = source->find('\n', offset);
                    offset               = newline == std::string_view::npos ? source->size() : newline + 1;
                }
                offset    = std::min(offset + position.character, source->size());
                wordBegin = offset;
                while (wordBegin > 0 && identifierPart((*source)[wordBegin - 1])) --wordBegin;
            }
            size_t dot = wordBegin;
            if (source) {
                while (dot > 0 && std::isspace(static_cast<unsigned char>((*source)[dot - 1])) != 0) --dot;
                if (dot > 0 && (*source)[dot - 1] == '.') --dot;
            }
            const auto localType = [&](std::string_view name) -> std::string {
                if (const auto unit = runtime_.scriptCompiler().metadata(ident)) {
                    for (const script::ScriptSymbolMetadata& symbol : unit->get().symbols) {
                        if (symbol.name != name) continue;
                        const std::string annotated = trimTypeName(symbol.erasedType);
                        if (runtime_.scriptCompiler().bindings().hasScriptClass(annotated)) return annotated;
                    }
                }
                return source ? inferAssignedType(*source, dot, name, slotClass_, runtime_.scriptCompiler().bindings())
                              : std::string{};
            };
            const std::string receiver =
                source ? typeOfTokens(tokenizeExpr(*source, statementStart(*source, dot), dot), slotClass_,
                                      runtime_.scriptCompiler().bindings(), localType)
                       : std::string{};
            const std::string expectedClass = !receiver.empty()
                                                  ? receiver
                                                  : (slotClass_.count(cursor.qualifier) ? slotClass_.at(cursor.qualifier)
                                                                                        : cursor.qualifier);
            if (expectedClass == "eve") {
                std::unordered_map<std::string, bool> seen;
                for (const script::BindingContract& contract : runtime_.scriptCompiler().bindings().snapshot()) {
                    if (contract.scriptClass.empty() || contract.scriptClass.rfind(cursor.word, 0) != 0) continue;
                    if (!seen.emplace(contract.scriptClass, true).second) continue;
                    result.push_back({contract.scriptClass, "class", "eve." + contract.scriptClass, contract.scriptClass});
                }
                return result;
            }
            for (const script::BindingContract& contract : runtime_.scriptCompiler().bindings().snapshot()) {
                if (contract.scriptClass != expectedClass) continue;
                if (contract.method.rfind(cursor.word, 0) != 0) continue;
                result.push_back(completionFromContract(contract));
            }
            return result;
        }

        result = runtime_.scriptCompiler().completions(ident, cursor.word);
        static constexpr const char* keywords[] = {"import", "export", "persist", "match", "async", "await",
                                                   "local",  "function", "class", "const", "foreach", "return",
                                                   "if",     "else",     "while",  "for",   "switch",  "case",
                                                   "break",  "continue", "try",    "catch", "throw",   "null",
                                                   "true",   "false",    "this",   "base",  "typeof",  "instanceof",
                                                   "resume", "yield",    "clone",  "delete"};
        for (const char* keyword : keywords) {
            if (std::string_view(keyword).rfind(cursor.word, 0) != 0) continue;
            result.push_back({keyword, "keyword", "EveScript keyword", keyword});
        }
        for (const std::string& slot : activeModules_) {
            if (slot.rfind(cursor.word, 0) != 0) continue;
            const auto cls = slotClass_.find(slot);
            result.push_back({slot, "slot", cls == slotClass_.end() ? "module slot" : cls->second, slot});
        }
        std::sort(result.begin(), result.end(), [](const script::ScriptCompletion& a, const script::ScriptCompletion& b) {
            if (a.label != b.label) return a.label < b.label;
            return a.kind < b.kind;
        });
        result.erase(std::unique(result.begin(), result.end(),
                                 [](const script::ScriptCompletion& a, const script::ScriptCompletion& b) {
                                     return a.label == b.label && a.kind == b.kind;
                                 }),
                     result.end());
        return result;
    }

    std::optional<script::ScriptHover> hover(std::string_view uri, lsp::Position position) const {
        const std::string* source = sourceFor(uri);
        if (!source) return std::nullopt;
        const CursorWord cursor = wordAt(*source, position.line, position.character);
        if (cursor.word.empty()) return std::nullopt;
        const std::string ident = identity(uri);
        if (auto value = runtime_.scriptCompiler().hover(ident, cursor.word)) return value;
        const auto slot = slotClass_.find(cursor.word);
        if (slot != slotClass_.end()) {
            script::ScriptHover hover;
            hover.symbol   = cursor.word;
            hover.markdown = "`" + cursor.word + "` — `" + slot->second + "` module slot";
            return hover;
        }
        if (const auto location = index_.definition(ident, position)) {
            script::ScriptHover hover;
            hover.symbol   = cursor.word;
            hover.markdown = "`" + cursor.word + "` defined in `" + location->uri + "`";
            return hover;
        }
        return std::nullopt;
    }

    std::optional<LanguageSignatureHelp> signatureHelp(std::string_view uri, lsp::Position position) const {
        const std::string* source = sourceFor(uri);
        if (!source) return std::nullopt;
        size_t offset = 0;
        for (size_t current = 0; current < position.line && offset < source->size(); ++current) {
            const size_t newline = source->find('\n', offset);
            offset               = newline == std::string_view::npos ? source->size() : newline + 1;
        }
        offset                    = std::min(offset + position.character, source->size());
        const std::string callee  = calleeBeforeParen(*source, offset);
        if (callee.empty()) return std::nullopt;
        const script::BindingContract* contract = runtime_.scriptCompiler().bindings().findMethod(callee);
        if (!contract) return std::nullopt;
        LanguageSignatureHelp help;
        help.activeParameter = activeParameterAt(*source, offset);
        help.documentation   = contract->documentationId;
        help.label           = contract->scriptClass + "." + contract->method + "(";
        for (size_t i = 0; i < contract->parameters.size(); ++i) {
            if (i != 0) help.label += ", ";
            help.label += contract->parameters[i].name + ": " + contract->parameters[i].type;
            help.parameters.push_back({contract->parameters[i].name + ": " + contract->parameters[i].type, {}});
        }
        help.label += ") -> " + contract->returnType;
        if (help.activeParameter >= static_cast<int>(help.parameters.size()))
            help.activeParameter = help.parameters.empty() ? 0 : static_cast<int>(help.parameters.size()) - 1;
        return help;
    }

    std::filesystem::path pathForCanonical(std::string_view canonical) const {
        constexpr std::string_view prefix = "game:/";
        return canonical.rfind(prefix, 0) == 0
                   ? std::filesystem::path(root_) / std::string(canonical.substr(prefix.size()))
                   : std::filesystem::path{};
    }

    void indexProject() {
        std::error_code error;
        for (std::filesystem::recursive_directory_iterator it(root_, error), end; it != end && !error;
             it.increment(error)) {
            if (it->is_directory(error)) {
                const std::string name = it->path().filename().string();
                if (name == ".git" || name == "build" || name == "third-party") it.disable_recursion_pending();
                continue;
            }
            if (!it->is_regular_file(error) || it->path().extension() != ".nut") continue;
            std::ifstream input(it->path(), std::ios::binary);
            if (!input) continue;
            std::string       source{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
            const std::string uri      = fileUriFromPath(it->path().string());
            const auto        relative = std::filesystem::relative(it->path(), std::filesystem::path(root_), error);
            if (error || relative.empty() || relative.generic_string().rfind("..", 0) == 0) continue;
            index_.update("game:/" + relative.generic_string(), uri, std::move(source));
        }
    }

    std::string                                  root_;
    eve::Runtime                                 runtime_;
    lsp::WorkspaceIndex                          index_;
    std::unordered_map<std::string, std::string> documents_;
    std::unordered_map<std::string, std::string> canonical_;
    std::vector<std::string>                     knownModules_;
    std::vector<std::string>                     activeModules_;
    std::unordered_map<std::string, std::string> slotClass_;
    bool                                         shutdown_ = false;
};

LanguageServer::LanguageServer(std::string projectRoot) : impl_(std::make_unique<Impl>(std::move(projectRoot))) {}
LanguageServer::~LanguageServer() = default;

const std::string& LanguageServer::projectRoot() const noexcept { return impl_->root_; }

std::string LanguageServer::fileUriFromPath(std::string_view path) {
    std::string generic = std::filesystem::absolute(std::filesystem::path(path)).lexically_normal().generic_string();
    std::string encoded;
    encoded.reserve(generic.size());
    for (const char value : generic) {
        if (value == ' ')
            encoded += "%20";
        else if (value == '#')
            encoded += "%23";
        else if (value == '%')
            encoded += "%25";
        else
            encoded += value;
    }
    if (!encoded.empty() && encoded.front() != '/') encoded.insert(encoded.begin(), '/');
    return "file://" + encoded;
}

void LanguageServer::openDocument(std::string uri, std::string text) { impl_->update(uri, std::move(text)); }
void LanguageServer::changeDocument(std::string uri, std::string text) { impl_->update(uri, std::move(text)); }
void LanguageServer::changeDocument(std::string uri, lsp::Range range, std::string text) {
    auto found = impl_->documents_.find(uri);
    if (found == impl_->documents_.end()) {
        impl_->update(uri, std::move(text));
        return;
    }
    lsp::applyIncremental(found->second, &range, text);
    impl_->update(uri, found->second);
}
void LanguageServer::closeDocument(std::string uri) { impl_->close(uri); }

std::vector<script::ScriptDiagnostic> LanguageServer::diagnosticsFor(std::string_view uri) const {
    return impl_->diagnostics(uri);
}

std::vector<script::ScriptCompletion> LanguageServer::complete(std::string_view uri, lsp::Position position) const {
    return impl_->complete(uri, position);
}

std::optional<script::ScriptHover> LanguageServer::hover(std::string_view uri, lsp::Position position) const {
    return impl_->hover(uri, position);
}

std::optional<lsp::Location> LanguageServer::definition(std::string_view uri, lsp::Position position) const {
    return impl_->index_.definition(impl_->identity(uri), position);
}

std::vector<lsp::Location> LanguageServer::references(std::string_view uri, lsp::Position position,
                                                      bool includeDeclaration) const {
    return impl_->index_.references(impl_->identity(uri), position, includeDeclaration);
}

std::optional<std::vector<lsp::TextEdit>> LanguageServer::rename(std::string_view uri, lsp::Position position,
                                                                 std::string_view newName) const {
    return impl_->index_.rename(impl_->identity(uri), position, newName);
}

std::vector<script::ScriptSymbolMetadata> LanguageServer::documentSymbols(std::string_view uri) const {
    const auto metadata = impl_->runtime_.scriptCompiler().metadata(impl_->identity(uri));
    return metadata ? metadata->get().symbols : std::vector<script::ScriptSymbolMetadata>{};
}

std::optional<LanguageSignatureHelp> LanguageServer::signatureHelp(std::string_view uri, lsp::Position position) const {
    return impl_->signatureHelp(uri, position);
}

std::vector<lsp::TextEdit> LanguageServer::formatDocument(std::string_view uri, lsp::FormatOptions options,
                                                          const lsp::Range* range) const {
    const std::string* source = impl_->sourceFor(uri);
    if (!source) return {};
    const std::string formatted = lsp::formatEveScript(*source, options);
    lsp::TextEdit edit;
    edit.location.uri = std::string(uri);
    if (range != nullptr && lsp::lastLineIndex(formatted) == lsp::lastLineIndex(*source)) {
        size_t startLine = range->start.line;
        size_t endLine   = range->end.line;
        if (range->end.character == 0 && endLine > startLine) --endLine;
        const std::string originalSpan  = lsp::sliceLines(*source, startLine, endLine);
        const std::string formattedSpan = lsp::sliceLines(formatted, startLine, endLine);
        if (formattedSpan == originalSpan) return {};
        edit.location.range = lsp::coveringLines(*source, startLine, endLine);
        edit.newText        = formattedSpan;
        return {std::move(edit)};
    }
    if (formatted == *source) return {};
    edit.location.range = lsp::documentRange(*source);
    edit.newText        = formatted;
    return {std::move(edit)};
}

std::vector<lsp::FoldingRange> LanguageServer::foldingRanges(std::string_view uri) const {
    const std::string* source = impl_->sourceFor(uri);
    return source ? lsp::foldingRanges(*source) : std::vector<lsp::FoldingRange>{};
}

std::vector<lsp::SemanticToken> LanguageServer::semanticTokens(std::string_view uri) const {
    std::vector<lsp::SemanticToken> tokens = impl_->index_.semanticTokens(impl_->identity(uri));
    const auto&                     bindings = impl_->runtime_.scriptCompiler().bindings();
    const auto                      metadata = impl_->runtime_.scriptCompiler().metadata(impl_->identity(uri));
    std::unordered_map<std::string, uint32_t> extra;
    if (metadata) {
        for (const script::ScriptSymbolMetadata& symbol : metadata->get().symbols) {
            if (symbol.kind == "class") extra[symbol.name] = lsp::SemanticTypes::Class;
            else if (symbol.kind == "function" || symbol.kind == "async")
                extra[symbol.name] = lsp::SemanticTypes::Function;
        }
    }
    for (lsp::SemanticToken& token : tokens) {
        if (token.type == lsp::SemanticTypes::Keyword) continue;
        if (token.type == lsp::SemanticTypes::Method || token.type == lsp::SemanticTypes::Property) {
            if (bindings.hasScriptClass(token.name)) token.type = lsp::SemanticTypes::Class;
            continue;
        }
        if (impl_->slotClass_.count(token.name) != 0) {
            token.type = lsp::SemanticTypes::Namespace;
            token.modifiers |= lsp::SemanticMods::DefaultLibrary;
            continue;
        }
        if (bindings.hasScriptClass(token.name)) {
            token.type = lsp::SemanticTypes::Class;
            continue;
        }
        const auto found = extra.find(token.name);
        if (found != extra.end() && token.type == lsp::SemanticTypes::Variable) token.type = found->second;
    }
    return tokens;
}

LspDispatch LanguageServer::handleMessage(std::string_view jsonBody, std::ostream& output) {
    Poco::JSON::Parser      parser;
    Poco::JSON::Object::Ptr request;
    try {
        request = parser.parse(std::string(jsonBody)).extract<Poco::JSON::Object::Ptr>();
    } catch (const std::exception& error) {
        writeFrame(output, rpcError(Poco::Dynamic::Var(), -32700, error.what()));
        return LspDispatch::Continue;
    }
    if (!request) return LspDispatch::Continue;
    const std::string method = request->optValue<std::string>("method", "");
    const auto        params = request->getObject("params");
    const bool        hasId  = request->has("id");
    const auto        id     = hasId ? request->get("id") : Poco::Dynamic::Var();
    const auto        documentUri = [&]() -> std::string {
        if (!params) return {};
        auto document = params->getObject("textDocument");
        return document ? document->optValue<std::string>("uri", "") : std::string{};
    };
    const auto requestPosition = [&]() -> lsp::Position {
        auto position = params ? params->getObject("position") : nullptr;
        return position ? lsp::Position{static_cast<size_t>(clampNonNegative(position->optValue<int>("line", 0))),
                                        static_cast<size_t>(clampNonNegative(position->optValue<int>("character", 0)))}
                        : lsp::Position{};
    };

    const auto publishDiagnostics = [&](const std::string& uri) {
        Poco::JSON::Array::Ptr items = new Poco::JSON::Array();
        for (const script::ScriptDiagnostic& diagnostic : diagnosticsFor(uri)) {
            const int line = clampNonNegative(static_cast<int>(diagnostic.position.line) - 1);
            const int col  = clampNonNegative(static_cast<int>(diagnostic.position.column) - 1);
            lsp::Range range{{static_cast<size_t>(line), static_cast<size_t>(col)},
                             {static_cast<size_t>(line), static_cast<size_t>(col + 1)}};
            Poco::JSON::Object::Ptr item = new Poco::JSON::Object();
            item->set("range", rangeObject(range));
            item->set("severity", diagnostic.severity == script::ScriptDiagnosticSeverity::Error ? 1 : 2);
            item->set("code", diagnostic.code);
            item->set("source", "evescript");
            item->set("message", diagnostic.message);
            items->add(item);
        }
        Poco::JSON::Object::Ptr diagParams = new Poco::JSON::Object();
        diagParams->set("uri", uri);
        diagParams->set("diagnostics", items);
        Poco::JSON::Object::Ptr notification = new Poco::JSON::Object();
        notification->set("jsonrpc", "2.0");
        notification->set("method", "textDocument/publishDiagnostics");
        notification->set("params", diagParams);
        writeFrame(output, notification);
    };

    if (method == "initialize") {
        Poco::JSON::Object::Ptr sync = new Poco::JSON::Object();
        sync->set("openClose", true);
        sync->set("change", 2);
        Poco::JSON::Object::Ptr completion = new Poco::JSON::Object();
        completion->set("resolveProvider", false);
        Poco::JSON::Array::Ptr triggers = new Poco::JSON::Array();
        triggers->add(".");
        triggers->add(":");
        completion->set("triggerCharacters", triggers);
        Poco::JSON::Object::Ptr signature = new Poco::JSON::Object();
        Poco::JSON::Array::Ptr  sigTriggers = new Poco::JSON::Array();
        sigTriggers->add("(");
        sigTriggers->add(",");
        signature->set("triggerCharacters", sigTriggers);
        Poco::JSON::Object::Ptr capabilities = new Poco::JSON::Object();
        capabilities->set("textDocumentSync", sync);
        capabilities->set("completionProvider", completion);
        capabilities->set("hoverProvider", true);
        capabilities->set("definitionProvider", true);
        capabilities->set("referencesProvider", true);
        capabilities->set("renameProvider", true);
        capabilities->set("documentSymbolProvider", true);
        capabilities->set("signatureHelpProvider", signature);
        capabilities->set("documentFormattingProvider", true);
        capabilities->set("documentRangeFormattingProvider", true);
        capabilities->set("foldingRangeProvider", true);
        Poco::JSON::Object::Ptr semantic = new Poco::JSON::Object();
        semantic->set("legend", semanticTokensLegend());
        semantic->set("full", true);
        semantic->set("range", false);
        capabilities->set("semanticTokensProvider", semantic);
        Poco::JSON::Object::Ptr diagnosticProvider = new Poco::JSON::Object();
        diagnosticProvider->set("interFileDependencies", true);
        diagnosticProvider->set("workspaceDiagnostics", false);
        capabilities->set("diagnosticProvider", diagnosticProvider);
        Poco::JSON::Object::Ptr result = new Poco::JSON::Object();
        result->set("capabilities", capabilities);
        Poco::JSON::Object::Ptr serverInfo = new Poco::JSON::Object();
        serverInfo->set("name", "evescript");
        serverInfo->set("version", "1");
        result->set("serverInfo", serverInfo);
        writeFrame(output, rpcResponse(id, Poco::Dynamic::Var(result)));
    } else if (method == "initialized" || method == "textDocument/didSave" || method.rfind("$/", 0) == 0) {
        return LspDispatch::Continue;
    } else if (method == "shutdown") {
        impl_->shutdown_ = true;
        writeFrame(output, rpcResponse(id, Poco::Dynamic::Var()));
    } else if (method == "exit") {
        return LspDispatch::Exit;
    } else if (method == "textDocument/didOpen") {
        auto document = params ? params->getObject("textDocument") : nullptr;
        if (document) {
            const std::string uri = document->optValue<std::string>("uri", "");
            openDocument(uri, document->optValue<std::string>("text", ""));
            publishDiagnostics(uri);
        }
    } else if (method == "textDocument/didChange") {
        const std::string uri     = documentUri();
        auto              changes = params ? params->getArray("contentChanges") : nullptr;
        if (changes) {
            for (unsigned int index = 0; index < changes->size(); ++index) {
                auto change = changes->getObject(index);
                if (!change) continue;
                const std::string text = change->optValue<std::string>("text", "");
                if (change->has("range"))
                    changeDocument(uri, parseRange(change->getObject("range")), text);
                else
                    changeDocument(uri, text);
            }
            if (changes->size() != 0) publishDiagnostics(uri);
        }
    } else if (method == "textDocument/didClose") {
        const std::string uri = documentUri();
        closeDocument(uri);
        Poco::JSON::Object::Ptr diagParams = new Poco::JSON::Object();
        diagParams->set("uri", uri);
        diagParams->set("diagnostics", Poco::JSON::Array::Ptr(new Poco::JSON::Array()));
        Poco::JSON::Object::Ptr notification = new Poco::JSON::Object();
        notification->set("jsonrpc", "2.0");
        notification->set("method", "textDocument/publishDiagnostics");
        notification->set("params", diagParams);
        writeFrame(output, notification);
    } else if (method == "textDocument/completion") {
        Poco::JSON::Array::Ptr items = new Poco::JSON::Array();
        for (const script::ScriptCompletion& completion : complete(documentUri(), requestPosition())) {
            Poco::JSON::Object::Ptr item = new Poco::JSON::Object();
            item->set("label", completion.label);
            item->set("detail", completion.detail);
            item->set("insertText", completion.insertText);
            item->set("kind", completionKind(completion.kind));
            items->add(item);
        }
        writeFrame(output, rpcResponse(id, Poco::Dynamic::Var(items)));
    } else if (method == "textDocument/hover") {
        const auto value = hover(documentUri(), requestPosition());
        if (!value) {
            writeFrame(output, rpcResponse(id, Poco::Dynamic::Var()));
        } else {
            Poco::JSON::Object::Ptr contents = new Poco::JSON::Object();
            contents->set("kind", "markdown");
            contents->set("value", value->markdown);
            Poco::JSON::Object::Ptr result = new Poco::JSON::Object();
            result->set("contents", contents);
            writeFrame(output, rpcResponse(id, Poco::Dynamic::Var(result)));
        }
    } else if (method == "textDocument/definition") {
        const auto value = definition(documentUri(), requestPosition());
        writeFrame(output, rpcResponse(id, value ? Poco::Dynamic::Var(locationObject(*value)) : Poco::Dynamic::Var()));
    } else if (method == "textDocument/references") {
        bool includeDeclaration = false;
        if (auto context = params ? params->getObject("context") : nullptr)
            includeDeclaration = context->optValue<bool>("includeDeclaration", false);
        Poco::JSON::Array::Ptr items = new Poco::JSON::Array();
        for (const lsp::Location& location : references(documentUri(), requestPosition(), includeDeclaration))
            items->add(locationObject(location));
        writeFrame(output, rpcResponse(id, Poco::Dynamic::Var(items)));
    } else if (method == "textDocument/rename") {
        const std::string newName = params ? params->optValue<std::string>("newName", "") : std::string{};
        const auto        edits   = rename(documentUri(), requestPosition(), newName);
        if (!edits) {
            writeFrame(output, rpcResponse(id, Poco::Dynamic::Var()));
        } else {
            Poco::JSON::Object::Ptr                                 changes = new Poco::JSON::Object();
            std::unordered_map<std::string, Poco::JSON::Array::Ptr> byUri;
            for (const lsp::TextEdit& edit : *edits) {
                auto& list = byUri[edit.location.uri];
                if (!list) list = new Poco::JSON::Array();
                Poco::JSON::Object::Ptr item = new Poco::JSON::Object();
                item->set("range", rangeObject(edit.location.range));
                item->set("newText", edit.newText);
                list->add(item);
            }
            for (const auto& [uri, list] : byUri) changes->set(uri, list);
            Poco::JSON::Object::Ptr result = new Poco::JSON::Object();
            result->set("changes", changes);
            writeFrame(output, rpcResponse(id, Poco::Dynamic::Var(result)));
        }
    } else if (method == "textDocument/documentSymbol") {
        Poco::JSON::Array::Ptr items = new Poco::JSON::Array();
        for (const script::ScriptSymbolMetadata& symbol : documentSymbols(documentUri())) {
            const int line = clampNonNegative(static_cast<int>(symbol.position.line) - 1);
            const int col  = clampNonNegative(static_cast<int>(symbol.position.column) - 1);
            lsp::Range range{{static_cast<size_t>(line), static_cast<size_t>(col)},
                             {static_cast<size_t>(line), static_cast<size_t>(col + symbol.name.size())}};
            Poco::JSON::Object::Ptr item = new Poco::JSON::Object();
            item->set("name", symbol.name);
            item->set("detail", symbol.erasedType);
            item->set("kind", symbolKind(symbol.kind));
            item->set("range", rangeObject(range));
            item->set("selectionRange", rangeObject(range));
            items->add(item);
        }
        writeFrame(output, rpcResponse(id, Poco::Dynamic::Var(items)));
    } else if (method == "textDocument/signatureHelp") {
        const auto value = signatureHelp(documentUri(), requestPosition());
        if (!value) {
            writeFrame(output, rpcResponse(id, Poco::Dynamic::Var()));
        } else {
            Poco::JSON::Array::Ptr parameters = new Poco::JSON::Array();
            for (const LanguageSignatureParameter& parameter : value->parameters) {
                Poco::JSON::Object::Ptr item = new Poco::JSON::Object();
                item->set("label", parameter.label);
                parameters->add(item);
            }
            Poco::JSON::Object::Ptr signature = new Poco::JSON::Object();
            signature->set("label", value->label);
            signature->set("documentation", value->documentation);
            signature->set("parameters", parameters);
            Poco::JSON::Array::Ptr signatures = new Poco::JSON::Array();
            signatures->add(signature);
            Poco::JSON::Object::Ptr result = new Poco::JSON::Object();
            result->set("signatures", signatures);
            result->set("activeSignature", 0);
            result->set("activeParameter", value->activeParameter);
            writeFrame(output, rpcResponse(id, Poco::Dynamic::Var(result)));
        }
    } else if (method == "textDocument/formatting") {
        const auto options = parseFormatOptions(params ? params->getObject("options") : nullptr);
        writeFrame(output, rpcResponse(id, Poco::Dynamic::Var(textEditsArray(formatDocument(documentUri(), options)))));
    } else if (method == "textDocument/rangeFormatting") {
        const auto options = parseFormatOptions(params ? params->getObject("options") : nullptr);
        lsp::Range range   = parseRange(params ? params->getObject("range") : nullptr);
        writeFrame(output,
                   rpcResponse(id, Poco::Dynamic::Var(textEditsArray(formatDocument(documentUri(), options, &range)))));
    } else if (method == "textDocument/foldingRange") {
        Poco::JSON::Array::Ptr items = new Poco::JSON::Array();
        for (const lsp::FoldingRange& fold : foldingRanges(documentUri())) {
            Poco::JSON::Object::Ptr item = new Poco::JSON::Object();
            item->set("startLine", static_cast<int>(fold.startLine));
            item->set("endLine", static_cast<int>(fold.endLine));
            if (!fold.kind.empty()) item->set("kind", fold.kind);
            items->add(item);
        }
        writeFrame(output, rpcResponse(id, Poco::Dynamic::Var(items)));
    } else if (method == "textDocument/semanticTokens/full") {
        Poco::JSON::Object::Ptr result = new Poco::JSON::Object();
        result->set("data", semanticTokensData(semanticTokens(documentUri())));
        writeFrame(output, rpcResponse(id, Poco::Dynamic::Var(result)));
    } else if (method == "textDocument/diagnostic") {
        Poco::JSON::Array::Ptr items = new Poco::JSON::Array();
        const std::string      uri   = documentUri();
        for (const script::ScriptDiagnostic& diagnostic : diagnosticsFor(uri)) {
            const int line = clampNonNegative(static_cast<int>(diagnostic.position.line) - 1);
            const int col  = clampNonNegative(static_cast<int>(diagnostic.position.column) - 1);
            lsp::Range range{{static_cast<size_t>(line), static_cast<size_t>(col)},
                             {static_cast<size_t>(line), static_cast<size_t>(col + 1)}};
            Poco::JSON::Object::Ptr item = new Poco::JSON::Object();
            item->set("range", rangeObject(range));
            item->set("severity", diagnostic.severity == script::ScriptDiagnosticSeverity::Error ? 1 : 2);
            item->set("code", diagnostic.code);
            item->set("source", "evescript");
            item->set("message", diagnostic.message);
            items->add(item);
        }
        Poco::JSON::Object::Ptr result = new Poco::JSON::Object();
        result->set("kind", "full");
        result->set("items", items);
        writeFrame(output, rpcResponse(id, Poco::Dynamic::Var(result)));
    } else if (hasId) {
        writeFrame(output, rpcError(id, -32601, "unsupported LSP method: " + method));
    }
    return LspDispatch::Continue;
}

int LanguageServer::runStdio(std::istream& input, std::ostream& output) {
    std::string body;
    while (readFrame(input, body)) {
        if (handleMessage(body, output) == LspDispatch::Exit) return impl_->shutdown_ ? 0 : 1;
    }
    return impl_->shutdown_ ? 0 : 1;
}

}  // namespace eve::dev
