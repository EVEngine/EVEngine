#include "settlement/SettlementRules.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <limits>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <string_view>
#include <utility>

namespace eve::settlement {
namespace {

constexpr std::size_t kMaximumConditionSource = 4096;
constexpr std::size_t kMaximumConditionNodes  = 256;
constexpr std::size_t kMaximumConditionDepth  = 32;
constexpr std::size_t kMaximumEvaluationSteps = 1024;

enum class ExpressionType : std::uint8_t { Unknown, Boolean, Number, String };

enum class ExpressionKind : std::uint8_t {
    Literal,
    Field,
    HasTag,
    Not,
    Negate,
    Add,
    Subtract,
    Multiply,
    Divide,
    Equal,
    NotEqual,
    Less,
    LessEqual,
    Greater,
    GreaterEqual,
    And,
    Or,
    Minimum,
    Maximum,
    Clamp,
    Absolute,
};

struct ExpressionNode {
    ExpressionKind kind  = ExpressionKind::Literal;
    ExpressionType type  = ExpressionType::Boolean;
    std::uint16_t  left  = 0;
    std::uint16_t  right = 0;
    std::uint16_t  third = 0;
    eve::Value     literal;
    std::string    name;
};

struct ExpressionProgram {
    std::vector<ExpressionNode> nodes;
    std::uint16_t               root = 0;
};

enum class TokenKind : std::uint8_t { End, Identifier, Number, String, Symbol };

struct Token {
    TokenKind   kind = TokenKind::End;
    std::string text;
    std::size_t line   = 1;
    std::size_t column = 1;
};

const char* typeName(ExpressionType type) noexcept {
    switch (type) {
        case ExpressionType::Unknown: return "Unknown";
        case ExpressionType::Boolean: return "Boolean";
        case ExpressionType::Number: return "Number";
        case ExpressionType::String: return "String";
    }
    return "Unknown";
}

eve::Diagnostic expressionDiagnostic(eve::DiagnosticCode code, std::string message, std::string path,
                                     const Token& token, std::string expected = {}, std::string actual = {}) {
    eve::DiagnosticDetails details{{"line", std::to_string(token.line)}, {"column", std::to_string(token.column)}};
    if (!expected.empty()) details.emplace_back("expected", std::move(expected));
    if (!actual.empty()) details.emplace_back("actual", std::move(actual));
    return eve::Diagnostic::error(code, std::move(message), std::move(path), std::move(details),
                                  "settlement.condition");
}

eve::Result<std::vector<Token>> tokenize(std::string_view source, const std::string& path) {
    std::vector<Token> tokens;
    std::size_t        offset  = 0;
    std::size_t        line    = 1;
    std::size_t        column  = 1;
    auto               advance = [&](char value) {
        ++offset;
        if (value == '\n') {
            ++line;
            column = 1;
        } else {
            ++column;
        }
    };
    while (offset < source.size()) {
        const char value = source[offset];
        if (value == ' ' || value == '\t' || value == '\r' || value == '\n') {
            advance(value);
            continue;
        }
        Token token;
        token.line   = line;
        token.column = column;
        if (std::isalpha(static_cast<unsigned char>(value)) || value == '_') {
            token.kind = TokenKind::Identifier;
            while (offset < source.size()) {
                const char next = source[offset];
                if (!std::isalnum(static_cast<unsigned char>(next)) && next != '_' && next != '.') break;
                token.text.push_back(next);
                advance(next);
            }
            tokens.push_back(std::move(token));
            continue;
        }
        if (std::isdigit(static_cast<unsigned char>(value)) ||
            (value == '.' && offset + 1 < source.size() &&
             std::isdigit(static_cast<unsigned char>(source[offset + 1])))) {
            token.kind              = TokenKind::Number;
            const std::size_t begin = offset;
            char*             end   = nullptr;
            const std::string remaining(source.substr(begin));
            const double      number = std::strtod(remaining.c_str(), &end);
            if (end == remaining.c_str() || !std::isfinite(number))
                return eve::Result<std::vector<Token>>::failure(
                    expressionDiagnostic(eve::DiagnosticCode::ParseError, "condition contains an invalid number", path,
                                         token, "finite Number", token.text));
            const auto length = static_cast<std::size_t>(end - remaining.c_str());
            token.text.assign(source.substr(begin, length));
            for (std::size_t index = 0; index < length; ++index) advance(source[begin + index]);
            tokens.push_back(std::move(token));
            continue;
        }
        if (value == '"') {
            token.kind = TokenKind::String;
            advance(value);
            bool closed = false;
            while (offset < source.size()) {
                const char next = source[offset];
                if (next == '"') {
                    advance(next);
                    closed = true;
                    break;
                }
                if (next == '\n' || next == '\r') break;
                if (next == '\\') {
                    advance(next);
                    if (offset >= source.size()) break;
                    const char escaped = source[offset];
                    switch (escaped) {
                        case 'n': token.text.push_back('\n'); break;
                        case 'r': token.text.push_back('\r'); break;
                        case 't': token.text.push_back('\t'); break;
                        case '"': token.text.push_back('"'); break;
                        case '\\': token.text.push_back('\\'); break;
                        default:
                            return eve::Result<std::vector<Token>>::failure(expressionDiagnostic(
                                eve::DiagnosticCode::ParseError, "condition contains an unsupported string escape",
                                path, token, "supported escape", std::string("\\") + escaped));
                    }
                    advance(escaped);
                    continue;
                }
                token.text.push_back(next);
                advance(next);
            }
            if (!closed)
                return eve::Result<std::vector<Token>>::failure(
                    expressionDiagnostic(eve::DiagnosticCode::ParseError, "condition string is not terminated", path,
                                         token, "closing quote"));
            tokens.push_back(std::move(token));
            continue;
        }
        token.kind = TokenKind::Symbol;
        token.text.push_back(value);
        if (offset + 1 < source.size()) {
            const std::string_view pair = source.substr(offset, 2);
            if (pair == "&&" || pair == "||" || pair == "==" || pair == "!=" || pair == "<=" || pair == ">=") {
                token.text.push_back(source[offset + 1]);
                advance(value);
                advance(source[offset]);
                tokens.push_back(std::move(token));
                continue;
            }
        }
        if (std::string_view("!<>+-*/(),").find(value) == std::string_view::npos)
            return eve::Result<std::vector<Token>>::failure(
                expressionDiagnostic(eve::DiagnosticCode::ParseError, "condition contains an unsupported character",
                                     path, token, "condition operator", token.text));
        advance(value);
        tokens.push_back(std::move(token));
    }
    tokens.push_back(Token{TokenKind::End, {}, line, column});
    return eve::Result<std::vector<Token>>::success(std::move(tokens));
}

class ExpressionParser {
public:
    ExpressionParser(std::vector<Token> tokens, std::string path)
        : tokens_(std::move(tokens)), path_(std::move(path)) {}

