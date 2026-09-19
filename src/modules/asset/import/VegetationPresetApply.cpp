#include "asset/import/VegetationPreset.h"

#include <cerrno>
#include <cmath>
#include <cstdlib>

namespace eve::asset_import {
namespace {

Result<double> number(std::string_view text, const VegetationPresetCommand& command) {
    const std::string token(text);
    char*             end = nullptr;
    errno                 = 0;
    const double value    = std::strtod(token.c_str(), &end);
    if (errno != 0 || end != token.c_str() + token.size() || !std::isfinite(value))
        return Result<double>::failure(Diagnostic::error(DiagnosticCode::ParseError, "preset numeric argument is invalid", command.sourcePath, {},
                                                "asset.import.vegetation-preset.apply"));
    return Result<double>::success(value);
}

Result<std::array<double, 4>> vector(const VegetationPresetCommand& command) {
    if (command.arguments.size() != 5)
        return Result<std::array<double, 4>>::failure(Diagnostic::error(DiagnosticCode::ParseError, "preset vector requires property and four values", command.sourcePath, {},
                                                "asset.import.vegetation-preset.apply"));
    std::array<double, 4> result{};
    for (std::size_t i = 0; i < 4; ++i) {
        auto value = number(command.arguments[i + 1], command);
        if (!value) return Result<std::array<double, 4>>::failure(value.status());
        result[i] = value.value();
    }
    return Result<std::array<double, 4>>::success(result);
}

std::string join(const std::vector<std::string>& values) {
    std::string result;
    for (const auto& value : values) {
        if (!result.empty()) result.push_back(' ');
        result += value;
    }
    return result;
}

bool metadata(std::string_view domain) {
    return domain == "InfoTitle" || domain == "InfoPreset" || domain == "InfoStatus" || domain == "InfoOnline" ||
           domain == "InfoWarning" || domain == "InfoMessage" || domain == "Updated" || domain == "Detect" ||
           domain == "Preset";
}

bool meshOperation(std::string_view operation) {
    static const std::set<std::string_view> values{"SetVariation", "SetOcclusion", "SetDetailMask", "SetDetailCoord",
                                                   "SetHeight",    "SetMotion2",   "SetMotion3",    "SetPivots",
                                                   "SetNormals",   "SetBounds",    "SetReadWrite"};
    return values.contains(operation);
}

bool outputDomain(std::string_view domain) {
    static const std::set<std::string_view> values{
        "OutputOptions", "OutputTransforms", "OutputMeshes", "OutputMaterials", "OutputTextures", "OutputBase",
        "OutputData",    "OutputCollection", "OutputGUIDs",  "OutputPipelines", "CollectBase",    "CollectData"};
    return values.contains(domain);
}

Result<void> applyMaterial(VegetationConversionCandidate& out, const VegetationPresetCommand& command) {
    const auto& a = command.arguments;
    if (command.operation == "SET_FLOAT") {
        if (a.size() != 2) return Result<void>::failure(Diagnostic::error(DiagnosticCode::ParseError, "SET_FLOAT requires property and value", command.sourcePath, {},
                                                "asset.import.vegetation-preset.apply"));
        auto value = number(a[1], command);
        if (!value) return Result<void>::failure(value.status());
        out.materialFloats[a[0]] = value.value();
    } else if (command.operation == "SET_VECTOR" || command.operation == "SET_COLOR") {
        auto value = vector(command);
        if (!value) return Result<void>::failure(value.status());
        if (command.operation == "SET_COLOR")
            out.materialColors[a[0]] = value.value();
        else
            out.materialVectors[a[0]] = value.value();
    } else if (command.operation == "COPY_FLOAT" || command.operation == "COPY_VECTOR" ||
               command.operation == "COPY_COLOR" || command.operation == "COPY_TEX") {
        const bool ignoredLegacyFloatArgument = command.operation == "COPY_FLOAT" && a.size() == 3;
        if (a.size() != 2 && !ignoredLegacyFloatArgument)
            return Result<void>::failure(Diagnostic::error(DiagnosticCode::ParseError, "COPY operation requires source and destination", command.sourcePath, {},
                                                "asset.import.vegetation-preset.apply"));
        if (command.operation == "COPY_FLOAT") {
            if (auto v = out.materialFloats.find(a[0]); v != out.materialFloats.end())
                out.materialFloats[a[1]] = v->second;
        } else if (command.operation == "COPY_VECTOR") {
            if (auto v = out.materialVectors.find(a[0]); v != out.materialVectors.end())
                out.materialVectors[a[1]] = v->second;
            else if (auto v = out.materialColors.find(a[0]); v != out.materialColors.end())
                out.materialVectors[a[1]] = v->second;
        } else if (command.operation == "COPY_COLOR") {
            if (auto v = out.materialColors.find(a[0]); v != out.materialColors.end())
                out.materialColors[a[1]] = v->second;
            else if (auto v = out.materialVectors.find(a[0]); v != out.materialVectors.end())
                out.materialColors[a[1]] = v->second;
        } else if (auto v = out.materialTextures.find(a[0]); v != out.materialTextures.end()) {
            if (out.firstValidTextureDestinations.contains(a[1]))
                out.materialTextures.try_emplace(a[1], v->second);
            else
                out.materialTextures[a[1]] = v->second;
        }
    } else if (command.operation == "COPY_ST_AS_VECTOR") {
        if (a.size() != 2) return Result<void>::failure(Diagnostic::error(DiagnosticCode::ParseError, "COPY_ST_AS_VECTOR requires source and destination", command.sourcePath, {},
                                                "asset.import.vegetation-preset.apply"));
        if (auto v = out.materialTextureTransforms.find(a[0]); v != out.materialTextureTransforms.end())
            out.materialVectors[a[1]] = v->second;
    } else if (command.operation.starts_with("COPY_FLOAT_AS_VECTOR_")) {
        if (a.size() != 2) return Result<void>::failure(Diagnostic::error(DiagnosticCode::ParseError, "float-to-vector copy requires source and destination", command.sourcePath, {},
                                                "asset.import.vegetation-preset.apply"));
        static constexpr std::string_view axes = "XYZW";
        const auto                        axis = axes.find(command.operation.back());
        if (axis == std::string_view::npos) return Result<void>::failure(Diagnostic::error(DiagnosticCode::ParseError, "float-to-vector axis is invalid", command.sourcePath, {},
                                                "asset.import.vegetation-preset.apply"));
        if (auto v = out.materialFloats.find(a[0]); v != out.materialFloats.end())
            out.materialVectors[a[1]][axis] = v->second;
    } else if (command.operation.starts_with("COPY_VECTOR_") && command.operation.ends_with("_AS_FLOAT")) {
        if (a.size() != 2) return Result<void>::failure(Diagnostic::error(DiagnosticCode::ParseError, "vector-to-float copy requires source and destination", command.sourcePath, {},
                                                "asset.import.vegetation-preset.apply"));
        static constexpr std::string_view axes = "XYZW";
        const auto                        axis = axes.find(command.operation[12]);
        if (axis == std::string_view::npos) return Result<void>::failure(Diagnostic::error(DiagnosticCode::ParseError, "vector-to-float axis is invalid", command.sourcePath, {},
                                                "asset.import.vegetation-preset.apply"));
        if (auto v = out.materialVectors.find(a[0]); v != out.materialVectors.end())
            out.materialFloats[a[1]] = v->second[axis];
    } else if (command.operation == "SET_SHADER") {
        if (a.size() != 1) return Result<void>::failure(Diagnostic::error(DiagnosticCode::ParseError, "SET_SHADER requires one alias", command.sourcePath, {},
                                                "asset.import.vegetation-preset.apply"));
        const auto found   = out.shaderAliases.find(a[0]);
        out.materialShader = found == out.shaderAliases.end() ? a[0] : found->second;
    } else if (command.operation == "SET_SHADER_BY_NAME") {
        if (a.empty()) return Result<void>::failure(Diagnostic::error(DiagnosticCode::ParseError, "SET_SHADER_BY_NAME requires a name", command.sourcePath, {},
                                                "asset.import.vegetation-preset.apply"));
        out.materialShader = join(a);
    } else if (command.operation == "SET_SHADER_BY_LIGHTING") {
        if (a.size() != 1) return Result<void>::failure(Diagnostic::error(DiagnosticCode::ParseError, "SET_SHADER_BY_LIGHTING requires one mode", command.sourcePath, {},
                                                "asset.import.vegetation-preset.apply"));
        out.materialLighting = a[0];
    } else if (command.operation == "ENABLE_KEYWORD" || command.operation == "LOCK_PROP") {
        if (a.size() != 1) return Result<void>::failure(Diagnostic::error(DiagnosticCode::ParseError, "keyword/property command requires one name", command.sourcePath, {},
                                                "asset.import.vegetation-preset.apply"));
        (command.operation == "ENABLE_KEYWORD" ? out.materialKeywords : out.lockedMaterialProperties).insert(a[0]);
    } else if (command.operation == "ENABLE_INSTANCING" || command.operation == "DISABLE_INSTANCING") {
        if (!a.empty()) return Result<void>::failure(Diagnostic::error(DiagnosticCode::ParseError, "instancing command takes no arguments", command.sourcePath, {},
                                                "asset.import.vegetation-preset.apply"));
        out.materialInstancing = command.operation == "ENABLE_INSTANCING";
    } else if (command.operation == "COPY_TEX_FIRST_VALID") {
        if (a.size() != 1) return Result<void>::failure(Diagnostic::error(DiagnosticCode::ParseError, "COPY_TEX_FIRST_VALID requires a destination", command.sourcePath, {},
                                                "asset.import.vegetation-preset.apply"));
        out.firstValidTextureDestinations.insert(a[0]);
    } else {
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::ParseError, "unknown Material preset operation: " + command.operation, command.sourcePath, {},
                                                "asset.import.vegetation-preset.apply"));
    }
    return Result<void>::success();
}
}  // namespace

