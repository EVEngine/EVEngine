#include "asset/AssetCooker.h"
#include <algorithm>
#include <functional>
#include <map>
#include <set>
#include <tuple>
#include "asset/AssetDependency.h"
#include "asset/AssetDiff.h"
#include "asset/AssetMigration.h"
#include "asset/CanonicalImageCook.h"
#include "asset/CanonicalPcgCook.h"
#include "asset/CanonicalVolumeTextureCook.h"
#include "asset/EvpackCompression.h"
#include "asset/RuntimeDefinition.h"
#include "asset/ShaderAsset.h"
#include "data/HashFunction.h"

// AssetCooker.cpp
namespace eve::asset {
namespace {

std::array<std::uint8_t, 32> sha256(std::span<const std::uint8_t> bytes) {
    data::HashFunction::Value digest{};
    data::HashFunction::getHashFunction("sha256")->hash("sha256", reinterpret_cast<const char*>(bytes.data()),
                                                        bytes.size(), digest);
    std::array<std::uint8_t, 32> result{};
    std::copy_n(reinterpret_cast<const std::uint8_t*>(digest.data), result.size(), result.begin());
    return result;
}

void appendString(std::vector<std::uint8_t>& bytes, std::string_view text) {
    const std::uint64_t size = text.size();
    for (unsigned shift = 0; shift != 64; shift += 8) bytes.push_back(static_cast<std::uint8_t>(size >> shift));
    bytes.insert(bytes.end(), text.begin(), text.end());
}

PersistentId buildIdentity(const EvaArchive& source, const AssetCookProfile& profile,
                           std::string_view canonicalManifest) {
    std::vector<std::uint8_t> key;
    appendString(key, "eve.asset-cooker/1");
    appendString(key, evpackCodecBuildIdentity(profile.chunkCodec));
    const auto packageBytes = source.manifest.packageId.bytes();
    key.insert(key.end(), packageBytes.begin(), packageBytes.end());
    appendString(key, canonicalManifest);
    appendString(key, profile.variant.os);
    appendString(key, profile.variant.arch);
    appendString(key, profile.variant.graphics);
    appendString(key, profile.variant.shaderFormat);
    appendString(key, profile.variant.quality);
    for (const auto& family : profile.variant.textureFamilies) appendString(key, family);
    for (const auto& feature : profile.variant.features) appendString(key, feature);
    for (unsigned shift = 0; shift != 32; shift += 8)
        key.push_back(static_cast<std::uint8_t>(profile.bulkAlignment >> shift));
    for (const auto& entry : source.entries) {
        appendString(key, entry.path);
        const auto hash = sha256(entry.bytes);
        key.insert(key.end(), hash.begin(), hash.end());
    }
    auto                digest = sha256(key);
    PersistentId::Bytes bytes{};
    std::copy_n(digest.begin(), bytes.size(), bytes.begin());
    bytes[6] = static_cast<std::uint8_t>((bytes[6] & 0x0f) | 0x50);
    bytes[8] = static_cast<std::uint8_t>((bytes[8] & 0x3f) | 0x80);
    return PersistentId(bytes);
}

const Value* field(const Value::Object& object, std::string_view name) {
    const auto found = object.find(std::string(name));
    return found == object.end() ? nullptr : &found->second;
}

bool predicateText(const Value::Object& predicate, std::string_view name, std::string_view actual) {
    const Value* value = field(predicate, name);
    return !value || (value->isString() && value->asString() == actual);
}

bool platformDependencyApplies(const EvaDependency& dependency, const EvpackVariant& variant) {
    if (dependency.kind != EvaDependencyKind::Platform) return true;
    return predicateText(dependency.predicate, "os", variant.os) &&
           predicateText(dependency.predicate, "arch", variant.arch) &&
           predicateText(dependency.predicate, "graphics", variant.graphics) &&
           predicateText(dependency.predicate, "quality", variant.quality);
}

Result<void> validatePublication(const EvaManifest& manifest, CookPublication publication) {
    if (publication == CookPublication::LocalInspection) return Result<void>::success();
    const Value* license        = field(manifest.provenance, "license");
    const auto*  object         = license ? license->getIf<Value::Object>() : nullptr;
    const Value* redistribution = object ? field(*object, "redistribution") : nullptr;
    if (!redistribution || !redistribution->isString() ||
        (redistribution->asString() != "runtime-embedded" && redistribution->asString() != "project-only"))
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::PreconditionViolation,
                                                       "public Cook requires an explicit redistribution policy",
                                                       "$.provenance.license.redistribution", {}, "asset.cook"));
    return Result<void>::success();
}

