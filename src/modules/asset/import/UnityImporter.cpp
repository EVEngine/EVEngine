#include "asset/import/UnityImporter.h"

#include "asset/import/ImportCommon.h"
#include "asset/import/UnitySourceInternal.h"

#include <charconv>
#include <cmath>
#include <regex>
#include <set>
#include <sstream>

namespace eve::asset_import {
namespace {

Result<std::string> textFile(const UnityProjectImportRequest& request, const std::string& path) {
    const auto found = request.files.find(path);
    if (found == request.files.end())
        return Result<std::string>::failure(Diagnostic::error(
            DiagnosticCode::NotFound, "Unity source file was not supplied", path, {}, "asset.import"));
    if (found->second.size() > request.limits.maximumSourceBytes)
        return Result<std::string>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                              "Unity source exceeds budget", path, {}, "asset.import"));
    if (std::find(found->second.begin(), found->second.end(), std::uint8_t(0)) != found->second.end())
        return Result<std::string>::failure(Diagnostic::error(
            DiagnosticCode::Unsupported, "Unity adapter requires Force Text serialization", path, {}, "asset.import"));
    return Result<std::string>::success({found->second.begin(), found->second.end()});
}

std::optional<std::string> firstMatch(std::string_view text, const std::regex& expression, std::size_t group = 1) {
    std::match_results<std::string_view::const_iterator> match;
    if (!std::regex_search(text.begin(), text.end(), match, expression) || group >= match.size()) return std::nullopt;
    return std::string(match[group].first, match[group].second);
}

std::optional<std::uint64_t> parseUnsignedText(std::string_view text) {
    std::uint64_t value  = 0;
    const auto    result = std::from_chars(text.data(), text.data() + text.size(), value);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size() ? std::optional<std::uint64_t>(value)
                                                                               : std::nullopt;
}

Result<float> parseFloat(std::string_view text, std::string path) {
    std::string owned(text);
    char*       end   = nullptr;
    errno             = 0;
    const float value = std::strtof(owned.c_str(), &end);
    if (errno != 0 || end != owned.c_str() + owned.size() || !std::isfinite(value))
        return Result<float>::failure(Diagnostic::error(DiagnosticCode::ParseError, "Unity numeric value is invalid",
                                                        std::move(path), {}, "asset.import"));
    return Result<float>::success(value);
}

const Value* member(const Value::Object& object, std::string_view name) {
    const auto found = object.find(std::string(name));
    return found == object.end() ? nullptr : &found->second;
}

std::string terrainDetailsPath(const UnityProjectImportRequest& request) {
    if (!request.terrainDetailsPath.empty()) return request.terrainDetailsPath;
    if (request.terrainDataPath.empty()) return {};
    const auto adjacent = request.terrainDataPath + ".eve-details.json";
    return request.files.contains(adjacent) ? adjacent : std::string{};
}

