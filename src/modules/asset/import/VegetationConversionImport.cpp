#include "asset/import/VegetationPreset.h"

#include "asset/SourcePng.h"
#include "asset/import/ImportCommon.h"
#include "asset/import/UnityImporter.h"
#include "asset/import/UnitySourceInternal.h"

#include <algorithm>
#include <cmath>
#include <new>
#include <set>

namespace eve::asset_import {
namespace {
Result<std::string> stableGuid(std::string_view seed) {
    const auto bytes = std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(seed.data()), seed.size());
    const auto hash  = detail::sha256(bytes);
    if (hash.size() < 39)
        return Result<std::string>::failure(Diagnostic::error(DiagnosticCode::Failed,
                                                              "stable vegetation texture hash is unavailable", {}, {},
                                                              "asset.import.vegetation-preset.publication"));
    return Result<std::string>::success(hash.substr(7, 32));
}

std::vector<std::uint8_t> bytes(std::string value) { return {value.begin(), value.end()}; }

Result<std::string> texturePathForGuid(const UnityVegetationAssetImportRequest& request, std::string_view guid) {
    std::string found;
    for (const auto& [path, content] : request.sourceFiles) {
        if (!path.ends_with(".meta")) continue;
        auto parsed = unity_detail::metaScalar(content, "guid", path);
        if (!parsed) return Result<std::string>::failure(parsed.status());
        if (unity_detail::foldAscii(parsed.value()) != guid) continue;
        const auto source = path.substr(0, path.size() - 5);
        if (!request.sourceFiles.contains(source))
            return Result<std::string>::failure(
                Diagnostic::error(DiagnosticCode::NotFound, "vegetation texture metadata has no source payload", source,
                                  {}, "asset.import.vegetation-preset.publication"));
        if (!found.empty())
            return Result<std::string>::failure(
                Diagnostic::error(DiagnosticCode::Conflict, "duplicate vegetation texture GUID", std::string(guid), {},
                                  "asset.import.vegetation-preset.publication"));
        found = source;
    }
    if (found.empty())
        return Result<std::string>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "referenced vegetation texture source is absent",
                              std::string(guid), {}, "asset.import.vegetation-preset.publication"));
    return Result<std::string>::success(std::move(found));
}

Result<void> appendMesh(PreparedAssetImport& prepared, const UnityVegetationAssetImportRequest& request,
                        const VegetationConversionResult& conversion) {
    if (!conversion.mesh) return Result<void>::success();
    if (prepared.manifest.assets.size() >= request.limits.maximumAssets)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "vegetation prepared asset count exceeds budget", {}, {},
                                                       "asset.import.vegetation-preset.publication"));
    auto blob = asset::encodeCanonicalMesh(
        *conversion.mesh, {request.limits.maximumVerticesPerPrimitive, request.limits.maximumIndicesPerPrimitive,
                           request.limits.maximumDecodedBytes});
    if (!blob) return Result<void>::failure(blob.status());
    const auto key = request.outputKey.empty() ? unity_detail::foldAscii(request.materialGuid) : request.outputKey;
    const auto id  = request.package.packageId.child("unity-converted:" + key + ":mesh");
    auto       ref = detail::assetRef(id);
    if (!ref) return Result<void>::failure(ref.status());
    std::array<float, 3> minimum{conversion.mesh->positions[0], conversion.mesh->positions[1],
                                 conversion.mesh->positions[2]};
    auto                 maximum = minimum;
    for (std::size_t offset = 3; offset < conversion.mesh->positions.size(); offset += 3)
        for (std::size_t axis = 0; axis < 3; ++axis) {
            minimum[axis] = std::min(minimum[axis], conversion.mesh->positions[offset + axis]);
            maximum[axis] = std::max(maximum[axis], conversion.mesh->positions[offset + axis]);
        }
    if (conversion.meshBoundsMinimum) minimum = *conversion.meshBoundsMinimum;
    if (conversion.meshBoundsMaximum) maximum = *conversion.meshBoundsMaximum;
    Value::Array uvSets;
    for (const auto& [set, values] : conversion.mesh->texcoords) uvSets.emplace_back(std::int64_t(set));
    Value::Array attributes;
    for (const auto& [name, attribute] : conversion.mesh->attributes)
        attributes.emplace_back(Value::Object{{"name", name}, {"components", std::int64_t(attribute.components)}});
    const std::string base           = "assets/" + id.format() + "/";
    const std::string definitionPath = base + "asset.json";
    const std::string blobPath       = base + "mesh.bin";
    auto              definition     = Value(Value::Object{
                                                 {"schema", "eve.mesh"},
                                                 {"schemaVersion", std::int64_t(2)},
                                                 {"topology", "triangles"},
                                                 {"vertexCount", std::int64_t(conversion.mesh->positions.size() / 3)},
                                                 {"indexCount", std::int64_t(conversion.mesh->indices.size())},
                                                 {"positions", true},
                                                 {"normals", !conversion.mesh->normals.empty()},
                                                 {"texcoordSets", std::move(uvSets)},
                                                 {"attributes", std::move(attributes)},
                                                 {"coordinateSystem", "right-handed-x-right-y-up-minus-z-forward"},
                                                 {"unit", "meter"},
                                                 {"frontFace", "counter-clockwise"},
                                                 {"boundsMin", Value::Array{minimum[0], minimum[1], minimum[2]}},
                                                 {"boundsMax", Value::Array{maximum[0], maximum[1], maximum[2]}},
                                                 {"cpuReadable", conversion.meshCpuReadable},
                                                 {"blob", blobPath},
                                             })
                                           .toJson();
    if (!definition) return Result<void>::failure(definition.status());
    auto encoded = bytes(std::move(definition).takeValue());
    prepared.manifest.assets.push_back({ref.value(),
                                        "eve.mesh",
                                        SchemaVersion(2),
                                        definitionPath,
                                        detail::sha256(encoded),
                                        {"mesh", "source:unity", "vegetation"}});
    prepared.entries.push_back({definitionPath, std::move(encoded)});
    prepared.entries.push_back({blobPath, std::move(blob).takeValue()});
    prepared.manifest.entrypoints.insert_or_assign("converted-mesh", ref.value());
    prepared.sourceMappings.push_back({"converted-mesh", ref.value()});
    prepared.findings.push_back({request.conversion.sourcePath, "TVE.Mesh", ImportDisposition::Baked,
                                 "preset vertex streams and bounds encoded as canonical EVMESH"});
    return Result<void>::success();
}
}  // namespace