    eve::Result<ExpressionProgram> parse() {
        const auto root = parseOr(0);
        if (error_) return eve::Result<ExpressionProgram>::failure(std::move(*error_));
        if (current().kind != TokenKind::End) {
            fail("condition contains trailing input", "end of expression", current().text);
            return eve::Result<ExpressionProgram>::failure(std::move(*error_));
        }
        if (nodes_[root].type != ExpressionType::Boolean) {
            fail("condition root must be Boolean", "Boolean", typeName(nodes_[root].type));
            return eve::Result<ExpressionProgram>::failure(std::move(*error_));
        }
        return eve::Result<ExpressionProgram>::success(ExpressionProgram{std::move(nodes_), root});
    }

private:
    const Token& current() const noexcept { return tokens_[position_]; }

    bool accept(std::string_view text) {
        if (current().text != text) return false;
        ++position_;
        return true;
    }

    void expect(std::string_view text) {
        if (!accept(text)) fail("condition expected '" + std::string(text) + "'", std::string(text), current().text);
    }

    void fail(std::string message, std::string expected = {}, std::string actual = {}) {
        if (!error_)
            error_ = expressionDiagnostic(eve::DiagnosticCode::ParseError, std::move(message), path_, current(),
                                          std::move(expected), std::move(actual));
    }

    std::uint16_t add(ExpressionNode node) {
        if (nodes_.size() >= kMaximumConditionNodes) {
            fail("condition exceeds the node limit", std::to_string(kMaximumConditionNodes));
            return 0;
        }
        nodes_.push_back(std::move(node));
        return static_cast<std::uint16_t>(nodes_.size() - 1);
    }

    std::uint16_t binary(ExpressionKind kind, ExpressionType result, std::uint16_t left, std::uint16_t right,
                         ExpressionType operand, const Token& operation) {
        if (error_) return 0;
        bind(left, operand);
        bind(right, operand);
        if (nodes_[left].type != operand || nodes_[right].type != operand) {
            error_ = expressionDiagnostic(
                eve::DiagnosticCode::TypeMismatch, "condition operator has incompatible operands", path_, operation,
                typeName(operand), std::string(typeName(nodes_[left].type)) + "," + typeName(nodes_[right].type));
            return 0;
        }
        return add(ExpressionNode{kind, result, left, right});
    }

    void bind(std::uint16_t index, ExpressionType type) {
        if (nodes_[index].type == ExpressionType::Unknown && nodes_[index].kind == ExpressionKind::Field)
            nodes_[index].type = type;
    }

    std::uint16_t parseOr(std::size_t depth) {
        auto left = parseAnd(depth + 1);
        while (!error_ && current().text == "||") {
            const auto operation = current();
            ++position_;
            left = binary(ExpressionKind::Or, ExpressionType::Boolean, left, parseAnd(depth + 1),
                          ExpressionType::Boolean, operation);
        }
        return left;
    }