Result<void> appendTerrainDetailInstances(const UnityProjectImportRequest& request, CanonicalTerrainInput& terrain,
                                          std::vector<ImportFinding>& findings) {
    const auto detailsPath = terrainDetailsPath(request);
    if (detailsPath.empty()) return Result<void>::success();
    auto text = textFile(request, detailsPath);
    if (!text) return Result<void>::failure(text.status());
    auto parsed = Value::fromJson(text.value());
    if (!parsed) return Result<void>::failure(parsed.status());
    const auto*  root            = parsed.value().getIf<Value::Object>();
    const Value* schema          = root ? member(*root, "schema") : nullptr;
    const Value* version         = root ? member(*root, "schemaVersion") : nullptr;
    const Value* instancesValue  = root ? member(*root, "instances") : nullptr;
    const Value* prototypesValue = root ? member(*root, "prototypes") : nullptr;
    const auto*  instances       = instancesValue ? instancesValue->getIf<Value::Array>() : nullptr;
    const auto*  prototypes      = prototypesValue ? prototypesValue->getIf<Value::Array>() : nullptr;
    if (!schema || !schema->isString() || schema->asString() != "eve.unity-terrain-details" || !version ||
        !version->isInt64() || (version->asInt() != 1 && version->asInt() != 2 && version->asInt() != 3) ||
        !instances || !prototypes)
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::Unsupported, "terrain detail sidecar must use eve.unity-terrain-details/1, /2 or /3",
            detailsPath, {}, "asset.import"));
    if (instances->size() > request.limits.maximumAssets || prototypes->size() > request.limits.maximumAssets ||
        terrain.instances.size() > request.limits.maximumAssets - instances->size())
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "terrain detail instance count exceeds import budget",
                                                       detailsPath, {}, "asset.import"));
    auto number = [&](const Value::Object& object, std::string_view name, std::size_t index) -> Result<float> {
        const Value* value = member(object, name);
        if (!value || !value->isNumeric())
            return Result<float>::failure(Diagnostic::error(
                DiagnosticCode::ParseError, "terrain detail number is invalid",
                "$.prototypes[" + std::to_string(index) + "]." + std::string(name), {}, "asset.import"));
        const double parsed = value->isInt64() ? double(value->asInt()) : value->asDouble();
        if (!std::isfinite(parsed) || parsed < -std::numeric_limits<float>::max() ||
            parsed > std::numeric_limits<float>::max())
            return Result<float>::failure(Diagnostic::error(
                DiagnosticCode::ParseError, "terrain detail number is non-finite",
                "$.prototypes[" + std::to_string(index) + "]." + std::string(name), {}, "asset.import"));
        return Result<float>::success(static_cast<float>(parsed));
    };
    if (version->asInt() == 3) {
        const Value* windValue = member(*root, "wavingGrass");
        const auto*  wind      = windValue ? windValue->getIf<Value::Object>() : nullptr;
        auto rootNumber = [&](std::string_view name) -> Result<float> {
            const Value* value = wind ? member(*wind, name) : nullptr;
            if (!value || !value->isNumeric())
                return Result<float>::failure(
                    Diagnostic::error(DiagnosticCode::ParseError, "terrain waving grass number is invalid",
                                              "$.wavingGrass." + std::string(name), {}, "asset.import"));
            const double parsed = value->isInt64() ? double(value->asInt()) : value->asDouble();
            if (!std::isfinite(parsed) || parsed < 0.0 || parsed > std::numeric_limits<float>::max())
                return Result<float>::failure(Diagnostic::error(
                    DiagnosticCode::InvalidArgument, "terrain waving grass number must be finite and nonnegative",
                    "$.wavingGrass." + std::string(name), {}, "asset.import"));
            return Result<float>::success(static_cast<float>(parsed));
        };
        auto amount = rootNumber("amount");
        auto speed = rootNumber("speed");
        auto strength = rootNumber("strength");
        const Value* tintValue = wind ? member(*wind, "tint") : nullptr;
        const auto* tint = tintValue ? tintValue->getIf<Value::Array>() : nullptr;
        if (!amount || !speed || !strength || !tint || tint->size() != 4)
            return Result<void>::failure(
                !amount     ? amount.status()
                : !speed    ? speed.status()
                : !strength ? strength.status()
                            : Result<void>::failure(Diagnostic::error(DiagnosticCode::ParseError,
                                                                      "terrain waving grass tint is invalid",
                                                                      "$.wavingGrass.tint", {}, "asset.import"))
                                  .status());
        for (std::size_t component = 0; component < 4; ++component) {
            if (!(*tint)[component].isNumeric())
                return Result<void>::failure(Diagnostic::error(DiagnosticCode::ParseError,
                                                               "terrain waving grass tint is invalid",
                                                               "$.wavingGrass.tint", {}, "asset.import"));
            const double value = (*tint)[component].isInt64() ? double((*tint)[component].asInt())
                                                               : (*tint)[component].asDouble();
            if (!std::isfinite(value) || value < 0.0 || value > 1.0)
                return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                               "terrain waving grass tint must be normalized RGBA",
                                                               "$.wavingGrass.tint", {}, "asset.import"));
            terrain.wavingGrassTint[component] = static_cast<float>(value);
        }
        terrain.hasWavingGrass = true;
        terrain.wavingGrassAmount = amount.value();
        terrain.wavingGrassSpeed = speed.value();
        terrain.wavingGrassStrength = strength.value();
    }
    std::vector<CanonicalTerrainDetailPrototype> detachedPrototypes;
    detachedPrototypes.reserve(prototypes->size());
    std::set<std::string> prototypeIds;
    for (std::size_t index = 0; index < prototypes->size(); ++index) {
        const auto*  object     = (*prototypes)[index].getIf<Value::Object>();
        const Value* id         = object ? member(*object, "prototype") : nullptr;
        const Value* mode       = object ? member(*object, "renderMode") : nullptr;
        const Value* mesh       = object ? member(*object, "usePrototypeMesh") : nullptr;
        const Value* instancing = object ? member(*object, "useInstancing") : nullptr;
        const Value* seed       = object ? member(*object, "noiseSeed") : nullptr;
        if (!object || !id || !id->isString() || id->asString().empty() ||
            id->asString().size() > request.limits.maximumStringBytes ||
            !isValidUtf8(id->asString(), Utf8NullPolicy::Reject) || !prototypeIds.emplace(id->asString()).second ||
            !mode || !mode->isString() ||
            (mode->asString() != "GrassBillboard" && mode->asString() != "Grass" && mode->asString() != "VertexLit") ||
            !mesh || !mesh->isBool() || !instancing || !instancing->isBool() || !seed || !seed->isInt64())
            return Result<void>::failure(
                Diagnostic::error(DiagnosticCode::ParseError, "terrain detail prototype metadata is invalid",
                                  "$.prototypes[" + std::to_string(index) + "]", {}, "asset.import"));
        CanonicalTerrainDetailPrototype prototype;
        prototype.prototype        = id->asString();
        prototype.renderMode       = mode->asString();
        prototype.usePrototypeMesh = mesh->asBool();
        prototype.useInstancing    = instancing->asBool();
        prototype.noiseSeed        = seed->asInt();
        const std::string texturePrefix = "unity-texture-guid:";
        const std::string objectPrefix  = "unity-guid:";
        if (!prototype.usePrototypeMesh && prototype.prototype.starts_with(texturePrefix)) {
            const auto guid = unity_detail::foldAscii(prototype.prototype.substr(texturePrefix.size()));
            auto resource = detail::assetRef(request.package.packageId.child("unity:" + guid).child("image:default"));
            if (!resource) return Result<void>::failure(resource.status());
            prototype.resourceAsset = resource.value().format();
        } else if (prototype.usePrototypeMesh && prototype.prototype.starts_with(objectPrefix)) {
            const auto guid = unity_detail::foldAscii(prototype.prototype.substr(objectPrefix.size()));
            auto resource = detail::assetRef(request.package.packageId.child("unity-prefab:" + guid));
            if (!resource) return Result<void>::failure(resource.status());
            prototype.resourceAsset = resource.value().format();
        } else {
            return Result<void>::failure(
                Diagnostic::error(DiagnosticCode::ParseError, "terrain detail prototype resource kind is inconsistent",
                                  "$.prototypes[" + std::to_string(index) + "].prototype", {}, "asset.import"));
        }
        auto minWidth              = number(*object, "minWidth", index);
        auto maxWidth              = number(*object, "maxWidth", index);
        auto minHeight             = number(*object, "minHeight", index);
        auto maxHeight             = number(*object, "maxHeight", index);
        auto noiseSpread           = number(*object, "noiseSpread", index);
        auto density               = number(*object, "density", index);
        auto align                 = number(*object, "alignToGround", index);
        auto jitter                = number(*object, "positionJitter", index);
        if (!minWidth || !maxWidth || !minHeight || !maxHeight || !noiseSpread || !density || !align || !jitter)
            return Result<void>::failure(!minWidth      ? minWidth.status()
                                         : !maxWidth    ? maxWidth.status()
                                         : !minHeight   ? minHeight.status()
                                         : !maxHeight   ? maxHeight.status()
                                         : !noiseSpread ? noiseSpread.status()
                                         : !density     ? density.status()
                                         : !align       ? align.status()
                                                        : jitter.status());
        prototype.minWidth       = minWidth.value();
        prototype.maxWidth       = maxWidth.value();
        prototype.minHeight      = minHeight.value();
        prototype.maxHeight      = maxHeight.value();
        prototype.noiseSpread    = noiseSpread.value();
        prototype.density        = density.value();
        prototype.alignToGround  = align.value();
        prototype.positionJitter = jitter.value();
        if (version->asInt() >= 2) {
            auto color = [&](std::string_view name, std::array<float, 4>& output) -> Result<void> {
                const Value* value = member(*object, name);
                const auto* array = value ? value->getIf<Value::Array>() : nullptr;
                if (!array || array->size() != 4)
                    return Result<void>::failure(Diagnostic::error(
                        DiagnosticCode::ParseError, "terrain detail color is invalid",
                        "$.prototypes[" + std::to_string(index) + "]." + std::string(name), {}, "asset.import"));
                for (std::size_t component = 0; component < 4; ++component) {
                    if (!(*array)[component].isNumeric())
                        return Result<void>::failure(Diagnostic::error(
                            DiagnosticCode::ParseError, "terrain detail color is invalid", {}, {}, "asset.import"));
                    const double value = (*array)[component].isInt64() ? double((*array)[component].asInt())
                                                                       : (*array)[component].asDouble();
                    if (!std::isfinite(value) || value < 0.0 || value > 1.0)
                        return Result<void>::failure(Diagnostic::error(
                            DiagnosticCode::InvalidArgument, "terrain detail color must be finite normalized RGBA", {},
                            {}, "asset.import"));
                    output[component] = static_cast<float>(value);
                }
                return Result<void>::success();
            };
            auto healthy = color("healthyColor", prototype.healthyColor);
            auto dry = color("dryColor", prototype.dryColor);
            auto bend = number(*object, "bendFactor", index);
            auto padding = number(*object, "holeEdgePadding", index);
            const Value* densityScaling = member(*object, "useDensityScaling");
            if (!healthy || !dry || !bend || !padding || !densityScaling || !densityScaling->isBool())
                return Result<void>::failure(
                    !healthy   ? healthy.status()
                    : !dry     ? dry.status()
                    : !bend    ? bend.status()
                    : !padding ? padding.status()
                               : Result<void>::failure(Diagnostic::error(DiagnosticCode::ParseError,
                                                                         "terrain detail density scaling is invalid",
                                                                         {}, {}, "asset.import"))
                                     .status());
            prototype.bendFactor = bend.value();
            prototype.holeEdgePadding = padding.value();
            prototype.useDensityScaling = densityScaling->asBool();
        }
        detachedPrototypes.push_back(std::move(prototype));
    }
    auto vector = [&](const Value::Object& object, std::string_view name, std::size_t count, float* output,
                      std::size_t index) -> Result<void> {
        const Value* value = member(object, name);
        const auto*  array = value ? value->getIf<Value::Array>() : nullptr;
        if (!array || array->size() != count)
            return Result<void>::failure(Diagnostic::error(
                DiagnosticCode::ParseError, "terrain detail vector is invalid",
                "$.instances[" + std::to_string(index) + "]." + std::string(name), {}, "asset.import"));
        for (std::size_t component = 0; component < count; ++component) {
            if (!(*array)[component].isNumeric())
                return Result<void>::failure(Diagnostic::error(
                    DiagnosticCode::ParseError, "terrain detail vector is non-numeric",
                    "$.instances[" + std::to_string(index) + "]." + std::string(name), {}, "asset.import"));
            const double number =
                (*array)[component].isInt64() ? double((*array)[component].asInt()) : (*array)[component].asDouble();
            if (!std::isfinite(number) || number < -std::numeric_limits<float>::max() ||
                number > std::numeric_limits<float>::max())
                return Result<void>::failure(Diagnostic::error(
                    DiagnosticCode::ParseError, "terrain detail vector is non-finite",
                    "$.instances[" + std::to_string(index) + "]." + std::string(name), {}, "asset.import"));
            output[component] = static_cast<float>(number);
        }
        return Result<void>::success();
    };
    std::vector<CanonicalTerrainInstance> detached;
    detached.reserve(instances->size());
    for (std::size_t index = 0; index < instances->size(); ++index) {
        const auto*  object    = (*instances)[index].getIf<Value::Object>();
        const Value* prototype = object ? member(*object, "prototype") : nullptr;
        if (!object || !prototype || !prototype->isString() || prototype->asString().empty() ||
            prototype->asString().size() > request.limits.maximumStringBytes ||
            !isValidUtf8(prototype->asString(), Utf8NullPolicy::Reject))
            return Result<void>::failure(
                Diagnostic::error(DiagnosticCode::ParseError, "terrain detail prototype is invalid",
                                  "$.instances[" + std::to_string(index) + "].prototype", {}, "asset.import"));
        if (!prototypeIds.contains(prototype->asString()))
            return Result<void>::failure(
                Diagnostic::error(DiagnosticCode::NotFound, "terrain detail instance prototype is undeclared",
                                  "$.instances[" + std::to_string(index) + "].prototype", {}, "asset.import"));
        CanonicalTerrainInstance instance;
        instance.prototype = prototype->asString();
        auto position      = vector(*object, "position", 3, instance.position, index);
        auto rotation      = vector(*object, "rotation", 4, instance.rotation, index);
        auto scale         = vector(*object, "scale", 3, instance.scale, index);
        if (!position || !rotation || !scale)
            return Result<void>::failure(!position   ? position.status()
                                         : !rotation ? rotation.status()
                                                     : scale.status());
        detached.push_back(std::move(instance));
    }
    terrain.detailPrototypes.insert(terrain.detailPrototypes.end(), std::make_move_iterator(detachedPrototypes.begin()),
                                    std::make_move_iterator(detachedPrototypes.end()));
    terrain.instances.insert(terrain.instances.end(), std::make_move_iterator(detached.begin()),
                             std::make_move_iterator(detached.end()));
    findings.push_back({detailsPath, "TerrainData.detail-instances", ImportDisposition::Translated,
                        "Unity public ComputeDetailInstanceTransforms output imported as canonical instances"});
    return Result<void>::success();
}

