#include "dnut_interpreter/DnutCompiler.h"

#include "dnut_interpreter/DnutBlockScanner.h"
#include "dnut_interpreter/DnutLexer.h"

#include <cstdlib>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace eve::dnut {
namespace {

class ConversationParseError final : public std::runtime_error {
public:
    ConversationParseError(const DnutToken& token, std::string message)
        : std::runtime_error(std::move(message)), line(token.line), column(token.column) {}

    int line;
    int column;
};

eve::Value numberValue(const std::string& raw) {
    if (raw.find_first_of(".eE") != std::string::npos) return eve::Value::number(std::strtod(raw.c_str(), nullptr));
    return eve::Value::integer(std::strtoll(raw.c_str(), nullptr, 10));
}

class ConversationBlockParser {
public:
    ConversationBlockParser(const std::vector<DnutToken>& tokens, const DnutBlock& block)
        : tokens_(tokens), end_(block.endToken), index_(block.beginToken) {}

    SequenceAsset parse() {
        const DnutToken declaration = cur();
        expectKeyword("conversation");
        SequenceAsset asset;
        asset.sourceLine   = declaration.line;
        asset.sourceColumn = declaration.column;
        asset.id           = expectIdentifier("a conversation id");
        while (!isPunct("{")) {
            const std::string key = expectIdentifier("a conversation field");
            expectPunct("=");
            if (key == "entry")
                asset.entry = scalarText();
            else if (key == "version") {
                if (cur().kind != DnutTokenKind::Number) fail("version requires an integer");
                asset.version = static_cast<int>(std::strtol(advance().text.c_str(), nullptr, 10));
            } else {
                fail("unknown conversation field '" + key + "'");
            }
        }
        expectPunct("{");
        while (!isPunct("}")) {
            if (atEnd()) fail("unterminated conversation block");
            if (isIdentifier("parameter"))
                asset.parameters.push_back(parseParameter());
            else if (isIdentifier("node"))
                asset.nodes.push_back(parseNode());
            else
                fail("conversation accepts only parameter or node declarations");
        }
        expectPunct("}");
        return asset;
    }

private:
    const DnutToken& cur() const {
        const std::size_t position = index_ < tokens_.size() ? index_ : tokens_.size() - 1;
        return tokens_[position];
    }
    bool atEnd() const { return index_ >= end_ || cur().kind == DnutTokenKind::EndOfFile; }
    DnutToken advance() {
        const DnutToken token = cur();
        if (!atEnd()) ++index_;
        return token;
    }
    bool isPunct(const char* text) const {
        return cur().kind == DnutTokenKind::Punctuator && cur().text == text;
    }
    bool isIdentifier(const char* text) const {
        return cur().kind == DnutTokenKind::Identifier && cur().text == text;
    }
    [[noreturn]] void fail(std::string message) const {
        throw ConversationParseError(cur(), std::move(message));
    }
    void expectPunct(const char* text) {
        if (!isPunct(text)) fail("expected '" + std::string(text) + "'");
        advance();
    }
    void expectKeyword(const char* text) {
        if (!isIdentifier(text)) fail("expected '" + std::string(text) + "'");
        advance();
    }
    std::string expectIdentifier(const std::string& description) {
        if (cur().kind != DnutTokenKind::Identifier) fail("expected " + description);
        return advance().text;
    }
    std::string scalarText() {
        if (cur().kind != DnutTokenKind::Identifier && cur().kind != DnutTokenKind::String &&
            cur().kind != DnutTokenKind::Number)
            fail("expected a scalar value");
        return advance().text;
    }

    eve::Value parseLiteral() {
        if (isPunct("-")) {
            advance();
            if (cur().kind != DnutTokenKind::Number) fail("'-' must be followed by a number");
            eve::Value value = numberValue(advance().text);
            return value.isInt64() ? eve::Value::integer(-value.asInt())
                                   : eve::Value::number(-value.asDouble());
        }
        if (cur().kind == DnutTokenKind::String) return eve::Value::string(advance().text);
        if (cur().kind == DnutTokenKind::Number) return numberValue(advance().text);
        if (cur().kind == DnutTokenKind::Identifier) {
            if (isIdentifier("true")) {
                advance();
                return eve::Value::boolean(true);
            }
            if (isIdentifier("false")) {
                advance();
                return eve::Value::boolean(false);
            }
            return eve::Value::string(advance().text);
        }
        fail("expected a literal value");
    }

