#include "asset/import/GltfMaterials.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include "asset/import/GltfDecode.h"
#include "asset/import/ImportCommon.h"
namespace eve::asset_import::gltf {
namespace {
struct Rejection {
    std::string message;
};
void require(bool value, const char* message) {
    if (!value) throw Rejection{message};
}
const Value::Object& object(const Value& value) {
    auto* o = value.getIf<Value::Object>();
    require(o, "material field must be an object");
    return *o;
}
const Value::Array& array(const Value* value) {
    auto* a = value ? value->getIf<Value::Array>() : nullptr;
    require(a, "material collection must be an array");
    return *a;
}
size_t index(const Value* value, size_t count) {
    require(value && value->isInt64() && value->asInt() >= 0 && uint64_t(value->asInt()) < count,
            "material reference index out of range");
    return size_t(value->asInt());
}
struct Factors {
    const GltfImportRequest& request;
    PreparedAssetImport&     output;
    std::string              path;
    double                   adjust(double value, double replacement, std::string_view field) {
        if (request.mode == GltfImportMode::Strict)
            throw Rejection{path + "." + std::string(field) + ": material factor out of range"};
        output.findings.push_back(
            {request.sourceName, path + "." + std::string(field), ImportDisposition::Baked,
             "warning: material factor " + std::to_string(value) + " clamped to " + std::to_string(replacement),
             ImportSeverity::Warning});
        return replacement;
    }
    double scalar(const Value* value, double fallback, double minimum = 0, double maximum = 1,
                  std::string_view field = "factor") {
        if (!value) return fallback;
        require(value->isNumeric(), "material factor must be numeric");
        const double n = value->isInt64() ? double(value->asInt()) : value->asDouble();
        require(std::isfinite(n), "material factor must be finite");
        return n < minimum || n > maximum ? adjust(n, std::clamp(n, minimum, maximum), field) : n;
    }
    Value vector(const Value* value, std::initializer_list<double> fallback, double minimum = 0, double maximum = 1,
                 std::string_view field = "vector") {
        if (!value) {
            Value::Array a;
            for (auto n : fallback) a.emplace_back(n);
            return Value(std::move(a));
        }
        const auto& a = array(value);
        require(a.size() == fallback.size(), "invalid material factor vector");
        Value::Array out;
        for (size_t i = 0; i < a.size(); ++i)
            out.emplace_back(scalar(&a[i], 0, minimum, maximum, std::string(field) + "[" + std::to_string(i) + "]"));
        return Value(std::move(out));
    }
};
AssetRef reference(PersistentId id) {
    auto r = detail::assetRef(id);
    require(r.ok(), "invalid material identity");
    return std::move(r).takeValue();
}
void budget(const GltfImportRequest& request, const PreparedAssetImport& out) {
    require(out.manifest.assets.size() <= request.limits.maximumAssets, "material asset budget exceeded");
    uint64_t size = 0;
    for (const auto& e : out.entries) {
        require(e.bytes.size() <= request.limits.maximumDecodedBytes - size, "material byte budget exceeded");
        size += e.bytes.size();
    }
}
void link(PreparedAssetImport& out, const AssetRef& from, const AssetRef& to, std::string role, std::string schema) {
    out.manifest.dependencies.push_back(
        {from, to, asset::EvaDependencyKind::RuntimeRequired, std::move(role), {}, std::move(schema)});
}
void append(const GltfImportRequest& request, const Value::Object& root,
            const std::vector<std::span<const uint8_t>>& buffers, PreparedAssetImport& out) {
    const auto* materialsValue = member(root, "materials");
    if (!materialsValue) {
        const auto& meshes = array(member(root, "meshes"));
        for (const auto& mesh : meshes)
            for (const auto& primitive : array(member(object(mesh), "primitives")))
                require(!member(object(primitive), "material"), "primitive material collection is missing");
        return;
    }
    const auto&                     materials = array(materialsValue);
    std::map<std::string, AssetRef> images;
    for (size_t m = 0; m < materials.size(); ++m) {
        const auto& source = object(materials[m]);
        Factors     factors{request, out, "materials[" + std::to_string(m) + "]"};

        const Value::Object empty;
        const auto*         pbrValue = member(source, "pbrMetallicRoughness");
        const auto&         pbr      = pbrValue ? object(*pbrValue) : empty;
        auto                ref      = reference(request.package.packageId.child("gltf:material:" + std::to_string(m)));
        std::string         mode     = "OPAQUE";
        if (auto* v = member(source, "alphaMode")) {
            require(v->isString(), "alphaMode must be a string");
            mode = v->asString();
        }
        require(mode == "OPAQUE" || mode == "MASK" || mode == "BLEND", "unknown alphaMode");
        Value::Object definition{{"schema", Value("eve.material")},
                                 {"schemaVersion", Value(int64_t(2))},
                                 {"shadingModel", Value("pbr")},
                                 {"surfaceMode", Value(mode == "OPAQUE" ? "opaque"
                                                       : mode == "MASK" ? "masked"
                                                                        : "transparent")},
                                 {"baseColor", factors.vector(member(pbr, "baseColorFactor"), {1, 1, 1, 1})},
                                 {"metallic", Value(factors.scalar(member(pbr, "metallicFactor"), 1))},
                                 {"roughness", Value(factors.scalar(member(pbr, "roughnessFactor"), 1))},
                                 {"emissive", factors.vector(member(source, "emissiveFactor"), {0, 0, 0})},
                                 {"alphaCutoff", Value(factors.scalar(member(source, "alphaCutoff"), 0.5))}};
        if (auto* v = member(source, "doubleSided")) {
            require(v->isBool(), "doubleSided must be boolean");
            definition["doubleSided"] = *v;
        } else
            definition["doubleSided"] = Value(false);
        if (mode == "BLEND") definition["blendMode"] = Value("alpha");
        if (auto* name = member(source, "name")) {
            require(name->isString() && name->asString().size() <= request.limits.maximumStringBytes,
                    "material name exceeds budget");
            definition["name"] = *name;
        }
        Value::Object textureInfos;
        for (const auto* role :
             {"baseColorTexture", "metallicRoughnessTexture", "normalTexture", "occlusionTexture", "emissiveTexture"}) {
            const auto* v = member(
                std::string_view(role) == "baseColorTexture" || std::string_view(role) == "metallicRoughnessTexture"
                    ? pbr
                    : source,
                role);
            if (v) textureInfos[role] = *v;
        }
        const double unbounded = std::numeric_limits<double>::max();
        if (const auto* extensions = member(source, "extensions")) {
            const auto& materialExtensions = object(*extensions);
            require(!materialExtensions.contains("KHR_materials_unlit") || materialExtensions.size() == 1,
                    "unlit cannot be combined with PBR material extensions");
            for (const auto& [name, value] : materialExtensions) {
                require(supportsMaterialExtension(name) && name != "KHR_texture_transform",
                        "unknown material extension");
                const auto& ext    = object(value);
                auto        factor = [&](const char* key, double fallback, double minimum = 0, double maximum = 1) {
                    definition[key] = Value(factors.scalar(member(ext, key), fallback, minimum, maximum, key));
                };
                auto texture = [&](const char* key) {
                    if (auto* v = member(ext, key)) textureInfos[key] = *v;
                };
                if (name == "KHR_materials_specular") {
                    factor("specularFactor", 1);
                    definition["specularColorFactor"] =
                        factors.vector(member(ext, "specularColorFactor"), {1, 1, 1}, 0, unbounded);
                    texture("specularTexture");
                    texture("specularColorTexture");
                } else if (name == "KHR_materials_anisotropy") {
                    factor("anisotropyStrength", 0);
                    factor("anisotropyRotation", 0, -unbounded, unbounded);
                    texture("anisotropyTexture");
                } else if (name == "KHR_materials_clearcoat") {
                    factor("clearcoatFactor", 0);
                    factor("clearcoatRoughnessFactor", 0);
                    texture("clearcoatTexture");
                    texture("clearcoatRoughnessTexture");
                    texture("clearcoatNormalTexture");
                } else if (name == "KHR_materials_emissive_strength") {
                    factor("emissiveStrength", 1, 0, unbounded);
                } else if (name == "KHR_materials_ior") {
                    double ior = factors.scalar(member(ext, "ior"), 1.5, 0, unbounded, "ior");
                    if (ior > 0 && ior < 1) ior = factors.adjust(ior, 1, "ior");
                    definition["ior"] = Value(ior);
                } else if (name == "KHR_materials_unlit")
                    definition["shadingModel"] = Value("unlit");
            }
        }
        for (const auto& [role, textureInfo] : textureInfos) {
            const bool color =
                role == "baseColorTexture" || role == "emissiveTexture" || role == "specularColorTexture";
            const auto&  info        = object(textureInfo);
            const Value* effectiveUv = member(info, "texCoord");
            if (effectiveUv) require(effectiveUv->isInt64() && effectiveUv->asInt() >= 0, "invalid texCoord");
            if (const auto* extensions = member(info, "extensions")) {
                for (const auto& [name, value] : object(*extensions)) {
                    require(name == "KHR_texture_transform", "unknown texture-info extension");
                    const auto& transform = object(value);
                    if (auto* uv = member(transform, "texCoord")) effectiveUv = uv;
                    definition[role + "Transform"] = Value(Value::Object{
                        {"offset", factors.vector(member(transform, "offset"), {0, 0}, -unbounded, unbounded)},
                        {"scale", factors.vector(member(transform, "scale"), {1, 1}, -unbounded, unbounded)},
                        {"rotation", Value(factors.scalar(member(transform, "rotation"), 0, -unbounded, unbounded))},
                        {"texCoord", effectiveUv ? *effectiveUv : Value(int64_t(0))}});
                }
            }
            if (effectiveUv)
                require(effectiveUv->isInt64() && effectiveUv->asInt() >= 0 &&
                            uint64_t(effectiveUv->asInt()) <= std::numeric_limits<uint32_t>::max(),
                        "invalid effective UV set");
            const auto uvSet = effectiveUv ? uint32_t(effectiveUv->asInt()) : 0u;
            if (!definition.contains(role + "Transform")) definition[role + "TexCoord"] = Value(int64_t(uvSet));
            for (const auto& mesh : array(member(root, "meshes"))) {
                for (const auto& primitive : array(member(object(mesh), "primitives"))) {
                    const auto& p             = object(primitive);
                    const auto* materialIndex = member(p, "material");
                    if (!materialIndex || index(materialIndex, materials.size()) != m) continue;
                    const auto* attributes = member(p, "attributes");
                    require(attributes && object(*attributes).contains("TEXCOORD_" + std::to_string(uvSet)),
                            "texture UV set is absent from a bound primitive");
                }
            }
            const auto& textures = array(member(root, "textures"));
            const auto& texture  = object(textures[index(member(info, "index"), textures.size())]);
            require(!member(texture, "extensions"), "texture extensions require explicit translation");
            const auto& sourceImages = array(member(root, "images"));
            auto        imageIndex   = index(member(texture, "source"), sourceImages.size());
            const auto  key          = std::to_string(imageIndex) + ":" + role;
            auto        found        = images.find(key);
            if (found == images.end()) {
                const auto&          image = object(sourceImages[imageIndex]);
                std::vector<uint8_t> bytes;
                if (auto* uri = member(image, "uri")) {
                    require(uri->isString(), "image URI must be a string");
                    auto resource = request.externalResources.find(uri->asString());
                    require(resource != request.externalResources.end(), "image URI was not supplied explicitly");
                    bytes = resource->second;
                } else {
                    const auto& views  = array(member(root, "bufferViews"));
                    const auto& view   = object(views[index(member(image, "bufferView"), views.size())]);
                    const auto  b      = index(member(view, "buffer"), buffers.size());
                    auto        offset = unsignedValue(member(view, "byteOffset"), "image offset", false, 0);
                    auto        length = unsignedValue(member(view, "byteLength"), "image length");
                    require(offset.ok() && length.ok(), "invalid image buffer range");
                    require(offset.value() <= buffers[b].size() && length.value() <= buffers[b].size() - offset.value(),
                            "image buffer range out of bounds");
                    auto span = buffers[b].subspan(size_t(offset.value()), size_t(length.value()));
                    bytes.assign(span.begin(), span.end());
                }
                auto identity      = request.package;
                identity.packageId = identity.packageId.child("gltf:image:" + key);
                auto imported =
                    prepareImageImport({identity, "images[" + std::to_string(imageIndex) + "]", std::move(bytes),
                                        color ? ImageColorSpace::Srgb : ImageColorSpace::Linear, role, request.limits});
                if (!imported) throw Rejection{imported.error()->message()};
                auto imageRef = imported.value().manifest.assets.front().asset;
                for (auto& a : imported.value().manifest.assets) out.manifest.assets.push_back(std::move(a));
                // Each sub-import report belongs to that sub-import only, not the aggregate archive.
                for (auto& e : imported.value().entries)
                    if (e.path.starts_with("assets/")) out.entries.push_back(std::move(e));
                found = images.emplace(key, imageRef).first;
                budget(request, out);
            }
            definition[role] = Value(found->second.format());
            link(out, ref, found->second, role, "eve.image/2");
            Value::Object sampler{{"wrapS", Value(int64_t(10497))}, {"wrapT", Value(int64_t(10497))}};
            if (auto* id = member(texture, "sampler")) {
                const auto& samplers = array(member(root, "samplers"));
                const auto& supplied = object(samplers[index(id, samplers.size())]);
                for (const auto& [name, value] : supplied) {
                    require(value.isInt64(), "sampler fields must be integer enums");
                    auto n = value.asInt();
                    require((name == "wrapS" || name == "wrapT") ? (n == 33071 || n == 33648 || n == 10497)
                            : name == "magFilter"                ? (n == 9728 || n == 9729)
                            : name == "minFilter"                ? (n == 9728 || n == 9729 || (n >= 9984 && n <= 9987))
                                                                 : false,
                            "unsupported sampler field");
                    sampler[name] = value;
                }
            }
            definition[std::string(role) + "Sampler"] = Value(std::move(sampler));
            if (role == "normalTexture" || role == "clearcoatNormalTexture")
                definition[role == "normalTexture" ? "normalScale" : "clearcoatNormalScale"] =
                    Value(factors.scalar(member(info, "scale"), 1, -1e6, 1e6));
            if (std::string_view(role) == "occlusionTexture")
                definition["occlusionStrength"] = Value(factors.scalar(member(info, "strength"), 1));
        }
        const auto path = "assets/" + ref.id().format() + "/asset.json";
        auto       json = Value(std::move(definition)).toJson();
        require(json.ok(), "cannot encode material");
        std::vector<uint8_t> bytes(json.value().begin(), json.value().end());
        out.manifest.assets.push_back(
            {ref, "eve.material", SchemaVersion(2), path, detail::sha256(bytes), {"source:gltf2"}});
        out.entries.push_back({path, std::move(bytes)});
        out.manifest.entrypoints.emplace("material:" + std::to_string(m), ref);
        out.findings.push_back({request.sourceName, "materials[" + std::to_string(m) + "]",
                                ImportDisposition::Translated,
                                "PBR material, image semantics and sampler bindings retained"});
        budget(request, out);
    }
    const auto& meshes = array(member(root, "meshes"));
    for (size_t m = 0; m < meshes.size(); ++m) {
        const auto& primitives = array(member(object(meshes[m]), "primitives"));
        for (size_t p = 0; p < primitives.size(); ++p) {
            const auto* value = member(object(primitives[p]), "material");
            if (!value) continue;
            auto material = reference(
                request.package.packageId.child("gltf:material:" + std::to_string(index(value, materials.size()))));
            auto mesh = reference(
                request.package.packageId.child("gltf:mesh:" + std::to_string(m) + ":primitive:" + std::to_string(p)));
            link(out, mesh, material, "material", "eve.material/2");
        }
    }
}
}  // namespace
bool supportsMaterialExtension(std::string_view name) {
    return name == "KHR_materials_specular" || name == "KHR_materials_anisotropy" ||
           name == "KHR_materials_clearcoat" || name == "KHR_materials_emissive_strength" ||
           name == "KHR_materials_ior" || name == "KHR_materials_unlit" || name == "KHR_texture_transform";
}
Result<void> appendMaterials(const GltfImportRequest& request, const Value::Object& root,
                             const std::vector<std::span<const uint8_t>>& buffers, PreparedAssetImport& output) {
    try {
        append(request, root, buffers, output);
        return Result<void>::success();
    } catch (const Rejection& error) {
        return detail::failure<void>(DiagnosticCode::Unsupported, error.message);
    }
}
}  // namespace eve::asset_import::gltf
