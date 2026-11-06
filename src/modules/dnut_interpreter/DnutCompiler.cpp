#include "dnut_interpreter/DnutCompiler.h"

#include "dnut_interpreter/DnutLexer.h"

#include <cstdlib>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace eve::dnut {

bool DnutCompileOutput::hasErrors() const { return dnut::hasErrors(diagnostics); }

namespace {

/** @brief Thrown to abandon one statement; the parser then resynchronizes. */
struct ParseAbort {};

struct CompiledOption;

struct CompiledStatement {
    enum class Kind { Step, If, Choice, Loop, Parallel, Break, Continue, Label, Jump };

    Kind        kind = Kind::Step;
    std::string nodeId;
    /** @brief Step type name, or the control-flow type the statement lowers to. */
    std::string type;
    eve::Value  payload = eve::Value::Object{};
    eve::Value  condition;
    std::vector<CompiledStatement> thenBody;
    std::vector<CompiledStatement> elseBody;
    std::vector<CompiledOption>    options;
    /** @brief Parallel lane bodies (`parallel { lane {…} lane {…} }`). */
    std::vector<std::vector<CompiledStatement>> lanes;
    /** @brief Label / jump name, or loop mode (`forever` / `while` / `times`). */
    std::string name;
    int         sourceLine   = 0;
    int         sourceColumn = 0;
};

/** @brief One `option` arm of a `choice` statement. */
struct CompiledOption {
    std::string                    label;
    eve::Value                     condition;
    std::vector<CompiledStatement> body;
    std::string                    nodeId;
};

/** @brief Open loop while emitting, so `break` / `continue` can resolve targets. */
struct LoopEmitFrame {
    std::string loopNodeId;
    std::string afterNodeId;
};

eve::Value numberValue(const std::string& raw) {
    if (raw.find_first_of(".eE") != std::string::npos) return eve::Value::number(std::strtod(raw.c_str(), nullptr));
    return eve::Value::integer(std::strtoll(raw.c_str(), nullptr, 10));
}

bool isComparisonOperator(const std::string& text) {
    return text == "==" || text == "!=" || text == ">" || text == "<" || text == ">=" || text == "<=";
}

const char* comparisonSpelling(const std::string& text) {
    if (text == "==") return "eq";
    if (text == "!=") return "ne";
    if (text == ">") return "gt";
    if (text == "<") return "lt";
    if (text == ">=") return "ge";
    return "le";
}

class StoryParser {
public:
    StoryParser(std::vector<DnutToken> tokens, std::string path, const StepKindRegistry& registry,
                DnutCompileOutput& output)
        : tokens_(std::move(tokens)), path_(std::move(path)), registry_(registry), output_(output) {}

    void run() {
        while (!atEnd()) {
            if (isIdentifier("story") || isIdentifier("common")) {
                try {
                    parseStory();
                } catch (const ParseAbort&) {
                    synchronize();
                }
                continue;
            }
            skipForeignBlock();
        }
    }

private:
    const DnutToken& cur() const { return tokens_[index_]; }
    const DnutToken& peek(std::size_t offset) const {
        const std::size_t position = index_ + offset;
        return tokens_[position < tokens_.size() ? position : tokens_.size() - 1];
    }
    bool atEnd() const { return cur().kind == DnutTokenKind::EndOfFile; }
    /** @brief Consume and return the current token; the EOF token repeats forever. */
    DnutToken advance() {
        const DnutToken token = tokens_[index_];
        if (!atEnd()) ++index_;
        return token;
    }
    bool isPunct(const std::string& text) const {
        return cur().kind == DnutTokenKind::Punctuator && cur().text == text;
    }
    bool isIdentifier(const std::string& text) const {
        return cur().kind == DnutTokenKind::Identifier && cur().text == text;
    }

    [[noreturn]] void error(const DnutToken& token, std::string message) {
        output_.diagnostics.push_back(
            {DnutSeverity::Error, path_, token.line, token.column, std::move(message)});
        errorLine_ = token.line;
        throw ParseAbort{};
    }
    [[noreturn]] void errorHere(std::string message) { error(cur(), std::move(message)); }

