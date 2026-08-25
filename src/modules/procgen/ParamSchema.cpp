#include "procgen/ParamSchema.h"

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <utility>

namespace eve::procgen {
namespace {

std::string floatString(float value) {
    std::ostringstream stream;
    stream << std::setprecision(9) << value;
    return stream.str();
}

}  // namespace

ParamDescriptor ParamDescriptor::integer(std::string key, std::string label, int defaultValue, int minimum, int maximum,
                                         int step) {
    ParamDescriptor result;
    result.key          = std::move(key);
    result.displayName  = std::move(label);
    result.kind         = ParamKind::Integer;
    result.defaultValue = std::to_string(defaultValue);
    result.hasMinimum = result.hasMaximum = true;
    result.minimum                        = minimum;
    result.maximum                        = maximum;
    result.step                           = step;
    return result;
}

ParamDescriptor ParamDescriptor::floating(std::string key, std::string label, float defaultValue, float minimum,
                                          float maximum, float step) {
    ParamDescriptor result;
    result.key          = std::move(key);
    result.displayName  = std::move(label);
    result.kind         = ParamKind::Float;
    result.defaultValue = floatString(defaultValue);
    result.hasMinimum = result.hasMaximum = true;
    result.minimum                        = minimum;
    result.maximum                        = maximum;
    result.step                           = step;
    return result;
}

ParamDescriptor ParamDescriptor::boolean(std::string key, std::string label, bool defaultValue) {
    ParamDescriptor result;
    result.key          = std::move(key);
    result.displayName  = std::move(label);
    result.kind         = ParamKind::Boolean;
    result.defaultValue = defaultValue ? "1" : "0";
    return result;
}

ParamDescriptor ParamDescriptor::text(std::string key, std::string label, std::string defaultValue) {
    ParamDescriptor result;
    result.key          = std::move(key);
    result.displayName  = std::move(label);
    result.kind         = ParamKind::String;
    result.defaultValue = std::move(defaultValue);
    return result;
}

ParamDescriptor ParamDescriptor::choice(std::string key, std::string label, std::string defaultValue,
                                        std::vector<std::string> choices) {
    ParamDescriptor result = text(std::move(key), std::move(label), std::move(defaultValue));
    result.kind            = ParamKind::Choice;
    result.choices         = std::move(choices);
    return result;
}

RecipeDescriptor RecipeDescriptor::grid(std::string id, std::string displayName, std::string category, int minimumWidth,
                                        int minimumHeight, int maximumWidth, int maximumHeight) {
    RecipeDescriptor result;
    result.id          = std::move(id);
    result.displayName = std::move(displayName);
    result.category    = std::move(category);
    result.params.push_back(ParamDescriptor::integer("seed", "Seed", 1, 0, 2147483647, 1));
    result.params.push_back(ParamDescriptor::integer("width", "Width", 32, minimumWidth, maximumWidth, 1));
    result.params.push_back(ParamDescriptor::integer("height", "Height", 32, minimumHeight, maximumHeight, 1));
    return result;
}

const ParamDescriptor* RecipeDescriptor::find(const std::string& key) const {
    const auto it =
        std::find_if(params.begin(), params.end(), [&](const ParamDescriptor& param) { return param.key == key; });
    return it == params.end() ? nullptr : &*it;
}

void RecipeDescriptor::applyDefaults(Params& values) const {
    for (const ParamDescriptor& param : params) {
        if (values.has(param.key)) continue;
        switch (param.kind) {
            case ParamKind::Integer:
            case ParamKind::Boolean: values.setInt(param.key, std::stoi(param.defaultValue)); break;
            case ParamKind::Float: values.setFloat(param.key, std::stof(param.defaultValue)); break;
            case ParamKind::String:
            case ParamKind::Choice: values.setString(param.key, param.defaultValue); break;
        }
    }
}

const ParamDescriptor* RecipeDescriptor::at(int index) const {
    return index < 0 || index >= int(params.size()) ? nullptr : &params[size_t(index)];
}

std::string RecipeDescriptor::getParamKey(int index) const {
    const ParamDescriptor* param = at(index);
    return param ? param->key : std::string{};
}

std::string RecipeDescriptor::getParamLabel(int index) const {
    const ParamDescriptor* param = at(index);
    return param ? param->displayName : std::string{};
}

std::string RecipeDescriptor::getParamDescription(int index) const {
    const ParamDescriptor* param = at(index);
    return param ? param->description : std::string{};
}

std::string RecipeDescriptor::getParamCategory(int index) const {
    const ParamDescriptor* param = at(index);
    return param ? param->category : std::string{};
}

std::string RecipeDescriptor::getParamKind(int index) const {
    const ParamDescriptor* param = at(index);
    if (!param) return {};
    switch (param->kind) {
        case ParamKind::Integer: return "int";
        case ParamKind::Float: return "float";
        case ParamKind::Boolean: return "bool";
        case ParamKind::String: return "string";
        case ParamKind::Choice: return "choice";
    }
    return {};
}

std::string RecipeDescriptor::getParamDefault(int index) const {
    const ParamDescriptor* param = at(index);
    return param ? param->defaultValue : std::string{};
}

bool RecipeDescriptor::paramHasMinimum(int index) const {
    const ParamDescriptor* param = at(index);
    return param && param->hasMinimum;
}

bool RecipeDescriptor::paramHasMaximum(int index) const {
    const ParamDescriptor* param = at(index);
    return param && param->hasMaximum;
}

float RecipeDescriptor::getParamMinimum(int index) const {
    const ParamDescriptor* param = at(index);
    return param ? float(param->minimum) : 0.f;
}

float RecipeDescriptor::getParamMaximum(int index) const {
    const ParamDescriptor* param = at(index);
    return param ? float(param->maximum) : 0.f;
}

float RecipeDescriptor::getParamStep(int index) const {
    const ParamDescriptor* param = at(index);
    return param ? float(param->step) : 0.f;
}

bool RecipeDescriptor::isParamAdvanced(int index) const {
    const ParamDescriptor* param = at(index);
    return param && param->advanced;
}

int RecipeDescriptor::getParamChoiceCount(int index) const {
    const ParamDescriptor* param = at(index);
    return param ? int(param->choices.size()) : 0;
}

std::string RecipeDescriptor::getParamChoice(int paramIndex, int choiceIndex) const {
    const ParamDescriptor* param = at(paramIndex);
    if (!param || choiceIndex < 0 || choiceIndex >= int(param->choices.size())) return {};
    return param->choices[size_t(choiceIndex)];
}

}  // namespace eve::procgen
