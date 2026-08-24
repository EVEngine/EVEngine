#include "dialogue/ConversationText.h"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <sstream>

namespace eve::dialogue {
namespace {
std::string scalarText(const StateValue* value) {
    if (!value || value->isNull()) return {};
    if (value->isString()) return value->asString();
    if (value->isBool()) return value->asBool() ? "true" : "false";
    if (value->isInt()) return std::to_string(value->asInt());
    if (value->isFloat()) {
        std::ostringstream output;
        output << std::setprecision(12) << value->asDouble();
        return output.str();
    }
    return {};
}
std::string transform(std::string value, const std::string& operation) {
    if (operation == "upper")
        std::transform(value.begin(), value.end(), value.begin(),
                       [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    else if (operation == "lower")
        std::transform(value.begin(), value.end(), value.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    else if (operation == "capitalize" && !value.empty())
        value[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(value[0])));
    return value;
}
std::string renderToken(std::string token, const StateValue& bindings, const StateValue& locals) {
    std::string operation;
    if (const size_t separator = token.find('|'); separator != std::string::npos) {
        operation = token.substr(separator + 1);
        token.resize(separator);
    }
    std::string fallback;
    if (const size_t separator = token.find("??"); separator != std::string::npos) {
        fallback = token.substr(separator + 2);
        token.resize(separator);
    }
    const StateValue* value = locals.get(token);
    if (!value) value = bindings.get(token);
    std::string rendered = scalarText(value);
    if (rendered.empty()) rendered = std::move(fallback);
    return transform(std::move(rendered), operation);
}
void replaceAll(std::string& text, const std::string& find, const std::string& replacement) {
    if (find.empty()) return;
    for (size_t position = 0; (position = text.find(find, position)) != std::string::npos;
         position += replacement.size())
        text.replace(position, find.size(), replacement);
}
}  // namespace

std::string ConversationTextRenderer::render(const std::string& text, const StateValue& bindings,
                                             const StateValue& locals, const Evaluator& evaluate) const {
    std::string output;
    output.reserve(text.size());
    for (size_t cursor = 0; cursor < text.size();) {
        if (text[cursor] != '{') {
            output.push_back(text[cursor++]);
            continue;
        }
        const size_t close = text.find('}', cursor + 1);
        if (close == std::string::npos) {
            output.append(text, cursor, std::string::npos);
            break;
        }
        const std::string value = renderToken(text.substr(cursor + 1, close - cursor - 1), bindings, locals);
        if (value.empty())
            output.append(text, cursor, close - cursor + 1);
        else
            output += value;
        cursor = close + 1;
    }
    for (const auto& rule : rules_) {
        if (!rule.expression.empty() && (!evaluate || !evaluate(rule.expression))) continue;
        replaceAll(output, rule.find, rule.replacement);
        output = rule.prefix + output + rule.suffix;
    }
    return output;
}

void ConversationTextRenderer::addToneRule(std::string expression, std::string prefix, std::string suffix,
                                           std::string find, std::string replacement) {
    rules_.push_back(
        {std::move(expression), std::move(prefix), std::move(suffix), std::move(find), std::move(replacement)});
}
}  // namespace eve::dialogue