Result<VegetationConversionCandidate> applyVegetationPreset(VegetationConversionCandidate               out,
                                                            const std::vector<VegetationPresetCommand>& commands) {
    for (const auto& command : commands) {
        if (command.domain == "Material") {
            auto result = applyMaterial(out, command);
            if (!result) return Result<VegetationConversionCandidate>::failure(result.status());
        } else if (command.domain == "Mesh") {
            if (!meshOperation(command.operation) || command.arguments.empty())
                return Result<VegetationConversionCandidate>::failure(Diagnostic::error(DiagnosticCode::ParseError, "Mesh rule is malformed", command.sourcePath, {},
                                                "asset.import.vegetation-preset.apply"));
            out.meshRules[command.operation] = command.arguments;
        } else if (command.domain == "Utility") {
            static const std::set<std::string_view> utilities{"START_TEXTURE_PACKING", "USE_MATERIAL_POST_PROCESSING",
                                                              "USE_CURRENT_MATERIAL_AS_BASE",
                                                              "USE_CONVERTED_MATERIAL_AS_BASE"};
            if (!utilities.contains(command.operation) || !command.arguments.empty())
                return Result<VegetationConversionCandidate>::failure(Diagnostic::error(DiagnosticCode::ParseError, "Utility command takes no arguments", command.sourcePath, {},
                                                "asset.import.vegetation-preset.apply"));
            out.utilityModes.push_back(command.operation);
            if (command.operation == "START_TEXTURE_PACKING") out.texturePacks.emplace_back();
        } else if (command.domain == "Texture") {
            if (out.texturePacks.empty())
                return Result<VegetationConversionCandidate>::failure(Diagnostic::error(DiagnosticCode::ParseError, "Texture command requires START_TEXTURE_PACKING", command.sourcePath, {},
                                                "asset.import.vegetation-preset.apply"));
            auto& recipe = out.texturePacks.back();
            if (command.operation == "PropName" || command.operation == "ImportType" ||
                command.operation == "TransformSpace") {
                if (command.arguments.size() != 1)
                    return Result<VegetationConversionCandidate>::failure(Diagnostic::error(DiagnosticCode::ParseError, "Texture property command requires one argument", command.sourcePath, {},
                                                "asset.import.vegetation-preset.apply"));
                if (command.operation == "PropName")
                    recipe.targetProperty = command.arguments[0];
                else if (command.operation == "ImportType")
                    recipe.importType = command.arguments[0];
                else
                    recipe.transformSpace = command.arguments[0];
            } else {
                const std::map<std::string, std::size_t> channels{
                    {"SetRed", 0}, {"SetGreen", 1}, {"SetBlue", 2}, {"SetAlpha", 3}};
                const auto found = channels.find(command.operation);
                const bool none  = command.arguments.size() == 1 && command.arguments[0] == "NONE";
                if (found == channels.end() ||
                    (!none && (command.arguments.size() < 2 || command.arguments.size() > 3)))
                    return Result<VegetationConversionCandidate>::failure(Diagnostic::error(DiagnosticCode::ParseError, "Texture channel command is malformed", command.sourcePath, {},
                                                "asset.import.vegetation-preset.apply"));
                recipe.channels[found->second] =
                    none ? VegetationTextureChannelRecipe{"NONE", {}, {}}
                         : VegetationTextureChannelRecipe{command.arguments[0], command.arguments[1],
                                                          command.arguments.size() == 3 ? command.arguments[2] : ""};
            }
        } else if (command.domain == "Shader") {
            if (!command.operation.starts_with("SHADER_") || command.arguments.empty())
                return Result<VegetationConversionCandidate>::failure(Diagnostic::error(DiagnosticCode::ParseError, "Shader alias is malformed", command.sourcePath, {},
                                                "asset.import.vegetation-preset.apply"));
            out.shaderAliases[command.operation] = join(command.arguments);
        } else if (outputDomain(command.domain)) {
            auto values = command.arguments;
            values.insert(values.begin(), command.operation);
            out.outputDirectives[command.domain] = std::move(values);
        } else if (!metadata(command.domain)) {
            return Result<VegetationConversionCandidate>::failure(Diagnostic::error(DiagnosticCode::ParseError, "unknown preset command domain: " + command.domain, command.sourcePath, {},
                                                "asset.import.vegetation-preset.apply"));
        }
    }
    return Result<VegetationConversionCandidate>::success(std::move(out));
}
}  // namespace eve::asset_import
