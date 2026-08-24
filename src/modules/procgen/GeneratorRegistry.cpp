#include "procgen/GeneratorRegistry.h"

#include <algorithm>
#include <iomanip>
#include <sstream>

namespace eve::procgen {
namespace {

std::string floatString(float value) {
    std::ostringstream stream;
    stream << std::setprecision(9) << value;
    return stream.str();
}

}  // namespace

ParamDescriptor ParamDescriptor::integer(std::string key, std::string label, int defaultValue,
                                         int minimum, int maximum, int step) {
    ParamDescriptor result;
    result.key = std::move(key);
    result.displayName = std::move(label);
    result.kind = ParamKind::Integer;
    result.defaultValue = std::to_string(defaultValue);
    result.hasMinimum = result.hasMaximum = true;
    result.minimum = minimum;
    result.maximum = maximum;
    result.step = step;
    return result;
}

ParamDescriptor ParamDescriptor::floating(std::string key, std::string label, float defaultValue,
                                          float minimum, float maximum, float step) {
    ParamDescriptor result;
    result.key = std::move(key);
    result.displayName = std::move(label);
    result.kind = ParamKind::Float;
    result.defaultValue = floatString(defaultValue);
    result.hasMinimum = result.hasMaximum = true;
    result.minimum = minimum;
    result.maximum = maximum;
    result.step = step;
    return result;
}

ParamDescriptor ParamDescriptor::boolean(std::string key, std::string label, bool defaultValue) {
    ParamDescriptor result;
    result.key = std::move(key);
    result.displayName = std::move(label);
    result.kind = ParamKind::Boolean;
    result.defaultValue = defaultValue ? "1" : "0";
    return result;
}

ParamDescriptor ParamDescriptor::text(std::string key, std::string label, std::string defaultValue) {
    ParamDescriptor result;
    result.key = std::move(key);
    result.displayName = std::move(label);
    result.kind = ParamKind::String;
    result.defaultValue = std::move(defaultValue);
    return result;
}

ParamDescriptor ParamDescriptor::choice(std::string key, std::string label, std::string defaultValue,
                                        std::vector<std::string> choices) {
    ParamDescriptor result = text(std::move(key), std::move(label), std::move(defaultValue));
    result.kind = ParamKind::Choice;
    result.choices = std::move(choices);
    return result;
}

GeneratorDescriptor GeneratorDescriptor::grid(std::string id, std::string displayName,
                                              std::string category, int minimumWidth,
                                              int minimumHeight, int maximumWidth,
                                              int maximumHeight) {
    GeneratorDescriptor result;
    result.id = std::move(id);
    result.displayName = std::move(displayName);
    result.category = std::move(category);
    result.params.push_back(ParamDescriptor::integer("seed", "Seed", 1, 0, 2147483647, 1));
    result.params.push_back(ParamDescriptor::integer("width", "Width", 32, minimumWidth, maximumWidth, 1));
    result.params.push_back(ParamDescriptor::integer("height", "Height", 32, minimumHeight, maximumHeight, 1));
    return result;
}

const ParamDescriptor *GeneratorDescriptor::find(const std::string &key) const {
    const auto it = std::find_if(params.begin(), params.end(),
                                 [&](const ParamDescriptor &param) { return param.key == key; });
    return it == params.end() ? nullptr : &*it;
}

GeneratorRegistry &GeneratorRegistry::instance() {
    static GeneratorRegistry reg;
    return reg;
}

void GeneratorRegistry::registerAlgorithm(const std::string &id, GeneratorFn fn) {
    GeneratorDescriptor descriptor;
    descriptor.id = id;
    descriptor.displayName = id;
    registerAlgorithm(std::move(descriptor), std::move(fn));
}

void GeneratorRegistry::registerAlgorithm(GeneratorDescriptor descriptor, GeneratorFn fn) {
    const std::string id = descriptor.id;
    algorithms_[id] = Entry{std::move(fn), std::move(descriptor)};
}

bool GeneratorRegistry::has(const std::string &id) const {
    return algorithms_.find(id) != algorithms_.end();
}

bool GeneratorRegistry::generate(const std::string &id, const Params &params, Grid2D &out,
                                 std::string &error) const {
    auto it = algorithms_.find(id);
    if (it == algorithms_.end()) {
        error = "unknown algorithm: " + id;
        return false;
    }
    return it->second.generate(params, out, error);
}

const GeneratorDescriptor *GeneratorRegistry::descriptor(const std::string &id) const {
    const auto it = algorithms_.find(id);
    return it == algorithms_.end() ? nullptr : &it->second.descriptor;
}

bool GeneratorRegistry::applyDefaults(const std::string &id, Params &params) const {
    const GeneratorDescriptor *schema = descriptor(id);
    if (!schema) return false;
    for (const ParamDescriptor &param : schema->params) {
        if (params.has(param.key)) continue;
        switch (param.kind) {
            case ParamKind::Integer:
            case ParamKind::Boolean: params.setInt(param.key, std::stoi(param.defaultValue)); break;
            case ParamKind::Float: params.setFloat(param.key, std::stof(param.defaultValue)); break;
            case ParamKind::String:
            case ParamKind::Choice: params.setString(param.key, param.defaultValue); break;
        }
    }
    return true;
}

std::vector<std::string> GeneratorRegistry::list() const {
    std::vector<std::string> ids;
    ids.reserve(algorithms_.size());
    for (const auto &kv : algorithms_) ids.push_back(kv.first);
    std::sort(ids.begin(), ids.end());
    return ids;
}

}  // namespace eve::procgen