std::optional<std::string> guidFromMeta(const UnityProjectImportRequest& request, const std::string& assetPath) {
    const auto found = request.files.find(assetPath + ".meta");
    if (found == request.files.end()) return std::nullopt;
    const std::string text(found->second.begin(), found->second.end());
    auto              guid = firstMatch(text, std::regex(R"((?:^|\n)guid:\s*([0-9a-fA-F]{32})(?:\r?\n|\r?$))"));
    if (guid) *guid = unity_detail::foldAscii(std::move(*guid));
    return guid;
}

std::map<std::string, std::string> guidPaths(const UnityProjectImportRequest& request) {
    std::map<std::string, std::string> result;
    for (const auto& [path, bytes] : request.files) {
        if (!path.ends_with(".meta")) continue;
        const std::string text(bytes.begin(), bytes.end());
        auto              guid = firstMatch(text, std::regex(R"((?:^|\n)guid:\s*([0-9a-fA-F]{32})(?:\r?\n|\r?$))"));
        if (guid) result.emplace(unity_detail::foldAscii(*guid), path.substr(0, path.size() - 5));
    }
    return result;
}

std::vector<std::string> sectionEntries(std::string_view yaml, std::string_view heading) {
    const auto start = yaml.find(heading);
    if (start == std::string_view::npos) return {};
    const auto lineEnd = yaml.find('\n', start);
    if (lineEnd == std::string_view::npos) return {};
    std::vector<std::string> entries;
    std::string              current;
    std::istringstream       lines(std::string(yaml.substr(lineEnd + 1)));
    std::string              line;
    while (std::getline(lines, line)) {
        if (line.starts_with("  m_") && !line.starts_with("    ")) break;
        if (line.starts_with("  - ") || line.starts_with("    - ")) {
            if (!current.empty()) entries.push_back(std::move(current));
            current = line + "\n";
        } else if (!current.empty()) {
            current += line + "\n";
        }
    }
    if (!current.empty()) entries.push_back(std::move(current));
    return entries;
}

