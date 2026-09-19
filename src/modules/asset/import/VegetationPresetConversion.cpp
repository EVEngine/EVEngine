#include "asset/import/VegetationPreset.h"

#include <algorithm>
#include <new>

namespace eve::asset_import {
namespace {
Result<void> applyOutputPlan(VegetationConversionResult& result) {
    const auto value = [&](const char* domain) -> const std::string* {
        const auto found = result.candidate.outputDirectives.find(domain);
        return found == result.candidate.outputDirectives.end() || found->second.size() != 1 ? nullptr
                                                                                             : &found->second.front();
    };
    const auto invalid = [](std::string message) {
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::ParseError, std::move(message), {}, {},
                                                       "asset.import.vegetation-preset.conversion"));
    };
    if (result.candidate.outputDirectives.contains("OutputMeshes")) {
        const auto* mode = value("OutputMeshes");
        if (!mode) return invalid("OutputMeshes requires one mode");
        if (*mode == "OFF" || *mode == "NONE")
            result.meshOutput = VegetationMeshOutputMode::Off;
        else if (*mode == "DEFAULT")
            result.meshOutput = VegetationMeshOutputMode::Default;
        else if (*mode == "CUSTOM")
            result.meshOutput = VegetationMeshOutputMode::Custom;
        else
            return invalid("unsupported OutputMeshes mode");
    }
    if (result.candidate.outputDirectives.contains("OutputMaterials")) {
        const auto* mode = value("OutputMaterials");
        if (!mode) return invalid("OutputMaterials requires one mode");
        if (*mode == "OFF" || *mode == "NONE")
            result.materialOutput = VegetationMaterialOutputMode::Off;
        else if (*mode == "DEFAULT")
            result.materialOutput = VegetationMaterialOutputMode::Default;
        else
            return invalid("unsupported OutputMaterials mode");
    }
    if (result.candidate.outputDirectives.contains("OutputTextures")) {
        const auto* mode = value("OutputTextures");
        if (!mode) return invalid("OutputTextures requires one mode");
        if (*mode == "SAVE_TEXTURES_AS_PNG")
            result.textureOutput = VegetationTextureOutputEncoding::Png;
        else if (*mode == "SAVE_TEXTURES_AS_TGA")
            result.textureOutput = VegetationTextureOutputEncoding::Tga;
        else if (*mode == "SAVE_TEXTURES_AS_EXR")
            result.textureOutput = VegetationTextureOutputEncoding::Exr;
        else if (*mode == "SAVE_TEXTURES_AS_ASSET")
            result.textureOutput = VegetationTextureOutputEncoding::UnityAsset;
        else
            return invalid("unsupported OutputTextures mode");
    }
    if (result.candidate.outputDirectives.contains("OutputTransforms")) {
        const auto* mode = value("OutputTransforms");
        if (!mode) return invalid("OutputTransforms requires one mode");
        if (*mode == "KEEP_ORIGINAL_TRANSFORMS" || *mode == "USE_ORIGINAL_TRANSFORMS")
            result.transformOutput = VegetationTransformOutputMode::KeepOriginal;
        else if (*mode == "TRANSFORM_TO_WORLD_SPACE")
            result.transformOutput = VegetationTransformOutputMode::TransformToWorldSpace;
        else
            return invalid("unsupported OutputTransforms mode");
    }
    return Result<void>::success();
}