    void expectPunct(const std::string& text) {
        if (!isPunct(text)) errorHere("expected '" + text + "'");
        advance();
    }
    std::string expectIdentifier(const std::string& what) {
        if (cur().kind != DnutTokenKind::Identifier) errorHere("expected " + what);
        return advance().text;
    }
    std::string nextNodeId() { return "n" + std::to_string(++nodeCounter_); }

    /** @brief Skip tokens after a failed statement until a plausible statement start. */
    void synchronize() {
        int  depth     = 0;
        bool consumed  = false;
        while (!atEnd()) {
            if (depth == 0) {
                if (isPunct("}")) return;
                if (consumed && cur().kind == DnutTokenKind::Identifier && cur().line > errorLine_) return;
            }
            if (isPunct("{")) {
                ++depth;
            } else if (isPunct("}")) {
                if (depth == 0) return;
                --depth;
            }
            advance();
            consumed = true;
        }
    }

    /** @brief Skip a top-level block owned by another `.dnut` dialect. */
    void skipForeignBlock() {
        int depth = 0;
        while (!atEnd()) {
            if (depth == 0 && (isIdentifier("story") || isIdentifier("common"))) return;
            if (isPunct("{")) {
                ++depth;
            } else if (isPunct("}")) {
                if (depth > 0) --depth;
            }
            advance();
        }
    }

    void parseStory() {
        const bool isCommon = isIdentifier("common");
        advance();  // 'story' | 'common'
        invalidStep_ = false;
        const std::string id = expectIdentifier(isCommon ? "a common-event id" : "a story id");
        bool              repeatable = isCommon;  // common events are reusable by default
        int               version    = 1;
        while (cur().kind == DnutTokenKind::Identifier) {
            const std::string modifier = cur().text;
            if (modifier == "repeatable") {
                repeatable = true;
                advance();
                continue;
            }
            if (modifier == "version") {
                advance();
                expectPunct("=");
                if (cur().kind != DnutTokenKind::Number) errorHere("version requires an integer");
                version = static_cast<int>(std::strtol(advance().text.c_str(), nullptr, 10));
                if (version < 1) errorHere("version must be positive");
                continue;
            }
            errorHere(std::string("unknown ") + (isCommon ? "common" : "story") + " modifier '" + modifier + "'");
        }

        std::vector<CompiledStatement> body;
        parseBlockBody(body);

        SequenceAsset asset;
        asset.id         = id;
        asset.version    = version;
        asset.repeatable = repeatable;
        const std::string endId = nextNodeId();
        SequenceNode      endNode;
        endNode.id   = endId;
        endNode.type = "end";
        loopStack_.clear();
        pendingJumps_.clear();
        labels_.clear();
        const std::string entry = emitBlock(body, endId, asset);
        asset.entry             = entry.empty() ? endId : entry;
        asset.nodes.push_back(std::move(endNode));
        resolveJumps(asset);

        bool publishable = !invalidStep_;
        if (auto validated = asset.validate(); !validated.ok()) {
            const auto* diagnostic = validated.error();
            output_.diagnostics.push_back(
                {DnutSeverity::Error, path_, 0, 0, diagnostic ? diagnostic->message() : "invalid sequence asset"});
            publishable = false;
        }
        if (!publishedIds_.insert(asset.id).second) {
            output_.diagnostics.push_back(
                {DnutSeverity::Error, path_, 0, 0,
                 std::string("duplicate ") + (isCommon ? "common" : "story") + " id '" + asset.id + "'"});
            publishable = false;
        }
        if (publishable) output_.assets.push_back(std::move(asset));
    }

    void parseBlockBody(std::vector<CompiledStatement>& out) {
        expectPunct("{");
        while (true) {
            if (atEnd()) errorHere("unterminated block");
            if (isPunct("}")) {
                advance();
                return;
            }
            try {
                out.push_back(parseStatement());
            } catch (const ParseAbort&) {
                synchronize();
            }
        }
    }