Result<CanonicalTerrainInput> parseTerrain(const UnityProjectImportRequest&          request,
                                           const std::map<std::string, std::string>& paths,
                                           std::vector<ImportFinding>&               findings) {
    auto textResult = textFile(request, request.terrainDataPath);
    if (!textResult) return Result<CanonicalTerrainInput>::failure(textResult.status());
    const std::string yaml = std::move(textResult).takeValue();
    auto resolutionText    = firstMatch(yaml, std::regex(R"((?:m_HeightmapResolution|m_Resolution):\s*([0-9]+))"));
    auto scaleMatchX =
        firstMatch(yaml, std::regex(R"(m_HeightmapScale:\s*\{x:\s*([^,]+),\s*y:\s*([^,]+),\s*z:\s*([^}]+)\})"), 1);
    auto scaleMatchY =
        firstMatch(yaml, std::regex(R"(m_HeightmapScale:\s*\{x:\s*([^,]+),\s*y:\s*([^,]+),\s*z:\s*([^}]+)\})"), 2);
    auto scaleMatchZ =
        firstMatch(yaml, std::regex(R"(m_HeightmapScale:\s*\{x:\s*([^,]+),\s*y:\s*([^,]+),\s*z:\s*([^}]+)\})"), 3);
    auto heightsHex = firstMatch(yaml, std::regex(R"(m_Heights:\s*([0-9a-fA-F]+))"));
    if (!resolutionText || !scaleMatchX || !scaleMatchY || !scaleMatchZ || !heightsHex)
        return Result<CanonicalTerrainInput>::failure(
            Diagnostic::error(DiagnosticCode::ParseError, "Unity TerrainData heightmap fields are incomplete",
                              request.terrainDataPath, {}, "asset.import"));
    std::uint64_t resolution = 0;
    for (char digit : *resolutionText) {
        if (resolution > (std::numeric_limits<std::uint32_t>::max() - (digit - '0')) / 10)
            return Result<CanonicalTerrainInput>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "Unity terrain resolution overflows", {}, {}, "asset.import"));
        resolution = resolution * 10 + (digit - '0');
    }
    if (resolution < 2 || resolution > request.limits.maximumVerticesPerPrimitive ||
        resolution > std::numeric_limits<std::uint64_t>::max() / resolution)
        return Result<CanonicalTerrainInput>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "Unity terrain resolution is outside limits", {}, {}, "asset.import"));
    const std::uint64_t samples = resolution * resolution;
    if (heightsHex->size() != samples * 4)
        return Result<CanonicalTerrainInput>::failure(Diagnostic::error(
            DiagnosticCode::ParseError, "Unity m_Heights must contain one little-endian UInt16 per sample", {}, {},
            "asset.import"));
    auto spacingX = parseFloat(*scaleMatchX, "m_HeightmapScale.x");
    auto scaleY   = parseFloat(*scaleMatchY, "m_HeightmapScale.y");
    auto spacingZ = parseFloat(*scaleMatchZ, "m_HeightmapScale.z");
    if (!spacingX || !scaleY || !spacingZ)
        return Result<CanonicalTerrainInput>::failure(
            Diagnostic::error(DiagnosticCode::ParseError, "Unity terrain scale is invalid", {}, {}, "asset.import"));
    const auto nibble = [](char value) -> int {
        if (value >= '0' && value <= '9') return value - '0';
        if (value >= 'a' && value <= 'f') return value - 'a' + 10;
        if (value >= 'A' && value <= 'F') return value - 'A' + 10;
        return -1;
    };
    std::vector<std::uint16_t> sourceHeights(samples);
    for (std::size_t index = 0; index < samples; ++index) {
        const std::size_t at = index * 4;
        sourceHeights[index] =
            static_cast<std::uint16_t>((nibble((*heightsHex)[at]) << 4) | nibble((*heightsHex)[at + 1]) |
                                       (nibble((*heightsHex)[at + 2]) << 12) | (nibble((*heightsHex)[at + 3]) << 8));
    }
    CanonicalTerrainInput terrain;
    const auto            imageAsset = [&](std::string_view guid) {
        return std::string("asset://") +
               request.package.packageId.child("unity:" + unity_detail::foldAscii(std::string(guid)))
                   .child("image:default")
                   .format();
    };
    terrain.width = terrain.height = static_cast<std::uint32_t>(resolution);
    terrain.spacingX               = spacingX.value();
    terrain.spacingZ               = spacingZ.value();
    terrain.heightsMeters.resize(samples);
    // Unity rows advance +Z; canonical forward is -Z, so reverse rows while preserving winding.
    for (std::uint32_t row = 0; row < terrain.height; ++row)
        for (std::uint32_t column = 0; column < terrain.width; ++column)
            terrain.heightsMeters[std::size_t(row) * terrain.width + column] =
                float(sourceHeights[std::size_t(terrain.height - 1 - row) * terrain.width + column]) / 65535.0f *
                scaleY.value();
    terrain.sourceTransform = {{"source", Value("unity-left-handed-x-right-y-up-z-forward")},
                               {"conversion", Value("reflect-z-and-reverse-height-rows")},
                               {"heightScaleMeters", Value(double(scaleY.value()))}};

    if (auto holes = firstMatch(yaml, std::regex(R"(m_HolesTexture:.*guid:\s*([0-9a-fA-F]{32}))"))) {
        terrain.holesSource = "unity-guid:" + unity_detail::foldAscii(*holes);
        terrain.holesAsset  = imageAsset(*holes);
    }
    const auto controls = sectionEntries(yaml, "m_AlphamapTextures:");
    if (controls.size() > terrain.controlSources.size())
        return Result<CanonicalTerrainInput>::failure(
            Diagnostic::error(DiagnosticCode::Unsupported, "TVE terrain supports at most four control textures",
                              request.terrainDataPath, {}, "asset.import"));
    for (std::size_t index = 0; index < controls.size(); ++index) {
        if (auto guid = firstMatch(controls[index], std::regex(R"(guid:\s*([0-9a-fA-F]{32}))"))) {
            terrain.controlSources[index] = "unity-guid:" + unity_detail::foldAscii(*guid);
            terrain.controlAssets[index]  = imageAsset(*guid);
        }
    }
    for (const auto& entry : sectionEntries(yaml, "m_TerrainLayers:")) {
        auto guid = firstMatch(entry, std::regex(R"(guid:\s*([0-9a-fA-F]{32}))"));
        if (!guid) continue;
        CanonicalTerrainLayer layer;
        const auto            resolved = paths.find(unity_detail::foldAscii(*guid));
        layer.name                     = resolved == paths.end() ? *guid : resolved->second;
        if (resolved != paths.end()) {
            auto layerText = textFile(request, resolved->second);
            if (layerText) {
                if (auto diffuse =
                        firstMatch(layerText.value(), std::regex(R"(m_DiffuseTexture:.*guid:\s*([0-9a-fA-F]{32}))"))) {
                    layer.diffuseSource = "unity-guid:" + *diffuse;
                    layer.diffuseAsset  = imageAsset(*diffuse);
                }
                if (auto normal = firstMatch(layerText.value(),
                                             std::regex(R"(m_NormalMapTexture:.*guid:\s*([0-9a-fA-F]{32}))"))) {
                    layer.normalSource = "unity-guid:" + *normal;
                    layer.normalAsset  = imageAsset(*normal);
                }
                if (auto mask =
                        firstMatch(layerText.value(), std::regex(R"(m_MaskMapTexture:.*guid:\s*([0-9a-fA-F]{32}))"))) {
                    layer.maskSource = "unity-guid:" + *mask;
                    layer.maskAsset  = imageAsset(*mask);
                }
                auto parseVec = [&](const char* name, auto& output) -> Result<void> {
                    const std::string expression =
                        std::string(name) + R"(:\s*\{x:\s*([^,]+),\s*y:\s*([^,]+),\s*z:\s*([^,]+),\s*w:\s*([^}]+)\})";
                    std::match_results<std::string::const_iterator> match;
                    if (!std::regex_search(layerText.value(), match, std::regex(expression)))
                        return Result<void>::success();
                    for (std::size_t component = 0; component < 4; ++component) {
                        auto parsed = parseFloat(match[component + 1].str(), resolved->second + "." + name);
                        if (!parsed) return Result<void>::failure(parsed.status());
                        output[component] = parsed.value();
                    }
                    return Result<void>::success();
                };
                for (auto [name, value] : {std::pair{"m_MaskMapRemapMin", &layer.maskRemapMinimum},
                                           {"m_MaskMapRemapMax", &layer.maskRemapMaximum},
                                           {"m_Specular", &layer.specular}}) {
                    auto parsed = parseVec(name, *value);
                    if (!parsed) return Result<CanonicalTerrainInput>::failure(parsed.status());
                }
                const std::regex tilePattern(R"(m_TileSize:\s*\{x:\s*([^,]+),\s*y:\s*([^}]+)\})");
                std::match_results<std::string::const_iterator> tileMatch;
                if (std::regex_search(layerText.value(), tileMatch, tilePattern)) {
                    auto x = parseFloat(tileMatch[1].str(), resolved->second + ".m_TileSize.x");
                    auto y = parseFloat(tileMatch[2].str(), resolved->second + ".m_TileSize.y");
                    if (!x || !y)
                        return Result<CanonicalTerrainInput>::failure(Diagnostic::error(
                            DiagnosticCode::ParseError, "Unity terrain tile size is invalid", {}, {}, "asset.import"));
                    layer.tileScaleMeters = {x.value(), y.value()};
                    layer.tileSizeMeters  = x.value();
                }
                const std::regex offsetPattern(R"(m_TileOffset:\s*\{x:\s*([^,]+),\s*y:\s*([^}]+)\})");
                std::match_results<std::string::const_iterator> offsetMatch;
                if (std::regex_search(layerText.value(), offsetMatch, offsetPattern)) {
                    auto x = parseFloat(offsetMatch[1].str(), resolved->second + ".m_TileOffset.x");
                    auto y = parseFloat(offsetMatch[2].str(), resolved->second + ".m_TileOffset.y");
                    if (!x || !y)
                        return Result<CanonicalTerrainInput>::failure(
                            Diagnostic::error(DiagnosticCode::ParseError, "Unity terrain tile offset is invalid", {},
                                              {}, "asset.import"));
                    layer.tileOffsetMeters = {x.value(), y.value()};
                }
                for (auto [name, target] : {std::pair{"m_Metallic", &layer.metallic},
                                            {"m_NormalScale", &layer.normalScale},
                                            {"m_Smoothness", &layer.smoothness}}) {
                    if (auto encoded =
                            firstMatch(layerText.value(), std::regex(std::string(name) + R"(:\s*([^\r\n]+))"))) {
                        auto parsed = parseFloat(*encoded, resolved->second + "." + name);
                        if (!parsed) return Result<CanonicalTerrainInput>::failure(parsed.status());
                        *target = parsed.value();
                    }
                }
            }
        }
        terrain.layers.push_back(std::move(layer));
    }

    std::vector<std::string> prototypes;
    for (const auto& entry : sectionEntries(yaml, "m_TreePrototypes:")) {
        auto guid = firstMatch(entry, std::regex(R"(guid:\s*([0-9a-fA-F]{32}))"));
        prototypes.push_back(guid ? "unity-guid:" + *guid : "unity-missing-prototype");
    }
    for (const auto& entry : sectionEntries(yaml, "m_TreeInstances:")) {
        auto position  = std::regex(R"(position:\s*\{x:\s*([^,]+),\s*y:\s*([^,]+),\s*z:\s*([^}]+)\})");
        auto x         = firstMatch(entry, position, 1);
        auto y         = firstMatch(entry, position, 2);
        auto z         = firstMatch(entry, position, 3);
        auto prototype = firstMatch(entry, std::regex(R"(prototypeIndex:\s*([0-9]+))"));
        if (!x || !y || !z || !prototype) continue;
        const auto prototypeValue = parseUnsignedText(*prototype);
        if (!prototypeValue || *prototypeValue >= prototypes.size())
            return Result<CanonicalTerrainInput>::failure(Diagnostic::error(
                DiagnosticCode::ParseError, "Unity tree prototype index is invalid", {}, {}, "asset.import"));
        const std::size_t prototypeIndex = static_cast<std::size_t>(*prototypeValue);
        auto              px             = parseFloat(*x, "tree.position.x");
        auto              py             = parseFloat(*y, "tree.position.y");
        auto              pz             = parseFloat(*z, "tree.position.z");
        if (!px || !py || !pz)
            return Result<CanonicalTerrainInput>::failure(Diagnostic::error(
                DiagnosticCode::ParseError, "Unity tree position is invalid", {}, {}, "asset.import"));
        CanonicalTerrainInstance instance;
        instance.prototype   = prototypes[prototypeIndex];
        instance.position[0] = px.value() * spacingX.value() * float(resolution - 1);
        instance.position[1] = py.value() * scaleY.value();
        instance.position[2] = -pz.value() * spacingZ.value() * float(resolution - 1);
        if (auto rotation = firstMatch(entry, std::regex(R"(rotation:\s*([^\r\n]+))"))) {
            auto value = parseFloat(*rotation, "tree.rotation");
            if (value) {
                instance.rotation[1] = std::sin(-value.value() * 0.5f);
                instance.rotation[3] = std::cos(-value.value() * 0.5f);
            }
        }
        if (auto width = firstMatch(entry, std::regex(R"(widthScale:\s*([^\r\n]+))"))) {
            auto value = parseFloat(*width, "tree.widthScale");
            if (value) instance.scale[0] = instance.scale[2] = value.value();
        }
        if (auto height = firstMatch(entry, std::regex(R"(heightScale:\s*([^\r\n]+))"))) {
            auto value = parseFloat(*height, "tree.heightScale");
            if (value) instance.scale[1] = value.value();
        }
        terrain.instances.push_back(std::move(instance));
    }
    if (yaml.find("m_DetailPrototypes:") != std::string::npos) {
        const bool hasDetailSidecar = !terrainDetailsPath(request).empty();
        findings.push_back(
            {request.terrainDataPath, "TerrainData.detail-prototypes",
             hasDetailSidecar ? ImportDisposition::Translated : ImportDisposition::PreservedSource,
             !hasDetailSidecar
                 ? "detail data requires an eve.unity-terrain-details/3 public-API export"
                 : "detail prototypes and placement are supplied by the public-API sidecar"});
    }
    findings.push_back({request.terrainDataPath, "TerrainData.heightmap", ImportDisposition::Translated,
                        "UInt16 heights converted to metres and canonical Z"});
    return Result<CanonicalTerrainInput>::success(std::move(terrain));
}

