#include "dialogue/DnutParser.h"

#include "dialogue/DialogueSequence.h"
#include "dnut_interpreter/DnutBlockScanner.h"
#include "dnut_interpreter/DnutCompiler.h"
#include "dnut_interpreter/DnutLexer.h"

#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace eve::dialogue {
namespace {

class PoolParseError final : public std::runtime_error {
public:
    PoolParseError(const eve::dnut::DnutToken& token, std::string message)
        : std::runtime_error(std::move(message)), line(token.line), column(token.column) {}

    int line;
    int column;
};

std::string floatToString(double value) {
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%g", value);
    return buffer;
}

std::string scalarToString(const DataValue& value) {
    if (value.isString()) return value.asString();
    if (value.isInt64()) return std::to_string(value.asInt());
    if (value.isDouble()) return floatToString(value.asDouble());
    if (value.isBool()) return value.asBool() ? "true" : "false";
    return {};
}

DataValue numberValue(const std::string& raw) {
    if (raw.find_first_of(".eE") != std::string::npos)
        return DataValue::number(std::strtod(raw.c_str(), nullptr));
    return DataValue::integer(std::strtoll(raw.c_str(), nullptr, 10));
}

class PoolBlockParser {
public:
    PoolBlockParser(const std::vector<eve::dnut::DnutToken>& tokens, const eve::dnut::DnutBlock& block)
        : tokens_(tokens), end_(block.endToken), index_(block.beginToken) {}

    std::pair<std::string, DataValue> parse() {
        expectKeyword("pool");
        const std::string poolId = expectIdentifier("pool name");
        long long         noRepeat = -1;
        while (isIdentifier("noRepeat")) {
            advance();
            expectPunct("=");
            if (cur().kind != eve::dnut::DnutTokenKind::Number) fail("noRepeat must be a number");
            noRepeat = std::strtoll(advance().text.c_str(), nullptr, 10);
        }
        expectPunct("{");

        std::vector<DataValue> lines;
        int                    lineIndex = 1;
        while (!isPunct("}")) {
            if (atEnd()) fail("unterminated pool block");
            if (isIdentifier("pool")) fail("pool blocks cannot be nested");
            if (isIdentifier("when")) {
                advance();
                DataValue condition = parseOr();
                expectPunct("{");
                while (!isPunct("}")) {
                    if (atEnd()) fail("unterminated when block");
                    lines.push_back(parseLine(poolId, lineIndex++, &condition));
                }
                advance();
            } else {
                lines.push_back(parseLine(poolId, lineIndex++, nullptr));
            }
        }
        advance();
        DataValue::Object fields;
        fields.emplace("lines", DataValue::array(std::move(lines)));
        if (noRepeat >= 0) fields.emplace("noRepeat", DataValue::integer(noRepeat));
        return {poolId, DataValue::object(std::move(fields))};
    }

private:
    const eve::dnut::DnutToken& cur() const {
        const std::size_t position = index_ < tokens_.size() ? index_ : tokens_.size() - 1;
        return tokens_[position];
    }
    bool atEnd() const {
        return index_ >= end_ || cur().kind == eve::dnut::DnutTokenKind::EndOfFile;
    }
    eve::dnut::DnutToken advance() {
        const auto token = cur();
        if (!atEnd()) ++index_;
        return token;
    }
    bool isPunct(const char* text) const {
        return cur().kind == eve::dnut::DnutTokenKind::Punctuator && cur().text == text;
    }
    bool isIdentifier(const char* text) const {
        return cur().kind == eve::dnut::DnutTokenKind::Identifier && cur().text == text;
    }
    [[noreturn]] void fail(std::string message) const {
        throw PoolParseError(cur(), std::move(message));
    }
    void expectKeyword(const char* text) {
        if (!isIdentifier(text)) fail("expected '" + std::string(text) + "'");
        advance();
    }
    void expectPunct(const char* text) {
        if (!isPunct(text)) fail("expected '" + std::string(text) + "'");
        advance();
    }
    std::string expectIdentifier(const std::string& description) {
        if (cur().kind != eve::dnut::DnutTokenKind::Identifier) fail("expected " + description);
        return advance().text;
    }

    DataValue parseLiteral() {
        if (isPunct("-")) {
            advance();
            if (cur().kind != eve::dnut::DnutTokenKind::Number) fail("'-' must be followed by a number");
            DataValue value = numberValue(advance().text);
            return value.isInt64() ? DataValue::integer(-value.asInt())
                                   : DataValue::number(-value.asDouble());
        }
        if (cur().kind == eve::dnut::DnutTokenKind::String)
            return DataValue::string(advance().text);
        if (cur().kind == eve::dnut::DnutTokenKind::Number)
            return numberValue(advance().text);
        if (isIdentifier("true")) {
            advance();
            return DataValue::boolean(true);
        }
        if (isIdentifier("false")) {
            advance();
            return DataValue::boolean(false);
        }
        fail("expected a string, number, true, or false");
    }