    std::uint16_t parseAnd(std::size_t depth) {
        auto left = parseEquality(depth + 1);
        while (!error_ && current().text == "&&") {
            const auto operation = current();
            ++position_;
            left = binary(ExpressionKind::And, ExpressionType::Boolean, left, parseEquality(depth + 1),
                          ExpressionType::Boolean, operation);
        }
        return left;
    }

    std::uint16_t parseEquality(std::size_t depth) {
        auto left = parseComparison(depth + 1);
        while (!error_ && (current().text == "==" || current().text == "!=")) {
            const auto operation = current();
            ++position_;
            const auto right = parseComparison(depth + 1);
            if (!error_ && nodes_[left].type == ExpressionType::Unknown &&
                nodes_[right].type != ExpressionType::Unknown)
                bind(left, nodes_[right].type);
            if (!error_ && nodes_[right].type == ExpressionType::Unknown &&
                nodes_[left].type != ExpressionType::Unknown)
                bind(right, nodes_[left].type);
            if (!error_ && nodes_[left].type == ExpressionType::Unknown &&
                nodes_[right].type == ExpressionType::Unknown) {
                error_ = expressionDiagnostic(eve::DiagnosticCode::TypeMismatch,
                                              "condition cannot infer either equality operand type", path_, operation,
                                              "one typed operand", "Unknown,Unknown");
                return 0;
            }
            if (!error_ && nodes_[left].type != nodes_[right].type) {
                error_ = expressionDiagnostic(eve::DiagnosticCode::TypeMismatch,
                                              "condition equality operands must have the same type", path_, operation,
                                              typeName(nodes_[left].type), typeName(nodes_[right].type));
                return 0;
            }
            left = add(ExpressionNode{operation.text == "==" ? ExpressionKind::Equal : ExpressionKind::NotEqual,
                                      ExpressionType::Boolean, left, right});
        }
        return left;
    }

    std::uint16_t parseComparison(std::size_t depth) {
        auto left = parseAdditive(depth + 1);
        while (!error_ &&
               (current().text == "<" || current().text == "<=" || current().text == ">" || current().text == ">=")) {
            const auto operation = current();
            ++position_;
            const auto     right = parseAdditive(depth + 1);
            ExpressionKind kind  = ExpressionKind::Less;
            if (operation.text == "<=")
                kind = ExpressionKind::LessEqual;
            else if (operation.text == ">")
                kind = ExpressionKind::Greater;
            else if (operation.text == ">=")
                kind = ExpressionKind::GreaterEqual;
            left = binary(kind, ExpressionType::Boolean, left, right, ExpressionType::Number, operation);
        }
        return left;
    }

    std::uint16_t parseAdditive(std::size_t depth) {
        auto left = parseMultiplicative(depth + 1);
        while (!error_ && (current().text == "+" || current().text == "-")) {
            const auto operation = current();
            ++position_;
            left =
                binary(operation.text == "+" ? ExpressionKind::Add : ExpressionKind::Subtract, ExpressionType::Number,
                       left, parseMultiplicative(depth + 1), ExpressionType::Number, operation);
        }
        return left;
    }

    std::uint16_t parseMultiplicative(std::size_t depth) {
        auto left = parseUnary(depth + 1);
        while (!error_ && (current().text == "*" || current().text == "/")) {
            const auto operation = current();
            ++position_;
            const auto right = parseUnary(depth + 1);
            if (!error_ && operation.text == "/" && nodes_[right].kind == ExpressionKind::Literal) {
                if (const auto* value = nodes_[right].literal.getIf<double>(); value && *value == 0.0) {
                    error_ = expressionDiagnostic(eve::DiagnosticCode::InvalidArgument,
                                                  "condition contains constant division by zero", path_, operation,
                                                  "non-zero Number", "0");
                    return 0;
                }
            }
            left = binary(operation.text == "*" ? ExpressionKind::Multiply : ExpressionKind::Divide,
                          ExpressionType::Number, left, right, ExpressionType::Number, operation);
        }
        return left;
    }

    std::uint16_t parseUnary(std::size_t depth) {
        if (depth > kMaximumConditionDepth) {
            fail("condition exceeds the nesting limit", std::to_string(kMaximumConditionDepth));
            return 0;
        }
        if (current().text == "!" || current().text == "-") {
            const auto operation = current();
            ++position_;
            const auto child    = parseUnary(depth + 1);
            const auto expected = operation.text == "!" ? ExpressionType::Boolean : ExpressionType::Number;
            if (!error_) bind(child, expected);
            if (!error_ && nodes_[child].type != expected) {
                error_ = expressionDiagnostic(eve::DiagnosticCode::TypeMismatch,
                                              "condition unary operator has an incompatible operand", path_, operation,
                                              typeName(expected), typeName(nodes_[child].type));
                return 0;
            }
            return add(
                ExpressionNode{operation.text == "!" ? ExpressionKind::Not : ExpressionKind::Negate, expected, child});
        }
        return parsePrimary(depth + 1);
    }

