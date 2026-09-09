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
    auto metallic = property(text, "_Metallic", 0);
    if (!metallic) return Result<PreparedAssetImport>::failure(metallic.status());
    auto smoothness = property(text, "_Glossiness", 0.5);
    if (!smoothness) return Result<PreparedAssetImport>::failure(smoothness.status());
    auto mode = property(text, "_Mode", 0);
    if (!mode) return Result<PreparedAssetImport>::failure(mode.status());
    if ((mode.value() != 0 && mode.value() != 2 && mode.value() != 3) || metallic.value() < 0 || metallic.value() > 1 ||
        smoothness.value() < 0 || smoothness.value() > 1)
        return detail::failure<PreparedAssetImport>(
            DiagnosticCode::Unsupported, "Standard opaque/fade/transparent materials require normalized factors",
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
                             {"surfaceMode", Value(mode.value() == 0 ? "opaque" : "transparent")},
                             {"baseColor", Value(std::move(color))},
                             {"metallic", Value(metallic.value())},
                             {"roughness", Value(1.0 - smoothness.value())}};
    if (mode.value() != 0) definition["blendMode"] = Value(mode.value() == 3 ? "premultiplied" : "alpha");
    if (auto main = match(text, R"(- _MainTex:([\s\S]*?)(?:\n    - |\n    m_Floats:))")) {
        const auto guid = match(*main, R"(m_Texture: *\{fileID: *2800000, guid: *([0-9a-fA-F]{32}), type: *3\})");
        if (guid) {
            if (mode.value() == 3)
                return detail::failure<PreparedAssetImport>(
                    DiagnosticCode::Unsupported, "premultiplied Standard texture alpha requires a shader conversion",
                    source.path);
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
    out.findings.push_back({source.path, "Material.Standard", ImportDisposition::Translated,
                            "metallic-roughness, base color and surface mode"});
    out.findings.push_back({source.path, "Material.extraProperties", ImportDisposition::Unsupported,
                            "additional shader keywords, non-base texture maps and saved properties are not applied"});
    return Result<PreparedAssetImport>::success(std::move(out));
}

Result<void> bindUnityRenderers(const UnityProjectImportRequest& request, const UnitySourceIndex& index,
                                PreparedAssetImport& out) {
    std::map<std::string, AssetRef> mappings;
    std::map<std::string, std::size_t> nativeSlots;
    const std::regex                   submeshMarker("(?:^|\\n)    firstByte:");
    for (const auto& source : index.assets) {
        if (detail::extension(source.path) != "asset") continue;
        const auto&       bytes = request.files.at(source.path);
        const std::string text(bytes.begin(), bytes.end());
        if (text.find("!u!43 ") != std::string::npos)
            nativeSlots[source.guid] = std::size_t(
                std::distance(std::sregex_iterator(text.begin(), text.end(), submeshMarker), std::sregex_iterator()));
    }
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
        auto definition = Value::fromJson(std::string(entry->bytes.begin(), entry->bytes.end()));
        if (!definition) return Result<void>::failure(definition.status());
        auto* object = definition.value().getIf<Value::Object>();
        if (!object) return detail::failure<void>(DiagnosticCode::ParseError, "invalid scene definition", source.path);
        auto* nodes = object->at("nodes").getIf<Value::Array>();
        if (!nodes) return detail::failure<void>(DiagnosticCode::ParseError, "invalid scene nodes", source.path);
        std::int64_t nextNode = 1;
        for (const auto& node : *nodes)
            nextNode = std::max(nextNode, node.getIf<Value::Object>()->at("sourceFileId").asInt());
        Value::Array          renderers;
        std::set<std::string> resolvedComponents;
        for (const auto& doc : docs) {
            if (doc.type != "23") continue;
            const auto game = match(doc.body, R"(m_GameObject: *\{fileID: *(-?[0-9]+)\})");
            if (!game || !transforms.contains(*game) || !filters.contains(*game)) continue;
            const auto& filter = filters.at(*game);
            const auto  meshGuid =
                match(filter.body, R"(m_Mesh: *\{fileID: *-?[0-9]+, guid: *([0-9a-fA-F]{32}), type: *[23]\})");
            const auto meshId = match(filter.body, R"(m_Mesh: *\{fileID: *(-?[0-9]+),)");
            if (!meshGuid || !meshId) continue;
            const auto key    = refKey(*meshGuid, *meshId);
            const bool native = nativeSlots.contains(unity_detail::foldAscii(*meshGuid));
            if (!native && !mappings.contains(key)) continue;
            const auto list = match(doc.body, R"(m_Materials:([^\n]*\n(?:  - [^\n]*(?:\n|$))*))");
            if (!list) continue;
            std::vector<std::string> materials;
            const std::regex         materialPattern(R"(  - (\{[^\n]+\}))");
            for (std::sregex_iterator it(list->begin(), list->end(), materialPattern), end; it != end; ++it) {
                auto guid = match((*it)[1].str(), R"(fileID: *2100000, guid: *([0-9a-fA-F]{32}), type: *2)");
                materials.push_back(guid.value_or(""));
            }
            std::size_t count = 1;
            if (native) count = nativeSlots.at(unity_detail::foldAscii(*meshGuid));
            if (materials.size() != count) {
                out.findings.push_back({source.path, "Prefab.materialSlots", ImportDisposition::Unsupported,
                                        "material slot count does not match mesh submeshes"});
                continue;
            }
            for (std::size_t slot = 0; slot < count; ++slot) {
                if (!mappings.contains(refKey(materials[slot], "2100000")) ||
                    (native && !mappings.contains(key + "/submesh/" + std::to_string(slot))))
                    continue;
                const auto meshRef     = mappings.at(native ? key + "/submesh/" + std::to_string(slot) : key);
                const auto materialRef = mappings.at(refKey(materials[slot], "2100000"));
                auto objectId = request.package.packageId.child("unity:" + source.guid + ":" + transforms.at(*game).id);
                if (native) {
                    if (nextNode == std::numeric_limits<std::int64_t>::max() ||
                        nodes->size() >= request.limits.maximumAssets)
                        return detail::failure<void>(DiagnosticCode::InvalidArgument, "submesh node budget exceeded",
                                                     source.path);
                    ++nextNode;
                    objectId = objectId.child("submesh:" + std::to_string(slot));
                    nodes->emplace_back(
                        Value::Object{{"objectId", Value(objectId.format())},
                                      {"sourceFileId", Value(nextNode)},
                                      {"parentSourceFileId", Value(std::int64_t(std::stoll(transforms.at(*game).id)))},
                                      {"name", Value("Submesh " + std::to_string(slot))},
                                      {"visible", Value(true)},
                                      {"position", Value(Value::Array{Value(0.0), Value(0.0), Value(0.0)})},
                                      {"rotation", Value(Value::Array{Value(0.0), Value(0.0), Value(0.0), Value(1.0)})},
                                      {"scale", Value(Value::Array{Value(1.0), Value(1.0), Value(1.0)})}});
                }
                renderers.emplace_back(
                    Value::Object{{"objectId", Value(objectId.format())},
                                  {"mesh", Value(meshRef.format())},
                                  {"material", Value(materialRef.format())},
                                  {"enabled", Value(!match(doc.body, R"(m_Enabled: *(0)(?:\s|$))").has_value())}});
                const auto prefix = "renderers[" + std::to_string(renderers.size() - 1) + "]";
                out.manifest.dependencies.push_back({sceneId.value(),
                                                     meshRef,
                                                     asset::EvaDependencyKind::RuntimeRequired,
                                                     prefix + ".mesh",
                                                     {},
                                                     "eve.mesh/1",
                                                     {}});
                out.manifest.dependencies.push_back({sceneId.value(),
                                                     materialRef,
                                                     asset::EvaDependencyKind::RuntimeRequired,
                                                     prefix + ".material",
                                                     {},
                                                     "eve.material/1",
                                                     {}});
            }
            resolvedComponents.insert("Prefab.component:23:" + doc.id);
            resolvedComponents.insert("Prefab.component:33:" + filter.id);
        }
        if (renderers.empty()) continue;
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
