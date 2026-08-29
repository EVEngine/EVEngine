#include "asset_import/UnityImporter.h"

#include "asset_import/ImportCommon.h"

#include <cmath>
#include <charconv>
#include <regex>
#include <sstream>

namespace eve::asset_import {
namespace {

Result<std::string> textFile(const UnityProjectImportRequest& request, const std::string& path) {
    const auto found = request.files.find(path);
    if (found == request.files.end())
        return detail::failure<std::string>(DiagnosticCode::NotFound, "Unity source file was not supplied", path);
    if (found->second.size() > request.limits.maximumSourceBytes)
        return detail::failure<std::string>(DiagnosticCode::InvalidArgument, "Unity source exceeds budget", path);
    if (std::find(found->second.begin(), found->second.end(), std::uint8_t(0)) != found->second.end())
        return detail::failure<std::string>(DiagnosticCode::Unsupported,
                                            "Unity adapter requires Force Text serialization", path);
    return Result<std::string>::success({found->second.begin(), found->second.end()});
}

std::optional<std::string> firstMatch(std::string_view text, const std::regex& expression,
                                      std::size_t group = 1) {
    std::match_results<std::string_view::const_iterator> match;
    if (!std::regex_search(text.begin(), text.end(), match, expression) || group >= match.size()) return std::nullopt;
    return std::string(match[group].first, match[group].second);
}

std::optional<std::uint64_t> parseUnsignedText(std::string_view text) {
    std::uint64_t value = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size()
               ? std::optional<std::uint64_t>(value) : std::nullopt;
}

Result<float> parseFloat(std::string_view text, std::string path) {
    std::string owned(text);
    char* end = nullptr;
    errno = 0;
    const float value = std::strtof(owned.c_str(), &end);
    if (errno != 0 || end != owned.c_str() + owned.size() || !std::isfinite(value))
        return detail::failure<float>(DiagnosticCode::ParseError, "Unity numeric value is invalid", std::move(path));
    return Result<float>::success(value);
}

std::optional<std::string> guidFromMeta(const UnityProjectImportRequest& request, const std::string& assetPath) {
    const auto found = request.files.find(assetPath + ".meta");
    if (found == request.files.end()) return std::nullopt;
    const std::string text(found->second.begin(), found->second.end());
    return firstMatch(text, std::regex(R"((?:^|\n)guid:\s*([0-9a-fA-F]{32})(?:\r?$|\n))"));
}

std::map<std::string, std::string> guidPaths(const UnityProjectImportRequest& request) {
    std::map<std::string, std::string> result;
    for (const auto& [path, bytes] : request.files) {
        if (!path.ends_with(".meta")) continue;
        const std::string text(bytes.begin(), bytes.end());
        auto guid = firstMatch(text, std::regex(R"((?:^|\n)guid:\s*([0-9a-fA-F]{32})(?:\r?$|\n))"));
        if (guid) result.emplace(*guid, path.substr(0, path.size() - 5));
    }
    return result;
}

std::vector<std::string> sectionEntries(std::string_view yaml, std::string_view heading) {
    const auto start = yaml.find(heading);
    if (start == std::string_view::npos) return {};
    const auto lineEnd = yaml.find('\n', start);
    if (lineEnd == std::string_view::npos) return {};
    std::vector<std::string> entries;
    std::string current;
    std::istringstream lines(std::string(yaml.substr(lineEnd + 1)));
    std::string line;
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

Result<CanonicalTerrainInput> parseTerrain(const UnityProjectImportRequest& request,
                                           const std::map<std::string, std::string>& paths,
                                           std::vector<ImportFinding>& findings) {
    auto textResult = textFile(request, request.terrainDataPath);
    if (!textResult) return Result<CanonicalTerrainInput>::failure(textResult.status());
    const std::string yaml = std::move(textResult).takeValue();
    auto resolutionText = firstMatch(yaml, std::regex(R"((?:m_HeightmapResolution|m_Resolution):\s*([0-9]+))"));
    auto scaleMatchX = firstMatch(yaml, std::regex(R"(m_HeightmapScale:\s*\{x:\s*([^,]+),\s*y:\s*([^,]+),\s*z:\s*([^}]+)\})"), 1);
    auto scaleMatchY = firstMatch(yaml, std::regex(R"(m_HeightmapScale:\s*\{x:\s*([^,]+),\s*y:\s*([^,]+),\s*z:\s*([^}]+)\})"), 2);
    auto scaleMatchZ = firstMatch(yaml, std::regex(R"(m_HeightmapScale:\s*\{x:\s*([^,]+),\s*y:\s*([^,]+),\s*z:\s*([^}]+)\})"), 3);
    auto heightsHex = firstMatch(yaml, std::regex(R"(m_Heights:\s*([0-9a-fA-F]+))"));
    if (!resolutionText || !scaleMatchX || !scaleMatchY || !scaleMatchZ || !heightsHex)
        return detail::failure<CanonicalTerrainInput>(DiagnosticCode::ParseError,
                                                      "Unity TerrainData heightmap fields are incomplete",
                                                      request.terrainDataPath);
    std::uint64_t resolution = 0;
    for (char digit : *resolutionText) {
        if (resolution > (std::numeric_limits<std::uint32_t>::max() - (digit - '0')) / 10)
            return detail::failure<CanonicalTerrainInput>(DiagnosticCode::InvalidArgument,
                                                          "Unity terrain resolution overflows");
        resolution = resolution * 10 + (digit - '0');
    }
    if (resolution < 2 || resolution > request.limits.maximumVerticesPerPrimitive ||
        resolution > std::numeric_limits<std::uint64_t>::max() / resolution)
        return detail::failure<CanonicalTerrainInput>(DiagnosticCode::InvalidArgument,
                                                      "Unity terrain resolution is outside limits");
    const std::uint64_t samples = resolution * resolution;
    if (heightsHex->size() != samples * 4)
        return detail::failure<CanonicalTerrainInput>(DiagnosticCode::ParseError,
                                                      "Unity m_Heights must contain one little-endian UInt16 per sample");
    auto spacingX = parseFloat(*scaleMatchX, "m_HeightmapScale.x");
    auto scaleY = parseFloat(*scaleMatchY, "m_HeightmapScale.y");
    auto spacingZ = parseFloat(*scaleMatchZ, "m_HeightmapScale.z");
    if (!spacingX || !scaleY || !spacingZ)
        return detail::failure<CanonicalTerrainInput>(DiagnosticCode::ParseError,
                                                      "Unity terrain scale is invalid");
    const auto nibble = [](char value) -> int {
        if (value >= '0' && value <= '9') return value - '0';
        if (value >= 'a' && value <= 'f') return value - 'a' + 10;
        if (value >= 'A' && value <= 'F') return value - 'A' + 10;
        return -1;
    };
    std::vector<std::uint16_t> sourceHeights(samples);
    for (std::size_t index = 0; index < samples; ++index) {
        const std::size_t at = index * 4;
        sourceHeights[index] = static_cast<std::uint16_t>((nibble((*heightsHex)[at]) << 4) |
                              nibble((*heightsHex)[at + 1]) |
                              (nibble((*heightsHex)[at + 2]) << 12) |
                              (nibble((*heightsHex)[at + 3]) << 8));
    }
    CanonicalTerrainInput terrain;
    terrain.width = terrain.height = static_cast<std::uint32_t>(resolution);
    terrain.spacingX = spacingX.value(); terrain.spacingZ = spacingZ.value();
    terrain.heightsMeters.resize(samples);
    // Unity rows advance +Z; canonical forward is -Z, so reverse rows while preserving winding.
    for (std::uint32_t row = 0; row < terrain.height; ++row)
        for (std::uint32_t column = 0; column < terrain.width; ++column)
            terrain.heightsMeters[std::size_t(row) * terrain.width + column] =
                float(sourceHeights[std::size_t(terrain.height - 1 - row) * terrain.width + column]) /
                65535.0f * scaleY.value();
    terrain.sourceTransform = {{"source", Value("unity-left-handed-x-right-y-up-z-forward")},
                               {"conversion", Value("reflect-z-and-reverse-height-rows")},
                               {"heightScaleMeters", Value(double(scaleY.value()))}};

    for (const auto& entry : sectionEntries(yaml, "m_TerrainLayers:")) {
        auto guid = firstMatch(entry, std::regex(R"(guid:\s*([0-9a-fA-F]{32}))"));
        if (!guid) continue;
        CanonicalTerrainLayer layer;
        const auto resolved = paths.find(*guid);
        layer.name = resolved == paths.end() ? *guid : resolved->second;
        if (resolved != paths.end()) {
            auto layerText = textFile(request, resolved->second);
            if (layerText) {
                if (auto diffuse = firstMatch(layerText.value(), std::regex(R"(m_DiffuseTexture:.*guid:\s*([0-9a-fA-F]{32}))")))
                    layer.diffuseSource = "unity-guid:" + *diffuse;
                if (auto normal = firstMatch(layerText.value(), std::regex(R"(m_NormalMapTexture:.*guid:\s*([0-9a-fA-F]{32}))")))
                    layer.normalSource = "unity-guid:" + *normal;
                if (auto tile = firstMatch(layerText.value(), std::regex(R"(m_TileSize:\s*\{x:\s*([^,]+),)"))) {
                    auto parsed = parseFloat(*tile, resolved->second + ".m_TileSize.x");
                    if (parsed) layer.tileSizeMeters = parsed.value();
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
        auto position = std::regex(R"(position:\s*\{x:\s*([^,]+),\s*y:\s*([^,]+),\s*z:\s*([^}]+)\})");
        auto x = firstMatch(entry, position, 1); auto y = firstMatch(entry, position, 2); auto z = firstMatch(entry, position, 3);
        auto prototype = firstMatch(entry, std::regex(R"(prototypeIndex:\s*([0-9]+))"));
        if (!x || !y || !z || !prototype) continue;
        const auto prototypeValue = parseUnsignedText(*prototype);
        if (!prototypeValue || *prototypeValue >= prototypes.size())
            return detail::failure<CanonicalTerrainInput>(DiagnosticCode::ParseError,
                                                          "Unity tree prototype index is invalid");
        const std::size_t prototypeIndex = static_cast<std::size_t>(*prototypeValue);
        auto px = parseFloat(*x, "tree.position.x"); auto py = parseFloat(*y, "tree.position.y");
        auto pz = parseFloat(*z, "tree.position.z");
        if (!px || !py || !pz) return detail::failure<CanonicalTerrainInput>(DiagnosticCode::ParseError,
                                                                             "Unity tree position is invalid");
        CanonicalTerrainInstance instance;
        instance.prototype = prototypes[prototypeIndex];
        instance.position[0] = px.value() * spacingX.value() * float(resolution - 1);
        instance.position[1] = py.value() * scaleY.value();
        instance.position[2] = -pz.value() * spacingZ.value() * float(resolution - 1);
        if (auto rotation = firstMatch(entry, std::regex(R"(rotation:\s*([^\r\n]+))"))) {
            auto value = parseFloat(*rotation, "tree.rotation");
            if (value) { instance.rotation[1] = std::sin(-value.value() * 0.5f);
                         instance.rotation[3] = std::cos(-value.value() * 0.5f); }
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
    if (yaml.find("m_DetailPrototypes:") != std::string::npos)
        findings.push_back({request.terrainDataPath, "TerrainData.detail-prototypes",
                            ImportDisposition::PreservedSource,
                            "detail prototype metadata is retained but detail density maps require a later adapter"});
    findings.push_back({request.terrainDataPath, "TerrainData.heightmap",
                        ImportDisposition::Translated, "UInt16 heights converted to metres and canonical Z"});
    return Result<CanonicalTerrainInput>::success(std::move(terrain));
}

struct UnityObject {
    std::uint64_t fileId = 0;
    std::uint32_t classId = 0;
    std::string body;
};

std::vector<UnityObject> documents(std::string_view yaml) {
    const std::regex header(R"(^---\s*!u!([0-9]+)\s*&(-?[0-9]+).*$)", std::regex::multiline);
    struct Start { std::size_t header; std::size_t body; UnityObject object; };
    std::vector<Start> starts;
    for (std::cregex_iterator it(yaml.begin(), yaml.end(), header), end; it != end; ++it) {
        const std::string signedText = (*it)[2].str();
        std::int64_t signedId = 0;
        const auto parsedId = std::from_chars(signedText.data(), signedText.data() + signedText.size(), signedId);
        const auto classText = (*it)[1].str();
        const auto classId = parseUnsignedText(classText);
        if (parsedId.ec != std::errc{} || parsedId.ptr != signedText.data() + signedText.size() ||
            signedId < 0 || !classId || *classId > std::numeric_limits<std::uint32_t>::max()) continue;
        starts.push_back({static_cast<std::size_t>((*it).position()),
                          static_cast<std::size_t>((*it).position() + (*it).length()),
                          {static_cast<std::uint64_t>(signedId), static_cast<std::uint32_t>(*classId), {}}});
    }
    std::vector<UnityObject> result;
    for (std::size_t index = 0; index < starts.size(); ++index) {
        const std::size_t end = index + 1 < starts.size() ? starts[index + 1].header : yaml.size();
        starts[index].object.body = std::string(yaml.substr(starts[index].body, end - starts[index].body));
        result.push_back(std::move(starts[index].object));
    }
    return result;
}

Result<void> appendPrefab(const UnityProjectImportRequest& request, PreparedAssetImport& output,
                          std::vector<ImportFinding>& findings) {
    if (request.prefabPath.empty()) return Result<void>::success();
    auto text = textFile(request, request.prefabPath);
    if (!text) return Result<void>::failure(text.status());
    const auto prefabGuid = guidFromMeta(request, request.prefabPath);
    if (!prefabGuid)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::NotFound,
                                                       "Unity prefab .meta GUID is required",
                                                       request.prefabPath + ".meta", {}, "asset.import.unity"));
    const auto objects = documents(text.value());
    std::map<std::uint64_t, std::string> gameNames;
    for (const auto& object : objects) {
        if (object.classId == 1) {
            auto name = firstMatch(object.body, std::regex(R"(m_Name:\s*([^\r\n]+))"));
            gameNames[object.fileId] = name.value_or("GameObject");
        } else if (object.classId == 114) {
            findings.push_back({request.prefabPath, "MonoBehaviour:" + std::to_string(object.fileId),
                                ImportDisposition::Unsupported,
                                "C# behaviour is not executed or translated by the data importer"});
        }
    }
    Value::Array nodes;
    for (const auto& object : objects) {
        if (object.classId != 4 && object.classId != 224) continue;
        auto game = firstMatch(object.body, std::regex(R"(m_GameObject:\s*\{fileID:\s*(-?[0-9]+)\})"));
        auto father = firstMatch(object.body, std::regex(R"(m_Father:\s*\{fileID:\s*(-?[0-9]+)\})"));
        const std::regex position(R"(m_LocalPosition:\s*\{x:\s*([^,]+),\s*y:\s*([^,]+),\s*z:\s*([^}]+)\})");
        const std::regex rotation(R"(m_LocalRotation:\s*\{x:\s*([^,]+),\s*y:\s*([^,]+),\s*z:\s*([^,]+),\s*w:\s*([^}]+)\})");
        const std::regex scale(R"(m_LocalScale:\s*\{x:\s*([^,]+),\s*y:\s*([^,]+),\s*z:\s*([^}]+)\})");
        if (!game) continue;
        auto vector = [&](const std::regex& regex, std::size_t count, float* target, bool reflect) -> Result<void> {
            for (std::size_t component = 0; component < count; ++component) {
                auto field = firstMatch(object.body, regex, component + 1);
                if (!field) return Result<void>::failure(Diagnostic::error(DiagnosticCode::ParseError,
                                                                           "Unity transform field is missing",
                                                                           request.prefabPath, {}, "asset.import.unity"));
                auto parsed = parseFloat(*field, request.prefabPath);
                if (!parsed) return Result<void>::failure(parsed.status());
                target[component] = parsed.value();
            }
            if (reflect) target[2] = -target[2];
            return Result<void>::success();
        };
        float p[3], q[4], s[3];
        auto pResult = vector(position, 3, p, true); if (!pResult) return pResult;
        auto qResult = vector(rotation, 4, q, false); if (!qResult) return qResult;
        q[0] = -q[0]; q[1] = -q[1];
        auto sResult = vector(scale, 3, s, false); if (!sResult) return sResult;
        const auto gameIdValue = parseUnsignedText(*game);
        const auto parentIdValue = father ? parseUnsignedText(*father) : std::optional<std::uint64_t>(0);
        if (!gameIdValue || !parentIdValue)
            return Result<void>::failure(Diagnostic::error(DiagnosticCode::ParseError,
                                                           "Unity transform fileID overflows",
                                                           request.prefabPath, {}, "asset.import.unity"));
        const auto gameId = *gameIdValue;
        const std::uint64_t parentId = *parentIdValue;
        Value::Object node;
        node["objectId"] = Value(request.package.packageId.child("unity:" + *prefabGuid + ":" +
                                                                  std::to_string(object.fileId)).format());
        node["sourceFileId"] = Value(static_cast<std::int64_t>(object.fileId));
        node["name"] = Value(gameNames.contains(gameId) ? gameNames[gameId] : "GameObject");
        node["parentSourceFileId"] = Value(static_cast<std::int64_t>(parentId));
        node["position"] = Value(Value::Array{Value(double(p[0])), Value(double(p[1])), Value(double(p[2]))});
        node["rotation"] = Value(Value::Array{Value(double(q[0])), Value(double(q[1])), Value(double(q[2])), Value(double(q[3]))});
        node["scale"] = Value(Value::Array{Value(double(s[0])), Value(double(s[1])), Value(double(s[2]))});
        nodes.emplace_back(std::move(node));
    }
    const PersistentId sceneId = request.package.packageId.child("unity-prefab:" + *prefabGuid);
    Value::Object definition;
    definition["schema"] = Value("eve.scene-template"); definition["schemaVersion"] = Value(std::int64_t(1));
    definition["sourceGuid"] = Value(*prefabGuid); definition["nodes"] = Value(std::move(nodes));
    definition["coordinateSystem"] = Value("right-handed-x-right-y-up-minus-z-forward");
    auto encoded = Value(std::move(definition)).toJson();
    if (!encoded) return Result<void>::failure(encoded.status());
    std::string definitionText = std::move(encoded).takeValue();
    const std::string path = "assets/" + sceneId.format() + "/asset.json";
    auto reference = detail::assetRef(sceneId);
    if (!reference) return Result<void>::failure(reference.status());
    output.manifest.assets.push_back({std::move(reference).takeValue(), "eve.scene-template", SchemaVersion(1), path,
                                      detail::sha256(std::span<const std::uint8_t>(
                                          reinterpret_cast<const std::uint8_t*>(definitionText.data()),
                                          definitionText.size())), {"scene", "source:unity-prefab"}});
    output.entries.push_back({path, {definitionText.begin(), definitionText.end()}});
    if (output.manifest.entrypoints.empty()) {
        auto entrypoint = detail::assetRef(sceneId);
        if (!entrypoint) return Result<void>::failure(entrypoint.status());
        output.manifest.entrypoints.emplace("default", std::move(entrypoint).takeValue());
    }
    findings.push_back({request.prefabPath, "Prefab.GameObject.Transform",
                        ImportDisposition::Translated, "GUID/fileID hierarchy converted to canonical TRS"});
    return Result<void>::success();
}

}  // namespace

Result<PreparedAssetImport> prepareUnityProjectImport(const UnityProjectImportRequest& request) {
    if (request.files.empty() || request.package.packageId.isNil())
        return detail::failure<PreparedAssetImport>(DiagnosticCode::InvalidArgument,
                                                    "Unity project files and package identity are required");
    std::uint64_t totalBytes = 0;
    for (const auto& [path, bytes] : request.files) {
        if (path.empty() || path.front() == '/' || path.find("..") != std::string::npos ||
            bytes.size() > request.limits.maximumSourceBytes ||
            totalBytes > request.limits.maximumSourceBytes - bytes.size())
            return detail::failure<PreparedAssetImport>(DiagnosticCode::InvalidArgument,
                                                        "Unity project path or total source budget is invalid", path);
        totalBytes += bytes.size();
    }
    std::vector<ImportFinding> findings;
    PreparedAssetImport output;
    if (!request.terrainDataPath.empty()) {
        auto terrain = parseTerrain(request, guidPaths(request), findings);
        if (!terrain) return Result<PreparedAssetImport>::failure(terrain.status());
        auto prepared = prepareCanonicalTerrainImport(request.package, terrain.value(), "eve.unity-terrain/1",
                                                      request.limits);
        if (!prepared) return Result<PreparedAssetImport>::failure(prepared.status());
        output = std::move(prepared).takeValue();
    } else {
        auto manifest = detail::baseManifest(request.package, "eve.unity-prefab/1");
        if (!manifest) return Result<PreparedAssetImport>::failure(manifest.status());
        output.manifest = std::move(manifest).takeValue();
    }
    auto prefab = appendPrefab(request, output, findings);
    if (!prefab) return Result<PreparedAssetImport>::failure(prefab.status());
    output.manifest.provenance["sourceEngine"] = Value("unity");
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