    std::uint16_t parseFunction(std::string name, const Token& token, std::size_t depth) {
        expect("(");
        std::vector<std::uint16_t> arguments;
        if (!error_ && current().text != ")") {
            do {
                arguments.push_back(parseOr(depth + 1));
            } while (!error_ && accept(","));
        }
        expect(")");
        if (error_) return 0;
        if (name == "has_tag") {
            if (arguments.size() != 1 || nodes_[arguments[0]].kind != ExpressionKind::Literal ||
                nodes_[arguments[0]].type != ExpressionType::String) {
                error_ = expressionDiagnostic(eve::DiagnosticCode::TypeMismatch, "has_tag expects one string literal",
                                              path_, token, "has_tag(String literal)");
                return 0;
            }
            ExpressionNode node{ExpressionKind::HasTag, ExpressionType::Boolean};
            node.name = *nodes_[arguments[0]].literal.getIf<std::string>();
            return add(std::move(node));
        }
        const std::size_t arity = name == "clamp" ? 3 : (name == "abs" ? 1 : 2);
        if ((name != "min" && name != "max" && name != "clamp" && name != "abs") || arguments.size() != arity) {
            error_ = expressionDiagnostic(eve::DiagnosticCode::ParseError,
                                          "condition uses an unknown function or invalid arity", path_, token,
                                          "min/max(2), clamp(3), abs(1), or has_tag(1)", name);
            return 0;
        }
        for (const auto argument : arguments) {
            bind(argument, ExpressionType::Number);
            if (nodes_[argument].type != ExpressionType::Number) {
                error_ =
                    expressionDiagnostic(eve::DiagnosticCode::TypeMismatch, "numeric function received a non-number",
                                         path_, token, "Number", typeName(nodes_[argument].type));
                return 0;
            }
        }
        ExpressionNode node;
        node.type  = ExpressionType::Number;
        node.left  = arguments[0];
        node.right = arguments.size() > 1 ? arguments[1] : 0;
        node.third = arguments.size() > 2 ? arguments[2] : 0;
        if (name == "min")
            node.kind = ExpressionKind::Minimum;
        else if (name == "max")
            node.kind = ExpressionKind::Maximum;
        else if (name == "clamp")
            node.kind = ExpressionKind::Clamp;
        else
            node.kind = ExpressionKind::Absolute;
        return add(std::move(node));
    }

    std::uint16_t parsePrimary(std::size_t depth) {
        if (depth > kMaximumConditionDepth) {
            fail("condition exceeds the nesting limit", std::to_string(kMaximumConditionDepth));
            return 0;
        }
        const auto token = current();
        if (accept("(")) {
            const auto expression = parseOr(depth + 1);
            expect(")");
            return expression;
        }
        if (token.kind == TokenKind::Number) {
            ++position_;
            ExpressionNode node{ExpressionKind::Literal, ExpressionType::Number};
            node.literal = eve::Value(std::strtod(token.text.c_str(), nullptr));
            return add(std::move(node));
        }
        if (token.kind == TokenKind::String) {
            ++position_;
            ExpressionNode node{ExpressionKind::Literal, ExpressionType::String};
            node.literal = eve::Value(token.text);
            return add(std::move(node));
        }
        if (token.kind != TokenKind::Identifier) {
            fail("condition expected a literal, field, function, or parenthesized expression", "expression",
                 token.text);
            return 0;
        }
        ++position_;
        if (token.text == "true" || token.text == "false") {
            ExpressionNode node{ExpressionKind::Literal, ExpressionType::Boolean};
            node.literal = eve::Value(token.text == "true");
            return add(std::move(node));
        }
        if (current().text == "(") return parseFunction(token.text, token, depth + 1);
        ExpressionNode node{ExpressionKind::Field, ExpressionType::Unknown};
        node.name = token.text;
        if (token.text == "kind" || token.text == "resource")
            node.type = ExpressionType::String;
        else if (token.text == "magnitude" || token.text == "tick")
            node.type = ExpressionType::Number;
        else if (!token.text.starts_with("context.") || token.text.size() == std::string_view("context.").size()) {
            error_ = expressionDiagnostic(eve::DiagnosticCode::NotFound, "condition references an unknown field", path_,
                                          token, "kind, resource, magnitude, tick, or context.<name>", token.text);
            return 0;
        }
        return add(std::move(node));
    }