    CompiledStatement parseStatement() {
        if (cur().kind != DnutTokenKind::Identifier) errorHere("expected a statement");
        const std::string keyword = cur().text;
        if (keyword == "if") return parseIf();
        if (keyword == "choice") return parseChoice();
        if (keyword == "wait") return parseWait();
        if (keyword == "call" || keyword == "common") return parseCall();
        if (keyword == "loop") return parseLoop();
        if (keyword == "break") return parseBreakOrContinue(true);
        if (keyword == "continue") return parseBreakOrContinue(false);
        if (keyword == "label") return parseLabel();
        if (keyword == "jump") return parseJump();
        if (keyword == "parallel") return parseParallel();
        if (keyword == "end") {
            advance();
            CompiledStatement statement;
            statement.type   = "end";
            statement.nodeId = nextNodeId();
            return statement;
        }
        return parseStep();
    }

    CompiledStatement parseStep() {
        const DnutToken  start = cur();
        CompiledStatement statement;
        statement.type   = advance().text;
        statement.nodeId = nextNodeId();
        while (cur().kind == DnutTokenKind::Identifier && peek(1).kind == DnutTokenKind::Punctuator &&
               peek(1).text == "=") {
            const std::string field = advance().text;
            advance();  // '='
            statement.payload.set(field, parseValue());
        }

        // Validate against the vocabulary here, while the statement's own source
        // location is still available, so a bad field points at the authored line
        // instead of at the whole document.
        SequenceNode candidate;
        candidate.id      = statement.nodeId;
        candidate.type    = statement.type;
        candidate.payload = statement.payload;
        if (auto validated = registry_.validate(candidate); !validated.ok()) {
            const auto* diagnostic = validated.error();
            output_.diagnostics.push_back({DnutSeverity::Error, path_, start.line, start.column,
                                           diagnostic ? diagnostic->message() : "invalid step"});
            invalidStep_ = true;
        }
        return statement;
    }

    CompiledStatement parseWait() {
        advance();  // 'wait'
        CompiledStatement statement;
        statement.type   = "wait";
        statement.nodeId = nextNodeId();
        if (cur().kind == DnutTokenKind::Number) {
            statement.payload.set("duration", numberValue(advance().text));
            return statement;
        }
        if (isIdentifier("duration")) {
            advance();
            expectPunct("=");
            statement.payload.set("duration", parseValue());
            return statement;
        }
        errorHere("wait requires a duration");
    }

    CompiledStatement parseCall() {
        advance();  // 'call' | 'common' (CallCommonEvent)
        CompiledStatement statement;
        statement.type   = "call";
        statement.nodeId = nextNodeId();
        statement.payload.set("target", eve::Value::string(expectIdentifier("a called sequence id")));
        return statement;
    }

    CompiledStatement parseLoop() {
        const DnutToken start = cur();
        advance();  // 'loop'
        CompiledStatement statement;
        statement.kind         = CompiledStatement::Kind::Loop;
        statement.nodeId       = nextNodeId();
        statement.sourceLine   = start.line;
        statement.sourceColumn = start.column;
        statement.name         = "forever";
        if (isIdentifier("while")) {
            advance();
            statement.name      = "while";
            statement.condition = parseCondition();
        } else if (isIdentifier("times")) {
            advance();
            expectPunct("=");
            statement.name = "times";
            statement.payload.set("count", parseValue());
        }
        parseBlockBody(statement.thenBody);
        return statement;
    }

    CompiledStatement parseBreakOrContinue(bool isBreak) {
        const DnutToken start = cur();
        advance();
        CompiledStatement statement;
        statement.kind         = isBreak ? CompiledStatement::Kind::Break : CompiledStatement::Kind::Continue;
        statement.type         = isBreak ? "break" : "continue";
        statement.nodeId       = nextNodeId();
        statement.sourceLine   = start.line;
        statement.sourceColumn = start.column;
        return statement;
    }

    CompiledStatement parseLabel() {
        const DnutToken start = cur();
        advance();  // 'label'
        CompiledStatement statement;
        statement.kind         = CompiledStatement::Kind::Label;
        statement.type         = "label";
        statement.nodeId       = nextNodeId();
        statement.name         = expectIdentifier("a label name");
        statement.sourceLine   = start.line;
        statement.sourceColumn = start.column;
        return statement;
    }

    CompiledStatement parseJump() {
        const DnutToken start = cur();
        advance();  // 'jump'
        CompiledStatement statement;
        statement.kind         = CompiledStatement::Kind::Jump;
        statement.type         = "jump";
        statement.nodeId       = nextNodeId();
        statement.name         = expectIdentifier("a label name");
        statement.sourceLine   = start.line;
        statement.sourceColumn = start.column;
        return statement;
    }