Result<PreparedAssetImport> prepareUnityVegetationConversionImport(const UnityVegetationAssetImportRequest& request) {
    try {
        if (!unity_detail::validGuid(request.materialGuid))
            return Result<PreparedAssetImport>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "vegetation material GUID must contain 32 hexadecimal digits", {}, {},
                "asset.import.vegetation-preset.publication"));
        const auto materialGuid = unity_detail::foldAscii(request.materialGuid);
        const auto outputKey    = request.outputKey.empty() ? materialGuid : request.outputKey;
        auto       conversion   = executeUnityVegetationConversion(request.conversion);
        if (!conversion) return Result<PreparedAssetImport>::failure(conversion.status());

        UnityProjectImportRequest project;
        project.package                 = request.package;
        project.limits                  = request.limits;
        auto                  candidate = conversion.value().candidate;
        std::set<std::string> generatedGuids;
        for (const auto& [property, image] : conversion.value().textures) {
            auto imageGuid = stableGuid(outputKey + ":" + property);
            if (!imageGuid) return Result<PreparedAssetImport>::failure(imageGuid.status());
            auto png = asset::detail::encodeSourcePng(image.width, image.height, image.pixels,
                                                      request.limits.maximumDecodedBytes);
            if (!png) return Result<PreparedAssetImport>::failure(png.status());
            const auto path               = "Assets/Generated/TVE/" + imageGuid.value() + ".png";
            project.files[path]           = std::move(png).takeValue();
            project.files[path + ".meta"] = bytes("fileFormatVersion: 2\nguid: " + imageGuid.value() +
                                                  "\nTextureImporter:\n  sRGBTexture: 0\n  wrapU: 0\n  wrapV: 0\n"
                                                  "  filterMode: 1\n  enableMipMap: 1\n");
            candidate.materialTextures[property] = imageGuid.value();
            generatedGuids.insert(imageGuid.value());
        }
        for (const auto& [property, textureGuid] : candidate.materialTextures) {
            if (generatedGuids.contains(textureGuid)) continue;
            auto path = texturePathForGuid(request, textureGuid);
            if (!path) return Result<PreparedAssetImport>::failure(path.status());
            project.files[path.value()]           = request.sourceFiles.at(path.value());
            project.files[path.value() + ".meta"] = request.sourceFiles.at(path.value() + ".meta");
        }
        if (conversion.value().materialOutput != VegetationMaterialOutputMode::Off) {
            auto material = encodeUnityVegetationConversionMaterial(candidate, request.limits.maximumDecodedBytes);
            if (!material) return Result<PreparedAssetImport>::failure(material.status());
            project.files[request.conversion.sourcePath] = std::move(material).takeValue();
            project.files[request.conversion.sourcePath + ".meta"] =
                bytes("fileFormatVersion: 2\nguid: " + materialGuid +
                      "\nNativeFormatImporter:\n  mainObjectFileID: 2100000\n");
        }
        Result<PreparedAssetImport> prepared =
            conversion.value().materialOutput == VegetationMaterialOutputMode::Off ? [&]() {
                auto manifest = detail::baseManifest(request.package, "eve.unity-vegetation-conversion/1");
                if (!manifest) return Result<PreparedAssetImport>::failure(manifest.status());
                PreparedAssetImport value;
                value.manifest = std::move(manifest).takeValue();
                return Result<PreparedAssetImport>::success(std::move(value));
            }()
                                                                                   : prepareUnityProjectImport(project);
        if (!prepared) return prepared;
        std::erase_if(prepared.value().entries,
                      [](const asset::EvaArchiveEntry& entry) { return entry.path == "reports/import.json"; });
        auto mesh = appendMesh(prepared.value(), request, conversion.value());
        if (!mesh) return Result<PreparedAssetImport>::failure(mesh.status());
        auto report = detail::finalizeImportReport(prepared.value(), request.package, "unity", "tve-12.6-conversion",
                                                   {{"preset", request.conversion.rootPreset}});
        if (!report) return Result<PreparedAssetImport>::failure(report.status());
        std::uint64_t totalBytes = 0;
        for (const auto& entry : prepared.value().entries) {
            if (entry.bytes.size() > request.limits.maximumDecodedBytes ||
                totalBytes > request.limits.maximumDecodedBytes - entry.bytes.size())
                return Result<PreparedAssetImport>::failure(Diagnostic::error(
                    DiagnosticCode::InvalidArgument, "vegetation prepared output exceeds aggregate byte budget", {}, {},
                    "asset.import.vegetation-preset.publication"));
            totalBytes += entry.bytes.size();
        }
        return prepared;
    } catch (const std::bad_alloc&) {
        return Result<PreparedAssetImport>::failure(
            Diagnostic::error(DiagnosticCode::Failed, "vegetation publication allocation failed", {}, {},
                              "asset.import.vegetation-preset.publication"));
    }
}