    std::vector<Token>             tokens_;
    std::string                    path_;
    std::size_t                    position_ = 0;
    std::vector<ExpressionNode>    nodes_;
    std::optional<eve::Diagnostic> error_;
};

struct EvaluatedValue {
    ExpressionType type    = ExpressionType::Boolean;
    bool           boolean = false;
    double         number  = 0.0;
    std::string    string;
};

eve::Result<EvaluatedValue> evaluationFailure(eve::DiagnosticCode code, std::string message, std::string path) {
    return eve::Result<EvaluatedValue>::failure(
        eve::Diagnostic::error(code, std::move(message), std::move(path), {}, "settlement.condition"));
}

const eve::Value* findContextValue(const eve::Value& root, std::string_view path) {
    const eve::Value* current = &root;
    while (!path.empty()) {
        const auto split = path.find('.');
        const auto part  = path.substr(0, split);
        if (part.empty() || !current->isObject()) return nullptr;
        current = current->find(std::string(part));
        if (!current) return nullptr;
        if (split == std::string_view::npos) break;
        path.remove_prefix(split + 1);
    }
    return current;
}

eve::Result<EvaluatedValue> evaluateNode(const ExpressionProgram& program, std::uint16_t index,
                                         const SettlementRequest& request, std::size_t& steps) {
    if (++steps > kMaximumEvaluationSteps)
        return evaluationFailure(eve::DiagnosticCode::PreconditionViolation,
                                 "condition exceeded the evaluation step limit", "when");
    const auto& node = program.nodes[index];
    if (node.kind == ExpressionKind::Literal) {
        EvaluatedValue result;
        result.type = node.type;
        if (const auto* value = node.literal.getIf<bool>())
            result.boolean = *value;
        else if (const auto* value = node.literal.getIf<double>())
            result.number = *value;
        else if (const auto* value = node.literal.getIf<std::string>())
            result.string = *value;
        return eve::Result<EvaluatedValue>::success(std::move(result));
    }
    if (node.kind == ExpressionKind::HasTag) {
        const bool     present = std::find(request.tags.begin(), request.tags.end(), node.name) != request.tags.end();
        EvaluatedValue result;
        result.type    = ExpressionType::Boolean;
        result.boolean = present;
        return eve::Result<EvaluatedValue>::success(std::move(result));
    }
    if (node.kind == ExpressionKind::Field) {
        EvaluatedValue result;
        result.type = node.type;
        if (node.name == "kind")
            result.string = request.kind;
        else if (node.name == "resource")
            result.string = request.resource;
        else if (node.name == "magnitude")
            result.number = request.magnitude;
        else if (node.name == "tick")
            result.number = static_cast<double>(request.tick.value());
        else {
            auto        contextPath = std::string_view(node.name).substr(std::string_view("context.").size());
            const auto* value       = findContextValue(request.context, contextPath);
            if (!value)
                return evaluationFailure(eve::DiagnosticCode::NotFound, "condition context field is missing",
                                         node.name);
            if (node.type == ExpressionType::Boolean) {
                const auto* typed = value->getIf<bool>();
                if (!typed)
                    return evaluationFailure(eve::DiagnosticCode::TypeMismatch,
                                             "condition context field is not Boolean", node.name);
                result.boolean = *typed;
            } else if (node.type == ExpressionType::String) {
                const auto* typed = value->getIf<std::string>();
                if (!typed)
                    return evaluationFailure(eve::DiagnosticCode::TypeMismatch, "condition context field is not String",
                                             node.name);
                result.string = *typed;
            } else if (const auto* integer = value->getIf<std::int64_t>()) {
                result.number = static_cast<double>(*integer);
            } else if (const auto* real = value->getIf<double>(); real && std::isfinite(*real)) {
                result.number = *real;
            } else {
                return evaluationFailure(eve::DiagnosticCode::TypeMismatch,
                                         "condition context field is not a finite Number", node.name);
            }
        }
        return eve::Result<EvaluatedValue>::success(std::move(result));
    }
    auto leftResult = evaluateNode(program, node.left, request, steps);
    if (!leftResult) return leftResult;
    auto left = std::move(leftResult).takeValue();
    if (node.kind == ExpressionKind::Not) {
        left.boolean = !left.boolean;
        return eve::Result<EvaluatedValue>::success(std::move(left));
    }
    if (node.kind == ExpressionKind::Negate) {
        left.number = -left.number;
        return eve::Result<EvaluatedValue>::success(std::move(left));
    }
    if (node.kind == ExpressionKind::Absolute) {
        left.number = std::abs(left.number);
        return eve::Result<EvaluatedValue>::success(std::move(left));
    }
    if (node.kind == ExpressionKind::And && !left.boolean) return eve::Result<EvaluatedValue>::success(std::move(left));
    if (node.kind == ExpressionKind::Or && left.boolean) return eve::Result<EvaluatedValue>::success(std::move(left));
    auto rightResult = evaluateNode(program, node.right, request, steps);
    if (!rightResult) return rightResult;
    auto           right = std::move(rightResult).takeValue();
    EvaluatedValue result;
    switch (node.kind) {
        case ExpressionKind::Add:
            result.type   = ExpressionType::Number;
            result.number = left.number + right.number;
            break;
        case ExpressionKind::Subtract:
            result.type   = ExpressionType::Number;
            result.number = left.number - right.number;
            break;
        case ExpressionKind::Multiply:
            result.type   = ExpressionType::Number;
            result.number = left.number * right.number;
            break;
        case ExpressionKind::Divide:
            if (right.number == 0.0)
                return evaluationFailure(eve::DiagnosticCode::InvalidArgument, "condition attempted division by zero",
                                         "when");
            result.type   = ExpressionType::Number;
            result.number = left.number / right.number;
            break;
        case ExpressionKind::Minimum:
            result.type   = ExpressionType::Number;
            result.number = std::min(left.number, right.number);
            break;
        case ExpressionKind::Maximum:
            result.type   = ExpressionType::Number;
            result.number = std::max(left.number, right.number);
            break;
        case ExpressionKind::Clamp: {
            auto thirdResult = evaluateNode(program, node.third, request, steps);
            if (!thirdResult) return thirdResult;
            const auto third = std::move(thirdResult).takeValue();
            if (right.number > third.number)
                return evaluationFailure(eve::DiagnosticCode::InvalidArgument,
                                         "condition clamp minimum exceeds maximum", "when");
            result.type   = ExpressionType::Number;
            result.number = std::clamp(left.number, right.number, third.number);
            break;
        }
        case ExpressionKind::Equal:
        case ExpressionKind::NotEqual: {
            result.type = ExpressionType::Boolean;
            bool equal  = false;
            if (left.type == ExpressionType::Boolean)
                equal = left.boolean == right.boolean;
            else if (left.type == ExpressionType::Number)
                equal = left.number == right.number;
            else
                equal = left.string == right.string;
            result.boolean = node.kind == ExpressionKind::Equal ? equal : !equal;
            break;
        }
        case ExpressionKind::Less:
            result.type    = ExpressionType::Boolean;
            result.boolean = left.number < right.number;
            break;
        case ExpressionKind::LessEqual:
            result.type    = ExpressionType::Boolean;
            result.boolean = left.number <= right.number;
            break;
        case ExpressionKind::Greater:
            result.type    = ExpressionType::Boolean;
            result.boolean = left.number > right.number;
            break;
        case ExpressionKind::GreaterEqual:
            result.type    = ExpressionType::Boolean;
            result.boolean = left.number >= right.number;
            break;
        case ExpressionKind::And:
            result.type    = ExpressionType::Boolean;
            result.boolean = left.boolean && right.boolean;
            break;
        case ExpressionKind::Or:
            result.type    = ExpressionType::Boolean;
            result.boolean = left.boolean || right.boolean;
            break;
        default:
            return evaluationFailure(eve::DiagnosticCode::InvariantViolation,
                                     "condition contains an invalid instruction", "when");
    }
    if (result.type == ExpressionType::Number && !std::isfinite(result.number))
        return evaluationFailure(eve::DiagnosticCode::InvalidArgument, "condition produced a non-finite number",
                                 "when");
    return eve::Result<EvaluatedValue>::success(std::move(result));
}

std::string digestCondition(std::string_view source) {
    std::uint64_t              hash    = 1469598103934665603ull;
    constexpr std::string_view version = "settlement-condition-v1\n";
    for (const char value : version) {
        hash ^= static_cast<unsigned char>(value);
        hash *= 1099511628211ull;
    }
    for (const char value : source) {
        hash ^= static_cast<unsigned char>(value);
        hash *= 1099511628211ull;
    }
    std::ostringstream stream;
    stream << std::hex << std::setfill('0') << std::setw(16) << hash;
    return stream.str();
}

struct CompiledExpression {
    std::shared_ptr<const ExpressionProgram> program;
    std::string                              digest;
};

eve::Result<CompiledExpression> compileExpression(std::string_view source, const std::string& path) {
    if (source.size() > kMaximumConditionSource) {
        Token token;
        return eve::Result<CompiledExpression>::failure(
            expressionDiagnostic(eve::DiagnosticCode::InvalidArgument, "condition exceeds the source length limit",
                                 path, token, std::to_string(kMaximumConditionSource), std::to_string(source.size())));
    }
    auto tokenResult = tokenize(source, path);
    if (!tokenResult) return eve::Result<CompiledExpression>::failure(tokenResult.status());
    ExpressionParser parser(std::move(tokenResult).takeValue(), path);
    auto             programResult = parser.parse();
    if (!programResult) return eve::Result<CompiledExpression>::failure(programResult.status());
    CompiledExpression compiled;
    compiled.program = std::make_shared<ExpressionProgram>(std::move(programResult).takeValue());
    compiled.digest  = digestCondition(source);
    return eve::Result<CompiledExpression>::success(std::move(compiled));
}

eve::Result<void> invalid(std::string message, std::string path) {
    return eve::Result<void>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, std::move(message), std::move(path)));
}