    CompiledStatement parseParallel() {
        const DnutToken start = cur();
        advance();  // 'parallel'
        CompiledStatement statement;
        statement.kind         = CompiledStatement::Kind::Parallel;
        statement.type         = "parallel";
        statement.nodeId       = nextNodeId();
        statement.sourceLine   = start.line;
        statement.sourceColumn = start.column;
        expectPunct("{");
        while (!isPunct("}")) {
            if (atEnd()) errorHere("unterminated parallel block");
            if (!isIdentifier("lane")) errorHere("parallel accepts only lane blocks");
            advance();
            std::vector<CompiledStatement> lane;
            parseBlockBody(lane);
            statement.lanes.push_back(std::move(lane));
        }
        advance();  // '}'
        if (statement.lanes.size() < 2) errorHere("parallel requires at least two lanes");
        return statement;
    }

    CompiledStatement parseIf() {
        advance();  // 'if'
        CompiledStatement statement;
        statement.kind      = CompiledStatement::Kind::If;
        statement.nodeId    = nextNodeId();
        statement.condition = parseCondition();
        parseBlockBody(statement.thenBody);
        if (isIdentifier("else")) {
            advance();
            parseBlockBody(statement.elseBody);
        }
        return statement;
    }

    CompiledStatement parseChoice() {
        advance();  // 'choice'
        CompiledStatement statement;
        statement.kind   = CompiledStatement::Kind::Choice;
        statement.nodeId = nextNodeId();
        expectPunct("{");
        while (!isPunct("}")) {
            if (atEnd()) errorHere("unterminated choice block");
            if (!isIdentifier("option")) errorHere("choice accepts only option statements");
            advance();
            CompiledOption option;
            option.nodeId = nextNodeId();
            if (cur().kind != DnutTokenKind::String) errorHere("option requires a quoted label");
            option.label = advance().text;
            if (isIdentifier("when")) {
                advance();
                option.condition = parseCondition();
            }
            parseBlockBody(option.body);
            statement.options.push_back(std::move(option));
        }
        advance();  // '}'
        if (statement.options.empty()) errorHere("choice requires at least one option");
        return statement;
    }

    eve::Value parseCondition() { return parseOr(); }

    eve::Value parseOr() {
        eve::Value left = parseAnd();
        while (isPunct("||")) {
            advance();
            eve::Value right = parseAnd();
            if (auto* object = left.getIf<eve::Value::Object>();
                object != nullptr && object->contains("any") && object->at("any").isArray()) {
                object->at("any").pushBack(std::move(right));
            } else {
                eve::Value::Array operands;
                operands.reserve(2);
                operands.emplace_back(std::move(left));
                operands.emplace_back(std::move(right));
                eve::Value::Object combined;
                combined.emplace("any", std::move(operands));
                left = eve::Value(std::move(combined));
            }
        }
        return left;
    }

    eve::Value parseAnd() {
        eve::Value left = parseNot();
        while (isPunct("&&")) {
            advance();
            eve::Value right = parseNot();
            if (auto* object = left.getIf<eve::Value::Object>();
                object != nullptr && object->contains("all") && object->at("all").isArray()) {
                object->at("all").pushBack(std::move(right));
            } else {
                eve::Value::Array operands;
                operands.reserve(2);
                operands.emplace_back(std::move(left));
                operands.emplace_back(std::move(right));
                eve::Value::Object combined;
                combined.emplace("all", std::move(operands));
                left = eve::Value(std::move(combined));
            }
        }
        return left;
    }

    eve::Value parseNot() {
        if (isPunct("!")) {
            advance();
            return eve::Value(eve::Value::Object{{"not", parseNot()}});
        }
        if (isPunct("(")) {
            advance();
            eve::Value inner = parseOr();
            expectPunct(")");
            return inner;
        }
        return parseComparison();
    }

    eve::Value parseComparison() {
        if (cur().kind != DnutTokenKind::Identifier) errorHere("condition must start with a variable name");
        const std::string variable = advance().text;
        if (cur().kind != DnutTokenKind::Punctuator || !isComparisonOperator(cur().text))
            errorHere("condition requires a comparison operator (== != > < >= <=)");
        const std::string operation = advance().text;
        eve::Value        expected  = parseValue();
        return eve::Value(eve::Value::Object{{"var", eve::Value::string(variable)},
                                             {"op", eve::Value::string(comparisonSpelling(operation))},
                                             {"value", std::move(expected)}});
    }

