#include "asset/import/VegetationPreset.h"

#include <charconv>
#include <cmath>

namespace eve::asset_import {
namespace {

template <class T>
Result<T> fail(DiagnosticCode code, std::string message, std::string path = {}) {
    return Result<T>::failure(
        Diagnostic::error(code, std::move(message), std::move(path), {}, "asset.import.vegetation-preset"));
}

const Value* member(const Value::Object& object, std::string_view name) {
    const auto found = object.find(std::string(name));
    return found == object.end() ? nullptr : &found->second;
}

Result<std::vector<std::string>> strings(const Value* value, std::string_view path) {
    const auto* array = value ? value->getIf<Value::Array>() : nullptr;
    if (!array)
        return fail<std::vector<std::string>>(DiagnosticCode::ParseError, "arguments must be an array",
                                              std::string(path));
    std::vector<std::string> result;
    result.reserve(array->size());
    for (const auto& item : *array) {
        if (!item.isString())
            return fail<std::vector<std::string>>(DiagnosticCode::ParseError, "argument must be a string",
                                                  std::string(path));
        result.push_back(item.asString());
    }
    return Result<std::vector<std::string>>::success(std::move(result));
}

std::string joined(std::string_view operation, const std::vector<std::string>& arguments) {
    std::string value(operation);
    for (const auto& argument : arguments) {
        if (!value.empty()) value.push_back(' ');
        value += argument;
    }
    return value;
}

bool contains(std::string_view text, const std::vector<std::string>& arguments) {
    return text.find(joined({}, arguments)) != std::string_view::npos;
}

Result<bool> predicate(std::string_view name, const std::vector<std::string>& arguments,
                       const VegetationPresetContext& context) {
    if (name == "OUTPUT_OPTION_CONTAINS") {
        if (arguments.size() != 1)
            return fail<bool>(DiagnosticCode::ParseError, "output predicate requires one argument");
        return Result<bool>::success(context.outputOptions.contains(arguments[0]));
    }
    if (name == "SHADER_NAME_CONTAINS") return Result<bool>::success(contains(context.shaderName, arguments));
    if (name == "MATERIAL_NAME_CONTAINS") return Result<bool>::success(contains(context.materialName, arguments));
    if (name == "MATERIAL_FLOAT_EQUALS") {
        if (arguments.size() != 2)
            return fail<bool>(DiagnosticCode::ParseError, "float predicate requires property and value");
        double     value  = 0;
        const auto parsed = std::from_chars(arguments[1].data(), arguments[1].data() + arguments[1].size(), value);
        if (parsed.ec != std::errc{} || parsed.ptr != arguments[1].data() + arguments[1].size() ||
            !std::isfinite(value))
            return fail<bool>(DiagnosticCode::ParseError, "float predicate value is invalid");
        const auto found = context.materialFloats.find(arguments[0]);
        return Result<bool>::success(found != context.materialFloats.end() && found->second == value);
    }
    if (arguments.size() != 1 && name != "SHADER_PIPELINE_IS_HD" && name != "SHADER_PIPELINE_IS_STANDARD" &&
        name != "SHADER_PIPELINE_IS_UNIVERSAL")
        return fail<bool>(DiagnosticCode::ParseError, "preset predicate requires one argument", std::string(name));
    if (name == "MATERIAL_KEYWORD_ENABLED" || name == "MATERIAL_HAS_KEYWORD")
        return Result<bool>::success(context.materialKeywords.contains(arguments[0]));
    if (name == "MATERIAL_HAS_PROP") return Result<bool>::success(context.materialProperties.contains(arguments[0]));
    if (name == "MATERIAL_HAS_TEX") return Result<bool>::success(context.materialTextures.contains(arguments[0]));
    if (!arguments.empty())
        return fail<bool>(DiagnosticCode::ParseError, "pipeline predicate takes no arguments", std::string(name));
    if (name == "SHADER_PIPELINE_IS_HD") return Result<bool>::success(context.shaderPipeline == "HD");
    if (name == "SHADER_PIPELINE_IS_STANDARD") return Result<bool>::success(context.shaderPipeline == "Standard");
    if (name == "SHADER_PIPELINE_IS_UNIVERSAL") return Result<bool>::success(context.shaderPipeline == "Universal");
    return fail<bool>(DiagnosticCode::Unsupported, "unknown TVE preset predicate", std::string(name));
}

struct Evaluator {
    const std::map<std::string, Value>&  definitions;
    const VegetationPresetContext&       context;
    std::set<std::string>                active;
    std::vector<VegetationPresetCommand> commands;