EvpackChunkKind kindForPath(std::string_view path) {
    if (path.find("/stream/") != std::string_view::npos) return EvpackChunkKind::Stream;
    if (path.find("/shaders/") != std::string_view::npos) return EvpackChunkKind::Shader;
    return EvpackChunkKind::Bulk;
}

Result<std::vector<std::uint8_t>> binaryDefinition(std::span<const std::uint8_t> json, std::uint64_t maximumBytes,
                                                   std::span<const EvaDependency> dependencies = {}) {
    auto parsed = Value::fromJson(std::string_view(reinterpret_cast<const char*>(json.data()), json.size()));
    if (!parsed) return Result<std::vector<std::uint8_t>>::failure(parsed.status());
    auto* object = parsed.value().getIf<Value::Object>();
    if (!object)
        return Result<std::vector<std::uint8_t>>::failure(Diagnostic::error(
            DiagnosticCode::ParseError, "runtime definition root must be an object", {}, {}, "asset.cook"));
    Value::Array fallbacks;
    for (const auto& dependency : dependencies) {
        if (dependency.kind != EvaDependencyKind::RuntimeOptional) continue;
        Value::Object fallback;
        fallback["path"]         = Value(dependency.path);
        fallback["to"]           = Value(dependency.to.format());
        fallback["expectedType"] = Value(dependency.expectedType);
        fallback["policy"]       = Value(dependency.fallback);
        fallbacks.emplace_back(std::move(fallback));
    }
    if (!fallbacks.empty()) (*object)["dependencyFallbacks"] = Value(std::move(fallbacks));
    RuntimeDefinitionLimits limits;
    limits.maximumBytes = maximumBytes;
    return encodeRuntimeDefinition(parsed.value(), limits);
}

}  // namespace

Result<AssetCookProfile> assetCookProfileForTarget(std::string_view target) {
    AssetCookProfile profile;
    profile.publication = CookPublication::LocalInspection;
    if (target == "windows-x86_64-vulkan")
        profile.variant = {"windows", "x86_64", "vulkan", {"bc", "rgba8"}, "spirv-1.6", "high", {}};
    else if (target == "linux-x86_64-vulkan")
        profile.variant = {"linux", "x86_64", "vulkan", {"bc", "rgba8"}, "spirv-1.6", "high", {}};
    else if (target == "macos-arm64-vulkan")
        profile.variant = {"macos", "arm64", "vulkan", {"rgba8"}, "spirv-1.6", "high", {}};
    else if (target == "android-arm64-vulkan")
        profile.variant = {"android", "arm64", "vulkan", {"astc", "etc2", "rgba8"}, "spirv-1.6", "high", {}};
    else if (target == "ios-arm64-vulkan")
        profile.variant = {"ios", "arm64", "vulkan", {"astc", "rgba8"}, "spirv-1.6", "high", {}};
    else if (target == "web-wasm32-webgpu")
        profile.variant = {"web", "wasm32", "webgpu", {"rgba8"}, "wgsl-1", "high", {}};
    else
        return Result<AssetCookProfile>::failure(Diagnostic::error(
            DiagnosticCode::Unsupported, "unknown asset Cook target", std::string(target), {}, "asset.cook"));
    return Result<AssetCookProfile>::success(std::move(profile));
}