struct UnityObject {
    std::int64_t  fileId  = 0;
    std::uint32_t classId = 0;
    std::string   body;
};

std::vector<UnityObject> documents(std::string_view yaml) {
    const std::regex header(R"(^---\s*!u!([0-9]+)\s*&(-?[0-9]+).*$)", std::regex::multiline);
    struct Start {
        std::size_t header;
        std::size_t body;
        UnityObject object;
    };
    std::vector<Start> starts;
    for (std::cregex_iterator it(yaml.data(), yaml.data() + yaml.size(), header), end; it != end; ++it) {
        const std::string signedText = (*it)[2].str();
        std::int64_t      signedId   = 0;
        const auto parsedId  = std::from_chars(signedText.data(), signedText.data() + signedText.size(), signedId);
        const auto classText = (*it)[1].str();
        const auto classId   = parseUnsignedText(classText);
        if (parsedId.ec != std::errc{} || parsedId.ptr != signedText.data() + signedText.size() || !classId ||
            *classId > std::numeric_limits<std::uint32_t>::max())
            continue;
        starts.push_back({static_cast<std::size_t>((*it).position()),
                          static_cast<std::size_t>((*it).position() + (*it).length()),
                          {signedId, static_cast<std::uint32_t>(*classId), {}}});
    }
    std::vector<UnityObject> result;
    for (std::size_t index = 0; index < starts.size(); ++index) {
        const std::size_t end     = index + 1 < starts.size() ? starts[index + 1].header : yaml.size();
        starts[index].object.body = std::string(yaml.substr(starts[index].body, end - starts[index].body));
        result.push_back(std::move(starts[index].object));
    }
    return result;
}