    eve::Value parseValue() {
        if (isPunct("-")) {
            advance();
            if (cur().kind != DnutTokenKind::Number) errorHere("'-' must be followed by a number");
            eve::Value magnitude = numberValue(advance().text);
            if (magnitude.isInt64()) return eve::Value::integer(-magnitude.asInt());
            return eve::Value::number(-magnitude.asDouble());
        }
        switch (cur().kind) {
            case DnutTokenKind::String: return eve::Value::string(advance().text);
            case DnutTokenKind::Number: return numberValue(advance().text);
            case DnutTokenKind::Identifier:
                if (isIdentifier("true")) {
                    advance();
                    return eve::Value::boolean(true);
                }
                if (isIdentifier("false")) {
                    advance();
                    return eve::Value::boolean(false);
                }
                return eve::Value::string(advance().text);
            default: errorHere("expected a literal value"); break;
        }
    }

    std::string emitBlock(const std::vector<CompiledStatement>& statements, const std::string& next,
                          SequenceAsset& asset) {
        std::string continuation = next;
        for (auto it = statements.rbegin(); it != statements.rend(); ++it)
            continuation = emitStatement(*it, continuation, asset);
        return continuation;
    }

    std::string emitStatement(const CompiledStatement& statement, const std::string& next, SequenceAsset& asset) {
        if (statement.kind == CompiledStatement::Kind::Step) {
            SequenceNode node;
            node.id      = statement.nodeId;
            node.type    = statement.type;
            node.next    = next;
            node.payload = statement.payload;
            asset.nodes.push_back(std::move(node));
            return statement.nodeId;
        }
        if (statement.kind == CompiledStatement::Kind::If) {
            const std::string thenEntry = emitBlock(statement.thenBody, next, asset);
            const std::string elseEntry = emitBlock(statement.elseBody, next, asset);
            SequenceNode      node;
            node.id   = statement.nodeId;
            node.type = "branch";
            node.next = elseEntry;
            SequenceRoute route;
            route.condition = statement.condition;
            route.target    = thenEntry;
            node.routes.push_back(std::move(route));
            asset.nodes.push_back(std::move(node));
            return statement.nodeId;
        }
        if (statement.kind == CompiledStatement::Kind::Choice) {
            SequenceNode node;
            node.id   = statement.nodeId;
            node.type = "choice";
            node.next = next;
            for (const auto& option : statement.options) {
                SequenceRoute route;
                route.label     = option.label;
                route.condition = option.condition;
                route.target    = emitBlock(option.body, next, asset);
                node.routes.push_back(std::move(route));
            }
            asset.nodes.push_back(std::move(node));
            return statement.nodeId;
        }
        if (statement.kind == CompiledStatement::Kind::Loop) {
            loopStack_.push_back({statement.nodeId, next});
            const std::string bodyEntry = emitBlock(statement.thenBody, statement.nodeId, asset);
            loopStack_.pop_back();
            SequenceNode node;
            node.id   = statement.nodeId;
            node.type = "loop";
            node.next = next;
            node.payload.set("mode", eve::Value::string(statement.name));
            if (statement.name == "times") {
                if (const eve::Value* count = statement.payload.find("count")) node.payload.set("count", *count);
            }
            SequenceRoute route;
            if (statement.name == "while") route.condition = statement.condition;
            route.target = bodyEntry;
            node.routes.push_back(std::move(route));
            asset.nodes.push_back(std::move(node));
            return statement.nodeId;
        }
        if (statement.kind == CompiledStatement::Kind::Break ||
            statement.kind == CompiledStatement::Kind::Continue) {
            if (loopStack_.empty()) {
                output_.diagnostics.push_back({DnutSeverity::Error, path_, statement.sourceLine,
                                               statement.sourceColumn,
                                               statement.type + " is not inside a loop"});
                invalidStep_ = true;
            }
            SequenceNode node;
            node.id   = statement.nodeId;
            node.type = statement.type;
            node.next = loopStack_.empty()
                            ? next
                            : (statement.kind == CompiledStatement::Kind::Break ? loopStack_.back().afterNodeId
                                                                                : loopStack_.back().loopNodeId);
            asset.nodes.push_back(std::move(node));
            return statement.nodeId;
        }
        if (statement.kind == CompiledStatement::Kind::Label) {
            if (!labels_.emplace(statement.name, statement.nodeId).second) {
                output_.diagnostics.push_back({DnutSeverity::Error, path_, statement.sourceLine,
                                               statement.sourceColumn, "duplicate label '" + statement.name + "'"});
                invalidStep_ = true;
            }
            SequenceNode node;
            node.id   = statement.nodeId;
            node.type = "label";
            node.next = next;
            node.payload.set("name", eve::Value::string(statement.name));
            asset.nodes.push_back(std::move(node));
            return statement.nodeId;
        }
        if (statement.kind == CompiledStatement::Kind::Jump) {
            SequenceNode node;
            node.id   = statement.nodeId;
            node.type = "jump";
            node.next = next;
            node.payload.set("name", eve::Value::string(statement.name));
            asset.nodes.push_back(std::move(node));
            pendingJumps_.push_back({statement.nodeId, statement.name, statement.sourceLine, statement.sourceColumn});
            return statement.nodeId;
        }
        if (statement.kind == CompiledStatement::Kind::Parallel) {
            const std::string joinId = nextNodeId();
            SequenceNode      join;
            join.id   = joinId;
            join.type = "join";
            asset.nodes.push_back(std::move(join));

            SequenceNode node;
            node.id   = statement.nodeId;
            node.type = "parallel";
            node.next = next;
            node.payload.set("join", eve::Value::string(joinId));
            for (const auto& lane : statement.lanes) {
                SequenceRoute route;
                route.target = emitBlock(lane, joinId, asset);
                node.routes.push_back(std::move(route));
            }
            asset.nodes.push_back(std::move(node));
            return statement.nodeId;
        }
        output_.diagnostics.push_back(
            {DnutSeverity::Error, path_, 0, 0, "internal: unhandled compiled statement kind"});
        invalidStep_ = true;
        return next;
    }