Result<void> transformToWorld(asset::CanonicalMeshData& mesh, const std::array<float, 16>& point,
                              const std::array<float, 9>& direction) {
    for (const float value : point)
        if (!std::isfinite(value))
            return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                           "source-to-world point matrix contains nonfinite values", {},
                                                           {}, "asset.import.vegetation-preset.conversion"));
    for (const float value : direction)
        if (!std::isfinite(value))
            return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                           "source-to-world direction matrix contains nonfinite values",
                                                           {}, {}, "asset.import.vegetation-preset.conversion"));
    const float determinant = direction[0] * (direction[4] * direction[8] - direction[7] * direction[5]) -
                              direction[3] * (direction[1] * direction[8] - direction[7] * direction[2]) +
                              direction[6] * (direction[1] * direction[5] - direction[4] * direction[2]);
    const auto  dot         = [&](unsigned a, unsigned b) {
        return direction[a] * direction[b] + direction[a + 1] * direction[b + 1] + direction[a + 2] * direction[b + 2];
    };
    if (std::abs(determinant - 1.f) > 1e-4f || std::abs(dot(0, 0) - 1.f) > 1e-4f || std::abs(dot(3, 3) - 1.f) > 1e-4f ||
        std::abs(dot(6, 6) - 1.f) > 1e-4f || std::abs(dot(0, 3)) > 1e-4f || std::abs(dot(0, 6)) > 1e-4f ||
        std::abs(dot(3, 6)) > 1e-4f)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "source-to-world direction matrix is not a proper rotation", {},
                                                       {}, "asset.import.vegetation-preset.conversion"));
    const auto transformDirection = [&](float& x, float& y, float& z) {
        const float ox     = direction[0] * x + direction[3] * y + direction[6] * z;
        const float oy     = direction[1] * x + direction[4] * y + direction[7] * z;
        const float oz     = direction[2] * x + direction[5] * y + direction[8] * z;
        const float length = std::sqrt(ox * ox + oy * oy + oz * oz);
        if (length < 1e-8f) return false;
        x = ox / length;
        y = oy / length;
        z = oz / length;
        return true;
    };
    for (std::size_t i = 0; i < mesh.positions.size(); i += 3) {
        const float x = mesh.positions[i], y = mesh.positions[i + 1], z = mesh.positions[i + 2];
        mesh.positions[i]     = point[0] * x + point[4] * y + point[8] * z + point[12];
        mesh.positions[i + 1] = point[1] * x + point[5] * y + point[9] * z + point[13];
        mesh.positions[i + 2] = point[2] * x + point[6] * y + point[10] * z + point[14];
        if (!transformDirection(mesh.normals[i], mesh.normals[i + 1], mesh.normals[i + 2]))
            return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                           "world transform produced a zero mesh normal", {}, {},
                                                           "asset.import.vegetation-preset.conversion"));
    }
    if (auto tangent = mesh.attributes.find("TANGENT"); tangent != mesh.attributes.end()) {
        if (tangent->second.components != 4 || tangent->second.values.size() / 4 != mesh.positions.size() / 3)
            return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                           "world transform tangent stream is malformed", {}, {},
                                                           "asset.import.vegetation-preset.conversion"));
        for (std::size_t i = 0; i < tangent->second.values.size(); i += 4)
            if (!transformDirection(tangent->second.values[i], tangent->second.values[i + 1],
                                    tangent->second.values[i + 2]))
                return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                               "world transform produced a zero mesh tangent", {}, {},
                                                               "asset.import.vegetation-preset.conversion"));
    }
    return Result<void>::success();
}
}  // namespace