Result<AssetCookReceipt> cookEvaToEvpack(const EvaArchive& source, const AssetCookProfile& profile,
                                         const EvaArchiveLimits& evaLimits, const EvpackLimits& evpackLimits) {
    auto migrated = migrateEvaArchive(source, evaLimits);
    if (!migrated) return Result<AssetCookReceipt>::failure(migrated.status());
    auto rebuilt = buildEvaArchive(migrated.value().manifest, migrated.value().entries, evaLimits);
    if (!rebuilt) return Result<AssetCookReceipt>::failure(rebuilt.status());
    auto admitted = parseEvaArchive(rebuilt.value(), evaLimits);
    if (!admitted) return Result<AssetCookReceipt>::failure(admitted.status());
    EvaArchive  canonical          = std::move(admitted).takeValue();
    EvaManifest dependencyManifest = canonical.manifest;
    std::erase_if(dependencyManifest.dependencies, [&](const EvaDependency& dependency) {
        return dependency.kind == EvaDependencyKind::Platform &&
               !platformDependencyApplies(dependency, profile.variant);
    });
    auto dependencyValidation = validateEvaDependencies(dependencyManifest, profile.availableDependencies);
    if (!dependencyValidation) return Result<AssetCookReceipt>::failure(dependencyValidation.status());
    auto canonicalManifest = serializeEvaManifest(canonical.manifest);
    if (!canonicalManifest) return Result<AssetCookReceipt>::failure(canonicalManifest.status());
    auto publication = validatePublication(canonical.manifest, profile.publication);
    if (!publication) return Result<AssetCookReceipt>::failure(publication.status());

    std::map<PersistentId, std::vector<PersistentId>> runtimeDependencies;
    for (const auto& dependency : canonical.manifest.dependencies) {
        if (dependency.kind == EvaDependencyKind::RuntimeOptional || dependency.kind == EvaDependencyKind::Build ||
            dependency.kind == EvaDependencyKind::Editor || dependency.kind == EvaDependencyKind::Source ||
            !platformDependencyApplies(dependency, profile.variant))
            continue;
        const bool present = std::binary_search(dependencyValidation.value().presentAssets.begin(),
                                                dependencyValidation.value().presentAssets.end(), dependency.to.id());
        if (present) runtimeDependencies[dependency.from.id()].push_back(dependency.to.id());
    }
    for (auto& [asset, dependencies] : runtimeDependencies) {
        (void)asset;
        std::sort(dependencies.begin(), dependencies.end());
        dependencies.erase(std::unique(dependencies.begin(), dependencies.end()), dependencies.end());
    }

    EvpackBuild build;
    build.packageId = canonical.manifest.packageId;
    build.buildId   = buildIdentity(canonical, profile, canonicalManifest.value());
    build.variants.push_back(profile.variant);
    for (const auto& asset : canonical.manifest.assets) {
        std::vector<EvaDependency> assetDependencyPolicies;
        for (const auto& dependency : canonical.manifest.dependencies)
            if (dependency.from.id() == asset.asset.id() && platformDependencyApplies(dependency, profile.variant))
                assetDependencyPolicies.push_back(dependency);
        const auto definition =
            std::lower_bound(canonical.entries.begin(), canonical.entries.end(), asset.definition,
                             [](const EvaArchiveEntry& entry, std::string_view path) { return entry.path < path; });
        if (definition == canonical.entries.end() || definition->path != asset.definition)
            return Result<AssetCookReceipt>::failure(Diagnostic::error(
                DiagnosticCode::NotFound, "asset definition is missing", asset.definition, {}, "asset.cook"));
        const auto dependencies = runtimeDependencies[asset.asset.id()];
        if (asset.type == "eve.shader") {
            if (profile.variant.graphics != "vulkan" || profile.variant.shaderFormat != "spirv-1.6")
                return Result<AssetCookReceipt>::failure(
                    Diagnostic::error(DiagnosticCode::Unsupported, "eve.shader/1 requires Vulkan SPIR-V 1.6 target",
                                      asset.definition, {}, "asset.cook"));
            auto value = Value::fromJson(
                std::string_view(reinterpret_cast<const char*>(definition->bytes.data()), definition->bytes.size()));
            if (!value) return Result<AssetCookReceipt>::failure(value.status());
            auto shader = decodeShaderAsset(value.value());
            if (!shader) return Result<AssetCookReceipt>::failure(shader.status());
            const std::string shaderPrefix = "assets/" + asset.asset.id().format() + "/";
            if (!assetDependencyPolicies.empty() ||
                std::any_of(canonical.entries.begin(), canonical.entries.end(), [&](const EvaArchiveEntry& entry) {
                    return entry.path.starts_with(shaderPrefix) && entry.path != asset.definition;
                }))
                return Result<AssetCookReceipt>::failure(Diagnostic::error(DiagnosticCode::Unsupported,
                                                                           "eve.shader/1 must be self-contained",
                                                                           asset.definition, {}, "asset.cook"));
        }
        if (asset.type == "eve.image") {
            if (std::find(profile.variant.textureFamilies.begin(), profile.variant.textureFamilies.end(), "rgba8") ==
                profile.variant.textureFamilies.end())
                return Result<AssetCookReceipt>::failure(Diagnostic::error(
                    DiagnosticCode::Unsupported, "eve.image/3 requires an explicit rgba8 target texture family",
                    asset.definition, {}, "asset.cook"));
            auto parsedDefinition = Value::fromJson(
                std::string_view(reinterpret_cast<const char*>(definition->bytes.data()), definition->bytes.size()));
            if (!parsedDefinition) return Result<AssetCookReceipt>::failure(parsedDefinition.status());
            const auto*  definitionObject = parsedDefinition.value().getIf<Value::Object>();
            const Value* blobValue        = definitionObject ? field(*definitionObject, "blob") : nullptr;
            if (!blobValue || !blobValue->isString())
                return Result<AssetCookReceipt>::failure(Diagnostic::error(DiagnosticCode::ParseError,
                                                                           "eve.image/3 source blob path is missing",
                                                                           asset.definition, {}, "asset.cook"));
            const auto sourceBlob =
                std::lower_bound(canonical.entries.begin(), canonical.entries.end(), blobValue->asString(),
                                 [](const EvaArchiveEntry& entry, std::string_view path) { return entry.path < path; });
            if (sourceBlob == canonical.entries.end() || sourceBlob->path != blobValue->asString())
                return Result<AssetCookReceipt>::failure(Diagnostic::error(DiagnosticCode::NotFound,
                                                                           "eve.image/3 source blob is missing",
                                                                           blobValue->asString(), {}, "asset.cook"));
            auto image = cookCanonicalImageRgba8(definition->bytes, sourceBlob->bytes, evpackLimits.maximumChunkBytes);
            if (!image) return Result<AssetCookReceipt>::failure(image.status());
            auto runtimeDefinition =
                binaryDefinition(image.value().definition, evpackLimits.maximumChunkBytes, assetDependencyPolicies);
            if (!runtimeDefinition) return Result<AssetCookReceipt>::failure(runtimeDefinition.status());
            build.chunks.push_back({asset.asset.id(), asset.type, asset.schemaVersion, 0, EvpackChunkKind::Definition,
                                    0, profile.chunkCodec, 8, dependencies, std::move(runtimeDefinition).takeValue()});
            build.chunks.push_back({asset.asset.id(), asset.type, asset.schemaVersion, 0, EvpackChunkKind::Bulk, 1,
                                    EvpackCodec::None, profile.bulkAlignment, dependencies,
                                    std::move(image).takeValue().bulk});
            continue;
        }
        if (asset.type == "eve.volume-texture") {
            if (std::find(profile.variant.textureFamilies.begin(), profile.variant.textureFamilies.end(), "rgba8") ==
                profile.variant.textureFamilies.end())
                return Result<AssetCookReceipt>::failure(
                    Diagnostic::error(DiagnosticCode::Unsupported,
                                      "eve.volume-texture/1 requires an explicit rgba8 target texture family",
                                      asset.definition, {}, "asset.cook"));
            auto parsedDefinition = Value::fromJson(
                std::string_view(reinterpret_cast<const char*>(definition->bytes.data()), definition->bytes.size()));
            if (!parsedDefinition) return Result<AssetCookReceipt>::failure(parsedDefinition.status());
            const auto*  definitionObject = parsedDefinition.value().getIf<Value::Object>();
            const Value* blobValue        = definitionObject ? field(*definitionObject, "blob") : nullptr;
            if (!blobValue || !blobValue->isString())
                return Result<AssetCookReceipt>::failure(
                    Diagnostic::error(DiagnosticCode::ParseError, "eve.volume-texture/1 source blob path is missing",
                                      asset.definition, {}, "asset.cook"));
            const auto sourceBlob =
                std::lower_bound(canonical.entries.begin(), canonical.entries.end(), blobValue->asString(),
                                 [](const EvaArchiveEntry& entry, std::string_view path) { return entry.path < path; });
            if (sourceBlob == canonical.entries.end() || sourceBlob->path != blobValue->asString())
                return Result<AssetCookReceipt>::failure(
                    Diagnostic::error(DiagnosticCode::NotFound, "eve.volume-texture/1 source blob is missing",
                                      blobValue->asString(), {}, "asset.cook"));
            auto volume =
                cookCanonicalVolumeTextureRgba8(definition->bytes, sourceBlob->bytes, evpackLimits.maximumChunkBytes);
            if (!volume) return Result<AssetCookReceipt>::failure(volume.status());
            auto runtimeDefinition =
                binaryDefinition(volume.value().definition, evpackLimits.maximumChunkBytes, assetDependencyPolicies);
            if (!runtimeDefinition) return Result<AssetCookReceipt>::failure(runtimeDefinition.status());
            build.chunks.push_back({asset.asset.id(), asset.type, asset.schemaVersion, 0, EvpackChunkKind::Definition,
                                    0, profile.chunkCodec, 8, dependencies, std::move(runtimeDefinition).takeValue()});
            build.chunks.push_back({asset.asset.id(), asset.type, asset.schemaVersion, 0, EvpackChunkKind::Bulk, 1,
                                    EvpackCodec::None, profile.bulkAlignment, dependencies,
                                    std::move(volume).takeValue().bulk});
            continue;
        }
        if (asset.type == "eve.pcg-graph") {
            auto pcg = cookCanonicalPcgGraph(definition->bytes, 4096, evpackLimits.maximumChunkBytes);
            if (!pcg) return Result<AssetCookReceipt>::failure(pcg.status());
            auto runtimeDefinition =
                binaryDefinition(pcg.value().definition, evpackLimits.maximumChunkBytes, assetDependencyPolicies);
            if (!runtimeDefinition) return Result<AssetCookReceipt>::failure(runtimeDefinition.status());
            build.chunks.push_back({asset.asset.id(), asset.type, asset.schemaVersion, 0, EvpackChunkKind::Definition,
                                    0, profile.chunkCodec, 8, dependencies, std::move(runtimeDefinition).takeValue()});
            build.chunks.push_back({asset.asset.id(), asset.type, asset.schemaVersion, 0, EvpackChunkKind::Bulk, 1,
                                    profile.chunkCodec, profile.bulkAlignment, dependencies,
                                    std::move(pcg).takeValue().executionPlan});
            continue;
        }
        auto runtimeDefinition =
            binaryDefinition(definition->bytes, evpackLimits.maximumChunkBytes, assetDependencyPolicies);
        if (!runtimeDefinition) return Result<AssetCookReceipt>::failure(runtimeDefinition.status());
        build.chunks.push_back({asset.asset.id(), asset.type, asset.schemaVersion, 0, EvpackChunkKind::Definition, 0,
                                profile.chunkCodec, 8, dependencies, std::move(runtimeDefinition).takeValue()});
        const std::string prefix  = "assets/" + asset.asset.id().format() + "/";
        std::uint32_t     chunkId = 1;
        for (const auto& entry : canonical.entries) {
            if (entry.path == asset.definition || !entry.path.starts_with(prefix)) continue;
            build.chunks.push_back({asset.asset.id(), asset.type, asset.schemaVersion, 0, kindForPath(entry.path),
                                    chunkId++, profile.chunkCodec, profile.bulkAlignment, dependencies, entry.bytes});
        }
    }
    const std::uint32_t chunkCount   = static_cast<std::uint32_t>(build.chunks.size());
    const PersistentId  buildId      = build.buildId;
    auto                packageBytes = buildEvpack(std::move(build), evpackLimits);
    if (!packageBytes) return Result<AssetCookReceipt>::failure(packageBytes.status());
    auto verified = parseEvpack(packageBytes.value(), evpackLimits);
    if (!verified) return Result<AssetCookReceipt>::failure(verified.status());
    return Result<AssetCookReceipt>::success(
        {canonical.manifest.packageId, buildId, chunkCount, std::move(packageBytes).takeValue()});
}

}  // namespace eve::asset

