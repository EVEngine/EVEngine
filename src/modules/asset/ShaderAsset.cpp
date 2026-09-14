#include "asset/ShaderAsset.h"

#include <cmath>
#include <limits>
#include <set>

namespace eve::asset {
namespace {
template <class T>
Result<T> failure(std::string message) {
    return Result<T>::failure(
        Diagnostic::error(DiagnosticCode::InvalidArgument, std::move(message), {}, {}, "asset.shader"));
}

Result<std::vector<std::uint32_t>> stage(const Value& value, std::uint32_t model, const ShaderAssetLimits& limits) {
    const auto* input = value.getIf<Value::Array>();
    if (!input || input->size() < 5 || input->size() > limits.maximumStageWords)
        return failure<std::vector<std::uint32_t>>("Shader stage size is invalid");
    std::vector<std::uint32_t> words;
    words.reserve(input->size());
    for (const auto& word : *input) {
        if (!word.isInt64() || word.asInt() < 0 || word.asInt() > std::numeric_limits<std::uint32_t>::max())
            return failure<std::vector<std::uint32_t>>("Shader stage requires unsigned 32-bit words");
        words.push_back(static_cast<std::uint32_t>(word.asInt()));
    }
    if (words[0] != 0x07230203 || words[1] < 0x00010000 || words[1] > 0x00010600 || (words[1] & 0xff) != 0 ||
        words[3] == 0 || words[4] != 0)
        return failure<std::vector<std::uint32_t>>("Unsupported SPIR-V header");
    bool main = false;
    for (std::size_t cursor = 5; cursor < words.size();) {
        const auto count  = words[cursor] >> 16;
        const auto opcode = words[cursor] & 0xffff;
        if (count == 0 || count > words.size() - cursor)
            return failure<std::vector<std::uint32_t>>("Truncated SPIR-V instruction");
        if (opcode == 15) {
            if (count < 4) return failure<std::vector<std::uint32_t>>("Invalid SPIR-V entry point");
            if (count >= 5 && words[cursor + 3] == 0x6e69616d && words[cursor + 4] == 0) {
                if (main || words[cursor + 1] != model || words[cursor + 2] == 0 || words[cursor + 2] >= words[3])
                    return failure<std::vector<std::uint32_t>>("Incompatible main entry point");
                main = true;
            }
        }
        cursor += count;
    }
    if (!main) return failure<std::vector<std::uint32_t>>("SPIR-V main entry point is missing");
    return Result<std::vector<std::uint32_t>>::success(std::move(words));
}
}  // namespace

Result<ShaderAsset> decodeShaderAsset(const Value& definition, const ShaderAssetLimits& limits) {
    const auto* fields = definition.getIf<Value::Object>();
    if (!fields || fields->size() != 7)
        return failure<ShaderAsset>("Shader definition requires exactly seven known fields");
    for (const auto name : {"schema", "schemaVersion", "interface", "format", "vertex", "fragment", "parameters"})
        if (!fields->contains(name)) return failure<ShaderAsset>("Shader field is missing or unknown");
    if (!fields->at("schema").isString() || fields->at("schema").asString() != "eve.shader" ||
        !fields->at("schemaVersion").isInt64() || fields->at("schemaVersion").asInt() != 1)
        return Result<ShaderAsset>::failure(Diagnostic::error(
            DiagnosticCode::UnknownVersion, "Unsupported shader schema/version", {}, {}, "asset.shader"));
    if (!fields->at("format").isString() || fields->at("format").asString() != "spirv-1.6" ||
        !fields->at("interface").isString())
        return failure<ShaderAsset>("Unsupported shader format/interface");
    ShaderAsset result;
    const auto& interface = fields->at("interface").asString();
    if (interface == "sprite2d")
        result.interface = ShaderAssetInterface::Sprite2D;
    else if (interface != "mesh3d")
        return failure<ShaderAsset>("Unsupported shader interface");
    const auto* parameters = fields->at("parameters").getIf<Value::Array>();
    if (!parameters || parameters->size() > 32) return failure<ShaderAsset>("Invalid shader parameter count");
    std::set<std::string> names;
    std::size_t           slots = 0;
    for (const auto& parameter : *parameters) {
        const auto* object = parameter.getIf<Value::Object>();
        if (!object || object->size() != 2 || !object->contains("name") || !object->contains("default") ||
            !object->at("name").isString())
            return failure<ShaderAsset>("Invalid parameter fields");
        const auto& name     = object->at("name").asString();
        const auto* defaults = object->at("default").getIf<Value::Array>();
        if (name.empty() || name.size() > 128 || name.find('\0') != std::string::npos || !names.insert(name).second ||
            !defaults || defaults->empty() || defaults->size() > 4 || (slots += defaults->size()) > 32)
            return failure<ShaderAsset>("Invalid parameter name or push-constant capacity");
        ShaderAssetParameter output{name, {}};
        for (const auto& value : *defaults) {
            if (!value.isDouble() && !value.isInt64()) return failure<ShaderAsset>("Non-numeric default");
            const double number = value.isDouble() ? value.asDouble() : static_cast<double>(value.asInt());
            if (!std::isfinite(number) || std::abs(number) > std::numeric_limits<float>::max())
                return failure<ShaderAsset>("Non-finite float default");
            output.defaults.push_back(static_cast<float>(number));
        }
        result.parameters.push_back(std::move(output));
    }
    auto vertex = stage(fields->at("vertex"), 0, limits);
    if (!vertex) return Result<ShaderAsset>::failure(vertex.status());
    auto fragment = stage(fields->at("fragment"), 4, limits);
    if (!fragment) return Result<ShaderAsset>::failure(fragment.status());
    result.vertex   = std::move(vertex).takeValue();
    result.fragment = std::move(fragment).takeValue();
    return Result<ShaderAsset>::success(std::move(result));
}
}  // namespace eve::asset