    void resolveJumps(SequenceAsset& asset) {
        for (const auto& jump : pendingJumps_) {
            auto found = labels_.find(jump.name);
            if (found == labels_.end()) {
                output_.diagnostics.push_back({DnutSeverity::Error, path_, jump.line, jump.column,
                                               "jump targets unknown label '" + jump.name + "'"});
                invalidStep_ = true;
                continue;
            }
            SequenceNode* node = nullptr;
            for (auto& candidate : asset.nodes) {
                if (candidate.id == jump.nodeId) {
                    node = &candidate;
                    break;
                }
            }
            if (!node) continue;
            node->payload.set("target", eve::Value::string(found->second));
            node->next = found->second;
        }
    }

    struct PendingJump {
        std::string nodeId;
        std::string name;
        int         line   = 0;
        int         column = 0;
    };

    std::vector<DnutToken>   tokens_;
    std::size_t              index_ = 0;
    std::string              path_;
    const StepKindRegistry&  registry_;
    DnutCompileOutput&       output_;
    int                      nodeCounter_   = 0;
    int                      errorLine_ = 0;
    /** @brief Set when a step statement failed vocabulary validation. */
    bool                     invalidStep_ = false;
    std::unordered_set<std::string> publishedIds_;
    std::vector<LoopEmitFrame>      loopStack_;
    std::unordered_map<std::string, std::string> labels_;
    std::vector<PendingJump>                     pendingJumps_;
};

}  // namespace

DnutCompileOutput compileDnut(std::string_view source, const std::string& path,
                              const StepKindRegistry& registry) {
    DnutCompileOutput output;
    auto              tokens = lexDnut(source, path);
    if (!tokens.ok()) {
        const auto* diagnostic = tokens.error();
        output.diagnostics.push_back({DnutSeverity::Error, path, 0, 0,
                                      diagnostic ? diagnostic->message() : "could not lex the document"});
        return output;
    }
    StoryParser parser(std::move(tokens).takeValue(), path, registry, output);
    parser.run();
    return output;
}

}  // namespace eve::dnut