// AssetDependency.cpp
namespace eve::asset {
namespace {
bool requiresPresence(EvaDependencyKind kind) {
    return kind == EvaDependencyKind::RuntimeRequired || kind == EvaDependencyKind::Build ||
           kind == EvaDependencyKind::Platform;
}
bool validFallback(const EvaDependency& dependency) {
    if (dependency.kind != EvaDependencyKind::RuntimeOptional) return dependency.fallback.empty();
    const auto behavior   = dependency.fallback.find("behavior");
    const auto observable = dependency.fallback.find("observableCode");
    if (behavior == dependency.fallback.end() || !behavior->second.isString() ||
        observable == dependency.fallback.end() || !observable->second.isString() ||
        observable->second.asString().empty())
        return false;
    const std::string& name  = behavior->second.asString();
    const auto         asset = dependency.fallback.find("asset");
    if (name == "use-asset")
        return asset != dependency.fallback.end() && asset->second.isString() &&
               AssetRef::parse(asset->second.asString()).ok();
    return (name == "omit-feature" || name == "use-default") && asset == dependency.fallback.end();
}
}  // namespace

Result<EvaDependencyValidation> validateEvaDependencies(const EvaManifest&                        manifest,
                                                        std::span<const AvailableAssetDependency> available) {
    using TypeIdentity = std::pair<std::string, SchemaVersion>;
    std::map<PersistentId, TypeIdentity> types;
    std::set<PersistentId>               local;
    for (const auto& asset : manifest.assets) {
        types.emplace(asset.asset.id(), TypeIdentity{asset.type, asset.schemaVersion});
        local.emplace(asset.asset.id());
    }
    for (const auto& external : available) {
        if (external.asset.id().isNil() || external.type.empty() || external.schemaVersion.isZero())
            return Result<EvaDependencyValidation>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "external dependency fact is invalid", {}, {}, "asset.dependency"));
        const auto [found, inserted] =
            types.emplace(external.asset.id(), TypeIdentity{external.type, external.schemaVersion});
        if (!inserted && found->second != TypeIdentity{external.type, external.schemaVersion})
            return Result<EvaDependencyValidation>::failure(
                Diagnostic::error(DiagnosticCode::TypeMismatch, "dependency resolver reports conflicting types",
                                  external.asset.format(), {}, "asset.dependency"));
    }
    for (const auto& [name, entrypoint] : manifest.entrypoints)
        if (!local.contains(entrypoint.id()))
            return Result<EvaDependencyValidation>::failure(
                Diagnostic::error(DiagnosticCode::NotFound, "entrypoint does not resolve to a local asset", name, {},
                                  "asset.dependency"));

    using EdgeKey = std::tuple<PersistentId, PersistentId, EvaDependencyKind, std::string>;
    std::set<EdgeKey>                                 edges;
    std::map<PersistentId, std::vector<PersistentId>> graph;
    EvaDependencyValidation                           result;
    for (const auto& dependency : manifest.dependencies) {
        if (!validFallback(dependency))
            return Result<EvaDependencyValidation>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                                              "dependency fallback policy is invalid",
                                                                              dependency.path, {}, "asset.dependency"));
        if (!local.contains(dependency.from.id()))
            return Result<EvaDependencyValidation>::failure(
                Diagnostic::error(DiagnosticCode::NotFound, "dependency source is not locally authoritative",
                                  dependency.from.format(), {}, "asset.dependency"));
        if (!edges.emplace(dependency.from.id(), dependency.to.id(), dependency.kind, dependency.path).second)
            return Result<EvaDependencyValidation>::failure(Diagnostic::error(
                DiagnosticCode::Conflict, "duplicate dependency edge", dependency.path, {}, "asset.dependency"));
        const auto target = types.find(dependency.to.id());
        if (target == types.end()) {
            if (requiresPresence(dependency.kind))
                return Result<EvaDependencyValidation>::failure(
                    Diagnostic::error(DiagnosticCode::NotFound, "required dependency is unavailable",
                                      dependency.to.format(), {}, "asset.dependency"));
            result.omittedOptionalAssets.push_back(dependency.to.id());
            continue;
        }
        const std::string resolvedType = target->second.first + "/" + std::to_string(target->second.second.value());
        if (!dependency.expectedType.empty() && resolvedType != dependency.expectedType)
            return Result<EvaDependencyValidation>::failure(
                Diagnostic::error(DiagnosticCode::TypeMismatch, "dependency resolved to the wrong asset type",
                                  dependency.path, {}, "asset.dependency"));
        result.presentAssets.push_back(dependency.to.id());
        if (dependency.kind == EvaDependencyKind::RuntimeRequired && local.contains(dependency.to.id()))
            graph[dependency.from.id()].push_back(dependency.to.id());
    }

    enum class Mark : std::uint8_t { Visiting, Complete };
    std::map<PersistentId, Mark>                     marks;
    std::function<Result<void>(const PersistentId&)> visit = [&](const PersistentId& node) -> Result<void> {
        const auto marked = marks.find(node);
        if (marked != marks.end()) {
            if (marked->second == Mark::Visiting)
                return Result<void>::failure(Diagnostic::error(DiagnosticCode::Conflict,
                                                               "runtime-required dependency cycle detected",
                                                               node.format(), {}, "asset.dependency"));
            return Result<void>::success();
        }
        marks.emplace(node, Mark::Visiting);
        auto& targets = graph[node];
        std::sort(targets.begin(), targets.end());
        for (const auto& target : targets) {
            auto nested = visit(target);
            if (!nested) return nested;
        }
        marks[node] = Mark::Complete;
        return Result<void>::success();
    };
    for (const auto& id : local) {
        auto visited = visit(id);
        if (!visited) return Result<EvaDependencyValidation>::failure(visited.status());
    }
    std::sort(result.presentAssets.begin(), result.presentAssets.end());
    result.presentAssets.erase(std::unique(result.presentAssets.begin(), result.presentAssets.end()),
                               result.presentAssets.end());
    std::sort(result.omittedOptionalAssets.begin(), result.omittedOptionalAssets.end());
    result.omittedOptionalAssets.erase(
        std::unique(result.omittedOptionalAssets.begin(), result.omittedOptionalAssets.end()),
        result.omittedOptionalAssets.end());
    return Result<EvaDependencyValidation>::success(std::move(result));
}
}  // namespace eve::asset