    DataValue parseComparison() {
        const std::string variable = expectIdentifier("a condition variable");
        if (cur().kind != eve::dnut::DnutTokenKind::Punctuator)
            fail("expected a comparison operator");
        const std::string operation = advance().text;
        std::string       mapped;
        if (operation == "==")
            mapped = "eq";
        else if (operation == "!=")
            mapped = "ne";
        else if (operation == ">")
            mapped = "gt";
        else if (operation == "<")
            mapped = "lt";
        else if (operation == ">=")
            mapped = "ge";
        else if (operation == "<=")
            mapped = "le";
        else
            fail("unsupported comparison operator '" + operation + "'");
        return DataValue::object({{"var", DataValue::string(variable)},
                                  {"op", DataValue::string(mapped)},
                                  {"value", parseLiteral()}});
    }

    DataValue parseNot() {
        if (isPunct("!")) {
            advance();
            return DataValue::object({{"not", parseNot()}});
        }
        if (isPunct("(")) {
            advance();
            DataValue value = parseOr();
            expectPunct(")");
            return value;
        }
        return parseComparison();
    }

    DataValue parseAnd() {
        DataValue left = parseNot();
        while (isPunct("&&")) {
            advance();
            DataValue right = parseNot();
            if (DataValue* all = left.find("all"); all && all->isArray())
                all->pushBack(std::move(right));
            else
                left = DataValue::object(
                    {{"all", DataValue::array({std::move(left), std::move(right)})}});
        }
        return left;
    }

    DataValue parseOr() {
        DataValue left = parseAnd();
        while (isPunct("||")) {
            advance();
            DataValue right = parseAnd();
            if (DataValue* any = left.find("any"); any && any->isArray())
                any->pushBack(std::move(right));
            else
                left = DataValue::object(
                    {{"any", DataValue::array({std::move(left), std::move(right)})}});
        }
        return left;
    }

    void parseAttributes(int sourceLine, DataValue::Object& out) {
        while (!atEnd() && cur().line == sourceLine && !isPunct("}")) {
            const std::string name = expectIdentifier("an attribute name");
            if (name == "meta" && isPunct("(")) {
                advance();
                DataValue::Object metadata;
                while (!isPunct(")")) {
                    const std::string key = expectIdentifier("a metadata key");
                    expectPunct("=");
                    DataValue value = parseLiteral();
                    const std::string scalar = scalarToString(value);
                    if (scalar.empty() && !value.isString()) fail("metadata values must be scalar");
                    metadata.emplace(key, DataValue::string(scalar));
                    if (isPunct(","))
                        advance();
                    else if (!isPunct(")"))
                        fail("expected ',' or ')' in metadata");
                }
                advance();
                out.emplace("meta", DataValue::object(std::move(metadata)));
                continue;
            }
            if (name == "tags") {
                expectPunct("=");
                expectPunct("[");
                std::vector<DataValue> tags;
                while (!isPunct("]")) {
                    if (cur().kind != eve::dnut::DnutTokenKind::String)
                        fail("tag values must be quoted strings");
                    tags.push_back(DataValue::string(advance().text));
                    if (isPunct(","))
                        advance();
                    else if (!isPunct("]"))
                        fail("expected ',' or ']' in tags");
                }
                advance();
                out.emplace("tags", DataValue::array(std::move(tags)));
                continue;
            }
            expectPunct("=");
            if (name != "weight" && name != "i18n" && name != "id")
                fail("unknown pool line attribute '" + name + "'");
            out.emplace(name, parseLiteral());
        }
    }

    DataValue parseLine(const std::string& poolId, int lineIndex, const DataValue* inheritedCondition) {
        const int sourceLine = cur().line;
        std::string speaker;
        if (isPunct("-")) {
            advance();
        } else {
            if (isIdentifier("when")) fail("when blocks cannot be nested");
            speaker = expectIdentifier("a speaker or '-'");
            expectPunct(":");
        }
        if (cur().kind != eve::dnut::DnutTokenKind::String) fail("expected quoted dialogue text");
        DataValue::Object fields;
        fields.emplace("speaker", DataValue::string(speaker));
        fields.emplace("text", DataValue::string(advance().text));
        if (inheritedCondition) fields.emplace("when", *inheritedCondition);

        DataValue::Object attributes;
        parseAttributes(sourceLine, attributes);
        bool hasId = false;
        for (auto& [key, value] : attributes) {
            if (key == "id") hasId = true;
            fields.emplace(std::move(key), std::move(value));
        }
        if (!hasId) fields.emplace("id", DataValue::string(poolId + "." + std::to_string(lineIndex)));
        return DataValue::object(std::move(fields));
    }

