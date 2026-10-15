#include "common/ScriptStructure.hpp"
#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <stdexcept>

namespace eve::script {
namespace {
struct Token {
    std::string kind, text;
    unsigned    line = 1, column = 1;
    size_t      begin = 0, end = 0;
};
struct Signature {
    std::string name;
    size_t      begin, end;
};
struct Parser {
    std::string_view                   source;
    std::vector<Token>                 tokens;
    std::map<size_t, size_t>           pairs;
    ScriptStructure                    out;
    std::map<size_t, ScriptCallSyntax> calls;
    std::vector<Signature>             signatures;

    void lex() {
        unsigned line = 1, column = 1;
        size_t   i       = 0;
        auto     advance = [&]() {
            if (source[i++] == '\n') {
                ++line;
                column = 1;
            } else
                ++column;
        };
        while (i < source.size()) {
            const auto c = static_cast<unsigned char>(source[i]);
            if (std::isspace(c)) {
                advance();
                continue;
            }
            if (source.substr(i, 2) == "//") {
                while (i < source.size() && source[i] != '\n') advance();
                continue;
            }
            if (source.substr(i, 2) == "/*") {
                advance();
                advance();
                while (i < source.size() && source.substr(i, 2) != "*/") advance();
                if (i == source.size()) throw std::runtime_error("Unterminated block comment");
                advance();
                advance();
                continue;
            }
            Token token{"punctuation", "", line, column, i, i};
            bool  verbatim = source[i] == '@' && i + 1 < source.size() && source[i + 1] == '"';
            if (source[i] == '"' || source[i] == '\'' || verbatim) {
                token.kind = "string";
                if (verbatim) advance();
                char quote = source[i];
                advance();
                bool closed = false;
                while (i < source.size()) {
                    char value = source[i];
                    advance();
                    if (value == quote) {
                        if (verbatim && i < source.size() && source[i] == quote) {
                            advance();
                            token.text += quote;
                            continue;
                        }
                        closed = true;
                        break;
                    }
                    if (!verbatim && value == '\\') {
                        if (i == source.size()) break;
                        value = source[i];
                        advance();
                        if (value == 'n')
                            value = '\n';
                        else if (value == 'r')
                            value = '\r';
                        else if (value == 't')
                            value = '\t';
                        else if (value != '\\' && value != '"' && value != '\'')
                            token.kind = "dynamic";
                    }
                    token.text += value;
                }
                if (!closed) throw std::runtime_error("Unterminated string");
            } else if (std::isalpha(c) || c == '_') {
                token.kind = "identifier";
                while (i < source.size() && (std::isalnum(static_cast<unsigned char>(source[i])) || source[i] == '_')) {
                    token.text += source[i];
                    advance();
                }
            } else if (std::isdigit(c)) {
                token.kind = "number";
                while (i < source.size() && (std::isalnum(static_cast<unsigned char>(source[i])) || source[i] == '.')) {
                    token.text += source[i];
                    advance();
                }
            } else {
                token.text += source[i];
                advance();
                if (i < source.size() && (token.text + source[i] == "<-" || token.text + source[i] == "==" ||
                                          token.text + source[i] == "!=" || token.text + source[i] == "::" ||
                                          token.text + source[i] == "<=" || token.text + source[i] == ">=")) {
                    token.text += source[i];
                    advance();
                }
            }
            token.end = i;
            tokens.push_back(std::move(token));
            if (tokens.size() > 65536) throw std::runtime_error("Script token budget exceeded");
        }
        std::vector<size_t> stack;
        for (size_t t = 0; t < tokens.size(); ++t) {
            auto text = tokens[t].text;
            if (tokens[t].kind != "punctuation") continue;
            if (text == "(" || text == "[" || text == "{") {
                stack.push_back(t);
                if (stack.size() > 64) throw std::runtime_error("Script nesting budget exceeded");
            } else if (text == ")" || text == "]" || text == "}") {
                if (stack.empty()) throw std::runtime_error("Unbalanced script delimiter");
                auto open = tokens[stack.back()].text;
                if ((open == "(" && text != ")") || (open == "[" && text != "]") || (open == "{" && text != "}"))
                    throw std::runtime_error("Mismatched script delimiter");
                pairs[stack.back()] = t;
                stack.pop_back();
            }
        }
        if (!stack.empty()) throw std::runtime_error("Unbalanced script delimiter");
    }
    ScriptExpression atom(size_t t, std::string kind = {}) const {
        const auto& token = tokens[t];
        return {kind.empty() ? token.kind : kind, token.text, {}, token.line, token.column, token.begin, token.end};
    }
    std::string scope(size_t offset) const {
        const ScriptFunctionSyntax* owner = nullptr;
        for (const auto& function : out.functions)
            if (function.begin <= offset && offset < function.end && (!owner || function.begin >= owner->begin))
                owner = &function;
        if (owner) return owner->name;
        for (const auto& signature : signatures)
            if (signature.begin <= offset && offset < signature.end) return signature.name;
        return classScope(offset);
    }
    std::string classScope(size_t offset) const {
        const ScriptClassSyntax* owner = nullptr;
        for (const auto& value : out.classes)
            if (value.begin <= offset && offset < value.end && (!owner || value.begin >= owner->begin)) owner = &value;
        return owner ? owner->name + "." : "";
    }
    ScriptExpression expression(size_t begin, size_t end, unsigned depth = 0, bool complete = true) {
        if (begin >= end || depth >= 64) return {"dynamic", "", {}, 1, 1, 0, 0};
        size_t p     = begin;
        auto   value = atom(p++);
        if (value.value == "(" && pairs.contains(begin) && pairs.at(begin) < end) {
            value = expression(begin + 1, pairs.at(begin), depth + 1);
            p     = pairs.at(begin) + 1;
        } else if (value.kind != "identifier" && value.kind != "string" && value.kind != "number")
            value.kind = "dynamic";
        while (p < end) {
            if (tokens[p].text == "." && p + 1 < end && tokens[p + 1].kind == "identifier") {
                auto member  = atom(p + 1, "member");
                member.begin = value.begin;
                member.operands.push_back(std::move(value));
                value = std::move(member);
                p += 2;
            } else if (tokens[p].text == "(" && pairs.contains(p) && pairs.at(p) < end) {
                const auto       close = pairs.at(p);
                ScriptExpression call{"call", "", {}, value.line, value.column, value.begin, tokens[close].end};
                call.operands.push_back(std::move(value));
                size_t argument = p + 1;
                for (size_t k = argument; k <= close; ++k) {
                    if (k < close && pairs.contains(k)) {
                        k = pairs.at(k);
                        continue;
                    }
                    if (k == close || tokens[k].text == ",") {
                        if (argument < k) call.operands.push_back(expression(argument, k, depth + 1));
                        argument = k + 1;
                    }
                }
                ScriptCallSyntax record{scope(call.begin), "", call, {"dynamic", "", {}, 1, 1, 0, 0}};
                if (close + 2 < tokens.size() && (tokens[close + 1].text == "==" || tokens[close + 1].text == "!=")) {
                    record.comparison    = tokens[close + 1].text;
                    record.comparedValue = atom(close + 2);
                }
                calls[call.begin] = std::move(record);
                if (calls.size() > 4096) throw std::runtime_error("Script call budget exceeded");
                value = std::move(call);
                p     = close + 1;
            } else
                break;
        }
        if (complete && p != end) return atom(begin, "dynamic");
        return value;
    }
    void functions() {
        for (size_t i = 0; i + 1 < tokens.size(); ++i) {
            if (tokens[i].kind != "identifier" || tokens[i].text != "class" || tokens[i + 1].kind != "identifier")
                continue;
            size_t body = i + 2;
            while (body < tokens.size() && tokens[body].text != "{" && tokens[body].text != ";") ++body;
            if (body < tokens.size() && tokens[body].text == "{" && pairs.contains(body))
                out.classes.push_back(
                    {tokens[i + 1].text, tokens[i].line, tokens[body].begin, tokens[pairs.at(body)].end});
            if (out.classes.size() > 512) throw std::runtime_error("Script class budget exceeded");
        }
        for (size_t i = 0; i < tokens.size(); ++i) {
            if (tokens[i].kind != "identifier" || tokens[i].text != "function") continue;
            size_t      p = i + 1;
            std::string name;
            if (p < tokens.size() && tokens[p].kind == "identifier")
                name = tokens[p++].text;
            else if (i >= 2 && (tokens[i - 1].text == "<-" || tokens[i - 1].text == "=") &&
                     tokens[i - 2].kind == "identifier")
                name = tokens[i - 2].text;
            if (name.empty() || p >= tokens.size() || tokens[p].text != "(" || !pairs.contains(p)) continue;
            size_t close = pairs.at(p), body = close + 1;
            while (body < tokens.size() && tokens[body].text != "{" && tokens[body].text != ";") ++body;
            if (body == tokens.size() || tokens[body].text != "{" || !pairs.contains(body)) continue;
            ScriptFunctionSyntax fn{
                classScope(tokens[i].begin) + name, {}, tokens[i].line, tokens[body].begin, tokens[pairs.at(body)].end};
            for (size_t k = p + 1; k < close;) {
                if (tokens[k].kind == "identifier") {
                    ScriptParameterSyntax parameter{tokens[k].text, ""};
                    if (k + 2 < close && tokens[k + 1].text == ":") parameter.type = tokens[k + 2].text;
                    fn.parameters.push_back(std::move(parameter));
                }
                while (k < close && tokens[k].text != ",") {
                    if (pairs.contains(k)) k = pairs.at(k);
                    ++k;
                }
                ++k;
            }
            signatures.push_back({fn.name, tokens[p].begin, tokens[body].begin});
            out.functions.push_back(std::move(fn));
            if (out.functions.size() > 512) throw std::runtime_error("Script function budget exceeded");
        }
    }
    void analyze() {
        lex();
        functions();
        const std::set<std::string> keywords{"if",       "while", "for",    "foreach", "switch", "catch",
                                             "function", "class", "typeof", "return",  "throw"};
        for (size_t i = 0; i < tokens.size(); ++i) {
            if (tokens[i].kind != "identifier" || keywords.contains(tokens[i].text)) continue;
            if (i && (tokens[i - 1].text == "." || tokens[i - 1].text == "function" || tokens[i - 1].text == ":"))
                continue;
            expression(i, tokens.size(), 0, false);
            if (std::any_of(signatures.begin(), signatures.end(), [&](const Signature& signature) {
                    return signature.begin <= tokens[i].begin && tokens[i].begin < signature.end;
                }))
                continue;
            size_t      assign = i + 1;
            std::string type;
            if (assign + 1 < tokens.size() && tokens[assign].text == ":") {
                type = tokens[assign + 1].text;
                assign += 2;
            }
            if (assign >= tokens.size() || (tokens[assign].text != "=" && tokens[assign].text != "<-")) continue;
            size_t end = assign + 1;
            while (end < tokens.size() && tokens[end].text != ";" && tokens[end].text != "}" &&
                   tokens[end].line == tokens[assign + 1].line) {
                if (pairs.contains(end)) end = pairs.at(end);
                ++end;
            }
            auto value = expression(assign + 1, end);
            out.bindings.push_back({tokens[i].text, scope(tokens[i].begin), type,
                                    !classScope(tokens[i].begin).empty() ||
                                        (i && (tokens[i - 1].text == "local" || tokens[i - 1].text == "const")),
                                    std::move(value)});
            if (out.bindings.size() > 2048) throw std::runtime_error("Script binding budget exceeded");
        }
        for (auto& [offset, call] : calls) {
            (void)offset;
            out.calls.push_back(std::move(call));
        }
    }
};
}  // namespace
ScriptStructure analyzeScriptStructure(std::string_view source) {
    Parser parser{source, {}, {}, {}, {}, {}};
    try {
        parser.analyze();
    } catch (const std::exception& error) {
        parser.out = {};
        parser.out.diagnostics.push_back(error.what());
    }
    return std::move(parser.out);
}
}  // namespace eve::script