    eve::Value parseObjectArguments() {
        expectPunct("(");
        eve::Value::Object fields;
        while (!isPunct(")")) {
            const std::string key = expectIdentifier("an argument name");
            expectPunct("=");
            fields.emplace(key, parseLiteral());
            if (isPunct(","))
                advance();
            else if (!isPunct(")"))
                fail("expected ',' or ')' in argument list");
        }
        advance();
        return eve::Value(std::move(fields));
    }

    SequenceParameter parseParameter() {
        const DnutToken declaration = advance();
        SequenceParameter parameter;
        parameter.sourceLine   = declaration.line;
        parameter.sourceColumn = declaration.column;
        parameter.name         = expectIdentifier("a parameter name");
        const std::string type = expectIdentifier("a parameter type");
        if (type == "string")
            parameter.type = SequenceParameterType::String;
        else if (type == "int")
            parameter.type = SequenceParameterType::Integer;
        else if (type == "float")
            parameter.type = SequenceParameterType::Number;
        else if (type == "bool")
            parameter.type = SequenceParameterType::Boolean;
        else
            fail("unknown parameter type '" + type + "'");

        while (!atEnd() && cur().line == declaration.line) {
            const std::string field = expectIdentifier("a parameter field");
            if (field == "required")
                parameter.required = true;
            else if (field == "optional")
                parameter.required = false;
            else if (field == "default") {
                expectPunct("=");
                parameter.defaultValue = parseLiteral();
                parameter.required     = false;
            } else {
                fail("unknown parameter field '" + field + "'");
            }
        }
        return parameter;
    }

    void appendMutation(eve::Value& payload, eve::Value mutation) {
        eve::Value* mutations = payload.find("stateMutations");
        if (!mutations) {
            payload.set("stateMutations", eve::Value(eve::Value::Array{}));
            mutations = payload.find("stateMutations");
        }
        mutations->pushBack(std::move(mutation));
    }

    SequenceRoute parseRoute() {
        const DnutToken declaration = advance();
        SequenceRoute route;
        route.sourceLine   = declaration.line;
        route.sourceColumn = declaration.column;
        route.label        = expectIdentifier("a stable route id");
        bool hasTarget     = false;
        while (!atEnd() && cur().line == declaration.line && !isPunct("}")) {
            const std::string key = expectIdentifier("a route field");
            if (key == "payment") {
                route.payload.set("payment", parseObjectArguments());
                continue;
            }
            if (key == "mutation") {
                appendMutation(route.payload, parseObjectArguments());
                continue;
            }
            expectPunct("=");
            eve::Value value = parseLiteral();
            if (key == "target") {
                if (!value.isString()) fail("route target must be a node id");
                route.target = value.asString();
                hasTarget    = true;
            } else if (key == "when") {
                route.condition = std::move(value);
            } else if (key == "text" || key == "i18n") {
                route.payload.set(key, std::move(value));
            } else {
                fail("unknown route field '" + key + "'");
            }
        }
        if (!hasTarget) fail("route requires target");
        return route;
    }

    SequenceNode parseNode() {
        const DnutToken declaration = advance();
        SequenceNode node;
        node.sourceLine   = declaration.line;
        node.sourceColumn = declaration.column;
        node.id           = expectIdentifier("a node id");
        node.type         = expectIdentifier("a node type");

        while (!atEnd() && cur().line == declaration.line && !isPunct("{") && !isPunct("}")) {
            const std::string key = expectIdentifier("a node field");
            if (key == "arguments") {
                node.payload.set("arguments", parseObjectArguments());
                continue;
            }
            if (key == "payment") {
                node.payload.set("payment", parseObjectArguments());
                continue;
            }
            if (key == "mutation") {
                appendMutation(node.payload, parseObjectArguments());
                continue;
            }
            expectPunct("=");
            eve::Value value = parseLiteral();
            if (key == "next") {
                if (!value.isString()) fail("next must be a node id");
                node.next = value.asString();
            } else if (key == "target" && node.type == "command") {
                node.payload.set("name", std::move(value));
            } else if (key == "result") {
                node.payload.set("resultLocal", std::move(value));
            } else {
                node.payload.set(key, std::move(value));
            }
        }
        if (isPunct("{")) {
            advance();
            while (!isPunct("}")) {
                if (atEnd()) fail("unterminated node block");
                if (!isIdentifier("route")) fail("node block accepts only route declarations");
                node.routes.push_back(parseRoute());
            }
            advance();
        }
        return node;
    }