Result<VegetationConversionResult> executeVegetationConversion(
    VegetationConversionCandidate source, const std::vector<VegetationPresetCommand>& commands,
    std::optional<asset::CanonicalMeshData> mesh, const std::map<std::string, VegetationPresetImage>& textures,
    float variationSeed, const std::optional<std::array<float, 16>>& sourceToWorld,
    const std::optional<std::array<float, 9>>& sourceDirectionToWorld) {
    try {
        auto applied = applyVegetationPreset(std::move(source), commands);
        if (!applied) return Result<VegetationConversionResult>::failure(applied.status());
        VegetationConversionResult result;
        result.candidate = std::move(applied).takeValue();
        auto outputPlan  = applyOutputPlan(result);
        if (!outputPlan) return Result<VegetationConversionResult>::failure(outputPlan.status());
        if (mesh && result.meshOutput != VegetationMeshOutputMode::Off &&
            result.transformOutput == VegetationTransformOutputMode::TransformToWorldSpace) {
            if (sourceToWorld.has_value() != sourceDirectionToWorld.has_value())
                return Result<VegetationConversionResult>::failure(Diagnostic::error(
                    DiagnosticCode::InvalidArgument, "world conversion requires both point and direction matrices", {},
                    {}, "asset.import.vegetation-preset.conversion"));
            if (sourceToWorld) {
                auto transformed = transformToWorld(*mesh, *sourceToWorld, *sourceDirectionToWorld);
                if (!transformed) return Result<VegetationConversionResult>::failure(transformed.status());
            }
        }
        if (mesh && result.meshOutput != VegetationMeshOutputMode::Off) {
            auto converted = executeVegetationMeshRules(result.candidate, std::move(*mesh), textures, variationSeed);
            if (!converted) return Result<VegetationConversionResult>::failure(converted.status());
            result.mesh       = std::move(converted).takeValue();
            const auto count  = result.mesh->positions.size() / 3;
            float      radius = 0, minimumY = result.mesh->positions[1], maximumY = minimumY;
            for (std::size_t i = 0; i < count; ++i) {
                radius   = std::max(radius, std::max(std::abs(result.mesh->positions[i * 3]),
                                                     std::abs(result.mesh->positions[i * 3 + 2])));
                minimumY = std::min(minimumY, result.mesh->positions[i * 3 + 1]);
                maximumY = std::max(maximumY, result.mesh->positions[i * 3 + 1]);
            }
            if (auto bounds = result.candidate.meshRules.find("SetBounds");
                bounds != result.candidate.meshRules.end()) {
                if (bounds->second.size() != 2 || bounds->second[0] != "GET_BOUNDS_PROCEDURAL")
                    return Result<VegetationConversionResult>::failure(
                        Diagnostic::error(DiagnosticCode::Unsupported, "unsupported vegetation bounds rule", {}, {},
                                          "asset.import.vegetation-preset.conversion"));
                const auto& option = bounds->second[1];
                const float expand = option == "0"   ? 1.2f
                                     : option == "1" ? 1.4f
                                     : option == "2" ? 1.6f
                                     : option == "3" ? 2.f
                                                     : 0.f;
                if (expand == 0)
                    return Result<VegetationConversionResult>::failure(
                        Diagnostic::error(DiagnosticCode::ParseError, "invalid vegetation bounds option", {}, {},
                                          "asset.import.vegetation-preset.conversion"));
                result.meshBoundsMinimum = std::array{-radius * expand, minimumY, -radius * expand};
                result.meshBoundsMaximum = std::array{radius * expand, maximumY * 1.25f, radius * expand};
            }
            if (auto readable = result.candidate.meshRules.find("SetReadWrite");
                readable != result.candidate.meshRules.end()) {
                if (readable->second.size() != 1 || (readable->second[0] != "MARK_MESHES_AS_READABLE" &&
                                                     readable->second[0] != "MARK_MESHES_AS_NON_READABLE"))
                    return Result<VegetationConversionResult>::failure(
                        Diagnostic::error(DiagnosticCode::Unsupported, "unsupported vegetation mesh readability rule",
                                          {}, {}, "asset.import.vegetation-preset.conversion"));
                result.meshCpuReadable = readable->second[0] == "MARK_MESHES_AS_READABLE";
            }
        } else if (!mesh && !result.candidate.meshRules.empty() && result.meshOutput != VegetationMeshOutputMode::Off) {
            return Result<VegetationConversionResult>::failure(
                Diagnostic::error(DiagnosticCode::NotFound, "vegetation preset requires a source mesh", {}, {},
                                  "asset.import.vegetation-preset.conversion"));
        }
        if (result.materialOutput != VegetationMaterialOutputMode::Off) {
            auto packed = result.mesh ? executeVegetationTexturePacks(result.candidate, textures, *result.mesh)
                                      : executeVegetationTexturePacks(result.candidate, textures);
            if (!packed) return Result<VegetationConversionResult>::failure(packed.status());
            result.textures = std::move(packed).takeValue();
        }
        return Result<VegetationConversionResult>::success(std::move(result));
    } catch (const std::bad_alloc&) {
        return Result<VegetationConversionResult>::failure(
            Diagnostic::error(DiagnosticCode::Failed, "vegetation conversion allocation failed", {}, {},
                              "asset.import.vegetation-preset.conversion"));
    }
}

Result<VegetationConversionResult> executeUnityVegetationConversion(const UnityVegetationConversionRequest& request) {
    if (request.sourcePath.empty() || request.shaderName.empty() || request.materialName.empty() ||
        request.rootPreset.empty())
        return Result<VegetationConversionResult>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument,
                              "Unity vegetation conversion requires source, shader, material, and root preset names",
                              request.sourcePath, {}, "asset.import.vegetation-preset.conversion"));
    try {
        auto source = decodeUnityVegetationConversionCandidate(request.materialYaml, request.sourcePath,
                                                               request.maximumMaterialBytes);
        if (!source) return Result<VegetationConversionResult>::failure(source.status());
        auto context  = makeVegetationPresetContext(source.value(), request.shaderName, request.materialName,
                                                    request.shaderPipeline, request.outputOptions);
        auto commands = evaluateVegetationPreset(request.presetDefinitions, request.rootPreset, context);
        if (!commands) return Result<VegetationConversionResult>::failure(commands.status());
        return executeVegetationConversion(std::move(source).takeValue(), commands.value(), request.mesh,
                                           request.textures, request.variationSeed, request.sourceToWorld,
                                           request.sourceDirectionToWorld);
    } catch (const std::bad_alloc&) {
        return Result<VegetationConversionResult>::failure(
            Diagnostic::error(DiagnosticCode::Failed, "Unity vegetation transaction allocation failed",
                              request.sourcePath, {}, "asset.import.vegetation-preset.conversion"));
    }
}
}  // namespace eve::asset_import