bool contains(const std::vector<std::string>& values, const std::string& value) {
    return std::find(values.begin(), values.end(), value) != values.end();
}

bool matches(const RuleFilter& filter, const SettlementRequest& request) {
    if (!filter.kinds.empty() && !contains(filter.kinds, request.kind)) return false;
    for (const auto& tag : filter.requiredTags)
        if (!contains(request.tags, tag)) return false;
    for (const auto& tag : filter.excludedTags)
        if (contains(request.tags, tag)) return false;
    return true;
}

eve::Result<void> applyRule(const SettlementRule&                                             rule,
                            const std::function<eve::Result<bool>(const SettlementRequest&)>& condition,
                            const std::string& conditionDigest, SettlementContext& context) {
    if (!matches(rule.filter, context.request()))
        return eve::Result<void>::success(eve::Status::success(eve::StatusCode::NoOp));
    if (condition) {
        auto accepted = condition(context.request());
        if (!accepted) return eve::Result<void>::failure(accepted.status());
        if (!accepted.value()) return eve::Result<void>::success(eve::Status::success(eve::StatusCode::NoOp));
    }
    const double operand = rule.value + rule.valuePerExtraStack * static_cast<double>(rule.stacks - 1);
    const double before  = context.magnitude();
    double       removed = 0.0;
    auto         outcome = [&]() -> eve::Result<void> {
        switch (rule.operation) {
            case RuleOperation::Add: return context.setMagnitude(std::max(0.0, before + operand));
            case RuleOperation::Multiply: return context.setMagnitude(before * operand);
            case RuleOperation::ResistFlat:
                removed = std::min(before, operand);
                if (auto changed = context.setMagnitude(before - removed); !changed) return changed;
                return context.addResisted(removed);
            case RuleOperation::ResistPercent:
                removed = before * operand;
                if (auto changed = context.setMagnitude(before - removed); !changed) return changed;
                return context.addResisted(removed);
            case RuleOperation::AbsorbFlat:
                removed = std::min(before, operand);
                if (auto changed = context.setMagnitude(before - removed); !changed) return changed;
                return context.addAbsorbed(removed);
            case RuleOperation::ClampMaximum: return context.setClampMax(operand);
            case RuleOperation::Critical: context.setCritical(operand != 0.0); return eve::Result<void>::success();
            case RuleOperation::Immune: return context.setDisposition(SettlementDisposition::Immune);
            case RuleOperation::Lifesteal:
            case RuleOperation::Reflect: {
                if (!context.request().source.isValid() || context.projectedResult().applied == 0.0)
                    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::NoOp));
                const double magnitude = context.projectedResult().applied * operand;
                if (!std::isfinite(magnitude))
                    return eve::Result<void>::failure(eve::Diagnostic::error(
                        eve::DiagnosticCode::InvariantViolation, "derived settlement magnitude overflowed",
                        "operation"));
                SettlementRequest derived;
                derived.source      = rule.operation == RuleOperation::Reflect ? context.request().target
                                                                               : context.request().source;
                derived.target      = context.request().source;
                derived.kind        = rule.operation == RuleOperation::Reflect ? "damage" : "heal";
                derived.resource    = context.request().resource;
                derived.magnitude   = magnitude;
                derived.tick        = context.request().tick;
                derived.correlation = context.request().correlation;
                derived.trigger     = "rule:" + rule.id;
                derived.tags.push_back(rule.operation == RuleOperation::Reflect ? "trigger:reflect"
                                                                               : "trigger:lifesteal");
                return context.emitDerived(std::move(derived));
            }
        }
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvariantViolation,
                                                                 "unknown settlement rule operation", "operation"));
    }();
    if (!outcome) return outcome;
    if (context.request().trace == SettlementTraceLevel::Full) {
        context.setStageDetail("rule", eve::Value(rule.id));
        context.setStageDetail("source", eve::Value(rule.source));
        context.setStageDetail("stacks", eve::Value(static_cast<std::int64_t>(rule.stacks)));
        context.setStageDetail("operand", eve::Value(operand));
        if (!conditionDigest.empty()) context.setStageDetail("condition_digest", eve::Value(conditionDigest));
    }
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

}  // namespace