    Result<void> statements(const Value::Array& values, const std::string& sourcePath) {
        for (const auto& value : values) {
            const auto* object = value.getIf<Value::Object>();
            const auto* kind   = object ? member(*object, "kind") : nullptr;
            if (!object || !kind || !kind->isString())
                return fail<void>(DiagnosticCode::ParseError, "preset statement is malformed", sourcePath);
            if (kind->asString() == "command") {
                const auto* domain    = member(*object, "domain");
                const auto* operation = member(*object, "operation");
                auto        arguments = strings(member(*object, "arguments"), sourcePath);
                if (!domain || !domain->isString() || !operation || !operation->isString() || !arguments)
                    return fail<void>(DiagnosticCode::ParseError, "preset command is malformed", sourcePath);
                if (domain->asString() == "Include") {
                    auto include = evaluate(joined(operation->asString(), arguments.value()));
                    if (!include) return include;
                } else {
                    commands.push_back(
                        {domain->asString(), operation->asString(), std::move(arguments).takeValue(), sourcePath});
                }
                continue;
            }
            if (kind->asString() != "condition")
                return fail<void>(DiagnosticCode::ParseError, "unknown preset statement kind", sourcePath);
            const auto* name      = member(*object, "predicate");
            const auto* negated   = member(*object, "negated");
            const auto* body      = member(*object, "statements");
            auto        arguments = strings(member(*object, "arguments"), sourcePath);
            if (!name || !name->isString() || !negated || !negated->isBool() || !body || !body->isArray() || !arguments)
                return fail<void>(DiagnosticCode::ParseError, "preset condition is malformed", sourcePath);
            auto result = predicate(name->asString(), arguments.value(), context);
            if (!result) return Result<void>::failure(result.status());
            if (result.value() != negated->asBool()) {
                auto nested = statements(*body->getIf<Value::Array>(), sourcePath);
                if (!nested) return nested;
            }
        }
        return Result<void>::success();
    }

    Result<void> evaluate(const std::string& name) {
        auto found = definitions.find(name);
        if (found == definitions.end() && name == "Use Default Flower Masks")
            found = definitions.find("Use Default Flowers Masks");
        if (found == definitions.end() && name == "Use Default Flower Settings")
            found = definitions.find("Use Default Flowers Settings");
        if (found == definitions.end())
            return fail<void>(DiagnosticCode::NotFound, "TVE preset include is missing", name);
        if (!active.insert(name).second) return fail<void>(DiagnosticCode::Conflict, "TVE preset include cycle", name);
        const auto* root    = found->second.getIf<Value::Object>();
        const auto* schema  = root ? member(*root, "schema") : nullptr;
        const auto* version = root ? member(*root, "schemaVersion") : nullptr;
        const auto* source  = root ? member(*root, "sourcePath") : nullptr;
        const auto* body    = root ? member(*root, "statements") : nullptr;
        if (!root || !schema || !schema->isString() || schema->asString() != "eve.vegetation-conversion-preset" ||
            !version || !version->isInt64() || version->asInt() != 1 || !source || !source->isString() || !body ||
            !body->isArray())
            return fail<void>(DiagnosticCode::ParseError, "TVE preset definition is malformed", name);
        auto result = statements(*body->getIf<Value::Array>(), source->asString());
        active.erase(name);
        return result;
    }
};

}  // namespace

Result<std::vector<VegetationPresetCommand>> evaluateVegetationPreset(const std::map<std::string, Value>& definitions,
                                                                      std::string_view                    root,
                                                                      const VegetationPresetContext&      context) {
    Evaluator evaluator{definitions, context};
    auto      result = evaluator.evaluate(std::string(root));
    if (!result) return Result<std::vector<VegetationPresetCommand>>::failure(result.status());
    return Result<std::vector<VegetationPresetCommand>>::success(std::move(evaluator.commands));
}

}  // namespace eve::asset_import