    const std::vector<eve::dnut::DnutToken>& tokens_;
    std::size_t                              end_;
    std::size_t                              index_;
};

void addError(std::vector<ConversationDiagnostic>& diagnostics, const std::string& path,
              int line, int column, std::string message, std::string code = "DnutParseError") {
    diagnostics.push_back({ConversationDiagnostic::Severity::Error, path, line,
                           path + ":" + std::to_string(line) + ": " + message,
                           std::move(code), column, {}});
}

bool validateEnvelope(const std::vector<eve::dnut::DnutToken>& tokens, const std::string& path,
                      std::vector<ConversationDiagnostic>& diagnostics, DnutDocument& document) {
    if (tokens.size() < 5 || tokens[0].kind != eve::dnut::DnutTokenKind::Identifier ||
        tokens[0].text != "schema" || tokens[1].kind != eve::dnut::DnutTokenKind::String ||
        tokens[2].kind != eve::dnut::DnutTokenKind::Identifier || tokens[2].text != "version" ||
        tokens[3].kind != eve::dnut::DnutTokenKind::Number) {
        const auto& token = tokens.front();
        addError(diagnostics, path, token.line, token.column,
                 "expected schema \"eve.dnut\" and version 1");
        return false;
    }
    document.schema  = tokens[1].text;
    document.version = static_cast<int>(std::strtol(tokens[3].text.c_str(), nullptr, 10));
    if (document.schema != "eve.dnut" || document.version != DnutDocument::CurrentVersion) {
        addError(diagnostics, path, tokens[0].line, tokens[0].column,
                 "unsupported dnut schema or version", "UnsupportedSchemaVersion");
        return false;
    }
    return true;
}

}  // namespace

eve::Result<DnutDocument> parseDnutDocument(const std::string& source, const std::string& path,
                                            std::vector<ConversationDiagnostic>& diagnostics) {
    DnutDocument document;
    auto tokensResult = eve::dnut::lexDnut(source, path);
    if (!tokensResult.ok()) {
        const eve::Diagnostic* error = tokensResult.error();
        int line = 1;
        int column = 1;
        if (error)
            for (const auto& [key, value] : error->details()) {
                if (key == "line") line = std::atoi(value.c_str());
                if (key == "column") column = std::atoi(value.c_str());
            }
        addError(diagnostics, path, line, column, error ? error->message() : "could not lex dnut");
        return eve::Result<DnutDocument>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::ParseError,
                                   diagnostics.back().message, path, {}, "dialogue.dnut.parse"));
    }
    std::vector<eve::dnut::DnutToken> tokens = std::move(tokensResult).takeValue();
    if (!validateEnvelope(tokens, path, diagnostics, document))
        return eve::Result<DnutDocument>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::UnknownVersion,
                                   diagnostics.back().message, path, {}, "dialogue.dnut.parse"));

    auto blocksResult = eve::dnut::scanDnutBlocks(tokens, path);
    if (!blocksResult.ok()) {
        const eve::Diagnostic* error = blocksResult.error();
        addError(diagnostics, path, 1, 1, error ? error->message() : "could not scan dnut blocks");
        return eve::Result<DnutDocument>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::ParseError,
                                   diagnostics.back().message, path, {}, "dialogue.dnut.parse"));
    }

    DataValue::Object pools;
    try {
        for (const auto& block : blocksResult.value()) {
            if (block.kind == "pool") {
                auto [id, pool] = PoolBlockParser(tokens, block).parse();
                if (pools.contains(id))
                    throw PoolParseError(tokens[block.beginToken], "duplicate pool id '" + id + "'");
                pools.emplace(std::move(id), std::move(pool));
            } else if (block.kind != "conversation" && block.kind != "story") {
                throw PoolParseError(tokens[block.beginToken],
                                     "unknown top-level block '" + block.kind + "'");
            }
        }
    } catch (const PoolParseError& error) {
        addError(diagnostics, path, error.line, error.column, error.what());
        return eve::Result<DnutDocument>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::ParseError,
                                   diagnostics.back().message, path, {}, "dialogue.dnut.parse"));
    }
    document.poolRoot = DataValue::object({{"pools", DataValue::object(std::move(pools))}});

    eve::dnut::StepKindRegistry registry;
    auto registered = registerDialogueSequenceSteps(registry);
    if (!registered.ok())
        return eve::Result<DnutDocument>::failure(registered.status());
    eve::dnut::DnutCompileOutput compiled =
        eve::dnut::compileDnutConversations(source, path, registry);
    for (const auto& diagnostic : compiled.diagnostics)
        diagnostics.push_back({diagnostic.severity == eve::dnut::DnutSeverity::Error
                                   ? ConversationDiagnostic::Severity::Error
                                   : ConversationDiagnostic::Severity::Warning,
                               diagnostic.path, diagnostic.line, diagnostic.message,
                               "DnutParseError", diagnostic.column, {}});
    if (compiled.hasErrors())
        return eve::Result<DnutDocument>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::ParseError,
                                   diagnostics.back().message, path, {}, "dialogue.dnut.parse"));
    document.conversations = std::move(compiled.assets);
    return eve::Result<DnutDocument>::success(std::move(document));
}

}  // namespace eve::dialogue