Result<void> appendPrefab(const UnityProjectImportRequest& request, PreparedAssetImport& output,
                          std::vector<ImportFinding>& findings, const std::string& prefabPath) {
    if (prefabPath.empty()) return Result<void>::success();
    auto text = textFile(request, prefabPath);
    if (!text) return Result<void>::failure(text.status());
    if (std::regex_search(text.value(), std::regex(R"((?:^|\n)---\s*!u!(?:1|4|224)\s*&-)")))
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::Unsupported,
                              "prefab signed hierarchy IDs are indexed but cannot yet be represented by scene-template",
                              prefabPath, {}, "asset.import"));
    const auto prefabGuid = guidFromMeta(request, prefabPath);
    if (!prefabGuid)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::NotFound, "Unity prefab .meta GUID is required",
                                                       prefabPath + ".meta", {}, "asset.import.unity"));
    const auto objects = documents(text.value());
    if (objects.size() > request.limits.maximumAssets)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "Unity prefab object count exceeds budget", prefabPath, {},
                                                       "asset.import"));
    std::map<std::uint64_t, std::string> gameNames;
    std::map<std::uint64_t, bool>        gameVisibility;
    for (const auto& object : objects) {
        if (object.classId == 1) {
            auto name                     = firstMatch(object.body, std::regex(R"(m_Name:\s*([^\r\n]+))"));
            gameNames[object.fileId]      = name.value_or("GameObject");
            gameVisibility[object.fileId] = !std::regex_search(object.body, std::regex(R"(m_IsActive:\s*0(?:\s|$))"));
        } else if (object.classId == 114) {
            findings.push_back({prefabPath, "MonoBehaviour:" + std::to_string(object.fileId),
                                ImportDisposition::Unsupported,
                                "C# behaviour is not executed or translated by the data importer"});
        } else if (object.classId != 4) {
            findings.push_back(
                {prefabPath, "Prefab.component:" + std::to_string(object.classId) + ":" + std::to_string(object.fileId),
                 ImportDisposition::Unsupported,
                 "component behavior is not converted; only GameObject/Transform hierarchy is available"});
        }
    }
    Value::Array nodes;
    for (const auto& object : objects) {
        if (object.classId != 4 && object.classId != 224) continue;
        auto             game   = firstMatch(object.body, std::regex(R"(m_GameObject:\s*\{fileID:\s*(-?[0-9]+)\})"));
        auto             father = firstMatch(object.body, std::regex(R"(m_Father:\s*\{fileID:\s*(-?[0-9]+)\})"));
        const std::regex position(R"(m_LocalPosition:\s*\{x:\s*([^,]+),\s*y:\s*([^,]+),\s*z:\s*([^}]+)\})");
        const std::regex rotation(
            R"(m_LocalRotation:\s*\{x:\s*([^,]+),\s*y:\s*([^,]+),\s*z:\s*([^,]+),\s*w:\s*([^}]+)\})");
        const std::regex scale(R"(m_LocalScale:\s*\{x:\s*([^,]+),\s*y:\s*([^,]+),\s*z:\s*([^}]+)\})");
        if (!game) continue;
        auto vector = [&](const std::regex& regex, std::size_t count, float* target, bool reflect) -> Result<void> {
            for (std::size_t component = 0; component < count; ++component) {
                auto field = firstMatch(object.body, regex, component + 1);
                if (!field)
                    return Result<void>::failure(Diagnostic::error(DiagnosticCode::ParseError,
                                                                   "Unity transform field is missing", prefabPath, {},
                                                                   "asset.import.unity"));
                auto parsed = parseFloat(*field, prefabPath);
                if (!parsed) return Result<void>::failure(parsed.status());
                target[component] = parsed.value();
            }
            if (reflect) target[2] = -target[2];
            return Result<void>::success();
        };
        float p[3], q[4], s[3];
        auto  pResult = vector(position, 3, p, true);
        if (!pResult) return pResult;
        auto qResult = vector(rotation, 4, q, false);
        if (!qResult) return qResult;
        q[0]         = -q[0];
        q[1]         = -q[1];
        auto sResult = vector(scale, 3, s, false);
        if (!sResult) return sResult;
        const auto gameIdValue   = parseUnsignedText(*game);
        const auto parentIdValue = father ? parseUnsignedText(*father) : std::optional<std::uint64_t>(0);
        if (!gameIdValue || !parentIdValue)
            return Result<void>::failure(Diagnostic::error(
                DiagnosticCode::ParseError, "Unity transform fileID overflows", prefabPath, {}, "asset.import.unity"));
        const auto          gameId   = *gameIdValue;
        const std::uint64_t parentId = *parentIdValue;
        Value::Object       node;
        node["objectId"] = Value(
            request.package.packageId.child("unity:" + *prefabGuid + ":" + std::to_string(object.fileId)).format());
        node["sourceFileId"]       = Value(static_cast<std::int64_t>(object.fileId));
        node["name"]               = Value(gameNames.contains(gameId) ? gameNames[gameId] : "GameObject");
        node["visible"]            = Value(!gameVisibility.contains(gameId) || gameVisibility.at(gameId));
        node["parentSourceFileId"] = Value(static_cast<std::int64_t>(parentId));
        node["position"]           = Value(Value::Array{Value(double(p[0])), Value(double(p[1])), Value(double(p[2]))});
        node["rotation"] =
            Value(Value::Array{Value(double(q[0])), Value(double(q[1])), Value(double(q[2])), Value(double(q[3]))});
        node["scale"] = Value(Value::Array{Value(double(s[0])), Value(double(s[1])), Value(double(s[2]))});
        nodes.emplace_back(std::move(node));
    }
    if (nodes.empty())
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::Unsupported,
                              "prefab has no directly convertible Transform nodes; nested prefab expansion is required",
                              prefabPath, {}, "asset.import"));
    const PersistentId sceneId = request.package.packageId.child("unity-prefab:" + *prefabGuid);
    Value::Object      definition;
    definition["schema"]           = Value("eve.scene-template");
    definition["schemaVersion"]    = Value(std::int64_t(3));
    definition["sourceGuid"]       = Value(*prefabGuid);
    definition["nodes"]            = Value(std::move(nodes));
    definition["renderers"]        = Value(Value::Array{});
    definition["coordinateSystem"] = Value("right-handed-x-right-y-up-minus-z-forward");
    auto encoded                   = Value(std::move(definition)).toJson();
    if (!encoded) return Result<void>::failure(encoded.status());
    std::string       definitionText = std::move(encoded).takeValue();
    const std::string path           = "assets/" + sceneId.format() + "/asset.json";
    auto              reference      = detail::assetRef(sceneId);
    if (!reference) return Result<void>::failure(reference.status());
    output.manifest.assets.push_back(
        {std::move(reference).takeValue(),
         "eve.scene-template",
         SchemaVersion(3),
         path,
         detail::sha256(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(definitionText.data()),
                                                      definitionText.size())),
         {"scene", "source:unity-prefab"}});
    output.entries.push_back({path, {definitionText.begin(), definitionText.end()}});
    if (output.manifest.entrypoints.empty()) {
        auto entrypoint = detail::assetRef(sceneId);
        if (!entrypoint) return Result<void>::failure(entrypoint.status());
        output.manifest.entrypoints.emplace("default", std::move(entrypoint).takeValue());
    }
    findings.push_back({prefabPath, "Prefab.GameObject.Transform", ImportDisposition::Translated,
                        "GUID/fileID hierarchy converted to canonical TRS"});
    return Result<void>::success();
}

}  // namespace