Result<PreparedAssetImport> prepareUnityVegetationConversionBatch(const UnityVegetationBatchImportRequest& request) {
    try {
        if (request.objects.empty() || request.objects.size() > request.limits.maximumAssets)
            return Result<PreparedAssetImport>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "vegetation batch requires a bounded non-empty object set", {}, {},
                "asset.import.vegetation-preset.batch"));
        auto manifest = detail::baseManifest(request.package, "eve.unity-vegetation-conversion-batch/1");
        if (!manifest) return Result<PreparedAssetImport>::failure(manifest.status());
        PreparedAssetImport batch;
        batch.manifest = std::move(manifest).takeValue();
        std::set<std::string>                            outputKeys;
        std::map<std::string, std::vector<std::uint8_t>> uniqueEntries;
        std::set<std::string>                            assetIds;
        for (const auto& object : request.objects) {
            if (object.outputKey.empty() || !outputKeys.insert(object.outputKey).second)
                return Result<PreparedAssetImport>::failure(Diagnostic::error(
                    DiagnosticCode::Conflict, "vegetation batch output keys must be non-empty and unique",
                    object.outputKey, {}, "asset.import.vegetation-preset.batch"));
            auto itemRequest    = object;
            itemRequest.package = request.package;
            itemRequest.limits  = request.limits;
            auto item           = prepareUnityVegetationConversionImport(itemRequest);
            if (!item) return Result<PreparedAssetImport>::failure(item.status());
            for (auto& asset : item.value().manifest.assets) {
                if (!assetIds.insert(asset.asset.format()).second)
                    return Result<PreparedAssetImport>::failure(
                        Diagnostic::error(DiagnosticCode::Conflict, "vegetation batch asset identity collision",
                                          asset.asset.format(), {}, "asset.import.vegetation-preset.batch"));
                batch.manifest.assets.push_back(std::move(asset));
            }
            for (auto& dependency : item.value().manifest.dependencies)
                batch.manifest.dependencies.push_back(std::move(dependency));
            for (auto& entry : item.value().entries) {
                if (entry.path == "reports/import.json") continue;
                auto [found, inserted] = uniqueEntries.emplace(entry.path, entry.bytes);
                if (!inserted && found->second != entry.bytes)
                    return Result<PreparedAssetImport>::failure(
                        Diagnostic::error(DiagnosticCode::Conflict, "vegetation batch entry collision", entry.path, {},
                                          "asset.import.vegetation-preset.batch"));
            }
            for (auto& mapping : item.value().sourceMappings) {
                mapping.sourceObject = object.outputKey + "/" + mapping.sourceObject;
                batch.sourceMappings.push_back(std::move(mapping));
            }
            for (auto& finding : item.value().findings) batch.findings.push_back(std::move(finding));
            for (const auto& [name, ref] : item.value().manifest.entrypoints) {
                const auto key =
                    object.outputKey + (name == "default" || name == object.conversion.sourcePath ? "" : "#" + name);
                if (!batch.manifest.entrypoints.emplace(key, ref).second)
                    return Result<PreparedAssetImport>::failure(
                        Diagnostic::error(DiagnosticCode::Conflict, "vegetation batch entrypoint collision", key, {},
                                          "asset.import.vegetation-preset.batch"));
            }
            if (batch.manifest.assets.size() > request.limits.maximumAssets)
                return Result<PreparedAssetImport>::failure(
                    Diagnostic::error(DiagnosticCode::InvalidArgument, "vegetation batch asset count exceeds budget",
                                      {}, {}, "asset.import.vegetation-preset.batch"));
        }
        for (auto& [path, content] : uniqueEntries) batch.entries.push_back({std::move(path), std::move(content)});
        auto report = detail::finalizeImportReport(batch, request.package, "unity", "tve-12.6-conversion-batch",
                                                   {{"objectCount", std::int64_t(request.objects.size())}});
        if (!report) return Result<PreparedAssetImport>::failure(report.status());
        std::uint64_t totalBytes = 0;
        for (const auto& entry : batch.entries) {
            if (entry.bytes.size() > request.limits.maximumDecodedBytes ||
                totalBytes > request.limits.maximumDecodedBytes - entry.bytes.size())
                return Result<PreparedAssetImport>::failure(
                    Diagnostic::error(DiagnosticCode::InvalidArgument, "vegetation batch exceeds aggregate byte budget",
                                      {}, {}, "asset.import.vegetation-preset.batch"));
            totalBytes += entry.bytes.size();
        }
        return Result<PreparedAssetImport>::success(std::move(batch));
    } catch (const std::bad_alloc&) {
        return Result<PreparedAssetImport>::failure(Diagnostic::error(DiagnosticCode::Failed,
                                                                      "vegetation batch allocation failed", {}, {},
                                                                      "asset.import.vegetation-preset.batch"));
    }
}
}  // namespace eve::asset_import