eve::Result<void> SettlementRuleSet::configure(std::vector<SettlementRule> rules) {
    std::set<std::string>          ids;
    std::vector<ConditionFunction> conditions;
    std::vector<std::string>       conditionDigests;
    conditions.reserve(rules.size());
    conditionDigests.reserve(rules.size());
    for (std::size_t index = 0; index < rules.size(); ++index) {
        const auto& rule = rules[index];
        const auto  path = "rules[" + std::to_string(index) + "]";
        if (rule.id.empty()) return invalid("settlement rule id must not be empty", path + ".id");
        if (!ids.insert(rule.id).second) return invalid("settlement rule ids must be unique", path + ".id");
        if (rule.stacks == 0) return invalid("settlement rule stack count must be positive", path + ".stacks");
        if (!std::isfinite(rule.value) || !std::isfinite(rule.valuePerExtraStack))
            return invalid("settlement rule operands must be finite", path + ".value");
        if (rule.stage == StageKind::Validate || rule.stage == StageKind::Apply || rule.stage == StageKind::Event)
            return invalid("settlement rule cannot mutate a terminal or validation phase", path + ".stage");
        if (rule.stage == StageKind::Decision && rule.operation != RuleOperation::Immune)
            return invalid("decision-stage rules currently support only immune outcomes", path + ".operation");
        if (rule.operation == RuleOperation::Immune && rule.stage != StageKind::Decision)
            return invalid("immune rules must run in the decision stage", path + ".stage");
        const bool triggerOperation =
            rule.operation == RuleOperation::Lifesteal || rule.operation == RuleOperation::Reflect;
        if (rule.stage == StageKind::Trigger && !triggerOperation)
            return invalid("trigger-stage rules require a derived-request operation", path + ".operation");
        if (triggerOperation && rule.stage != StageKind::Trigger)
            return invalid("derived-request rules must run in the trigger stage", path + ".stage");
        if ((rule.operation == RuleOperation::Multiply || rule.operation == RuleOperation::ResistFlat ||
             rule.operation == RuleOperation::AbsorbFlat || rule.operation == RuleOperation::ClampMaximum ||
             triggerOperation) &&
            (rule.value < 0.0 || rule.valuePerExtraStack < 0.0))
            return invalid("settlement rule operation requires non-negative operands", path + ".value");
        if (rule.operation == RuleOperation::ResistPercent &&
            (rule.value < 0.0 || rule.value > 1.0 || rule.valuePerExtraStack < 0.0 ||
             rule.value + rule.valuePerExtraStack * static_cast<double>(rule.stacks - 1) > 1.0))
            return invalid("settlement resistance percentage must remain in [0,1]", path + ".value");
        if (rule.when.empty()) {
            conditions.emplace_back();
            conditionDigests.emplace_back();
        } else {
            auto compiled = compileExpression(rule.when, path + ".when");
            if (!compiled) return eve::Result<void>::failure(compiled.status());
            auto expression = std::move(compiled).takeValue();
            auto program    = std::move(expression.program);
            conditions.emplace_back([program = std::move(program)](const SettlementRequest& request) {
                std::size_t steps     = 0;
                auto        evaluated = evaluateNode(*program, program->root, request, steps);
                if (!evaluated) return eve::Result<bool>::failure(evaluated.status());
                return eve::Result<bool>::success(std::move(evaluated).takeValue().boolean);
            });
            conditionDigests.push_back(std::move(expression.digest));
        }
    }
    rules_            = std::move(rules);
    conditions_       = std::move(conditions);
    conditionDigests_ = std::move(conditionDigests);
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> SettlementRuleSet::install(SettlementPipeline& pipeline) const {
    struct InstalledRule {
        SettlementRule                                             rule;
        std::function<eve::Result<bool>(const SettlementRequest&)> condition;
        std::string                                                digest;
    };
    std::vector<InstalledRule> rules;
    rules.reserve(rules_.size());
    for (std::size_t index = 0; index < rules_.size(); ++index)
        rules.push_back(InstalledRule{rules_[index], conditions_[index], conditionDigests_[index]});
    std::sort(rules.begin(), rules.end(), [](const auto& left, const auto& right) {
        const auto leftStage  = static_cast<std::uint8_t>(left.rule.stage);
        const auto rightStage = static_cast<std::uint8_t>(right.rule.stage);
        if (leftStage != rightStage) return leftStage < rightStage;
        if (left.rule.priority != right.rule.priority) return left.rule.priority < right.rule.priority;
        return left.rule.id < right.rule.id;
    });
    for (auto& installed : rules) {
        const auto stage = installed.rule.stage;
        // Canonical policy stages use simple names (for example
        // source_modifiers). The prefix deliberately sorts rules after the
        // domain policy while Clamp's terminal boundary still sorts last.
        const auto name     = std::string("zz_rule.") + installed.rule.id;
        const auto priority = installed.rule.priority;
        auto added = pipeline.addStage(stage, name, priority,
                                       [rule = std::move(installed.rule), condition = std::move(installed.condition),
                                        digest = std::move(installed.digest)](SettlementContext& context) {
                                           return applyRule(rule, condition, digest, context);
                                       });
        if (!added) return added;
    }
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

}  // namespace eve::settlement
