#include "asset/import/VegetationPreset.h"

#include <cctype>
#include <cmath>
#include <new>
#include <regex>
#include <sstream>

namespace eve::asset_import {
namespace {
template <class T>
Result<T> failure(DiagnosticCode code, std::string message, const std::string& path) {
    return Result<T>::failure(
        Diagnostic::error(code, std::move(message), path, {}, "asset.import.vegetation-preset.source"));
}

Result<double> number(std::string_view text, const std::string& path) {
    std::string value(text);
    char*       end   = nullptr;
    errno             = 0;
    const auto parsed = std::strtod(value.c_str(), &end);
    if (errno || end != value.c_str() + value.size() || !std::isfinite(parsed))
        return failure<double>(DiagnosticCode::ParseError, "Unity material number is invalid", path);
    return Result<double>::success(parsed);
}

template <class Map, class ValueType>
Result<void> insertUnique(Map& values, std::string name, ValueType value, const std::string& path) {
    if (!values.emplace(std::move(name), std::move(value)).second)
        return failure<void>(DiagnosticCode::Conflict, "duplicate Unity material saved property", path);
    return Result<void>::success();
}
}  // namespace

Result<VegetationConversionCandidate> decodeUnityVegetationConversionCandidate(std::span<const std::uint8_t> yaml,
                                                                               std::string                   sourcePath,
                                                                               std::uint64_t maximumBytes) {
    if (yaml.empty() || yaml.size() > maximumBytes)
        return failure<VegetationConversionCandidate>(DiagnosticCode::InvalidArgument,
                                                      "Unity material source exceeds parsing budget", sourcePath);
    try {
        const std::string text(reinterpret_cast<const char*>(yaml.data()), yaml.size());
        if (text.find("\nMaterial:") == std::string::npos && !text.starts_with("Material:"))
            return failure<VegetationConversionCandidate>(DiagnosticCode::TypeMismatch,
                                                          "Unity source is not a text Material", sourcePath);
        VegetationConversionCandidate out;
        const std::regex scalarPattern(R"((?:^|\n)[ \t]*-[ \t]+([A-Za-z_][A-Za-z0-9_]*):[ \t]*([-+0-9.eE]+))");
        for (auto i = std::sregex_iterator(text.begin(), text.end(), scalarPattern); i != std::sregex_iterator(); ++i) {
            auto value = number((*i)[2].str(), sourcePath);
            if (!value) return Result<VegetationConversionCandidate>::failure(value.status());
            auto inserted = insertUnique(out.materialFloats, (*i)[1].str(), value.value(), sourcePath);
            if (!inserted) return Result<VegetationConversionCandidate>::failure(inserted.status());
        }
        const std::regex vectorPattern(
            R"((?:^|\n)[ \t]*-[ \t]+([A-Za-z_][A-Za-z0-9_]*):[ \t]*\{(?:r|x):[ \t]*([^,}]+),[ \t]*(?:g|y):[ \t]*([^,}]+),[ \t]*(?:b|z):[ \t]*([^,}]+),[ \t]*(?:a|w):[ \t]*([^}]+)\})");
        for (auto i = std::sregex_iterator(text.begin(), text.end(), vectorPattern); i != std::sregex_iterator(); ++i) {
            std::array<double, 4> value{};
            for (std::size_t component = 0; component < 4; ++component) {
                auto parsed = number((*i)[component + 2].str(), sourcePath);
                if (!parsed) return Result<VegetationConversionCandidate>::failure(parsed.status());
                value[component] = parsed.value();
            }
            const auto name           = (*i)[1].str();
            auto       vectorInserted = insertUnique(out.materialVectors, name, value, sourcePath);
            if (!vectorInserted) return Result<VegetationConversionCandidate>::failure(vectorInserted.status());
            auto colorInserted = insertUnique(out.materialColors, name, value, sourcePath);
            if (!colorInserted) return Result<VegetationConversionCandidate>::failure(colorInserted.status());
        }
        const std::regex texturePattern(
            R"((?:^|\n)[ \t]*-[ \t]+([A-Za-z_][A-Za-z0-9_]*):[ \t]*(?:\r?\n)[ \t]+m_Texture:[ \t]*\{fileID:[ \t]*(-?[0-9]+)(?:,[ \t]*guid:[ \t]*([0-9a-fA-F]{32}),[ \t]*type:[ \t]*[0-9]+)?\}[ \t]*(?:\r?\n)[ \t]+m_Scale:[ \t]*\{x:[ \t]*([^,}]+),[ \t]*y:[ \t]*([^}]+)\}[ \t]*(?:\r?\n)[ \t]+m_Offset:[ \t]*\{x:[ \t]*([^,}]+),[ \t]*y:[ \t]*([^}]+)\})");
        for (auto i = std::sregex_iterator(text.begin(), text.end(), texturePattern); i != std::sregex_iterator();
             ++i) {
            const auto name = (*i)[1].str();
            if ((*i)[2].str() != "0") {
                if (!(*i)[3].matched)
                    return failure<VegetationConversionCandidate>(DiagnosticCode::ParseError,
                                                                  "Unity texture reference lacks a GUID", sourcePath);
                auto guid = (*i)[3].str();
                std::transform(guid.begin(), guid.end(), guid.begin(),
                               [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
                auto inserted = insertUnique(out.materialTextures, name, std::move(guid), sourcePath);
                if (!inserted) return Result<VegetationConversionCandidate>::failure(inserted.status());
            }
            std::array<double, 4> transform{};
            for (std::size_t component = 0; component < 4; ++component) {
                auto parsed = number((*i)[component + 4].str(), sourcePath);
                if (!parsed) return Result<VegetationConversionCandidate>::failure(parsed.status());
                transform[component] = parsed.value();
            }
            auto inserted = insertUnique(out.materialTextureTransforms, name, transform, sourcePath);
            if (!inserted) return Result<VegetationConversionCandidate>::failure(inserted.status());
        }
        const std::regex shaderPattern(
            R"((?:^|\n)[ \t]*m_Shader:[ \t]*\{fileID:[ \t]*[0-9]+,[ \t]*guid:[ \t]*([0-9a-fA-F]{32}),[ \t]*type:[ \t]*[0-9]+\})");
        std::smatch shader;
        if (!std::regex_search(text, shader, shaderPattern))
            return failure<VegetationConversionCandidate>(DiagnosticCode::ParseError,
                                                          "Unity material shader GUID is missing", sourcePath);
        out.materialShader = shader[1].str();
        std::transform(out.materialShader.begin(), out.materialShader.end(), out.materialShader.begin(),
                       [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
        const std::regex instancingPattern(R"((?:^|\n)[ \t]*m_EnableInstancingVariants:[ \t]*([01])(?:\r?\n|$))");
        std::smatch      instancing;
        if (std::regex_search(text, instancing, instancingPattern)) out.materialInstancing = instancing[1].str() == "1";
        std::istringstream lines(text);
        std::string        line;
        bool               validKeywords = false;
        while (std::getline(lines, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line == "  m_ValidKeywords:") {
                validKeywords = true;
                continue;
            }
            if (validKeywords) {
                if (line.starts_with("  - ")) {
                    if (line.size() > 4) out.materialKeywords.insert(line.substr(4));
                    continue;
                }
                validKeywords = false;
            }
            constexpr std::string_view legacy = "  m_ShaderKeywords:";
            if (line.starts_with(legacy)) {
                std::istringstream words(line.substr(legacy.size()));
                std::string        keyword;
                while (words >> keyword) out.materialKeywords.insert(std::move(keyword));
            }
        }
        return Result<VegetationConversionCandidate>::success(std::move(out));
    } catch (const std::regex_error&) {
        return failure<VegetationConversionCandidate>(DiagnosticCode::Failed, "Unity material parser expression failed",
                                                      sourcePath);
    } catch (const std::bad_alloc&) {
        return failure<VegetationConversionCandidate>(DiagnosticCode::Failed,
                                                      "Unity material candidate allocation failed", sourcePath);
    }
}

VegetationPresetContext makeVegetationPresetContext(const VegetationConversionCandidate& candidate,
                                                    std::string shaderName, std::string materialName,
                                                    std::string shaderPipeline, std::set<std::string> outputOptions) {
    VegetationPresetContext context;
    context.outputOptions    = std::move(outputOptions);
    context.shaderName       = std::move(shaderName);
    context.materialName     = std::move(materialName);
    context.shaderPipeline   = std::move(shaderPipeline);
    context.materialFloats   = candidate.materialFloats;
    context.materialKeywords = candidate.materialKeywords;
    const auto addProperties = [&](const auto& values) {
        for (const auto& [name, unused] : values) {
            (void)unused;
            context.materialProperties.insert(name);
        }
    };
    addProperties(candidate.materialFloats);
    addProperties(candidate.materialVectors);
    addProperties(candidate.materialColors);
    addProperties(candidate.materialTextureTransforms);
    for (const auto& [name, unused] : candidate.materialTextures) {
        (void)unused;
        context.materialProperties.insert(name);
        context.materialTextures.insert(name);
    }
    return context;
}
}  // namespace eve::asset_import