#include "asset/import/VegetationPreset.h"

#include <cctype>
#include <cmath>
#include <new>
#include <regex>
#include <sstream>

namespace eve::asset_import {
namespace {

Result<double> number(std::string_view text, const std::string& path) {
    std::string value(text);
    char*       end   = nullptr;
    errno             = 0;
    const auto parsed = std::strtod(value.c_str(), &end);
    if (errno || end != value.c_str() + value.size() || !std::isfinite(parsed))
        return Result<double>::failure(Diagnostic::error(DiagnosticCode::ParseError, "Unity material number is invalid",
                                                         path, {}, "asset.import.vegetation-preset.source"));
    return Result<double>::success(parsed);
}

template <class Map, class ValueType>
Result<void> insertUnique(Map& values, std::string name, ValueType value, const std::string& path) {
    if (!values.emplace(std::move(name), std::move(value)).second)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::Conflict,
                                                       "duplicate Unity material saved property", path, {},
                                                       "asset.import.vegetation-preset.source"));
    return Result<void>::success();
}
}  // namespace

Result<VegetationConversionCandidate> decodeUnityVegetationConversionCandidate(std::span<const std::uint8_t> yaml,
                                                                               std::string                   sourcePath,
                                                                               std::uint64_t maximumBytes) {
    if (yaml.empty() || yaml.size() > maximumBytes)
        return Result<VegetationConversionCandidate>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "Unity material source exceeds parsing budget",
                              sourcePath, {}, "asset.import.vegetation-preset.source"));
    try {
        const std::string text(reinterpret_cast<const char*>(yaml.data()), yaml.size());
        if (text.find("\nMaterial:") == std::string::npos && !text.starts_with("Material:"))
            return Result<VegetationConversionCandidate>::failure(
                Diagnostic::error(DiagnosticCode::TypeMismatch, "Unity source is not a text Material", sourcePath, {},
                                  "asset.import.vegetation-preset.source"));
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
                    return Result<VegetationConversionCandidate>::failure(
                        Diagnostic::error(DiagnosticCode::ParseError, "Unity texture reference lacks a GUID",
                                          sourcePath, {}, "asset.import.vegetation-preset.source"));
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
            return Result<VegetationConversionCandidate>::failure(
                Diagnostic::error(DiagnosticCode::ParseError, "Unity material shader GUID is missing", sourcePath, {},
                                  "asset.import.vegetation-preset.source"));
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
        return Result<VegetationConversionCandidate>::failure(
            Diagnostic::error(DiagnosticCode::Failed, "Unity material parser expression failed", sourcePath, {},
                              "asset.import.vegetation-preset.source"));
    } catch (const std::bad_alloc&) {
        return Result<VegetationConversionCandidate>::failure(
            Diagnostic::error(DiagnosticCode::Failed, "Unity material candidate allocation failed", sourcePath, {},
                              "asset.import.vegetation-preset.source"));
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