    const std::vector<DnutToken>& tokens_;
    std::size_t                   end_;
    std::size_t                   index_;
};

void appendResultFailure(DnutCompileOutput& output, const std::string& path,
                         const eve::Diagnostic* diagnostic, const char* fallback) {
    int line   = 0;
    int column = 0;
    if (diagnostic) {
        for (const auto& [key, value] : diagnostic->details()) {
            if (key == "line") line = std::atoi(value.c_str());
            if (key == "column") column = std::atoi(value.c_str());
        }
    }
    output.diagnostics.push_back(
        {DnutSeverity::Error, path, line, column, diagnostic ? diagnostic->message() : fallback});
}

bool validateDocumentEnvelope(const std::vector<DnutToken>& tokens, const std::string& path,
                              DnutCompileOutput& output) {
    if (tokens.size() < 5 || tokens[0].kind != DnutTokenKind::Identifier || tokens[0].text != "schema" ||
        tokens[1].kind != DnutTokenKind::String || tokens[1].text != "eve.dnut" ||
        tokens[2].kind != DnutTokenKind::Identifier || tokens[2].text != "version" ||
        tokens[3].kind != DnutTokenKind::Number || std::strtol(tokens[3].text.c_str(), nullptr, 10) != 1) {
        const DnutToken& token = tokens.empty() ? DnutToken{} : tokens.front();
        output.diagnostics.push_back(
            {DnutSeverity::Error, path, token.line, token.column,
             "expected schema \"eve.dnut\" and version 1"});
        return false;
    }
    return true;
}

}  // namespace

DnutCompileOutput compileDnutConversations(std::string_view source, const std::string& path,
                                           const StepKindRegistry& registry) {
    DnutCompileOutput output;
    auto tokensResult = lexDnut(source, path);
    if (!tokensResult.ok()) {
        appendResultFailure(output, path, tokensResult.error(), "could not lex the document");
        return output;
    }
    std::vector<DnutToken> tokens = std::move(tokensResult).takeValue();
    if (!validateDocumentEnvelope(tokens, path, output)) return output;

    auto blocksResult = scanDnutBlocks(tokens, path);
    if (!blocksResult.ok()) {
        appendResultFailure(output, path, blocksResult.error(), "could not scan the document");
        return output;
    }

    std::unordered_set<std::string> assetIds;
    for (const auto& block : blocksResult.value()) {
        if (block.kind != "conversation") continue;
        try {
            SequenceAsset asset = ConversationBlockParser(tokens, block).parse();
            bool          valid = true;
            if (!assetIds.insert(asset.id).second) {
                output.diagnostics.push_back(
                    {DnutSeverity::Error, path, asset.sourceLine, asset.sourceColumn,
                     "duplicate conversation id '" + asset.id + "'"});
                valid = false;
            }
            if (auto graph = asset.validate(); !graph.ok()) {
                output.diagnostics.push_back(
                    {DnutSeverity::Error, path, asset.sourceLine, asset.sourceColumn,
                     graph.error() ? graph.error()->message() : "invalid conversation graph"});
                valid = false;
            }
            for (const auto& node : asset.nodes) {
                if (isCoreSequenceNodeType(node.type) && !registry.contains(node.type)) continue;
                auto validated = registry.validate(node);
                if (!validated.ok()) {
                    output.diagnostics.push_back(
                        {DnutSeverity::Error, path, node.sourceLine, node.sourceColumn,
                         validated.error() ? validated.error()->message() : "invalid conversation node"});
                    valid = false;
                }
            }
            if (valid) output.assets.push_back(std::move(asset));
        } catch (const ConversationParseError& error) {
            output.diagnostics.push_back(
                {DnutSeverity::Error, path, error.line, error.column, error.what()});
        }
    }
    return output;
}

}  // namespace eve::dnut