Result<PreparedAssetImport> prepareUnityPrefab(const UnityProjectImportRequest& request, const std::string& path) {
    auto manifest = detail::baseManifest(request.package, "eve.unity-prefab/1");
    if (!manifest) return Result<PreparedAssetImport>::failure(manifest.status());
    PreparedAssetImport output;
    output.manifest = std::move(manifest).takeValue();
    auto prefab     = appendPrefab(request, output, output.findings, path);
    if (!prefab) return Result<PreparedAssetImport>::failure(prefab.status());
    for (const auto& asset : output.manifest.assets) output.sourceMappings.push_back({"Prefab#Transform", asset.asset});
    return Result<PreparedAssetImport>::success(std::move(output));
}

Result<PreparedAssetImport> prepareUnityProjectImport(const UnityProjectImportRequest& request) {
    if (request.files.empty() || request.package.packageId.isNil())
        return Result<PreparedAssetImport>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "Unity project files and package identity are required",
                              {}, {}, "asset.import"));
    auto sourceIndex = indexUnitySources(request.files, request.limits);
    if (!sourceIndex) return Result<PreparedAssetImport>::failure(sourceIndex.status());
    if (request.terrainDataPath.empty() && request.prefabPath.empty())
        return prepareUnityCollection(request, sourceIndex.value());
    std::vector<ImportFinding> findings = sourceIndex.value().findings;
    PreparedAssetImport        output;
    if (!request.terrainDataPath.empty()) {
        auto terrain = parseTerrain(request, guidPaths(request), findings);
        if (!terrain) return Result<PreparedAssetImport>::failure(terrain.status());
        auto details = appendTerrainDetailInstances(request, terrain.value(), findings);
        if (!details) return Result<PreparedAssetImport>::failure(details.status());
        auto prepared =
            prepareCanonicalTerrainImport(request.package, terrain.value(), "eve.unity-terrain/1", request.limits);
        if (!prepared) return Result<PreparedAssetImport>::failure(prepared.status());
        output = std::move(prepared).takeValue();
        if (!terrain.value().detailPrototypes.empty()) {
            auto collection = prepareUnityCollection(request, sourceIndex.value());
            if (collection) {
                std::set<PersistentId> selected;
                for (const auto& prototype : terrain.value().detailPrototypes) {
                    auto resource = AssetRef::parse(prototype.resourceAsset);
                    if (resource) selected.emplace(resource.value().id());
                }
                bool changed = true;
                while (changed) {
                    changed = false;
                    for (const auto& dependency : collection.value().manifest.dependencies)
                        if (selected.contains(dependency.from.id()) && selected.emplace(dependency.to.id()).second)
                            changed = true;
                }
                std::set<std::string> runtimePrefixes;
                for (const auto& asset : collection.value().manifest.assets) {
                    if (!selected.contains(asset.asset.id())) continue;
                    const bool duplicate = std::any_of(output.manifest.assets.begin(), output.manifest.assets.end(),
                                                       [&](const auto& existing) {
                                                           return existing.asset.id() == asset.asset.id();
                                                       });
                    if (!duplicate) output.manifest.assets.push_back(asset);
                    runtimePrefixes.emplace("assets/" + asset.asset.id().format() + "/");
                }
                for (const auto& dependency : collection.value().manifest.dependencies)
                    if (selected.contains(dependency.from.id()) && selected.contains(dependency.to.id()))
                        output.manifest.dependencies.push_back(dependency);
                for (const auto& entry : collection.value().entries)
                    if (std::any_of(runtimePrefixes.begin(), runtimePrefixes.end(), [&](const auto& prefix) {
                            return entry.path.starts_with(prefix);
                        }))
                        output.entries.push_back(entry);
            }
        }
    } else {
        auto manifest = detail::baseManifest(request.package, "eve.unity-prefab/1");
        if (!manifest) return Result<PreparedAssetImport>::failure(manifest.status());
        output.manifest = std::move(manifest).takeValue();
    }
    auto prefab = appendPrefab(request, output, findings, request.prefabPath);
    if (!prefab) return Result<PreparedAssetImport>::failure(prefab.status());
    output.manifest.provenance["sourceEngine"]           = Value("unity");
    output.manifest.provenance["sourceCoordinateSystem"] = Value("left-handed-x-right-y-up-z-forward");
    output.findings.insert(output.findings.end(), std::make_move_iterator(findings.begin()),
                           std::make_move_iterator(findings.end()));
    for (const auto& asset : output.manifest.assets) {
        std::string sourceObject = request.terrainDataPath + "#" + asset.type;
        if (asset.type == "eve.scene-template") sourceObject = request.prefabPath + "#Transform";
        output.sourceMappings.push_back({std::move(sourceObject), asset.asset});
    }
    auto report = detail::finalizeImportReport(output, request.package, "unity", "force-text",
                                               {{"serialization", Value("force-text")}});
    if (!report) return Result<PreparedAssetImport>::failure(report.status());
    return Result<PreparedAssetImport>::success(std::move(output));
}

}  // namespace eve::asset_import
