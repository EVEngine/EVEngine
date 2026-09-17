#include "asset/import/VegetationPreset.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <new>
#include <sstream>

namespace eve::asset_import {
namespace {
Result<std::string> shaderGuid(std::string_view shader) {
    if (shader == "7befaa6f41d00a6478d5f4af21d66518" ||
        shader == "BOXOPHOBIC/The Vegetation Engine/Geometry/Plant Standard Lit")
        return Result<std::string>::success("7befaa6f41d00a6478d5f4af21d66518");
    if (shader == "a933075b367f9b24981408633f72ff34" ||
        shader == "BOXOPHOBIC/The Vegetation Engine/Geometry/Plant Subsurface Lit")
        return Result<std::string>::success("a933075b367f9b24981408633f72ff34");
    if (shader == "6e6307b56f9201d40ad738f21cf03495" ||
        shader == "BOXOPHOBIC/The Vegetation Engine/Geometry/Prop Standard Lit")
        return Result<std::string>::success("6e6307b56f9201d40ad738f21cf03495");
    if (shader == "d9a724745053dee46bf301b216cdd348" ||
        shader == "BOXOPHOBIC/The Vegetation Engine/Geometry/Prop Subsurface Lit")
        return Result<std::string>::success("d9a724745053dee46bf301b216cdd348");
    return Result<std::string>::failure(Diagnostic::error(DiagnosticCode::Unsupported,
                                                          "vegetation output shader is not an admitted TVE 12.6 target",
                                                          {}, {}, "asset.import.vegetation-preset.material"));
}

bool propertyName(std::string_view value) {
    return !value.empty() && value.size() <= 127 && std::all_of(value.begin(), value.end(), [](unsigned char c) {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
    });
}

bool finite(const std::array<double, 4>& value) {
    return std::all_of(value.begin(), value.end(), [](double component) { return std::isfinite(component); });
}

bool guid(std::string_view value) {
    return value.size() == 32 && std::all_of(value.begin(), value.end(), [](unsigned char c) {
               return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
           });
}
}  // namespace

Result<std::vector<std::uint8_t>> encodeUnityVegetationConversionMaterial(
    const VegetationConversionCandidate& candidate, std::uint64_t maximumBytes) {
    try {
        auto shader = shaderGuid(candidate.materialShader);
        if (!shader) return Result<std::vector<std::uint8_t>>::failure(shader.status());
        std::map<std::string, std::array<double, 4>> vectors = candidate.materialVectors;
        for (const auto& [name, value] : candidate.materialColors) {
            if (const auto found = vectors.find(name); found != vectors.end() && found->second != value)
                return Result<std::vector<std::uint8_t>>::failure(
                    Diagnostic::error(DiagnosticCode::Conflict, "material vector and color values conflict", name, {},
                                      "asset.import.vegetation-preset.material"));
            vectors[name] = value;
        }
        for (const auto& [name, value] : candidate.materialFloats)
            if (!propertyName(name) || !std::isfinite(value))
                return Result<std::vector<std::uint8_t>>::failure(
                    Diagnostic::error(DiagnosticCode::InvalidArgument, "invalid vegetation material float", name, {},
                                      "asset.import.vegetation-preset.material"));
        for (const auto& [name, value] : vectors)
            if (!propertyName(name) || !finite(value))
                return Result<std::vector<std::uint8_t>>::failure(
                    Diagnostic::error(DiagnosticCode::InvalidArgument, "invalid vegetation material vector", name, {},
                                      "asset.import.vegetation-preset.material"));
        for (const auto& [name, value] : candidate.materialTextures)
            if (!propertyName(name) || !guid(value))
                return Result<std::vector<std::uint8_t>>::failure(Diagnostic::error(
                    DiagnosticCode::InvalidArgument, "vegetation material texture requires a lowercase Unity GUID",
                    name, {}, "asset.import.vegetation-preset.material"));

        std::ostringstream text;
        text << std::setprecision(17);
        text << "%YAML 1.1\n--- !u!21 &2100000\nMaterial:\n"
             << "  m_Shader: {fileID: 4800000, guid: " << shader.value() << ", type: 3}\n"
             << "  m_ValidKeywords:\n";
        for (const auto& keyword : candidate.materialKeywords) text << "  - " << keyword << '\n';
        text << "  m_EnableInstancingVariants: " << (candidate.materialInstancing ? 1 : 0) << '\n'
             << "  m_SavedProperties:\n    serializedVersion: 3\n    m_TexEnvs:\n";
        for (const auto& [name, texture] : candidate.materialTextures) {
            const auto transform = candidate.materialTextureTransforms.contains(name)
                                       ? candidate.materialTextureTransforms.at(name)
                                       : std::array<double, 4>{1, 1, 0, 0};
            if (!finite(transform))
                return Result<std::vector<std::uint8_t>>::failure(
                    Diagnostic::error(DiagnosticCode::InvalidArgument, "invalid vegetation texture transform", name, {},
                                      "asset.import.vegetation-preset.material"));
            text << "    - " << name << ":\n"
                 << "        m_Texture: {fileID: 2800000, guid: " << texture << ", type: 3}\n"
                 << "        m_Scale: {x: " << transform[0] << ", y: " << transform[1] << "}\n"
                 << "        m_Offset: {x: " << transform[2] << ", y: " << transform[3] << "}\n";
        }
        text << "    m_Ints: []\n    m_Floats:\n";
        for (const auto& [name, value] : candidate.materialFloats) text << "    - " << name << ": " << value << '\n';
        text << "    m_Colors:\n";
        for (const auto& [name, value] : vectors)
            text << "    - " << name << ": {r: " << value[0] << ", g: " << value[1] << ", b: " << value[2]
                 << ", a: " << value[3] << "}\n";
        const auto encoded = text.str();
        if (encoded.size() > maximumBytes)
            return Result<std::vector<std::uint8_t>>::failure(
                Diagnostic::error(DiagnosticCode::InvalidArgument, "encoded vegetation material exceeds byte budget",
                                  {}, {}, "asset.import.vegetation-preset.material"));
        return Result<std::vector<std::uint8_t>>::success({encoded.begin(), encoded.end()});
    } catch (const std::bad_alloc&) {
        return Result<std::vector<std::uint8_t>>::failure(
            Diagnostic::error(DiagnosticCode::Failed, "vegetation material encoding allocation failed", {}, {},
                              "asset.import.vegetation-preset.material"));
    }
}
}  // namespace eve::asset_import