// AssetDiff.cpp
namespace eve::asset {
namespace {
using DependencyKey =
    std::tuple<PersistentId, PersistentId, EvaDependencyKind, std::string, std::string, std::string, std::string>;
Result<std::set<DependencyKey>> dependencyKeys(const EvaManifest& manifest) {
    std::set<DependencyKey> result;
    for (const auto& value : manifest.dependencies) {
        auto predicate = Value(value.predicate).toJson();
        if (!predicate) return Result<std::set<DependencyKey>>::failure(predicate.status());
        auto fallback = Value(value.fallback).toJson();
        if (!fallback) return Result<std::set<DependencyKey>>::failure(fallback.status());
        result.emplace(value.from.id(), value.to.id(), value.kind, value.path, value.expectedType,
                       std::move(predicate).takeValue(), std::move(fallback).takeValue());
    }
    return Result<std::set<DependencyKey>>::success(std::move(result));
}
}  // namespace

Result<EvaPackageDiff> diffEvaManifests(const EvaManifest& before, const EvaManifest& after) {
    std::map<PersistentId, const EvaAssetEntry*> left, right;
    for (const auto& asset : before.assets) left.emplace(asset.asset.id(), &asset);
    for (const auto& asset : after.assets) right.emplace(asset.asset.id(), &asset);
    EvaPackageDiff result;
    for (const auto& [id, asset] : left) {
        const auto found = right.find(id);
        if (found == right.end()) {
            result.assets.push_back(
                {id, EvaAssetChangeKind::Removed, asset->type, asset->schemaVersion, asset->contentHash, {}, {}, {}});
        } else if (asset->type != found->second->type || asset->schemaVersion != found->second->schemaVersion ||
                   asset->contentHash != found->second->contentHash) {
            result.assets.push_back({id, EvaAssetChangeKind::Changed, asset->type, asset->schemaVersion,
                                     asset->contentHash, found->second->type, found->second->schemaVersion,
                                     found->second->contentHash});
        }
    }
    for (const auto& [id, asset] : right)
        if (!left.contains(id))
            result.assets.push_back(
                {id, EvaAssetChangeKind::Added, {}, {}, {}, asset->type, asset->schemaVersion, asset->contentHash});
    std::sort(result.assets.begin(), result.assets.end(),
              [](const auto& a, const auto& b) { return a.assetId < b.assetId; });
    const auto leftDependencies = dependencyKeys(before);
    if (!leftDependencies) return Result<EvaPackageDiff>::failure(leftDependencies.status());
    const auto rightDependencies = dependencyKeys(after);
    if (!rightDependencies) return Result<EvaPackageDiff>::failure(rightDependencies.status());
    std::vector<DependencyKey> added, removed;
    std::set_difference(rightDependencies.value().begin(), rightDependencies.value().end(),
                        leftDependencies.value().begin(), leftDependencies.value().end(), std::back_inserter(added));
    std::set_difference(leftDependencies.value().begin(), leftDependencies.value().end(),
                        rightDependencies.value().begin(), rightDependencies.value().end(),
                        std::back_inserter(removed));
    result.addedDependencies   = static_cast<std::uint32_t>(added.size());
    result.removedDependencies = static_cast<std::uint32_t>(removed.size());
    result.entrypointsChanged  = before.entrypoints != after.entrypoints;
    return Result<EvaPackageDiff>::success(std::move(result));
}
}  // namespace eve::asset
