#include "asset/import/ImportCommon.h"
#include "asset/import/UnityImporter.h"
#include "asset/import/UnitySourceInternal.h"

#include <cmath>
#include <regex>
#include <set>

namespace eve::asset_import {
namespace {
std::optional<std::string> match(const std::string& text, const std::string& pattern, unsigned group = 1) {
    std::smatch result;
    if (!std::regex_search(text, result, std::regex(pattern))) return {};
    return result[group].str();
}
Result<double> property(const std::string& text, const std::string& name, double defaultValue) {
    const auto value = match(text, "(?:^|\\n)\\s*- " + name + ": *([^\\r\\n]+)");
    if (!value) return Result<double>::success(defaultValue);
    auto parsed = Value::fromJson(*value);
    if (!parsed || !parsed.value().isNumeric())
        return detail::failure<double>(DiagnosticCode::ParseError, "invalid Unity material scalar", name);
    const auto number = parsed.value().isInt64() ? double(parsed.value().asInt()) : parsed.value().asDouble();
    if (!std::isfinite(number))
        return detail::failure<double>(DiagnosticCode::ParseError, "nonfinite material scalar", name);
    return Result<double>::success(number);
}
std::string refKey(const std::string& guid, const std::string& id) {
    return "unity:" + unity_detail::foldAscii(guid) + "/" + id;
}
struct Document {
    std::string type, id, body;
};
std::vector<Document> documents(const std::string& text) {
    const std::regex      header(R"((?:^|\n)--- !u!([0-9]+) &(-?[0-9]+)[^\n]*\n)");
    std::vector<Document> out;
    std::size_t           start = 0;
    for (std::sregex_iterator it(text.begin(), text.end(), header), end; it != end; ++it) {
        if (!out.empty()) out.back().body = text.substr(start, std::size_t(it->position()) - start);
        out.push_back({(*it)[1].str(), (*it)[2].str(), {}});
        start = std::size_t(it->position() + it->length());
    }
    if (!out.empty()) out.back().body = text.substr(start);
    return out;
}
}  // namespace

Result<PreparedAssetImport> prepareUnityMaterial(const UnityProjectImportRequest& request,
                                                 const UnitySourceAsset&          source) {
    const auto&       bytes = request.files.at(source.path);
    const std::string text(bytes.begin(), bytes.end());
    if (!match(text, R"(m_Shader: *\{fileID: *(46), guid: *0000000000000000f000000000000000, type: *0\})"))
        return detail::failure<PreparedAssetImport>(DiagnosticCode::Unsupported,
                                                    "only built-in Standard shader is converted", source.path);
    auto metallic = property(text, "_Metallic", 0), smoothness = property(text, "_Glossiness", 0.5),
         mode = property(text, "_Mode", 0);
    if (!metallic) return Result<PreparedAssetImport>::failure(metallic.status());
    if (!smoothness) return Result<PreparedAssetImport>::failure(smoothness.status());
    if (!mode) return Result<PreparedAssetImport>::failure(mode.status());
    if (mode.value() != 0 || metallic.value() < 0 || metallic.value() > 1 || smoothness.value() < 0 ||
        smoothness.value() > 1)
        return detail::failure<PreparedAssetImport>(
            DiagnosticCode::Unsupported, "only opaque Standard materials with normalized factors are converted",
            source.path);
    Value::Array color{Value(1.0), Value(1.0), Value(1.0), Value(1.0)};
    if (auto value = match(text, R"(- _Color: *(\{[^\r\n]+\}))")) {
        auto parsed = Value::fromJson(std::regex_replace(*value, std::regex(R"(([rgba]):)"), "\"$1\":"));
        if (!parsed || !parsed.value().isObject())
            return detail::failure<PreparedAssetImport>(DiagnosticCode::ParseError, "invalid material color",
                                                        source.path);
        unsigned i = 0;
        for (const auto key : {"r", "g", "b", "a"}) {
            const auto& object = *parsed.value().getIf<Value::Object>();
            const auto  entry  = object.find(key);
            if (entry == object.end() || !entry->second.isNumeric())
                return detail::failure<PreparedAssetImport>(DiagnosticCode::ParseError,
                                                            "invalid material color component", source.path);
            color[i++] = entry->second;
        }
    }
    auto manifest = detail::baseManifest(request.package, "eve.unity-material/1");
    if (!manifest) return Result<PreparedAssetImport>::failure(manifest.status());
    PreparedAssetImport out;
    out.manifest   = std::move(manifest).takeValue();
    const auto id  = request.package.packageId.child("unity:" + source.guid + ":2100000");
    auto       ref = detail::assetRef(id);
    if (!ref) return Result<PreparedAssetImport>::failure(ref.status());
    Value::Object definition{{"schema", Value("eve.material")},
                             {"schemaVersion", Value(std::int64_t(1))},
                             {"shadingModel", Value("pbr")},
                             {"surfaceMode", Value("opaque")},
                             {"baseColor", Value(std::move(color))},
                             {"metallic", Value(metallic.value())},
                             {"roughness", Value(1.0 - smoothness.value())}};
    if (auto main = match(text, R"(- _MainTex:([\s\S]*?)(?:\n    - |\n    m_Floats:))")) {
        const auto guid = match(*main, R"(m_Texture: *\{fileID: *2800000, guid: *([0-9a-fA-F]{32}), type: *3\})");
        if (guid) {
            if (!match(*main, R"(m_Scale: *\{x: *(1), y: *1\})") || !match(*main, R"(m_Offset: *\{x: *(0), y: *0\})"))
                return detail::failure<PreparedAssetImport>(DiagnosticCode::Unsupported,
                                                            "material texture transform is unsupported", source.path);
            auto image = detail::assetRef(
                request.package.packageId.child("unity:" + unity_detail::foldAscii(*guid)).child("image:default"));
            if (!image) return Result<PreparedAssetImport>::failure(image.status());
            definition["baseColorTexture"] = Value(image.value().format());
            out.manifest.dependencies.push_back({ref.value(),
                                                 image.value(),
                                                 asset::EvaDependencyKind::RuntimeRequired,
                                                 "baseColorTexture",
                                                 {},
                                                 "eve.image/2",
                                                 {}});
        }
    }
    const auto path = "assets/" + id.format() + "/asset.json";
    auto       json = Value(std::move(definition)).toJson();
    if (!json) return Result<PreparedAssetImport>::failure(json.status());
    std::vector<std::uint8_t> encoded(json.value().begin(), json.value().end());
    out.manifest.assets.push_back(
        {ref.value(), "eve.material", SchemaVersion(1), path, detail::sha256(encoded), {"material", "source:unity"}});
    out.entries.push_back({path, std::move(encoded)});
    out.manifest.entrypoints.emplace("default", ref.value());
    out.sourceMappings.push_back({"2100000", ref.value()});
    out.findings.push_back(
        {source.path, "Material.Standard", ImportDisposition::Translated, "opaque metallic-roughness and base color"});
    out.findings.push_back({source.path, "Material.extraProperties", ImportDisposition::Unsupported,
                            "additional shader keywords, non-base texture maps and saved properties are not applied"});
    return Result<PreparedAssetImport>::success(std::move(out));
}

Result<void> bindUnityRenderers(const UnityProjectImportRequest& request, const UnitySourceIndex& index,
                                PreparedAssetImport& out) {
    std::map<std::string, AssetRef> mappings;
    for (const auto& mapping : out.sourceMappings) mappings.emplace(mapping.sourceObject, mapping.asset);
    for (const auto& source : index.assets) {
        if (source.kind != UnitySourceKind::Prefab) continue;
        const auto sceneId = detail::assetRef(request.package.packageId.child("unity-prefab:" + source.guid));
        if (!sceneId) return Result<void>::failure(sceneId.status());
        auto asset = std::find_if(out.manifest.assets.begin(), out.manifest.assets.end(),
                                  [&](const auto& a) { return a.asset == sceneId.value(); });
        if (asset == out.manifest.assets.end()) continue;
        auto entry = std::find_if(out.entries.begin(), out.entries.end(),
                                  [&](const auto& e) { return e.path == asset->definition; });
        if (entry == out.entries.end())
            return detail::failure<void>(DiagnosticCode::NotFound, "scene definition absent", source.path);
        const auto&                     bytes = request.files.at(source.path);
        const std::string               text(bytes.begin(), bytes.end());
        auto                            docs = documents(text);
        std::map<std::string, Document> transforms, filters;
        for (const auto& doc : docs) {
            if (doc.type != "4" && doc.type != "33") continue;
            const auto game = match(doc.body, R"(m_GameObject: *\{fileID: *(-?[0-9]+)\})");
            if (!game) continue;
            auto& target = doc.type == "4" ? transforms : filters;
            if (!target.emplace(*game, doc).second)
                return detail::failure<void>(DiagnosticCode::Conflict,
                                             "duplicate Transform or MeshFilter on GameObject", source.path);
        }
        Value::Array          renderers;
        std::set<std::string> resolvedComponents;
        for (const auto& doc : docs) {
            if (doc.type != "23") continue;
            const auto game = match(doc.body, R"(m_GameObject: *\{fileID: *(-?[0-9]+)\})");
            if (!game || !transforms.contains(*game) || !filters.contains(*game)) continue;
            const auto& filter = filters.at(*game);
            const auto  meshGuid =
                match(filter.body, R"(m_Mesh: *\{fileID: *-?[0-9]+, guid: *([0-9a-fA-F]{32}), type: *3\})");
            const auto meshId = match(filter.body, R"(m_Mesh: *\{fileID: *(-?[0-9]+),)");
            if (!meshGuid || !meshId || !mappings.contains(refKey(*meshGuid, *meshId))) continue;
            const auto material =
                match(doc.body, R"(m_Materials:\s*\n  - \{fileID: *2100000, guid: *([0-9a-fA-F]{32}), type: *2\})");
            if (!material || !mappings.contains(refKey(*material, "2100000"))) continue;
            // A second material slot requires submesh routing; never silently select its first slot.
            if (match(doc.body, R"(m_Materials:\s*\n  - [^\n]+\n  (- ))")) continue;
            const auto meshRef     = mappings.at(refKey(*meshGuid, *meshId));
            const auto materialRef = mappings.at(refKey(*material, "2100000"));
            const auto objectId =
                request.package.packageId.child("unity:" + source.guid + ":" + transforms.at(*game).id);
            renderers.emplace_back(
                Value::Object{{"objectId", Value(objectId.format())},
                              {"mesh", Value(meshRef.format())},
                              {"material", Value(materialRef.format())},
                              {"enabled", Value(!match(doc.body, R"(m_Enabled: *(0)(?:\s|$))").has_value())}});
            out.manifest.dependencies.push_back({sceneId.value(),
                                                 meshRef,
                                                 asset::EvaDependencyKind::RuntimeRequired,
                                                 "renderers[" + std::to_string(renderers.size() - 1) + "].mesh",
                                                 {},
                                                 "eve.mesh/1",
                                                 {}});
            out.manifest.dependencies.push_back({sceneId.value(),
                                                 materialRef,
                                                 asset::EvaDependencyKind::RuntimeRequired,
                                                 "renderers[" + std::to_string(renderers.size() - 1) + "].material",
                                                 {},
                                                 "eve.material/1",
                                                 {}});
            resolvedComponents.insert("Prefab.component:23:" + doc.id);
            resolvedComponents.insert("Prefab.component:33:" + filter.id);
        }
        if (renderers.empty()) continue;
        auto definition = Value::fromJson(std::string(entry->bytes.begin(), entry->bytes.end()));
        if (!definition) return Result<void>::failure(definition.status());
        auto* object = definition.value().getIf<Value::Object>();
        if (!object) return detail::failure<void>(DiagnosticCode::ParseError, "invalid scene definition", source.path);
        (*object)["schemaVersion"] = Value(std::int64_t(2));
        (*object)["renderers"]     = Value(std::move(renderers));
        auto encoded               = definition.value().toJson();
        if (!encoded) return Result<void>::failure(encoded.status());
        entry->bytes.assign(encoded.value().begin(), encoded.value().end());
        asset->schemaVersion = SchemaVersion(2);
        asset->contentHash   = detail::sha256(entry->bytes);
        std::erase_if(out.findings, [&](const auto& finding) {
            return finding.sourcePath == source.path && resolvedComponents.contains(finding.feature);
        });
        out.findings.push_back({source.path, "Prefab.renderBindings", ImportDisposition::Translated,
                                "resolved mesh and material by GUID/fileID"});
        out.findings.push_back({source.path, "Prefab.shadowPass", ImportDisposition::Unsupported,
                                "static draw loader does not submit shadow-caster passes; original renderer shadow "
                                "settings are preserved as source"});
    }
    return Result<void>::success();
}
}  // namespace eve::asset_import
